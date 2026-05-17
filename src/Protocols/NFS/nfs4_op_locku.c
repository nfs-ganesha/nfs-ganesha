// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * @file    nfs4_op_locku.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "hashtable.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs4.h"
#endif

static const char *locku_tag = "LOCKU";

/**
 *
 * @brief The NFS4_OP_LOCKU operation
 *
 * This function implements the NFS4_OP_LOCKU operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 368
 *
 * @see nfs4_Compound
 */

enum nfs_req_result nfs4_op_locku(struct nfs_argop4 *op, compound_data_t *data,
				  struct nfs_resop4 *resp)
{
	/* Alias for arguments */
	LOCKU4args *const arg_LOCKU4 = &op->nfs_argop4_u.oplocku;
	/* Alias for response */
	LOCKU4res *const res_LOCKU4 = &resp->nfs_resop4_u.oplocku;
	/* Return for state functions */
	state_status_t state_status = STATE_SUCCESS;
	/* Found lock state */
	state_t *state_found = NULL;
	/* Owner of lock state */
	state_owner_t *lock_owner = NULL;
	/* Description of lock to free */
	fsal_lock_param_t lock_desc;
	/*  */
	nfsstat4 nfs_status = NFS4_OK;
	struct fsal_obj_handle *state_obj = NULL;
	bool state_handle_lock_held = false;
	uint64_t maxfilesize = op_ctx->fsal_export->exp_ops.fs_maxfilesize(
		op_ctx->fsal_export);

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Entering NFS v4 LOCKU handler ----------------------------");
	GSH_AUTO_TRACEPOINT(
		nfs4, op_locku_start, TRACE_INFO,
		"LOCKU arg: type={} seqid={} stateid={} offset={} length={}",
		arg_LOCKU4->locktype, arg_LOCKU4->seqid,
		arg_LOCKU4->lock_stateid.seqid, arg_LOCKU4->offset,
		arg_LOCKU4->length);

	/* Initialize to sane default */
	resp->resop = NFS4_OP_LOCKU;
	res_LOCKU4->status = NFS4_OK;

	res_LOCKU4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (res_LOCKU4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Convert lock parameters to internal types */
	switch (arg_LOCKU4->locktype) {
	case READ_LT:
	case READW_LT:
		lock_desc.lock_type = FSAL_LOCK_R;
		break;

	case WRITE_LT:
	case WRITEW_LT:
		lock_desc.lock_type = FSAL_LOCK_W;
		break;
	default:
		LogDebug(COMPONENT_NFS_V4_LOCK, "Invalid lock type");
		res_LOCKU4->status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	lock_desc.lock_start = arg_LOCKU4->offset;
	lock_desc.lock_sle_type = FSAL_POSIX_LOCK;
	lock_desc.lock_reclaim = false;

	if (arg_LOCKU4->length != STATE_LOCK_OFFSET_EOF)
		lock_desc.lock_length = arg_LOCKU4->length;
	else
		lock_desc.lock_length = 0;

	nfs_status = nfs4_check_stateid_acquire_state_lock(
		&arg_LOCKU4->lock_stateid, data->current_obj, data,
		STATEID_SPECIAL_FOR_LOCK, arg_LOCKU4->seqid,
		data->minorversion == 0,
		/*should_lock=*/true, locku_tag, &state_found, &state_obj,
		&lock_owner, &state_handle_lock_held);

	if (nfs_status != NFS4_OK && nfs_status != NFS4ERR_REPLAY) {
		res_LOCKU4->status = nfs_status;
		return NFS_REQ_ERROR;
	}

	if (lock_owner == NULL) {
		/* State is going stale. */
		res_LOCKU4->status = NFS4ERR_STALE;
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "UNLOCK failed nfs4_Check_Stateid, stale lock owner");
		goto out2;
	}

	/* Check seqid (lock_seqid or open_seqid) */
	if (data->minorversion == 0) {
		PTHREAD_MUTEX_lock(&lock_owner->so_mutex);
		if (!Check_nfs4_seqid_locked(lock_owner, arg_LOCKU4->seqid, op,
					     data->current_obj, resp,
					     locku_tag)) {
			/* Response is all setup for us and LogDebug
			 * told what was wrong
			 */
			PTHREAD_MUTEX_unlock(&lock_owner->so_mutex);
			goto out2;
		}
		PTHREAD_MUTEX_unlock(&lock_owner->so_mutex);
	}

	/* Lock length should not be 0 */
	if (arg_LOCKU4->length == 0LL) {
		res_LOCKU4->status = NFS4ERR_INVAL;
		goto out;
	}

	/* Check for range overflow Remember that a length with all
	 * bits set to 1 means "lock until the end of file" (RFC3530,
	 * page 157)
	 */
	if (lock_desc.lock_length >
	    (STATE_LOCK_OFFSET_EOF - lock_desc.lock_start)) {
		res_LOCKU4->status = NFS4ERR_INVAL;
		goto out;
	}

	/* Check for range overflow past maxfilesize.  Comparing beyond 2^64 is
	 * not possible in 64 bits precision, but off+len > maxfilesize is
	 * equivalent to len > maxfilesize - off.  We checked 64-bit overflow
	 * above, so treat overflowing the FS maxsize as a request to lock the
	 * entire file.
	 */
	if (lock_desc.lock_length > (maxfilesize - lock_desc.lock_start)) {
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "LOCK past maxfilesize %" PRIx64 " start %" PRIx64
			 " length %" PRIx64,
			 maxfilesize, lock_desc.lock_start,
			 lock_desc.lock_length);
		lock_desc.lock_length = 0;
	}

	LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG, locku_tag,
		data->current_obj, lock_owner, &lock_desc);

	if (data->minorversion == 0) {
		op_ctx->clientid =
			&lock_owner->so_owner.so_nfs4_owner.so_clientid;
	}

	/* Now we have a lock owner and a stateid.  Go ahead and push
	   unlock into SAL (and FSAL). */
	state_status = state_unlock_locked(data->current_obj, state_found,
					   lock_owner, false, 0, &lock_desc);

	if (state_status != STATE_SUCCESS) {
		res_LOCKU4->status = nfs4_Errno_state(state_status);
		goto out;
	}

	if (data->minorversion == 0)
		op_ctx->clientid = NULL;

	/* Successful exit */
	res_LOCKU4->status = NFS4_OK;

	/* Handle stateid/seqid for success */
	update_stateid_locked(state_found,
			      &res_LOCKU4->LOCKU4res_u.lock_stateid, data,
			      locku_tag);

out:
	if (data->minorversion == 0) {
		/* Save the response in the lock owner */
		Copy_nfs4_state_req(lock_owner, arg_LOCKU4->seqid, op,
				    data->current_obj, resp, locku_tag);
	}

out2:
	if (state_handle_lock_held) {
		STATELOCK_unlock(state_obj);
		state_handle_lock_held = false;
	}
	if (lock_owner != NULL) {
		dec_state_owner_ref(lock_owner);
	}
	if (state_obj != NULL) {
		state_obj->obj_ops->put_ref(state_obj);
	}
	if (state_found != NULL) {
		dec_state_t_ref(state_found);
	}

	GSH_AUTO_TRACEPOINT(nfs4, op_locku_end, TRACE_INFO,
			    "LOCKU res: status={} stateid={}",
			    res_LOCKU4->status,
			    res_LOCKU4->LOCKU4res_u.lock_stateid.seqid);
	return nfsstat4_to_nfs_req_result(res_LOCKU4->status);
} /* nfs4_op_locku */

/**
 * @brief Free memory allocated for LOCKU result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOCKU operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_locku_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}

void nfs4_op_locku_CopyRes(LOCKU4res *res_dst, LOCKU4res *res_src)
{
	/* Nothing to deep copy */
}
