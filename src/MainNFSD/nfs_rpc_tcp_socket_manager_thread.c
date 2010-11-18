/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * \file    nfs_rpc_tcp_socket_manager_thread.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/02/23 12:33:05 $
 * \version $Revision: 1.96 $
 * \brief   The file that contain the 'rpc_tcp_socket_manager_thread' routine for the nfsd.
 *
 * nfs_rpc_dispatcher.c : The file that contain the 'rpc_tcp_socket_manager_thread.c' routine for the nfsd (and all
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

#if defined( _USE_TIRPC )
#include <rpc/rpc.h>
#elif defined( _USE_GSSRPC )
#include <gssapi/gssapi.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
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

/* Useful prototypes */
int nfs_rpc_get_worker_index(int mount_protocol_flag);

extern fd_set Svc_fdset;
extern nfs_worker_data_t *workers_data;
extern nfs_parameter_t nfs_param;
extern exportlist_t *pexportlist;
#ifdef _RPCSEC_GS_64_INSTALLED
struct svc_rpc_gss_data **TabGssData;
#endif
extern hash_table_t *ht_dupreq; /* duplicate request hash */
extern int rpcsec_gss_flag;

#ifndef _NO_BUDDY_SYSTEM
extern buddy_parameter_t buddy_param_worker;
#endif

/**
 * rpc_tcp_socket_manager_thread: manages a TCP socket connected to a client.
 *
 * this thread will manage a connection related to a specific TCP client.
 * 
 * @param IndexArg contains the socket number to be managed by this thread
 * 
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *rpc_tcp_socket_manager_thread(void *Arg)
{
  int rc = 0;
  long int tcp_sock = (long int)Arg;

  enum xprt_stat stat;
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  register SVCXPRT *xprt;
  register SVCXPRT *xprt_copy;
  char *cred_area;
  LRU_entry_t *pentry = NULL;
  LRU_status_t status;
  nfs_request_data_t *pnfsreq = NULL;
  int worker_index;
  static char my_name[MAXNAMLEN];

  struct sockaddr_in *paddr_caller = NULL;
  char str_caller[MAXNAMLEN];
  nfs_function_desc_t funcdesc;
  fridge_entry_t * pfe = NULL ;

  snprintf(my_name, MAXNAMLEN, "tcp_sock_mgr#fd=%ld", tcp_sock);
  SetNameFunction(my_name);

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_tcp_mgr)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH, "Memory manager could not be initialized");
      #ifdef _DEBUG_MEMLEAKS
      {
        FILE *output = fopen("/tmp/buddymem", "w");
        if (output != NULL)
          BuddyDumpAll(output);
      }
      #endif
      exit(1);
    }
#endif

  /* Calling dispatcher main loop */
  LogDebug(COMPONENT_DISPATCH,
           "TCP SOCKET MANAGER Sock=%ld(%p): Starting with pthread id #%p",
           tcp_sock, Arg, (caddr_t) pthread_self());

  for(;;)
    {
      /* Get a worker to do the job */
      if((worker_index = nfs_rpc_get_worker_index(FALSE)) < 0)
        {
          LogCrit(COMPONENT_DISPATCH, "CRITICAL ERROR: Couldn't choose a worker !!");
          return NULL;
        }

      /* Get a pnfsreq from the worker's pool */
      P(workers_data[worker_index].request_pool_mutex);

      GetFromPool(pnfsreq, &workers_data[worker_index].request_pool,
                  nfs_request_data_t);
 
      V(workers_data[worker_index].request_pool_mutex);

      if(pnfsreq == NULL)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "CRITICAL ERROR: empty request pool for the chosen worker ! Exiting...");
          exit(0);
        }

      xprt = Xports[tcp_sock];
      if(xprt == NULL)
        {
          /* But do we control sock? */
          LogCrit(COMPONENT_DISPATCH,
                  "CRITICAL ERROR: Incoherency found in Xports array, sock=%d",
                  (int)tcp_sock);
          return NULL;
        }
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
      LogFullDebug(COMPONENT_DISPATCH, "Use request from spool #%d, xprt->xp_fd=%d",
                   worker_index, xprt->xp_fd);
#else
      LogFullDebug(COMPONENT_DISPATCH, "Use request from spool #%d, xprt->xp_sock=%d",
                   worker_index, xprt->xp_sock);
#endif
      LogFullDebug(COMPONENT_DISPATCH, "Thread #%d has now %d pending requests",
                   worker_index, workers_data[worker_index].pending_request->nb_entry);

      /* Set up pointers */

      cred_area = pnfsreq->cred_area;
      preq = &(pnfsreq->req);
      pmsg = &(pnfsreq->msg);

      pmsg->rm_call.cb_cred.oa_base = cred_area;
      pmsg->rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
      preq->rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);

      /*
       * UDP RPCs are quite simple: everything comes to the same socket, so several SVCXPRT
       * can be defined, one per tbuf to handle the stuff
       * TCP RPCs are more complex:
       *   - a unique SVCXPRT exists that deals with initial tcp rendez vous. It does the accept
       *     with the client, but recv no message from the client. But SVC_RECV on it creates
       *     a new SVCXPRT dedicated to the client. This specific SVXPRT is bound on TCPSocket
       *
       * while receiving something on the Svc_fdset, I must know if this is a UDP request,
       * an initial TCP request or a TCP socket from an already connected client.
       * This is how to distinguish the cases:
       * UDP connections are bound to socket NFS_UDPSocket
       * TCP initial connections are bound to socket NFS_TCPSocket
       * all the other cases are requests from already connected TCP Clients
       */

      LogFullDebug(COMPONENT_DISPATCH,
                   "TCP SOCKET MANAGER : A NFS TCP request from an already connected client");
      pnfsreq->tcp_xprt = xprt;
      pnfsreq->xprt = pnfsreq->tcp_xprt;
      pnfsreq->ipproto = IPPROTO_TCP;

#if defined( _USE_TIRPC ) || defined( _FREEBSD )
      if(pnfsreq->xprt->xp_fd != tcp_sock)
#else
      if(pnfsreq->xprt->xp_sock != tcp_sock)
#endif
        LogCrit(COMPONENT_DISPATCH,
             "TCP SOCKET MANAGER : /!\\ Trying to access a bad socket ! Check the source file=%s, line=%u",
             __FILE__, __LINE__);

      //TODO FSF: I think this is a redundant message
      LogFullDebug(COMPONENT_DISPATCH, "Before waiting on select for socket %d", (int)tcp_sock);

      LogFullDebug(COMPONENT_DISPATCH, "Before calling SVC_RECV on socket %d", (int)tcp_sock);

      /* Will block until the client operates on the socket */
      pnfsreq->status = SVC_RECV(pnfsreq->xprt, pmsg);
      LogFullDebug(COMPONENT_DISPATCH, "Status for SVC_RECV on socket %d is %d", (int)tcp_sock,
                   pnfsreq->status);

      /* If status is ok, the request will be processed by the related
       * worker, otherwise, it should be released by being tagged as invalid*/
      if(!pnfsreq->status)
        {
          /* RPC over TCP specific: RPC/UDP's xprt know only one state: XPRT_IDLE, because UDP is mostly
           * a stateless protocol. With RPC/TCP, they can be XPRT_DIED especially when the client closes
           * the peer's socket. We have to cope with this aspect in the next lines */

          stat = SVC_STAT(pnfsreq->xprt);

          if(stat == XPRT_DIED)
            {
#ifndef _USE_TIRPC
              if((paddr_caller = svc_getcaller(pnfsreq->xprt)) != NULL)
                {
                  snprintf(str_caller, MAXNAMLEN, "0x%x=%d.%d.%d.%d",
                           ntohl(paddr_caller->sin_addr.s_addr),
                           (ntohl(paddr_caller->sin_addr.s_addr) & 0xFF000000) >> 24,
                           (ntohl(paddr_caller->sin_addr.s_addr) & 0x00FF0000) >> 16,
                           (ntohl(paddr_caller->sin_addr.s_addr) & 0x0000FF00) >> 8,
                           (ntohl(paddr_caller->sin_addr.s_addr) & 0x000000FF));
                }
              else
#endif                          /* _USE_TIRPC */
                strncpy(str_caller, "unresolved", MAXNAMLEN);

              LogEvent(COMPONENT_DISPATCH,
                   "TCP SOCKET MANAGER Sock=%d: the client (%s) disappeared... Freezing thread %p",
                   (int)tcp_sock, str_caller, (caddr_t)pthread_self());

              if(Xports[tcp_sock] != NULL)
                SVC_DESTROY(Xports[tcp_sock]);
              else
                LogCrit(COMPONENT_DISPATCH,
                     "TCP SOCKET MANAGER : /!\\ **** ERROR **** Mismatch between tcp_sock and xprt array");

              P(workers_data[worker_index].request_pool_mutex);
              ReleaseToPool(pnfsreq, &workers_data[worker_index].request_pool);
              V(workers_data[worker_index].request_pool_mutex);

             
              if( ( pfe = fridgethr_freeze( ) ) == NULL ) 
                {
		  /* Fridge expiration, the thread and exit */
                  LogEvent( COMPONENT_DISPATCH,
		            "TCP connection manager has expired in the fridge, let's kill it" ) ;
#ifndef _NO_BUDDY_SYSTEM
              /* Free stuff allocated by BuddyMalloc before thread exists */
		  /*              sleep(nfs_param.core_param.expiration_dupreq * 2);   / ** @todo : remove this for a cleaner fix */
              if((rc = BuddyDestroy()) != BUDDY_SUCCESS)
                LogCrit(COMPONENT_DISPATCH,
                        "TCP SOCKET MANAGER Sock=%d (on exit): got error %d from BuddyDestroy",
                        (int)tcp_sock, (int)rc);
#endif                          /*  _NO_BUDDY_SYSTEM */

                  return NULL  ;
                }

              tcp_sock = (long int )pfe->arg ;
              LogEvent( COMPONENT_DISPATCH,
			"TCP SOCKET MANAGER Now working on sock=%d after going out of the fridge", (int)tcp_sock ) ;
         
              continue ;
		
            }
          else if(stat == XPRT_MOREREQS)
            {
              LogDebug(COMPONENT_DISPATCH,
                       "TCP SOCKET MANAGER Sock=%d: XPRT has MOREREQS status",
                       (int)tcp_sock);
            }

          /* Release the entry */
          LogFullDebug(COMPONENT_DISPATCH,
                       "TCP SOCKET MANAGER Sock=%d: Invalidating entry with xprt_stat=%d",
                       (int)tcp_sock, stat);
          workers_data[worker_index].passcounter += 1;
        }
      else
        {
          struct timeval timer_start;
          struct timeval timer_end;
          struct timeval timer_diff;

          nfs_stat_type_t stat_type;
          nfs_request_latency_stat_t latency_stat;

          memset(&timer_start, 0, sizeof(struct timeval));
          memset(&timer_end, 0, sizeof(struct timeval));
          memset(&timer_diff, 0, sizeof(struct timeval));

          gettimeofday(&timer_start, NULL);

          /* Regular management of the request (UDP request or TCP request on connected handler */
          LogFullDebug(COMPONENT_DISPATCH, "Awaking thread #%d Xprt=%p", worker_index,
                       pnfsreq->xprt);
          P(workers_data[worker_index].mutex_req_condvar);
          P(workers_data[worker_index].request_pool_mutex);

          if((pentry =
              LRU_new_entry(workers_data[worker_index].pending_request, &status)) == NULL)
            {
              V(workers_data[worker_index].mutex_req_condvar);
              V(workers_data[worker_index].request_pool_mutex);
              LogMajor(COMPONENT_DISPATCH,
                       "Error while inserting pending request to Thread #%d",
                       worker_index);
              return NULL;
            }

          /* Call svc_getargs before making copy to prevent race conditions. */
          pnfsreq->req.rq_prog = pmsg->rm_call.cb_prog;
          pnfsreq->req.rq_vers = pmsg->rm_call.cb_vers;
          pnfsreq->req.rq_proc = pmsg->rm_call.cb_proc;

          rc = nfs_rpc_get_funcdesc(pnfsreq, &funcdesc);
          if (rc != FALSE)
            nfs_rpc_get_args(pnfsreq, &funcdesc);

          /* Update a copy of SVCXPRT and pass it to the worker thread to use it. */
          xprt_copy = pnfsreq->xprt_copy;
          Svcxprt_copy(xprt_copy, xprt);
          pnfsreq->xprt = xprt_copy;

          pentry->buffdata.pdata = (caddr_t) pnfsreq;
          pentry->buffdata.len = sizeof(*pnfsreq);

          if(pthread_cond_signal(&(workers_data[worker_index].req_condvar)) == -1)
            {
              V(workers_data[worker_index].mutex_req_condvar);
              V(workers_data[worker_index].request_pool_mutex);
              LogCrit(COMPONENT_DISPATCH,
                   "TCP SOCKET MANAGER Sock=%d: Cond signal failed for thr#%d , errno = %d",
                   (int)tcp_sock, worker_index, errno);
            }
          V(workers_data[worker_index].mutex_req_condvar);
          V(workers_data[worker_index].request_pool_mutex);
          LogFullDebug(COMPONENT_DISPATCH, "Waiting for commit from thread #%d",
                       worker_index);

          if (rc != FALSE)
            {
              gettimeofday(&timer_end, NULL);
              timer_diff = time_diff(timer_start, timer_end);
            
              /* Update await time. */
              stat_type = GANESHA_STAT_SUCCESS;
              latency_stat.type = AWAIT_TIME;
              latency_stat.latency = timer_diff.tv_sec * 1000000 + timer_diff.tv_usec; /* microseconds */
              nfs_stat_update(stat_type, &(workers_data[worker_index].stats.stat_req), &(pnfsreq->req), &latency_stat);
            
              LogFullDebug(COMPONENT_DISPATCH, "Thread #%d has committed the operation: end_time %llu.%.6llu await %llu.%.6llu",
                           worker_index, (unsigned long long int)timer_end.tv_sec, (unsigned long long int)timer_end.tv_usec, 
                           (unsigned long long int)timer_diff.tv_sec,(unsigned long long int)timer_diff.tv_usec);
            }
        }
    }

  LogDebug(COMPONENT_DISPATCH, "TCP SOCKET MANAGER Sock=%d: Stopping", (int)tcp_sock);

  /* Never reached */ 
  return NULL;
}                               /* rpc_tcp_socket_manager_thread */
