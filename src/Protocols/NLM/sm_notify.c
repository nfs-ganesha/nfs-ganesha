// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *             : M. Mohan Kumar <mohan@in.ibm.com>
 *
 * --------------------------
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
 *
 */

#include <memory.h> /* for memset */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <rpc/types.h>
#include <rpc/nettype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netconfig.h>
#include "nsm.h"
#include "ip_utils.h"

#define STR_SIZE 100

char *argv0;

#define USAGE                                       \
	"usage: %s [-p <port>] -l <local address> " \
	"-m <monitor host> -r <remote address> -s <state>\n\n" \
	"Only IPv4 and IPv6 addresses are supported\n"

#define ERR_MSG1 "%s address too long\n"

/* attempt to match (irrational) behaviour of previous versions */
static const struct timespec tout = { 15, 0 };

/* This function is dragged in by the use of abstract_mem.h, so
 * we define a simple version that does a printf rather than
 * pull in the entirety of log_functions.c into this standalone
 * program.
 */
void LogMallocFailure(const char *file, int line, const char *function,
		      const char *allocator)
{
	printf("Aborting %s due to out of memory", allocator);
}

void rpc_warnx(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");

	va_end(ap);
}

static void tirpc_thread_name(const char *p)
{
	/* do nothing */
}

tirpc_pkg_params ntirpc_pp = {
#if 0
	UINT32_MAX,
#else
	TIRPC_DEBUG_FLAG_DEFAULT,
#endif
	0,
	tirpc_thread_name,
	(mem_format_t)rpc_warnx,
	gsh_free_size,
	gsh_malloc__,
	gsh_malloc_aligned__,
	gsh_calloc__,
	gsh_realloc__,
};

CLIENT *client_create(sockaddr_t *caddr, sockaddr_t *saddr, uint32_t port,
		      int prog, int vers)
{
	CLIENT *clnt;
	struct netbuf cli_netbuf = { sizeof(sockaddr_t), sizeof(sockaddr_t),
				     caddr };
	struct netbuf srv_netbuf = { sizeof(sockaddr_t), sizeof(sockaddr_t),
				     saddr };
	int net_port = htons(port);
	struct netconfig *nconf;
	char client[256];
	struct display_buffer clbuf = { sizeof(client), client, client };
	char server[256];
	struct display_buffer svbuf = { sizeof(server), server, server };

	/* Need to set port in client_address for complete sendback
	 * address
	 */
	switch (saddr->ss_family) {
	case AF_INET:
		((struct sockaddr_in *)saddr)->sin_port = net_port;
		nconf = getnetconfigent("udp");
		break;

	case AF_INET6:
		((struct sockaddr_in6 *)saddr)->sin6_port = net_port;
		nconf = getnetconfigent("udp6");
		break;

	default:
		fprintf(stderr, "Unexpected address family\n\n");
		fprintf(stderr, USAGE, argv0);
		exit(1);
	}

	display_sockaddr(&clbuf, caddr);
	display_sockaddr(&svbuf, saddr);

	fprintf(stderr,
		"Creating client for %s family %d server %s family %d nconf=%p\n",
		client, caddr->ss_family, server, saddr->ss_family, nconf);

	clnt = clnt_tli_ncreate_opt(RPC_ANYFD, nconf, &cli_netbuf, &srv_netbuf,
				    prog, vers, 0, 0, !IPV6_V6ONLY);

	if (CLNT_FAILURE(clnt)) {
		char *err = rpc_sperror(&clnt->cl_error, "failed");

		fprintf(stderr, "connect to client %s\n", err);

		gsh_free(err);
		CLNT_DESTROY(clnt);
		clnt = NULL;
	}

	freenetconfigent(nconf);

	return clnt;
}

uint32_t portmap_1(sockaddr_t *caddr, sockaddr_t *saddr)
{
	static char clnt_res;
	struct clnt_req *cc;
	enum clnt_stat ret;
	uint32_t res;
	struct pmap pmap_args;
	CLIENT *clnt;

	clnt = client_create(caddr, saddr, PMAPPORT, RPCBPROG, PMAPVERS);

	if (clnt == NULL)
		return 0;

	pmap_args.pm_prog = SM_PROG;
	pmap_args.pm_vers = SM_VERS;
	pmap_args.pm_prot = IPPROTO_UDP;
	pmap_args.pm_port = 0;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));

	cc = gsh_malloc(sizeof(*cc));

	clnt_req_fill(cc, clnt, authnone_ncreate(), PMAPPROC_GETPORT,
		      (xdrproc_t)xdr_pmap, &pmap_args, (xdrproc_t)xdr_uint32_t,
		      &res);

	/* Now set up the request */
	ret = clnt_req_setup(cc, tout);

	if (ret == RPC_SUCCESS) {
		cc->cc_refreshes = 1;
		ret = CLNT_CALL_WAIT(cc);
	}

	if (ret != RPC_SUCCESS) {
		char *err = rpc_sperror(&cc->cc_error, "failed");

		fprintf(stderr, "PMAPPROC_GETPORT failed error: %s\n", err);
		gsh_free(err);
		res = 0;
	}

	clnt_req_release(cc);

	return res;
}

int nsm_notify_1(notify *argp, sockaddr_t *caddr, sockaddr_t *saddr)
{
	char clnt_res;
	struct clnt_req *cc;
	enum clnt_stat ret;
	CLIENT *clnt;
	int port = portmap_1(caddr, saddr);

	if (port == 0)
		return 1;

	clnt = client_create(caddr, saddr, port, SM_PROG, SM_VERS);

	if (clnt == NULL)
		return 1;

	memset((char *)&clnt_res, 0, sizeof(clnt_res));

	cc = gsh_malloc(sizeof(*cc));

	clnt_req_fill(cc, clnt, authnone_ncreate(), SM_NOTIFY,
		      (xdrproc_t)xdr_notify, argp, (xdrproc_t)xdr_void,
		      &clnt_res);

	ret = clnt_req_setup(cc, tout);

	if (ret == RPC_SUCCESS) {
		cc->cc_refreshes = 1;
		ret = CLNT_CALL_WAIT(cc);
	}

	if (ret != RPC_SUCCESS) {
		char *err = rpc_sperror(&cc->cc_error, "failed");

		fprintf(stderr, "SM_NOTIFY failed error: %s\n", err);
		gsh_free(err);
	}

	clnt_req_release(cc);

	/* Return 0 for success, 1 for failure */
	return ret != RPC_SUCCESS;
}

enum evchan {
	UDP_UREG_CHAN, /*< Put UDP on a dedicated channel */
	TCP_UREG_CHAN, /*< Accepts new TCP connections */
	EVCHAN_SIZE
};

#define N_TCP_EVENT_CHAN \
	3 /*< We don't really want to have too many,
				   relative to the number of available cores. */
#define N_EVENT_CHAN (N_TCP_EVENT_CHAN + EVCHAN_SIZE)

struct rpc_evchan {
	uint32_t chan_id; /*< Channel ID */
};

static struct rpc_evchan rpc_evchan[EVCHAN_SIZE];

static struct svc_req *alloc_nfs_request(SVCXPRT *xprt, XDR *xdrs)
{
	struct svc_req *req = gsh_calloc(1, sizeof(*req));

	/* set up req */
	SVC_REF(xprt, SVC_REF_FLAG_NONE);
	req->rq_xprt = xprt;
	req->rq_xdrs = xdrs;
	req->rq_refcnt = 1;

	return req;
}

static void free_nfs_request(struct svc_req *req, enum xprt_stat stat)
{
	SVC_RELEASE(req->rq_xprt, SVC_REF_FLAG_NONE);

	gsh_free(req);
}

void ntirpc_init(void)
{
	svc_init_params svc_params;
	int ix;
	int code;

	/* Redirect TI-RPC allocators, log channel */
	if (!tirpc_control(TIRPC_PUT_PARAMETERS, &ntirpc_pp)) {
		fprintf(stderr, "Setting nTI-RPC parameters failed\n");
		exit(1);
	}

	/* New TI-RPC package init function */
	svc_params.disconnect_cb = NULL;
	svc_params.alloc_cb = alloc_nfs_request;
	svc_params.free_cb = free_nfs_request;
	svc_params.flags = SVC_INIT_EPOLL; /* use EPOLL event mgmt */
	svc_params.flags |= SVC_INIT_NOREG_XPRTS; /* don't call xprt_register */
	svc_params.max_connections = nfs_param.core_param.rpc.max_connections;
	svc_params.max_events = 1024; /* length of epoll event queue */
	svc_params.ioq_send_max = nfs_param.core_param.rpc.max_send_buffer_size;
	svc_params.channels = N_EVENT_CHAN;
	svc_params.idle_timeout = nfs_param.core_param.rpc.idle_timeout_s;
	svc_params.ioq_thrd_min = nfs_param.core_param.rpc.ioq_thrd_min;
	svc_params.ioq_thrd_max = nfs_param.core_param.rpc.ioq_thrd_max;
	/* GSS ctx cache tuning, expiration */
	svc_params.gss_ctx_hash_partitions =
		nfs_param.core_param.rpc.gss.ctx_hash_partitions;
	svc_params.gss_max_ctx = nfs_param.core_param.rpc.gss.max_ctx;
	svc_params.gss_max_gc = nfs_param.core_param.rpc.gss.max_gc;

	/* Only after TI-RPC allocators, log channel are setup */
	if (!svc_init(&svc_params)) {
		fprintf(stderr, "SVC initialization failed\n");
		exit(1);
	}

	for (ix = 0; ix < EVCHAN_SIZE; ++ix) {
		rpc_evchan[ix].chan_id = 0;
		code = svc_rqst_new_evchan(&rpc_evchan[ix].chan_id,
					   NULL /* u_data */,
					   SVC_RQST_FLAG_NONE);
		if (code) {
			fprintf(stderr,
				"Cannot create TI-RPC event channel (%d, %d)\n",
				ix, code);
			exit(1);
		}
	}
}

int main(int argc, char **argv)
{
	int c;
	int port = 0, ret;
	int state = 0, sflag = 0;
	char mon_client[STR_SIZE], mflag = 0;
	int okflag = 1, rflag = 0, lflag = 0;
	notify arg;
	sockaddr_t local_addr;
	sockaddr_t remote_addr;

	ntirpc_init();

	argv0 = argv[0];

	while ((c = getopt(argc, argv, "p:r:m:l:s:")) != EOF)
		switch (c) {
		case 'p':
			port = atoi(optarg);
			break;
		case 's':
			state = atoi(optarg);
			sflag = 1;
			break;
		case 'm':
			if (strlcpy(mon_client, optarg, sizeof(mon_client)) >=
			    sizeof(mon_client)) {
				fprintf(stderr, ERR_MSG1, "monitor host");
				exit(1);
			}
			mflag = 1;
			break;
		case 'r':
			ret = ip_str_to_sockaddr(optarg, &remote_addr);
			if (ret == 0)
				rflag = 1;
			else
				fprintf(stderr,
					"Remote address %s is not valid\n",
					optarg);
			break;
		case 'l':
			ret = ip_str_to_sockaddr(optarg, &local_addr);
			if (ret == 0)
				lflag = 1;
			else
				fprintf(stderr,
					"Local address %s is not valid\n",
					optarg);
			break;
		case '?':
		default:
			/* Force USAGE message */
			okflag = 0;
			break;
		}

	if ((sflag + lflag + mflag + rflag + okflag) != 5) {
		fprintf(stderr, USAGE, argv0);
		exit(1);
	}

	/* Add local port to local address */
	switch (local_addr.ss_family) {
	case AF_INET:
		((struct sockaddr_in *)&local_addr)->sin_port = htons(port);
		break;

	case AF_INET6:
		((struct sockaddr_in6 *)&local_addr)->sin6_port = htons(port);
		break;

	default:
		fprintf(stderr, "Unexpected address family\n\n");
		fprintf(stderr, USAGE, argv0);
		exit(1);
	}

	arg.my_name = mon_client;
	arg.state = state;

	return nsm_notify_1(&arg, &local_addr, &remote_addr);
}
