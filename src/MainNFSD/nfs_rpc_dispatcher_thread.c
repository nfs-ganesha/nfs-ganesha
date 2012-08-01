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
#include <poll.h>
#include <assert.h>
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"
#include "nfs_tcb.h"

#ifndef _USE_TIRPC_IPV6
  #define P_FAMILY AF_INET
#else
  #define P_FAMILY AF_INET6
#endif

static pthread_mutex_t lock_worker_selection = PTHREAD_MUTEX_INITIALIZER;

/* TI-RPC event channels.  Each channel is a thread servicing an event
 * demultiplexer. */

struct rpc_evchan {
    uint32_t chan_id;
    pthread_t thread_id;
};

#define N_TCP_EVENT_CHAN  3 /* we don't really want to have too many, relative to the
                             * number of available cores. */
#define UDP_EVENT_CHAN    0 /* put udp on a dedicated channel */
#define TCP_RDVS_CHAN     1 /* accepts new tcp connections */
#define TCP_EVCHAN_0      2
#define N_EVENT_CHAN N_TCP_EVENT_CHAN + 2

static struct rpc_evchan rpc_evchan[N_EVENT_CHAN];

static u_int nfs_rpc_rdvs(SVCXPRT *xprt, SVCXPRT *newxprt, const u_int flags,
                          void *u_data);
static bool_t nfs_rpc_getreq_ng(SVCXPRT *xprt /*, int chan_id */);
static void nfs_rpc_free_xprt(SVCXPRT *xprt);

/**
 *
 * nfs_rpc_dispatch_dummy: Function never called, but the symbol is needed
 * for svc_register.
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

const char *tags[] = {
  "NFS",
  "MNT",
#ifdef _USE_NLM
  "NLM",
#endif
#ifdef _USE_RQUOTA
  "RQUOTA",
#endif
};

typedef struct proto_data
{
  struct sockaddr_in sinaddr;
#ifdef _USE_TIRPC_IPV6
  struct sockaddr_in6 sinaddr_udp6;
  struct sockaddr_in6 sinaddr_tcp6;
  struct netbuf netbuf_udp6;
  struct netbuf netbuf_tcp6;
  struct t_bind bindaddr_udp6;
  struct t_bind bindaddr_tcp6;
  struct __rpc_sockinfo si_udp6;
  struct __rpc_sockinfo si_tcp6;
#endif
} proto_data;

proto_data pdata[P_COUNT];

struct netconfig *netconfig_udpv4;
struct netconfig *netconfig_tcpv4;
#ifdef _USE_TIRPC_IPV6
struct netconfig *netconfig_udpv6;
struct netconfig *netconfig_tcpv6;
#endif

/* RPC Service Sockets and Transports */
int udp_socket[P_COUNT];
int tcp_socket[P_COUNT];
SVCXPRT *udp_xprt[P_COUNT];
SVCXPRT *tcp_xprt[P_COUNT];

/**
 * unregister: Unregister an RPC program.
 *
 */
static void unregister(const rpcprog_t prog, const rpcvers_t vers1, const rpcvers_t vers2)
{
  rpcvers_t vers;
  for(vers = vers1; vers <= vers2; vers++)
   {
     rpcb_unset(prog, vers, netconfig_udpv4);
     rpcb_unset(prog, vers, netconfig_tcpv4);
#ifdef _USE_TIRPC_IPV6
     rpcb_unset(prog, vers, netconfig_udpv6);
     rpcb_unset(prog, vers, netconfig_tcpv6);
#endif
   }
}

static void unregister_rpc(void)
{
  unregister(nfs_param.core_param.program[P_NFS], NFS_V2, NFS_V4);
  unregister(nfs_param.core_param.program[P_MNT], MOUNT_V1, MOUNT_V3);
#ifdef _USE_NLM
  unregister(nfs_param.core_param.program[P_NLM], 1, NLM4_VERS);
#endif
#ifdef _USE_RQUOTA
  unregister(nfs_param.core_param.program[P_RQUOTA], RQUOTAVERS, EXT_RQUOTAVERS);
#endif
}

#ifdef _USE_NLM
#define test_for_additional_nfs_protocols(p) \
  ((p != P_MNT && p != P_NLM) || \
  (nfs_param.core_param.core_options & (CORE_OPTION_NFSV2 | CORE_OPTION_NFSV3)) != 0)
#else
#define test_for_additional_nfs_protocols(p) \
  (p != P_MNT || \
  (nfs_param.core_param.core_options & (CORE_OPTION_NFSV2 | CORE_OPTION_NFSV3)) != 0)
#endif

/**
 * close_rpc_fd - close file descriptors used for RPC services so that restarting
 * the NFS server wont encounter issues of "Addres Already In Use" - this has
 * occured even though we set the SO_REUSEADDR option when restarting the server
 * with a single export (i.e.: a small config) & no logging at all, making the
 * restart very fast.
 * when closing a listening socket it will be closed immediately if no connection
 * is pending on it, hence drastically reducing the probability for trouble.
 */
static void close_rpc_fd()
{
    protos p;

    for(p = P_NFS; p < P_COUNT; p++) {
	if (udp_socket[p] != -1) {
	    close(udp_socket[p]);
	}
	if (tcp_socket[p] != -1) {
	    close(tcp_socket[p]);
	}
    }
}


void Create_udp(protos prot)
{
    udp_xprt[prot] = svc_dg_create(udp_socket[prot],
                                   nfs_param.core_param.max_send_buffer_size,
                                   nfs_param.core_param.max_recv_buffer_size);
    if(udp_xprt[prot] == NULL)
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot allocate %s/UDP SVCXPRT", tags[prot]);

    /* Hook xp_getreq */
    (void) SVC_CONTROL(udp_xprt[prot], SVCSET_XP_GETREQ, nfs_rpc_getreq_ng);

    /* Hook xp_free_xprt (finalize/free private data) */
    (void) SVC_CONTROL(udp_xprt[prot], SVCSET_XP_FREE_XPRT, nfs_rpc_free_xprt);

    /* Setup private data */
    (udp_xprt[prot])->xp_u1 = alloc_gsh_xprt_private(XPRT_PRIVATE_FLAG_REF);

    /* bind xprt to channel--unregister it from the global event
     * channel (if applicable) */
    (void) svc_rqst_evchan_reg(rpc_evchan[UDP_EVENT_CHAN].chan_id, udp_xprt[prot],
                               SVC_RQST_FLAG_XPRT_UREG);

    /* XXXX why are we doing this?  Is it also stale (see below)? */
#ifdef _USE_TIRPC_IPV6
    udp_xprt[prot]->xp_netid = gsh_strdup(netconfig_udpv6->nc_netid);
    udp_xprt[prot]->xp_tp    = gsh_strdup(netconfig_udpv6->nc_device);
#endif
}

void Create_tcp(protos prot)
{
#if 0
    /* XXXX By itself, non-block mode will currently stall, so, we probably
     * will remove this. */
    int maxrec = nfs_param.core_param.max_recv_buffer_size;
    rpc_control(RPC_SVC_CONNMAXREC_SET, &maxrec);
#endif

    tcp_xprt[prot] = svc_vc_create2(tcp_socket[prot],
                                    nfs_param.core_param.max_send_buffer_size,
                                    nfs_param.core_param.max_recv_buffer_size,
                                    SVC_VC_CREATE_FLAG_LISTEN);
    if(tcp_xprt[prot] == NULL)
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot allocate %s/TCP SVCXPRT", tags[prot]);

    /* bind xprt to channel--unregister it from the global event
     * channel (if applicable) */
    (void) svc_rqst_evchan_reg(rpc_evchan[TCP_RDVS_CHAN].chan_id,
                               tcp_xprt[prot], SVC_RQST_FLAG_XPRT_UREG);

    /* Hook xp_getreq */
    (void) SVC_CONTROL(tcp_xprt[prot], SVCSET_XP_GETREQ, nfs_rpc_getreq_ng);

    /* Hook xp_rdvs -- allocate new xprts to event channels */
    (void) SVC_CONTROL(tcp_xprt[prot], SVCSET_XP_RDVS, nfs_rpc_rdvs);

    /* Hook xp_free_xprt (finalize/free private data) */
    (void) SVC_CONTROL(tcp_xprt[prot], SVCSET_XP_FREE_XPRT, nfs_rpc_free_xprt);

    /* Setup private data */
    (tcp_xprt[prot])->xp_u1 = alloc_gsh_xprt_private(XPRT_PRIVATE_FLAG_REF);

/* XXXX the following code cannot compile (socket, binadaddr_udp6 are gone)
 * (Matt) */
#ifdef _USE_TIRPC_IPV6
    if(listen(tcp_socket[prot], pdata[prot].bindaddr_udp6.qlen) != 0)
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot listen on  %s/TCPv6 SVCXPRT, errno=%u (%s)",
                 tags[prot], errno, strerror(errno));
    /* XXX what if we errored above? */
    tcp_xprt[prot]->xp_netid = gsh_strdup(netconfig_tcpv6->nc_netid);
    tcp_xprt[prot]->xp_tp    = gsh_strdup(netconfig_tcpv6->nc_device);
#endif
}

/**
 * Create_SVCXPRTs: Create the SVCXPRT for each protocol in use.
 *
 */
void Create_SVCXPRTs(void)
{
  protos p;

  LogFullDebug(COMPONENT_DISPATCH, "Allocation of the SVCXPRT");
  for(p = P_NFS; p < P_COUNT; p++)
    if(test_for_additional_nfs_protocols(p))
      {
        Create_udp(p);
        Create_tcp(p);
      }
}

/**
 * Bind_sockets: bind the udp and tcp sockets.
 *
 */
void Bind_sockets(void)
{
  protos p;

  for(p = P_NFS; p < P_COUNT; p++)
    if(test_for_additional_nfs_protocols(p))
      {
        proto_data *pdatap = &pdata[p];
#ifndef _USE_TIRPC_IPV6
        memset(&pdatap->sinaddr, 0, sizeof(pdatap->sinaddr));
        pdatap->sinaddr.sin_family      = AF_INET;
        pdatap->sinaddr.sin_addr.s_addr = nfs_param.core_param.bind_addr.sin_addr.s_addr;
        pdatap->sinaddr.sin_port        = htons(nfs_param.core_param.port[p]);

        if(bind(udp_socket[p],
                (struct sockaddr *)&pdatap->sinaddr, sizeof(pdatap->sinaddr)) == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot bind %s udp socket, error %d (%s)",
                   tags[p], errno, strerror(errno));

        if(bind(tcp_socket[p],
                (struct sockaddr *)&pdatap->sinaddr, sizeof(pdatap->sinaddr)) == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot bind %s tcp socket, error %d (%s)",
                   tags[p], errno, strerror(errno));
#else
        memset(&pdatap->sinaddr_udp6, 0, sizeof(pdatap->sinaddr_udp6));
        pdatap->sinaddr_udp6.sin6_family = AF_INET6;
        pdatap->sinaddr_udp6.sin6_addr   = in6addr_any;     /* All the interfaces on the machine are used */
        pdatap->sinaddr_udp6.sin6_port   = htons(nfs_param.core_param.port[p]);

        pdatap->netbuf_udp6.maxlen = sizeof(pdatap->sinaddr_udp6);
        pdatap->netbuf_udp6.len    = sizeof(pdatap->sinaddr_udp6);
        pdatap->netbuf_udp6.buf    = &pdatap->sinaddr_udp6;

        pdatap->bindaddr_udp6.qlen = SOMAXCONN;
        pdatap->bindaddr_udp6.addr = pdatap->netbuf_udp6;

        if(!__rpc_fd2sockinfo(udp_socket[p], &pdatap->si_udp6))
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot get %s socket info for udp6 socket errno=%d (%s)",
                   tags[p], errno, strerror(errno));

        if(bind(udp_socket[p],
                (struct sockaddr *)pdatap->bindaddr_udp6.addr.buf,
                (socklen_t) pdatap->si_udp6.si_alen) == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot bind %s udp6 socket, error %d (%s)",
                   tags[p], errno, strerror(errno));

        memset(&pdatap->sinaddr_tcp6, 0, sizeof(pdatap->sinaddr_tcp6));
        pdatap->sinaddr_tcp6.sin6_family = AF_INET6;
        pdatap->sinaddr_tcp6.sin6_addr   = in6addr_any;     /* All the interfaces on the machine are used */
        pdatap->sinaddr_tcp6.sin6_port   = htons(nfs_param.core_param.port[p]);

        pdatap->netbuf_tcp6.maxlen = sizeof(pdatap->sinaddr_tcp6);
        pdatap->netbuf_tcp6.len    = sizeof(pdatap->sinaddr_tcp6);
        pdatap->netbuf_tcp6.buf    = &pdatap->sinaddr_tcp6;

        pdatap->bindaddr_tcp6.qlen = SOMAXCONN;
        pdatap->bindaddr_tcp6.addr = pdatap->netbuf_tcp6;

        if(!__rpc_fd2sockinfo(tcp_socket[p], &pdatap->si_tcp6))
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot get %s socket info for tcp6 socket errno=%d (%s)",
                   tags[p], errno, strerror(errno));

        if(bind(tcp_socket[p],
                (struct sockaddr *)pdatap->bindaddr_tcp6.addr.buf,
                (socklen_t) pdatap->si_tcp6.si_alen) == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot bind %s tcp6 socket, error %d (%s)",
                   tags[p], errno, strerror(errno));
#endif
      }
}

void Clean_RPC(void)
{
  //TODO: consider the need to call Svc_dg_destroy for UDP & ?? for TCP based services
  unregister_rpc();
  close_rpc_fd();
}

cleanup_list_element clean_rpc = {NULL, Clean_RPC};

#define UDP_REGISTER(prot, vers, netconfig) \
    svc_register(udp_xprt[prot], nfs_param.core_param.program[prot], (u_long) vers, \
                 nfs_rpc_dispatch_dummy, IPPROTO_UDP)

#define TCP_REGISTER(prot, vers, netconfig) \
    svc_register(tcp_xprt[prot], nfs_param.core_param.program[prot], (u_long) vers, \
                 nfs_rpc_dispatch_dummy, IPPROTO_TCP)

void Register_program(protos prot, int flag, int vers)
{
  if((nfs_param.core_param.core_options & flag) != 0)
    {
      LogInfo(COMPONENT_DISPATCH,
              "Registering %s V%d/UDP",
              tags[prot], (int)vers);

      /* XXXX fix svc_register! */
      if(!UDP_REGISTER(prot, vers, netconfig_udpv4))
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot register %s V%d on UDP",
                 tags[prot], (int)vers);

#ifdef _USE_TIRPC_IPV6
      LogInfo(COMPONENT_DISPATCH,
              "Registering %s V%d/UDPv6",
              tags[prot], (int)vers);
      if(!UDP_REGISTER(prot, vers, netconfig_udpv6))
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot register %s V%d on UDPv6",
                 tags[prot], (int)vers);
#endif

#ifndef _NO_TCP_REGISTER
      LogInfo(COMPONENT_DISPATCH,
              "Registering %s V%d/TCP",
              tags[prot], (int)vers);

      if(!TCP_REGISTER(prot, vers, netconfig_tcpv4))
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot register %s V%d on TCP",
                 tags[prot], (int)vers);

#ifdef _USE_TIRPC_IPV6
      LogInfo(COMPONENT_DISPATCH,
              "Registering %s V%d/TCPv6",
              tags[prot], (int)vers);
      if(!TCP_REGISTER(prot, vers, netconfig_tcpv6))
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot register %s V%d on TCPv6",
                 tags[prot], (int)vers);
#endif /* _USE_TIRPC_IPV6 */
#endif /* _NO_TCP_REGISTER */
    }
}

/**
 * nfs_Init_svc: Init the svc descriptors for the nfs daemon.
 *
 * Perform all the required initialization for the RPC subsystem and event
 * channels.
 *
 * @param attr_thr  pointer to a set of pre-initialized pthread attributes that
 * should be used for new threads
 */
void nfs_Init_svc()
{
    protos p;
    svc_init_params svc_params;
    int ix, code __attribute__((unused)) = 0;
    int one = 1;

    LogDebug(COMPONENT_DISPATCH, "NFS INIT: Core options = %d",
            nfs_param.core_param.core_options);

    LogInfo(COMPONENT_DISPATCH, "NFS INIT: using TIRPC");

    /* New TI-RPC package init function */
    svc_params.flags = SVC_INIT_EPOLL; /* use EPOLL event mgmt */
    svc_params.flags |= SVC_INIT_NOREG_XPRTS; /* don't call xprt_register */
    svc_params.max_connections = nfs_param.core_param.nb_max_fd;
    svc_params.max_events = 1024; /* length of epoll event queue */

    svc_init(&svc_params);

  /* Redirect TI-RPC allocators, log channel */
  if (!tirpc_control(TIRPC_SET_WARNX, (warnx_t) rpc_warnx))
      LogCrit(COMPONENT_INIT, "Failed redirecting TI-RPC __warnx");

#define TIRPC_SET_ALLOCATORS 0
#if TIRPC_SET_ALLOCATORS
  if (!tirpc_control(TIRPC_SET_MALLOC, (mem_alloc_t) gsh_malloc))
      LogCrit(COMPONENT_INIT, "Failed redirecting TI-RPC alloc");

  if (!tirpc_control(TIRPC_SET_MEM_FREE, (mem_free_t) gsh_free_size))
      LogCrit(COMPONENT_INIT, "Failed redirecting TI-RPC mem_free");

  if (!tirpc_control(TIRPC_SET_FREE, (std_free_t) gsh_free))
      LogCrit(COMPONENT_INIT, "Failed redirecting TI-RPC __free");
#endif /* TIRPC_SET_ALLOCATORS */

    for (ix = 0; ix < N_EVENT_CHAN; ++ix) {
        rpc_evchan[ix].chan_id = 0;
        if ((code = svc_rqst_new_evchan(&rpc_evchan[ix].chan_id, NULL /* u_data */,
                                        SVC_RQST_FLAG_NONE)))
            LogFatal(COMPONENT_DISPATCH,
                     "Cannot create TI-RPC event channel (%d, %d)", ix, code);
        /* XXX bail?? */
    }

  /* Get the netconfig entries from /etc/netconfig */
    if((netconfig_udpv4 = (struct netconfig *)getnetconfigent("udp")) == NULL)
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot get udp netconfig, cannot get a entry for udp in netconfig file. Check file /etc/netconfig...");

    /* Get the netconfig entries from /etc/netconfig */
    if((netconfig_tcpv4 = (struct netconfig *)getnetconfigent("tcp")) == NULL)
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot get tcp netconfig, cannot get a entry for tcp in netconfig file. Check file /etc/netconfig...");

    /* A short message to show that /etc/netconfig parsing was a success */
    LogFullDebug(COMPONENT_DISPATCH, "netconfig found for UDPv4 and TCPv4");

#ifdef _USE_TIRPC_IPV6
    LogInfo(COMPONENT_DISPATCH, "NFS INIT: Using IPv6");

    /* Get the netconfig entries from /etc/netconfig */
    if((netconfig_udpv6 = (struct netconfig *)getnetconfigent("udp6")) == NULL)
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot get udp6 netconfig, cannot get a entry for udp6 in netconfig file. Check file /etc/netconfig...");

  /* Get the netconfig entries from /etc/netconfig */
    if((netconfig_tcpv6 = (struct netconfig *)getnetconfigent("tcp6")) == NULL)
        LogFatal(COMPONENT_DISPATCH,
                 "Cannot get tcp6 netconfig, cannot get a entry for tcp in netconfig file. Check file /etc/netconfig...");

    /* A short message to show that /etc/netconfig parsing was a success */
    LogFullDebug(COMPONENT_DISPATCH, "netconfig found for UDPv6 and TCPv6");
#endif

  /* Allocate the UDP and TCP sockets for the RPC */
  LogFullDebug(COMPONENT_DISPATCH, "Allocation of the sockets");
  for(p = P_NFS; p < P_COUNT; p++)
    if(test_for_additional_nfs_protocols(p))
      {
        /* Initialize all the sockets to -1 because it makes some code later easier */
        udp_socket[p] = -1;
        tcp_socket[p] = -1;

        udp_socket[p] = socket(P_FAMILY, SOCK_DGRAM, IPPROTO_UDP);

        if(udp_socket[p] == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot allocate a udp socket for %s, error %d (%s)",
                   tags[p], errno, strerror(errno));

        tcp_socket[p] = socket(P_FAMILY, SOCK_STREAM, IPPROTO_TCP);

        if(tcp_socket[p] == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot allocate a tcp socket for %s, error %d (%s)",
                   tags[p], errno, strerror(errno));

        /* Use SO_REUSEADDR in order to avoid wait the 2MSL timeout */
        if(setsockopt(udp_socket[p],
                      SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
          LogFatal(COMPONENT_DISPATCH,
                   "Bad udp socket options for %s, error %d (%s)",
                   tags[p], errno, strerror(errno));

        if(setsockopt(tcp_socket[p],
                      SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
          LogFatal(COMPONENT_DISPATCH,
                   "Bad tcp socket options for %s, error %d (%s)",
                   tags[p], errno, strerror(errno));

        /* We prefer using non-blocking socket in the specific case */
        if(fcntl(udp_socket[p], F_SETFL, FNDELAY) == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot set udp socket for %s as non blocking, error %d (%s)",
                   tags[p], errno, strerror(errno));
      }

  socket_setoptions(tcp_socket[P_NFS]);

  if((nfs_param.core_param.core_options & (CORE_OPTION_NFSV2 | CORE_OPTION_NFSV3)) != 0)
    {
#ifdef _USE_NLM
     /* Some log that can be useful when debug ONC/RPC and RPCSEC_GSS matter */
     LogDebug(COMPONENT_DISPATCH, "Socket numbers are: nfs_udp=%u  nfs_tcp=%u "
              "mnt_udp=%u  mnt_tcp=%u nlm_tcp=%u nlm_udp=%u",
              udp_socket[P_NFS],
              tcp_socket[P_NFS],
              udp_socket[P_MNT],
              tcp_socket[P_MNT],
              udp_socket[P_NLM],
              tcp_socket[P_NLM]);
#else
      /* Some log that can be useful when debug ONC/RPC and RPCSEC_GSS matter */
      LogDebug(COMPONENT_DISPATCH, "Socket numbers are: nfs_udp=%u  nfs_tcp=%u "
               "mnt_udp=%u  mnt_tcp=%u",
               udp_socket[P_NFS],
               tcp_socket[P_NFS],
               udp_socket[P_MNT],
               tcp_socket[P_MNT]);
#endif                          /* USE_NLM */
    }
  else
    {
      /* Some log that can be useful when debug ONC/RPC and RPCSEC_GSS matter */
      LogDebug(COMPONENT_DISPATCH, "Socket numbers are: nfs_udp=%u  nfs_tcp=%u",
               udp_socket[P_NFS],
               tcp_socket[P_NFS]);
    }

#ifdef _USE_RQUOTA
  /* Some log that can be useful when debug ONC/RPC and RPCSEC_GSS matter */
  LogDebug(COMPONENT_DISPATCH,
           "Socket numbers are: rquota_udp=%u  rquota_tcp=%u",
           udp_socket[P_RQUOTA],
           tcp_socket[P_RQUOTA]) ;
#endif

  /* Bind the tcp and udp sockets */
  Bind_sockets();

  /* Unregister from portmapper/rpcbind */
  unregister_rpc();
  RegisterCleanup(&clean_rpc);

  /* Set up well-known xprt handles */
  Create_SVCXPRTs();

#ifdef _HAVE_GSSAPI
  /* Acquire RPCSEC_GSS basis if needed */
  if(nfs_param.krb5_param.active_krb5 == TRUE)
    {
      if(Svcauth_gss_import_name(nfs_param.krb5_param.svc.principal) != TRUE)
        {
          LogFatal(COMPONENT_DISPATCH,
                   "Could not import principal name %s into GSSAPI",
                   nfs_param.krb5_param.svc.principal);
        }
      else
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Successfully imported principal %s into GSSAPI",
                  nfs_param.krb5_param.svc.principal);

          /* Trying to acquire a credentials for checking name's validity */
          if(!Svcauth_gss_acquire_cred())
            LogCrit(COMPONENT_DISPATCH,
                     "Cannot acquire credentials for principal %s",
                     nfs_param.krb5_param.svc.principal);
          else
            LogDebug(COMPONENT_DISPATCH,
                    "Principal %s is suitable for acquiring credentials",
                    nfs_param.krb5_param.svc.principal);
        }
    }
#endif                          /* _HAVE_GSSAPI */

#ifndef _NO_PORTMAPPER
  /* Perform all the RPC registration, for UDP and TCP, for NFS_V2, NFS_V3 and NFS_V4 */
  Register_program(P_NFS, CORE_OPTION_NFSV2, NFS_V2);
  Register_program(P_NFS, CORE_OPTION_NFSV3, NFS_V3);
  Register_program(P_NFS, CORE_OPTION_NFSV4, NFS_V4);
  Register_program(P_MNT, (CORE_OPTION_NFSV2 | CORE_OPTION_NFSV3), MOUNT_V1);
  Register_program(P_MNT, CORE_OPTION_NFSV3, MOUNT_V3);
#ifdef _USE_NLM
  Register_program(P_NLM, CORE_OPTION_NFSV3, NLM4_VERS);
#endif                          /* USE_NLM */
#ifdef _USE_RQUOTA
  Register_program(P_RQUOTA, CORE_OPTION_ALL_VERS, RQUOTAVERS);
  Register_program(P_RQUOTA, CORE_OPTION_ALL_VERS, EXT_RQUOTAVERS);
#endif                          /* USE_QUOTA */
#endif                          /* _NO_PORTMAPPER */

}                               /* nfs_Init_svc */

/*
 * Start service threads.
 */
void nfs_rpc_dispatch_threads(pthread_attr_t *attr_thr)
{
    int ix, code = 0;

    /* Start event channel service threads */
    for (ix = 0; ix < N_EVENT_CHAN; ++ix) {
        if((code = pthread_create(&rpc_evchan[ix].thread_id,
                                  attr_thr,
                                  rpc_dispatcher_thread,
                                  (void *) &rpc_evchan[ix].chan_id)) != 0) {
            LogFatal(COMPONENT_THREAD,
                   "Could not create rpc_dispatcher_thread #%u, error = %d (%s)",
                     ix, errno, strerror(errno));
        }
    }
    LogInfo(COMPONENT_THREAD,
             "%d rpc dispatcher threads were started successfully",
             N_EVENT_CHAN); 
}

/*
 * Rendezvous callout.  This routine will be called by TI-RPC after newxprt
 * has been accepted.
 *
 * Register newxprt on a TCP event channel.  Balancing events/channels could
 * become involved.  To start with, just cycle through them as new connections
 * are accepted.
 */
static u_int nfs_rpc_rdvs(SVCXPRT *xprt, SVCXPRT *newxprt, const u_int flags,
                          void *u_data)
{
    static uint32_t next_chan = TCP_EVCHAN_0;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    uint32_t tchan;

    pthread_mutex_lock(&mtx);

    tchan = next_chan;
    assert((next_chan >= TCP_EVCHAN_0) && (next_chan < N_EVENT_CHAN));
    if (++next_chan >= N_EVENT_CHAN)
        next_chan = TCP_EVCHAN_0;

    /* setup private data (freed when xprt is destroyed) */
    newxprt->xp_u1 = alloc_gsh_xprt_private(XPRT_PRIVATE_FLAG_REF);

    pthread_mutex_unlock(&mtx);

    (void) svc_rqst_evchan_reg(rpc_evchan[tchan].chan_id, newxprt,
                               SVC_RQST_FLAG_NONE);

    return (0);
}

static void nfs_rpc_free_xprt(SVCXPRT *xprt)
{
    if (xprt->xp_u1)
        free_gsh_xprt_private(xprt->xp_u1);
}

/**
 * Selects the smallest request queue,
 * whome the worker is ready and is not garbagging.
 */

/* PhD: Please note that I renamed this function, added
 * it prototype to include/nfs_core.h and removed its "static" tag.
 * This is done to share this code with the 9P implementation */

static inline worker_available_rc
worker_available(unsigned long worker_index, unsigned int avg_number_pending)
{
  worker_available_rc rc = WORKER_AVAILABLE;
  P(workers_data[worker_index].wcb.tcb_mutex);
  switch(workers_data[worker_index].wcb.tcb_state)
    {
      case STATE_AWAKE:
      case STATE_AWAKEN:
        /* Choose only fully initialized workers and that does not gc. */
        if(workers_data[worker_index].wcb.tcb_ready == FALSE)
          {
            LogFullDebug(COMPONENT_THREAD,
                         "worker thread #%lu is not ready", worker_index);
            rc = WORKER_PAUSED;
          }
        else if(workers_data[worker_index].gc_in_progress == TRUE)
          {
            LogFullDebug(COMPONENT_THREAD,
                         "worker thread #%lu is doing garbage collection", worker_index);
            rc = WORKER_GC;
          }
        else if(workers_data[worker_index].pending_request_len >= avg_number_pending)
          {
            rc = WORKER_BUSY;
          }
        break;

      case STATE_STARTUP:
      case STATE_PAUSE:
      case STATE_PAUSED:
        rc = WORKER_ALL_PAUSED;
        break;

      case STATE_EXIT:
        rc = WORKER_EXIT;
        break;
    }
  V(workers_data[worker_index].wcb.tcb_mutex);

  return rc;
}

unsigned int
nfs_core_select_worker_queue(unsigned int avoid_index)
{
  #define NO_VALUE_CHOOSEN  1000000
  unsigned int worker_index = NO_VALUE_CHOOSEN;
  unsigned int avg_number_pending = NO_VALUE_CHOOSEN;
  unsigned int total_number_pending = 0;
  unsigned int i;
  unsigned int cpt = 0;

  static unsigned int counter;
  static unsigned int last;
  worker_available_rc rc_worker;

  P(lock_worker_selection);
  counter++;

  /* Calculate the average queue length if counter is bigger than configured value. */
  if(counter > nfs_param.core_param.nb_call_before_queue_avg)
    {
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        {
          total_number_pending += workers_data[i].pending_request_len;
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
      /* Avoid worker at avoid_index (provided to permit a worker thread to avoid
       * dispatching work to itself). */
      if (i == avoid_index)
          continue;

      /* Choose only fully initialized workers and that does not gc. */
      rc_worker = worker_available(i, avg_number_pending);
      if(rc_worker == WORKER_AVAILABLE)
      {
        worker_index = i;
        break;
      }
      else if(rc_worker == WORKER_ALL_PAUSED)
      {
        /* Wait for the threads to awaken */
        wait_for_threads_to_awaken();
      }
      else if(rc_worker == WORKER_EXIT)
      {
        /* do nothing */
      }
    } /* for */

  if(worker_index == NO_VALUE_CHOOSEN)
    worker_index = (last + 1) % nfs_param.core_param.nb_worker;

  last = worker_index;

  V(lock_worker_selection);

  return worker_index;

} /* nfs_core_select_worker_queue */

/**
 * nfs_rpc_get_nfsreq: get a request frame (call or svc request)
 */
request_data_t *
nfs_rpc_get_nfsreq(nfs_worker_data_t *worker, uint32_t flags)
{
    request_data_t *nfsreq = NULL;

    nfsreq = pool_alloc(request_pool, NULL);

    return (nfsreq);
}

process_status_t
dispatch_rpc_subrequest(nfs_worker_data_t *mydata,
                        request_data_t *onfsreq)
{
  char *cred_area;
  struct rpc_msg *msg;
  struct svc_req *req;
  request_data_t *nfsreq = NULL;
  unsigned int worker_index;
  process_status_t rc = PROCESS_DONE;

  /* choose a worker who is not us */
  worker_index = nfs_core_select_worker_queue(mydata->worker_index);

  LogDebug(COMPONENT_DISPATCH,
           "Use request from Worker Thread #%u's pool, xprt->xp_fd=%d, "
           "thread has %d pending requests",
           worker_index, onfsreq->r_u.nfs->xprt->xp_fd,
           workers_data[worker_index].pending_request_len);

  /* Get a nfsreq from the worker's pool */
  nfsreq = pool_alloc(request_pool, NULL);

  if(nfsreq == NULL)
    {
      LogMajor(COMPONENT_DISPATCH,
               "Unable to allocate request. Exiting...");
      Fatal();
    }

  /* Set the request as NFS already-read */
  nfsreq->rtype = NFS_REQUEST;

  /* tranfer onfsreq */
  nfsreq->r_u.nfs = onfsreq->r_u.nfs;

  /* And fixup onfsreq */
  onfsreq->r_u.nfs = pool_alloc(request_data_pool, NULL);

  if(onfsreq->r_u.nfs == NULL)
    {
      LogMajor(COMPONENT_DISPATCH,
               "Empty request data pool! Exiting...");
      Fatal();
    }

  /* Set up cred area */
  cred_area = onfsreq->r_u.nfs->cred_area;
  req = &(onfsreq->r_u.nfs->req);
  msg = &(onfsreq->r_u.nfs->msg);

  msg->rm_call.cb_cred.oa_base = cred_area;
  msg->rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
  req->rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);

  /* Set up xprt */
  onfsreq->r_u.nfs->xprt = nfsreq->r_u.nfs->xprt;
  req->rq_xprt = onfsreq->r_u.nfs->xprt;

  /* count as 1 ref */
  gsh_xprt_ref(req->rq_xprt, XPRT_PRIVATE_FLAG_LOCKED);

  /* Hand it off */
  DispatchWorkNFS(nfsreq, worker_index);

  return (rc);
}

/**
 * process_rpc_request: process an RPC request.
 *
 */
process_status_t dispatch_rpc_request(SVCXPRT *xprt)
{
  char *cred_area;
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  request_data_t *nfsreq = NULL;
  unsigned int worker_index;
  process_status_t rc = PROCESS_DONE;

  /* A few thread manage only mount protocol, check for this */

  /* Get a worker to do the job */
#ifndef _NO_MOUNT_LIST
  if((udp_socket[P_MNT] == xprt->xp_fd) ||
     (tcp_socket[P_MNT] == xprt->xp_fd))
    {
      /* worker #0 is dedicated to mount protocol */
      worker_index = 0;
    }
  else
#endif
    {
       /* choose a worker depending on its queue length */
       worker_index = nfs_core_select_worker_queue( WORKER_INDEX_ANY );
    }

  LogFullDebug(COMPONENT_DISPATCH,
               "Use request from Worker Thread #%u's pool, xprt->xp_fd=%d, thread "
               "has %d pending requests",
               worker_index, xprt->xp_fd,
               workers_data[worker_index].pending_request_len);

  /* Get a nfsreq from the worker's pool */
  nfsreq = pool_alloc(request_pool, NULL);

  if(nfsreq == NULL)
    {
      LogMajor(COMPONENT_DISPATCH,
               "Unable to allocate request.  Exiting...");
      Fatal();
    }

  /* Set the request as NFS with xprt hand-off */
  nfsreq->rtype = NFS_REQUEST_LEADER ;

  nfsreq->r_u.nfs = pool_alloc(request_data_pool, NULL);
  if(nfsreq->r_u.nfs == NULL)
    {
      LogMajor(COMPONENT_DISPATCH,
               "Unable to allocate request data.  Exiting...");
      Fatal();
    }

  /* Set up cred area */
  cred_area = nfsreq->r_u.nfs->cred_area;
  preq = &(nfsreq->r_u.nfs->req);
  pmsg = &(nfsreq->r_u.nfs->msg);

  pmsg->rm_call.cb_cred.oa_base = cred_area;
  pmsg->rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
  preq->rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);

  /* Set up xprt */
  nfsreq->r_u.nfs->xprt = xprt;
  preq->rq_xprt = xprt;

  /* Count as 1 ref */
  gsh_xprt_ref(xprt, XPRT_PRIVATE_FLAG_NONE);

  /* Hand it off */
  DispatchWorkNFS(nfsreq, worker_index);

  return (rc);
}

static bool_t
nfs_rpc_getreq_ng(SVCXPRT *xprt /*, int chan_id */)
{
    /* Ok, in the new world, TI-RPC's job is merely to tell us there is activity
     * on a specific xprt handle.
     *
     * Note that we have a builtin mechanism to bind, unbind, and (in response
     * to connect events, through a new callout made from within the rendezvous
     * in vc xprts) rebind/rebalance xprt handles to independent event channels,
     * each with their own platform event demultiplexer.  The current callout
     * is one event (request, or, if applicable, new vc connect) on the active
     * xprt handle xprt.
     *
     * We are a blocking call from the svc_run thread specific to our current
     * event channel (whatever it is).  Our goal is to hand off processing of
     * xprt to a request dispatcher thread as quickly as possible, to minimize
     * latency of all xprts on this channel.
     *
     * Next, the preferred dispatch thread should be, I speculate, one which has
     * (most) recently handled a request for this xprt.
     */

    /*
     * UDP RPCs are quite simple: everything comes to the same socket, so
     * several SVCXPRT can be defined, one per tbuf to handle the stuff
     * TCP RPCs are more complex:
     *   - a unique SVCXPRT exists that deals with initial tcp rendez vous.
     *     It does the accept with the client, but recv no message from the
     *     client. But SVC_RECV on it creates a new SVCXPRT dedicated to the
     *     client. This specific SVXPRT is bound on TCPSocket
     *
     * while receiving something on the Svc_fdset, I must know if this is a UDP
     * request, an initial TCP request or a TCP socket from an already connected
     * client.
     * This is how to distinguish the cases:
     * UDP connections are bound to socket NFS_UDPSocket
     * TCP initial connections are bound to socket NFS_TCPSocket
     * all the other cases are requests from already connected TCP Clients
     */

    /* The following actions are now purely diagnostic, the only side effect is a message to
     * the log. */
    int code  __attribute__((unused)) = 0;
    int rpc_fd = xprt->xp_fd;

    if(udp_socket[P_NFS] == rpc_fd)
        LogFullDebug(COMPONENT_DISPATCH, "A NFS UDP request fd %d",
                     rpc_fd);
    else if(udp_socket[P_MNT] == rpc_fd)
        LogFullDebug(COMPONENT_DISPATCH, "A MOUNT UDP request %d",
                     rpc_fd);
#ifdef _USE_NLM
    else if(udp_socket[P_NLM] == rpc_fd)
        LogFullDebug(COMPONENT_DISPATCH, "A NLM UDP request %d",
                     rpc_fd);
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
    else if(udp_socket[P_RQUOTA] == rpc_fd)
        LogFullDebug(COMPONENT_DISPATCH, "A RQUOTA UDP request %d",
                     rpc_fd);
#endif                        /* _USE_QUOTA */
    else if(tcp_socket[P_NFS] == rpc_fd) {
        /*
         * This is an initial tcp connection
         * There is no RPC message, this is only a TCP connect.
         * In this case, the SVC_RECV only produces a new connected socket (it does
         * just a call to accept)
         */
        LogFullDebug(COMPONENT_DISPATCH,
                     "An initial NFS TCP request from a new client %d",
                     rpc_fd);
    }
    else if(tcp_socket[P_MNT] == rpc_fd)
        LogFullDebug(COMPONENT_DISPATCH,
                     "An initial MOUNT TCP request from a new client %d",
                     rpc_fd);
#ifdef _USE_NLM
    else if(tcp_socket[P_NLM] == rpc_fd)
        LogFullDebug(COMPONENT_DISPATCH,
                     "An initial NLM request from a new client %d",
                     rpc_fd);
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
    else if(tcp_socket[P_RQUOTA] == rpc_fd)
        LogFullDebug(COMPONENT_DISPATCH,
                     "An initial RQUOTA request from a new client %d",
                     rpc_fd);
#endif                          /* _USE_QUOTA */
    else
        LogDebug(COMPONENT_DISPATCH,
                 "A NFS TCP request from an already connected client %d",
                 rpc_fd);

    /* Block events in the interval from initial dispatch to the
     * completion of SVC_RECV */
    (void) svc_rqst_block_events(xprt, SVC_RQST_FLAG_NONE);

    dispatch_rpc_request(xprt);

    return (TRUE);
}

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
 * rpc_dispatcher_thread
 *
 * Thread used to service an (epoll, etc) event channel.
 *
 * @param arg, points to the id of the associated event channel
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *rpc_dispatcher_thread(void *arg)
{
    int32_t chan_id = *((int32_t *) arg);
    
    SetNameFunction("dispatch_thr");

    /* Calling dispatcher main loop */
    LogInfo(COMPONENT_DISPATCH,
            "Entering nfs/rpc dispatcher");

    LogDebug(COMPONENT_DISPATCH,
             "My pthread id is %p", (caddr_t) pthread_self());

    svc_rqst_thrd_run(chan_id, SVC_RQST_FLAG_NONE);

    return (NULL);
}                               /* rpc_dispatcher_thread */

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

void constructor_nfs_request_data_t(void *ptr, void *parameters)
{
  nfs_request_data_t * pdata = (nfs_request_data_t *) ptr;
  memset(pdata, 0, sizeof(nfs_request_data_t));
}

/**
 * constructor_request_data_t: Constructor for a request_data_t structure
 *
 * This function is used to init the request_data for a worker. These data are used by the
 * worker for RPC processing.
 *
 * @param ptr void pointer to the structure to be managed
 *
 * @return nothing (void function) will exit the program if failed.
 *
 */
void constructor_request_data_t(void *ptr, void *parameters)
{
  request_data_t * pdata = (request_data_t *) ptr;
  memset(pdata, 0, sizeof(request_data_t));
}
