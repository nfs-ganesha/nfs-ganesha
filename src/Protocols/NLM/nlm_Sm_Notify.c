// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
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
#include <arpa/inet.h>
#include "log.h"
#include "gsh_rpc.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nlm_util.h"

#define IN4_LOCALHOST_STRING "127.0.0.1"
#define IN6_LOCALHOST_STRING "::1"
#define IN6_ENCAPSULATED_IN4_LOCALHOST_STRING "::ffff:127.0.0.1"

/**
 * @brief NSM notification
 *
 * @param[in]  args
 * @param[in]  req
 * @param[out] res
 */

int nlm4_Sm_Notify(nfs_arg_t *args, struct svc_req *req, nfs_res_t *res)
{
	nlm4_sm_notifyargs *arg = &args->arg_nlm4_sm_notify;
	state_status_t state_status = STATE_SUCCESS;
	state_nsm_client_t *nsm_client;
	sockaddr_t *orginal_caller_addr = op_ctx->caller_addr;
	struct gsh_client *original_client = op_ctx->client;

	if (!is_loopback(op_ctx->caller_addr)) {
		LogEvent(COMPONENT_NLM,
			 "Client %s sent an SM_NOTIFY, ignoring",
			 op_ctx->client->hostaddr_str);
		return NFS_REQ_OK;
	}

	LogDebug(COMPONENT_NLM,
		 "REQUEST PROCESSING: Calling nlm4_sm_notify for %s state %"
		 PRIu32,
		 arg->name, arg->state);

	/* We don't have a client for the call to get_nsm_client. Note that
	 * nsm_use_caller_name is true or not, note that we ALWAYS look up
	 * the nsm_client using caller name. For nsm_use_caller_name == false
	 * the caller name is the string form of the IP address. In that case
	 * op_ctx->client being NULL will just signal get_nsm_client to use
	 * caller name instead of the op_ctx->client_addr.
	 */
	op_ctx->client = NULL;
	op_ctx->caller_addr = NULL;

	/* Now find the nsm_client using the provided caller_name */
	nsm_client = get_nsm_client(CARE_NOT, arg->name);

	if (nsm_client != NULL) {
		/* Now that we have an nsm_client, we can grab the
		 * gsh_client from ssc_client (which SHOULD be non-NULL)
		 * to use, and if it IS non-NULL, we can also fill in
		 * op_ctx->caller_addr.
		 */
		op_ctx->client = nsm_client->ssc_client;

		if (op_ctx->client != NULL) {
			op_ctx->caller_addr = &op_ctx->client->cl_addrbuf;
			SetClientIP(op_ctx->client->hostaddr_str);
		}

		/* Cast the state number into a state pointer to protect
		 * locks from a client that has rebooted from being released
		 * by this SM_NOTIFY.
		 */
		LogFullDebug(COMPONENT_NLM, "Starting cleanup");
		state_status = state_nlm_notify(nsm_client, true, arg->state);

		if (state_status != STATE_SUCCESS) {
			/** @todo FSF: Deal with error
			 */
		}

		LogFullDebug(COMPONENT_NLM, "Cleanup complete");
		dec_nsm_client_ref(nsm_client);
	}

	if (op_ctx->caller_addr != orginal_caller_addr)
		op_ctx->caller_addr = orginal_caller_addr;

	if (op_ctx->client != original_client) {
		op_ctx->client = original_client;
		SetClientIP(original_client->hostaddr_str);
	}

	LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_sm_notify DONE");

	return NFS_REQ_OK;
}

/**
 * nlm4_Sm_Notify_Free: Frees the result structure allocated for nlm4_Sm_Notify
 *
 * Frees the result structure allocated for nlm4_Sm_Notify. Does Nothing in
 * fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Sm_Notify_Free(nfs_res_t *res)
{
	/* Nothing to do */
}
