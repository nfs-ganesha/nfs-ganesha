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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs_rpc_dispatcher.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 12:33:05 $
 * \version $Revision: 1.96 $
 * \brief   The file that contain the 'rpc_dispatcher_thread' routine for nfsd.
 *
 * nfs_rpc_dispatcher.c : The file that contain the 'rpc_dispatcher_thread'
 * routine for nfsd (and all the related stuff).
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
#include "abstract_atomic.h"
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
#include "nfs_req_queue.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"
#include "nfs_tcb.h"
#include "fridgethr.h"

#ifndef _USE_TIRPC_IPV6
  #define P_FAMILY AF_INET
#else
  #define P_FAMILY AF_INET6
#endif

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

thr_fridge_t req_fridge[1]; /* decoder thread pool */
struct nfs_req_st nfs_req_st; /* shared request queues */

const char *req_q_s[N_REQ_QUEUES] =
{
    "REQ_Q_MOUNT",
    "REQ_Q_CALL",
    "REQ_Q_LOW_LATENCY",
    "REQ_Q_HIGH_LATENCY"
};

static u_int nfs_rpc_rdvs(SVCXPRT *xprt, SVCXPRT *newxprt, const u_int flags,
                          void *u_data);
static bool_t nfs_rpc_getreq_ng(SVCXPRT *xprt /*, int chan_id */);
static void nfs_rpc_free_xprt(SVCXPRT *xprt);

const char *xprt_stat_s[3] =
{
    "XPRT_DIED",
    "XPRT_MOREREQS",
    "XPRT_IDLE"
};

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
    (udp_xprt[prot])->xp_u1 = alloc_gsh_xprt_private(udp_xprt[prot],
                                                     XPRT_PRIVATE_FLAG_REF);

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
    (tcp_xprt[prot])->xp_u1 = alloc_gsh_xprt_private(tcp_xprt[prot],
                                                     XPRT_PRIVATE_FLAG_REF);

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

    /* Init request queue before RPC stack */
    nfs_rpc_queue_init();

    LogInfo(COMPONENT_DISPATCH, "NFS INIT: using TIRPC");

    /* New TI-RPC package init function */
    svc_params.flags = SVC_INIT_EPOLL; /* use EPOLL event mgmt */
    svc_params.flags |= SVC_INIT_NOREG_XPRTS; /* don't call xprt_register */
    svc_params.max_connections = nfs_param.core_param.nb_max_fd;
    svc_params.max_events = 1024; /* length of epoll event queue */
    svc_params.warnx = NULL;

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
    newxprt->xp_u1 = alloc_gsh_xprt_private(newxprt, XPRT_PRIVATE_FLAG_REF);

    pthread_mutex_unlock(&mtx);

    (void) svc_rqst_evchan_reg(rpc_evchan[tchan].chan_id, newxprt,
                               SVC_RQST_FLAG_NONE);

    return (0);
}

/**
 * nfs_rpc_free_xprt:  xprt destructor callout
 */
static void
nfs_rpc_free_xprt(SVCXPRT *xprt)
{
    if (xprt->xp_u1) {
        free_gsh_xprt_private(xprt->xp_u1);
        xprt->xp_p1 = NULL;
    }
}

/**
 * nfs_rpc_get_nfsreq: get a request frame (call or svc request)
 */
request_data_t *
nfs_rpc_get_nfsreq(uint32_t __attribute__((unused)) flags)
{
    request_data_t *nfsreq = NULL;

    nfsreq = pool_alloc(request_pool, NULL);

    return (nfsreq);
}

uint32_t
nfs_rpc_outstanding_reqs_est(void)
{
    static uint32_t ctr = 0;
    static uint32_t nreqs = 0;
    struct req_q_pair *qpair;
    int ix;

    if ((atomic_inc_uint32_t(&ctr) % 10) != 0)
        return (nreqs);

    atomic_store_uint32_t(&nreqs, 0);
    for (ix = 0; ix < N_REQ_QUEUES; ++ix) {
        qpair = &(nfs_req_st.reqs.nfs_request_q.qset[ix]);
        atomic_add_uint32_t(&nreqs,
                            atomic_fetch_int32_t(&qpair->producer.size));
        atomic_add_uint32_t(&nreqs,
                            atomic_fetch_int32_t(&qpair->consumer.size));
    }

    return (atomic_fetch_uint32_t(&nreqs));
}


static inline bool
stallq_should_unstall(gsh_xprt_private_t *xu)
{
	return ((xu->req_cnt <
		 nfs_param.core_param.dispatch_max_reqs_xprt/2) ||
		(xu->flags & XPRT_PRIVATE_FLAG_DESTROYED));
}

void *
thr_stallq(void *arg)
{
    fridge_thr_contex_t *thr_ctx  __attribute__((unused)) =
        (fridge_thr_contex_t *) arg;
    gsh_xprt_private_t *xu;
    struct glist_head *l;
    SVCXPRT *xprt;

    while (1) {
        thread_delay_ms(1000);
        pthread_spin_lock(&nfs_req_st.stallq.sp);
    restart:
        if (nfs_req_st.stallq.stalled == 0) {
            nfs_req_st.stallq.active = FALSE;
            pthread_spin_unlock(&nfs_req_st.stallq.sp);
            break;
        }

        glist_for_each(l, &nfs_req_st.stallq.q) {
            xu = glist_entry(l, gsh_xprt_private_t, stallq);
            /* handle stalled xprts that idle out */
            if (stallq_should_unstall(xu)) {
                xprt = xu->xprt;
                /* lock ordering (cf. nfs_rpc_cond_stall_xprt) */
                pthread_spin_unlock(&nfs_req_st.stallq.sp);
                pthread_spin_lock(&xprt->sp);
                pthread_spin_lock(&nfs_req_st.stallq.sp);
                glist_del(&xu->stallq);
                --(nfs_req_st.stallq.stalled);
                xu->flags &= ~XPRT_PRIVATE_FLAG_STALLED;
                /* drop stallq ref */
                --(xu->refcnt);
                pthread_spin_unlock(&xprt->sp);
                goto restart;
            }
        }
        pthread_spin_unlock(&nfs_req_st.stallq.sp);
    }

    LogDebug(COMPONENT_DISPATCH, "stallq idle, thread exit");

  return (NULL);
}

static bool
nfs_rpc_cond_stall_xprt(SVCXPRT *xprt)
{
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) xprt->xp_u1;
    bool activate = FALSE;
    uint32_t nreqs;

    pthread_spin_lock(&xprt->sp);
    nreqs = xu->req_cnt;

    /* check per-xprt quota */
    if (likely(nreqs < nfs_param.core_param.dispatch_max_reqs_xprt)) {
        pthread_spin_unlock(&xprt->sp);
        goto out;
    }

    /* XXX can't happen */
    if (unlikely(xu->flags & XPRT_PRIVATE_FLAG_STALLED)) {
        pthread_spin_unlock(&xprt->sp);
        LogDebug(COMPONENT_DISPATCH, "xprt %p already stalled (oops)",
                 xprt);
        goto out;
    }

    LogDebug(COMPONENT_DISPATCH, "xprt %p has %u reqs, marking stalled",
             xprt, nreqs);

    /* ok, need to stall */
    pthread_spin_lock(&nfs_req_st.stallq.sp);

    glist_add_tail(&nfs_req_st.stallq.q, &xu->stallq);
    ++(nfs_req_st.stallq.stalled);
    xu->flags |= XPRT_PRIVATE_FLAG_STALLED;
    pthread_spin_unlock(&xprt->sp);

    /* if no thread is servicing the stallq, start one */
    if (! nfs_req_st.stallq.active) {
        nfs_req_st.stallq.active = TRUE;
        activate = TRUE;
    }
    pthread_spin_unlock(&nfs_req_st.stallq.sp);

    if (activate) {
        LogDebug(COMPONENT_DISPATCH, "starting stallq service thread");
        (void) fridgethr_get(req_fridge, thr_stallq, NULL /* no arg */);
    }

out:
    return (FALSE);
}

void
nfs_rpc_queue_init(void)
{
    struct req_q_pair *qpair;
    int ix;

    /* decoder thread pool */
    req_fridge->stacksize = 16384;
    req_fridge->expiration_delay_s =
        (nfs_param.core_param.decoder_fridge_expiration_delay > 0) ?
        nfs_param.core_param.decoder_fridge_expiration_delay : 600;
    (void) fridgethr_init(req_fridge, "decoder_thr");

    /* queues */
    pthread_spin_init(&nfs_req_st.reqs.sp, PTHREAD_PROCESS_PRIVATE);
    nfs_req_st.reqs.size = 0;
    for (ix = 0; ix < N_REQ_QUEUES; ++ix) {
        qpair = &(nfs_req_st.reqs.nfs_request_q.qset[ix]);
        qpair->s = req_q_s[ix];
        nfs_rpc_q_init(&qpair->producer);
        nfs_rpc_q_init(&qpair->consumer);
    }

    /* rollover safe counter */
    pthread_spin_init(&nfs_req_st.reqs.slot_sp, PTHREAD_PROCESS_PRIVATE);

    /* waitq */
    init_glist(&nfs_req_st.reqs.wait_list);
    nfs_req_st.reqs.waiters = 0;

    /* stallq */
    pthread_spin_init(&nfs_req_st.stallq.sp, PTHREAD_PROCESS_PRIVATE);
    init_glist(&nfs_req_st.stallq.q);
    nfs_req_st.stallq.active = FALSE;
    nfs_req_st.stallq.stalled = 0;
}

void
nfs_rpc_enqueue_req(request_data_t *req)
{
    struct req_q_set *nfs_request_q;
    struct req_q_pair *qpair;
    struct req_q *q;

    LogFullDebug(COMPONENT_DISPATCH,
                 "enter rq_xid=%u lookahead.flags=%u",
                 req->r_u.nfs->req.rq_xid,
                 req->r_u.nfs->lookahead.flags);

    nfs_request_q = &nfs_req_st.reqs.nfs_request_q;

    switch (req->rtype) {
    case NFS_REQUEST:
        if (req->r_u.nfs->lookahead.flags & NFS_LOOKAHEAD_MOUNT) {
            qpair = &(nfs_request_q->qset[REQ_Q_MOUNT]);
            break;
        }
        if (NFS_LOOKAHEAD_HIGH_LATENCY(req->r_u.nfs->lookahead))
            qpair = &(nfs_request_q->qset[REQ_Q_HIGH_LATENCY]);
        else
            qpair = &(nfs_request_q->qset[REQ_Q_LOW_LATENCY]);
        break;
    case NFS_CALL:
        qpair = &(nfs_request_q->qset[REQ_Q_CALL]);
        break;
#ifdef _USE_9P
    case _9P_REQUEST:
        /* XXX identify high-latency requests and allocate to the high-latency
         * queue, as above */
        qpair = &(nfs_request_q->qset[REQ_Q_LOW_LATENCY]);
        break;
#endif
    default:
        goto out;
        break;
    }

    /* always append to producer queue */
    q = &qpair->producer;
    pthread_spin_lock(&q->we.sp);
    glist_add_tail(&q->q, &req->req_q);
    ++(q->size);
    pthread_spin_unlock(&q->we.sp);

    LogFullDebug(COMPONENT_DISPATCH, "enqueued req, q %p (%s %p:%p) size is %d",
                 q, qpair->s, &qpair->producer, &qpair->consumer, q->size);

    /* potentially wakeup some thread */

    /* global waitq */
    {
        wait_q_entry_t *wqe;
        pthread_spin_lock(&nfs_req_st.reqs.sp); /* SPIN LOCKED */
        if (nfs_req_st.reqs.waiters) {
            wqe = glist_first_entry(&nfs_req_st.reqs.wait_list, wait_q_entry_t,
                                    waitq);

            LogFullDebug(COMPONENT_DISPATCH,
                         "nfs_req_st.reqs.waiters %u signal wqe %p (for q %p)", 
                         nfs_req_st.reqs.waiters, wqe, q);

            /* release 1 waiter */
            glist_del(&wqe->waitq);
            --(nfs_req_st.reqs.waiters);
            --(wqe->waiters);
            pthread_spin_unlock(&nfs_req_st.reqs.sp); /* ! SPIN LOCKED */
            pthread_mutex_lock(&wqe->lwe.mtx);
            /* XXX reliable handoff */
            wqe->flags |= Wqe_LFlag_SyncDone;
            if (wqe->flags & Wqe_LFlag_WaitSync) {
                pthread_cond_signal(&wqe->lwe.cv);
            }
            pthread_mutex_unlock(&wqe->lwe.mtx);
        } else
            pthread_spin_unlock(&nfs_req_st.reqs.sp); /* ! SPIN LOCKED */
    }

out:
    return;
}

static inline request_data_t *
nfs_rpc_consume_req(struct req_q_pair *qpair)
{
    request_data_t * nfsreq = NULL;

    pthread_spin_lock(&qpair->consumer.we.sp);
    if (qpair->consumer.size > 0) {
        nfsreq = glist_first_entry(&qpair->consumer.q, request_data_t, req_q);
        glist_del(&nfsreq->req_q);
        --(qpair->consumer.size);
        pthread_spin_unlock(&qpair->consumer.we.sp);
        goto out;
    } else {
        char *s = NULL;
        uint32_t csize;
        uint32_t psize;

        pthread_spin_lock(&qpair->producer.we.sp);
        if (isFullDebug(COMPONENT_DISPATCH)) {
            s = (char*) qpair->s;
            csize = qpair->consumer.size;
            psize = qpair->producer.size;
        }
        if (qpair->producer.size > 0) {
            /* splice */
            glist_splice_tail(&qpair->consumer.q, &qpair->producer.q);
            qpair->consumer.size = qpair->producer.size;
            qpair->producer.size = 0;
            /* consumer.size > 0 */
            pthread_spin_unlock(&qpair->producer.we.sp);
            nfsreq = glist_first_entry(&qpair->consumer.q, request_data_t,
                                       req_q);
            glist_del(&nfsreq->req_q);
            --(qpair->consumer.size);
            pthread_spin_unlock(&qpair->consumer.we.sp);
            if (s)
                LogFullDebug(COMPONENT_DISPATCH,
                             "try splice, qpair %s consumer qsize=%u "
                             "producer qsize=%u",
                             s, csize, psize);
            goto out;
        }

        pthread_spin_unlock(&qpair->producer.we.sp);
        pthread_spin_unlock(&qpair->consumer.we.sp);

        if (s)
            LogFullDebug(COMPONENT_DISPATCH,
                         "try splice, qpair %s consumer qsize=%u "
                         "producer qsize=%u",
                         s, csize, psize);
    }
out:
    return (nfsreq);
}

request_data_t *
nfs_rpc_dequeue_req(nfs_worker_data_t *worker)
{
    request_data_t *nfsreq = NULL;
    struct req_q_set *nfs_request_q = &nfs_req_st.reqs.nfs_request_q;
    struct req_q_pair *qpair;
    uint32_t ix, slot;
    struct timespec timeout;

    /* XXX: the following stands in for a more robust/flexible
     * weighting function */

    /* slot in 1..4 */
retry_deq:
    slot = (nfs_rpc_q_next_slot() % 4);
    for (ix = 0; ix < 4; ++ix) {
        switch (slot) {
        case 0:
            /* MOUNT */
            qpair = &(nfs_request_q->qset[REQ_Q_MOUNT]);
            break;
        case 1:
            /* NFS_CALL */
            qpair = &(nfs_request_q->qset[REQ_Q_CALL]);
            break;
        case 2:
            /* LL */
            qpair = &(nfs_request_q->qset[REQ_Q_LOW_LATENCY]);
            break;
        case 3:
            /* HL */
            qpair = &(nfs_request_q->qset[REQ_Q_HIGH_LATENCY]);
            break;
        default:
            /* not here */
            abort();
            break;
        }

        LogFullDebug(COMPONENT_DISPATCH, "dequeue_req try qpair %s %p:%p", 
                     qpair->s, &qpair->producer, &qpair->consumer);

        /* anything? */
        nfsreq = nfs_rpc_consume_req(qpair);
        if (nfsreq)
            break;

        ++slot; slot = slot % 4;

    } /* for */

    /* wait */
    if (! nfsreq) {
        wait_q_entry_t *wqe = &worker->wqe;
        assert(wqe->waiters == 0); /* wqe is not on any wait queue */
        pthread_mutex_lock(&wqe->lwe.mtx);
        wqe->flags = Wqe_LFlag_WaitSync;
        wqe->waiters = 1;
        /* XXX functionalize */
        pthread_spin_lock(&nfs_req_st.reqs.sp);
        glist_add_tail(&nfs_req_st.reqs.wait_list, &wqe->waitq);
        ++(nfs_req_st.reqs.waiters);
        pthread_spin_unlock(&nfs_req_st.reqs.sp);

        while (! (wqe->flags & Wqe_LFlag_SyncDone)) {
            timeout.tv_sec  = time(NULL) + 1;
            timeout.tv_nsec = 0;
            pthread_cond_timedwait(&wqe->lwe.cv, &wqe->lwe.mtx, &timeout);
            if ((worker->wcb.tcb_state) == STATE_EXIT) {
                /* We are returning; so take us out of the waitq */
                pthread_spin_lock(&nfs_req_st.reqs.sp);
                if (wqe->waitq.next != NULL || wqe->waitq.prev != NULL) {
                    /* Element is still in wqitq, remove it */
                    glist_del(&wqe->waitq);
                    --(nfs_req_st.reqs.waiters);
                    --(wqe->waiters);
                    wqe->flags &= ~(Wqe_LFlag_WaitSync|Wqe_LFlag_SyncDone);
                }
                pthread_spin_unlock(&nfs_req_st.reqs.sp);
                pthread_mutex_unlock(&wqe->lwe.mtx);
                return NULL;
            }
        }

        /* XXX wqe was removed from nfs_req_st.waitq (by signalling thread) */
        wqe->flags &= ~(Wqe_LFlag_WaitSync|Wqe_LFlag_SyncDone);
        pthread_mutex_unlock(&wqe->lwe.mtx);
        LogFullDebug(COMPONENT_DISPATCH, "wqe wakeup %p", wqe);
        goto retry_deq;
    }

    return (nfsreq);
}

/**
 * nfs_worker_process_rpc_requests: read and process a sequence of RPC
 * requests.
 */
static inline request_data_t *
alloc_nfs_request(SVCXPRT *xprt)
{
    request_data_t *nfsreq;
    struct rpc_msg *msg;
    struct svc_req *req;
    char *cred_area;

    nfsreq = pool_alloc(request_pool, NULL);
    if (! nfsreq) {
        LogMajor(COMPONENT_DISPATCH,
                 "Unable to allocate request. Exiting...");
        Fatal();
    }

    /* Set the request as NFS already-read */
    nfsreq->rtype = NFS_REQUEST;

    nfsreq->r_u.nfs = pool_alloc(request_data_pool, NULL);
    if (! nfsreq->r_u.nfs) {
        LogMajor(COMPONENT_DISPATCH,
                 "Empty request data pool! Exiting...");
        Fatal();
    }

    /* Set up cred area */
    cred_area = nfsreq->r_u.nfs->cred_area;
    req = &(nfsreq->r_u.nfs->req);
    msg = &(nfsreq->r_u.nfs->msg);

    msg->rm_call.cb_cred.oa_base = cred_area;
    msg->rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
    req->rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);

    /* Set up xprt */
    nfsreq->r_u.nfs->xprt = xprt;
    req->rq_xprt = xprt;

    return (nfsreq);
}

static inline void
free_nfs_request(request_data_t *nfsreq)
{
    switch(nfsreq->rtype) {
    case NFS_REQUEST:
        pool_free(request_data_pool, nfsreq->r_u.nfs);
        break;
    default:
        break;
    }
    pool_free(request_pool, nfsreq);
}

#define DISP_LOCK(x) do { \
    if (! locked) { \
        svc_dplx_lock_x(xprt, sigmask, __FILE__, __LINE__); \
        locked = TRUE; \
      }\
    } while (0);

#define DISP_UNLOCK(x) do { \
    if (locked) { \
        svc_dplx_unlock_x(xprt, sigmask); \
        locked = FALSE; \
      }\
    } while (0);

static inline enum xprt_stat
thr_decode_rpc_request(fridge_thr_contex_t *thr_ctx, SVCXPRT *xprt)
{
    sigset_t *sigmask = &thr_ctx->sigmask;
    request_data_t *nfsreq;
    struct rpc_msg *msg;
    struct svc_req *req;
    enum xprt_stat stat = XPRT_IDLE;
    bool_t no_dispatch = TRUE;
    bool locked = FALSE;
    bool enqueued = FALSE;
    bool recv_status;

    LogDebug(COMPONENT_DISPATCH, "enter");

    nfsreq = alloc_nfs_request(xprt); /* ! NULL */
    req = &(nfsreq->r_u.nfs->req);
    msg = &(nfsreq->r_u.nfs->msg);

    DISP_LOCK(xprt);
    recv_status = SVC_RECV(xprt, msg);

    LogFullDebug(COMPONENT_DISPATCH,
                 "SVC_RECV on socket %d returned %s, xid=%u",
                 xprt->xp_fd,
                 (recv_status) ? "TRUE" : "FALSE",
                 msg->rm_xid);

    if (unlikely(! recv_status)) {

      /* RPC over TCP specific: RPC/UDP's xprt know only one state: XPRT_IDLE,
       * because UDP is mostly a stateless protocol.  With RPC/TCP, they can be
       * XPRT_DIED especially when the client closes the peer's socket. We
       * have to cope with this aspect in the next lines.  Finally, xdrrec
       * uses XPRT_MOREREQS to indicate that additional records are ready to
       * be consumed immediately. */

        /* XXXX */
        sockaddr_t addr;
        char addrbuf[SOCK_NAME_MAX];

        if(isDebug(COMPONENT_DISPATCH)) {
            if(copy_xprt_addr(&addr, xprt) == 1)
                sprint_sockaddr(&addr, addrbuf, sizeof(addrbuf));
            else
                sprintf(addrbuf, "<unresolved>");
        }

        stat = SVC_STAT(xprt);
        DISP_UNLOCK(xprt);

        if (stat == XPRT_IDLE) {
            /* typically, a new connection */
            LogDebug(COMPONENT_DISPATCH,
                     "Client on socket=%d, addr=%s has status XPRT_IDLE",
                     xprt->xp_fd, addrbuf);
        }
        else if (stat == XPRT_DIED) {
            LogDebug(COMPONENT_DISPATCH,
                     "Client on socket=%d, addr=%s disappeared (XPRT_DIED)",
                     xprt->xp_fd, addrbuf);
        }
        else if (stat == XPRT_MOREREQS) {
            /* unexpected case */
            LogDebug(COMPONENT_DISPATCH,
                     "Client on socket=%d, addr=%s has status XPRT_MOREREQS",
                     xprt->xp_fd, addrbuf);
        }
        else {
            LogDebug(COMPONENT_DISPATCH,
                     "Client on socket=%d, addr=%s has unknown status (%d)",
                     xprt->xp_fd, addrbuf, stat);
        }
        goto done;
    }
    else {
        nfsreq->r_u.nfs->req.rq_prog = msg->rm_call.cb_prog;
        nfsreq->r_u.nfs->req.rq_vers = msg->rm_call.cb_vers;
        nfsreq->r_u.nfs->req.rq_proc = msg->rm_call.cb_proc;
        nfsreq->r_u.nfs->req.rq_xid = msg->rm_xid;

        /* XXX so long as nfs_rpc_get_funcdesc calls is_rpc_call_valid
         * and fails if that call fails, there is no reason to call that
         * function again, below */
        nfsreq->r_u.nfs->funcdesc = nfs_rpc_get_funcdesc(nfsreq->r_u.nfs);
        if (nfsreq->r_u.nfs->funcdesc == INVALID_FUNCDESC)
            goto finish;

        if (AuthenticateRequest(nfsreq->r_u.nfs,
                                &no_dispatch) != AUTH_OK || no_dispatch) {
            goto finish;
        }

        if (!nfs_rpc_get_args(nfsreq->r_u.nfs))
            goto finish;

        req->rq_xprt = xprt;

        /* update accounting */
        gsh_xprt_ref(xprt, XPRT_PRIVATE_FLAG_INCREQ);

        /* XXX as above, the call has already passed is_rpc_call_valid,
         * the former check here is removed. */
        nfs_rpc_enqueue_req(nfsreq);
        enqueued = TRUE;
    }

finish:
    stat = SVC_STAT(xprt);
    DISP_UNLOCK(xprt);

done:
    /* if recv failed, request is not enqueued */
    if (! enqueued)
        free_nfs_request(nfsreq);

    return (stat);
}

void *
thr_decode_rpc_requests(void *arg)
{
    enum xprt_stat stat;
    fridge_thr_contex_t *thr_ctx = (fridge_thr_contex_t *) arg;
    SVCXPRT *xprt = (SVCXPRT *) thr_ctx->arg;

    /* continue receiving if data is already buffered--failure to do so
     * will result in stalls (TCP) */
    do {
        stat = thr_decode_rpc_request(thr_ctx, xprt);
    } while (stat == XPRT_MOREREQS);

    LogDebug(COMPONENT_DISPATCH, "exiting, stat=%s", xprt_stat_s[stat]);

    /* done decoding, rearm */
    if (stat != XPRT_DIED)
        (void) svc_rqst_rearm_events(xprt, SVC_RQST_FLAG_NONE);

    /* update accounting, clear decoding flag */
    gsh_xprt_unref(xprt, XPRT_PRIVATE_FLAG_DECODING);

    /* XXX EPOLLONESHOT semantics -should- make this safe */
    if (stat == XPRT_DIED)
        gsh_xprt_destroy(xprt);

  return (NULL);
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

    /* The following actions are now purely diagnostic, the only side effect 
     * is a message to the log. */
    int code  __attribute__((unused)) = 0;
    int rpc_fd = xprt->xp_fd;
    uint32_t nreqs;

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
         * In this case, the SVC_RECV only produces a new connected socket (it
         * does just a call to accept)
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
        LogFullDebug(COMPONENT_DISPATCH,
                     "A NFS TCP request from an already connected client %d",
                     rpc_fd);

    /* XXX
     * Decoder backpressure.  There are multiple considerations here.
     * One is to avoid decoding if doing so would cause the server to exceed
     * global resource constraints.  Another is to adjust flow parameters on
     * underlying network resources, to avoid moving the problem back into
     * the kernel.  The latter requires continuous, but low overhead, flow
     * measurement with hysteretic control.  For now, just do global and
     * per-xprt request quotas.
     */

    /* check max outstanding quota */
    nreqs = nfs_rpc_outstanding_reqs_est();
    if (unlikely(nreqs >  nfs_param.core_param.dispatch_max_reqs)) {
        /* request queue is flow controlled */
        LogDebug(COMPONENT_DISPATCH,
                 "global outstanding reqs quota exceeded (have %u, allowed %u)",
                 nreqs, nfs_param.core_param.dispatch_max_reqs);
        thread_delay_ms(5); /* don't busy-wait */
        (void) svc_rqst_rearm_events(xprt, SVC_RQST_FLAG_NONE);
        goto out;
    }

    LogFullDebug(COMPONENT_DISPATCH, "before guard_ref");

    /* clock duplicate, queued+stalled wakeups, queued wakeups */
    if (! gsh_xprt_decoder_guard_ref(xprt, XPRT_PRIVATE_FLAG_NONE)) {
        thread_delay_ms(5);
        (void) svc_rqst_rearm_events(xprt, SVC_RQST_FLAG_NONE);
        goto out;
    }

    LogFullDebug(COMPONENT_DISPATCH, "before cond stall");

    /* Check per-xprt max outstanding quota */
    if (nfs_rpc_cond_stall_xprt(xprt)) {
        /* Xprt stalled--bail.  Stall queue owns xprt ref and state. */
        LogDebug(COMPONENT_DISPATCH, "stalled, bail");
        goto out;
    }

    LogFullDebug(COMPONENT_DISPATCH, "before fridgethr_get");

    /* schedule a thread to decode */
    (void) fridgethr_get(req_fridge, thr_decode_rpc_requests, xprt);

    LogFullDebug(COMPONENT_DISPATCH, "after fridgethr_get");

out:
    return (TRUE);
}

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
