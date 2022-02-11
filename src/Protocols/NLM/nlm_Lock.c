// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "fsal.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * @brief Set a range lock
 *
 * @param[in]  arg
 * @param[in]  req
 * @param[out] res
 *
 */

int nlm4_Lock(nfs_arg_t *args, struct svc_req *req, nfs_res_t *res)
{
	nlm4_lockargs *arg = &args->arg_nlm4_lock;
	struct fsal_obj_handle *obj;
	state_status_t state_status = STATE_SUCCESS;
	char buffer[MAXNETOBJ_SZ * 2] = "\0";
	state_nsm_client_t *nsm_client;
	state_nlm_client_t *nlm_client;
	state_owner_t *nlm_owner;
	state_t *nlm_state;
	fsal_lock_param_t lock;
	int rc;
	state_block_data_t *pblock_data = NULL;
	const char *proc_name = "nlm4_Lock";
	care_t care = CARE_MONITOR;
	/* Indicate if we let FSAL to handle requests during grace. */
	bool grace_ref;

	if (req->rq_msg.cb_proc == NLMPROC4_NM_LOCK) {
		/* If call is a NM lock, indicate that we care about NLM
		 * client but will not monitor.
		 */
		proc_name = "nlm4_NM_Lock";
		care = CARE_NO_MONITOR;
	}

	/* NLM doesn't have a BADHANDLE error, nor can rpc_execute deal with
	 * responding to an NLM_*_MSG call, so we check here if the export is
	 * NULL and if so, handle the response.
	 */
	if (op_ctx->ctx_export == NULL) {
		res->res_nlm4.stat.stat = NLM4_STALE_FH;
		LogInfo(COMPONENT_NLM, "INVALID HANDLE: %s", proc_name);
		return NFS_REQ_OK;
	}

	netobj_to_string(&arg->cookie, buffer, 1024);
	LogDebug(COMPONENT_NLM,
		 "REQUEST PROCESSING: Calling %s svid=%d off=%llx len=%llx cookie=%s reclaim=%s",
		 proc_name, (int)arg->alock.svid,
		 (unsigned long long)arg->alock.l_offset,
		 (unsigned long long)arg->alock.l_len, buffer,
		 arg->reclaim ? "yes" : "no");

	copy_netobj(&res->res_nlm4.cookie, &arg->cookie);

	grace_ref = !op_ctx->fsal_export->exp_ops.fs_supports(
					op_ctx->fsal_export, fso_grace_method);
	if (grace_ref) {
		if (!nfs_get_grace_status(arg->reclaim)) {
			grace_ref = false;
			res->res_nlm4.stat.stat = NLM4_DENIED_GRACE_PERIOD;
			LogDebug(COMPONENT_NLM,
				 "REQUEST RESULT:%s in grace %s %s",
				 arg->reclaim ? " NOT" : "",
				 proc_name,
				 lock_result_str(res->res_nlm4.stat.stat));
			return NFS_REQ_OK;
		}
	}

	rc = nlm_process_parameters(req,
				    arg->exclusive,
				    &arg->alock,
				    &lock,
				    &obj,
				    care,
				    &nsm_client,
				    &nlm_client,
				    &nlm_owner,
				    arg->block ? &pblock_data : NULL,
				    arg->state,
				    &nlm_state);

	lock.lock_reclaim = arg->reclaim;

	if (rc >= 0) {
		/* Present the error back to the client */
		res->res_nlm4.stat.stat = (nlm4_stats) rc;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: %s %s",
			 proc_name,
			 lock_result_str(res->res_nlm4.stat.stat));
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Check if v4 delegations conflict with v3 op */
	if (state_deleg_conflict(obj, lock.lock_type == FSAL_LOCK_W)) {
		LogDebug(COMPONENT_NLM,
			 "NLM lock request DROPPED due to delegation conflict");
		rc = NFS_REQ_DROP;
		goto out_dec;
	} else {
		(void) atomic_inc_uint32_t(&obj->state_hdl->file.anon_ops);
	}

	/* Cast the state number into a state pointer to protect
	 * locks from a client that has rebooted from the SM_NOTIFY
	 * that will release old locks
	 */
	STATELOCK_lock(obj);
	state_status = state_lock(obj,
				  nlm_owner,
				  nlm_state,
				  arg->block ? STATE_NLM_BLOCKING :
					       STATE_NON_BLOCKING,
				  arg->block ? &pblock_data : NULL,
				  &lock,
				  NULL, /* We don't need conflict info */
				  NULL);
	STATELOCK_unlock(obj);

	/* We prevented delegations from being granted while trying to acquire
	 * the lock. However, when attempting to get a delegation in the
	 * future existing locks will result in a conflict. Thus, we can
	 * decrement the anonymous operations counter now. */
	(void) atomic_dec_uint32_t(&obj->state_hdl->file.anon_ops);

	if (state_status != STATE_SUCCESS) {
		res->res_nlm4.stat.stat =
				nlm_convert_state_error(state_status);

		if (state_status == STATE_IN_GRACE)
			res->res_nlm4.stat.stat = NLM4_DENIED_GRACE_PERIOD;
	} else {
		res->res_nlm4.stat.stat = NLM4_GRANTED;
	}
	rc = NFS_REQ_OK;

 out_dec:
	/* If we didn't block, release the block data. Note that
	 * state_lock() would set pblock_data to NULL if the lock was
	 * blocked!
	 */
	gsh_free(pblock_data);

	/* Release the NLM Client and NLM Owner references we have */
	dec_nsm_client_ref(nsm_client);
	dec_nlm_client_ref(nlm_client);
	dec_state_owner_ref(nlm_owner);
	obj->obj_ops->put_ref(obj);
	dec_nlm_state_ref(nlm_state);

	LogDebug(COMPONENT_NLM,
		 "REQUEST RESULT: %s %s",
		 proc_name,
		 lock_result_str(res->res_nlm4.stat.stat));
out:
	if (grace_ref)
		nfs_put_grace_status();
	return rc;
}

static void nlm4_lock_message_resp(state_async_queue_t *arg)
{
	state_nlm_async_data_t *nlm_arg =
	    &arg->state_async_data.state_nlm_async_data;
	nfs_res_t *res = &nlm_arg->nlm_async_args.nlm_async_res;

	if (isFullDebug(COMPONENT_NLM)) {
		char buffer[1024] = "\0";

		netobj_to_string(&res->res_nlm4test.cookie, buffer, 1024);

		LogFullDebug(COMPONENT_NLM,
			     "Calling nlm_send_async cookie=%s status=%s",
			     buffer, lock_result_str(res->res_nlm4.stat.stat));
	}

	nlm_send_async(NLMPROC4_LOCK_RES, nlm_arg->nlm_async_host, res, NULL);

	nlm4_Lock_Free(res);
	dec_nsm_client_ref(nlm_arg->nlm_async_host->slc_nsm_client);
	dec_nlm_client_ref(nlm_arg->nlm_async_host);
	gsh_free(arg);
}

/**
 * @brief Lock Message
 *
 * @param[in]  arg
 * @param[in]  req
 * @param[out] res
 *
 */
int nlm4_Lock_Message(nfs_arg_t *args, struct svc_req *req, nfs_res_t *res)
{
	state_nlm_client_t *nlm_client = NULL;
	state_nsm_client_t *nsm_client;
	nlm4_lockargs *arg = &args->arg_nlm4_lock;
	int rc = NFS_REQ_OK;

	LogDebug(COMPONENT_NLM, "REQUEST PROCESSING: Calling nlm_Lock_Message");

	nsm_client = get_nsm_client(CARE_NO_MONITOR, arg->alock.caller_name);

	if (nsm_client != NULL)
		nlm_client = get_nlm_client(CARE_NO_MONITOR,
					    req->rq_xprt,
					    nsm_client,
					    arg->alock.caller_name);

	if (nlm_client == NULL)
		rc = NFS_REQ_DROP;
	else
		rc = nlm4_Lock(args, req, res);

	if (rc == NFS_REQ_OK)
		rc = nlm_send_async_res_nlm4(nlm_client,
					     nlm4_lock_message_resp,
					     res);

	if (rc == NFS_REQ_DROP) {
		if (nsm_client != NULL)
			dec_nsm_client_ref(nsm_client);
		if (nlm_client != NULL)
			dec_nlm_client_ref(nlm_client);

		LogCrit(COMPONENT_NLM,
			"Could not send async response for nlm_Lock_Message");
	}

	return NFS_REQ_DROP;

}

/**
 * nlm4_Lock_Free: Frees the result structure allocated for nlm4_Lock
 *
 * Frees the result structure allocated for nlm4_Lock. Does Nothing in fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Lock_Free(nfs_res_t *res)
{
	netobj_free(&res->res_nlm4.cookie);
}
