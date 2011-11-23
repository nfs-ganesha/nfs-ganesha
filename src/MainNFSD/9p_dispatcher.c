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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
#include <poll.h>
#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"
#include "9p.h"

#ifndef _USE_TIRPC_IPV6
  #define P_FAMILY AF_INET
#else
  #define P_FAMILY AF_INET6
#endif

void DispatchWork9P( request_data_t *preq, unsigned int worker_index)
{
  LRU_entry_t *pentry = NULL;
  LRU_status_t status;

  LogDebug(COMPONENT_DISPATCH,
           "Awaking Worker Thread #%u for 9P request %p, tcpsock=%lu",
           worker_index, preq, preq->rcontent._9p.pconn->sockfd);

  P(workers_data[worker_index].wcb.tcb_mutex);
  P(workers_data[worker_index].request_pool_mutex);

  pentry = LRU_new_entry(workers_data[worker_index].pending_request, &status);

  if(pentry == NULL)
    {
      V(workers_data[worker_index].request_pool_mutex);
      V(workers_data[worker_index].wcb.tcb_mutex);
      LogMajor(COMPONENT_DISPATCH,
               "Error while inserting 9P pending request to Worker Thread #%u... Exiting",
               worker_index);
      Fatal();
    }

  pentry->buffdata.pdata = (caddr_t) preq;
  pentry->buffdata.len = sizeof(*preq);

  if(pthread_cond_signal(&(workers_data[worker_index].wcb.tcb_condvar)) == -1)
    {
      V(workers_data[worker_index].request_pool_mutex);
      V(workers_data[worker_index].wcb.tcb_mutex);
      LogMajor(COMPONENT_THREAD,
               "Error %d (%s) while signalling Worker Thread #%u... Exiting",
               errno, strerror(errno), worker_index);
      Fatal();
    }
  V(workers_data[worker_index].request_pool_mutex);
  V(workers_data[worker_index].wcb.tcb_mutex);
}


/**
 * Selects the smallest request queue,
 * whome the worker is ready and is not garbagging.
 */
static unsigned int select_worker_queue()
{
  #define NO_VALUE_CHOOSEN  1000000
  unsigned int worker_index = NO_VALUE_CHOOSEN;
  unsigned int avg_number_pending = NO_VALUE_CHOOSEN;
  unsigned int total_number_pending = 0;

  static unsigned int counter;

  unsigned int i;
  static unsigned int last;
  unsigned int cpt = 0;
  worker_available_rc rc;

  counter++;

  /* Calculate the average queue length if counter is bigger than configured value. */
  if(counter > nfs_param.core_param.nb_call_before_queue_avg)
    {
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        {
          total_number_pending += workers_data[i].pending_request->nb_entry;
        }
      avg_number_pending = total_number_pending / nfs_param.core_param.nb_worker;
      /* Reset counter. */
      counter = 0;
    }

  /* Choose the queue whose length is smaller than average. */
      for(i = (last + 1) % nfs_param.core_param.nb_worker, cpt = 0;
          cpt < nfs_param.core_param.nb_worker;
          cpt++, i = (i + 1) % nfs_param.core_param.nb_worker)
        {
          /* Choose only fully initialized workers and that does not gc. */
          rc = worker_available(i, avg_number_pending);
          if(rc == WORKER_AVAILABLE)
            {
              worker_index = i;
              break;
            }
          else if(rc == WORKER_ALL_PAUSED)
            {
              /* Wait for the threads to awaken */
              rc = wait_for_workers_to_awaken();
              /*              if(rc == PAUSE_EXIT)
                {
                }
              */
            }
          else if(rc == WORKER_EXIT)
            {
            } 
        }

  if(worker_index == NO_VALUE_CHOOSEN)
    worker_index = (last + 1) % nfs_param.core_param.nb_worker;

  last = worker_index;

  return worker_index;

}                               /* select_worker_queue */

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
  unsigned int worker_index;

  char * _9pmsg ;
  uint32_t * p_9pmsglen = NULL ;

  _9p_conn_t _9p_conn ;

  int readlen = 0  ;

  printf( "Je gère la socket %ld\n", tcp_sock );

  snprintf(my_name, MAXNAMLEN, "9p_sock_mgr#fd=%ld", tcp_sock);
  SetNameFunction(my_name);

  /* Init the _9p_conn_t structure */
  _9p_conn.sockfd = tcp_sock ;
 
  if( gettimeofday( &_9p_conn.birth, NULL ) == -1 )
   LogFatal( COMPONENT_9P, "Can get connection's time of birth" ) ;

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_tcp_mgr)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      #ifdef _DEBUG_MEMLEAKS
      {
        FILE *output = fopen("/tmp/buddymem", "w");
        if (output != NULL)
          BuddyDumpAll(output);
      }
      #endif
      LogFatal(COMPONENT_DISPATCH, "Memory manager could not be initialized");
    }
#endif

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
                  close( tcp_sock );
        return NULL ;
      }

     if( fds[0].revents & (POLLERR|POLLHUP|POLLRDHUP) )
      {
        LogEvent( COMPONENT_9P, "Client %s on socket %lu has shut down and closed", strcaller, tcp_sock ) ;
                  close( tcp_sock );
        return NULL ;
      }

     if( fds[0].revents & (POLLIN|POLLRDNORM) )
      {
        /* choose a worker depending on its queue length */
        worker_index = select_worker_queue();

        /* Get a preq from the worker's pool */
        P(workers_data[worker_index].request_pool_mutex);

        GetFromPool(preq, &workers_data[worker_index].request_pool,
                    request_data_t);

        V(workers_data[worker_index].request_pool_mutex);

        /* Prepare to read the message */
        preq->rtype = _9P_REQUEST ;
        _9pmsg = preq->rcontent._9p._9pmsg ;
        preq->rcontent._9p.pconn = &_9p_conn ;

        /* An incoming 9P request: the msg has a 4 bytes header showing the size of the msg including the header */
        if( (readlen = recv( fds[0].fd, _9pmsg ,_9P_HDR_SIZE, 0) == _9P_HDR_SIZE ) )
         {
	    p_9pmsglen = (uint32_t *)_9pmsg ;

            LogFullDebug( COMPONENT_9P,
                          "Received message of size %u from client %s on socket %lu",
			   *p_9pmsglen, strcaller, tcp_sock ) ;

	     if( ( *p_9pmsglen < _9P_HDR_SIZE ) ||
		 ( readlen = recv( fds[0].fd,
				   (char *)(_9pmsg + _9P_HDR_SIZE),  
                                    *p_9pmsglen - _9P_HDR_SIZE, 0 ) ) !=  *p_9pmsglen - _9P_HDR_SIZE )
                 
             {
		LogEvent( COMPONENT_9P, 
			  "Badly formed 9P message: Header is too small for client %s on socket %lu", 
                          strcaller, tcp_sock ) ;

                /* Release the entry */
                P(workers_data[worker_index].request_pool_mutex);
                ReleaseToPool(preq, &workers_data[worker_index].request_pool);
  		workers_data[worker_index].passcounter += 1;
                V(workers_data[worker_index].request_pool_mutex);

                continue ;
             }
	    else
             {
		/* Message os OK push it the request to the right worker */
                DispatchWork9P(preq, worker_index);
             }
         }
        else if( readlen == 0 )
         {
           LogEvent( COMPONENT_9P, "Client %s on socket %lu has shut down", strcaller, tcp_sock ) ;
           close( tcp_sock );
           return NULL ;
         }
        else
         {
	   LogEvent( COMPONENT_9P, "Badly formed 9P header for client %s on socket %lu", strcaller, tcp_sock ) ;
           continue ;
         }
      }
   } /* for( ;; ) */
 
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
  struct sockaddr_in sinaddr;
#ifdef _USE_TIRPC_IPV6
  struct sockaddr_in6 sinaddr_tcp6;
  struct netbuf netbuf_tcp6;
  struct t_bind bindaddr_tcp6;
  struct __rpc_sockinfo si_tcp6;
#endif

  if( ( sock= socket(P_FAMILY, SOCK_STREAM, IPPROTO_TCP) ) == -1 )
    {
          LogFatal(COMPONENT_9P_DISPATCH,
                   "Cannot allocate a tcp socket for 9p, error %d (%s)", errno, strerror(errno));
	  return -1 ;
    }

  if(setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
	LogFatal(COMPONENT_9P_DISPATCH,
                 "Bad tcp socket options for 9p, error %d (%s)", errno, strerror(errno));
        return -1 ;
    }

  socket_setoptions(sock);

#ifndef _USE_TIRPC_IPV6
  memset( &sinaddr, 0, sizeof(sinaddr));
  sinaddr.sin_family      = AF_INET;
  sinaddr.sin_addr.s_addr = nfs_param.core_param.bind_addr.sin_addr.s_addr;
  sinaddr.sin_port        = htons(nfs_param._9p_param._9p_port);

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
  sinaddr_tcp6.sin6_port   = htons(nfs_param.core_param._9p_port);

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

#ifdef _DEBUG_MEMLEAKS
  static int nb_iter_memleaks = 0;
#endif

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
  while(TRUE)
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

#ifdef _DEBUG_MEMLEAKS
      if(nb_iter_memleaks > 1000)
        {
          nb_iter_memleaks = 0;
          nfs_debug_buddy_info();
        }
      else
        nb_iter_memleaks += 1;
#endif

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

#ifndef _NO_BUDDY_SYSTEM
  /* Initialisation of the Buddy Malloc */
  LogInfo(COMPONENT_9P_DISPATCH,
          "Initialization of memory manager");
  if(BuddyInit(&nfs_param.buddy_param_worker) != BUDDY_SUCCESS)
    LogFatal(COMPONENT_DISPATCH,
             "Memory manager could not be initialized");
#endif
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

