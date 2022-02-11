// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "fsal.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"
#include "export_mgr.h"

/**
 * @brief Lock Granted Result Handler
 *
 * @param[in]  arg
 * @param[in]  req
 * @param[out] res
 *
 */
int nlm4_Granted_Res(nfs_arg_t *args, struct svc_req *req, nfs_res_t *res)
{
	nlm4_res *arg = &args->arg_nlm4_res;
	char buffer[1024] = "\0";
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

	if (cookie_entry->sce_lock_entry == NULL
	    || cookie_entry->sce_lock_entry->sle_block_data == NULL) {
		/* This must be an old NLM_GRANTED_RES */
		LogFullDebug(COMPONENT_NLM,
			     "Could not find block data for cookie=%s (must be an old NLM_GRANTED_RES)",
			     buffer);
		return NFS_REQ_OK;
	}

	/* Fill in op_ctx, nfs_rpc_process_request will release the export ref.
	 * We take an export reference even if the export is stale because
	 * we want to properly clean up the cookie_entry.
	 */
	get_gsh_export_ref(cookie_entry->sce_lock_entry->sle_export);
	set_op_context_export(cookie_entry->sce_lock_entry->sle_export);

	/* If the client returned an error or the export has gone stale,
	 * release the grant to properly clean up cookie_entry.
	 */
	if (arg->stat.stat != NLM4_GRANTED ||
		!export_ready(op_ctx->ctx_export)) {
		LogEvent(COMPONENT_NLM,
			 "Granted call failed due to %s, releasing lock",
			 arg->stat.stat != NLM4_GRANTED
				? "client error"
				: "export stale");
		state_status = state_release_grant(cookie_entry);
		if (state_status != STATE_SUCCESS) {
			LogDebug(COMPONENT_NLM,
				 "state_release_grant failed");
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
	/* Nothing to do */
}
