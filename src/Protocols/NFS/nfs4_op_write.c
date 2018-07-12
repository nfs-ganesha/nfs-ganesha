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
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "fsal_pnfs.h"
#include "server_stats.h"
#include "export_mgr.h"

struct nfs4_write_data {
	WRITE4res *res_WRITE4;		/**< Results for write */
	state_owner_t *owner;		/**< Owner of state */
};

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
	struct fsal_io_arg *write_arg = write_data;
	struct gsh_buffdesc verf_desc;

	/* Fixup ERR_FSAL_SHARE_DENIED status */
	if (ret.major == ERR_FSAL_SHARE_DENIED)
		ret = fsalstat(ERR_FSAL_LOCKED, 0);

	/* Get result */
	data->res_WRITE4->status = nfs4_Errno_status(ret);

	if (FSAL_IS_ERROR(ret)) {
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
}

/**
 * @brief Write for a data server
 *
 * This function bypasses cache_inode and calls directly into the FSAL
 * to perform a pNFS data server write.
 *
 * @param[in]     op    Arguments for nfs41_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs41_op
 *
 * @return per RFC5661, p. 376
 *
 */

static int op_dswrite(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *resp)
{
	WRITE4args * const arg_WRITE4 = &op->nfs_argop4_u.opwrite;
	WRITE4res * const res_WRITE4 = &resp->nfs_resop4_u.opwrite;
	/* NFSv4 return code */
	nfsstat4 nfs_status = 0;

	nfs_status = data->current_ds->dsh_ops.write(
				data->current_ds,
				op_ctx,
				&arg_WRITE4->stateid,
				arg_WRITE4->offset,
				arg_WRITE4->data.data_len,
				arg_WRITE4->data.data_val,
				arg_WRITE4->stable,
				&res_WRITE4->WRITE4res_u.resok4.count,
				&res_WRITE4->WRITE4res_u.resok4.writeverf,
				&res_WRITE4->WRITE4res_u.resok4.committed);

	res_WRITE4->status = nfs_status;
	return res_WRITE4->status;
}

/**
 * @brief Write plus for a data server
 *
 * This function bypasses cache_inode and calls directly into the FSAL
 * to perform a pNFS data server write.
 *
 * @param[in]     op    Arguments for nfs41_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs41_op
 *
 * @return per RFC5661, p. 376
 *
 */

static int op_dswrite_plus(struct nfs_argop4 *op, compound_data_t *data,
			  struct nfs_resop4 *resp, struct io_info *info)
{
	WRITE4args * const arg_WRITE4 = &op->nfs_argop4_u.opwrite;
	WRITE4res * const res_WRITE4 = &resp->nfs_resop4_u.opwrite;
	/* NFSv4 return code */
	nfsstat4 nfs_status = 0;

	if (info->io_content.what == NFS4_CONTENT_DATA)
		nfs_status = data->current_ds->dsh_ops.write(
				data->current_ds,
				op_ctx,
				&arg_WRITE4->stateid,
				arg_WRITE4->offset,
				arg_WRITE4->data.data_len,
				arg_WRITE4->data.data_val,
				arg_WRITE4->stable,
				&res_WRITE4->WRITE4res_u.resok4.count,
				&res_WRITE4->WRITE4res_u.resok4.writeverf,
				&res_WRITE4->WRITE4res_u.resok4.committed);
	else
		nfs_status = data->current_ds->dsh_ops.write_plus(
				data->current_ds,
				op_ctx,
				&arg_WRITE4->stateid,
				arg_WRITE4->offset,
				arg_WRITE4->data.data_len,
				arg_WRITE4->data.data_val,
				arg_WRITE4->stable,
				&res_WRITE4->WRITE4res_u.resok4.count,
				&res_WRITE4->WRITE4res_u.resok4.writeverf,
				&res_WRITE4->WRITE4res_u.resok4.committed,
				info);

	res_WRITE4->status = nfs_status;
	return res_WRITE4->status;
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

static int nfs4_write(struct nfs_argop4 *op, compound_data_t *data,
		     struct nfs_resop4 *resp, fsal_io_direction_t io,
		     struct io_info *info)
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
	struct nfs4_write_data write_data;
	struct fsal_io_arg *write_arg = alloca(sizeof(*write_arg) +
						sizeof(struct iovec));

	/* Lock are not supported */
	resp->resop = NFS4_OP_WRITE;
	res_WRITE4->status = NFS4_OK;

	if ((data->minorversion > 0)
	     && (nfs4_Is_Fh_DSHandle(&data->currentFH))) {
		if (io == FSAL_IO_WRITE)
			return op_dswrite(op, data, resp);
		else
			return op_dswrite_plus(op, data, resp, info);
	}

	/*
	 * Do basic checks on a filehandle
	 * Only files can be written
	 */
	res_WRITE4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, true);
	if (res_WRITE4->status != NFS4_OK)
		return res_WRITE4->status;

	/* if quota support is active, then we should check is the FSAL
	   allows inode creation or not */
	fsal_status = op_ctx->fsal_export->exp_ops.check_quota(
						op_ctx->fsal_export,
						op_ctx->ctx_export->fullpath,
						FSAL_QUOTA_BLOCKS);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_WRITE4->status = NFS4ERR_DQUOT;
		return res_WRITE4->status;
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
		return res_WRITE4->status;

	/* NB: After this points, if state_found == NULL, then
	 * the stateid is all-0 or all-1
	 */
	if (state_found != NULL) {
		struct state_deleg *sdeleg;

		if (info)
			info->io_advise = state_found->state_data.io_advise;
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
			state_open = state_found->state_data.lock.openstate;
			inc_state_t_ref(state_open);
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
				return res_WRITE4->status;
			}

			state_open = NULL;
			break;

		default:
			res_WRITE4->status = NFS4ERR_BAD_STATEID;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "WRITE with invalid stateid of type %d",
				 (int)state_found->state_type);
			return res_WRITE4->status;
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
				 "A client tryed to violate max file size %"
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

		if (info == NULL ||
		    info->io_content.what != NFS4_CONTENT_HOLE) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "write requested size = %" PRIu64
				     " write allowed size = %" PRIu64,
				     size, MaxWrite);
			size = MaxWrite;
		}
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

	/* Set up args */
	write_arg->info = info;
	write_arg->state = state_found;
	write_arg->offset = offset;
	write_arg->iov_count = 1;
	write_arg->iov[0].iov_len = size;
	write_arg->iov[0].iov_base = arg_WRITE4->data.data_val;
	write_arg->io_amount = 0;
	if (arg_WRITE4->stable == UNSTABLE4)
		write_arg->fsal_stable = false;
	else
		write_arg->fsal_stable = true;


	write_data.res_WRITE4 = res_WRITE4;
	write_data.owner = owner;

	/* Do the actual write */
	obj->obj_ops->write2(obj, false, nfs4_write_cb, write_arg, &write_data);

 out:

	if (state_open != NULL)
		dec_state_t_ref(state_open);

	return res_WRITE4->status;
}				/* nfs4_op_write */

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

int nfs4_op_write(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	int err;

	err = nfs4_write(op, data, resp, FSAL_IO_WRITE, NULL);

	return err;
}

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

int nfs4_op_write_same(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	WRITE_SAME4res * const res_WSAME = &resp->nfs_resop4_u.opwrite_plus;

	resp->resop = NFS4_OP_WRITE_SAME;
	res_WSAME->wpr_status =  NFS4ERR_NOTSUPP;

	return res_WSAME->wpr_status;
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
