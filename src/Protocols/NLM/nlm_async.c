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

#include "sal_functions.h"
#include "nlm4.h"
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

	if (arg != NULL) {
		nlm_arg = &arg->state_async_data.state_nlm_async_data;
		memset(arg, 0, sizeof(*arg));
		arg->state_async_func = func;
		nlm_arg->nlm_async_host = host;
		nlm_arg->nlm_async_args.nlm_async_res = *pres;
		if (!copy_netobj
		    (&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4.cookie,
		     &pres->res_nlm4.cookie)) {
			LogCrit(COMPONENT_NLM,
				"Unable to copy async response file handle");
			gsh_free(arg);
			return NFS_REQ_DROP;
		}
	} else {
		LogCrit(COMPONENT_NLM, "Unable to allocate async response");
		return NFS_REQ_DROP;
	}

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

	if (arg != NULL) {
		nlm_arg = &arg->state_async_data.state_nlm_async_data;
		memset(arg, 0, sizeof(*arg));
		arg->state_async_func = func;
		nlm_arg->nlm_async_host = host;
		nlm_arg->nlm_async_args.nlm_async_res = *pres;
		if (!copy_netobj(
		     &nlm_arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie,
		     &pres->res_nlm4test.cookie)) {
			LogCrit(COMPONENT_NLM,
				"Unable to copy async response file handle");
			gsh_free(arg);
			return NFS_REQ_DROP;
		} else if (pres->res_nlm4test.test_stat.stat == NLM4_DENIED) {
			if (!copy_netobj(
			     &nlm_arg->nlm_async_args.nlm_async_res.
			      res_nlm4test.test_stat.nlm4_testrply_u.holder.oh,
			     &pres->res_nlm4test.test_stat.nlm4_testrply_u.
			      holder.oh)) {
				LogCrit(COMPONENT_NLM,
					"Unable to copy async response oh");
				netobj_free(&nlm_arg->nlm_async_args.
					    nlm_async_res.res_nlm4test.cookie);
				gsh_free(arg);
				return NFS_REQ_DROP;
			}
		}
	} else {
		LogCrit(COMPONENT_NLM, "Unable to allocate async response");
		return NFS_REQ_DROP;
	}

	status = state_async_schedule(arg);

	if (status != STATE_SUCCESS) {
		netobj_free(
		    &nlm_arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie);
		if (pres->res_nlm4test.test_stat.stat == NLM4_DENIED)
			netobj_free(&nlm_arg->nlm_async_args.nlm_async_res.
				    res_nlm4test.test_stat.nlm4_testrply_u.
				    holder.oh);
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

/* Client routine  to send the asynchrnous response,
 * key is used to wait for a response
 */
int nlm_send_async(int proc, state_nlm_client_t *host, void *inarg, void *key)
{
	struct timeval tout = { 0, 10 };
	int retval, retry;
	struct timeval start, now;
	struct timespec timeout;

	for (retry = 1; retry <= MAX_ASYNC_RETRY; retry++) {
		if (host->slc_callback_clnt == NULL) {
			LogFullDebug(COMPONENT_NLM,
				     "gsh_clnt_create %s",
				     host->slc_nsm_client->ssc_nlm_caller_name);

			if (host->slc_client_type == XPRT_TCP) {
				int fd;
				struct sockaddr_in6 server_addr;
				struct netbuf *buf, local_buf;
				struct addrinfo *result;
				struct addrinfo hints;
				char port_str[20];

				fd = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
				if (fd < 0)
					return -1;

				memcpy(&server_addr,
				       &(host->slc_server_addr),
				       sizeof(struct sockaddr_in6));
				server_addr.sin6_port = 0;

				if (bind(fd,
					 (struct sockaddr *)&server_addr,
					  sizeof(server_addr)) == -1) {
					LogMajor(COMPONENT_NLM, "Cannot bind");
					close(fd);
					return -1;
				}

				buf = rpcb_find_mapped_addr(
				     (char *) xprt_type_to_str(
							host->slc_client_type),
				     NLMPROG, NLM4_VERS,
				     host->slc_nsm_client->ssc_nlm_caller_name);
				/* handle error here, for example,
				 * client side blocking rpc call
				 */
				if (buf == NULL) {
					LogMajor(COMPONENT_NLM,
						 "Cannot create NLM async %s connection to client %s",
						 xprt_type_to_str(
							host->slc_client_type),
						 host->slc_nsm_client->
						 ssc_nlm_caller_name);
					close(fd);
					return -1;
				}

				memset(&hints, 0, sizeof(struct addrinfo));
				hints.ai_family = AF_INET6;	/* only INET6 */
				hints.ai_socktype = SOCK_STREAM; /* TCP */
				hints.ai_protocol = 0;	/* Any protocol */
				hints.ai_canonname = NULL;
				hints.ai_addr = NULL;
				hints.ai_next = NULL;

				/* convert port to string format */
				sprintf(port_str, "%d",
					htons(((struct sockaddr_in *)
						buf->buf)->sin_port));

				/* buf with inet is only needed for the port */
				gsh_free(buf->buf);
				gsh_free(buf);

				/* get the IPv4 mapped IPv6 address */
				getaddrinfo(host->slc_nsm_client->
					    ssc_nlm_caller_name,
					    port_str,
					    &hints,
					    &result);

				/* setup the netbuf with in6 address */
				local_buf.buf = result->ai_addr;
				local_buf.len = local_buf.maxlen =
				    result->ai_addrlen;

				host->slc_callback_clnt =
				    clnt_vc_ncreate(fd, &local_buf, NLMPROG,
						    NLM4_VERS, 0, 0);
				freeaddrinfo(result);
			} else {

				host->slc_callback_clnt = gsh_clnt_create(
				    host->slc_nsm_client->ssc_nlm_caller_name,
				    NLMPROG,
				    NLM4_VERS,
				    (char *) xprt_type_to_str(
						host->slc_client_type));
			}

			if (host->slc_callback_clnt == NULL) {
				LogMajor(COMPONENT_NLM,
					 "Cannot create NLM async %s connection to client %s",
					 xprt_type_to_str(host->
							  slc_client_type),
					 host->slc_nsm_client->
					 ssc_nlm_caller_name);
				return -1;
			}

			/* split auth (for authnone, idempotent) */
			host->slc_callback_auth = authnone_create();
		}

		pthread_mutex_lock(&nlm_async_resp_mutex);
		resp_key = key;
		pthread_mutex_unlock(&nlm_async_resp_mutex);

		LogFullDebug(COMPONENT_NLM, "About to make clnt_call");

		retval = clnt_call(host->slc_callback_clnt,
				   host->slc_callback_auth,
				   proc,
				   nlm_reply_proc[proc],
				   inarg,
				   (xdrproc_t) xdr_void,
				   NULL,
				   tout);

		LogFullDebug(COMPONENT_NLM, "Done with clnt_call");

		if (retval == RPC_TIMEDOUT || retval == RPC_SUCCESS) {
			retval = RPC_SUCCESS;
			break;
		}

		LogCrit(COMPONENT_NLM,
			"NLM async Client procedure call %d failed with return code %d %s",
			proc, retval,
			clnt_sperror(host->slc_callback_clnt, ""));

		gsh_clnt_destroy(host->slc_callback_clnt);
		host->slc_callback_clnt = NULL;

		if (retry == MAX_ASYNC_RETRY) {
			LogMajor(COMPONENT_NLM,
				 "NLM async Client exceeded retry count %d",
				 MAX_ASYNC_RETRY);
			pthread_mutex_lock(&nlm_async_resp_mutex);
			resp_key = NULL;
			pthread_mutex_unlock(&nlm_async_resp_mutex);
			return retval;
		}
	}

	pthread_mutex_lock(&nlm_async_resp_mutex);

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

	pthread_mutex_unlock(&nlm_async_resp_mutex);

	return retval;
}

void nlm_signal_async_resp(void *key)
{
	pthread_mutex_lock(&nlm_async_resp_mutex);

	if (resp_key == key) {
		resp_key = NULL;
		pthread_cond_signal(&nlm_async_resp_cond);
		LogFullDebug(COMPONENT_NLM, "Signaled condition variable");
	} else {
		LogFullDebug(COMPONENT_NLM, "Didn't signal condition variable");
	}

	pthread_mutex_unlock(&nlm_async_resp_mutex);
}
