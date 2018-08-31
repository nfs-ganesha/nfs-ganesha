/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz <ffilz@us.ibm.com>
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
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "fsal.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * @brief Set a share reservation
 *
 * @param[in]  arg
 * @param[in]  req
 * @param[out] res
 */

int nlm4_Share(nfs_arg_t *args, struct svc_req *req, nfs_res_t *res)
{
	nlm4_shareargs *arg = &args->arg_nlm4_share;
	struct fsal_obj_handle *obj;
	state_status_t state_status = STATE_SUCCESS;
	char buffer[MAXNETOBJ_SZ * 2] = "\0";
	state_nsm_client_t *nsm_client;
	state_nlm_client_t *nlm_client;
	state_owner_t *nlm_owner;
	state_t *nlm_state;
	int rc;
	bool grace_ref = false;

	/* NLM doesn't have a BADHANDLE error, nor can rpc_execute deal with
	 * responding to an NLM_*_MSG call, so we check here if the export is
	 * NULL and if so, handle the response.
	 */
	if (op_ctx->ctx_export == NULL) {
		res->res_nlm4share.stat = NLM4_STALE_FH;
		LogInfo(COMPONENT_NLM, "INVALID HANDLE: NLM4_SHARE");
		return NFS_REQ_OK;
	}

	res->res_nlm4share.sequence = 0;

	netobj_to_string(&arg->cookie, buffer, 1024);

	if (isDebug(COMPONENT_NLM)) {
		char str[LEN_FH_STR];
		char oh[MAXNETOBJ_SZ * 2] = "\0";

		sprint_fhandle3(str, (struct nfs_fh3 *)&arg->share.fh);
		netobj_to_string(&arg->share.oh, oh, 1024);

		LogDebug(COMPONENT_NLM,
			 "REQUEST PROCESSING: Calling NLM4_SHARE handle: %s, cookie=%s, reclaim=%s, owner=%s, access=%d, deny=%d",
			 str, buffer, arg->reclaim ? "yes" : "no", oh,
			 arg->share.access,
			 arg->share.mode);
	}

	copy_netobj(&res->res_nlm4share.cookie, &arg->cookie);

	/* Allow only reclaim share request during recovery and visa versa.
	 * Note: NLM_SHARE is indicated to be non-monitored, however, it does
	 * have a reclaim flag, so we will honor the reclaim flag if used.
	 */
	grace_ref = !op_ctx->fsal_export->exp_ops.fs_supports(
					op_ctx->fsal_export, fso_grace_method);
	if (grace_ref) {
		if (!nfs_get_grace_status(arg->reclaim)) {
			res->res_nlm4share.stat = NLM4_DENIED_GRACE_PERIOD;
			LogDebug(COMPONENT_NLM,
				 "REQUEST RESULT: NLM4_SHARE %s",
				 lock_result_str(res->res_nlm4share.stat));
			return NFS_REQ_OK;
		}
	}

	rc = nlm_process_share_parms(req,
				     &arg->share,
				     op_ctx->fsal_export,
				     &obj,
				     CARE_NO_MONITOR,
				     &nsm_client,
				     &nlm_client,
				     &nlm_owner,
				     &nlm_state);

	if (rc >= 0) {
		/* Present the error back to the client */
		res->res_nlm4share.stat = (nlm4_stats) rc;
		LogDebug(COMPONENT_NLM,
			 "REQUEST RESULT: NLM4_SHARE %s",
			 lock_result_str(res->res_nlm4share.stat));
		goto out;
	}

	state_status = state_nlm_share(obj,
				       arg->share.access,
				       arg->share.mode,
				       nlm_owner,
				       nlm_state,
				       arg->reclaim,
				       false);

	if (state_status != STATE_SUCCESS) {
		res->res_nlm4share.stat =
		    nlm_convert_state_error(state_status);
	} else {
		res->res_nlm4share.stat = NLM4_GRANTED;
	}

	/* Release the NLM Client and NLM Owner references we have */
	dec_nsm_client_ref(nsm_client);
	dec_nlm_client_ref(nlm_client);
	dec_state_owner_ref(nlm_owner);
	obj->obj_ops->put_ref(obj);
	dec_nlm_state_ref(nlm_state);

	LogDebug(COMPONENT_NLM, "REQUEST RESULT: NLM4_SHARE %s",
		 lock_result_str(res->res_nlm4share.stat));
out:
	if (grace_ref)
		nfs_put_grace_status();
	return NFS_REQ_OK;
}

/**
 * nlm4_Share_Free: Frees the result structure allocated for nlm4_Lock
 *
 * Frees the result structure allocated for nlm4_Lock. Does Nothing in fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Share_Free(nfs_res_t *res)
{
	netobj_free(&res->res_nlm4share.cookie);
}
