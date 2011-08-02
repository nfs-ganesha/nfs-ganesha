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
 * \file    ninep_dispatcher.c
 * \date    $Date: 2006/02/23 12:33:05 $
 * \brief   The file that contain the 'ninep_dispatcher_thread' routine for ganesha.
 *
 * ninep_dispatcher.c : The file that contain the 'ninep_dispatcher_thread' routine for ganesha (and all
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

/**
 * ninep_socket_thread: 9p socket manager.
 *
 * This function is the main loop for the 9p socket manager. One such thread exists per connection.
 *
 * @param Arg the socket number cast as a void * in pthread_create
 *
 * @return NULL 
 *
 */

typedef union sockarg__
{
   void * arg;
   int sock ;
} sockarg_t ;

void * ninep_socket_thread( void * Arg )
{
  sockarg_t sockarg ;
 
  sockarg.arg = Arg ;
 
  printf( "Je gère la socket %d\n", sockarg.sock );
  
  while( 1 ) 
   {
      sleep( 10 ) ;
   }
 
  return NULL ;
} /* ninep_socket_thread */

/**
 * ninep_create_socket: create the accept socket for 9P 
 *
 * This function create the accept socket for the 9p dispatcher thread.
 *
 * @param (none)
 *
 * @return socket fd or -1 if failed.
 *
 */
int ninep_create_socket( void )
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
  sinaddr.sin_port        = htons(nfs_param.core_param.ninep_port);

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
  sinaddr_tcp6.sin6_port   = htons(nfs_param.core_param.ninep_port);

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
} /* ninep_create_socket */

/**
 * ninep_dispatcher_svc_run: main loop for 9p dispatcher
 *
 * This function is the main loop for the 9p dispatcher. It never returns because it is an infinite loop.
 *
 * @param sock accept socket for 9p dispatch
 *
 * @return nothing (void function). 
 *
 */
void ninep_dispatcher_svc_run( int sock )
{
  int rc = 0;
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof( addr ) ;
  int newsock = -1 ;
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
      if( ( rc = pthread_create( &tcp_thrid, &attr_thr, ninep_socket_thread, (void *)sock ) ) != 0 )
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
}                               /* ninep_dispatcher_svc_run */ 


/**
 * ninep_dispatcher_thread: thread used for RPC dispatching.
 *
 * Thead used for RPC dispatching. It gets the requests and then spool it to one of the worker's LRU.
 * The worker chosen is the one with the smaller load (its LRU is the shorter one).
 *
 * @param Arg (unused)
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *ninep_dispatcher_thread(void *Arg)
{
  int ninep_socket = -1 ;

  SetNameFunction("ninep_dispatch_thr");

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

  /* Set up the ninep_socket */
  if( ( ninep_socket =  ninep_create_socket() ) == -1 )
   {
     LogCrit( COMPONENT_9P_DISPATCH,
              "Can't get socket for 9p dispatcher" ) ;
     exit( 1 ) ;
   }

  ninep_dispatcher_svc_run( ninep_socket );

  return NULL;
}                               /* ninep_dispatcher_thread */

