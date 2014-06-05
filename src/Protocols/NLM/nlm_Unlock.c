/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 * --------------------------
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
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * @brief Free a range lock
 *
 * @param[in]  args
 * @param[in]  export
 * @param[in]  worker
 * @param[in]  req
 * @param[out] res
 */

int nlm4_Unlock(nfs_arg_t *args,
		nfs_worker_data_t *worker,
		struct svc_req *req, nfs_res_t *res)
{
	nlm4_unlockargs *arg = &args->arg_nlm4_unlock;
	cache_entry_t *pentry;
	state_status_t state_status = STATE_SUCCESS;
	char buffer[MAXNETOBJ_SZ * 2];
	state_nsm_client_t *nsm_client;
	state_nlm_client_t *nlm_client;
	state_owner_t *nlm_owner;
	fsal_lock_param_t lock;
	int rc;

	/* NLM doesn't have a BADHANDLE error, nor can rpc_execute deal with
	 * responding to an NLM_*_MSG call, so we check here if the export is
	 * NULL and if so, handle the response.
	 */
	if (op_ctx->export == NULL) {
		res->res_nlm4.stat.stat = NLM4_STALE_FH;
		LogInfo(COMPONENT_NLM, "INVALID HANDLE: nlm4_Unlock");
		return NFS_REQ_OK;
	}

	netobj_to_string(&arg->cookie, buffer, sizeof(buffer));

	LogDebug(COMPONENT_NLM,
		 "REQUEST PROCESSING: Calling nlm4_Unlock svid=%d off=%llx len=%llx cookie=%s",
		 (int)arg->alock.svid,
		 (unsigned long long)arg->alock.l_offset,
		 (unsigned long long)arg->alock.l_len, buffer);

	if (!copy_netobj(&res->res_nlm4test.cookie, &arg->cookie)) {
		res->res_nlm4.stat.stat = NLM4_FAILED;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: nlm4_Unlock %s",
			 lock_result_str(res->res_nlm4.stat.stat));
		return NFS_REQ_OK;
	}

	if (nfs_in_grace()) {
		res->res_nlm4.stat.stat = NLM4_DENIED_GRACE_PERIOD;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: nlm4_Unlock %s",
			 lock_result_str(res->res_nlm4.stat.stat));
		return NFS_REQ_OK;
	}

	/* unlock doesn't care if owner is found */
	rc = nlm_process_parameters(req,
				    false,	/* exlcusive doesn't matter */
				    &arg->alock,
				    &lock,
				    &pentry,
				    CARE_NOT,
				    &nsm_client,
				    &nlm_client,
				    &nlm_owner,
				    NULL);

	if (rc >= 0) {
		/* resent the error back to the client */
		res->res_nlm4.stat.stat = (nlm4_stats) rc;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: nlm4_Unlock %s",
			 lock_result_str(res->res_nlm4.stat.stat));
		return NFS_REQ_OK;
	}

	state_status = state_unlock(pentry,
				    nlm_owner,
				    NULL,
				    &lock,
				    POSIX_LOCK);

	if (state_status != STATE_SUCCESS) {
		/* Unlock could fail in the FSAL and make a bit of a mess,
		 * especially if we are in out of memory situation. Such an
		 * error is logged by Cache Inode.
		 */
		res->res_nlm4test.test_stat.stat =
		    nlm_convert_state_error(state_status);
	} else {
		res->res_nlm4.stat.stat = NLM4_GRANTED;
	}

	/* Release the NLM Client and NLM Owner references we have */
	dec_nsm_client_ref(nsm_client);
	dec_nlm_client_ref(nlm_client);
	dec_state_owner_ref(nlm_owner);
	cache_inode_put(pentry);

	LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Unlock %s",
		 lock_result_str(res->res_nlm4.stat.stat));
	return NFS_REQ_OK;
}

static void nlm4_unlock_message_resp(state_async_queue_t *arg)
{
	state_nlm_async_data_t *nlm_arg =
	    &arg->state_async_data.state_nlm_async_data;

	if (isFullDebug(COMPONENT_NLM)) {
		char buffer[1024];

		netobj_to_string(&nlm_arg->nlm_async_args.nlm_async_res.
				 res_nlm4test.cookie, buffer, 1024);

		LogFullDebug(COMPONENT_NLM,
			     "Calling nlm_send_async cookie=%s status=%s",
			     buffer,
			     lock_result_str(nlm_arg->nlm_async_args.
					     nlm_async_res.res_nlm4.stat.stat));
	}

	nlm_send_async(NLMPROC4_UNLOCK_RES, nlm_arg->nlm_async_host,
		       &(nlm_arg->nlm_async_args.nlm_async_res), NULL);

	nlm4_Unlock_Free(&nlm_arg->nlm_async_args.nlm_async_res);
	dec_nsm_client_ref(nlm_arg->nlm_async_host->slc_nsm_client);
	dec_nlm_client_ref(nlm_arg->nlm_async_host);
	gsh_free(arg);
}

/**
 * @brief Unlock Message
 *
 * @param[in]  args
 * @param[in]  export
 * @param[in]  worker
 * @param[in]  req
 * @param[out] res
 *
 */
int nlm4_Unlock_Message(nfs_arg_t *args,
			nfs_worker_data_t *worker, struct svc_req *req,
			nfs_res_t *res)
{
	state_nlm_client_t *nlm_client = NULL;
	state_nsm_client_t *nsm_client;
	nlm4_unlockargs *arg = &args->arg_nlm4_unlock;
	int rc = NFS_REQ_OK;

	LogDebug(COMPONENT_NLM,
		 "REQUEST PROCESSING: Calling nlm_Unlock_Message");

	nsm_client = get_nsm_client(CARE_NO_MONITOR,
				    req->rq_xprt,
				    arg->alock.caller_name);

	if (nsm_client != NULL)
		nlm_client = get_nlm_client(CARE_NO_MONITOR,
					    req->rq_xprt, nsm_client,
					    arg->alock.caller_name);

	if (nlm_client == NULL)
		rc = NFS_REQ_DROP;
	else
		rc = nlm4_Unlock(args, worker, req, res);

	if (rc == NFS_REQ_OK)
		rc = nlm_send_async_res_nlm4(nlm_client,
					     nlm4_unlock_message_resp,
					     res);

	if (rc == NFS_REQ_DROP) {
		if (nsm_client != NULL)
			dec_nsm_client_ref(nsm_client);

		if (nlm_client != NULL)
			dec_nlm_client_ref(nlm_client);

		LogCrit(COMPONENT_NLM,
			"Could not send async response for nlm_Unlock_Message");
	}

	return NFS_REQ_DROP;
}

/**
 * nlm4_Unlock_Free: Frees the result structure allocated for nlm4_Unlock
 *
 * Frees the result structure allocated for nlm4_Lock. Does Nothing in fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Unlock_Free(nfs_res_t *res)
{
	netobj_free(&res->res_nlm4test.cookie);
}
