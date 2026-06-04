// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
 * Author: Deeraj Patil <deeraj.patil@ibm.com>
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
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/>
 *
 * ---------------------------------------
 */

/**
 * @file nfs4_op_offload_cancel.c
 * @brief NFS4_OP_OFFLOAD_CANCEL - Cancel an async COPY operation.
 *
 * Implements the NFSv4.2 OFFLOAD_CANCEL operation (RFC 7862 Sec.15.8) for
 * cancelling an in-progress asynchronous COPY operation.
 */

#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "fsal.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "sal_data.h"

#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs4.h"
#endif

/**
 * @brief NFS4_OP_OFFLOAD_CANCEL
 *
 * Client sends a copy stateid to abort.
 * Server sets the cancelled flag; the worker stops at the next chunk
 * boundary.
 *
 * Stateid validation uses nfs4_Check_Stateid() - the standard SAL path -
 * because create_copy_offload_state() now sets:
 *   state_obj   = destination fsal_obj_handle  (CURRENT_FH at COPY time)
 *   state_owner = &clientid->cid_owner         (STATE_CLIENTID_OWNER_NFSV4)
 * matching the pattern of STATE_TYPE_LAYOUT (nfs4_op_layoutget.c:100,182).
 *
 * "A copy offload stateid's seqid MUST NOT be zero."
 * seqid=0 is rejected before calling nfs4_Check_Stateid() because
 * nfs4_Check_Stateid() would otherwise skip the seqid check for
 * non-LAYOUT states when seqid==0 (nfs4_state_id.c:1119).
 *
 * Lease validation: SEQUENCE already called reserve_lease_or_expire()
 * and set data->preserved_clientid.  nfs4_Check_Stateid() line 1075
 * skips lease reservation because preserved_clientid == cid_owner's
 * so_clientrec - the lease is already held by SEQUENCE.
 *
 * @param[in] op  Pointer to the incoming NFS argument structure
 * @param[in] data  Pointer to the compound operation execution context
 * @param[out] resp  Pointer to the response structure for results
 *
 * @return NFS request result code (nfs_req_result)
 */
enum nfs_req_result nfs4_op_offload_cancel(struct nfs_argop4 *op,
					   compound_data_t *data,
					   struct nfs_resop4 *resp)
{
	OFFLOAD_CANCEL4args *const args = &op->nfs_argop4_u.opoffload_cancel;
	OFFLOAD_CANCEL4res *const res = &resp->nfs_resop4_u.opoffload_cancel;
	struct state_t *state = NULL;
	struct copy_offload_state *cos;

	resp->resop = NFS4_OP_OFFLOAD_CANCEL;
	res->ocr_status = NFS4_OK;

	LogFullDebug(COMPONENT_NFS_V4,
		     "OFFLOAD_CANCEL: looking up copy stateid seqid=%" PRIu32,
		     args->oca_stateid.seqid);

	if (args->oca_stateid.seqid == 0) {
		LogWarn(COMPONENT_NFS_V4,
			"OFFLOAD_CANCEL: seqid=0 invalid return BAD_STATEID");
		res->ocr_status = NFS4ERR_BAD_STATEID;
		return nfsstat4_to_nfs_req_result(res->ocr_status);
	}

	res->ocr_status = nfs4_Check_Stateid(&args->oca_stateid,
					     data->current_obj, &state, data,
					     STATEID_SPECIAL_ANY, 0, false,
					     "OFFLOAD_CANCEL");
	if (res->ocr_status != NFS4_OK) {
		LogFullDebug(COMPONENT_NFS_V4,
			     "OFFLOAD_CANCEL: nfs4_Check_Stateid returned %s",
			     nfsstat4_to_str(res->ocr_status));
		return nfsstat4_to_nfs_req_result(res->ocr_status);
	}

	if (state->state_type != STATE_TYPE_COPY_OFFLOAD) {
		LogWarn(COMPONENT_NFS_V4,
			"OFFLOAD_CANCEL: stateid type=%d, expected "
			"STATE_TYPE_COPY_OFFLOAD, returning BAD_STATEID",
			(int)state->state_type);
		res->ocr_status = NFS4ERR_BAD_STATEID;
		goto out;
	}

	cos = container_of(state, struct copy_offload_state, cos_state);

	atomic_store_uint8_t(&cos->cos_cancelled, 1);

	LogFullDebug(COMPONENT_NFS_V4,
		     "OFFLOAD_CANCEL: flagged stateid seqid=%u "
		     "- worker will stop at next chunk boundary",
		     cos->cos_state.state_seqid);

out:
	if (state != NULL)
		dec_state_t_ref(state);

	return nfsstat4_to_nfs_req_result(res->ocr_status);
}

void nfs4_op_offload_cancel_Free(nfs_resop4 *resp)
{
	(void)resp;
}
