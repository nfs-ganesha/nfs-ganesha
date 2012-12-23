/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_dispatcher.c
 * \date    $Date: 2006/02/23 12:33:05 $
 * \brief   The file that contain the '_9p_dispatcher_thread' routine for ganesha.
 *
 * 9p_dispatcher.c : The file that contain the '_9p_dispatcher_thread' routine for ganesha (and all
 * the related stuff).
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <sys/select.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include "HashTable.h"
#include "log.h"
#include "abstract_mem.h"
#include "abstract_atomic.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "9p.h"
#include <stdbool.h>

#ifndef _USE_TIRPC_IPV6
  #define P_FAMILY AF_INET
#else
  #define P_FAMILY AF_INET6
#endif

void DispatchWork9P( request_data_t *preq )
{
  switch( preq->rtype )
   {
	case _9P_REQUEST:
          switch( preq->r_u._9p.pconn->trans_type )
           {
	      case _9P_TCP:
	        LogDebug(COMPONENT_DISPATCH,
        	         "Dispatching 9P/TCP request %p, tcpsock=%lu",
           	         preq, preq->r_u._9p.pconn->trans_data.sockfd);
		break ;

              case _9P_RDMA:
	        LogDebug(COMPONENT_DISPATCH,
        	         "Dispatching 9P/RDMA request %p",
           	         preq);
	        break ;
                 
              default:
		LogCrit( COMPONENT_DISPATCH, "/!\\ Implementation error, bad 9P transport type" ) ;
		return ;
		break ;
           }
	  break ;

	default:
	  LogCrit( COMPONENT_DISPATCH,
		   "/!\\ Implementation error, 9P Dispatch function is called for non-9P request !!!!");
	  return ;
	  break ;
   }

  /* increase connection refcount */
  atomic_inc_uint32_t(&preq->r_u._9p.pconn->refcount);

  /* new-style dispatch */
  nfs_rpc_enqueue_req(preq);
}


/**
 * _9p_socket_thread: 9p socket manager.
 *
 * This function is the main loop for the 9p socket manager. One such thread exists per connection.
 *
 * @param Arg the socket number cast as a void * in pthread_create
 *
 * @return NULL 
 *
 */


void * _9p_socket_thread( void * Arg )
{
  long int tcp_sock = (long int)Arg;
  int rc = -1 ;
  struct pollfd fds[1] ;
  int fdcount = 1 ;
  static char my_name[MAXNAMLEN];
  struct sockaddr_in addrpeer ;
  socklen_t addrpeerlen = sizeof( addrpeer ) ;
  char strcaller[MAXNAMLEN] ;
  request_data_t *preq = NULL;
  int tag;
  unsigned long sequence = 0;
  unsigned int i = 0 ;
  char * _9pmsg  = NULL;
  uint32_t * p_9pmsglen = NULL ;

  _9p_conn_t _9p_conn ;

  int readlen = 0  ;
  int total_readlen = 0  ;

  snprintf(my_name, MAXNAMLEN, "9p_sock_mgr#fd=%ld", tcp_sock);
  SetNameFunction(my_name);

  /* Init the _9p_conn_t structure */
  _9p_conn.trans_type = _9P_TCP ;
  _9p_conn.trans_data.sockfd = tcp_sock ;
  for (i = 0; i < FLUSH_BUCKETS; i++) {
          pthread_mutex_init(&_9p_conn.flush_buckets[i].lock, NULL);
          init_glist(&_9p_conn.flush_buckets[i].list);
  }
  atomic_store_uint32_t(&_9p_conn.refcount, 0);


  if( gettimeofday( &_9p_conn.birth, NULL ) == -1 )
   LogFatal( COMPONENT_9P, "Cannot get connection's time of birth" ) ;

  if( ( rc =  getpeername( tcp_sock, (struct sockaddr *)&addrpeer, &addrpeerlen) ) == -1 )
   {
      LogMajor(COMPONENT_9P,
               "Cannot get peername to tcp socket for 9p, error %d (%s)", errno, strerror(errno));
      strncpy( strcaller, "(unresolved)", MAXNAMLEN ) ;
   }
  else
   {
     snprintf(strcaller, MAXNAMLEN, "0x%x=%d.%d.%d.%d",
              ntohl(addrpeer.sin_addr.s_addr),
             (ntohl(addrpeer.sin_addr.s_addr) & 0xFF000000) >> 24,
             (ntohl(addrpeer.sin_addr.s_addr) & 0x00FF0000) >> 16,
             (ntohl(addrpeer.sin_addr.s_addr) & 0x0000FF00) >> 8,
             (ntohl(addrpeer.sin_addr.s_addr) & 0x000000FF));

     LogEvent( COMPONENT_9P, "9p socket #%ld is connected to %s", tcp_sock, strcaller ) ; 
   }

  /* Set up the structure used by poll */
  memset( (char *)fds, 0, sizeof( struct pollfd ) ) ;
  fds[0].fd = tcp_sock ;
  fds[0].events = POLLIN|POLLPRI| POLLRDBAND|POLLRDNORM|POLLRDHUP|POLLHUP|POLLERR|POLLNVAL;


  for( ;; ) /* Infinite loop */
   {
    
     if( ( rc = poll( fds, fdcount, -1 ) ) == -1 ) /* timeout = -1 =>Wait indefinitely for incoming events */
      {
         /* Interruption if not an issue */
         if( errno == EINTR )
           continue ;

         LogCrit( COMPONENT_9P,
                  "Got error %u (%s) on fd %ld connect to %s while polling on socket", 
                  errno, strerror( errno ), tcp_sock, strcaller ) ;
      }

     if( fds[0].revents & POLLNVAL )
      {
        LogEvent( COMPONENT_9P, "Client %s on socket %lu produced POLLNVAL", strcaller, tcp_sock ) ;
        goto end;
      }

     if( fds[0].revents & (POLLERR|POLLHUP|POLLRDHUP) )
      {
        LogEvent( COMPONENT_9P, "Client %s on socket %lu has shut down and closed", strcaller, tcp_sock ) ;
        goto end;
      }

     if( fds[0].revents & (POLLIN|POLLRDNORM) )
      {
        /* Prepare to read the message */
        if( ( _9pmsg = gsh_malloc( _9P_MSG_SIZE ) ) == NULL )
         {
            LogCrit( COMPONENT_9P, "Could not allocate 9pmsg buffer for client %s on socket %lu", strcaller, tcp_sock ) ;
            goto end;
         }

        /* An incoming 9P request: the msg has a 4 bytes header
           showing the size of the msg including the header */
        if( (readlen = recv( fds[0].fd, _9pmsg ,_9P_HDR_SIZE, MSG_WAITALL)) == _9P_HDR_SIZE ) 
         {
            p_9pmsglen = (uint32_t *)_9pmsg ;

            LogFullDebug( COMPONENT_9P,
                          "Received 9P/TCP message of size %u from client %s on socket %lu",
                          *p_9pmsglen, strcaller, tcp_sock ) ;

            total_readlen = readlen;
            while( total_readlen < *p_9pmsglen )
             {
		 readlen = recv( fds[0].fd,
				 _9pmsg + total_readlen,  
                                 *p_9pmsglen - total_readlen, 0 ) ;
               
                 /* Signal management */
                 if( readlen < 0 && errno == EINTR )
                    continue ;

                 /* Error management */
                 if( readlen < 0 )  
                  {
	            LogEvent( COMPONENT_9P, 
                              "Read error client %s on socket %lu errno=%d", 
                              strcaller, tcp_sock, errno ) ;
                    goto end;
                  }

                 /* Client quit */
                 if( readlen == 0 )  
                  {
	            LogEvent( COMPONENT_9P,
                              "Client %s closed on socket %lu",
                              strcaller, tcp_sock) ;
                    goto end;
                  }
                 
                 /* After this point, read() is supposed to be OK */
                 total_readlen += readlen ;
             } /* while */
             
             /* Message is good. */
             preq = pool_alloc( request_pool, NULL ) ;
 
             preq->rtype = _9P_REQUEST ;
             preq->r_u._9p._9pmsg = _9pmsg;
             preq->r_u._9p.pconn = &_9p_conn ;
             
             /* Add this request to the request list, should it be flushed later. */
             tag = *(u16*) (_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE);
             _9p_AddFlushHook(&preq->r_u._9p, tag, sequence++);
             LogFullDebug( COMPONENT_9P, "Request tag is %d\n", tag);

	     /* Message was OK push it */
             DispatchWork9P(preq);

             /* We are not responsible for this buffer anymore: */
             _9pmsg = NULL;
         }
        else /* readlen != _9P_HDR_SIZE */
         {
           if (readlen == 0)
             LogEvent( COMPONENT_9P, "Client %s on socket %lu has shut down", strcaller, tcp_sock ) ;
           else
	     LogEvent( COMPONENT_9P, 
	               "Badly formed 9P/TCP message: Header is too small for client %s on socket %lu: readlen=%u expected=%u", 
                       strcaller, tcp_sock, readlen, _9P_HDR_SIZE ) ;
           
           /* Either way, we close the connection. It is not possible to survive
            * once we get out of sync in the TCP stream with the client
            */ 
           goto end;
         }
      } /* if( fds[0].revents & (POLLIN|POLLRDNORM) ) */
   } /* for( ;; ) */
 

end:
  LogEvent( COMPONENT_9P, "Closing connection on socket %lu", tcp_sock ) ;
  close( tcp_sock ) ;

  /* Free buffer if we encountered an error before we could give it to a worker */
  if (_9pmsg)
           gsh_free( _9pmsg ) ;

  while(atomic_fetch_uint32_t(&_9p_conn.refcount)) {
           LogEvent( COMPONENT_9P, "Waiting for workers to release pconn") ;
           sleep(1);
  }

  return NULL ;
} /* _9p_socket_thread */

/**
 * _9p_create_socket: create the accept socket for 9P 
 *
 * This function create the accept socket for the 9p dispatcher thread.
 *
 * @param (none)
 *
 * @return socket fd or -1 if failed.
 *
 */
int _9p_create_socket( void )
{
  int sock = -1 ;
  int one = 1 ;
  int centvingt = 120 ;
  int neuf = 9 ;
  struct sockaddr_in sinaddr;
#ifdef _USE_TIRPC_IPV6
  struct sockaddr_in6 sinaddr_tcp6;
  struct netbuf netbuf_tcp6;
  struct t_bind bindaddr_tcp6;
  struct __rpc_sockinfo si_tcp6;
#endif
  int bad = 1 ;
  
  if( ( sock= socket(P_FAMILY, SOCK_STREAM, IPPROTO_TCP) ) != -1 )
   if(!setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    if(!setsockopt( sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)))
     if(!setsockopt( sock, IPPROTO_TCP, TCP_KEEPIDLE, &centvingt, sizeof(centvingt)))
      if(!setsockopt( sock, IPPROTO_TCP, TCP_KEEPINTVL, &centvingt, sizeof(centvingt)))
       if(!setsockopt( sock, IPPROTO_TCP, TCP_KEEPCNT, &neuf, sizeof(neuf)))
        bad = 0 ;

   if( bad ) 
    {
	LogFatal(COMPONENT_9P_DISPATCH,
                 "Bad socket option 9p, error %d (%s)", errno, strerror(errno));
        return -1 ;
    }
  socket_setoptions( sock ) ;

#ifndef _USE_TIRPC_IPV6
  memset( &sinaddr, 0, sizeof(sinaddr));
  sinaddr.sin_family      = AF_INET;
  sinaddr.sin_addr.s_addr = nfs_param.core_param.bind_addr.sin_addr.s_addr;
  sinaddr.sin_port        = htons(nfs_param._9p_param._9p_tcp_port);

  if(bind(sock, (struct sockaddr *)&sinaddr, sizeof(sinaddr)) == -1)
   {
     LogFatal(COMPONENT_9P_DISPATCH,
              "Cannot bind 9p tcp socket, error %d (%s)",
              errno, strerror(errno));
     return -1 ;
   }

  if( listen( sock, 20 ) == -1 )
   {
       LogFatal(COMPONENT_DISPATCH,
                "Cannot bind 9p socket, error %d (%s)",
                errno, strerror(errno));
       return -1 ;
   }
#else
  memset(&sinaddr_tcp6, 0, sizeof(sinaddr_tcp6));
  sinaddr_tcp6.sin6_family = AF_INET6;
  sinaddr_tcp6.sin6_addr   = in6addr_any;     /* All the interfaces on the machine are used */
  sinaddr_tcp6.sin6_port   = htons(nfs_param.core_param._9p_tcp_port);

  netbuf_tcp6.maxlen = sizeof(sinaddr_tcp6);
  netbuf_tcp6.len    = sizeof(sinaddr_tcp6);
  netbuf_tcp6.buf    = &sinaddr_tcp6;

  bindaddr_tcp6.qlen = SOMAXCONN;
  bindaddr_tcp6.addr = netbuf_tcp6;

  if(!__rpc_fd2sockinfo(sock, &si_tcp6))
   {
     LogFatal(COMPONENT_DISPATCH,
              "Cannot get 9p socket info for tcp6 socket rc=%d errno=%d (%s)",
              rc, errno, strerror(errno));
     return -1 ;
   }

  if(bind( sock,
          (struct sockaddr *)bindaddr_tcp6.addr.buf,
          (socklen_t) si_nfs_tcp6.si_alen) == -1)
   {
       LogFatal(COMPONENT_DISPATCH,
                "Cannot bind 9p tcp6 socket, error %d (%s)",
                errno, strerror(errno));
       return -1 ;
   }

  if( listen( sock, 20 ) == -1 )
   {
       LogFatal(COMPONENT_DISPATCH,
                "Cannot bind 9p tcp6 socket, error %d (%s)",
                errno, strerror(errno));
       return -1 ;
   }

#endif

  return sock ;
} /* _9p_create_socket */

/**
 * _9p_dispatcher_svc_run: main loop for 9p dispatcher
 *
 * This function is the main loop for the 9p dispatcher. It never returns because it is an infinite loop.
 *
 * @param sock accept socket for 9p dispatch
 *
 * @return nothing (void function). 
 *
 */
void _9p_dispatcher_svc_run( long int sock )
{
  int rc = 0;
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof( addr ) ;
  long int newsock = -1 ;
  pthread_attr_t attr_thr;
  pthread_t tcp_thrid ;

  /* Init for thread parameter (mostly for scheduling) */
  if(pthread_attr_init(&attr_thr) != 0)
    LogDebug(COMPONENT_9P_DISPATCH, "can't init pthread's attributes");

  if(pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
    LogDebug(COMPONENT_9P_DISPATCH, "can't set pthread's scope");

  if(pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE) != 0)
    LogDebug(COMPONENT_9P_DISPATCH, "can't set pthread's join state");

  if(pthread_attr_setstacksize(&attr_thr, THREAD_STACK_SIZE) != 0)
    LogDebug(COMPONENT_9P_DISPATCH, "can't set pthread's stack size");


  LogEvent( COMPONENT_9P_DISPATCH, "9P dispatcher started" ) ;
  while(true)
    {
      if( ( newsock = accept( sock, (struct sockaddr *)&addr, &addrlen ) ) < 0 )
       {
	 LogCrit( COMPONENT_9P_DISPATCH, "accept failed" ) ;
	 continue ; 
       }

      /* Starting the thread dedicated to signal handling */
      if( ( rc = pthread_create( &tcp_thrid, &attr_thr, _9p_socket_thread, (void *)newsock ) ) != 0 )
       {
         LogFatal(COMPONENT_THREAD,
                  "Could not create 9p socket manager thread, error = %d (%s)",
                  errno, strerror(errno));
       }
    }                           /* while */
  return;
}                               /* _9p_dispatcher_svc_run */ 


/**
 * _9p_dispatcher_thread: thread used for RPC dispatching.
 *
 * Thead used for RPC dispatching. It gets the requests and then spool it to one of the worker's LRU.
 * The worker chosen is the one with the smaller load (its LRU is the shorter one).
 *
 * @param Arg (unused)
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void * _9p_dispatcher_thread(void *Arg)
{
  int _9p_socket = -1 ;

  SetNameFunction("_9p_dispatch_thr");

  /* Calling dispatcher main loop */
  LogInfo(COMPONENT_9P_DISPATCH,
          "Entering nfs/rpc dispatcher");

  LogDebug(COMPONENT_9P_DISPATCH,
           "My pthread id is %p", (caddr_t) pthread_self());

  /* Set up the _9p_socket */
  if( ( _9p_socket =  _9p_create_socket() ) == -1 )
   {
     LogCrit( COMPONENT_9P_DISPATCH,
              "Can't get socket for 9p dispatcher" ) ;
     exit( 1 ) ;
   }

  _9p_dispatcher_svc_run( _9p_socket );

  return NULL;
}                               /* _9p_dispatcher_thread */

