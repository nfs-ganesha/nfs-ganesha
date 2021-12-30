/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *                William Allen Simpson <william.allen.simpson@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file  nfs_rpc_dispatcher_thread.c
 * @brief Contains the @c rpc_dispatcher_thread routine and support code
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <sys/select.h>
#include <poll.h>
#ifdef RPC_VSOCK
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/vm_sockets.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "abstract_atomic.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfsacl.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"

#ifdef USE_LTTNG
#include "gsh_lttng/nfs_rpc.h"
#endif

#ifdef __APPLE__
#include <sys/socketvar.h>
#include <netinet/tcp_var.h>
#define TCP_KEEPIDLE TCPCTL_KEEPIDLE
#endif

#define NFS_options nfs_param.core_param.core_options
#define NFS_program nfs_param.core_param.program

/**
 * TI-RPC event channels.  Each channel is a thread servicing an event
 * demultiplexer.
 */

struct rpc_evchan {
	uint32_t chan_id;	/*< Channel ID */
};

enum evchan {
	UDP_UREG_CHAN,		/*< Put UDP on a dedicated channel */
	TCP_UREG_CHAN,		/*< Accepts new TCP connections */
#ifdef _USE_NFS_RDMA
	RDMA_UREG_CHAN,		/*< Accepts new RDMA connections */
#endif
	EVCHAN_SIZE
};
#define N_TCP_EVENT_CHAN  3	/*< We don't really want to have too many,
				   relative to the number of available cores. */
#define N_EVENT_CHAN (N_TCP_EVENT_CHAN + EVCHAN_SIZE)

static struct rpc_evchan rpc_evchan[EVCHAN_SIZE];

static enum xprt_stat nfs_rpc_tcp_user_data(SVCXPRT *);
static enum xprt_stat nfs_rpc_free_user_data(SVCXPRT *);
static struct svc_req *alloc_nfs_request(SVCXPRT *xprt, XDR *xdrs);
static void free_nfs_request(struct svc_req *req, enum xprt_stat stat);

const char *xprt_stat_s[XPRT_DESTROYED + 1] = {
	"XPRT_IDLE",
	"XPRT_DISPATCH",
	"XPRT_DIED",
	"XPRT_DESTROYED"
};

/**
 * @brief Function never called, but the symbol is needed for svc_register.
 *
 * @param[in] ptr_req Unused
 * @param[in] ptr_svc Unused
 */
void nfs_rpc_dispatch_dummy(struct svc_req *req)
{
	LogMajor(COMPONENT_DISPATCH,
		 "Possible error, function %s should never be called",
		 __func__);
}

const char *tags[P_COUNT] = {
	"NFS",
#ifdef _USE_NFS3
	"MNT",
#endif
#ifdef _USE_NLM
	"NLM",
#ifdef _USE_RQUOTA
	"RQUOTA",
#endif
#endif
#ifdef USE_NFSACL3
	"NFSACL",
#endif
#ifdef RPC_VSOCK
	"NFS_VSOCK",
#endif
#ifdef _USE_NFS_RDMA
	"NFS_RDMA",
#endif
};

typedef struct proto_data {
	struct sockaddr_in sinaddr_udp;
	struct sockaddr_in sinaddr_tcp;
	struct sockaddr_in6 sinaddr_udp6;
	struct sockaddr_in6 sinaddr_tcp6;
	struct netbuf netbuf_udp6;
	struct netbuf netbuf_tcp6;
	struct t_bind bindaddr_udp6;
	struct t_bind bindaddr_tcp6;
	struct __rpc_sockinfo si_udp6;
	struct __rpc_sockinfo si_tcp6;
} proto_data;

proto_data pdata[P_COUNT];

struct netconfig *netconfig_udpv4;
struct netconfig *netconfig_tcpv4;
struct netconfig *netconfig_udpv6;
struct netconfig *netconfig_tcpv6;

/* RPC Service Sockets and Transports */
int udp_socket[P_COUNT];
int tcp_socket[P_COUNT];
SVCXPRT *udp_xprt[P_COUNT];
SVCXPRT *tcp_xprt[P_COUNT];

/* Flag to indicate if V6 interfaces on the host are enabled */
bool v6disabled;
bool vsock;
bool rdma;

/**
 * @brief Unregister an RPC program.
 *
 * @param[in] prog  Program to unregister
 * @param[in] vers1 Lowest version
 * @param[in] vers2 Highest version
 */
static void unregister(const rpcprog_t prog, const rpcvers_t vers1,
		       const rpcvers_t vers2)
{
	rpcvers_t vers;

	for (vers = vers1; vers <= vers2; vers++) {
		rpcb_unset(prog, vers, netconfig_udpv4);
		rpcb_unset(prog, vers, netconfig_tcpv4);
		if (!v6disabled && netconfig_udpv6)
			rpcb_unset(prog, vers, netconfig_udpv6);
		if (!v6disabled && netconfig_tcpv6)
			rpcb_unset(prog, vers, netconfig_tcpv6);
	}
}

static void unregister_rpc(void)
{
	if ((NFS_options & CORE_OPTION_NFSV3) != 0) {
#ifdef _USE_NFS3
		unregister(NFS_program[P_NFS], NFS_V3, NFS_V4);
		unregister(NFS_program[P_MNT], MOUNT_V1, MOUNT_V3);
#endif
	} else {
		unregister(NFS_program[P_NFS], NFS_V4, NFS_V4);
	}
#ifdef _USE_NLM
	if (nfs_param.core_param.enable_NLM)
		unregister(NFS_program[P_NLM], 1, NLM4_VERS);
#endif /* _USE_NLM */
#ifdef _USE_RQUOTA
	if (nfs_param.core_param.enable_RQUOTA) {
		unregister(NFS_program[P_RQUOTA], RQUOTAVERS, EXT_RQUOTAVERS);
	}
#endif

#ifdef USE_NFSACL3
	if (nfs_param.core_param.enable_NFSACL) {
		unregister(NFS_program[P_NFSACL], NFSACL_V3, NFSACL_V3);
	}
#endif
}

static inline bool nfs_protocol_enabled(protos p)
{
#ifdef _USE_NFS3
	bool nfsv3 = NFS_options & CORE_OPTION_NFSV3;
#endif

	switch (p) {
	case P_NFS:
		return true;

#ifdef _USE_NFS3
	case P_MNT: /* valid only for NFSv3 environments */
		if (nfsv3)
			return true;
		break;
#endif

#ifdef _USE_NLM
	case P_NLM: /* valid only for NFSv3 environments */
		if (nfsv3 && nfs_param.core_param.enable_NLM)
			return true;
		break;
#endif

#ifdef _USE_RQUOTA
	case P_RQUOTA:
		if (nfs_param.core_param.enable_RQUOTA)
			return true;
		break;
#endif

#ifdef USE_NFSACL3
	case P_NFSACL: /* valid only for NFSv3 environments */
		if (nfsv3 && nfs_param.core_param.enable_NFSACL) {
			return true;
		}
		break;
#endif

	default:
		break;
	}

	return false;
}

/**
 * @brief Close transports and file descriptors used for RPC services.
 *
 * So that restarting the NFS server wont encounter issues of "Address
 * Already In Use" - this has occurred even though we set the
 * SO_REUSEADDR option when restarting the server with a single export
 * (i.e.: a small config) & no logging at all, making the restart very
 * fast.  when closing a listening socket it will be closed
 * immediately if no connection is pending on it, hence drastically
 * reducing the probability for trouble.
 */
static void close_rpc_fd(void)
{
	protos p;

	for (p = P_NFS; p < P_COUNT; p++) {
		if (udp_socket[p] != -1)
			close(udp_socket[p]);
		if (udp_xprt[p])
			SVC_DESTROY(udp_xprt[p]);
		if (tcp_socket[p] != -1)
			close(tcp_socket[p]);
		if (tcp_xprt[p])
			SVC_DESTROY(tcp_xprt[p]);
	}
	/* no need for special tcp_xprt[P_NFS_VSOCK] treatment */
}

/**
 * @brief Dispatch after rendezvous
 *
 * Record activity on a rendezvous transport handle.
 *
 * @note
 * Cases are distinguished by separate callbacks for each fd.
 * UDP connections are bound to socket NFS_UDPSocket
 * TCP initial connections are bound to socket NFS_TCPSocket
 * all the other cases are requests from already connected TCP Clients
 */
static enum xprt_stat nfs_rpc_dispatch_udp_NFS(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "NFS UDP request for SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
#ifdef USE_LTTNG
	tracepoint(nfs_rpc, before_recv, __func__, __LINE__, xprt);
#endif
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NFS;
	return SVC_RECV(xprt);
}

#ifdef _USE_NFS3
static enum xprt_stat nfs_rpc_dispatch_udp_MNT(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "MOUNT UDP request for SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
#ifdef USE_LTTNG
	tracepoint(nfs_rpc, before_recv, __func__, __LINE__, xprt);
#endif
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_MNT;
	return SVC_RECV(xprt);
}
#endif

#ifdef _USE_NLM
static enum xprt_stat nfs_rpc_dispatch_udp_NLM(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "NLM UDP request for SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
#ifdef USE_LTTNG
	tracepoint(nfs_rpc, before_recv, __func__, __LINE__, xprt);
#endif
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NLM;
	return SVC_RECV(xprt);
}
#endif

#ifdef _USE_RQUOTA
static enum xprt_stat nfs_rpc_dispatch_udp_RQUOTA(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "RQUOTA UDP request for SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
#ifdef USE_LTTNG
	tracepoint(nfs_rpc, before_recv, __func__, __LINE__, xprt);
#endif
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_RQUOTA;
	return SVC_RECV(xprt);
}
#endif

#ifdef USE_NFSACL3
static enum xprt_stat nfs_rpc_dispatch_udp_NFSACL(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "NFSACL UDP request for SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NFSACL;
	return SVC_RECV(xprt);
}
#endif

const svc_xprt_fun_t udp_dispatch[] = {
	nfs_rpc_dispatch_udp_NFS,
#ifdef _USE_NFS3
	nfs_rpc_dispatch_udp_MNT,
#endif
#ifdef _USE_NLM
	nfs_rpc_dispatch_udp_NLM,
#endif
#ifdef _USE_RQUOTA
	nfs_rpc_dispatch_udp_RQUOTA,
#endif
#ifdef USE_NFSACL3
	nfs_rpc_dispatch_udp_NFSACL,
#endif
#ifdef RPC_VSOCK
	NULL,
#endif
#ifdef _USE_NFS_RDMA
	NULL,
#endif
};

static enum xprt_stat nfs_rpc_dispatch_tcp_NFS(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "NFS TCP request on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NFS;
	return nfs_rpc_tcp_user_data(xprt);
}

#ifdef _USE_NFS3
static enum xprt_stat nfs_rpc_dispatch_tcp_MNT(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "MOUNT TCP request on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_MNT;
	return nfs_rpc_tcp_user_data(xprt);
}
#endif

#ifdef _USE_NLM
static enum xprt_stat nfs_rpc_dispatch_tcp_NLM(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "NLM TCP request on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NLM;
	return nfs_rpc_tcp_user_data(xprt);
}
#endif

#ifdef _USE_RQUOTA
static enum xprt_stat nfs_rpc_dispatch_tcp_RQUOTA(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "RQUOTA TCP request on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_RQUOTA;
	return nfs_rpc_tcp_user_data(xprt);
}
#endif

#ifdef USE_NFSACL3
static enum xprt_stat nfs_rpc_dispatch_tcp_NFSACL(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "NFSACL TCP request on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NFSACL;
	return nfs_rpc_tcp_user_data(xprt);
}
#endif

#ifdef RPC_VSOCK
static enum xprt_stat nfs_rpc_dispatch_tcp_VSOCK(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "VSOCK TCP request on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NFS;
	return nfs_rpc_tcp_user_data(xprt);
}
#endif

const svc_xprt_fun_t tcp_dispatch[P_COUNT] = {
	nfs_rpc_dispatch_tcp_NFS,
#ifdef _USE_NFS3
	nfs_rpc_dispatch_tcp_MNT,
#endif
#ifdef _USE_NLM
	nfs_rpc_dispatch_tcp_NLM,
#endif
#ifdef _USE_RQUOTA
	nfs_rpc_dispatch_tcp_RQUOTA,
#endif
#ifdef USE_NFSACL3
	nfs_rpc_dispatch_tcp_NFSACL,
#endif
#ifdef RPC_VSOCK
	nfs_rpc_dispatch_tcp_VSOCK,
#endif
#ifdef _USE_NFS_RDMA
	NULL,
#endif
};

void Create_udp(protos prot)
{
	if (udp_socket[prot] != -1) {
		udp_xprt[prot] =
		    svc_dg_create(udp_socket[prot],
			nfs_param.core_param.rpc.max_send_buffer_size,
			nfs_param.core_param.rpc.max_recv_buffer_size);
		if (udp_xprt[prot] == NULL)
			LogFatal(COMPONENT_DISPATCH,
				 "Cannot allocate %s/UDP SVCXPRT", tags[prot]);

		udp_xprt[prot]->xp_dispatch.rendezvous_cb = udp_dispatch[prot];

		/* Hook xp_free_user_data (finalize/free private data) */
		(void)SVC_CONTROL(udp_xprt[prot], SVCSET_XP_FREE_USER_DATA,
				  nfs_rpc_free_user_data);

		(void)svc_rqst_evchan_reg(rpc_evchan[UDP_UREG_CHAN].chan_id,
					  udp_xprt[prot],
					  SVC_RQST_FLAG_XPRT_UREG);
	}
}

void Create_tcp(protos prot)
{
	tcp_xprt[prot] =
		svc_vc_ncreatef(tcp_socket[prot],
				nfs_param.core_param.rpc.max_send_buffer_size,
				nfs_param.core_param.rpc.max_recv_buffer_size,
				SVC_CREATE_FLAG_CLOSE | SVC_CREATE_FLAG_LISTEN);
	if (tcp_xprt[prot] == NULL)
		LogFatal(COMPONENT_DISPATCH, "Cannot allocate %s/TCP SVCXPRT",
			 tags[prot]);

	tcp_xprt[prot]->xp_dispatch.rendezvous_cb = tcp_dispatch[prot];

	/* Hook xp_free_user_data (finalize/free private data) */
	(void)SVC_CONTROL(tcp_xprt[prot], SVCSET_XP_FREE_USER_DATA,
			  nfs_rpc_free_user_data);

	(void)svc_rqst_evchan_reg(rpc_evchan[TCP_UREG_CHAN].chan_id,
				  tcp_xprt[prot], SVC_RQST_FLAG_XPRT_UREG);
}

#ifdef _USE_NFS_RDMA
struct rpc_rdma_attr rpc_rdma_xa = {
	.statistics_prefix = NULL,
	.node = "::",
	.port = "20049",
	.sq_depth = 32,			/* default was 50 */
	.max_send_sge = 32,		/* minimum 2 */
	.rq_depth = 32,			/* default was 50 */
	.max_recv_sge = 31,		/* minimum 1 */
	.backlog = 10,			/* minimum 2 */
	.credits = 30,			/* default 10 */
	.destroy_on_disconnect = true,
	.use_srq = false,
};

static enum xprt_stat nfs_rpc_dispatch_RDMA(SVCXPRT *xprt)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "RDMA request on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);
	xprt->xp_dispatch.process_cb = nfs_rpc_valid_NFS;
	return SVC_STAT(xprt->xp_parent);
}

void Create_RDMA(protos prot)
{
	/* This has elements of both UDP and TCP setup */
	tcp_xprt[prot] =
		svc_rdma_create(&rpc_rdma_xa,
				nfs_param.core_param.rpc.max_send_buffer_size,
				nfs_param.core_param.rpc.max_recv_buffer_size);
	if (tcp_xprt[prot] == NULL)
		LogFatal(COMPONENT_DISPATCH, "Cannot allocate RPC/%s SVCXPRT",
			 tags[prot]);

	tcp_xprt[prot]->xp_dispatch.rendezvous_cb = nfs_rpc_dispatch_RDMA;

	/* Hook xp_free_user_data (finalize/free private data) */
	(void)SVC_CONTROL(tcp_xprt[prot], SVCSET_XP_FREE_USER_DATA,
			  nfs_rpc_free_user_data);

	(void)svc_rqst_evchan_reg(rpc_evchan[RDMA_UREG_CHAN].chan_id,
				  tcp_xprt[prot], SVC_RQST_FLAG_XPRT_UREG);
}
#endif

/**
 * @brief Create the SVCXPRT for each protocol in use
 */
void Create_SVCXPRTs(void)
{
	protos p;

	LogFullDebug(COMPONENT_DISPATCH, "Allocation of the SVCXPRT");
	for (p = P_NFS; p < P_COUNT; p++)
		if (nfs_protocol_enabled(p)) {
			Create_udp(p);
			Create_tcp(p);
		}
#ifdef RPC_VSOCK
	if (vsock)
		Create_tcp(P_NFS_VSOCK);
#endif /* RPC_VSOCK */
#ifdef _USE_NFS_RDMA
	if (rdma)
		Create_RDMA(P_NFS_RDMA);
#endif /* _USE_NFS_RDMA */
}

/**
 * @brief Bind the udp and tcp sockets for V6 Interfaces
 */
static int Bind_sockets_V6(void)
{
	protos p;
	int    rc = 0;

	if (isInfo(COMPONENT_DISPATCH)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dbuf = {sizeof(str), str, str};

		display_sockaddr(&dbuf, &nfs_param.core_param.bind_addr);
		LogInfo(COMPONENT_DISPATCH, "Binding to address %s", str);
	}

	for (p = P_NFS; p < P_COUNT; p++) {
		if (nfs_protocol_enabled(p)) {

			proto_data *pdatap = &pdata[p];

			if (udp_socket[p] != -1) {
				memset(&pdatap->sinaddr_udp6, 0,
				       sizeof(pdatap->sinaddr_udp6));
				pdatap->sinaddr_udp6.sin6_family = AF_INET6;
				/* all interfaces */
				pdatap->sinaddr_udp6.sin6_addr =
				    ((struct sockaddr_in6 *)
				    &nfs_param.core_param.bind_addr)->sin6_addr;
				pdatap->sinaddr_udp6.sin6_port =
				    htons(nfs_param.core_param.port[p]);

				pdatap->netbuf_udp6.maxlen =
				    sizeof(pdatap->sinaddr_udp6);
				pdatap->netbuf_udp6.len =
				    sizeof(pdatap->sinaddr_udp6);
				pdatap->netbuf_udp6.buf = &pdatap->sinaddr_udp6;

				pdatap->bindaddr_udp6.qlen = SOMAXCONN;
				pdatap->bindaddr_udp6.addr =
				    pdatap->netbuf_udp6;

				if (!__rpc_fd2sockinfo(udp_socket[p],
				    &pdatap->si_udp6)) {
					LogWarn(COMPONENT_DISPATCH,
						"Cannot get %s socket info for udp6 socket errno=%d (%s)",
						tags[p], errno,
						strerror(errno));
					return -1;
				}

				if (isInfo(COMPONENT_DISPATCH)) {
					char str[LOG_BUFF_LEN] = "\0";
					struct display_buffer dbuf = {
							sizeof(str), str, str};

					display_sockaddr(
					    &dbuf,
					    (sockaddr_t *)
					    pdatap->bindaddr_udp6.addr.buf);

					LogInfo(COMPONENT_DISPATCH,
						"Binding UDP socket to address %s for %s",
						str, tags[p]);
				}

				rc = bind(udp_socket[p],
					  (struct sockaddr *)
					  pdatap->bindaddr_udp6.addr.buf,
					  (socklen_t) pdatap->si_udp6.si_alen);
				if (rc == -1) {
					LogWarn(COMPONENT_DISPATCH,
						"Cannot bind %s udp6 socket, error %d (%s)",
						tags[p], errno,
						strerror(errno));
					goto exit;
				}
			}

			memset(&pdatap->sinaddr_tcp6, 0,
			       sizeof(pdatap->sinaddr_tcp6));
			pdatap->sinaddr_tcp6.sin6_family = AF_INET6;
			/* all interfaces */
			pdatap->sinaddr_tcp6.sin6_addr =
			    ((struct sockaddr_in6 *)
			    &nfs_param.core_param.bind_addr)->sin6_addr;
			pdatap->sinaddr_tcp6.sin6_port =
			    htons(nfs_param.core_param.port[p]);

			pdatap->netbuf_tcp6.maxlen =
			    sizeof(pdatap->sinaddr_tcp6);
			pdatap->netbuf_tcp6.len = sizeof(pdatap->sinaddr_tcp6);
			pdatap->netbuf_tcp6.buf = &pdatap->sinaddr_tcp6;

			pdatap->bindaddr_tcp6.qlen = SOMAXCONN;
			pdatap->bindaddr_tcp6.addr = pdatap->netbuf_tcp6;

			if (!__rpc_fd2sockinfo(tcp_socket[p],
			    &pdatap->si_tcp6)) {
				LogWarn(COMPONENT_DISPATCH,
					 "Cannot get %s socket info for tcp6 socket errno=%d (%s)",
					 tags[p], errno, strerror(errno));
				return -1;
			}

			if (isInfo(COMPONENT_DISPATCH)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dbuf = {
						sizeof(str), str, str};

				display_sockaddr(
				    &dbuf,
				    (sockaddr_t *)
				    pdatap->bindaddr_tcp6.addr.buf);

				LogInfo(COMPONENT_DISPATCH,
					"Binding TCP socket to address %s for %s",
					str, tags[p]);
			}

			rc = bind(tcp_socket[p],
				  (struct sockaddr *)
				   pdatap->bindaddr_tcp6.addr.buf,
				 (socklen_t) pdatap->si_tcp6.si_alen);
			if (rc == -1) {
				LogWarn(COMPONENT_DISPATCH,
					"Cannot bind %s tcp6 socket, error %d (%s)",
					tags[p], errno, strerror(errno));
				goto exit;
			}
		}
	}

exit:
	return rc;
}

/**
 * @brief Bind the udp and tcp sockets for V4 Interfaces
 */
static int Bind_sockets_V4(void)
{
	protos p;
	int    rc = 0;

	if (isInfo(COMPONENT_DISPATCH)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dbuf = {sizeof(str), str, str};

		display_sockaddr(&dbuf, &nfs_param.core_param.bind_addr);
		LogInfo(COMPONENT_DISPATCH, "Binding to address %s", str);
	}

	for (p = P_NFS; p < P_COUNT; p++) {
		if (nfs_protocol_enabled(p)) {

			proto_data *pdatap = &pdata[p];

			if (udp_socket[p] != -1) {
				memset(&pdatap->sinaddr_udp, 0,
				       sizeof(pdatap->sinaddr_udp));
				pdatap->sinaddr_udp.sin_family = AF_INET;
				/* all interfaces */
				pdatap->sinaddr_udp.sin_addr.s_addr =
				    ((struct sockaddr_in *)
				    &nfs_param.core_param.bind_addr)->
				    sin_addr.s_addr;
				pdatap->sinaddr_udp.sin_port =
				    htons(nfs_param.core_param.port[p]);

				pdatap->netbuf_udp6.maxlen =
				    sizeof(pdatap->sinaddr_udp);
				pdatap->netbuf_udp6.len =
				    sizeof(pdatap->sinaddr_udp);
				pdatap->netbuf_udp6.buf = &pdatap->sinaddr_udp;

				pdatap->bindaddr_udp6.qlen = SOMAXCONN;
				pdatap->bindaddr_udp6.addr =
				    pdatap->netbuf_udp6;

				if (!__rpc_fd2sockinfo(udp_socket[p],
				    &pdatap->si_udp6)) {
					LogWarn(COMPONENT_DISPATCH,
						"Cannot get %s socket info for udp6 socket errno=%d (%s)",
						tags[p], errno,
						strerror(errno));
					return -1;
				}

				if (isInfo(COMPONENT_DISPATCH)) {
					char str[LOG_BUFF_LEN] = "\0";
					struct display_buffer dbuf = {
							sizeof(str), str, str};

					display_sockaddr(
					    &dbuf,
					    (sockaddr_t *)
					    pdatap->bindaddr_udp6.addr.buf);

					LogInfo(COMPONENT_DISPATCH,
						"Binding UDP socket to address %s for %s",
						str, tags[p]);
				}

				rc = bind(udp_socket[p],
					  (struct sockaddr *)
					  pdatap->bindaddr_udp6.addr.buf,
					  (socklen_t) pdatap->si_udp6.si_alen);
				if (rc == -1) {
					LogWarn(COMPONENT_DISPATCH,
						"Cannot bind %s udp6 socket, error %d (%s)",
						tags[p], errno,
						strerror(errno));
					return -1;
				}
			}

			memset(&pdatap->sinaddr_tcp, 0,
			       sizeof(pdatap->sinaddr_tcp));
			pdatap->sinaddr_tcp.sin_family = AF_INET;
			/* all interfaces */
			pdatap->sinaddr_tcp.sin_addr.s_addr =
			    ((struct sockaddr_in *)
			    &nfs_param.core_param.bind_addr)->sin_addr.s_addr;
			pdatap->sinaddr_tcp.sin_port =
			    htons(nfs_param.core_param.port[p]);

			pdatap->netbuf_tcp6.maxlen =
			    sizeof(pdatap->sinaddr_tcp);
			pdatap->netbuf_tcp6.len = sizeof(pdatap->sinaddr_tcp);
			pdatap->netbuf_tcp6.buf = &pdatap->sinaddr_tcp;

			pdatap->bindaddr_tcp6.qlen = SOMAXCONN;
			pdatap->bindaddr_tcp6.addr = pdatap->netbuf_tcp6;

			if (!__rpc_fd2sockinfo(tcp_socket[p],
			    &pdatap->si_tcp6)) {
				LogWarn(COMPONENT_DISPATCH,
					"V4 : Cannot get %s socket info for tcp socket error %d(%s)",
					tags[p], errno, strerror(errno));
				return -1;
			}

			if (isInfo(COMPONENT_DISPATCH)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dbuf = {
						sizeof(str), str, str};

				display_sockaddr(
				    &dbuf,
				    (sockaddr_t *)
				    pdatap->bindaddr_tcp6.addr.buf);

				LogInfo(COMPONENT_DISPATCH,
					"Binding TCP socket to address %s for %s",
					str, tags[p]);
			}

			rc = bind(tcp_socket[p],
				  (struct sockaddr *)
				  pdatap->bindaddr_tcp6.addr.buf,
				  (socklen_t) pdatap->si_tcp6.si_alen);
			if (rc == -1) {
				LogWarn(COMPONENT_DISPATCH,
					"Cannot bind %s tcp socket, error %d(%s)",
					tags[p], errno, strerror(errno));
				return -1;
			}
		}
	}

	return rc;
}

#ifdef RPC_VSOCK
int bind_sockets_vsock(void)
{
	int rc = 0;

	struct sockaddr_vm sa_listen = {
		.svm_family = AF_VSOCK,
		.svm_cid = VMADDR_CID_ANY,
		.svm_port = nfs_param.core_param.port[P_NFS],
	};

	rc = bind(tcp_socket[P_NFS_VSOCK], (struct sockaddr *)
		(struct sockaddr *)&sa_listen, sizeof(sa_listen));
	if (rc == -1) {
		LogWarn(COMPONENT_DISPATCH,
			"cannot bind %s stream socket, error %d(%s)",
			tags[P_NFS_VSOCK], errno, strerror(errno));
	}
	return rc;
}
#endif /* RPC_VSOCK */

void Bind_sockets(void)
{
	int rc = 0;

	/*
	 * See Allocate_sockets(), which should already
	 * have set the global v6disabled accordingly
	 */
	if (v6disabled) {
		rc = Bind_sockets_V4();
		if (rc)
			LogFatal(COMPONENT_DISPATCH,
				 "Error binding to V4 interface. Cannot continue.");
	} else {
		rc = Bind_sockets_V6();
		if (rc)
			LogFatal(COMPONENT_DISPATCH,
				 "Error binding to V6 interface. Cannot continue.");
	}
#ifdef RPC_VSOCK
	if (vsock) {
		rc = bind_sockets_vsock();
		if (rc)
			LogMajor(COMPONENT_DISPATCH,
				"AF_VSOCK bind failed (continuing startup)");
	}
#endif /* RPC_VSOCK */
	LogInfo(COMPONENT_DISPATCH,
		"Bind sockets successful, v6disabled = %d, vsock = %d, rdma = %d",
		v6disabled, vsock, rdma);
}

/**
 * @brief Function to set the socket options on the allocated
 *	  udp and tcp sockets
 *
 */
static int alloc_socket_setopts(int p)
{
	int one = 1;
	const struct nfs_core_param *nfs_cp = &nfs_param.core_param;

	/* Use SO_REUSEADDR in order to avoid wait
	 * the 2MSL timeout */
	if (udp_socket[p] != -1) {
		if (setsockopt(udp_socket[p],
			       SOL_SOCKET, SO_REUSEADDR,
			       &one, sizeof(one))) {
			LogWarn(COMPONENT_DISPATCH,
				"Bad udp socket options for %s, error %d(%s)",
				tags[p], errno, strerror(errno));

			return -1;
		}
	}

	if (setsockopt(tcp_socket[p],
		       SOL_SOCKET, SO_REUSEADDR,
		       &one, sizeof(one))) {
		LogWarn(COMPONENT_DISPATCH,
			"Bad tcp socket option reuseaddr for %s, error %d(%s)",
			tags[p], errno, strerror(errno));

		return -1;
	}

	if (nfs_cp->enable_tcp_keepalive) {
		if (setsockopt(tcp_socket[p],
			       SOL_SOCKET, SO_KEEPALIVE,
			       &one, sizeof(one))) {
			LogWarn(COMPONENT_DISPATCH,
				"Bad tcp socket option keepalive for %s, error %d(%s)",
				tags[p], errno, strerror(errno));

			return -1;
		}

		if (nfs_cp->tcp_keepcnt) {
			if (setsockopt(tcp_socket[p], IPPROTO_TCP, TCP_KEEPCNT,
				       &nfs_cp->tcp_keepcnt,
				       sizeof(nfs_cp->tcp_keepcnt))) {
				LogWarn(COMPONENT_DISPATCH,
					"Bad tcp socket option TCP_KEEPCNT for %s, error %d(%s)",
					tags[p], errno, strerror(errno));

				return -1;
			}
		}

		if (nfs_cp->tcp_keepidle) {
			if (setsockopt(tcp_socket[p], IPPROTO_TCP, TCP_KEEPIDLE,
				       &nfs_cp->tcp_keepidle,
				       sizeof(nfs_cp->tcp_keepidle))) {
				LogWarn(COMPONENT_DISPATCH,
					"Bad tcp socket option TCP_KEEPIDLE for %s, error %d(%s)",
					tags[p], errno, strerror(errno));

				return -1;
			}
		}

		if (nfs_cp->tcp_keepintvl) {
			if (setsockopt(tcp_socket[p], IPPROTO_TCP,
				       TCP_KEEPINTVL, &nfs_cp->tcp_keepintvl,
				       sizeof(nfs_cp->tcp_keepintvl))) {
				LogWarn(COMPONENT_DISPATCH,
					"Bad tcp socket option TCP_KEEPINTVL for %s, error %d(%s)",
					tags[p], errno, strerror(errno));

				return -1;
			}
		}
	}

	if (udp_socket[p] != -1) {
		/* We prefer using non-blocking socket
		 * in the specific case */
		if (fcntl(udp_socket[p], F_SETFL, FNDELAY) == -1) {
			LogWarn(COMPONENT_DISPATCH,
				"Cannot set udp socket for %s as non blocking, error %d(%s)",
				tags[p], errno, strerror(errno));

			return -1;
		}
	}

	return 0;
}

static bool enable_udp_listener(protos prot)
{
	if (nfs_param.core_param.enable_UDP & UDP_LISTENER_ALL)
		return true;
#ifdef _USE_NFS3
	if (prot == P_MNT &&
	    (nfs_param.core_param.enable_UDP & UDP_LISTENER_MOUNT))
		return true;
#endif
	return false;
}

/**
 * @brief Allocate the tcp and udp sockets for the nfs daemon
 * using V4 interfaces
 */
static int Allocate_sockets_V4(int p)
{
	udp_socket[p] = tcp_socket[p] = -1;
	if (enable_udp_listener(p)) {
		udp_socket[p] = socket(AF_INET,
				       SOCK_DGRAM,
				       IPPROTO_UDP);

		if (udp_socket[p] == -1) {
			if (errno == EAFNOSUPPORT) {
				LogInfo(COMPONENT_DISPATCH,
					"No V6 and V4 intfs configured?!");
			}

			LogWarn(COMPONENT_DISPATCH,
				"Cannot allocate a udp socket for %s, error %d(%s)",
				tags[p], errno, strerror(errno));

			return -1;
		}
	}

	tcp_socket[p] = socket(AF_INET,
			       SOCK_STREAM,
			       IPPROTO_TCP);

	if (tcp_socket[p] == -1) {
		LogWarn(COMPONENT_DISPATCH,
			"Cannot allocate a tcp socket for %s, error %d(%s)",
			tags[p], errno, strerror(errno));
		return -1;
	}

	return 0;

}

#ifdef RPC_VSOCK
/**
 * @brief Create vmci stream socket
 */
static int allocate_socket_vsock(void)
{
	int one = 1;

	tcp_socket[P_NFS_VSOCK] = socket(AF_VSOCK, SOCK_STREAM, 0);
	if (tcp_socket[P_NFS_VSOCK] == -1) {
		LogWarn(COMPONENT_DISPATCH,
			"socket create failed for %s, error %d(%s)",
			tags[P_NFS_VSOCK], errno, strerror(errno));
		return -1;
	}
	if (setsockopt(tcp_socket[P_NFS_VSOCK],
			SOL_SOCKET, SO_REUSEADDR,
			&one, sizeof(one))) {
		LogWarn(COMPONENT_DISPATCH,
			"bad tcp socket options for %s, error %d(%s)",
			tags[P_NFS_VSOCK], errno, strerror(errno));

		return -1;
	}

	LogDebug(COMPONENT_DISPATCH,
		"Socket numbers are: %s tcp=%d",
		tags[P_NFS_VSOCK],
		tcp_socket[P_NFS_VSOCK]);
	return 0;
}
#endif /* RPC_VSOCK */

/**
 * @brief Allocate the tcp and udp sockets for the nfs daemon
 */
static void Allocate_sockets(void)
{
	protos	p;
	int	rc = 0;

	LogFullDebug(COMPONENT_DISPATCH, "Allocation of the sockets");

	for (p = P_NFS; p < P_COUNT; p++) {
		/* Initialize all the sockets to -1 because
		 * it makes some code later easier */
		udp_socket[p] = tcp_socket[p] = -1;

		if (nfs_protocol_enabled(p)) {
			if (v6disabled)
				goto try_V4;

			if (enable_udp_listener(p)) {
				udp_socket[p] = socket(AF_INET6,
						       SOCK_DGRAM,
						       IPPROTO_UDP);

				if (udp_socket[p] == -1) {
					/*
					 * We assume that EAFNOSUPPORT points
					 * to the likely case when the host has
					 * V6 interfaces disabled. So we will
					 * try to use the existing V4 interfaces
					 * instead
					 */
					if (errno == EAFNOSUPPORT) {
						v6disabled = true;
						LogWarn(COMPONENT_DISPATCH,
						    "System may not have V6 intfs configured error %d(%s)",
						    errno, strerror(errno));

						goto try_V4;
					}

					LogFatal(COMPONENT_DISPATCH,
						 "Cannot allocate a udp socket for %s, error %d(%s)",
						 tags[p], errno,
						 strerror(errno));
				}
			}

			tcp_socket[p] = socket(AF_INET6,
					       SOCK_STREAM,
					       IPPROTO_TCP);

			if (tcp_socket[p] == -1) {
				/* We fail with LogFatal here on error because
				 * it shouldn't be that we have managed to
				 * create a V6 based udp socket and have failed
				 * for the tcp sock. If it were a case of V6
				 * being disabled, then we would have
				 * encountered that case with the first udp
				 * sock create and would have moved on to
				 * create the V4 sockets.
				 */
				if (enable_udp_listener(p)
				    || errno != EAFNOSUPPORT) {
					LogFatal(COMPONENT_DISPATCH,
						 "Cannot allocate a tcp socket for %s, error %d(%s)",
						 tags[p], errno,
						 strerror(errno));
				}

				v6disabled = true;
				LogWarn(COMPONENT_DISPATCH,
				    "System may not have V6 intfs configured error %d(%s)",
				    errno, strerror(errno));

				goto try_V4;
			}

try_V4:
			if (v6disabled) {
				rc = Allocate_sockets_V4(p);
				if (rc) {
					LogFatal(COMPONENT_DISPATCH,
						 "Error allocating V4 socket for proto %d, %s",
						 p, tags[p]);
				}
			}

			rc = alloc_socket_setopts(p);
			if (rc) {
				LogFatal(COMPONENT_DISPATCH,
					 "Error setting socket option for proto %d, %s",
					 p, tags[p]);
			}
			LogDebug(COMPONENT_DISPATCH,
				"Socket numbers are: %s tcp=%d udp=%d",
				tags[p],
				tcp_socket[p],
				udp_socket[p]);
		}
	}
#ifdef RPC_VSOCK
	if (vsock)
		allocate_socket_vsock();
#endif /* RPC_VSOCK */
}

/* The following routine must ONLY be called from the shutdown
 * thread */
void Clean_RPC(void)
{
  /**
   * @todo Consider the need to call Svc_dg_destroy for UDP & ?? for
   * TCP based services
   */
	unregister_rpc();
	close_rpc_fd();

	freenetconfigent(netconfig_udpv4);
	freenetconfigent(netconfig_tcpv4);
	freenetconfigent(netconfig_udpv6);
	freenetconfigent(netconfig_tcpv6);
}

#define UDP_REGISTER(prot, vers, netconfig) \
	svc_reg(udp_xprt[prot], NFS_program[prot], \
		(u_long) vers,					    \
		nfs_rpc_dispatch_dummy, netconfig)

#define TCP_REGISTER(prot, vers, netconfig) \
	svc_reg(tcp_xprt[prot], NFS_program[prot], \
		(u_long) vers,					    \
		nfs_rpc_dispatch_dummy, netconfig)

#ifdef RPCBIND
static bool __Register_program(protos prot, int vers)
{
	if (enable_udp_listener(prot)) {
		LogInfo(COMPONENT_DISPATCH, "Registering %s V%d/UDP",
			tags[prot], (int)vers);

		/* XXXX fix svc_register! */
		if (!UDP_REGISTER(prot, vers, netconfig_udpv4)) {
			LogMajor(COMPONENT_DISPATCH,
				 "Cannot register %s V%d on UDP",
				 tags[prot], (int)vers);
			return false;
		}

		if (!v6disabled && netconfig_udpv6) {
			LogInfo(COMPONENT_DISPATCH,
				"Registering %s V%d/UDPv6",
				tags[prot], (int)vers);
			if (!UDP_REGISTER(prot, vers,
					  netconfig_udpv6)) {
				LogMajor(COMPONENT_DISPATCH,
					 "Cannot register %s V%d on UDPv6",
					 tags[prot], (int)vers);
				return false;
			}
		}
	}

#ifndef _NO_TCP_REGISTER
	LogInfo(COMPONENT_DISPATCH, "Registering %s V%d/TCP",
		tags[prot], (int)vers);

	if (!TCP_REGISTER(prot, vers, netconfig_tcpv4)) {
		LogMajor(COMPONENT_DISPATCH,
			 "Cannot register %s V%d on TCP", tags[prot],
			 (int)vers);
		return false;
	}

	if (!v6disabled && netconfig_tcpv6) {
		LogInfo(COMPONENT_DISPATCH, "Registering %s V%d/TCPv6",
			tags[prot], (int)vers);
		if (!TCP_REGISTER(prot, vers, netconfig_tcpv6)) {
			LogMajor(COMPONENT_DISPATCH,
				 "Cannot register %s V%d on TCPv6",
				 tags[prot], (int)vers);
			return false;
		}
	}
#endif				/* _NO_TCP_REGISTER */
	return true;
}

static inline void Register_program(protos prot, int vers)
{
	if (!__Register_program(prot, vers))
		Fatal();
}
#endif /* RPCBIND */

/**
 * @brief Init the svc descriptors for the nfs daemon
 *
 * Perform all the required initialization for the RPC subsystem and event
 * channels.
 */
void nfs_Init_svc(void)
{
	svc_init_params svc_params;
	int ix;
	int code;

	LogDebug(COMPONENT_DISPATCH, "NFS INIT: Core options = %d",
		 NFS_options);

	LogInfo(COMPONENT_DISPATCH, "NFS INIT: using TIRPC");

	memset(&svc_params, 0, sizeof(svc_params));

#ifdef __FreeBSD__
	v6disabled = true;
#else
	v6disabled = false;
#endif
#ifdef RPC_VSOCK
	vsock = NFS_options & CORE_OPTION_NFS_VSOCK;
#endif
#ifdef _USE_NFS_RDMA
	rdma = NFS_options & CORE_OPTION_NFS_RDMA;
#endif

	/* New TI-RPC package init function */
	svc_params.disconnect_cb = NULL;
	svc_params.alloc_cb = alloc_nfs_request;
	svc_params.free_cb = free_nfs_request;
	svc_params.flags = SVC_INIT_EPOLL;	/* use EPOLL event mgmt */
	svc_params.flags |= SVC_INIT_NOREG_XPRTS; /* don't call xprt_register */
	svc_params.max_connections = nfs_param.core_param.rpc.max_connections;
	svc_params.max_events = 1024;	/* length of epoll event queue */
	svc_params.ioq_send_max =
	    nfs_param.core_param.rpc.max_send_buffer_size;
	svc_params.channels = N_EVENT_CHAN;
	svc_params.idle_timeout = nfs_param.core_param.rpc.idle_timeout_s;
	svc_params.ioq_thrd_min = nfs_param.core_param.rpc.ioq_thrd_min;
	svc_params.ioq_thrd_max = nfs_param.core_param.rpc.ioq_thrd_max;
	/* GSS ctx cache tuning, expiration */
	svc_params.gss_ctx_hash_partitions =
		nfs_param.core_param.rpc.gss.ctx_hash_partitions;
	svc_params.gss_max_ctx =
		nfs_param.core_param.rpc.gss.max_ctx;
	svc_params.gss_max_gc =
		nfs_param.core_param.rpc.gss.max_gc;

	/* Only after TI-RPC allocators, log channel are setup */
	if (!svc_init(&svc_params))
		LogFatal(COMPONENT_INIT, "SVC initialization failed");

	for (ix = 0; ix < EVCHAN_SIZE; ++ix) {
		rpc_evchan[ix].chan_id = 0;
		code = svc_rqst_new_evchan(&rpc_evchan[ix].chan_id,
					   NULL /* u_data */,
					   SVC_RQST_FLAG_NONE);
		if (code)
			LogFatal(COMPONENT_DISPATCH,
				 "Cannot create TI-RPC event channel (%d, %d)",
				 ix, code);
		/* XXX bail?? */
	}

	/* Get the netconfig entries from /etc/netconfig */
	netconfig_udpv4 = (struct netconfig *)getnetconfigent("udp");
	if (netconfig_udpv4 == NULL)
		LogFatal(COMPONENT_DISPATCH,
			 "Cannot get udp netconfig, cannot get an entry for udp in netconfig file. Check file /etc/netconfig...");

	/* Get the netconfig entries from /etc/netconfig */
	netconfig_tcpv4 = (struct netconfig *)getnetconfigent("tcp");
	if (netconfig_tcpv4 == NULL)
		LogFatal(COMPONENT_DISPATCH,
			 "Cannot get tcp netconfig, cannot get an entry for tcp in netconfig file. Check file /etc/netconfig...");

	/* A short message to show that /etc/netconfig parsing was a success */
	LogFullDebug(COMPONENT_DISPATCH, "netconfig found for UDPv4 and TCPv4");

	LogInfo(COMPONENT_DISPATCH, "NFS INIT: Using IPv6");

	/* Get the netconfig entries from /etc/netconfig */
	netconfig_udpv6 = (struct netconfig *)getnetconfigent("udp6");
	if (netconfig_udpv6 == NULL)
		LogInfo(COMPONENT_DISPATCH,
			"Cannot get udp6 netconfig, cannot get an entry for udp6 in netconfig file. Check file /etc/netconfig...");

	/* Get the netconfig entries from /etc/netconfig */
	netconfig_tcpv6 = (struct netconfig *)getnetconfigent("tcp6");
	if (netconfig_tcpv6 == NULL)
		LogInfo(COMPONENT_DISPATCH,
			 "Cannot get tcp6 netconfig, cannot get an entry for tcp in netconfig file. Check file /etc/netconfig...");

	/* A short message to show that /etc/netconfig parsing was a success
	 * for ipv6
	 */
	if (netconfig_udpv6 && netconfig_tcpv6)
		LogFullDebug(COMPONENT_DISPATCH,
			     "netconfig found for UDPv6 and TCPv6");

	/* Allocate the UDP and TCP sockets for the RPC */
	Allocate_sockets();

	if ((NFS_options & CORE_OPTION_ALL_NFS_VERS) != 0) {
		/* Bind the tcp and udp sockets */
		Bind_sockets();

		/* Unregister from portmapper/rpcbind */
		unregister_rpc();

		/* Set up well-known xprt handles */
		Create_SVCXPRTs();
	}

#ifdef _HAVE_GSSAPI
	/* Acquire RPCSEC_GSS basis if needed */
	if (nfs_param.krb5_param.active_krb5) {
		if (!svcauth_gss_import_name
		    (nfs_param.krb5_param.svc.principal)) {
			LogFatal(COMPONENT_DISPATCH,
				 "Could not import principal name %s into GSSAPI",
				 nfs_param.krb5_param.svc.principal);
		} else {
			LogInfo(COMPONENT_DISPATCH,
				"Successfully imported principal %s into GSSAPI",
				nfs_param.krb5_param.svc.principal);

			/* Trying to acquire a credentials
			 * for checking name's validity */
			if (!svcauth_gss_acquire_cred())
				LogCrit(COMPONENT_DISPATCH,
					"Cannot acquire credentials for principal %s",
					nfs_param.krb5_param.svc.principal);
			else
				LogDebug(COMPONENT_DISPATCH,
					 "Principal %s is suitable for acquiring credentials",
					 nfs_param.krb5_param.svc.principal);
		}
	}
#endif				/* _HAVE_GSSAPI */

#ifdef RPCBIND
	/*
	 * Perform all the RPC registration, for UDP and TCP, on both NFS_V3
	 * and NFS_V4. Note that v4 servers are not required to register with
	 * rpcbind, so we don't fail to start if only that fails.
	 */
#ifdef _USE_NFS3
	if (NFS_options & CORE_OPTION_NFSV3) {
		Register_program(P_NFS, NFS_V3);
		Register_program(P_MNT, MOUNT_V1);
		Register_program(P_MNT, MOUNT_V3);
#ifdef _USE_NLM
		if (nfs_param.core_param.enable_NLM)
			Register_program(P_NLM, NLM4_VERS);
#endif /* _USE_NLM */
#ifdef USE_NFSACL3
		if (nfs_param.core_param.enable_NFSACL)
			Register_program(P_NFSACL, NFSACL_V3);
#endif
	}
#endif /* _USE_NFS3 */

	/* v4 registration is optional */
	if (NFS_options & CORE_OPTION_NFSV4)
		__Register_program(P_NFS, NFS_V4);

#ifdef _USE_RQUOTA
	if (nfs_param.core_param.enable_RQUOTA &&
	    (NFS_options & CORE_OPTION_ALL_NFS_VERS)) {
		Register_program(P_RQUOTA, RQUOTAVERS);
		Register_program(P_RQUOTA, EXT_RQUOTAVERS);
	}
#endif
#endif	/* RPCBIND */
}

/**
 * @brief Rendezvous callout.  This routine will be called by TI-RPC
 *        after newxprt has been accepted.
 *
 * Register newxprt on a TCP event channel.  Balancing events/channels
 * could become involved.  To start with, just cycle through them as
 * new connections are accepted.
 *
 * @param[in] newxprt Newly created transport
 *
 * @return status of parent.
 */
static enum xprt_stat nfs_rpc_tcp_user_data(SVCXPRT *newxprt)
{
	return SVC_STAT(newxprt->xp_parent);
}

/**
 * @brief xprt destructor callout
 *
 * @param[in] xprt Transport to destroy
 */
static enum xprt_stat nfs_rpc_free_user_data(SVCXPRT *xprt)
{
	if (xprt->xp_u2) {
		nfs_dupreq_put_drc(xprt->xp_u2);
		xprt->xp_u2 = NULL;
	}
	return XPRT_DESTROYED;
}

/**
 * @brief Allocate a new request
 *
 * @param[in] xprt Transport to use
 * @param[in] xdrs XDR to use
 *
 * @return New svc request
 */
static struct svc_req *alloc_nfs_request(SVCXPRT *xprt, XDR *xdrs)
{
	nfs_request_t *reqdata = gsh_calloc(1, sizeof(nfs_request_t));

	if (!xprt) {
		LogFatal(COMPONENT_DISPATCH,
			 "missing xprt!");
	}

	if (!xdrs) {
		LogFatal(COMPONENT_DISPATCH,
			 "missing xdrs!");
	}

	LogDebug(COMPONENT_DISPATCH,
		 "%p fd %d context %p",
		 xprt, xprt->xp_fd, xdrs);

	(void) atomic_inc_uint64_t(&nfs_health_.enqueued_reqs);

	/* set up req */
	SVC_REF(xprt, SVC_REF_FLAG_NONE);
	reqdata->svc.rq_xprt = xprt;
	reqdata->svc.rq_xdrs = xdrs;
	reqdata->svc.rq_refcnt = 1;

	TAILQ_INIT_ENTRY(reqdata, dupes);

	return &reqdata->svc;
}

static void free_nfs_request(struct svc_req *req, enum xprt_stat stat)
{
	nfs_request_t *reqdata = container_of(req, nfs_request_t, svc);
	SVCXPRT *xprt = reqdata->svc.rq_xprt;

	if (unlikely(stat > XPRT_DESTROYED)) {
		LogInfo(COMPONENT_DISPATCH,
			"SVC_DECODE on %p fd %d returned unknown %u",
			xprt, xprt->xp_fd,
			stat);
	} else if (isDebug(COMPONENT_DISPATCH)) {
		sockaddr_t addr;
		char addrbuf[SOCK_NAME_MAX];
		struct display_buffer dspbuf = {
					sizeof(addrbuf), addrbuf, addrbuf};

		copy_xprt_addr(&addr, xprt);
		display_sockaddr(&dspbuf, &addr);

		LogDebug(COMPONENT_DISPATCH,
			 "SVC_DECODE on %p fd %d (%s) xid=%" PRIu32
			 " returned %s",
			 xprt, xprt->xp_fd,
			 addrbuf,
			 reqdata->svc.rq_msg.rm_xid,
			 xprt_stat_s[stat]);
	}

	LogFullDebug(COMPONENT_DISPATCH,
		     "%s: %p fd %d xp_refcnt %" PRIu32,
		     __func__, xprt, xprt->xp_fd, xprt->xp_refcnt);

	gsh_free(reqdata);

	SVC_RELEASE(xprt, SVC_REF_FLAG_NONE);

	(void) atomic_inc_uint64_t(&nfs_health_.dequeued_reqs);
}
