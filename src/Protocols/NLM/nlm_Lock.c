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
 * @brief Set a range lock
 *
 * @param[in]  arg
 * @param[in]  export
 * @param[in]  worker
 * @param[in]  req
 * @param[out] res
 *
 */

int nlm4_Lock(nfs_arg_t *args,
	      nfs_worker_data_t *worker,
	      struct svc_req *req, nfs_res_t *res)
{
	nlm4_lockargs *arg = &args->arg_nlm4_lock;
	cache_entry_t *entry;
	state_status_t state_status = STATE_SUCCESS;
	char buffer[MAXNETOBJ_SZ * 2];
	state_nsm_client_t *nsm_client;
	state_nlm_client_t *nlm_client;
	state_owner_t *nlm_owner, *holder;
	fsal_lock_param_t lock, conflict;
	int rc;
	int grace = nfs_in_grace();
	state_block_data_t *pblock_data;
	const char *proc_name = "nlm4_Lock";
	care_t care = CARE_MONITOR;

	if (req->rq_proc == NLMPROC4_NM_LOCK) {
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
	if (op_ctx->export == NULL) {
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

	if (!copy_netobj(&res->res_nlm4test.cookie, &arg->cookie)) {
		res->res_nlm4.stat.stat = NLM4_FAILED;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: %s %s",
			 proc_name, lock_result_str(res->res_nlm4.stat.stat));
		return NFS_REQ_OK;
	}

	/* allow only reclaim lock request during recovery and visa versa */
	if (!fsal_grace() &&
	    ((grace && !arg->reclaim) || (!grace && arg->reclaim))) {
		res->res_nlm4.stat.stat = NLM4_DENIED_GRACE_PERIOD;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: %s %s",
			 proc_name, lock_result_str(res->res_nlm4.stat.stat));
		return NFS_REQ_OK;
	}
	lock.lock_reclaim = arg->reclaim;

	rc = nlm_process_parameters(req,
				    arg->exclusive,
				    &arg->alock,
				    &lock,
				    &entry,
				    care,
				    &nsm_client,
				    &nlm_client,
				    &nlm_owner,
				    &pblock_data);

	if (rc >= 0) {
		/* Present the error back to the client */
		res->res_nlm4.stat.stat = (nlm4_stats) rc;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: %s %s",
			 proc_name,
			 lock_result_str(res->res_nlm4.stat.stat));
		return NFS_REQ_OK;
	}

	/* Cast the state number into a state pointer to protect
	 * locks from a client that has rebooted from the SM_NOTIFY
	 * that will release old locks
	 */
	state_status = state_lock(entry,
				  nlm_owner,
				  (void *)(ptrdiff_t) arg->state,
				  arg->block ? STATE_NLM_BLOCKING :
					       STATE_NON_BLOCKING,
				  pblock_data,
				  &lock,
				  &holder,
				  &conflict,
				  POSIX_LOCK);

	if (state_status != STATE_SUCCESS) {
		res->res_nlm4test.test_stat.stat =
				nlm_convert_state_error(state_status);

		if (state_status == STATE_LOCK_CONFLICT) {
			nlm_process_conflict(
			    &res->res_nlm4test.test_stat.nlm4_testrply_u.holder,
			    holder,
			    &conflict);
		}

		/* If we didn't block, release the block data */
		if (state_status != STATE_LOCK_BLOCKED && pblock_data != NULL)
			gsh_free(pblock_data);

		if (state_status == STATE_IN_GRACE) {
			res->res_nlm4.stat.stat = NLM4_DENIED_GRACE_PERIOD;
			return NFS_REQ_OK;
		}

	} else {
		res->res_nlm4.stat.stat = NLM4_GRANTED;
	}

	/* Release the NLM Client and NLM Owner references we have */
	dec_nsm_client_ref(nsm_client);
	dec_nlm_client_ref(nlm_client);
	dec_state_owner_ref(nlm_owner);
	cache_inode_put(entry);

	LogDebug(COMPONENT_NLM,
		 "REQUEST RESULT: %s %s",
		 proc_name,
		 lock_result_str(res->res_nlm4.stat.stat));

	return NFS_REQ_OK;
}

static void nlm4_lock_message_resp(state_async_queue_t *arg)
{
	state_nlm_async_data_t *nlm_arg =
	    &arg->state_async_data.state_nlm_async_data;

	if (isFullDebug(COMPONENT_NLM)) {
		char buffer[1024];

		netobj_to_string(&nlm_arg->nlm_async_args.nlm_async_res.
				 res_nlm4test.cookie,
				 buffer,
				 1024);

		LogFullDebug(COMPONENT_NLM,
			     "Calling nlm_send_async cookie=%s status=%s",
			     buffer,
			     lock_result_str(nlm_arg->nlm_async_args.
					     nlm_async_res.res_nlm4.stat.stat));
	}

	nlm_send_async(NLMPROC4_LOCK_RES,
		       nlm_arg->nlm_async_host,
		       &nlm_arg->nlm_async_args.nlm_async_res,
		       NULL);

	nlm4_Lock_Free(&nlm_arg->nlm_async_args.nlm_async_res);
	dec_nsm_client_ref(nlm_arg->nlm_async_host->slc_nsm_client);
	dec_nlm_client_ref(nlm_arg->nlm_async_host);
	gsh_free(arg);
}

/**
 * @brief Lock Message
 *
 * @param[in]  arg
 * @param[in]  export
 * @param[in]  worker
 * @param[in]  req
 * @param[out] res
 *
 */
int nlm4_Lock_Message(nfs_arg_t *args,
		      nfs_worker_data_t *worker, struct svc_req *req,
		      nfs_res_t *res)
{
	state_nlm_client_t *nlm_client = NULL;
	state_nsm_client_t *nsm_client;
	nlm4_lockargs *arg = &args->arg_nlm4_lock;
	int rc = NFS_REQ_OK;

	LogDebug(COMPONENT_NLM, "REQUEST PROCESSING: Calling nlm_Lock_Message");

	nsm_client = get_nsm_client(CARE_NO_MONITOR,
				    req->rq_xprt,
				    arg->alock.caller_name);

	if (nsm_client != NULL)
		nlm_client = get_nlm_client(CARE_NO_MONITOR,
					    req->rq_xprt,
					    nsm_client,
					    arg->alock.caller_name);

	if (nlm_client == NULL)
		rc = NFS_REQ_DROP;
	else
		rc = nlm4_Lock(args, worker, req, res);

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
	netobj_free(&res->res_nlm4test.cookie);
}
