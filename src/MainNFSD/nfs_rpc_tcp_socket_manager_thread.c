/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
#include <gssapi/gssapi.h>
#include <sys/select.h>
#include "HashData.h"
#include "HashTable.h"

#if defined( _USE_TIRPC )
#include <rpc/rpc.h>
#elif defined( _USE_GSSRPC )
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
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
extern SVCXPRT **Xports;        /* The one from RPCSEC_GSS library */
#ifdef _RPCSEC_GS_64_INSTALLED
struct svc_rpc_gss_data **TabGssData;
#endif
extern hash_table_t *ht_dupreq; /* duplicate request hash */
extern int rpcsec_gss_flag;

#ifndef _NO_BUDDY_SYSTEM
extern buddy_parameter_t buddy_param_worker;
#endif

extern pthread_mutex_t mutex_cond_xprt[FD_SETSIZE];
extern pthread_cond_t condvar_xprt[FD_SETSIZE];
extern int etat_xprt[FD_SETSIZE];

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
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *rpc_tcp_socket_manager_thread(void *Arg)
{
  int rc = 0;
  long int tcp_sock = (long int)Arg;

  enum xprt_stat stat;
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  register SVCXPRT *xprt;
  char *cred_area;
  LRU_entry_t *pentry = NULL;
  LRU_status_t status;
  nfs_request_data_t *pnfsreq = NULL;
  int worker_index;
  static char my_name[MAXNAMLEN];

  snprintf(my_name, MAXNAMLEN, "tcp_sock_mgr#fd=%ld", tcp_sock);
  SetNameFunction(my_name);

  /* Calling dispatcher main loop */
  DisplayLogLevel(NIV_DEBUG,
                  "TCP SOCKET MANAGER Sock=%ld(%p): Starting with pthread id #%p",
                  tcp_sock, Arg, (caddr_t) pthread_self());

#ifndef _NO_BUDDY_SYSTEM

  if ((rc = BuddyInit(&nfs_param.buddy_param_worker)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      DisplayLog("Memory manager could not be initialized");
      exit(1);
    }
#endif

  for (;;)
    {
      /* Get a worker to do the job */
      if ((worker_index = nfs_rpc_get_worker_index(FALSE)) < 0)
        {
          DisplayLog("CRITICAL ERROR: Couldn't choose a worker !!");
          return NULL;
        }

      /* Get a pnfsreq from the worker's pool */
      P(workers_data[worker_index].request_pool_mutex);

#ifdef _DEBUG_MEMLEAKS
      /* For debugging memory leaks */
      BuddySetDebugLabel("nfs_request_data_t");
#endif

      GET_PREALLOC_CONSTRUCT(pnfsreq,
                             workers_data[worker_index].request_pool,
                             nfs_param.worker_param.nb_pending_prealloc,
                             nfs_request_data_t,
                             next_alloc, constructor_nfs_request_data_t);

#ifdef _DEBUG_MEMLEAKS
      /* For debugging memory leaks */
      BuddySetDebugLabel("N/A");
#endif

      V(workers_data[worker_index].request_pool_mutex);

      if (pnfsreq == NULL)
        {
          DisplayLogLevel(NIV_CRIT,
                          "CRITICAL ERROR: empty request pool for the chosen worker ! Exiting...");
          exit(0);
        }

      xprt = Xports[tcp_sock];
      if (xprt == NULL)
        {
          /* But do we control sock? */
          DisplayLogLevel(NIV_CRIT,
                          "CRITICAL ERROR: Incoherency found in Xports array, sock=%d",
                          tcp_sock);
          return NULL;
        }
#ifdef _DEBUG_DISPATCH
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
      DisplayLogLevel(NIV_FULL_DEBUG, "Use request from spool #%d, xprt->xp_fd=%d",
                      worker_index, xprt->xp_fd);
#else
      DisplayLogLevel(NIV_FULL_DEBUG, "Use request from spool #%d, xprt->xp_sock=%d",
                      worker_index, xprt->xp_sock);
#endif
      DisplayLogLevel(NIV_FULL_DEBUG, "Thread #%d has now %d pending requests",
                      worker_index, workers_data[worker_index].pending_request->nb_entry);
#endif

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

#ifdef _DEBUG_DISPATCH
      DisplayLogLevel(NIV_FULL_DEBUG,
                      "TCP SOCKET MANAGER : A NFS TCP request from an already connected client");
#endif
      pnfsreq->tcp_xprt = xprt;
      pnfsreq->xprt = pnfsreq->tcp_xprt;
      pnfsreq->ipproto = IPPROTO_TCP;

#if defined( _USE_TIRPC ) || defined( _FREEBSD )
      if (pnfsreq->xprt->xp_fd != tcp_sock)
#else
      if (pnfsreq->xprt->xp_sock != tcp_sock)
#endif
        DisplayLog
            ("TCP SOCKET MANAGER : /!\\ Trying to access a bad socket ! Check the source file=%s, line=%s",
             __FILE__, __LINE__);

#ifdef _DEBUG_DISPATCH
      DisplayLogLevel(NIV_FULL_DEBUG, "Before waiting on select for socket %d", tcp_sock);
#endif

#ifdef _DEBUG_DISPATCH
      DisplayLogLevel(NIV_FULL_DEBUG, "Before calling SVC_RECV on socket %d", tcp_sock);
#endif

      /* Will block until the client operates on the socket */
      pnfsreq->status = SVC_RECV(pnfsreq->xprt, pmsg);
#ifdef _DEBUG_DISPATCH
      DisplayLogLevel(NIV_FULL_DEBUG, "Status for SVC_RECV on socket %d is %d", tcp_sock,
                      pnfsreq->status);
#endif

      /* If status is ok, the request will be processed by the related
       * worker, otherwise, it should be released by being tagged as invalid*/
      if (!pnfsreq->status)
        {
          /* RPC over TCP specific: RPC/UDP's xprt know only one state: XPRT_IDLE, because UDP is mostly
           * a stateless protocol. With RPC/TCP, they can be XPRT_DIED especially when the client closes
           * the peer's socket. We have to cope with this aspect in the next lines */

          stat = SVC_STAT(pnfsreq->xprt);

          if (stat == XPRT_DIED)
            {
              DisplayLogLevel(NIV_DEBUG,
                              "TCP SOCKET MANAGER Sock=%d: the client disappeared... Stopping thread ",
                              tcp_sock);

              if (Xports[tcp_sock] != NULL)
                SVC_DESTROY(Xports[tcp_sock]);
              else
                DisplayLog
                    ("TCP SOCKET MANAGER : /!\\ **** ERROR **** Mismatch between tcp_sock and xprt array");

              P(workers_data[worker_index].request_pool_mutex);
              RELEASE_PREALLOC(pnfsreq, workers_data[worker_index].request_pool,
                               next_alloc);
              V(workers_data[worker_index].request_pool_mutex);

              return NULL;
            }
          else if (stat == XPRT_MOREREQS)
            {
              DisplayLogLevel(NIV_DEBUG,
                              "TCP SOCKET MANAGER Sock=%d: XPRT has MOREREQS status",
                              tcp_sock);
            }

          /* Release the entry */
#ifdef _DEBUG_DISPATCH
          DisplayLogLevel(NIV_FULL_DEBUG,
                          "TCP SOCKET MANAGER Sock=%d: Invalidating entry with xprt_stat=%d",
                          tcp_sock, stat);
#endif
          workers_data[worker_index].passcounter += 1;
        }
      else
        {
          /* Regular management of the request (UDP request or TCP request on connected handler */
#ifdef _DEBUG_DISPATCH
          DisplayLogLevel(NIV_FULL_DEBUG, "Awaking thread #%d Xprt=%p", worker_index,
                          pnfsreq->xprt);
#endif
          P(workers_data[worker_index].mutex_req_condvar);
          P(workers_data[worker_index].request_pool_mutex);

          if ((pentry =
               LRU_new_entry(workers_data[worker_index].pending_request,
                             &status)) == NULL)
            {
              V(workers_data[worker_index].mutex_req_condvar);
              V(workers_data[worker_index].request_pool_mutex);
              DisplayLogLevel(NIV_MAJOR,
                              "Error while inserting pending request to Thread #%d",
                              worker_index);
              return NULL;
            }
          pentry->buffdata.pdata = (caddr_t) pnfsreq;
          pentry->buffdata.len = sizeof(*pnfsreq);

          if (pthread_cond_signal(&(workers_data[worker_index].req_condvar)) == -1)
            {
              V(workers_data[worker_index].mutex_req_condvar);
              V(workers_data[worker_index].request_pool_mutex);
              DisplayLog
                  ("TCP SOCKET MANAGER Sock=%d: Cond signal failed for thr#%d , errno = %d",
                   tcp_sock, worker_index, errno);
            }
          V(workers_data[worker_index].mutex_req_condvar);
          V(workers_data[worker_index].request_pool_mutex);
#ifdef _DEBUG_DISPATCH
          DisplayLogLevel(NIV_FULL_DEBUG, "Waiting for commit from thread #%d",
                          worker_index);
#endif

          P(mutex_cond_xprt[tcp_sock]);
          while (etat_xprt[tcp_sock] != 1)
            {
              pthread_cond_wait(&(condvar_xprt[tcp_sock]), &(mutex_cond_xprt[tcp_sock]));
            }
          etat_xprt[tcp_sock] = 0;
          V(mutex_cond_xprt[tcp_sock]);

#ifdef _DEBUG_DISPATCH
          DisplayLogLevel(NIV_FULL_DEBUG, "Thread #%d has committed the operation",
                          worker_index);
#endif
        }
    }

  DisplayLogLevel(NIV_DEBUG, "TCP SOCKET MANAGER Sock=%d: Stopping", tcp_sock);

  return NULL;
}                               /* rpc_tcp_socket_manager_thread */
