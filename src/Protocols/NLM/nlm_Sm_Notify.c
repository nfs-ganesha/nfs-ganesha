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

static void check_use_caller_name_ipv4(char *name)
{
	unsigned char addrbuf[4];
	sockaddr_t name_addr;
	struct gsh_client *name_client;

	if (strcmp(op_ctx->client->hostaddr_str,
		IN4_LOCALHOST_STRING) != 0)
		return;

	/* get the gsh_client for the caller name */
	if (inet_pton(AF_INET, name, addrbuf) != 1)
		return;

	memcpy(&(((struct sockaddr_in *)&name_addr)->sin_addr),
		addrbuf, 4);
	name_addr.ss_family = AF_INET;
	name_client = get_gsh_client(&name_addr, false);

	/* Check if localhost has sent SM NOTIFY request on behalf of a client
	 * with name as caller name
	 * If yes, then we can use the client IP instead of localhost IP
	 */
	if (name_client != NULL &&
		(strcmp(name_client->hostaddr_str,
		op_ctx->client->hostaddr_str) != 0)) {

		LogDebug(COMPONENT_NLM,
			"SM_NOTIFY request using host address: %s",
			name_client->hostaddr_str);
		memcpy(&(((struct sockaddr_in *)op_ctx->caller_addr)
			->sin_addr), addrbuf, 4);
		SetClientIP(name_client->hostaddr_str);
		op_ctx->client = name_client;
	}
}

static void check_use_caller_name_ipv6(char *name)
{
	unsigned char addrbuf[16];
	sockaddr_t name_addr;
	struct gsh_client *name_client;

	if ((strcmp(op_ctx->client->hostaddr_str,
		IN6_LOCALHOST_STRING) != 0) &&
		(strcmp(op_ctx->client->hostaddr_str,
		IN6_ENCAPSULATED_IN4_LOCALHOST_STRING) != 0))
		return;

	/* get the gsh_client for the caller name */
	if (inet_pton(AF_INET6, name, addrbuf) != 1)
		return;

	memcpy(&(((struct sockaddr_in6 *)&name_addr)->sin6_addr),
		addrbuf, 16);
	name_addr.ss_family = AF_INET6;
	name_client = get_gsh_client(&name_addr, false);

	/* Check if localhost has sent SM NOTIFY request on behalf of a client
	 * with name as caller name
	 * If yes, then we can use the client IP instead of localhost IP
	 */
	if (name_client != NULL &&
		(strcmp(name_client->hostaddr_str,
		op_ctx->client->hostaddr_str) != 0)) {

		LogDebug(COMPONENT_NLM,
			"SM_NOTIFY request using host address: %s",
			name_client->hostaddr_str);
		memcpy(&(((struct sockaddr_in6 *)op_ctx->caller_addr)
			->sin6_addr), addrbuf, 16);
		SetClientIP(name_client->hostaddr_str);
		op_ctx->client = name_client;
	}
}

/*
 * Check if a SM_NOTIFY request is given by the 'localhost'
 * If that is the case check the caller name, if it is not same as 'localhost'
 * then a 'localhost' has formed a NLM_SM_NOTIFY request for a client which
 * has held lock(s) but not available to release the lock(s). The client is
 * specified with the caller name in the request. Use the client's caller
 * name to get the client IP and use it instead of the 'localhost' IP.
 */
static void check_use_caller_name_ip(char *name)
{
	if (op_ctx->caller_addr->ss_family == AF_INET)
		check_use_caller_name_ipv4(name);
	else
		check_use_caller_name_ipv6(name);
}

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

	LogDebug(COMPONENT_NLM,
		 "REQUEST PROCESSING: Calling nlm4_sm_notify for %s",
		 arg->name);

	/* Check if the SM_NOTIFY request is from the 'localhost' */
	check_use_caller_name_ip(arg->name);

	nsm_client = get_nsm_client(CARE_NOT, NULL, arg->name);

	if (nsm_client != NULL) {
		/* Cast the state number into a state pointer to protect
		 * locks from a client that has rebooted from being released
		 * by this SM_NOTIFY.
		 */
		state_status = state_nlm_notify(nsm_client, true, arg->state);

		if (state_status != STATE_SUCCESS) {
			/** @todo FSF: Deal with error
			 */
		}

		dec_nsm_client_ref(nsm_client);
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
