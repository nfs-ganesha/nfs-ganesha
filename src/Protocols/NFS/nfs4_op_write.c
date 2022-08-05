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
 * @file nfs4_op_write.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */

#include "config.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "fsal_pnfs.h"
#include "server_stats.h"
#include "export_mgr.h"

struct nfs4_write_data {
	/** Results for write */
	WRITE4res *res_WRITE4;
	/** Owner of state */
	state_owner_t *owner;
	/* Pointer to compound data */
	compound_data_t *data;
	/** Object being acted on */
	struct fsal_obj_handle *obj;
	/** Flags to control synchronization */
	uint32_t flags;
	/** Arguments for write call - must be last */
	struct fsal_io_arg write_arg;
};

static enum nfs_req_result nfs4_complete_write(struct nfs4_write_data *data)
{
	struct fsal_io_arg *write_arg = &data->write_arg;
	struct gsh_buffdesc verf_desc;

	if (data->res_WRITE4->status != NFS4_OK) {
		goto done;
	}

	if (write_arg->fsal_stable)
		data->res_WRITE4->WRITE4res_u.resok4.committed = FILE_SYNC4;
	else
		data->res_WRITE4->WRITE4res_u.resok4.committed = UNSTABLE4;

	data->res_WRITE4->WRITE4res_u.resok4.count = write_arg->io_amount;

	verf_desc.addr = data->res_WRITE4->WRITE4res_u.resok4.writeverf;
	verf_desc.len = sizeof(verifier4);
	op_ctx->fsal_export->exp_ops.get_write_verifier(op_ctx->fsal_export,
							&verf_desc);

done:

	server_stats_io_done(write_arg->iov[0].iov_len, write_arg->io_amount,
			     (data->res_WRITE4->status == NFS4_OK) ? true :
			     false, true /*is_write*/);

	if (data->owner != NULL) {
		op_ctx->clientid = NULL;
		dec_state_owner_ref(data->owner);
	}

	if (write_arg->state)
		dec_state_t_ref(write_arg->state);

	return nfsstat4_to_nfs_req_result(data->res_WRITE4->status);
}

enum nfs_req_result nfs4_op_write_resume(struct nfs_argop4 *op,
					 compound_data_t *data,
					 struct nfs_resop4 *resp)
{
	enum nfs_req_result rc = nfs4_complete_write(data->op_data);

	/* NOTE: we do not expect rc == NFS_REQ_ASYNC_WAIT */
	assert(rc != NFS_REQ_ASYNC_WAIT);

	if (rc != NFS_REQ_ASYNC_WAIT) {
		/* We are completely done with the request. This test wasn't
		 * strictly necessary since nfs4_complete_read doesn't async but
		 * at some future time, the getattr it does might go async so we
		 * might as well be prepared here. Our caller is already
		 * prepared for such a scenario.
		 */
		gsh_free(data->op_data);
		data->op_data = NULL;
	}

	return rc;
}

/**
 * @brief Callback for NFS4 write done
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] write_data		Data for write call
 * @param[in] caller_data	Data for caller
 */
static void nfs4_write_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			  void *write_data, void *caller_data)
{
	struct nfs4_write_data *data = caller_data;
	uint32_t flags;

	/* Fixup ERR_FSAL_SHARE_DENIED status */
	if (ret.major == ERR_FSAL_SHARE_DENIED)
		ret = fsalstat(ERR_FSAL_LOCKED, 0);

	/* Get result */
	data->res_WRITE4->status = nfs4_Errno_status(ret);

	flags = atomic_postset_uint32_t_bits(&data->flags, ASYNC_PROC_DONE);

	if ((flags & ASYNC_PROC_EXIT) == ASYNC_PROC_EXIT) {
		/* nfs4_op_write has already exited, we will need to reschedule
		 * the request for completion.
		 */
		svc_resume(data->data->req);
	}
}

/**
 * @brief Write for a data server
 *
 * This function bypasses mdcache and calls directly into the FSAL
 * to perform a pNFS data server write.
 *
 * @param[in]     op    Arguments for nfs41_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs41_op
 *
 * @return per RFC5661, p. 376
 *
 */

static enum nfs_req_result op_dswrite(struct nfs_argop4 *op,
				      compound_data_t *data,
				      struct nfs_resop4 *resp)
{
	WRITE4args * const arg_WRITE4 = &op->nfs_argop4_u.opwrite;
	WRITE4res * const res_WRITE4 = &resp->nfs_resop4_u.opwrite;
	/* NFSv4 return code */
	nfsstat4 nfs_status = 0;

	res_WRITE4->status = op_ctx->ctx_pnfs_ds->s_ops.dsh_write(
				data->current_ds,
				&arg_WRITE4->stateid,
				arg_WRITE4->offset,
				arg_WRITE4->data.data_len,
				arg_WRITE4->data.data_val,
				arg_WRITE4->stable,
				&res_WRITE4->WRITE4res_u.resok4.count,
				&res_WRITE4->WRITE4res_u.resok4.writeverf,
				&res_WRITE4->WRITE4res_u.resok4.committed);

	res_WRITE4->status = nfs_status;
	return nfsstat4_to_nfs_req_result(res_WRITE4->status);
}

/**
 * @brief The NFS4_OP_WRITE operation
 *
 * This functions handles the NFS4_OP_WRITE operation in NFSv4. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661, p. 376
 */

enum nfs_req_result nfs4_op_write(struct nfs_argop4 *op, compound_data_t *data,
				  struct nfs_resop4 *resp)
{
	WRITE4args * const arg_WRITE4 = &op->nfs_argop4_u.opwrite;
	WRITE4res * const res_WRITE4 = &resp->nfs_resop4_u.opwrite;
	uint64_t size = 0;
	uint64_t offset;
	state_t *state_found = NULL;
	state_t *state_open = NULL;
	fsal_status_t fsal_status = {0, 0};
	struct fsal_obj_handle *obj = NULL;
	bool anonymous_started = false;
	struct gsh_buffdesc verf_desc;
	state_owner_t *owner = NULL;
	uint64_t MaxWrite =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxWrite);
	uint64_t MaxOffsetWrite =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxOffsetWrite);
	bool force_sync = op_ctx->export_perms.options & EXPORT_OPTION_COMMIT;
	struct nfs4_write_data *write_data = NULL;
	struct fsal_io_arg *write_arg;
	/* In case we don't call write2, we indicate the I/O as already done
	 * since in that case we should go ahead and exit as expected.
	 */
	uint32_t flags = ASYNC_PROC_DONE;

	/* Lock are not supported */
	resp->resop = NFS4_OP_WRITE;
	res_WRITE4->status = NFS4_OK;

	if ((data->minorversion > 0)
	     && (nfs4_Is_Fh_DSHandle(&data->currentFH))) {
		return op_dswrite(op, data, resp);
	}

	/*
	 * Do basic checks on a filehandle
	 * Only files can be written
	 */
	res_WRITE4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, true);
	if (res_WRITE4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* if quota support is active, then we should check is the FSAL
	   allows inode creation or not */
	fsal_status = op_ctx->fsal_export->exp_ops.check_quota(
						op_ctx->fsal_export,
						CTX_FULLPATH(op_ctx),
						FSAL_QUOTA_BLOCKS);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_WRITE4->status = NFS4ERR_DQUOT;
		return NFS_REQ_ERROR;
	}


	/* vnode to manage is the current one */
	obj = data->current_obj;

	/* Check stateid correctness and get pointer to state
	 * (also checks for special stateids)
	 */
	res_WRITE4->status = nfs4_Check_Stateid(&arg_WRITE4->stateid,
						obj,
						&state_found,
						data,
						STATEID_SPECIAL_ANY,
						0,
						false,
						"WRITE");

	if (res_WRITE4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* NB: After this points, if state_found == NULL, then
	 * the stateid is all-0 or all-1
	 */
	if (state_found != NULL) {
		struct state_deleg *sdeleg;

		switch (state_found->state_type) {
		case STATE_TYPE_SHARE:
			state_open = state_found;
			/* Note this causes an extra refcount, but it
			 * simplifies logic below.
			 */
			inc_state_t_ref(state_open);
			/** @todo FSF: need to check against existing locks */
			break;

		case STATE_TYPE_LOCK:
			state_open = nfs4_State_Get_Pointer(
			    state_found->state_data.lock.openstate_key);

			if (state_open == NULL) {
				res_WRITE4->status = NFS4ERR_BAD_STATEID;
				goto out;
			}

			/**
			 * @todo FSF: should check that write is in range of an
			 * exclusive lock...
			 */
			break;

		case STATE_TYPE_DELEG:
			/* Check if the delegation state allows READ */
			sdeleg = &state_found->state_data.deleg;
			if (!(sdeleg->sd_type & OPEN_DELEGATE_WRITE)) {
				/* Invalid delegation for this operation. */
				LogDebug(COMPONENT_STATE,
					"Delegation type:%d state:%d",
					sdeleg->sd_type,
					sdeleg->sd_state);
				res_WRITE4->status = NFS4ERR_BAD_STATEID;
				goto out;
			}

			state_open = NULL;
			break;

		default:
			res_WRITE4->status = NFS4ERR_BAD_STATEID;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "WRITE with invalid stateid of type %d",
				 (int)state_found->state_type);
			goto out;
		}

		/* This is a write operation, this means that the file
		 * MUST have been opened for writing
		 */
		if (state_open != NULL &&
		    (state_open->state_data.share.share_access &
		     OPEN4_SHARE_ACCESS_WRITE) == 0) {
			/* Bad open mode, return NFS4ERR_OPENMODE */
			res_WRITE4->status = NFS4ERR_OPENMODE;
				if (isDebug(COMPONENT_NFS_V4_LOCK)) {
					char str[LOG_BUFF_LEN] = "\0";
					struct display_buffer dspbuf = {
							sizeof(str), str, str};
					display_stateid(&dspbuf, state_found);
					LogDebug(COMPONENT_NFS_V4_LOCK,
						 "WRITE %s doesn't have OPEN4_SHARE_ACCESS_WRITE",
						 str);
				}
			goto out;
		}
	} else {
		/* Special stateid, no open state, check to see if any
		 * share conflicts
		 */
		state_open = NULL;

		/* Check for delegation conflict. */
		if (state_deleg_conflict(obj, true)) {
			res_WRITE4->status = NFS4ERR_DELAY;
			goto out;
		}

		anonymous_started = true;
	}

	/* Need to permission check the write. */
	fsal_status = obj->obj_ops->test_access(obj, FSAL_WRITE_ACCESS,
					       NULL, NULL, true);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_WRITE4->status = nfs4_Errno_status(fsal_status);
		goto out;
	}

	/* Get the characteristics of the I/O to be made */
	offset = arg_WRITE4->offset;
	size = arg_WRITE4->data.data_len;
	LogFullDebug(COMPONENT_NFS_V4,
		     "offset = %" PRIu64 "  length = %" PRIu64 "  stable = %d",
		     offset, size, arg_WRITE4->stable);

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
			res_WRITE4->status = NFS4ERR_FBIG;
			goto out;
		}
	}

	if (size > MaxWrite) {
		/*
		 * The client asked for too much data, we
		 * must restrict him
		 */
		LogFullDebug(COMPONENT_NFS_V4,
			     "write requested size = %" PRIu64
			     " write allowed size = %" PRIu64,
			     size, MaxWrite);
		size = MaxWrite;
	}

	LogFullDebug(COMPONENT_NFS_V4,
		     "offset = %" PRIu64 " length = %" PRIu64,
		     offset, size);

	/* if size == 0 , no I/O) are actually made and everything is alright */
	if (size == 0) {
		res_WRITE4->WRITE4res_u.resok4.count = 0;
		res_WRITE4->WRITE4res_u.resok4.committed = FILE_SYNC4;

		verf_desc.addr = res_WRITE4->WRITE4res_u.resok4.writeverf;
		verf_desc.len = sizeof(verifier4);
		op_ctx->fsal_export->exp_ops.get_write_verifier(
					op_ctx->fsal_export, &verf_desc);

		res_WRITE4->status = NFS4_OK;
		server_stats_io_done(0, 0, true, true);
		goto out;
	}

	if (!anonymous_started && data->minorversion == 0) {
		owner = get_state_owner_ref(state_found);
		if (owner != NULL) {
			op_ctx->clientid =
				&owner->so_owner.so_nfs4_owner.so_clientid;
		}
	}

	/* Set up args, allocate from heap, iov_len will be 1 */
	write_data = gsh_calloc(1, sizeof(*write_data) + sizeof(struct iovec));
	LogFullDebug(COMPONENT_NFS_V4, "Allocated write_data %p", write_data);
	write_arg = &write_data->write_arg;
	write_arg->info = NULL;
	write_arg->state = state_found;
	write_arg->offset = offset;
	write_arg->iov_count = 1;
	write_arg->iov[0].iov_len = size;
	write_arg->iov[0].iov_base = arg_WRITE4->data.data_val;
	write_arg->io_amount = 0;
	write_arg->fsal_stable = arg_WRITE4->stable != UNSTABLE4 || force_sync;


	write_data->res_WRITE4 = res_WRITE4;
	write_data->owner = owner;
	write_data->data = data;
	write_data->obj = obj;

	data->op_data = write_data;

	/* Do the actual write */
	obj->obj_ops->write2(obj, false, nfs4_write_cb, write_arg, write_data);

	/* Only atomically set the flags if we actually call write2, otherwise
	 * we will have indicated as having been DONE.
	 */
	flags =
	    atomic_postset_uint32_t_bits(&write_data->flags, ASYNC_PROC_EXIT);

 out:

	if (state_open != NULL)
		dec_state_t_ref(state_open);

	if ((flags & ASYNC_PROC_DONE) != ASYNC_PROC_DONE) {
		/* The write was not finished before we got here. When the
		 * write completes, nfs4_write_cb() will have to reschedule the
		 * request for completion. The resume will be resolved by
		 * nfs4_simple_resume() which will free write_data and return
		 * the appropriate return result. We will NOT go async again for
		 * the write op (but could for a subsequent op in the compound).
		 */
		return NFS_REQ_ASYNC_WAIT;
	}

	if (data->op_data != NULL) {
		enum nfs_req_result rc;

		/* We did actually call write2 but it has called back already.
		 * Do stuff to finally wrap up the write.
		 */
		rc = nfs4_complete_write(data->op_data);

		/* NOTE: we do not expect rc == NFS_REQ_ASYNC_WAIT */
		assert(rc != NFS_REQ_ASYNC_WAIT);

		if (rc != NFS_REQ_ASYNC_WAIT && data->op_data != NULL) {
			/* We are completely done with the request. This test
			 * wasn't strictly necessary since nfs4_complete_write
			 * doesn't async but at some future time, maybe it will
			 * do something that does require more async. If it does
			 * might go async so we might as well be prepared here.
			 * Our caller is already prepared for such a scenario.
			 */
			gsh_free(data->op_data);
			data->op_data = NULL;
		}

		return rc;
	}

	return nfsstat4_to_nfs_req_result(res_WRITE4->status);
}				/* nfs4_op_write */

/**
 * @brief Free memory allocated for WRITE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_WRITE operation.
 *
 * @param[in,out] resp nfs4_op results
*
 */
void nfs4_op_write_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}

/**
 * @brief The NFS4_OP_WRITE_SAME operation
 *
 * This functions handles the NFS4_OP_WRITE_SAME operation in NFSv4.2. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 */

enum nfs_req_result nfs4_op_write_same(struct nfs_argop4 *op,
				       compound_data_t *data,
				       struct nfs_resop4 *resp)
{
	WRITE_SAME4res * const res_WSAME = &resp->nfs_resop4_u.opwrite_same;

	resp->resop = NFS4_OP_WRITE_SAME;
	res_WSAME->wpr_status =  NFS4ERR_NOTSUPP;

	return NFS_REQ_ERROR;
}

/**
 * @brief Free memory allocated for WRITE_SAME result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_WRITE_SAME operation.
 *
 * @param[in,out] resp nfs4_op results
*
 */
void nfs4_op_write_same_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
