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
 * \file    nfs_rpc_dispatcher.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 12:33:05 $
 * \version $Revision: 1.96 $
 * \brief   The file that contain the 'rpc_dispatcher_thread' routine for the nfsd.
 *
 * nfs_rpc_dispatcher.c : The file that contain the 'rpc_dispatcher_thread' routine for the nfsd (and all
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
#include "nlm4.h"
#include "rquota.h"
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

#ifdef _APPLE
#define __FDS_BITS(set) ((set)->fds_bits)
#endif

extern nfs_start_info_t nfs_start_info;

#ifdef _USE_TIRPC
struct netconfig *getnetconfigent(const char *netid);
void freenetconfigent(struct netconfig *);
SVCXPRT *Svc_vc_create(int, u_int, u_int);
SVCXPRT *Svc_dg_create(int, u_int, u_int);
#else
SVCXPRT *Svcfd_create(int fd, u_int sendsize, u_int recvsize);
SVCXPRT *Svctcp_create(register int sock, u_int sendsize, u_int recvsize);
SVCXPRT *Svcudp_bufcreate(register int sock, u_int sendsz, u_int recvsz);
bool_t Svc_register(SVCXPRT * xprt, u_long prog, u_long vers, void (*dispatch) (),
                    int protocol);
#endif

void socket_setoptions(int socketFd);

#define NULL_SVC ((struct svc_callout *)0)

extern fd_set Svc_fdset;
extern nfs_worker_data_t *workers_data;
extern nfs_parameter_t nfs_param;
extern SVCXPRT *Xports[FD_SETSIZE];     /* The one from RPCSEC_GSS library */
#ifdef _RPCSEC_GS_64_INSTALLED
struct svc_rpc_gss_data **TabGssData;
#endif
extern hash_table_t *ht_dupreq; /* duplicate request hash */

extern pthread_mutex_t mutex_cond_xprt[FD_SETSIZE];
extern pthread_cond_t condvar_xprt[FD_SETSIZE];
extern int etat_xprt[FD_SETSIZE];

#if _USE_TIRPC
/* public data : */
rw_lock_t Svc_lock;
rw_lock_t Svc_fd_lock;
#endif

/* These two variables keep state of the thread that gc at this time */
unsigned int nb_current_gc_workers;
pthread_mutex_t lock_nb_current_gc_workers;

#ifdef _DEBUG_MEMLEAKS
/**
 *
 * nfs_debug_debug_label_debug_info: a function used for debugging purpose, tracing Buddy Malloc activity.
 *
 * @param none (void arguments)
 * 
 * @return nothing (void function) 
 * 
 */
void nfs_debug_debug_label_info()
{
  buddy_stats_t bstats;

  BuddyLabelsSummary();

  BuddyGetStats(&bstats);
  LogFullDebug(COMPONENT_MEMLEAKS,"------- TOTAL SPACE USED FOR WORKER THREAD: %12lu (on %2u pages)",
         (unsigned long)bstats.StdUsedSpace, bstats.NbStdUsed);

  /* DisplayMemoryMap(); */

  LogFullDebug(COMPONENT_MEMLEAKS,"--------------------------------------------------");

}                               /* nfs_debug_debug_label_info */

void nfs_debug_buddy_info()
{
  buddy_stats_t bstats;

  BuddyLabelsSummary();

  BuddyGetStats(&bstats);
  LogFullDebug(COMPONENT_MEMLEAKS,"------- TOTAL SPACE USED FOR DISPATCHER THREAD: %12lu (on %2u pages)",
         (unsigned long)bstats.StdUsedSpace, bstats.NbStdUsed);

  LogFullDebug(COMPONENT_MEMLEAKS,"--------------------------------------------------");
}

#endif

/**
 *
 * nfs_rpc_dispatch_dummy: Function never called, but the symbol is necessary for Svc_register/
 *
 * @param ptr_req the RPC request to be managed
 * @param ptr_svc SVCXPRT pointer to be used for managing this request
 * 
 * @return nothing (void function) and is never called indeed.
 * 
 */
void nfs_rpc_dispatch_dummy(struct svc_req *ptr_req, SVCXPRT * ptr_svc)
{
  LogMajor(COMPONENT_DISPATCH,
      "NFS DISPATCH DUMMY: Possible error, function nfs_rpc_dispatch_dummy should never be called");
  return;
}                               /* nfs_rpc_dispatch_dummy */

/**
 * nfs_Init_svc: Init the svc descriptors for the nfs daemon. 
 *
 * Perform all the required initialization for the SVCXPRT pointer. 
 * 
 *
 */
int nfs_Init_svc()
{
#ifdef _USE_TIRPC
  struct netconfig *netconfig_udpv4;
  struct netconfig *netconfig_tcpv4;
#ifdef _USE_TIRPC_IPV6
  struct netconfig *netconfig_udpv6;
  struct netconfig *netconfig_tcpv6;
#endif
#endif
  struct sockaddr_in sinaddr_nfs;
  struct sockaddr_in sinaddr_mnt;
  struct sockaddr_in sinaddr_nlm;
  struct sockaddr_in sinaddr_rquota;
#ifdef _USE_TIRPC_IPV6
  struct sockaddr_in6 sinaddr_nfs_udp6;
  struct sockaddr_in6 sinaddr_mnt_udp6;
  struct netbuf netbuf_nfs_udp6;
  struct netbuf netbuf_mnt_udp6;
  struct sockaddr_in6 sinaddr_nfs_tcp6;
  struct sockaddr_in6 sinaddr_mnt_tcp6;
  struct netbuf netbuf_nfs_tcp6;
  struct netbuf netbuf_mnt_tcp6;
  struct t_bind bindaddr_nfs_udp6;
  struct t_bind bindaddr_mnt_udp6;
  struct t_bind bindaddr_nfs_tcp6;
  struct t_bind bindaddr_mnt_tcp6;
  struct __rpc_sockinfo si_nfs_tcp6;
  struct __rpc_sockinfo si_nfs_udp6;
  struct __rpc_sockinfo si_mnt_tcp6;
  struct __rpc_sockinfo si_mnt_udp6;
#endif
  int one = 1;
  int rc = 0;
  int nb_svc_nfs_ok = 0;
  int nb_svc_mnt_ok = 0;
  int nb_svc_nlm_ok = 0;
  int nb_svc_rquota_ok = 0;
#ifdef _USE_GSSRPC
  OM_uint32 maj_stat;
  OM_uint32 min_stat;
  char GssError[1024];
  gss_cred_id_t test_gss_cred = NULL;
  gss_name_t imported_name = NULL;
#endif

  FD_ZERO(&Svc_fdset);

#ifdef _USE_TIRPC
  LogEvent(COMPONENT_DISPATCH, "NFS INIT: Using TIRPC");

  /* Get the netconfig entries from /etc/netconfig */
  if((netconfig_udpv4 = (struct netconfig *)getnetconfigent("udp")) == NULL)
    {
      LogCrit(COMPONENT_DISPATCH,
          "/!\\ Cannot get a entry for udp in netconfig file. Check file /etc/netconfig...");
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get udp netconfig");
      return -1;
    }

  /* Get the netconfig entries from /etc/netconfig */
  if((netconfig_tcpv4 = (struct netconfig *)getnetconfigent("tcp")) == NULL)
    {
      LogCrit(COMPONENT_DISPATCH,
          "/!\\ Cannot get a entry for tcp in netconfig file. Check file /etc/netconfig...");
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get tcp netconfig");
      return -1;
    }
#ifdef _USE_TIRPC_IPV6
  LogEvent(COMPONENT_DISPATCH, "NFS INIT: Using IPv6");

  /* Get the netconfig entries from /etc/netconfig */
  if((netconfig_udpv6 = (struct netconfig *)getnetconfigent("udp6")) == NULL)
    {
      LogCrit(COMPONENT_DISPATCH,
          "/!\\ Cannot get a entry for udp6 in netconfig file. Check file /etc/netconfig...");
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get udp6 netconfig");
      return -1;
    }

  /* Get the netconfig entries from /etc/netconfig */
  if((netconfig_tcpv6 = (struct netconfig *)getnetconfigent("tcp6")) == NULL)
    {
      LogCrit(COMPONENT_DISPATCH,
          "/!\\ Cannot get a entry for tcp in netconfig file. Check file /etc/netconfig...");
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get tcp6 netconfig");
      return -1;
    }
#endif

  /* A short message to show that /etc/netconfig parsing was a success */
  LogEvent(COMPONENT_DISPATCH, "netconfig found for UDPv4 and TCPv4");
#ifdef _USE_TIRPC_IPV6
  LogEvent(COMPONENT_DISPATCH, "netconfig found for UDPv6 and TCPv6");
#endif

  /* RW_lock need to be initialized */
  rw_lock_init(&Svc_lock);
  rw_lock_init(&Svc_fd_lock);
#endif                          /* _USE_TIRPC */

#ifndef _USE_TIRPC_IPV6
  /* Allocate the UDP and TCP structure for the RPC */
  if((nfs_param.worker_param.nfs_svc_data.socket_nfs_udp =
      socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp =
      socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_mnt_udp =
      socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp =
      socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp socket");
      return -1;
    }
#ifdef _USE_NLM
  if((nfs_param.worker_param.nfs_svc_data.socket_nlm_udp =
      socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp =
      socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp socket");
      return -1;
    }
#endif                          /* USE_NLM */

#ifdef _USE_QUOTA
  if((nfs_param.worker_param.nfs_svc_data.socket_rquota_udp =
      socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_rquota_tcp =
      socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp socket");
      return -1;
    }
#endif                          /* _USE_QUOTA */

#else
  /* Allocate the UDP and TCP structure for the RPC */
  if((nfs_param.worker_param.nfs_svc_data.socket_nfs_udp =
      socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp6 socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp =
      socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp6 socket");
      return -1;

    }

  if((nfs_param.worker_param.nfs_svc_data.socket_mnt_udp =
      socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp6 socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp =
      socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp6 socket");
      return -1;
    }
#ifdef _USE_NLM
  if((nfs_param.worker_param.nfs_svc_data.socket_nlm_udp =
      socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp6 socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp =
      socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp6 socket");
      return -1;
    }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
  if((nfs_param.worker_param.nfs_svc_data.socket_rquota_udp =
      socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a udp6 socket");
      return -1;
    }

  if((nfs_param.worker_param.nfs_svc_data.socket_rquota_tcp =
      socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SOCKET, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate a tcp6 socket");
      return -1;
    }
#endif                          /* _USE_QUOTA */

#endif

  /* Use SO_REUSEADDR in order to avoid wait the 2MSL timeout */
  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad udp socket options");
      return -1;
    }

  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad tcp socket options");
      return -1;
    }

  socket_setoptions(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp);

  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad udp socket options");
      return -1;
    }

  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad tcp socket options");
      return -1;
    }
#ifdef _USE_NLM
  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad udp socket options");
      return -1;
    }

  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad tcp socket options");
      return -1;
    }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad udp socket options");
      return -1;
    }

  if(setsockopt(nfs_param.worker_param.nfs_svc_data.socket_rquota_tcp,
                SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SETSOCKOPT, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Bad tcp socket options");
      return -1;
    }
#endif                          /* _USE_QUOTA */

  /* We prefer using non-blocking socket in the specific case */
  if(fcntl(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp, F_SETFL, FNDELAY) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_FCNTL, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot set udp socket as non blocking");
      return -1;
    }

  if(fcntl(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp, F_SETFL, FNDELAY) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_FCNTL, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot set udp socket as non blocking");
      return -1;
    }
#ifdef _USE_NLM
  if(fcntl(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp, F_SETFL, FNDELAY) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_FCNTL, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot set udp socket as non blocking");
      return -1;
    }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
  if(fcntl(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp, F_SETFL, FNDELAY) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_FCNTL, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot set udp socket as non blocking");
      return -1;
    }
#endif                          /* _USE_QUOTA */

#ifdef _USE_NLM
  /* Some log that can be useful when debug ONC/RPC and RPCSEC_GSS matter */
  LogEvent(COMPONENT_DISPATCH,
           "Socket numbers are: nfs_udp=%u  nfs_tcp=%u "
           "mnt_udp=%u  mnt_tcp=%u nlm_tcp=%u nlm_udp=%u",
           nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
           nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
           nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
           nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp,
           nfs_param.worker_param.nfs_svc_data.socket_nlm_udp,
           nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp);
#else
  /* Some log that can be useful when debug ONC/RPC and RPCSEC_GSS matter */
  LogEvent(COMPONENT_DISPATCH,
           "Socket numbers are: nfs_udp=%u  nfs_tcp=%u "
           "mnt_udp=%u  mnt_tcp=%u",
           nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
           nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
           nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
           nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp);
#endif                          /* USE_NLM */

  /* Bind the udp and tcp socket to port 2049/tcp and 2049/udp */
  memset((char *)&sinaddr_nfs, 0, sizeof(sinaddr_nfs));
  sinaddr_nfs.sin_family = AF_INET;
  sinaddr_nfs.sin_addr.s_addr = nfs_param.core_param.bind_addr.sin_addr.s_addr;
  sinaddr_nfs.sin_port = htons(nfs_param.core_param.nfs_port);

  /* It's now time for binding the sockets */
#ifndef _USE_TIRPC_IPV6
  if(bind(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
          (struct sockaddr *)&sinaddr_nfs, sizeof(sinaddr_nfs)) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind udp socket");
      return -1;
    }

  if((rc = bind(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
                (struct sockaddr *)&sinaddr_nfs, sizeof(sinaddr_nfs))) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind tcp socket rc=%d errno=%d", rc, errno);
      return -1;
    }

  /* Bind the udp and tcp socket to ephemeral port for mountd */
  memset((char *)&sinaddr_mnt, 0, sizeof(sinaddr_mnt));
  sinaddr_mnt.sin_family = AF_INET;
  sinaddr_mnt.sin_addr.s_addr = nfs_param.core_param.bind_addr.sin_addr.s_addr;
  sinaddr_mnt.sin_port = htons(nfs_param.core_param.mnt_port);

  /* It's now time for binding the sockets */
  if(bind(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
          (struct sockaddr *)&sinaddr_mnt, sizeof(sinaddr_mnt)) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind udp socket");
      return -1;
    }

  if((rc = bind(nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp,
                (struct sockaddr *)&sinaddr_mnt, sizeof(sinaddr_mnt))) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind tcp socket rc=%d errno=%d", rc, errno);
      return -1;
    }
#ifdef _USE_NLM
  /* Bind the nlm service */
  memset((char *)&sinaddr_nlm, 0, sizeof(sinaddr_nlm));
  sinaddr_nlm.sin_family = AF_INET;
  sinaddr_nlm.sin_addr.s_addr = nfs_param.core_param.bind_addr.sin_addr.s_addr;
  sinaddr_nlm.sin_port = htons(nfs_param.core_param.nlm_port);

  /* It's now time for binding the sockets */
  if(bind(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp,
          (struct sockaddr *)&sinaddr_nlm, sizeof(sinaddr_nlm)) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind udp socket");
      return -1;
    }

  if((rc = bind(nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp,
                (struct sockaddr *)&sinaddr_nlm, sizeof(sinaddr_nlm))) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind tcp socket rc=%d errno=%d", rc, errno);
      return -1;
    }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
  /* Bind the rquota service */
  memset((char *)&sinaddr_rquota, 0, sizeof(sinaddr_rquota));
  sinaddr_rquota.sin_family = AF_INET;
  sinaddr_rquota.sin_addr.s_addr = nfs_param.core_param.bind_addr.sin_addr.s_addr;
  sinaddr_rquota.sin_port = htons(nfs_param.core_param.rquota_port);

  /* It's now time for binding the sockets */
  if(bind(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp,
          (struct sockaddr *)&sinaddr_rquota, sizeof(sinaddr_rquota)) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind udp socket");
      return -1;
    }

  if((rc = bind(nfs_param.worker_param.nfs_svc_data.socket_rquota_tcp,
                (struct sockaddr *)&sinaddr_rquota, sizeof(sinaddr_rquota))) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind tcp socket rc=%d errno=%d", rc, errno);
      return -1;
    }
#endif                          /* _USE_QUOTA */

#else
  /* Bind the udp and tcp socket to port 2049/tcp and 2049/udp */
  memset((char *)&sinaddr_nfs_udp6, 0, sizeof(sinaddr_nfs_udp6));
  sinaddr_nfs_udp6.sin6_family = AF_INET6;
  sinaddr_nfs_udp6.sin6_addr = in6addr_any;     /* All the interfaces on the machine are used */
  sinaddr_nfs_udp6.sin6_port = htons(nfs_param.core_param.nfs_port);

  netbuf_nfs_udp6.maxlen = sizeof(sinaddr_nfs_udp6);
  netbuf_nfs_udp6.len = sizeof(sinaddr_nfs_udp6);
  netbuf_nfs_udp6.buf = &sinaddr_nfs_udp6;

  bindaddr_nfs_udp6.qlen = SOMAXCONN;
  bindaddr_nfs_udp6.addr = netbuf_nfs_udp6;

  if(!__rpc_fd2sockinfo(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp, &si_nfs_udp6))
    {
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get socket info for udp6 socket rc=%d errno=%d", rc,
                 errno);
      return -1;
    }

  memset((char *)&sinaddr_nfs_tcp6, 0, sizeof(sinaddr_nfs_tcp6));
  sinaddr_nfs_tcp6.sin6_family = AF_INET6;
  sinaddr_nfs_tcp6.sin6_addr = in6addr_any;     /* All the interfaces on the machine are used */
  sinaddr_nfs_tcp6.sin6_port = htons(nfs_param.core_param.nfs_port);

  netbuf_nfs_tcp6.maxlen = sizeof(sinaddr_nfs_tcp6);
  netbuf_nfs_tcp6.len = sizeof(sinaddr_nfs_tcp6);
  netbuf_nfs_tcp6.buf = &sinaddr_nfs_tcp6;

  bindaddr_nfs_tcp6.qlen = SOMAXCONN;
  bindaddr_nfs_tcp6.addr = netbuf_nfs_tcp6;

  if(!__rpc_fd2sockinfo(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp, &si_nfs_tcp6))
    {
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get socket info for tcp6 socket rc=%d errno=%d", rc,
                 errno);
      return -1;
    }

  if(bind(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
          (struct sockaddr *)bindaddr_nfs_tcp6.addr.buf,
          (socklen_t) si_nfs_tcp6.si_alen) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind tcp6 socket");
      return -1;
    }

  if(bind(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
          (struct sockaddr *)bindaddr_nfs_udp6.addr.buf,
          (socklen_t) si_nfs_udp6.si_alen) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind udp6 socket");
      return -1;
    }

  memset((char *)&sinaddr_mnt_udp6, 0, sizeof(sinaddr_mnt_udp6));
  sinaddr_mnt_udp6.sin6_family = AF_INET6;
  sinaddr_mnt_udp6.sin6_addr = in6addr_any;     /* All the interfaces on the machine are used */
  sinaddr_mnt_udp6.sin6_port = htons(nfs_param.core_param.mnt_port);

  netbuf_mnt_udp6.maxlen = sizeof(sinaddr_mnt_udp6);
  netbuf_mnt_udp6.len = sizeof(sinaddr_mnt_udp6);
  netbuf_mnt_udp6.buf = &sinaddr_mnt_udp6;

  bindaddr_mnt_udp6.qlen = SOMAXCONN;
  bindaddr_mnt_udp6.addr = netbuf_mnt_udp6;

  if(!__rpc_fd2sockinfo(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp, &si_mnt_udp6))
    {
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get socket info for udp6 socket rc=%d errno=%d", rc,
                 errno);
      return -1;
    }

  memset((char *)&sinaddr_mnt_tcp6, 0, sizeof(sinaddr_mnt_tcp6));
  sinaddr_mnt_tcp6.sin6_family = AF_INET6;
  sinaddr_mnt_tcp6.sin6_addr = in6addr_any;     /* All the interfaces on the machine are used */
  sinaddr_mnt_tcp6.sin6_port = htons(nfs_param.core_param.mnt_port);

  netbuf_mnt_tcp6.maxlen = sizeof(sinaddr_mnt_tcp6);
  netbuf_mnt_tcp6.len = sizeof(sinaddr_mnt_tcp6);
  netbuf_mnt_tcp6.buf = &sinaddr_mnt_tcp6;

  bindaddr_mnt_tcp6.qlen = SOMAXCONN;
  bindaddr_mnt_tcp6.addr = netbuf_mnt_tcp6;

  if(!__rpc_fd2sockinfo(nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp, &si_mnt_tcp6))
    {
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot get socket info for udp6 socket rc=%d errno=%d", rc,
                 errno);
      return -1;
    }

  if(bind(nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp,
          (struct sockaddr *)bindaddr_mnt_tcp6.addr.buf,
          (socklen_t) si_mnt_tcp6.si_alen) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind tcp6 socket");
      return -1;
    }

  if(bind(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
          (struct sockaddr *)bindaddr_mnt_tcp6.addr.buf,
          (socklen_t) si_mnt_udp6.si_alen) == -1)
    {
      LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_BIND, errno);

      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot bind udp6 socket");
      return -1;
    }
  /* FIXME do the nlm part here */
#endif                          /* _USE_TIRPC_IPV6 */

#ifdef _USE_TIRPC
  /* Unset the former registration to the rpcbind daemon */
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V2, netconfig_udpv4);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V3, netconfig_udpv4);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V4, netconfig_udpv4);

  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V1, netconfig_udpv4);
  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V3, netconfig_udpv4);

  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V2, netconfig_tcpv4);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V3, netconfig_tcpv4);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V4, netconfig_tcpv4);

  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V1, netconfig_tcpv4);
  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V3, netconfig_tcpv4);

  rpcb_unset(nfs_param.core_param.nlm_program, NLM4_VERS, netconfig_tcpv4);

  rpcb_unset(nfs_param.core_param.rquota_program, RQUOTAVERS, netconfig_tcpv4);
  rpcb_unset(nfs_param.core_param.rquota_program, EXT_RQUOTAVERS, netconfig_tcpv4);

  rpcb_unset(nfs_param.core_param.rquota_program, RQUOTAVERS, netconfig_udpv4);
  rpcb_unset(nfs_param.core_param.rquota_program, EXT_RQUOTAVERS, netconfig_udpv4);
#ifdef _USE_TIRPC_IPV6
  /* Unset rpcbind registration for IPv6 related protocols */
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V2, netconfig_udpv6);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V3, netconfig_udpv6);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V4, netconfig_udpv6);

  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V1, netconfig_udpv6);
  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V3, netconfig_udpv6);

  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V2, netconfig_tcpv6);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V3, netconfig_tcpv6);
  rpcb_unset(nfs_param.core_param.nfs_program, NFS_V4, netconfig_tcpv6);

  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V1, netconfig_tcpv6);
  rpcb_unset(nfs_param.core_param.mnt_program, MOUNT_V3, netconfig_tcpv6);

  rpcb_unset(nfs_param.core_param.rquota_program, RQUOTAVERS, netconfig_tcpv6);
  rpcb_unset(nfs_param.core_param.rquota_program, EXT_RQUOTAVERS, netconfig_tcpv6);

  rpcb_unset(nfs_param.core_param.rquota_program, RQUOTAVERS, netconfig_udpv6);
  rpcb_unset(nfs_param.core_param.rquota_program, EXT_RQUOTAVERS, netconfig_udpv6);
#endif                          /* _USE_TIRPC_IPV6 */
#else
  /* Unset the port mapper */
  pmap_unset(nfs_param.core_param.nfs_program, NFS_V2);
  pmap_unset(nfs_param.core_param.nfs_program, NFS_V3);
  pmap_unset(nfs_param.core_param.nfs_program, NFS_V4);

  pmap_unset(nfs_param.core_param.mnt_program, MOUNT_V1);
  pmap_unset(nfs_param.core_param.mnt_program, MOUNT_V3);

  pmap_unset(nfs_param.core_param.nlm_program, NLM4_VERS);

  pmap_unset(nfs_param.core_param.rquota_program, RQUOTAVERS);
  pmap_unset(nfs_param.core_param.rquota_program, EXT_RQUOTAVERS);
#endif

  /* Allocation of the SVCXPRT */
  LogEvent(COMPONENT_DISPATCH, "Allocation of the SVCXPRT");

  if((nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp =
#ifdef _USE_TIRPC
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate NFS/UDP SVCXPRT");
    return -1;
  }

#ifdef _USE_TIRPC_IPV6
  nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp->xp_netid =
      strdup(netconfig_udpv6->nc_netid);
  nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp->xp_tp =
      strdup(netconfig_udpv6->nc_device);
#endif                          /* _USE_TIRPC_IPV6 */

  if((nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp =
#ifdef _USE_TIRPC
      Svc_vc_create(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svctcp_create(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCTCP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate NFS/TCP SVCXPRT");
    return -1;
  }

#ifdef _USE_TIRPC_IPV6
  if(listen
     (nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp,
      (int)bindaddr_nfs_tcp6.qlen) != 0)
    {
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot listen on  NFS/TCPv6 SVCXPRT, errno=%u", errno);
      return -1;
    }

  nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp->xp_netid =
      strdup(netconfig_tcpv6->nc_netid);
  nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp->xp_tp =
      strdup(netconfig_tcpv6->nc_device);
#endif                          /* _USE_TIRPC_IPV6 */

  if((nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp =
#ifdef _USE_TIRPC
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate MNT/UDP SVCXPRT");
    return -1;
  }

#ifdef _USE_TIRPC_IPV6
  nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp->xp_netid =
      strdup(netconfig_udpv6->nc_netid);
  nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp->xp_tp =
      strdup(netconfig_udpv6->nc_device);
#endif                          /* _USE_TIRPC_IPV6 */

  if((nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp =
#ifdef _USE_TIRPC
      Svc_vc_create(nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svctcp_create(nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCTCP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate MNT/TCP SVCXPRT");
    return -1;
  }

#ifdef _USE_NLM
  if((nfs_param.worker_param.nfs_svc_data.xprt_nlm_udp =
#ifdef _USE_TIRPC
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif                          /* _USE_TIRPC */
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate NLM/UDP SVCXPRT");
    return -1;
  }

  if((nfs_param.worker_param.nfs_svc_data.xprt_nlm_tcp =
#ifdef _USE_TIRPC
      Svc_vc_create(nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svctcp_create(nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif                          /* _USE_TIRPC */
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCTCP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate NLM/TCP SVCXPRT");
    return -1;
  }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
  if((nfs_param.worker_param.nfs_svc_data.xprt_rquota_udp =
#ifdef _USE_TIRPC
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif                          /* _USE_TIRPC */
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate RQUOTA/UDP SVCXPRT");
    return -1;
  }

  if((nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp =
#ifdef _USE_TIRPC
      Svc_vc_create(nfs_param.worker_param.nfs_svc_data.socket_rquota_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
      Svctcp_create(nfs_param.worker_param.nfs_svc_data.socket_rquota_tcp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif                          /* _USE_TIRPC */
  {
    LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCTCP_CREATE, 0);
    LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot allocate RQUOTA/TCP SVCXPRT");
    return -1;
  }
#endif                          /* _USE_QUOTA */

#ifdef _USE_TIRPC_IPV6
  if(listen
     (nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp,
      (int)bindaddr_mnt_tcp6.qlen) != 0)
    {
      LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot listen on  MNT/TCPv6 SVCXPRT, errno=%u", errno);
      return -1;
    }

  nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp->xp_netid =
      strdup(netconfig_tcpv6->nc_netid);
  nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp->xp_tp =
      strdup(netconfig_tcpv6->nc_device);
#endif                          /* _USE_TIRPC_IPV6 */

#ifdef _USE_GSSRPC
  /* Acquire RPCSEC_GSS basis if needed */
  if(nfs_param.krb5_param.active_krb5 == TRUE)
    {
      if(Svcauth_gss_import_name(nfs_param.krb5_param.principal) != TRUE)
        {
          LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Could not import principal name %s into GSSAPI",
                     nfs_param.krb5_param.principal);
          exit(1);
        }
      else
        {
          LogEvent(COMPONENT_DISPATCH,
 "Successfully imported principal %s into GSSAPI",
                          nfs_param.krb5_param.principal);

          /* Trying to acquire a credentials for checking name's validity */
          if(!Svcauth_gss_acquire_cred())
            {
              LogCrit(COMPONENT_DISPATCH, "NFS EXIT: Cannot acquire credentials for principal %s",
                         nfs_param.krb5_param.principal);
              exit(1);
            }
          else
            {
              LogEvent(COMPONENT_DISPATCH,
                       "Principal %s is suitable for acquiring credentials",
                       nfs_param.krb5_param.principal);
            }
        }
    }
  /* if( nfs_param.krb5_param.active_krb5 == TRUE ) */
#endif                          /* _USE_GSSRPC */

#ifndef _NO_PORTMAPPER

  /* Perform all the RPC registration, for UDP and TCP, for NFS_V2, NFS_V3 and NFS_V4 */
  LogEvent(COMPONENT_DISPATCH,
           "Registration to the portmapper for NFS and MOUNT, on UDP and TCP");

/* only NFSv4 is supported for the FSAL_PROXY */
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  LogEvent(COMPONENT_DISPATCH, "Registering NFS V2/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
              nfs_param.core_param.nfs_program,
              NFS_V2, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
                   nfs_param.core_param.nfs_program,
                   NFS_V2, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V2 on UDP");
    }
  else
    nb_svc_nfs_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V3/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
              nfs_param.core_param.nfs_program,
              NFS_V3, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
                   nfs_param.core_param.nfs_program,
                   NFS_V3, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V3 on UDP");
    }
  else
    nb_svc_nfs_ok += 1;
#endif                          /* _USE_PROXY */

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V4/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
              nfs_param.core_param.nfs_program,
              NFS_V4, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
                   nfs_param.core_param.nfs_program,
                   NFS_V4, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V4 on UDP");
    }
  else
    {
      nb_svc_nfs_ok += 1;
#ifdef _USE_PROXY
      nb_svc_nfs_ok += 1;
#endif
    }

#ifdef _USE_TIRPC_IPV6
  LogEvent(COMPONENT_DISPATCH, "Registering NFS V2/UDPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
              nfs_param.core_param.nfs_program,
              NFS_V2, nfs_rpc_dispatch_dummy, netconfig_udpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V2 on UDPv6");
    }
  else
    nb_svc_nfs_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V3/UDPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
              nfs_param.core_param.nfs_program,
              NFS_V3, nfs_rpc_dispatch_dummy, netconfig_udpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V3 on UDPv6");
    }
  else
    nb_svc_nfs_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V4/UDPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_udp,
              nfs_param.core_param.nfs_program,
              NFS_V4, nfs_rpc_dispatch_dummy, netconfig_udpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V4 on UDPv6");
    }
  else
    nb_svc_nfs_ok += 1;

#endif

#ifndef _NO_TCP_REGISTER

/* only NFSv4 is supported for the FSAL_PROXY */
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V2/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
              nfs_param.core_param.nfs_program,
              NFS_V2, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
                   nfs_param.core_param.nfs_program,
                   NFS_V2, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V2 on TCP");
    }
  else
    nb_svc_nfs_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V3/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
              nfs_param.core_param.nfs_program,
              NFS_V3, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
                   nfs_param.core_param.nfs_program,
                   NFS_V3, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V3 on TCP");
    }
  else
    nb_svc_nfs_ok += 1;
#endif                          /* _USE_PROXY */

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V4/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
              nfs_param.core_param.nfs_program,
              NFS_V4, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
                   nfs_param.core_param.nfs_program,
                   NFS_V4, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V4 on TCP");
    }
  else
    {
      nb_svc_nfs_ok += 1;
#ifdef _USE_PROXY
      nb_svc_nfs_ok = 1;
#endif
    }

#ifdef _USE_TIRPC_IPV6
  LogEvent(COMPONENT_DISPATCH, "Registering NFS V2/TCPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
              nfs_param.core_param.nfs_program,
              NFS_V2, nfs_rpc_dispatch_dummy, netconfig_tcpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V2 on TCPv6");
    }
  else
    nb_svc_nfs_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V3/TCPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
              nfs_param.core_param.nfs_program,
              NFS_V3, nfs_rpc_dispatch_dummy, netconfig_tcpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V3 on TCPv6");
    }
  else
    nb_svc_nfs_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering NFS V4/TCPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp,
              nfs_param.core_param.nfs_program,
              NFS_V4, nfs_rpc_dispatch_dummy, netconfig_tcpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NFS V4 on TCPv6");
    }
  else
    nb_svc_nfs_ok += 1;

#endif                          /* _USE_TIRPC_IPV6 */

#endif

#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V1/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp,
              nfs_param.core_param.mnt_program,
              MOUNT_V1, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp,
                   nfs_param.core_param.mnt_program,
                   MOUNT_V1, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V1 on UDP");
    }
  else
    nb_svc_mnt_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V3/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp,
              nfs_param.core_param.mnt_program,
              MOUNT_V3, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp,
                   nfs_param.core_param.mnt_program,
                   MOUNT_V3, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V3 on UDP");
    }
  else
    nb_svc_mnt_ok += 1;
#endif                          /* _USE_PROXY */

#ifdef _USE_TIRPC_IPV6
  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V1/UDPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp,
              nfs_param.core_param.mnt_program,
              MOUNT_V1, nfs_rpc_dispatch_dummy, netconfig_udpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V1 on UDPv6");
    }
  else
    nb_svc_mnt_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V3/UDPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_udp,
              nfs_param.core_param.mnt_program,
              MOUNT_V3, nfs_rpc_dispatch_dummy, netconfig_udpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V3 on UDPv6");
    }
  else
    nb_svc_mnt_ok += 1;

#endif

#ifndef _NO_TCP_REGISTER

#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V1/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp,
              nfs_param.core_param.mnt_program,
              MOUNT_V1, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp,
                   nfs_param.core_param.mnt_program,
                   MOUNT_V1, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V1 on TCP");
    }
  else
    nb_svc_mnt_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V3/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp,
              nfs_param.core_param.mnt_program,
              MOUNT_V3, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp,
                   nfs_param.core_param.mnt_program,
                   MOUNT_V3, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V3 on TCP");
    }
  else
    nb_svc_mnt_ok += 1;
#else
  nb_svc_mnt_ok += 1;
#endif                          /* _USE_PROXY */

#ifdef _USE_TIRPC_IPV6
  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V1/TCPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp,
              nfs_param.core_param.mnt_program,
              MOUNT_V1, nfs_rpc_dispatch_dummy, netconfig_tcpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V1 on TCPv6");
    }
  else
    nb_svc_mnt_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering MOUNT V3/TCPv6");
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp,
              nfs_param.core_param.mnt_program,
              MOUNT_V3, nfs_rpc_dispatch_dummy, netconfig_tcpv6))
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register MOUNT V3 on TCPv6");
    }
  else
    nb_svc_mnt_ok += 1;

#endif

#ifdef _USE_NLM
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  LogEvent(COMPONENT_DISPATCH, "Registering NLM V4/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nlm_udp,
              nfs_param.core_param.nlm_program,
              NLM4_VERS, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nlm_udp,
                   nfs_param.core_param.nlm_program,
                   NLM4_VERS, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif                          /* _USE_TIRPC */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NLM V4 on UDP");
    }
  else
    nb_svc_nlm_ok += 1;
#else
  nb_svc_nlm_ok = 1;
#endif                          /* _USE_PROXY */

#endif                          /* USE_NLM */

#ifdef _USE_QUOTA
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )

  LogEvent(COMPONENT_DISPATCH, "Registering RQUOTA/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_rquota_udp,
              nfs_param.core_param.rquota_program,
              RQUOTAVERS, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_rquota_udp,
                   nfs_param.core_param.rquota_program,
                   RQUOTAVERS, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif                          /* _USE_TIRPC */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register RQUOTA v1 on UDP");
    }
  else
    nb_svc_rquota_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering RQUOTA/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp,
              nfs_param.core_param.rquota_program,
              RQUOTAVERS, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp,
                   nfs_param.core_param.rquota_program,
                   RQUOTAVERS, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif                          /* _USE_TIRPC */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register RQUOTA v1 on TCP");
    }
  else
    nb_svc_rquota_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering EXT_RQUOTA/UDP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_rquota_udp,
              nfs_param.core_param.rquota_program,
              EXT_RQUOTAVERS, nfs_rpc_dispatch_dummy, netconfig_udpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_rquota_udp,
                   nfs_param.core_param.rquota_program,
                   EXT_RQUOTAVERS, nfs_rpc_dispatch_dummy, IPPROTO_UDP))
#endif                          /* _USE_TIRPC */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register RQUOTA v2 on UDP");
    }
  else
    nb_svc_rquota_ok += 1;

  LogEvent(COMPONENT_DISPATCH, "Registering EXT_RQUOTA/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp,
              nfs_param.core_param.rquota_program,
              EXT_RQUOTAVERS, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp,
                   nfs_param.core_param.rquota_program,
                   EXT_RQUOTAVERS, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif                          /* _USE_TIRPC */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register RQUOTA v2 on TCP");
    }
  else
    nb_svc_rquota_ok += 1;

#else
  nb_svc_rquota_ok = 1;
#endif                          /* _USE_PROXY */

#endif                          /* USE_QUOTA */

#ifdef _USE_NLM
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  LogEvent(COMPONENT_DISPATCH, "Registering NLM V4/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_nlm_tcp,
              nfs_param.core_param.mnt_program,
              NLM4_VERS, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_nlm_tcp,
                   nfs_param.core_param.nlm_program,
                   NLM4_VERS, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif                          /* USE_NLM */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NLM V4 on TCP");
    }
  else
    nb_svc_nlm_ok += 1;
#else
  nb_svc_nlm_ok += 1;
#endif                          /* _USE_PROXY */
#else
  nb_svc_nlm_ok = 1;
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  LogEvent(COMPONENT_DISPATCH, "Registering NLM V4/TCP");
#ifdef _USE_TIRPC
  if(!svc_reg(nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp,
              nfs_param.core_param.mnt_program,
              NLM4_VERS, nfs_rpc_dispatch_dummy, netconfig_tcpv4))
#else
  if(!Svc_register(nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp,
                   nfs_param.core_param.rquota_program,
                   NLM4_VERS, nfs_rpc_dispatch_dummy, IPPROTO_TCP))
#endif                          /* USE_NLM */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVC_REGISTER, 0);
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Cannot register NLM V4 on TCP");
    }
  else
    nb_svc_rquota_ok += 1;
#else
  nb_svc_rquota_ok += 1;
#endif                          /* _USE_PROXY */
#else
  nb_svc_rquota_ok = 1;
#endif                          /* _USE_QUOTA */

#endif                          /* _NO_TCP_REGISTER */

  /* Were at least one NFS/MNT registration ok ? */
  if(nb_svc_nfs_ok == 0 || nb_svc_mnt_ok == 0 || nb_svc_nlm_ok == 0
     || nb_svc_rquota_ok == 0)
    {
      /* Not enough registration for servicing clients */
      LogCrit(COMPONENT_DISPATCH,
          "NFS_DISPATCHER: /!\\ | No registration to NFS and/or MOUNT programs were done...");

#if _USE_TIRPC
      freenetconfigent(netconfig_udpv4);
      freenetconfigent(netconfig_tcpv4);
#endif
      return 1;
    }
#if _USE_TIRPC
  freenetconfigent(netconfig_udpv4);
  freenetconfigent(netconfig_tcpv4);
#endif

#endif                          /* _NO_PORTMAPPER */

  return 0;
}                               /* nfs_Init_svc */

/**
 * Selects the smallest request queue,
 * whome the worker is ready and is not garbagging.
 */
static unsigned int select_worker_queue()
{
#define NO_VALUE_CHOOSEN  1000000
  unsigned int worker_index = NO_VALUE_CHOOSEN;
  unsigned int min_number_pending = NO_VALUE_CHOOSEN;

  unsigned int i;
  static unsigned int last;
  unsigned int cpt = 0;

  do
    {

      /* chose the smallest queue */

      for(i = (last + 1) % nfs_param.core_param.nb_worker, cpt = 0;
          cpt < nfs_param.core_param.nb_worker;
          cpt++, i = (i + 1) % nfs_param.core_param.nb_worker)
        {
          /* Choose only fully initialized workers and that does not gc */

          if((workers_data[i].gc_in_progress == FALSE)
             && (workers_data[i].is_ready == TRUE))
            {
              if(workers_data[i].pending_request->nb_entry < min_number_pending)
                {
                  worker_index = i;
                  min_number_pending = workers_data[i].pending_request->nb_entry;
                }
            }
          else if(!workers_data[i].is_ready)
            LogFullDebug(COMPONENT_DISPATCH, "worker thread #%u is not ready", i);
          else if(workers_data[i].gc_in_progress)
            LogFullDebug(COMPONENT_DISPATCH,
                         "worker thread #%u is doing garbage collection", i);
        }

    }
  while(worker_index == NO_VALUE_CHOOSEN);

  last = worker_index;

  return worker_index;

}                               /* select_worker_queue */

/**
 *
 * nfs_rpc_get_worker_index: Returns the index of the worker to be used
 *
 * @param mount_protocol_flag a flag (TRUE of FALSE) to tell if the worker is to be used for mount protocol
 *
 * @return the chosen worker index.
 *
 */
int nfs_rpc_get_worker_index(int mount_protocol_flag)
{
  int worker_index = -1;

#ifndef _NO_MOUNT_LIST
  if(mount_protocol_flag == TRUE)
    worker_index = 0;           /* worker #0 is dedicated to mount protocol */
  else
    worker_index = select_worker_queue();
#else
  /* choose a worker depending on its queue length */
  worker_index = select_worker_queue();
#endif

  return worker_index;
}                               /* nfs_rpc_get_worker_index */

/**
 * nfs_rpc_getreq: Do half of the work done by svc_getreqset.
 *
 * This function wait for an incoming ONC message by waiting on a 'select' statement. Then getting a request
 * it perform the authentication and extracts the RPC message for the related socket. It then find the less busy
 * worker (the one with the shortest pending queue) and put the msg in this queue.
 * 
 * @param readfds File Descriptor Set related to the socket used for RPC management.
 * 
 * @return Nothing (void function), but calls svcerr_* function to notify the client when an error occures. 
 *
 */
void nfs_rpc_getreq(fd_set * readfds, nfs_parameter_t * pnfs_para)
{
  enum xprt_stat stat;
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  register SVCXPRT *xprt;
  register int bit;
  register long mask, *maskp;
  register int sock;
  char *cred_area;
  struct sockaddr_in *pdead_caller = NULL;
  char dead_caller[MAXNAMLEN];

  LRU_entry_t *pentry = NULL;
  LRU_status_t status;
  nfs_request_data_t *pnfsreq = NULL;
  int worker_index;
  int mount_flag = FALSE;

  /* portable access to fds_bits field */
  maskp = __FDS_BITS(readfds);

  for(sock = 0; sock < FD_SETSIZE; sock += NFDBITS)
    {
      for(mask = *maskp++; bit = ffs(mask); mask ^= (1 << (bit - 1)))
        {
          /* sock has input waiting */
          xprt = Xports[sock + bit - 1];
          if(xprt == NULL)
            {
              /* But do we control sock? */
              LogCrit(COMPONENT_DISPATCH,
                      "CRITICAL ERROR: Incoherency found in Xports array");
              continue;
            }

          /* A few thread manage only mount protocol, check for this */
          if((nfs_param.worker_param.nfs_svc_data.socket_mnt_udp == sock + bit - 1) ||
             (nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp == sock + bit - 1))
            mount_flag = TRUE;
          else
            mount_flag = FALSE;

          /* Get a worker to do the job */
          if((worker_index = nfs_rpc_get_worker_index(mount_flag)) < 0)
            {
              LogCrit(COMPONENT_DISPATCH, "CRITICAL ERROR: Couldn't choose a worker ! Exiting...");
              exit(1);
            }
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
          LogFullDebug(COMPONENT_DISPATCH, "Use request from spool #%d, xprt->xp_sock=%d",
                       worker_index, xprt->xp_fd);
#else
          LogFullDebug(COMPONENT_DISPATCH, "Use request from spool #%d, xprt->xp_sock=%d",
                       worker_index, xprt->xp_sock);
#endif

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

          if(pnfsreq == NULL)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "CRITICAL ERROR: empty request pool for the chosen worker ! Exiting...");
              exit(0);
            }

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

          if(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp == sock + bit - 1)
            {
              /* This is a regular UDP connection */
              LogFullDebug(COMPONENT_DISPATCH, "A NFS UDP request");
              pnfsreq->xprt = pnfsreq->nfs_udp_xprt;
              pnfsreq->ipproto = IPPROTO_UDP;

              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
          else if(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp == sock + bit - 1)
            {
              LogFullDebug(COMPONENT_DISPATCH, "A MOUNT UDP request");
              pnfsreq->xprt = pnfsreq->mnt_udp_xprt;
              pnfsreq->ipproto = IPPROTO_UDP;

              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
#ifdef _USE_NLM
          else if(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp == sock + bit - 1)
            {
              LogFullDebug(COMPONENT_DISPATCH, "A NLM UDP request");
              pnfsreq->xprt = pnfsreq->nlm_udp_xprt;
              pnfsreq->ipproto = IPPROTO_UDP;
              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
          else if(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp == sock + bit - 1)
            {
              LogFullDebug(COMPONENT_DISPATCH, "A RQUOTA UDP request");
              pnfsreq->xprt = pnfsreq->rquota_udp_xprt;
              pnfsreq->ipproto = IPPROTO_UDP;
              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
#endif                          /* _USE_QUOTA */
          else if(nfs_param.worker_param.nfs_svc_data.socket_nfs_tcp == sock + bit - 1)
            {
              /* 
               * This is an initial tcp connection 
               * There is no RPC message, this is only a TCP connect.
               * In this case, the SVC_RECV does only produces a new connected socket (it does
               * just a call to accept and FD_SET)
               * there is no need of worker thread processing to be done
               */
              LogFullDebug(COMPONENT_DISPATCH,
                           "An initial NFS TCP request from a new client");
              pnfsreq->xprt = nfs_param.worker_param.nfs_svc_data.xprt_nfs_tcp;
              pnfsreq->ipproto = IPPROTO_TCP;

              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
          else if(nfs_param.worker_param.nfs_svc_data.socket_mnt_tcp == sock + bit - 1)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "An initial MOUNT TCP request from a new client");
              pnfsreq->xprt = nfs_param.worker_param.nfs_svc_data.xprt_mnt_tcp;
              pnfsreq->ipproto = IPPROTO_TCP;

              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
#ifdef _USE_NLM
          else if(nfs_param.worker_param.nfs_svc_data.socket_nlm_tcp == sock + bit - 1)
            {
              LogFullDebug(COMPONENT_DISPATCH, "An initial NLM request from a new client");
              pnfsreq->xprt = nfs_param.worker_param.nfs_svc_data.xprt_nlm_tcp;
              pnfsreq->ipproto = IPPROTO_TCP;

              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
          else if(nfs_param.worker_param.nfs_svc_data.socket_rquota_tcp == sock + bit - 1)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "An initial RQUOTA request from a new client");
              pnfsreq->xprt = nfs_param.worker_param.nfs_svc_data.xprt_rquota_tcp;
              pnfsreq->ipproto = IPPROTO_TCP;

              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
#endif                          /* _USE_QUOTA */
          else
            {
              /* This is a regular tcp request on an established connection, should be handle by a dedicated thread */
              LogFullDebug(COMPONENT_DISPATCH,
                           "A NFS TCP request from an already connected client");
              pnfsreq->tcp_xprt = xprt;
              pnfsreq->xprt = pnfsreq->tcp_xprt;
              pnfsreq->ipproto = IPPROTO_TCP;

              pnfsreq->status = SVC_RECV(pnfsreq->xprt, &(pnfsreq->msg));
            }
          LogFullDebug(COMPONENT_DISPATCH, "Status for SVC_RECV on socket %d is %d",
                       sock + bit - 1, pnfsreq->status);

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
                  if((pdead_caller = svc_getcaller(pnfsreq->xprt)) != NULL)
                    {
                      snprintf(dead_caller, MAXNAMLEN, "0x%x=%d.%d.%d.%d",
                               ntohl(pdead_caller->sin_addr.s_addr),
                               (ntohl(pdead_caller->sin_addr.s_addr) & 0xFF000000) >> 24,
                               (ntohl(pdead_caller->sin_addr.s_addr) & 0x00FF0000) >> 16,
                               (ntohl(pdead_caller->sin_addr.s_addr) & 0x0000FF00) >> 8,
                               (ntohl(pdead_caller->sin_addr.s_addr) & 0x000000FF));
                    }
                  else
#endif                          /* _USE_TIRPC */
                    strncpy(dead_caller, "unresolved", MAXNAMLEN);

#if defined( _USE_TIRPC ) || defined( _FREEBSD )
                  LogEvent(COMPONENT_DISPATCH, "A client disappeared... socket=%d, addr=%s",
                             pnfsreq->xprt->xp_fd, dead_caller);
                  if(Xports[pnfsreq->xprt->xp_fd] != NULL)
                    SVC_DESTROY(Xports[pnfsreq->xprt->xp_fd]);
#else
                  LogEvent(COMPONENT_DISPATCH, "A client disappeared... socket=%d, addr=%s",
                             pnfsreq->xprt->xp_sock, dead_caller);
                  if(Xports[pnfsreq->xprt->xp_sock] != NULL)
                    SVC_DESTROY(Xports[pnfsreq->xprt->xp_sock]);
#endif

                  P(workers_data[worker_index].request_pool_mutex);
                  RELEASE_PREALLOC(pnfsreq, workers_data[worker_index].request_pool,
                                   next_alloc);
                  V(workers_data[worker_index].request_pool_mutex);

                }
              else if(stat == XPRT_MOREREQS)
                {
                  LogDebug(COMPONENT_DISPATCH,
                           "Client on socket %d has status XPRT_MOREREQS",
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
                                  pnfsreq->xprt->xp_fd);
#else
                                  pnfsreq->xprt->xp_sock);
#endif
                }

              /* Release the entry */
              LogFullDebug(COMPONENT_DISPATCH,
                           "NFS DISPATCH: Invalidating entry with xprt_stat=%d", stat);
              workers_data[worker_index].passcounter += 1;
            }
          else
            {
              /* This should be used for UDP requests only, TCP request have dedicted management threads */
              LogFullDebug(COMPONENT_DISPATCH, "Awaking thread #%d", worker_index);

              P(workers_data[worker_index].mutex_req_condvar);
              P(workers_data[worker_index].request_pool_mutex);

              if((pentry =
                  LRU_new_entry(workers_data[worker_index].pending_request,
                                &status)) == NULL)
                {
                  V(workers_data[worker_index].mutex_req_condvar);
                  V(workers_data[worker_index].request_pool_mutex);
                  LogMajor(COMPONENT_DISPATCH,
                           "Error while inserting pending request to Thread #%d... exiting",
                           worker_index);
                  exit(1);
                }
              pentry->buffdata.pdata = (caddr_t) pnfsreq;
              pentry->buffdata.len = sizeof(*pnfsreq);

              if(pthread_cond_signal(&(workers_data[worker_index].req_condvar)) == -1)
                {
                  V(workers_data[worker_index].mutex_req_condvar);
                  V(workers_data[worker_index].request_pool_mutex);
                  LogCrit(COMPONENT_DISPATCH, "NFS DISPATCH: Cond signal failed for thr#%d , errno = %d",
                             worker_index, errno);
                  exit(1);
                }
              V(workers_data[worker_index].mutex_req_condvar);
              V(workers_data[worker_index].request_pool_mutex);
            }
        }
    }
}                               /* nfs_rpc_getreq */

/**
 *
 * clean_pending_request: cleans an entry in a nfs request LRU,
 *
 * cleans an entry in a nfs request LRU.
 *
 * @param pentry [INOUT] entry to be cleaned. 
 * @param addparam [IN] additional parameter used for cleaning.
 *
 * @return 0 if ok, other values mean an error.
 *
 */
int clean_pending_request(LRU_entry_t * pentry, void *addparam)
{
  nfs_request_data_t **preqnfspool = (nfs_request_data_t **) addparam;
  nfs_request_data_t *preqnfs = (nfs_request_data_t *) (pentry->buffdata.pdata);

  /* Send the entry back to the pool */
  RELEASE_PREALLOC(preqnfs, *preqnfspool, next_alloc);

  return 0;
}                               /* clean_pending_request */

/**
 *
 * print_pending_request: prints an entry related to a pending request in the LRU list.
 * 
 * prints an entry related to a pending request in the LRU list.
 *
 * @param data [IN] data stored in a LRU entry to be printed.
 * @param str [OUT] string used to store the result. 
 *
 * @return 0 if ok, other values mean an error.
 *
 */
int print_pending_request(LRU_data_t data, char *str)
{
  return snprintf(str, LRU_DISPLAY_STRLEN, "not implemented for now");
}                               /* print_pending_request */

/**
 * nfs_rpc_dispatcher_svc_run: the same as svc_run.
 *
 * The same as svc_run.
 * 
 * @param none
 * 
 * @return nothing (void function)
 *
 */

void rpc_dispatcher_svc_run(nfs_parameter_t * pnfs_param)
{
  fd_set readfdset;
  int rc = 0;

#ifdef _DEBUG_MEMLEAKS
  static int nb_iter_memleaks = 0;
#endif

  while(TRUE)
    {
      /* Always work on a copy of Svc_fdset */
      readfdset = Svc_fdset;

      /* Select on a fdset build with all socket used in NFS/RPC */
      LogDebug(COMPONENT_DISPATCH, "Waiting for incoming RPC requests");

      /* Do the select on the RPC fdset */
      rc = select(FD_SETSIZE, &readfdset, NULL, NULL, NULL);

      LogDebug(COMPONENT_DISPATCH, "Waiting for incoming RPC requests, after select rc=%d",
               rc);
      switch (rc)
        {
        case -1:
          if(errno == EBADF)
            {
              LogError(COMPONENT_DISPATCH, ERR_SYS, ERR_SELECT, errno);
              return;
            }
          break;

        case 0:
          continue;

        default:
          LogFullDebug(COMPONENT_DISPATCH, "NFS SVC RUN: request(s) received");
          nfs_rpc_getreq(&readfdset, pnfs_param);
          break;

        }                       /* switch */

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
}                               /* rpc_dispatcher_svc_run */

/**
 * rpc_dispatcher_thread: thread used for RPC dispatching.
 *
 * Thead used for RPC dispatching. It gets the requests and then spool it to one of the worker's LRU.
 * The worker chosen is the one with the smaller load (its LRU is the shorter one).
 * 
 * @param IndexArg the index for the worker thread (unused)
 * 
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *rpc_dispatcher_thread(void *Arg)
{
  int rc = 0;
  nfs_parameter_t *pnfs_param = (nfs_parameter_t *) Arg;

  SetNameFunction("dispatch_thr");

#ifndef _NO_BUDDY_SYSTEM
  /* Initialisation of the Buddy Malloc */
  LogEvent(COMPONENT_DISPATCH, "NFS DISPATCHER: Initialization of memory manager");
  if((rc = BuddyInit(&nfs_param.buddy_param_worker)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: Memory manager could not be initialized, exiting...");
      exit(1);
    }
#endif
  /* Calling dispatcher main loop */
  LogEvent(COMPONENT_DISPATCH, "NFS DISPATCHER: Entering nfs/rpc dispatcher");

  LogDebug(COMPONENT_DISPATCH, "NFS DISPATCHER: my pthread id is %p", (caddr_t) pthread_self());

  rpc_dispatcher_svc_run(pnfs_param);

  return NULL;
}                               /* rpc_dispatcher_thread */

/**
 * nfs_Init_request_data: Init the data associated with a pending request 
 *
 * This function is used to init the nfs_request_data for a worker. These data are used by the
 * worker for RPC processing.
 * 
 * @param param A structure of type nfs_worker_parameter_t with all the necessary information related to a worker
 * @param pdata Pointer to the data to be initialized.
 * 
 * @return 0 if successfull, -1 otherwise. 
 *
 */
int nfs_Init_request_data(nfs_request_data_t * pdata)
{
  pdata->ipproto = 0;

  /* Init the SVCXPRT for the tcp socket */
  /* The choice of the fd to be used here doesn't really matter, this fd will be overwrittem later 
   * when processing the request */
  pdata->tcp_xprt = NULL;

#ifdef _USE_TIRPC
  if((pdata->nfs_udp_xprt =
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
  if((pdata->nfs_udp_xprt =
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_nfs_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
      return -1;
    }

#ifdef _USE_TIRPC
  if((pdata->mnt_udp_xprt =
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
  if((pdata->mnt_udp_xprt =
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_mnt_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
      return -1;
    }

#ifdef _USE_NLM
#ifdef _USE_TIRPC
  if((pdata->nlm_udp_xprt =
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
  if((pdata->nlm_udp_xprt =
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_nlm_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif                          /* _USE_TIRPC */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
      return -1;
    }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
#ifdef _USE_TIRPC
  if((pdata->rquota_udp_xprt =
      Svc_dg_create(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp,
                    NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#else
  if((pdata->rquota_udp_xprt =
      Svcudp_bufcreate(nfs_param.worker_param.nfs_svc_data.socket_rquota_udp,
                       NFS_SEND_BUFFER_SIZE, NFS_RECV_BUFFER_SIZE)) == NULL)
#endif                          /* _USE_TIRPC */
    {
      LogError(COMPONENT_DISPATCH, ERR_RPC, ERR_SVCUDP_CREATE, 0);
      return -1;
    }
#endif                          /* _USE_QUOTA */

  pdata->xprt = NULL;

  return 0;
}                               /* nfs_Init_request_data */

/**
 * constructor_nfs_request_data_t: Constructor for a nfs_request_data_t structure
 *
 * This function is used to init the nfs_request_data for a worker. These data are used by the
 * worker for RPC processing.
 * 
 * @param ptr void pointer to the structure to be managed
 * 
 * @return nothing (void function) will exit the program if failed.
 *
 */

void constructor_nfs_request_data_t(void *ptr)
{
  if(nfs_Init_request_data(ptr) != 0)
    {
      LogCrit(COMPONENT_DISPATCH, "NFS INIT: Error initializing request data ");
      exit(1);
    }
}

/**
 * nfs_Init_gc_counter: Init the worker's gc counters.
 *
 * This functions is used to init a mutex and a counter associated with it, to keep track of the number of worker currently 
 * performing the garbagge collection.
 *
 * @param void No parameters
 * 
 * @return 0 if successfull, -1 otherwise.
 *
 */

int nfs_Init_gc_counter(void)
{
  pthread_mutexattr_t mutexattr;

  if(pthread_mutexattr_init(&mutexattr) != 0)
    return -1;

  if(pthread_mutex_init(&lock_nb_current_gc_workers, &mutexattr) != 0)
    return -1;

  nb_current_gc_workers = 0;

  return 0;                     /* Success */
}                               /* nfs_Init_gc_counter */
