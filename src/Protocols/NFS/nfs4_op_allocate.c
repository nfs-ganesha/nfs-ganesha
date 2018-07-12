/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2018 Jeff Layton <jlayton@redhat.com>
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
 * @file nfs4_op_allocate.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions ALLOCATE and
 * DEALLOCATE.
 */

#include "config.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "fsal_pnfs.h"
#include "server_stats.h"
#include "export_mgr.h"

static int allocate_deallocate(compound_data_t *data, stateid4 *stateid,
			       uint64_t offset, uint64_t size, bool allocate)
{
	int status;
	state_t *state = NULL;
	fsal_status_t fsal_status = {0, 0};
	struct fsal_obj_handle *obj = NULL;
	uint64_t MaxOffsetWrite =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxOffsetWrite);

	/* Only files can have their allocation info changed */
	status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);
	if (status != NFS4_OK)
		return status;

	/* if quota support is active, then we should check if the FSAL
	   allows block allocation */
	fsal_status = op_ctx->fsal_export->exp_ops.check_quota(
						op_ctx->fsal_export,
						op_ctx->ctx_export->fullpath,
						FSAL_QUOTA_BLOCKS);
	if (FSAL_IS_ERROR(fsal_status)) {
		status = NFS4ERR_DQUOT;
		return status;
	}

	obj = data->current_obj;

	/* Check stateid correctness and get pointer to state
	 * (also checks for special stateids)
	 */
	status = nfs4_Check_Stateid(stateid, obj, &state, data,
				    STATEID_SPECIAL_ANY, 0, false,
				    allocate ? "ALLOCATE" : "DEALLOCATE");

	if (status != NFS4_OK)
		return status;

	/* NB: After this points, if state == NULL, then
	 * the stateid is all-0 or all-1
	 */
	if (state != NULL) {
		struct state_t *state_open;
		struct state_deleg *sdeleg;

		switch (state->state_type) {
		case STATE_TYPE_SHARE:
			break;
		case STATE_TYPE_LOCK:
			state_open = state->state_data.lock.openstate;
			inc_state_t_ref(state_open);
			dec_state_t_ref(state);
			state = state_open;
			break;
		case STATE_TYPE_DELEG:
			/*
			 * As with WRITE, the stateid is just here to provide
			 * ordering info with respect to locks and such.
			 * Delegation and layout stateids aren't generally
			 * useful for ordering so we just use a NULL state
			 * pointer here (conceptually similar to the anonymous
			 * stateids).
			 */
			/* Check if the delegation state allows WRITE */
			sdeleg = &state->state_data.deleg;
			if (!(sdeleg->sd_type & OPEN_DELEGATE_WRITE)) {
				/* Invalid delegation for this operation. */
				LogDebug(COMPONENT_STATE,
					"Delegation type:%d state:%d",
					sdeleg->sd_type,
					sdeleg->sd_state);
				status = NFS4ERR_BAD_STATEID;
				goto out;
			}
			break;
		default:
			status = NFS4ERR_BAD_STATEID;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "ALLOCATE with invalid stateid of type %d",
				 (int)state->state_type);
			goto out;
		}

		/* This is an ALLOCATE operation, this means that the file
		 * MUST have been opened for writing
		 */
		if (state != NULL &&
		    (state->state_data.share.share_access &
		     OPEN4_SHARE_ACCESS_WRITE) == 0) {
			/* Bad open mode, return NFS4ERR_OPENMODE */
			status = NFS4ERR_OPENMODE;
				if (isDebug(COMPONENT_NFS_V4_LOCK)) {
					char str[LOG_BUFF_LEN] = "\0";
					struct display_buffer dspbuf = {
							sizeof(str), str, str};
					display_stateid(&dspbuf, state);
					LogDebug(COMPONENT_NFS_V4_LOCK,
						 "ALLOCATE %s doesn't have OPEN4_SHARE_ACCESS_WRITE",
						 str);
				}
			goto out;
		}
	} else {
		/*
		 * We have an anonymous stateid -- ensure that it doesn't
		 * conflict with an outstanding delegation.
		 */
		if (state_deleg_conflict(obj, true)) {
			status = NFS4ERR_DELAY;
			goto out;
		}
	}

	/* Same permissions as required for a WRITE */
	fsal_status = obj->obj_ops->test_access(obj, FSAL_WRITE_ACCESS,
					       NULL, NULL, true);

	if (FSAL_IS_ERROR(fsal_status)) {
		status = nfs4_Errno_status(fsal_status);
		goto out;
	}

	/* Get the characteristics of the I/O to be made */
	if (MaxOffsetWrite < UINT64_MAX) {
		LogFullDebug(COMPONENT_NFS_V4,
			     "Write offset=%" PRIu64 " count=%" PRIu64
			     " MaxOffSet=%" PRIu64, offset, size,
			     MaxOffsetWrite);

		if ((offset + size) > MaxOffsetWrite) {
			LogEvent(COMPONENT_NFS_V4,
				 "A client tried to violate max file size %"
				 PRIu64 " for exportid #%hu",
				 MaxOffsetWrite,
				 op_ctx->ctx_export->export_id);
			status = NFS4ERR_FBIG;
			goto out;
		}
	}

	LogFullDebug(COMPONENT_NFS_V4,
		     "offset = %" PRIu64 "  length = %" PRIu64 "allocate = %d",
		     offset, size, allocate);

	/* if size == 0 , nothing changes -- just say success */
	if (size == 0) {
		status = NFS4_OK;
		goto out;
	}

	/* Do the actual fallocate */
	fsal_status = obj->obj_ops->fallocate(obj, state, offset, size,
						allocate);
	if (FSAL_IS_ERROR(fsal_status))
		status = nfs4_Errno_status(fsal_status);
out:
	if (state != NULL)
		dec_state_t_ref(state);

	return status;
}				/* nfs4_op_allocate */

/**
 * @brief The NFS4_OP_ALLOCATE operation
 * This functions handles the NFS4_OP_ALLOCATE operation in NFSv4.2. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 */
int nfs4_op_allocate(struct nfs_argop4 *op, compound_data_t *data,
		     struct nfs_resop4 *resp)
{
	int status;
	ALLOCATE4args * const arg_ALLOCATE4 = &op->nfs_argop4_u.opallocate;
	ALLOCATE4res * const res_ALLOCATE4 = &resp->nfs_resop4_u.opallocate;

	resp->resop = NFS4_OP_ALLOCATE;

	status = allocate_deallocate(data, &arg_ALLOCATE4->aa_stateid,
				     arg_ALLOCATE4->aa_offset,
				     arg_ALLOCATE4->aa_length, true);
	res_ALLOCATE4->ar_status = status;
	return status;
}


/**
 * @brief The NFS4_OP_DEALLOCATE
 * This functions handles the NFS4_OP_DEALLOCATE operation in NFSv4.2. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 */
int nfs4_op_deallocate(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	int status;
	DEALLOCATE4args * const arg_DEALLOC = &op->nfs_argop4_u.opdeallocate;
	DEALLOCATE4res * const res_DEALLOC = &resp->nfs_resop4_u.opdeallocate;

	resp->resop = NFS4_OP_DEALLOCATE;

	status = allocate_deallocate(data, &arg_DEALLOC->da_stateid,
				     arg_DEALLOC->da_offset,
				     arg_DEALLOC->da_length, false);
	res_DEALLOC->dr_status = status;
	return status;
}
