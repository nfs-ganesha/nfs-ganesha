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
 * @file    nfs4_op_read.c
 * @brief   NFSv4 read operation
 *
 * This file implements NFS4_OP_READ within an NFSv4 compound call.
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
#include "nfs_convert.h"
#include <stdlib.h>
#include <unistd.h>
#include "fsal_pnfs.h"
#include "server_stats.h"
#include "export_mgr.h"

/**
 * @brief Read on a pNFS pNFS data server
 *
 * This function bypasses cache_inode and calls directly into the FSAL
 * to perform a data-server read.
 *
 * @param[in]     op   Arguments for nfs41_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs41_op
 *
 * @return per RFC5661, p. 371
 *
 */

static int op_dsread(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	READ4args * const arg_READ4 = &op->nfs_argop4_u.opread;
	READ4res * const res_READ4 = &resp->nfs_resop4_u.opread;
	/* NFSv4 return code */
	nfsstat4 nfs_status = 0;
	/* Buffer into which data is to be read */
	void *buffer = NULL;
	/* End of file flag */
	bool eof = false;

	/* Don't bother calling the FSAL if the read length is 0. */

	if (arg_READ4->count == 0) {
		res_READ4->READ4res_u.resok4.eof = FALSE;
		res_READ4->READ4res_u.resok4.data.data_len = 0;
		res_READ4->READ4res_u.resok4.data.data_val = NULL;
		res_READ4->status = NFS4_OK;
		return res_READ4->status;
	}

	/* Construct the FSAL file handle */

	buffer = gsh_malloc_aligned(4096, arg_READ4->count);
	if (buffer == NULL) {
		LogEvent(COMPONENT_NFS_V4, "FAILED to allocate read buffer");
		res_READ4->status = NFS4ERR_SERVERFAULT;
		return res_READ4->status;
	}

	res_READ4->READ4res_u.resok4.data.data_val = buffer;

	nfs_status = data->current_ds->dsh_ops.read(
				data->current_ds,
				op_ctx,
				&arg_READ4->stateid,
				arg_READ4->offset,
				arg_READ4->count,
				res_READ4->READ4res_u.resok4.data.data_val,
				&res_READ4->READ4res_u.resok4.data.data_len,
				&eof);

	if (nfs_status != NFS4_OK) {
		gsh_free(buffer);
		res_READ4->READ4res_u.resok4.data.data_val = NULL;
	}

	if (eof)
		res_READ4->READ4res_u.resok4.eof = TRUE;
	else
		res_READ4->READ4res_u.resok4.eof = FALSE;

	res_READ4->status = nfs_status;

	return res_READ4->status;
}

/**
 * @brief Read on a pNFS pNFS data server
 *
 * This function bypasses cache_inode and calls directly into the FSAL
 * to perform a data-server read.
 *
 * @param[in]     op   Arguments for nfs41_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs41_op
 *
 * @return per RFC5661, p. 371
 *
 */

static int op_dsread_plus(struct nfs_argop4 *op, compound_data_t *data,
			 struct nfs_resop4 *resp, struct io_info *info)
{
	READ4args * const arg_READ4 = &op->nfs_argop4_u.opread;
	READ_PLUS4res * const res_RPLUS = &resp->nfs_resop4_u.opread_plus;
	contents *contentp = &res_RPLUS->rpr_resok4.rpr_contents;
	/* NFSv4 return code */
	nfsstat4 nfs_status = 0;
	/* Buffer into which data is to be read */
	void *buffer = NULL;
	/* End of file flag */
	bool eof = false;

	/* Don't bother calling the FSAL if the read length is 0. */

	if (arg_READ4->count == 0) {
		res_RPLUS->rpr_resok4.rpr_contents_count = 1;
		res_RPLUS->rpr_resok4.rpr_eof = FALSE;
		contentp->what = NFS4_CONTENT_DATA;
		contentp->data.d_offset = arg_READ4->offset;
		contentp->data.d_allocated = FALSE;
		contentp->data.d_data.data_len =  0;
		contentp->data.d_data.data_val = NULL;
		res_RPLUS->rpr_status = NFS4_OK;
		return res_RPLUS->rpr_status;
	}

	/* Construct the FSAL file handle */

	buffer = gsh_malloc_aligned(4096, arg_READ4->count);
	if (buffer == NULL) {
		LogEvent(COMPONENT_NFS_V4, "FAILED to allocate read buffer");
		res_RPLUS->rpr_status = NFS4ERR_SERVERFAULT;
		return res_RPLUS->rpr_status;
	}

	nfs_status = data->current_ds->dsh_ops.read_plus(
				data->current_ds,
				op_ctx,
				&arg_READ4->stateid,
				arg_READ4->offset,
				arg_READ4->count,
				buffer,
				arg_READ4->count,
				&eof, info);

	res_RPLUS->rpr_status = nfs_status;
	if (nfs_status != NFS4_OK) {
		gsh_free(buffer);
		return res_RPLUS->rpr_status;
	}

	contentp->what = info->io_content.what;
	res_RPLUS->rpr_resok4.rpr_contents_count = 1;
	res_RPLUS->rpr_resok4.rpr_eof = eof;

	if (info->io_content.what == NFS4_CONTENT_HOLE) {
		contentp->hole.di_offset = info->io_content.hole.di_offset;
		contentp->hole.di_length = info->io_content.hole.di_length;
	}
	if (info->io_content.what == NFS4_CONTENT_DATA) {
		contentp->data.d_offset = info->io_content.data.d_offset;
		contentp->data.d_allocated = info->io_content.data.d_allocated;
		contentp->data.d_data.data_len =
					info->io_content.data.d_data.data_len;
		contentp->data.d_data.data_val =
					info->io_content.data.d_data.data_val;
	}
	return res_RPLUS->rpr_status;
}


static int nfs4_read(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp, cache_inode_io_direction_t io,
		    struct io_info *info)
{
	READ4args * const arg_READ4 = &op->nfs_argop4_u.opread;
	READ4res * const res_READ4 = &resp->nfs_resop4_u.opread;
	uint64_t size = 0;
	size_t read_size = 0;
	uint64_t offset = 0;
	bool eof_met = false;
	void *bufferdata = NULL;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	state_t *state_found = NULL;
	state_t *state_open = NULL;
	uint64_t file_size = 0;
	cache_entry_t *entry = NULL;
	bool sync = false;
	bool anonymous_started = false;

	/* Say we are managing NFS4_OP_READ */
	resp->resop = NFS4_OP_READ;
	res_READ4->status = NFS4_OK;

	/* Do basic checks on a filehandle Only files can be read */

	if ((data->minorversion > 0)
	    && nfs4_Is_Fh_DSHandle(&data->currentFH)) {
		if (io == CACHE_INODE_READ)
			return op_dsread(op, data, resp);
		else
			return op_dsread_plus(op, data, resp, info);
	}

	res_READ4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, true);
	if (res_READ4->status != NFS4_OK)
		return res_READ4->status;

	entry = data->current_entry;
	/* Check stateid correctness and get pointer to state (also
	   checks for special stateids) */

	res_READ4->status =
	    nfs4_Check_Stateid(&arg_READ4->stateid, entry, &state_found, data,
			       STATEID_SPECIAL_ANY, 0, false, "READ");
	if (res_READ4->status != NFS4_OK)
		return res_READ4->status;

	/* NB: After this point, if state_found == NULL, then the
	   stateid is all-0 or all-1 */

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
			/**
			 * @todo FSF: need to check against existing locks
			 */
			break;

		case STATE_TYPE_LOCK:
			state_open = state_found->state_data.lock.openstate;
			inc_state_t_ref(state_open);
			/**
			 * @todo FSF: should check that write is in
			 * range of an byte range lock...
			 */
			break;

		case STATE_TYPE_DELEG:
			/* Check if the delegation state allows READ */
			sdeleg = &state_found->state_data.deleg;
			if (!(sdeleg->sd_type & OPEN_DELEGATE_READ) ||
				(sdeleg->sd_state != DELEG_GRANTED)) {
				/* Invalid delegation for this operation. */
				LogDebug(COMPONENT_STATE,
					"Delegation type:%d state:%d",
					sdeleg->sd_type,
					sdeleg->sd_state);
				res_READ4->status = NFS4ERR_BAD_STATEID;
				goto out;
			}

			state_open = NULL;
			break;

		default:
			res_READ4->status = NFS4ERR_BAD_STATEID;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "READ with invalid statid of type %d",
				 state_found->state_type);
			goto out;
		}

		/* This is a read operation, this means that the file
		   MUST have been opened for reading */
		if (state_open != NULL
		    && (state_open->state_data.share.
			share_access & OPEN4_SHARE_ACCESS_READ) == 0) {
			/* Even if file is open for write, the client
			   may do accidently read operation (caching).
			   Because of this, READ is allowed if not
			   explicitely denied.  See page 72 in RFC3530
			   for more details */

			if (state_open->state_data.share.
			    share_deny & OPEN4_SHARE_DENY_READ) {
				/* Bad open mode, return NFS4ERR_OPENMODE */
				res_READ4->status = NFS4ERR_OPENMODE;
				LogDebug(COMPONENT_NFS_V4_LOCK,
					 "READ state %p doesn't have "
					 "OPEN4_SHARE_ACCESS_READ",
					 state_found);
				goto out;
			}
		}

		/**
		 * @todo : this piece of code looks a bit suspicious
		 *  (see Rong's mail)
		 *
		 * @todo: ACE: This works for now.  How do we want to
		 * handle owner confirmation across NFSv4.0/NFSv4.1?
		 * Do we want to mark every NFSv4.1 owner
		 * pre-confirmed, or make the check conditional on
		 * minorversion like we do here?
		 */
		switch (state_found->state_type) {
		case STATE_TYPE_SHARE:
			if ((data->minorversion == 0)
			    &&
			    (!(state_found->state_owner->so_owner.so_nfs4_owner.
			       so_confirmed))) {
				res_READ4->status = NFS4ERR_BAD_STATEID;
				goto out;
			}
			break;
		case STATE_TYPE_LOCK:
		case STATE_TYPE_DELEG:
			break;
		default:
			/* Sanity check: all other types are illegal.
			 * we should not got that place (similar check
			 * above), anyway it costs nothing to add this
			 * test */
			res_READ4->status = NFS4ERR_BAD_STATEID;
			goto out;
			break;
		}
	} else {
		/* Special stateid, no open state, check to see if any
		   share conflicts */
		state_open = NULL;

		/* Special stateid, no open state, check to see if any share
		   conflicts The stateid is all-0 or all-1 */
		res_READ4->status = nfs4_Errno_state(
				state_share_anonymous_io_start(
					entry,
					OPEN4_SHARE_ACCESS_READ,
					arg_READ4->stateid.seqid != 0
						? SHARE_BYPASS_READ
						: SHARE_BYPASS_NONE));

		if (res_READ4->status != NFS4_OK)
			goto out;

		anonymous_started = true;
	}

	/** @todo this is racy, use cache_inode_lock_trust_attrs and
	 *        cache_inode_access_no_mutex
	 */
	if (state_open == NULL &&
	    entry->obj_handle->attributes.owner !=
	    op_ctx->creds->caller_uid) {
		/* Need to permission check the read. */
		cache_status =
		    cache_inode_access(entry, FSAL_READ_ACCESS);

		if (cache_status == CACHE_INODE_FSAL_EACCESS) {
			/* Test for execute permission */
			cache_status =
			    cache_inode_access(entry,
					       FSAL_MODE_MASK_SET(FSAL_X_OK) |
					       FSAL_ACE4_MASK_SET
					       (FSAL_ACE_PERM_EXECUTE));
		}

		if (cache_status != CACHE_INODE_SUCCESS) {
			res_READ4->status = nfs4_Errno(cache_status);
			goto done;
		}
	}

	/* Get the size and offset of the read operation */
	offset = arg_READ4->offset;
	size = arg_READ4->count;

	if (op_ctx->export->MaxOffsetRead < UINT64_MAX) {
		LogFullDebug(COMPONENT_NFS_V4,
			     "Read offset=%" PRIu64 " count=%zd "
			     "MaxOffSet=%" PRIu64, offset, size,
			     op_ctx->export->MaxOffsetRead);

		if ((offset + size) > op_ctx->export->MaxOffsetRead) {
			LogEvent(COMPONENT_NFS_V4,
				 "A client tryed to violate max "
				 "file size %" PRIu64 " for exportid #%hu",
				 op_ctx->export->MaxOffsetRead,
				 op_ctx->export->export_id);

			res_READ4->status = NFS4ERR_DQUOT;
			goto done;
		}
	}

	if (size > op_ctx->export->MaxRead) {
		/* the client asked for too much data, this should normally
		   not happen because client will get FATTR4_MAXREAD value
		   at mount time */

		if (info == NULL ||
		    info->io_content.what != NFS4_CONTENT_HOLE) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "read requested size = %"PRIu64
				     " read allowed size = %" PRIu64,
				     size, op_ctx->export->MaxRead);
			size = op_ctx->export->MaxRead;
		}
	}

	/* If size == 0, no I/O is to be made and everything is
	   alright */
	if (size == 0) {
		/* A size = 0 can not lead to EOF */
		res_READ4->READ4res_u.resok4.eof = false;
		res_READ4->READ4res_u.resok4.data.data_len = 0;
		res_READ4->READ4res_u.resok4.data.data_val = NULL;
		res_READ4->status = NFS4_OK;
		goto done;
	}

	/* Some work is to be done */
	bufferdata = gsh_malloc_aligned(4096, size);

	if (bufferdata == NULL) {
		LogEvent(COMPONENT_NFS_V4, "FAILED to allocate bufferdata");
		res_READ4->status = NFS4ERR_SERVERFAULT;
		goto done;
	}

	if (!anonymous_started && data->minorversion == 0) {
		op_ctx->clientid =
		    &state_found->state_owner->so_owner.so_nfs4_owner.
		    so_clientid;
	}

	cache_status =
	    cache_inode_rdwr_plus(entry, io, offset, size, &read_size,
				  bufferdata, &eof_met, &sync, info);
	if (cache_status != CACHE_INODE_SUCCESS) {
		res_READ4->status = nfs4_Errno(cache_status);
		gsh_free(bufferdata);
		res_READ4->READ4res_u.resok4.data.data_val = NULL;
		goto done;
	}

	if (cache_inode_size(entry, &file_size) !=
	    CACHE_INODE_SUCCESS) {
		res_READ4->status = nfs4_Errno(cache_status);
		gsh_free(bufferdata);
		res_READ4->READ4res_u.resok4.data.data_val = NULL;
		goto done;
	}

	if (!anonymous_started && data->minorversion == 0)
		op_ctx->clientid = NULL;

	res_READ4->READ4res_u.resok4.data.data_len = read_size;
	res_READ4->READ4res_u.resok4.data.data_val = bufferdata;

	LogFullDebug(COMPONENT_NFS_V4,
		     "NFS4_OP_READ: offset = %" PRIu64
		     " read length = %zu eof=%u", offset, read_size, eof_met);

	/* Is EOF met or not ? */
	res_READ4->READ4res_u.resok4.eof = (eof_met
					    || ((offset + read_size) >=
						file_size));

	/* Say it is ok */
	res_READ4->status = NFS4_OK;

 done:

	if (anonymous_started)
		state_share_anonymous_io_done(entry, OPEN4_SHARE_ACCESS_READ);

	server_stats_io_done(size, read_size,
			     (res_READ4->status == NFS4_OK) ? true : false,
			     false);

 out:

	if (state_found != NULL)
		dec_state_t_ref(state_found);

	if (state_open != NULL)
		dec_state_t_ref(state_open);

	return res_READ4->status;
}				/* nfs4_op_read */

/**
 * @brief The NFS4_OP_READ operation
 *
 * This functions handles the READ operation in NFSv4.0 This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    The nfs4_op arguments
 * @param[in,out] data  The compound request's data
 * @param[out]    resp  The nfs4_op results
 *
 * @return Errors as specified by RFC3550 RFC5661 p. 371.
 */

int nfs4_op_read(struct nfs_argop4 *op, compound_data_t *data,
		 struct nfs_resop4 *resp)
{
	int err;

	err = nfs4_read(op, data, resp, CACHE_INODE_READ, NULL);

	return err;
}

/**
 * @brief Free data allocated for READ result.
 *
 * This function frees any data allocated for the result of the
 * NFS4_OP_READ operation.
 *
 * @param[in,out] resp  Results fo nfs4_op
 *
 */
void nfs4_op_read_Free(nfs_resop4 *res)
{
	READ4res *resp = &res->nfs_resop4_u.opread;

	if (resp->status == NFS4_OK)
		if (resp->READ4res_u.resok4.data.data_val != NULL)
			gsh_free(resp->READ4res_u.resok4.data.data_val);
	return;
}				/* nfs4_op_read_Free */

/**
 * @brief The NFS4_OP_READ_PLUS operation
 *
 * This functions handles the READ_PLUS operation in NFSv4.2 This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    The nfs4_op arguments
 * @param[in,out] data  The compound request's data
 * @param[out]    resp  The nfs4_op results
 *
 * @return Errors as specified by RFC3550 RFC5661 p. 371.
 */

int nfs4_op_read_plus(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *resp)
{
	struct nfs_resop4 res;
	struct io_info info;
	READ_PLUS4res * const res_RPLUS = &resp->nfs_resop4_u.opread_plus;
	READ4res *res_READ4 = &res.nfs_resop4_u.opread;
	contents *contentp = &res_RPLUS->rpr_resok4.rpr_contents;
	resp->resop = NFS4_OP_READ_PLUS;

	nfs4_read(op, data, &res, CACHE_INODE_READ_PLUS, &info);

	res_RPLUS->rpr_status = res_READ4->status;
	if (res_RPLUS->rpr_status != NFS4_OK)
		return res_RPLUS->rpr_status;

	contentp->what = info.io_content.what;
	res_RPLUS->rpr_resok4.rpr_contents_count = 1;
	res_RPLUS->rpr_resok4.rpr_eof =
			res_READ4->READ4res_u.resok4.eof;

	if (info.io_content.what == NFS4_CONTENT_HOLE) {
		contentp->hole.di_offset = info.io_content.hole.di_offset;
		contentp->hole.di_length = info.io_content.hole.di_length;
	}
	if (info.io_content.what == NFS4_CONTENT_DATA) {
		contentp->data.d_offset = info.io_content.data.d_offset;
		contentp->data.d_allocated = info.io_content.data.d_allocated;
		contentp->data.d_data.data_len =
					info.io_content.data.d_data.data_len;
		contentp->data.d_data.data_val =
					info.io_content.data.d_data.data_val;
	}
	return res_RPLUS->rpr_status;
}

void nfs4_op_read_plus_Free(nfs_resop4 *res)
{
	READ_PLUS4res *resp = &res->nfs_resop4_u.opread_plus;
	contents *conp = &resp->rpr_resok4.rpr_contents;

	if (resp->rpr_status == NFS4_OK && conp->what == NFS4_CONTENT_DATA)
		if (conp->data.d_data.data_val != NULL)
			gsh_free(conp->data.d_data.data_val);

	return;
}				/* nfs4_op_read_Free */

/**
 * @brief The NFS4_OP_IO_ADVISE operation
 *
 * This functions handles the IO_ADVISE operation in NFSv4.2 This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    The nfs4_op arguments
 * @param[out]    resp  The nfs4_op results
 *
 * @return Errors as specified by RFC3550 RFC5661 p. 371.
 */

int nfs4_op_io_advise(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *resp)
{
	IO_ADVISE4args * const arg_IO_ADVISE = &op->nfs_argop4_u.opio_advise;
	IO_ADVISE4res * const res_IO_ADVISE = &resp->nfs_resop4_u.opio_advise;
	fsal_status_t fsal_status = { 0, 0 };
	struct io_hints hints;
	state_t *state_found = NULL;
	cache_entry_t *entry = NULL;

	/* Say we are managing NFS4_OP_IO_ADVISE */
	resp->resop = NFS4_OP_IO_ADVISE;
	res_IO_ADVISE->iaa_status = NFS4_OK;

	hints.hints = 0;
	hints.offset = 0;
	hints.count = 0;

	if (data->minorversion < 2) {
		res_IO_ADVISE->iaa_status = NFS4ERR_NOTSUPP;
		goto done;
	}

	/* Do basic checks on a filehandle Only files can be set */

	res_IO_ADVISE->iaa_status = nfs4_sanity_check_FH(data, REGULAR_FILE,
							 true);
	if (res_IO_ADVISE->iaa_status != NFS4_OK)
		goto done;

	entry = data->current_entry;
	/* Check stateid correctness and get pointer to state (also
	   checks for special stateids) */

	res_IO_ADVISE->iaa_status =
	    nfs4_Check_Stateid(&arg_IO_ADVISE->iaa_stateid, entry,
				&state_found, data,  STATEID_SPECIAL_ANY,
				0, false, "IO_ADVISE");
	if (res_IO_ADVISE->iaa_status != NFS4_OK)
		goto done;

	if (state_found && entry) {
		hints.hints = arg_IO_ADVISE->iaa_hints.map[0];
		hints.offset = arg_IO_ADVISE->iaa_offset;
		hints.count = arg_IO_ADVISE->iaa_count;

		fsal_status = entry->obj_handle->obj_ops.io_advise(
					entry->obj_handle,
					&hints);
		if (FSAL_IS_ERROR(fsal_status)) {
			res_IO_ADVISE->iaa_status = NFS4ERR_NOTSUPP;
			goto done;
		}
		/* save hints to use with other operations */
		state_found->state_data.io_advise = hints.hints;

		res_IO_ADVISE->iaa_status = NFS4_OK;
		res_IO_ADVISE->iaa_hints.bitmap4_len = 1;
		res_IO_ADVISE->iaa_hints.map[0] = hints.hints;
	}
done:
	LogDebug(COMPONENT_NFS_V4,
		 "Status  %s hints 0x%X offset %ld count %ld ",
		 nfsstat4_to_str(res_IO_ADVISE->iaa_status),
		 hints.hints, hints.offset, hints.count);

	if (state_found != NULL)
		dec_state_t_ref(state_found);

	return res_IO_ADVISE->iaa_status;
}				/* nfs4_op_io_advise */

/**
 * @brief Free memory allocated for IO_ADVISE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_IO_ADVISE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_io_advise_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_io_advise_Free */


int nfs4_op_seek(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	SEEK4args * const arg_SEEK = &op->nfs_argop4_u.opseek;
	SEEK4res * const res_SEEK = &resp->nfs_resop4_u.opseek;
	fsal_status_t fsal_status = { 0, 0 };
	state_t *state_found = NULL;
	cache_entry_t *entry = NULL;
	struct io_info info;

	/* Say we are managing NFS4_OP_SEEK */
	resp->resop = NFS4_OP_SEEK;

	if (data->minorversion < 2) {
		res_SEEK->sr_status = NFS4ERR_NOTSUPP;
		goto done;
	}
	res_SEEK->sr_status = NFS4_OK;

	/* Do basic checks on a filehandle Only files can be set */
	res_SEEK->sr_status = nfs4_sanity_check_FH(data, REGULAR_FILE, true);
	if (res_SEEK->sr_status != NFS4_OK)
		goto done;

	entry = data->current_entry;
	/* Check stateid correctness and get pointer to state (also
	   checks for special stateids) */

	res_SEEK->sr_status =
	    nfs4_Check_Stateid(&arg_SEEK->sa_stateid, entry,
				&state_found, data,  STATEID_SPECIAL_ANY,
				0, false, "SEEK");
	if (res_SEEK->sr_status != NFS4_OK)
		goto done;

	if (state_found != NULL) {
		info.io_advise = state_found->state_data.io_advise;
		info.io_content.what = arg_SEEK->sa_what;

		if (arg_SEEK->sa_what == NFS4_CONTENT_DATA ||
				arg_SEEK->sa_what == NFS4_CONTENT_HOLE) {
			info.io_content.hole.di_offset = arg_SEEK->sa_offset;
}
		else
			info.io_content.adb.adb_offset = arg_SEEK->sa_offset;

		fsal_status = entry->obj_handle->obj_ops.seek(
					entry->obj_handle,
					&info);
		if (FSAL_IS_ERROR(fsal_status)) {
			res_SEEK->sr_status = NFS4ERR_NXIO;
			goto done;
		}
		res_SEEK->sr_resok4.sr_eof = info.io_eof;
		res_SEEK->sr_resok4.sr_offset = info.io_content.hole.di_offset;
	}
done:
	LogDebug(COMPONENT_NFS_V4,
		 "Status  %s type %d offset %ld ",
		 nfsstat4_to_str(res_SEEK->sr_status), arg_SEEK->sa_what,
		 arg_SEEK->sa_offset);

	if (state_found != NULL)
		dec_state_t_ref(state_found);

	return res_SEEK->sr_status;
}

