// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2026, IBM . All rights reserved.
 * Author: Deeraj Patil <deeraj.patil@ibm.com>
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
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/>
 *
 * ---------------------------------------
 */

/**
 * @file nfs4_op_offload_status.c
 * @brief NFS4_OP_OFFLOAD_STATUS - Query progress of async COPY operation.
 *
 * Implements the NFSv4.2 OFFLOAD_STATUS operation (RFC 7862 Sec.15.9) for
 * polling/returns  the live progress of an asynchronous COPY operation.
 * Identified by:
 * CURRENT_FH (destination file) + osa_stateid (copy offload stateid).
 */

#include "config.h"
#include <time.h>

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
 * @brief NFS4_OP_OFFLOAD_STATUS - Query progress of async COPY operation
 *
 * RFC 7862 Section 15.9: Returns the live progress of an asynchronous
 * COPY operation identified by CURRENT_FH (destination file) + osa_stateid
 * (copy offload stateid). Can be polled at any time between steps 1 and 3
 * of the async copy workflow described in nfs4_op_copy().
 *
 * @param[in]  op   NFS argument containing OFFLOAD_STATUS4args with stateid
 * @param[in]  data Compound data including CURRENT_FH for state validation
 * @param[out] resp NFS response to populate with progress results
 *
 * @return NFS request result status code.
 */
enum nfs_req_result nfs4_op_offload_status(struct nfs_argop4 *op,
					   compound_data_t *data,
					   struct nfs_resop4 *resp)
{
	OFFLOAD_STATUS4args *const args = &op->nfs_argop4_u.opoffload_status;
	OFFLOAD_STATUS4res *const res = &resp->nfs_resop4_u.opoffload_status;
	struct state_t *state = NULL;
	struct copy_offload_state *cos;
	uint64_t bytes_copied;
	bool complete;
	nfsstat4 completion_status;

	resp->resop = NFS4_OP_OFFLOAD_STATUS;
	res->osr_status = NFS4_OK;

	/*
	 * RFC 15.9.3: "OFFLOAD_STATUS can be used by the client to query
	 * the progress of an asynchronous operation."  Seeing this in the
	 * log means the client IS polling (rhel9 kernel does not support this).
	 */
	LogFullDebug(COMPONENT_NFS_V4,
		     "OFFLOAD_STATUS received poll copy stateid seqid=%" PRIu32,
		     args->osa_stateid.seqid);

	if (args->osa_stateid.seqid == 0) {
		LogWarn(COMPONENT_NFS_V4,
			"OFFLOAD_STATUS: seqid=0 is invalid return BAD_STATEID");
		res->osr_status = NFS4ERR_BAD_STATEID;
		return nfsstat4_to_nfs_req_result(res->osr_status);
	}

	/*
	 * Passing data->current_obj let nfs4_Check_Stateid() verify that the
	 * stateid's state_obj (the destination recorded at COPY time)
	 * matches the CURRENT_FH sent now.
	 */
	res->osr_status = nfs4_Check_Stateid(&args->osa_stateid,
					     data->current_obj, &state, data,
					     STATEID_SPECIAL_ANY, 0, false,
					     "OFFLOAD_STATUS");
	if (res->osr_status != NFS4_OK) {
		LogFullDebug(COMPONENT_NFS_V4,
			     "OFFLOAD_STATUS: nfs4_Check_Stateid returned %s",
			     nfsstat4_to_str(res->osr_status));
		return nfsstat4_to_nfs_req_result(res->osr_status);
	}

	/*
	 * nfs4_Check_Stateid() returns any matching state_t; guard against
	 * a stateid that happens to hit the same hash slot but belongs to a
	 * different state type
	 * expected state is STATE_TYPE_COPY_OFFLOAD.
	 */
	if (state->state_type != STATE_TYPE_COPY_OFFLOAD) {
		LogWarn(COMPONENT_NFS_V4,
			"OFFLOAD_STATUS: failed type=%d, return BAD_STATEID",
			(int)state->state_type);
		res->osr_status = NFS4ERR_BAD_STATEID;
		goto out;
	}

	cos = container_of(state, struct copy_offload_state, cos_state);
	complete = atomic_fetch_uint8_t((uint8_t *)&cos->cos_complete);
	bytes_copied = atomic_fetch_uint64_t(&cos->cos_bytes_copied);
	completion_status = cos->cos_status;

	/* Log detailed progress for tracking */
	uint64_t total = cos->cos_total_count;
	uint32_t seqid = cos->cos_state.state_seqid;
	uint64_t elapsed_s = 0;

	if (cos->cos_start_time.seconds) {
		struct timespec now;

		clock_gettime(CLOCK_REALTIME, &now);
		elapsed_s = (uint64_t)now.tv_sec -
			    (uint64_t)cos->cos_start_time.seconds;
	}

	LogFullDebug(COMPONENT_NFS_V4,
		     "OFFLOAD_STATUS poll detail: copystateid_seqid=%" PRIu32
		     " bytes_copied=%" PRIu64 " total_requested=%" PRIu64
		     " pct=%" PRIu64 "%% elapsed_s=%" PRIu64
		     " complete=%s status=%d (%s)  cancelled=%s",
		     seqid, bytes_copied, total,
		     total ? (bytes_copied * 100 / total) : 0, elapsed_s,
		     complete ? "YES" : "NO",
		     (int)(complete ? completion_status : NFS4_OK),
		     complete ? nfsstat4_to_str(completion_status)
			      : "in-progress",
		     atomic_fetch_uint8_t((uint8_t *)&cos->cos_cancelled)
			     ? "YES"
			     : "NO");

	res->OFFLOAD_STATUS4res_u.osr_resok4.osr_bytes_copied =
		(length4)bytes_copied;

	if (complete) {
		res->OFFLOAD_STATUS4res_u.osr_resok4.osr_count_complete = 1;
		res->OFFLOAD_STATUS4res_u.osr_resok4.osr_complete =
			completion_status;
	} else {
		res->OFFLOAD_STATUS4res_u.osr_resok4.osr_count_complete = 0;
		res->OFFLOAD_STATUS4res_u.osr_resok4.osr_complete = NFS4_OK;
	}

	LogFullDebug(COMPONENT_NFS_V4,
		     "OFFLOAD_STATUS response osr_bytes_copied=%" PRIu64
		     " osr_count_complete=%u osr_complete=%d (%s)",
		     bytes_copied, complete ? 1u : 0u,
		     complete ? (int)completion_status : (int)NFS4_OK,
		     complete ? (completion_status == NFS4_OK ? "done-OK"
							      : "done-ERR")
			      : "in-progress");

out:
	if (state != NULL)
		dec_state_t_ref(state);

	return nfsstat4_to_nfs_req_result(res->osr_status);
}

void nfs4_op_offload_status_Free(nfs_resop4 *resp)
{
	(void)resp;
}
