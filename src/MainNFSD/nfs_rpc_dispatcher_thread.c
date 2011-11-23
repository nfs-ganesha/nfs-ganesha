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
#include "rpc.h"
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
#include "nfs_tcb.h"

#ifndef _USE_TIRPC_IPV6
  #define P_FAMILY AF_INET
#else
  #define P_FAMILY AF_INET6
#endif

static pthread_mutex_t lock_worker_selection = PTHREAD_MUTEX_INITIALIZER;

#if !defined(_NO_BUDDY_SYSTEM) && defined(_DEBUG_MEMLEAKS)
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

  if(isFullDebug(COMPONENT_MEMLEAKS) && isFullDebug(COMPONENT_DISPATCH))
    {
      BuddyLabelsSummary(COMPONENT_DISPATCH);

      BuddyGetStats(&bstats);
      LogFullDebug(COMPONENT_MEMLEAKS,
                   "------- TOTAL SPACE USED FOR WORKER THREAD: %12lu (on %2u pages)",
                   (unsigned long)bstats.StdUsedSpace, bstats.NbStdUsed);

      /* DisplayMemoryMap(); */

      LogFullDebug(COMPONENT_MEMLEAKS,
                   "--------------------------------------------------");
    }

}                               /* nfs_debug_debug_label_info */

void nfs_debug_buddy_info()
{
  buddy_stats_t bstats;

  if(isFullDebug(COMPONENT_MEMLEAKS) && isFullDebug(COMPONENT_DISPATCH))
    {
      BuddyLabelsSummary(COMPONENT_DISPATCH);

      BuddyGetStats(&bstats);
      LogFullDebug(COMPONENT_MEMLEAKS,
                   "------- TOTAL SPACE USED FOR DISPATCHER THREAD: %12lu (on %2u pages)",
                   (unsigned long)bstats.StdUsedSpace, bstats.NbStdUsed);

      LogFullDebug(COMPONENT_MEMLEAKS,
                   "--------------------------------------------------");
    }
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

const char *tags[] = {
  "NFS",
  "MNT",
#ifdef _USE_NLM
  "NLM",
#endif
#ifdef _USE_QUOTA
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

#ifdef _USE_TIRPC
struct netconfig *netconfig_udpv4;
struct netconfig *netconfig_tcpv4;
#ifdef _USE_TIRPC_IPV6
struct netconfig *netconfig_udpv6;
struct netconfig *netconfig_tcpv6;
#endif
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
#ifdef _USE_TIRPC
void unregister(const rpcprog_t prog, const rpcvers_t vers1, const rpcvers_t vers2)
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
#else
void unregister(const u_long prog, const u_long vers1, const u_long vers2)
{
  u_long vers;
  for(vers = vers1; vers <= vers2; vers++)
    {
      pmap_unset(prog, vers);
    }
}
#endif

void unregister_rpc(void)
{
  unregister(nfs_param.core_param.program[P_NFS], NFS_V2, NFS_V4);
  unregister(nfs_param.core_param.program[P_MNT], MOUNT_V1, MOUNT_V3);
#ifdef _USE_NLM
  unregister(nfs_param.core_param.program[P_NLM], 1, NLM4_VERS);
#endif
#ifdef _USE_QUOTA
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

void Create_udp(protos prot)
{
#ifdef _USE_TIRPC
  udp_xprt[prot] = Svc_dg_create(udp_socket[prot],
                                 nfs_param.core_param.max_send_buffer_size,
                                 nfs_param.core_param.max_recv_buffer_size);
#else
  udp_xprt[prot] = Svcudp_bufcreate(udp_socket[prot],
                                    nfs_param.core_param.max_send_buffer_size,
                                    nfs_param.core_param.max_recv_buffer_size);
#endif
  if(udp_xprt[prot] == NULL)
    LogFatal(COMPONENT_DISPATCH,
             "Cannot allocate %s/UDP SVCXPRT", tags[prot]);

#ifdef _USE_TIRPC_IPV6
  udp_xprt[prot]->xp_netid = Str_Dup(netconfig_udpv6->nc_netid);
  udp_xprt[prot]->xp_tp    = Str_Dup(netconfig_udpv6->nc_device);
#endif
}

void Create_tcp(protos prot)
{
#ifdef _USE_TIRPC
  tcp_xprt[prot] = Svc_vc_create(tcp_socket[prot],
                                 nfs_param.core_param.max_send_buffer_size,
                                 nfs_param.core_param.max_recv_buffer_size);
#else
  tcp_xprt[prot] = Svctcp_create(tcp_socket[prot],
                                 nfs_param.core_param.max_send_buffer_size,
                                 nfs_param.core_param.max_recv_buffer_size);
#endif
  if(tcp_xprt[prot] == NULL)
    LogFatal(COMPONENT_DISPATCH,
             "Cannot allocate %s/TCP SVCXPRT", tags[prot]);

#ifdef _USE_TIRPC_IPV6
  if(listen(socket, pdata[prot].bindaddr_udp6.qlen) != 0)
    LogFatal(COMPONENT_DISPATCH,
            "Cannot listen on  %s/TCPv6 SVCXPRT, errno=%u (%s)",
            tags[prot], errno, strerror(errno));

  tcp_xprt[prot]->xp_netid = Str_Dup(netconfig_tcpv6->nc_netid);
  tcp_xprt[prot]->xp_tp    = Str_Dup(netconfig_tcpv6->nc_device);
#endif
}

/**
 * Create_SVCXPRT: Create the SVCXPRT for each protocol in use.
 *
 */
void Create_SVCXPRT(void)
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
                   "Cannot get %s socket info for udp6 socket rc=%d errno=%d (%s)",
                   tags[p], rc, errno, strerror(errno));

        if(bind(udp_socket[p],
                (struct sockaddr *)pdatap->bindaddr_udp6.addr.buf,
                (socklen_t) si_nfs_udp6.si_alen) == -1)
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
                   "Cannot get %s socket info for tcp6 socket rc=%d errno=%d (%s)",
                   tags[p], rc, errno, strerror(errno));

        if(bind(tcp_socket[p],
                (struct sockaddr *)pdatap->bindaddr_tcp6.addr.buf,
                (socklen_t) si_nfs_tcp6.si_alen) == -1)
          LogFatal(COMPONENT_DISPATCH,
                   "Cannot bind %s tcp6 socket, error %d (%s)",
                   tags[p], errno, strerror(errno));
#endif
      }
}

void Clean_RPC(void)
{
  unregister_rpc();
}

cleanup_list_element clean_rpc = {NULL, Clean_RPC};

#ifdef _USE_TIRPC
#define UDP_REGISTER(prot, vers, netconfig) \
  svc_reg(udp_xprt[prot], nfs_param.core_param.program[prot], (rpcvers_t) vers, nfs_rpc_dispatch_dummy, netconfig)
#define TCP_REGISTER(prot, vers, netconfig) \
  svc_reg(tcp_xprt[prot], nfs_param.core_param.program[prot], (rpcvers_t) vers, nfs_rpc_dispatch_dummy, netconfig)
#else
#define UDP_REGISTER(prot, vers, netconfig) \
  Svc_register(udp_xprt[prot], nfs_param.core_param.program[prot], (u_long) vers, nfs_rpc_dispatch_dummy, IPPROTO_UDP)
#define TCP_REGISTER(prot, vers, netconfig) \
  Svc_register(tcp_xprt[prot], nfs_param.core_param.program[prot], (u_long) vers, nfs_rpc_dispatch_dummy, IPPROTO_TCP)
#endif

void Register_program(protos prot, int flag, int vers)
{
  if((nfs_param.core_param.core_options & flag) != 0)
    {
      LogInfo(COMPONENT_DISPATCH,
              "Registering %s V%d/UDP",
              tags[prot], (int)vers);

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
 * Perform all the required initialization for the SVCXPRT pointer.
 *
 *
 */
void nfs_Init_svc()
{
  int one = 1;
  protos p;

  /* Initialize all the sockets to -1 because it makes some code later easier */
  for(p = P_NFS; p < P_COUNT; p++)
    {
      udp_socket[p] = -1;
      tcp_socket[p] = -1;
    }

  LogInfo(COMPONENT_DISPATCH, "NFS INIT: Core options = %d",
          nfs_param.core_param.core_options);

  InitRPC(nfs_param.core_param.nb_max_fd);

#ifdef _USE_TIRPC
  LogInfo(COMPONENT_DISPATCH, "NFS INIT: using TIRPC");

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
#endif

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

#ifdef _USE_QUOTA
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

  /* Allocation of the SVCXPRT */
  Create_SVCXPRT();

#ifdef _HAVE_GSSAPI
  /* Acquire RPCSEC_GSS basis if needed */
  if(nfs_param.krb5_param.active_krb5 == TRUE)
    {
      if(Svcauth_gss_import_name(nfs_param.krb5_param.principal) != TRUE)
        {
          LogFatal(COMPONENT_DISPATCH,
                   "Could not import principal name %s into GSSAPI",
                   nfs_param.krb5_param.principal);
        }
      else
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Successfully imported principal %s into GSSAPI",
                  nfs_param.krb5_param.principal);

          /* Trying to acquire a credentials for checking name's validity */
          if(!Svcauth_gss_acquire_cred())
            LogFatal(COMPONENT_DISPATCH,
                     "Cannot acquire credentials for principal %s",
                     nfs_param.krb5_param.principal);
          else
            LogInfo(COMPONENT_DISPATCH,
                    "Principal %s is suitable for acquiring credentials",
                    nfs_param.krb5_param.principal);
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
#ifdef _USE_QUOTA
  Register_program(P_NLM, CORE_OPTION_ALL_VERS, RQUOTAVERS);
  Register_program(P_NLM, CORE_OPTION_ALL_VERS, EXT_RQUOTAVERS);
#endif                          /* USE_QUOTA */
#endif                          /* _NO_PORTMAPPER */

}                               /* nfs_Init_svc */

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

  P(lock_worker_selection);
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
  V(lock_worker_selection);

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
              rc = wait_for_threads_to_awaken();
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
 * process_rpc_request: process an RPC request.
 *
 */
process_status_t process_rpc_request(SVCXPRT *xprt)
{
  char *cred_area;
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  enum xprt_stat stat;
  const nfs_function_desc_t *pfuncdesc;
  bool_t no_dispatch = TRUE, recv_status;
  request_data_t *pnfsreq = NULL;
  unsigned int worker_index;
  process_status_t rc = PROCESS_DONE;

  /* A few thread manage only mount protocol, check for this */

  /* Get a worker to do the job */
#ifndef _NO_MOUNT_LIST
  if((udp_socket[P_MNT] == xprt->XP_SOCK) ||
     (tcp_socket[P_MNT] == xprt->XP_SOCK))
    {
      /* worker #0 is dedicated to mount protocol */
      worker_index = 0;
    }
  else
#endif
    {
       /* choose a worker depending on its queue length */
       worker_index = select_worker_queue();
    }

  LogFullDebug(COMPONENT_DISPATCH,
               "Use request from Worker Thread #%u's pool, xprt->xp_sock=%d, thread has %d pending requests",
               worker_index, xprt->XP_SOCK,
               workers_data[worker_index].pending_request->nb_entry);

  /* Get a pnfsreq from the worker's pool */
  P(workers_data[worker_index].request_pool_mutex);

  GetFromPool(pnfsreq, &workers_data[worker_index].request_pool,
              request_data_t);

  V(workers_data[worker_index].request_pool_mutex);

  if(pnfsreq == NULL)
    {
      LogMajor(COMPONENT_DISPATCH,
               "Empty request pool for the chosen worker ! Exiting...");
      Fatal();
    }

  /* Set the request as a NFS related one */
  pnfsreq->rtype = NFS_REQUEST ;

  /* Set up cred area */
  cred_area = pnfsreq->rcontent.nfs.cred_area;
  preq = &(pnfsreq->rcontent.nfs.req);
  pmsg = &(pnfsreq->rcontent.nfs.msg);

  pmsg->rm_call.cb_cred.oa_base = cred_area;
  pmsg->rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
  preq->rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);

  /* Set up xprt */
  pnfsreq->rcontent.nfs.xprt = xprt;
  preq->rq_xprt = xprt;

  /*
   * Receive from socket.
   * Will block until the client operates on the socket
   */
  LogFullDebug(COMPONENT_DISPATCH,
               "Before calling SVC_RECV on socket %d",
               pnfsreq->rcontent.nfs.xprt->XP_SOCK);

  recv_status = SVC_RECV(pnfsreq->rcontent.nfs.xprt, pmsg);

  LogFullDebug(COMPONENT_DISPATCH,
               "Status for SVC_RECV on socket %d is %d, xid=%lu",
               pnfsreq->rcontent.nfs.xprt->XP_SOCK, recv_status,
               (unsigned long)pmsg->rm_xid);

  /* If status is ok, the request will be processed by the related
   * worker, otherwise, it should be released by being tagged as invalid*/
  if(!recv_status)
    {
      /* RPC over TCP specific: RPC/UDP's xprt know only one state: XPRT_IDLE, because UDP is mostly
       * a stateless protocol. With RPC/TCP, they can be XPRT_DIED especially when the client closes
       * the peer's socket. We have to cope with this aspect in the next lines */

      sockaddr_t addr;
      char addrbuf[SOCK_NAME_MAX];

      if(copy_xprt_addr(&addr, pnfsreq->rcontent.nfs.xprt) == 1)
        sprint_sockaddr(&addr, addrbuf, sizeof(addrbuf));
      else
        sprintf(addrbuf, "<unresolved>");

      stat = SVC_STAT(pnfsreq->rcontent.nfs.xprt);

      if(stat == XPRT_DIED)
        {

          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s disappeared...",
                   pnfsreq->rcontent.nfs.xprt->XP_SOCK, addrbuf);

          if(Xports[pnfsreq->rcontent.nfs.xprt->XP_SOCK] != NULL)
            SVC_DESTROY(Xports[pnfsreq->rcontent.nfs.xprt->XP_SOCK]);

          rc = PROCESS_LOST_CONN;
        }
      else if(stat == XPRT_MOREREQS)
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s has status XPRT_MOREREQS",
                   pnfsreq->rcontent.nfs.xprt->XP_SOCK, addrbuf);
        }
      else if(stat == XPRT_IDLE)
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s has status XPRT_IDLE",
                   pnfsreq->rcontent.nfs.xprt->XP_SOCK, addrbuf);
        }
      else
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s has status unknown (%d)",
                   pnfsreq->rcontent.nfs.xprt->XP_SOCK, addrbuf, (int)stat);
        }

      goto free_req;
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
      pnfsreq->rcontent.nfs.req.rq_prog = pmsg->rm_call.cb_prog;
      pnfsreq->rcontent.nfs.req.rq_vers = pmsg->rm_call.cb_vers;
      pnfsreq->rcontent.nfs.req.rq_proc = pmsg->rm_call.cb_proc;

      /* Use primary xprt for now (in case xprt has GSS state)
       * until we make a copy
       */
      pnfsreq->rcontent.nfs.xprt = xprt;
      pnfsreq->rcontent.nfs.req.rq_xprt = xprt;

      pfuncdesc = nfs_rpc_get_funcdesc(&pnfsreq->rcontent.nfs);

      if(pfuncdesc == INVALID_FUNCDESC)
        goto free_req;

      if(AuthenticateRequest(&pnfsreq->rcontent.nfs, &no_dispatch) != AUTH_OK || no_dispatch)
        goto free_req;

      if(!nfs_rpc_get_args(&pnfsreq->rcontent.nfs, pfuncdesc))
        goto free_req;

      /* Update a copy of SVCXPRT and pass it to the worker thread to use it. */
      pnfsreq->rcontent.nfs.xprt_copy = Svcxprt_copy(pnfsreq->rcontent.nfs.xprt_copy, xprt);
      if(pnfsreq->rcontent.nfs.xprt_copy == NULL)
        goto free_req;

      pnfsreq->rcontent.nfs.xprt = pnfsreq->rcontent.nfs.xprt_copy;
      preq->rq_xprt = pnfsreq->rcontent.nfs.xprt_copy;

      /* Regular management of the request (UDP request or TCP request on connected handler */
      DispatchWorkNFS(pnfsreq, worker_index);

      gettimeofday(&timer_end, NULL);
      timer_diff = time_diff(timer_start, timer_end);

      /* Update await time. */
      stat_type = GANESHA_STAT_SUCCESS;
      latency_stat.type = AWAIT_TIME;
      latency_stat.latency = timer_diff.tv_sec * 1000000 + timer_diff.tv_usec; /* microseconds */
      nfs_stat_update(stat_type,
                      &(workers_data[worker_index].stats.stat_req),
                      &(pnfsreq->rcontent.nfs.req),
                      &latency_stat);

      LogFullDebug(COMPONENT_DISPATCH,
                   "Worker Thread #%u has committed the operation: end_time %llu.%.6llu await %llu.%.6llu",
                   worker_index,
                   (unsigned long long int)timer_end.tv_sec,
                   (unsigned long long int)timer_end.tv_usec,
                   (unsigned long long int)timer_diff.tv_sec,
                   (unsigned long long int)timer_diff.tv_usec);
      return PROCESS_DISPATCHED;
    }

free_req:
  /* Release the entry */
  P(workers_data[worker_index].request_pool_mutex);
  ReleaseToPool(pnfsreq, &workers_data[worker_index].request_pool);
  workers_data[worker_index].passcounter += 1;
  V(workers_data[worker_index].request_pool_mutex);
  return rc;
}

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
void nfs_rpc_getreq(fd_set * readfds)
{
  register SVCXPRT *xprt;
  register int bit;
  register long mask, *maskp;
  register int sock;
  register int rpc_sock;
  process_status_t status;

  /* portable access to fds_bits field */
  maskp = __FDS_BITS(readfds);

  for(sock = 0; sock < FD_SETSIZE; sock += NFDBITS)
    {
      for(mask = *maskp++; (bit = ffs(mask)); mask ^= (1 << (bit - 1)))
        {
          /* sock has input waiting */
          rpc_sock = sock + bit - 1;

          xprt = Xports[rpc_sock];
          if(xprt == NULL)
            {
              /* But do we control sock? */
              LogCrit(COMPONENT_DISPATCH,
                      "CRITICAL ERROR: Incoherency found in Xports array");
              continue;
            }

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

          if(udp_socket[P_NFS] == rpc_sock)
            {
              /* This is a regular UDP connection */
              LogFullDebug(COMPONENT_DISPATCH, "A NFS UDP request");
              if(xprt != udp_xprt[P_NFS])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_nfs_udp=%p",
                        xprt, udp_xprt[P_NFS]);
              xprt = udp_xprt[P_NFS];
            }
          else if(udp_socket[P_MNT] == rpc_sock)
            {
              LogFullDebug(COMPONENT_DISPATCH, "A MOUNT UDP request");
              if(xprt != udp_xprt[P_MNT])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_mnt_udp=%p",
                        xprt, udp_xprt[P_MNT]);
              xprt = udp_xprt[P_MNT];
            }
#ifdef _USE_NLM
          else if(udp_socket[P_NLM] == rpc_sock)
            {
              LogFullDebug(COMPONENT_DISPATCH, "A NLM UDP request");
              if(xprt != udp_xprt[P_NLM])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_nlm_udp=%p",
                        xprt, udp_xprt[P_NLM]);
              xprt = udp_xprt[P_NLM];
            }
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
          else if(udp_socket[P_RQUOTA] == rpc_sock)
            {
              LogFullDebug(COMPONENT_DISPATCH, "A RQUOTA UDP request");
              if(xprt != udp_xprt[P_RQUOTA])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_rquota_udp=%p",
                        xprt, udp_xprt[P_RQUOTA]);
              xprt = udp_xprt[P_RQUOTA];
            }
#endif                          /* _USE_QUOTA */
          else if(tcp_socket[P_NFS] == rpc_sock)
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
              if(xprt != tcp_xprt[P_NFS])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_nfs_tcp=%p",
                        xprt, tcp_xprt[P_NFS]);
              xprt = tcp_xprt[P_NFS];
            }
          else if(tcp_socket[P_MNT] == rpc_sock)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "An initial MOUNT TCP request from a new client");
              if(xprt != tcp_xprt[P_MNT])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_mnt_tcp=%p",
                        xprt, tcp_xprt[P_MNT]);
              xprt = tcp_xprt[P_MNT];
            }
#ifdef _USE_NLM
          else if(tcp_socket[P_NLM] == rpc_sock)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "An initial NLM request from a new client");
              if(xprt != tcp_xprt[P_NLM])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_nlm_tcp=%p",
                        xprt, tcp_xprt[P_NLM]);
              xprt = tcp_xprt[P_NLM];
            }
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
          else if(tcp_socket[P_RQUOTA] == rpc_sock)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "An initial RQUOTA request from a new client");
              if(xprt != tcp_xprt[P_RQUOTA])
                LogCrit(COMPONENT_DISPATCH,
                        "Oops, UDP xprt doesn't match xprt=%p xprt_rquota_tcp=%p",
                        xprt, tcp_xprt[P_RQUOTA]);
              xprt = tcp_xprt[P_RQUOTA];
            }
#endif                          /* _USE_QUOTA */
          else
            {
              /* This is a regular tcp request on an established connection, should be handle by a dedicated thread */
              LogDebug(COMPONENT_DISPATCH,
                       "A NFS TCP request from an already connected client");
            }

          status = process_rpc_request(xprt);
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
  struct prealloc_pool *request_pool = (struct prealloc_pool *) addparam;
  nfs_request_data_t *preqnfs = (nfs_request_data_t *) (pentry->buffdata.pdata);

  /* Send the entry back to the pool */
  ReleaseToPool(preqnfs, request_pool);

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

void rpc_dispatcher_svc_run()
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
      LogFullDebug(COMPONENT_DISPATCH,
                   "rpc dispatcher thread waiting for incoming RPC requests");

      /* Do the select on the RPC fdset */
      rc = select(FD_SETSIZE, &readfdset, NULL, NULL, NULL);

      LogFullDebug(COMPONENT_DISPATCH,
                   "Waiting for incoming RPC requests, after select rc=%d",
                   rc);
      switch (rc)
        {
        case -1:
          if(errno == EBADF)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Select failed, error %d (%s)", errno, strerror(errno));
              return;
            }
          break;

        case 0:
          continue;

        default:
          LogFullDebug(COMPONENT_DISPATCH, "NFS SVC RUN: request(s) received");
          nfs_rpc_getreq(&readfdset);
          break;

        }                       /* switch */

#ifdef _DEBUG_MEMLEAKS
      if(nb_iter_memleaks > 1000)
        {
          nb_iter_memleaks = 0;
#ifndef _NO_BUDDY_SYSTEM
          nfs_debug_buddy_info();
#endif
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
 * @param Arg (unused)
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *rpc_dispatcher_thread(void *Arg)
{
  SetNameFunction("dispatch_thr");

#ifndef _NO_BUDDY_SYSTEM
  /* Initialisation of the Buddy Malloc */
  LogInfo(COMPONENT_DISPATCH,
          "Initialization of memory manager");
  if(BuddyInit(&nfs_param.buddy_param_worker) != BUDDY_SUCCESS)
    LogFatal(COMPONENT_DISPATCH,
             "Memory manager could not be initialized");
#endif
  /* Calling dispatcher main loop */
  LogInfo(COMPONENT_DISPATCH,
          "Entering nfs/rpc dispatcher");

  LogDebug(COMPONENT_DISPATCH,
           "My pthread id is %p", (caddr_t) pthread_self());

  rpc_dispatcher_svc_run();

  return NULL;
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

void constructor_nfs_request_data_t(void *ptr)
{
  nfs_request_data_t * pdata = (nfs_request_data_t *) ptr;

  memset(pdata, 0, sizeof(*pdata));
  pdata->xprt_copy = Svcxprt_copycreate();
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

void constructor_request_data_t(void *ptr)
{
  request_data_t * pdata = (request_data_t *) ptr;

  constructor_nfs_request_data_t( &(pdata->rcontent.nfs) ) ;
}
