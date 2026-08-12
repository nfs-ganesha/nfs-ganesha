// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2026, IBM . All rights reserved.
 * Author: Suhas Athani <Suhas.Athani@ibm.com>
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
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

/**
 * @file nfs_cbsim_grpc.c
 * @brief C-side implementation of CBSIM FakeRecall for gRPC
 */

#include "config.h"
#include <string.h>
#include <stdio.h>
#include "log.h"
#include "sal_functions.h"
#include "nfs_rpc_callback.h"
#include "nfs_cbsim_grpc.h"
#include "abstract_mem.h"

/**
 * @brief Completion hook for FakeRecall CB_RECALL calls
 */
static void fake_recall_completion(rpc_call_t *call)
{
	nfs_client_id_t *pclientid = call->call_arg;

	LogDebug(COMPONENT_NFS_CB, "%p %s", call,
		 !(call->states & NFS_CB_CALL_ABORTED) ? "Success" : "Failed");
	if (!(call->states & NFS_CB_CALL_ABORTED)) {
		LogMidDebug(COMPONENT_NFS_CB, "call result: %d",
			    call->call_req.cc_error.re_status);
	} else {
		LogDebug(COMPONENT_NFS_CB, "Aborted: %d",
			 call->call_req.cc_error.re_status);
	}

	if (pclientid)
		dec_client_id_ref(pclientid);
}

/**
 * @brief Fake/force a CB_RECALL for the given client id
 *
 * Mirrors org.ganesha.nfsd.cbsim.fake_recall: test the callback
 * channel, then submit a demonstration CB_RECALL compound.
 */
void grpc_cbsim_fake_recall(uint64_t client_id, bool *success, char *errmsg,
			    size_t errmsg_len)
{
	clientid4 clientid = client_id;
	nfs_client_id_t *pclientid = NULL;
	rpc_call_channel_t *chan = NULL;
	nfs_cb_argop4 argop[1];
	rpc_call_t *call;
	int code;

	if (success)
		*success = false;
	if (errmsg && errmsg_len > 0)
		errmsg[0] = '\0';

	LogDebug(COMPONENT_GRPC, "FakeRecall called with clientid %" PRIx64,
		 clientid);

	code = nfs_client_id_get_confirmed(clientid, &pclientid);
	if (code != CLIENT_ID_SUCCESS) {
		LogCrit(COMPONENT_NFS_CB,
			"No clid record for %" PRIx64 " (%d) code %d", clientid,
			(int32_t)clientid, code);
		if (errmsg && errmsg_len > 0)
			snprintf(errmsg, errmsg_len,
				 "No confirmed client id record");
		return;
	}

	/* Exercise the callback channel before sending CB_RECALL */
	nfs_test_cb_chan(pclientid);

	chan = nfs_rpc_get_chan(pclientid, NFS_RPC_FLAG_NONE);
	if (!chan || !chan->clnt || !chan->auth) {
		LogCrit(COMPONENT_NFS_CB,
			"nfs_rpc_get_chan failed for clientid %" PRIx64,
			clientid);
		dec_client_id_ref(pclientid);
		if (errmsg && errmsg_len > 0)
			snprintf(errmsg, errmsg_len,
				 "Failed to get callback channel");
		return;
	}

	call = alloc_rpc_call();
	call->chan = chan;

	/* Tag must outlive the async CB call (literal is fine). */
	cb_compound_init_v4(&call->cbt, 6, 0,
			    pclientid->cid_cb.v40.cb_callback_ident,
			    "brrring!!!", 10);

	memset(argop, 0, sizeof(nfs_cb_argop4));
	argop->argop = NFS4_OP_CB_RECALL;
	argop->nfs_cb_argop4_u.opcbrecall.stateid.seqid = 0xdeadbeef;
	memcpy(argop->nfs_cb_argop4_u.opcbrecall.stateid.other,
	       "\0xde\0xad\0xbe\0xef\0xde\0xad\0xbe\0xef\0xde\0xad\0xbe\0xef",
	       sizeof(argop->nfs_cb_argop4_u.opcbrecall.stateid.other));
	argop->nfs_cb_argop4_u.opcbrecall.truncate = TRUE;
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_len = 11;
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val =
		gsh_strdup("0xabadcafe", MEM_COMP_PROTOCOL);

	cb_compound_add_op(&call->cbt, argop);

	/* Hold clientid ref until completion frees the call */
	call->call_hook = fake_recall_completion;
	call->call_arg = pclientid;

	code = nfs_rpc_call(call, NFS_RPC_CALL_NONE);
	if (code) {
		free_rpc_call(call);
		dec_client_id_ref(pclientid);
		if (errmsg && errmsg_len > 0)
			snprintf(errmsg, errmsg_len, "nfs_rpc_call failed");
		return;
	}

	if (success)
		*success = true;
}
