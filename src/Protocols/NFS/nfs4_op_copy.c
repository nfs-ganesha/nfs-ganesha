// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
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
 * @file nfs4_op_copy.c
 * NFS4_OP_COPY — synchronous.
 *
 *  Implements the NFSv4.2 COPY operation (RFC 7862 Sec.15.2)
 *  supporting inline copy.
 *
 * Per RFC 7862 15.2:
 *  - SAVED_FH  : source file
 *  - CURRENT_FH: destination file
 *  - ca_source_server_len == 0  → intra-server (this file)
 *  - ca_source_server_len  > 0  → inter-server (between the servers NOTSUPP)
 *
 */

#include "sal_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs4.h"
#endif

/**
 * Thin validation context — stack-allocated in nfs4_op_copy.
 *
 * Holds the copy parameters produced by copy_validate() and consumed by
 * copy_run_sync(). (copy_run_async to be implemented for RFC 7862 15.2.3)
 * Its lifetime ends the moment the
 * dispatch call returns:
 *
 *   Sync path:  copy_run_sync() reads all fields directly from ctx.
 *               nfs4_op_copy's out: label dec's src_state/dst_state.
 *
 */
struct copy_ctx {
	struct fsal_obj_handle *src; /* source file handle */
	struct fsal_obj_handle *dst; /* destination file handle */
	state_t *src_state; /* source open/lock stateid */
	state_t *dst_state; /* destination open/lock stateid */
	uint64_t src_off; /* source file offset */
	uint64_t dst_off; /* destination file offset */
	uint64_t to_copy; /* number of bytes to copy */
};

/**
 * @brief Validate a stateid for COPY source or destination.
 *
 * Checks that the stateid is valid and that the open/lock/delegation
 * state allows read (source) or write (destination) access.
 *
 * @param[in]  sid        Stateid from the client
 * @param[in]  obj        File handle being accessed
 * @param[in]  data       Compound request data
 * @param[in]  need_write TRUE for destination (write check)
 * @param[out] state_out  Resolved state_t (caller must dec_state_t_ref)
 *
 * @return NFS4_OK on success, error code otherwise
 */
static nfsstat4 check_copy_stateid(stateid4 *sid, struct fsal_obj_handle *obj,
				   compound_data_t *data, bool need_write,
				   state_t **state_out)
{
	state_t *state = NULL;
	state_t *state_open = NULL;
	nfsstat4 status;

	status = nfs4_Check_Stateid(sid, obj, &state, data, STATEID_SPECIAL_ANY,
				    0, false,
				    need_write ? "COPY_DST" : "COPY_SRC");
	if (status != NFS4_OK)
		return status;

	if (state != NULL) {
		switch (state->state_type) {
		case STATE_TYPE_SHARE:
			if (need_write &&
			    !(state->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_WRITE)) {
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			if (!need_write &&
			    !(state->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_READ)) {
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			break;
		case STATE_TYPE_LOCK:
			state_open = nfs4_State_Get_Pointer(
				state->state_data.lock.openstate_key);
			if (state_open == NULL) {
				dec_state_t_ref(state);
				return NFS4ERR_BAD_STATEID;
			}
			if (need_write &&
			    !(state_open->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_WRITE)) {
				dec_state_t_ref(state_open);
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			if (!need_write &&
			    !(state_open->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_READ)) {
				dec_state_t_ref(state_open);
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			dec_state_t_ref(state_open);
			break;
		case STATE_TYPE_DELEG:
			if (need_write) {
				struct state_deleg *sd =
					&state->state_data.deleg;
				if (sd->sd_type != OPEN_DELEGATE_WRITE &&
				    sd->sd_type !=
					    OPEN_DELEGATE_WRITE_ATTRS_DELEG) {
					return NFS4ERR_BAD_STATEID;
				}
			}
			break;
		default:
			dec_state_t_ref(state);
			return NFS4ERR_BAD_STATEID;
		}
	} else if (state_deleg_conflict(obj, need_write)) {
		return NFS4ERR_DELAY;
	}

	*state_out = state;
	return NFS4_OK;
}

/**
 * @brief Fill COPY4resok — the single place that stamps the RPC reply.
 *
 * Sync (RFC 7862 15.2.3):
 *   wr_ids=0, wr_count=<bytes>, FILE_SYNC4, cr_synchronous=TRUE
 *
 * Async (RFC 7862 15.2.3):
 *   wr_ids=1, wr_callback_id=<copy stateid>, wr_count=0, UNSTABLE4,
 *   cr_synchronous=FALSE; CB_OFFLOAD follows when the worker finishes.
 */
static void fill_copy_response(COPY4res *res, uint64_t sync_bytes_copied)
{
	COPY4resok *resok = &res->COPY4res_u.cr_resok4;
	struct gsh_buffdesc verf_desc;

	res->cr_status = NFS4_OK;
	resok->cr_consecutive = TRUE;

	resok->cr_response.wr_ids = 0;
	resok->cr_response.wr_count = (length4)sync_bytes_copied;
	resok->cr_response.wr_committed = FILE_SYNC4;

	verf_desc.addr = resok->cr_response.wr_writeverf;
	verf_desc.len = sizeof(verifier4);
	op_ctx->fsal_export->exp_ops.get_write_verifier(op_ctx->fsal_export,
							&verf_desc);

	resok->cr_synchronous = TRUE;

	LogDebug(COMPONENT_NFS_V4,
		 "COPY sync response: wr_count=%" PRIu64
		 " wr_ids=0 cr_synchronous=TRUE",
		 sync_bytes_copied);
}

/**
 * @brief Validate handles, stateids, access, and compute copy length.
 *
 * Writes into the already-allocated @a ctx (zeroed by the caller).
 *
 * On success the caller owns ctx->src_state / ctx->dst_state and MUST
 * release them via dec_state_t_ref().
 *
 * On error this function releases any state refs it has already acquired
 * before returning, so the caller never needs to inspect ctx on failure.
 */
static nfsstat4 copy_validate(COPY4args *args, compound_data_t *data,
			      struct copy_ctx *ctx)
{
	struct fsal_attrlist attrs;
	struct saved_export_context saved_src_ctx = { NULL };
	fsal_status_t fsal_st;
	uint64_t src_size = 0;
	uint64_t to_copy = args->ca_count;
	uint64_t MaxOffsetWrite;
	nfsstat4 status;

	ctx->src_off = args->ca_src_offset;
	ctx->dst_off = args->ca_dst_offset;

	if (args->ca_source_server_len > 0)
		return NFS4ERR_NOTSUPP;

	status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);
	if (status != NFS4_OK)
		return status;

	status = nfs4_sanity_check_saved_FH(data, REGULAR_FILE, false);
	if (status != NFS4_OK)
		return status;

	ctx->src = data->saved_obj;
	ctx->dst = data->current_obj;

	if (ctx->src == ctx->dst)
		return NFS4ERR_INVAL;

	status = check_copy_stateid(&args->ca_src_stateid, ctx->src, data,
				    false, &ctx->src_state);
	if (status != NFS4_OK)
		return status;

	status = check_copy_stateid(&args->ca_dst_stateid, ctx->dst, data, true,
				    &ctx->dst_state);
	if (status != NFS4_OK)
		goto err_release;

	fsal_st = ctx->src->obj_ops->test_access(ctx->src, FSAL_READ_ACCESS,
						 NULL, NULL, true);
	if (FSAL_IS_ERROR(fsal_st)) {
		status = nfs4_Errno_status(fsal_st);
		goto err_release;
	}

	fsal_st = ctx->dst->obj_ops->test_access(ctx->dst, FSAL_WRITE_ACCESS,
						 NULL, NULL, true);
	if (FSAL_IS_ERROR(fsal_st)) {
		status = nfs4_Errno_status(fsal_st);
		goto err_release;
	}

	/*
	 * Cross-FSAL guard: op_ctx->fsal_export currently points at the dst
	 * export (set by the preceding PUTFH for the copy destination).
	 * Temporarily switch op_ctx to the saved (src) export for this call.
	 */
	get_gsh_export_ref(data->saved_export);
	save_op_context_export_and_set_export(&saved_src_ctx,
					      data->saved_export);
	op_ctx->export_perms = data->saved_export_perms;

	fsal_prepare_attrs(&attrs, ATTR_SIZE);
	fsal_st = ctx->src->obj_ops->getattrs(ctx->src, &attrs);
	restore_op_context_export(&saved_src_ctx);

	if (FSAL_IS_ERROR(fsal_st)) {
		fsal_release_attrs(&attrs);
		status = nfs4_Errno_status(fsal_st);
		goto err_release;
	}
	if (FSAL_TEST_MASK(attrs.valid_mask, ATTR_SIZE))
		src_size = attrs.filesize;
	fsal_release_attrs(&attrs);

	if (args->ca_count == 0) {
		to_copy = (ctx->src_off < src_size) ? (src_size - ctx->src_off)
						    : 0;
	} else if (ctx->src_off >= src_size ||
		   ctx->src_off + to_copy > src_size) {
		/*
		 * If the source offset or the source offset plus count
		 * is greater than the size of the source file, the
		 * operation MUST fail with NFS4ERR_INVAL.
		 * Silently clipping the range is NOT permitted.
		 */
		status = NFS4ERR_INVAL;
		goto err_release;
	}

	ctx->to_copy = to_copy;
	if (to_copy == 0)
		return NFS4_OK;

	MaxOffsetWrite =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxOffsetWrite);
	if (MaxOffsetWrite < UINT64_MAX &&
	    ctx->dst_off + to_copy > MaxOffsetWrite) {
		status = NFS4ERR_FBIG;
		goto err_release;
	}

	return NFS4_OK;

err_release:
	if (ctx->src_state != NULL) {
		dec_state_t_ref(ctx->src_state);
		ctx->src_state = NULL;
	}
	if (ctx->dst_state != NULL) {
		dec_state_t_ref(ctx->dst_state);
		ctx->dst_state = NULL;
	}
	return status;
}

/**
 * @brief Execute the inline (synchronous) copy on the service thread.
 *
 * Pure work function: calls copy_file_range and returns the byte count.
 * Does NOT touch the RPC response — the caller fills it via
 * fill_copy_response() so that all reply stamping is visible in one place.
 */
static nfsstat4 copy_run_sync(const struct copy_ctx *ctx, uint64_t *copied_out)
{
	fsal_status_t fsal_st;

	fsal_st = ctx->src->obj_ops->copy_file_range(ctx->src, ctx->src_state,
						     ctx->dst, ctx->dst_state,
						     ctx->src_off, ctx->dst_off,
						     ctx->to_copy, copied_out);
	if (FSAL_IS_ERROR(fsal_st))
		return nfs4_Errno_status(fsal_st);

	LogDebug(COMPONENT_NFS_V4,
		 "COPY sync done: src_off=%" PRIu64 " dst_off=%" PRIu64
		 " req=%" PRIu64 " copied=%" PRIu64,
		 ctx->src_off, ctx->dst_off, ctx->to_copy, *copied_out);

	return NFS4_OK;
}

/**
 * @brief NFS4_OP_COPY - Synchronous copy operation (RFC 7862 15.2).
 *
 * Handles intra-server file copying with optional future async offload.
 * Currently only implements the synchronous path.
 *
 * @param[in]  op    The COPY argument from client request
 * @param[in]  data  Compound operation context/data
 * @param[out] resp  Response structure to populate on success
 *
 * @return NFS_REQ_OK on success, error code otherwise
 */
enum nfs_req_result nfs4_op_copy(struct nfs_argop4 *op, compound_data_t *data,
				 struct nfs_resop4 *resp)
{
	COPY4args *const args = &op->nfs_argop4_u.opcopy;
	COPY4res *const res = &resp->nfs_resop4_u.opcopy;
	struct copy_ctx ctx = { 0 };
	nfsstat4 status;
	uint64_t copied = 0;

	resp->resop = NFS4_OP_COPY;
	res->cr_status = NFS4_OK;
	status = copy_validate(args, data, &ctx);
	if (status != NFS4_OK) {
		res->cr_status = status;
		goto out;
	}

	if (ctx.to_copy == 0) {
		fill_copy_response(res, 0);
		goto out;
	}

	status = copy_run_sync(&ctx, &copied);
	if (status == NFS4_OK)
		fill_copy_response(res, copied);
	else
		res->cr_status = status;

out:
	/*
	 * Sync path and error path: release stateid refs still held by ctx.
	 * Async dispatch path (future): copy_run_async() already nulled both;
	 * these null-guards are no-ops.
	 * ctx is on the stack — no gsh_free.
	 */
	if (ctx.src_state != NULL)
		dec_state_t_ref(ctx.src_state);
	if (ctx.dst_state != NULL)
		dec_state_t_ref(ctx.dst_state);

	GSH_AUTO_TRACEPOINT(nfs4, op_copy_end, TRACE_INFO,
			    "COPY status={} bytes={}", res->cr_status,
			    res->cr_status == NFS4_OK ? copied : 0ULL);

	return nfsstat4_to_nfs_req_result(res->cr_status);
}

void nfs4_op_copy_Free(nfs_resop4 *resp)
{
	/* Nothing to do */
}
