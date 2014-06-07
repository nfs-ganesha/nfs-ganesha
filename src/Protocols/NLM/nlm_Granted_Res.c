/*
 * Copyright IBM Corporation, 2010
 *  Contributor: M. Mohan Kumar <mohan@in.ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nlm4.h"
#include "sal_data.h"
#include "cache_inode.h"
#include "nlm_util.h"
#include "nlm_async.h"
#include "export_mgr.h"

/**
 * @brief Lock Granted Result Handler
 *
 * @param[in]  arg
 * @param[in]  export
 * @param[in]  worker
 * @param[in]  req
 * @param[out] res
 *
 */
int nlm4_Granted_Res(nfs_arg_t *args,
		     nfs_worker_data_t *worker, struct svc_req *req,
		     nfs_res_t *res)
{
	nlm4_res *arg = &args->arg_nlm4_res;
	char buffer[1024];
	state_status_t state_status = STATE_SUCCESS;
	state_cookie_entry_t *cookie_entry;

	netobj_to_string(&arg->cookie, buffer, 1024);
	LogDebug(COMPONENT_NLM,
		 "REQUEST PROCESSING: Calling nlm_Granted_Res cookie=%s",
		 buffer);

	state_status = state_find_grant(arg->cookie.n_bytes,
					arg->cookie.n_len,
					&cookie_entry);

	if (state_status != STATE_SUCCESS) {
		/* This must be an old NLM_GRANTED_RES */
		LogFullDebug(COMPONENT_NLM,
			     "Could not find cookie=%s (must be an old NLM_GRANTED_RES)",
			     buffer);
		return NFS_REQ_OK;
	}

	PTHREAD_RWLOCK_wrlock(&cookie_entry->sce_entry->state_lock);

	if (cookie_entry->sce_lock_entry == NULL
	    || cookie_entry->sce_lock_entry->sle_block_data == NULL) {
		/* This must be an old NLM_GRANTED_RES */
		PTHREAD_RWLOCK_unlock(&cookie_entry->sce_entry->state_lock);
		LogFullDebug(COMPONENT_NLM,
			     "Could not find block data for cookie=%s (must be an old NLM_GRANTED_RES)",
			     buffer);
		return NFS_REQ_OK;
	}

	PTHREAD_RWLOCK_unlock(&cookie_entry->sce_entry->state_lock);

	/* Fill in op_ctx */
	op_ctx->export = cookie_entry->sce_lock_entry->sle_export;
	get_gsh_export_ref(op_ctx->export); /* nfs_rpc_execute will release */
	op_ctx->fsal_export = op_ctx->export->fsal_export;
	if (arg->stat.stat != NLM4_GRANTED) {
		LogMajor(COMPONENT_NLM,
			 "Granted call failed due to client error, releasing lock");
		state_status = state_release_grant(cookie_entry);
		if (state_status != STATE_SUCCESS) {
			LogDebug(COMPONENT_NLM,
				 "cache_inode_release_grant failed");
		}
	} else {
		state_complete_grant(cookie_entry);
		nlm_signal_async_resp(cookie_entry);
	}

	return NFS_REQ_OK;
}

/**
 * nlm4_Granted_Res_Free: Frees the result structure allocated for
 * nlm4_Granted_Res
 *
 * Frees the result structure allocated for nlm4_Granted_Res. Does Nothing
 * in fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Granted_Res_Free(nfs_res_t *res)
{
	return;
}
