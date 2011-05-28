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
#include "rpc.h"
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
unsigned int nfs_rpc_get_worker_index(int mount_protocol_flag);

extern nfs_worker_data_t *workers_data;
extern exportlist_t *pexportlist;
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
  char *cred_area;
  nfs_request_data_t *pnfsreq = NULL;
  unsigned int worker_index;
  static char my_name[MAXNAMLEN];
  const nfs_function_desc_t *pfuncdesc;
  fridge_entry_t * pfe = NULL ;
  bool_t no_dispatch = TRUE, recv_status;

  snprintf(my_name, MAXNAMLEN, "tcp_sock_mgr#fd=%ld", tcp_sock);
  SetNameFunction(my_name);

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_tcp_mgr)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogMajor(COMPONENT_DISPATCH, "Memory manager could not be initialized");
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
           "Starting with pthread id #%p",
           (caddr_t) pthread_self());

  for(;;)
    {
      /* Get a worker to do the job */
      worker_index = nfs_rpc_get_worker_index(FALSE);

      /* Get a pnfsreq from the worker's pool */
      P(workers_data[worker_index].request_pool_mutex);

      GetFromPool(pnfsreq, &workers_data[worker_index].request_pool,
                  nfs_request_data_t);

      V(workers_data[worker_index].request_pool_mutex);

      if(pnfsreq == NULL)
        {
          LogMajor(COMPONENT_DISPATCH,
                   "Empty request pool for the chosen worker! Exiting...");
          exit(1);
        }

      xprt = Xports[tcp_sock];
      if(xprt == NULL)
        {
          /* But do we control sock? */
          LogMajor(COMPONENT_DISPATCH,
                   "Incoherency found in Xports array! Exiting...");
          exit(1);
        }
      LogFullDebug(COMPONENT_DISPATCH,
                   "Use request from Worker Thread #%u's pool, xprt->xp_sock=%d",
                   worker_index, xprt->XP_SOCK);
      LogFullDebug(COMPONENT_DISPATCH,
                   "Worker Thread #%u has now %d pending requests",
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
                   "A NFS TCP request from an already connected client");
      pnfsreq->xprt = xprt;
      preq->rq_xprt = xprt;
      pnfsreq->ipproto = IPPROTO_TCP;

      if(pnfsreq->xprt->XP_SOCK != tcp_sock)
        LogCrit(COMPONENT_DISPATCH,
                "Trying to access a bad socket! Check the source file=%s, line=%u",
                __FILE__, __LINE__);

      LogFullDebug(COMPONENT_DISPATCH,
                   "Before calling SVC_RECV on socket %d", (int)tcp_sock);

      /* Will block until the client operates on the socket */
      recv_status = SVC_RECV(pnfsreq->xprt, pmsg);
      LogFullDebug(COMPONENT_DISPATCH,
                   "Status for SVC_RECV on socket %d is %d xid=%u",
                   (int)tcp_sock, recv_status, pmsg->rm_xid);

      /* If status is ok, the request will be processed by the related
       * worker, otherwise, it should be released by being tagged as invalid*/
      if(!recv_status)
        {
          /* RPC over TCP specific: RPC/UDP's xprt know only one state: XPRT_IDLE, because UDP is mostly
           * a stateless protocol. With RPC/TCP, they can be XPRT_DIED especially when the client closes
           * the peer's socket. We have to cope with this aspect in the next lines */

          stat = SVC_STAT(pnfsreq->xprt);

          if(stat == XPRT_DIED)
            {
              sockaddr_t addr;
              char addrbuf[SOCK_NAME_MAX];
              if(copy_xprt_addr(&addr, pnfsreq->xprt) == 1)
                sprint_sockaddr(&addr, addrbuf, sizeof(addrbuf));
              else
                sprintf(addrbuf, "<unresolved>");

              LogDebug(COMPONENT_DISPATCH,
                       "The client (%s) disappeared... Freezing thread %p",
                       addrbuf, (caddr_t)pthread_self());

              if(Xports[tcp_sock] != NULL)
                SVC_DESTROY(Xports[tcp_sock]);
              else
                LogCrit(COMPONENT_DISPATCH,
                        "Mismatch between tcp_sock and xprt array");

              if( ( pfe = fridgethr_freeze( ) ) == NULL )
                {
                  /* Fridge expiration, the thread and exit */
                  LogDebug(COMPONENT_DISPATCH,
                           "TCP connection manager has expired in the fridge, let's kill it" ) ;
#ifndef _NO_BUDDY_SYSTEM
              /* Free stuff allocated by BuddyMalloc before thread exists */
                  /*              sleep(nfs_param.core_param.expiration_dupreq * 2);   / ** @todo : remove this for a cleaner fix */
              if((rc = BuddyDestroy()) != BUDDY_SUCCESS)
                LogCrit(COMPONENT_DISPATCH,
                        "Sock=%d (on exit): got error %d from BuddyDestroy",
                        (int)tcp_sock, (int)rc);
#endif                          /*  _NO_BUDDY_SYSTEM */

                  break;
                }

              tcp_sock = (long int )pfe->arg;
              LogDebug(COMPONENT_DISPATCH,
                       "Now working on sock=%d after going out of the fridge",
                       (int)tcp_sock);
              snprintf(my_name, MAXNAMLEN, "tcp_sock_mgr#fd=%ld", tcp_sock);
              SetNameFunction(my_name);

              continue;
            }
          else if(stat == XPRT_MOREREQS)
            {
              LogDebug(COMPONENT_DISPATCH,
                       "XPRT has MOREREQS status");
            }

          /* Release the entry */
          P(workers_data[worker_index].request_pool_mutex);
          ReleaseToPool(pnfsreq, &workers_data[worker_index].request_pool);
          V(workers_data[worker_index].request_pool_mutex);

          LogFullDebug(COMPONENT_DISPATCH,
                       "Invalidating entry with xprt_stat=%d",
                       stat);
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

          /* Call svc_getargs before making copy to prevent race conditions. */
          pnfsreq->req.rq_prog = pmsg->rm_call.cb_prog;
          pnfsreq->req.rq_vers = pmsg->rm_call.cb_vers;
          pnfsreq->req.rq_proc = pmsg->rm_call.cb_proc;

          /* Use primary xprt for now (in case xprt has GSS state)
           * until we make a copy
           */
          pnfsreq->xprt = xprt;
          pnfsreq->req.rq_xprt = xprt;

          pfuncdesc = nfs_rpc_get_funcdesc(pnfsreq);

          if(pfuncdesc != INVALID_FUNCDESC)
            AuthenticateRequest(pnfsreq, &no_dispatch);
          else
            no_dispatch = TRUE;

          if(!no_dispatch && !nfs_rpc_get_args(pnfsreq, pfuncdesc))
            no_dispatch = TRUE;

          if(!no_dispatch)
            {
              /* Update a copy of SVCXPRT and pass it to the worker thread to use it. */
              pnfsreq->xprt_copy = Svcxprt_copy(pnfsreq->xprt_copy, xprt);
              if(pnfsreq->xprt_copy == NULL)
                no_dispatch = TRUE;
            }
          if(no_dispatch)
            {
              /* Release the entry */
              P(workers_data[worker_index].request_pool_mutex);
              ReleaseToPool(pnfsreq, &workers_data[worker_index].request_pool);
              V(workers_data[worker_index].request_pool_mutex);
            }
          else
            {
              pnfsreq->xprt = pnfsreq->xprt_copy;
              preq->rq_xprt = pnfsreq->xprt_copy;

              /* Regular management of the request (UDP request or TCP request on connected handler */
              DispatchWork(pnfsreq, worker_index);

              gettimeofday(&timer_end, NULL);
              timer_diff = time_diff(timer_start, timer_end);

              /* Update await time. */
              stat_type = GANESHA_STAT_SUCCESS;
              latency_stat.type = AWAIT_TIME;
              latency_stat.latency = timer_diff.tv_sec * 1000000 + timer_diff.tv_usec; /* microseconds */
              nfs_stat_update(stat_type,
                              &(workers_data[worker_index].stats.stat_req),
                              &(pnfsreq->req),
                              &latency_stat);

              LogFullDebug(COMPONENT_DISPATCH,
                           "Worker Thread #%u has committed the operation: end_time %llu.%.6llu await %llu.%.6llu",
                           worker_index,
                           (unsigned long long int)timer_end.tv_sec,
                           (unsigned long long int)timer_end.tv_usec,
                           (unsigned long long int)timer_diff.tv_sec,
                           (unsigned long long int)timer_diff.tv_usec);
            }
        }
    }

  LogCrit(COMPONENT_DISPATCH, "Stopping");

  /* Never reached */
  return NULL;
}                               /* rpc_tcp_socket_manager_thread */
