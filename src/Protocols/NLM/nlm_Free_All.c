/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz <ffilz@us.ibm.com>
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
#include "ganesha_rpc.h"
#include "log.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nlm_util.h"

/**
 * @brief Free All Locks
 *
 * @param[in]  arg
 * @param[in]  export
 * @param[in]  worker
 * @param[in]  req
 * @param[out] res
 */

int nlm4_Free_All(nfs_arg_t *args,
		  nfs_worker_data_t *worker,
		  struct svc_req *req, nfs_res_t *res)
{
	nlm4_free_allargs *arg = &args->arg_nlm4_free_allargs;
	state_status_t state_status = STATE_SUCCESS;
	state_nsm_client_t *nsm_client;

	LogDebug(COMPONENT_NLM,
		 "REQUEST PROCESSING: Calling nlm4_Free_All for %s",
		 arg->name);

	nsm_client = get_nsm_client(CARE_NOT, req->rq_xprt, arg->name);
	if (nsm_client != NULL) {
		/* NLM_FREE_ALL has the same semantics as handling SM_NOTIFY.
		 *
		 * Cast the state number into a state pointer to protect
		 * locks from a client that has rebooted from being released
		 * by this NLM_FREE_ALL.
		 */
		state_status =
		    state_nlm_notify(nsm_client, true,
				     (void *)(ptrdiff_t) arg->state);
		if (state_status != STATE_SUCCESS) {
			/* NLM_FREE_ALL has void result so all we can do is
			 * log error
			 */
			LogWarn(COMPONENT_NLM,
				"NLM_FREE_ALL failed with result %s",
				state_err_str(state_status));
		}

		dec_nsm_client_ref(nsm_client);
	}

	LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Free_All DONE");

	return NFS_REQ_OK;
}

/**
 * nlm4_Free_All_Free: Frees the result structure allocated for nlm4_Free_All
 *
 * Frees the result structure allocated for nlm4_Free_All. Does Nothing in fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Free_All_Free(nfs_res_t *res)
{
	return;
}
