// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
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

#include "config.h"
#include <stdio.h>
#include <pthread.h>
#include <rpc/types.h>
#include <rpc/nettype.h>
#include <netdb.h>

#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"

pthread_mutex_t nlm_async_resp_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t nlm_async_resp_cond = PTHREAD_COND_INITIALIZER;

int nlm_send_async_res_nlm4(state_nlm_client_t *host, state_async_func_t func,
			    nfs_res_t *pres)
{
	state_async_queue_t *arg = gsh_malloc(sizeof(*arg));
	state_nlm_async_data_t *nlm_arg;
	state_status_t status;

	nlm_arg = &arg->state_async_data.state_nlm_async_data;
	memset(arg, 0, sizeof(*arg));
	arg->state_async_func = func;
	nlm_arg->nlm_async_host = host;
	nlm_arg->nlm_async_args.nlm_async_res = *pres;

	copy_netobj(&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4.cookie,
		    &pres->res_nlm4.cookie);

	status = state_async_schedule(arg);

	if (status != STATE_SUCCESS) {
		gsh_free(arg);
		return NFS_REQ_DROP;
	}

	return NFS_REQ_OK;
}

int nlm_send_async_res_nlm4test(state_nlm_client_t *host,
				state_async_func_t func, nfs_res_t *pres)
{
	state_async_queue_t *arg = gsh_malloc(sizeof(*arg));
	state_nlm_async_data_t *nlm_arg;
	state_status_t status;
	nfs_res_t *res;

	nlm_arg = &arg->state_async_data.state_nlm_async_data;
	res = &nlm_arg->nlm_async_args.nlm_async_res;
	memset(arg, 0, sizeof(*arg));
	arg->state_async_func = func;
	nlm_arg->nlm_async_host = host;
	*res = *pres;

	copy_netobj(&res->res_nlm4test.cookie, &pres->res_nlm4test.cookie);

	if (pres->res_nlm4test.test_stat.stat == NLM4_DENIED) {
		copy_netobj(
		     &res->res_nlm4test.test_stat.nlm4_testrply_u.holder.oh,
		     &pres->res_nlm4test.test_stat.nlm4_testrply_u.holder.oh);
	}

	status = state_async_schedule(arg);

	if (status != STATE_SUCCESS) {
		nlm4_Test_Free(res);
		gsh_free(arg);
		return NFS_REQ_DROP;
	}

	return NFS_REQ_OK;
}

xdrproc_t nlm_reply_proc[] = {
	[NLMPROC4_GRANTED_MSG] = (xdrproc_t) xdr_nlm4_testargs,
	[NLMPROC4_TEST_RES] = (xdrproc_t) xdr_nlm4_testres,
	[NLMPROC4_LOCK_RES] = (xdrproc_t) xdr_nlm4_res,
	[NLMPROC4_CANCEL_RES] = (xdrproc_t) xdr_nlm4_res,
	[NLMPROC4_UNLOCK_RES] = (xdrproc_t) xdr_nlm4_res,
};

static void *resp_key;

static const int MAX_ASYNC_RETRY = 2;
static const struct timespec tout = { 0, 0 }; /* one-shot */

int find_peer_addr(char *caller_name, in_port_t sin_port, sockaddr_t *client)
{
	struct addrinfo hints;
	struct addrinfo *result;
	char port_str[20];
	int retval;
	bool stats = nfs_param.core_param.enable_AUTHSTATS;
	struct sockaddr_in *in;
	struct sockaddr_in6 *in6;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;	/* only INET6 */
	hints.ai_socktype = SOCK_STREAM; /* TCP */
	hints.ai_protocol = 0;	/* Any protocol */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	/* convert port to string format */
	(void) sprintf(port_str, "%d", htons(sin_port));

	/* get the IPv4 mapped IPv6 address */
	retval = gsh_getaddrinfo(caller_name, port_str, &hints, &result, stats);

	if (retval == 0) {
		memcpy(client, result->ai_addr, result->ai_addrlen);
		freeaddrinfo(result);
		return retval;
	}

	if (retval == 0 || (retval != EAI_NONAME && retval != EAI_AGAIN))
		return retval;

	/* Couldn't find an AF_INET6 address, look for AF_INET address */
	hints.ai_family = AF_INET;

	retval = gsh_getaddrinfo(caller_name, port_str, &hints, &result, stats);

	if (retval != 0)
		return retval;

	/* Format of an IPv4 address encapsulated in IPv6:
	 * |---------------------------------------------------------------|
	 * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
	 * |---------------------------------------------------------------|
	 * |            0          |        FFFF       |    IPv4 address   |
	 * |---------------------------------------------------------------|
	 *
	 * An IPv4 loop back address is 127.b.c.d, so we only need to examine
	 * the first byte past ::ffff, or s6_addr[12].
	 *
	 * Otherwise we compare to ::1
	 */
	in = (struct sockaddr_in *) result->ai_addr;
	in6 = (struct sockaddr_in6 *) client;

	/* Copy __SOCKADDR_COMMON part plus sin_port */
	memcpy(in6, in, offsetof(struct sockaddr_in, sin_addr));

	/* Change to AF_INET6 */
	in6->sin6_family = AF_INET6;

	/* Set all those 0s */
	memset(&in6->sin6_addr, 0, 10);

	/* Set those 16 "1" bits */
	in6->sin6_addr.s6_addr16[5] = 0xFFFF;

	/* And the IPv4 address goes at the end. */
	in6->sin6_addr.s6_addr32[3] = in->sin_addr.s_addr;

	/* Finish up the IPv6 address */
	in6->sin6_flowinfo = 0;
	in6->sin6_scope_id = 0;

	freeaddrinfo(result);

	return retval;
}

/* Client routine  to send the asynchrnous response,
 * key is used to wait for a response
 */
int nlm_send_async(int proc, state_nlm_client_t *host, void *inarg, void *key)
{
	struct clnt_req *cc;
	char *t;
	struct timeval start, now;
	struct timespec timeout;
	int retval, retry;
	char *caller_name = host->slc_nsm_client->ssc_nlm_caller_name;
	const char *client_type_str = xprt_type_to_str(host->slc_client_type);

	for (retry = 0; retry < MAX_ASYNC_RETRY; retry++) {
		if (host->slc_callback_clnt == NULL) {
			LogFullDebug(COMPONENT_NLM,
				     "clnt_ncreate %s",
				     caller_name);

			if (host->slc_client_type == XPRT_TCP) {
				int fd;
				struct sockaddr_in6 server_addr;
				sockaddr_t client_addr;
				struct netbuf *buf, local_buf;

				fd = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
				if (fd < 0)
					return -1;

				memcpy(&server_addr,
				       &(host->slc_server_addr),
				       sizeof(struct sockaddr_in6));
				server_addr.sin6_port = 0;

				if (isFullDebug(COMPONENT_NLM)) {
					char str[LOG_BUFF_LEN] = "\0";
					struct display_buffer db = {
							sizeof(str), str, str};

					display_sockaddr(&db,
							 (sockaddr_t *)
								&server_addr);

					LogFullDebug(COMPONENT_NLM,
						     "Server address %s for NLM callback",
						     str);
				}

				if (bind(fd,
					 (struct sockaddr *)&server_addr,
					  sizeof(server_addr)) == -1) {
					LogMajor(COMPONENT_NLM, "Cannot bind");
					close(fd);
					return -1;
				}

				buf = rpcb_find_mapped_addr(
						(char *) client_type_str,
						NLMPROG, NLM4_VERS,
						caller_name);
				/* handle error here, for example,
				 * client side blocking rpc call
				 */
				if (buf == NULL) {
					LogMajor(COMPONENT_NLM,
						 "Cannot create NLM async %s connection to client %s",
						 client_type_str, caller_name);
					close(fd);
					return -1;
				}

				retval = find_peer_addr(caller_name,
							((struct sockaddr_in *)
							    buf->buf)->sin_port,
							&client_addr);

				/* buf with inet is only needed for the port */
				gsh_free(buf->buf);
				gsh_free(buf);

				/* retry for spurious EAI_NONAME errors */
				if (retval == EAI_NONAME ||
				    retval == EAI_AGAIN) {
					LogEvent(COMPONENT_NLM,
						 "failed to resolve %s to an address: %s",
						 caller_name,
						 gai_strerror(retval));
					/* getaddrinfo() failed, retry */
					retval = RPC_UNKNOWNADDR;
					usleep(1000);
					continue;
				} else if (retval != 0) {
					LogMajor(COMPONENT_NLM,
						 "failed to resolve %s to an address: %s",
						 caller_name,
						 gai_strerror(retval));
					return -1;
				}

				/* setup the netbuf with in6 address */
				local_buf.buf = &client_addr;
				local_buf.len = sizeof(struct sockaddr_in6);

				host->slc_callback_clnt =
				    clnt_vc_ncreate(fd, &local_buf, NLMPROG,
						    NLM4_VERS, 0, 0);
			} else {

				host->slc_callback_clnt =
				    clnt_ncreate(caller_name, NLMPROG,
						 NLM4_VERS,
						 (char *) client_type_str);
			}

			if (CLNT_FAILURE(host->slc_callback_clnt)) {
				char *err = rpc_sperror(
					&host->slc_callback_clnt->cl_error,
					"failed");

				LogMajor(COMPONENT_NLM,
					 "Create NLM async %s connection to client %s %s",
					 client_type_str, caller_name, err);
				gsh_free(err);
				CLNT_DESTROY(host->slc_callback_clnt);
				host->slc_callback_clnt = NULL;
				return -1;
			}

			/* split auth (for authnone, idempotent) */
			host->slc_callback_auth = authnone_ncreate();
		}

		PTHREAD_MUTEX_lock(&nlm_async_resp_mutex);
		resp_key = key;
		PTHREAD_MUTEX_unlock(&nlm_async_resp_mutex);

		LogFullDebug(COMPONENT_NLM, "About to make clnt_call");

		cc = gsh_malloc(sizeof(*cc));
		clnt_req_fill(cc, host->slc_callback_clnt,
			      host->slc_callback_auth, proc,
			      (xdrproc_t) nlm_reply_proc[proc], inarg,
			      (xdrproc_t) xdr_void, NULL);
		cc->cc_error.re_status = clnt_req_setup(cc, tout);
		if (cc->cc_error.re_status == RPC_SUCCESS) {
			cc->cc_refreshes = 0;
			cc->cc_error.re_status = CLNT_CALL_ONCE(cc);
		}

		LogFullDebug(COMPONENT_NLM, "Done with clnt_call");

		if (cc->cc_error.re_status == RPC_TIMEDOUT ||
		    cc->cc_error.re_status == RPC_SUCCESS) {
			retval = RPC_SUCCESS;
			clnt_req_release(cc);
			break;
		}

		retval = cc->cc_error.re_status;

		t = rpc_sperror(&cc->cc_error, "failed");
		LogCrit(COMPONENT_NLM,
			"NLM async Client procedure call %d %s",
			proc, t);
		gsh_free(t);

		clnt_req_release(cc);
		CLNT_DESTROY(host->slc_callback_clnt);
		host->slc_callback_clnt = NULL;
	}

	if (retry == MAX_ASYNC_RETRY) {
		LogMajor(COMPONENT_NLM,
			 "NLM async Client exceeded retry count %d",
			 MAX_ASYNC_RETRY);
		PTHREAD_MUTEX_lock(&nlm_async_resp_mutex);
		resp_key = NULL;
		PTHREAD_MUTEX_unlock(&nlm_async_resp_mutex);
		return retval;
	}

	PTHREAD_MUTEX_lock(&nlm_async_resp_mutex);

	if (resp_key != NULL) {
		/* Wait for 5 seconds or a signal */
		gettimeofday(&start, NULL);
		gettimeofday(&now, NULL);
		timeout.tv_sec = 5 + start.tv_sec;
		timeout.tv_nsec = 0;

		LogFullDebug(COMPONENT_NLM,
			     "About to wait for signal for key %p", resp_key);

		while (resp_key != NULL && now.tv_sec < (start.tv_sec + 5)) {
			int rc;

			rc = pthread_cond_timedwait(&nlm_async_resp_cond,
						    &nlm_async_resp_mutex,
						    &timeout);
			LogFullDebug(COMPONENT_NLM,
				     "pthread_cond_timedwait returned %d",
				     rc);
			gettimeofday(&now, NULL);
		}
		LogFullDebug(COMPONENT_NLM, "Done waiting");
	}

	PTHREAD_MUTEX_unlock(&nlm_async_resp_mutex);

	return retval;
}

void nlm_signal_async_resp(void *key)
{
	PTHREAD_MUTEX_lock(&nlm_async_resp_mutex);

	if (resp_key == key) {
		resp_key = NULL;
		pthread_cond_signal(&nlm_async_resp_cond);
		LogFullDebug(COMPONENT_NLM, "Signaled condition variable");
	} else {
		LogFullDebug(COMPONENT_NLM, "Didn't signal condition variable");
	}

	PTHREAD_MUTEX_unlock(&nlm_async_resp_mutex);
}
