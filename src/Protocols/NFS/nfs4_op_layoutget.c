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

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <stdint.h>
#include "hashtable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "export_mgr.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "fsal.h"
#include "fsal_api.h"
#include "fsal_pnfs.h"
#include "sal_data.h"
#include "sal_functions.h"

/**
 *
 * @brief Get or make a layout state
 *
 * If the stateid supplied by the client refers to a layout state,
 * that state is returned.  Otherwise, if it is a share, lock, or
 * delegation, a new state is created.  Any layout state matching
 * clientid, file, and type is freed.
 *
 * @param[in,out] data Compound    Compound request's data
 * @param[in]     supplied_stateid The stateid included in
 *                                 the arguments to layoutget
 * @param[in]     layout_type      Type of layout being requested
 * @param[out]    layout_state     The layout state
 *
 * @return NFS4_OK if successfull, other values on error
 */

static nfsstat4 acquire_layout_state(compound_data_t *data,
				     stateid4 *supplied_stateid,
				     layouttype4 layout_type,
				     state_t **layout_state, const char *tag)
{
	/* State associated with the client-supplied stateid */
	state_t *supplied_state = NULL;
	/* State owner for per-clientid states */
	state_owner_t *clientid_owner = NULL;
	/* Return from this function */
	nfsstat4 nfs_status = 0;
	/* Return from state functions */
	state_status_t state_status = 0;
	/* Layout state, forgotten about by caller */
	state_t *condemned_state = NULL;
	/* Tracking data for the layout state */
	struct state_refer refer;

	memcpy(refer.session, data->session->session_id, sizeof(sessionid4));
	refer.sequence = data->sequence;
	refer.slot = data->slot;

	state_status = get_clientid_owner(data->session->clientid,
					  &clientid_owner);

	if (state_status != STATE_SUCCESS)
		nfs_status = nfs4_Errno_state(state_status);

	/* Retrieve state corresponding to supplied ID, inspect it
	 * and, if necessary, create a new layout state
	 */
	nfs_status = nfs4_Check_Stateid(supplied_stateid,
					data->current_entry,
					&supplied_state,
					data,
					STATEID_SPECIAL_CURRENT,
					0,
					false,
					tag);

	if (nfs_status != NFS4_OK)
		goto out;

	if (supplied_state->state_type == STATE_TYPE_LAYOUT) {
		/* If the state supplied is a layout state, we can
		 * simply use it */
		*layout_state = supplied_state;
	} else if ((supplied_state->state_type == STATE_TYPE_SHARE)
		   || (supplied_state->state_type == STATE_TYPE_DELEG)
		   || (supplied_state->state_type == STATE_TYPE_LOCK)) {
		/* For share, delegation, and lock states, create a
		   new layout state. */
		state_data_t layout_data;
		memset(&layout_data, 0, sizeof(state_data_t));

		/* See if a layout state already exists */
		state_status =
		    state_lookup_layout_state(data->current_entry,
					      clientid_owner,
					      layout_type,
					      &condemned_state);

		/* If it does, we assume that the client is using the
		 * forgetful model and has forgotten it had any
		 * layouts.  Free all layouts associated with the
		 * state and delete it.
		 */
		if (state_status == STATE_SUCCESS) {
			/* Flag indicating whether all layouts were returned
			 * and the state was deleted
			 */
			bool deleted = false;
			struct pnfs_segment entire = {
				.io_mode = LAYOUTIOMODE4_ANY,
				.offset = 0,
				.length = NFS4_UINT64_MAX
			};

			if (condemned_state->state_data.layout.granting) {
				nfs_status = NFS4ERR_DELAY;
				goto out;
			}

			nfs_status =
			     nfs4_return_one_state(data->current_entry,
						   0,
						   circumstance_forgotten,
						   condemned_state,
						   entire,
						   0,
						   NULL,
						   &deleted,
						   false);

			if (nfs_status != NFS4_OK)
				goto out;

			if (!deleted) {
				nfs_status = NFS4ERR_SERVERFAULT;
				goto out;
			}

			condemned_state = NULL;

		} else if (state_status != STATE_NOT_FOUND) {
			nfs_status = nfs4_Errno_state(state_status);
			goto out;
		}

		layout_data.layout.state_layout_type = layout_type;
		layout_data.layout.state_return_on_close = false;

		state_status = state_add(data->current_entry,
					 STATE_TYPE_LAYOUT,
					 &layout_data,
					 clientid_owner,
					 layout_state,
					 &refer);

		if (state_status != STATE_SUCCESS) {
			nfs_status = nfs4_Errno_state(state_status);
			goto out;
		}

		glist_init(&(*layout_state)->state_data.layout.state_segments);

		/* Attach this open to an export */
		(*layout_state)->state_export = op_ctx->export;
		PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
		glist_add_tail(&op_ctx->export->exp_state_list,
			       &(*layout_state)->state_export_list);
		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
	} else {
		/* A state eixsts but is of an invalid type. */
		nfs_status = NFS4ERR_BAD_STATEID;
		goto out;
	}

 out:

	return nfs_status;
}

/**
 *
 * @brief Free layouts array.
 *
 * @param[in]     layouts    Layouts array
 * @param[in]     numlayouts Size of array
 */

void free_layouts(layout4 *layouts, uint32_t numlayouts)
{
	size_t i;

	for (i = 0; i < numlayouts; i++) {
		if (layouts[i].lo_content.loc_body.loc_body_val)
			gsh_free(layouts[i].lo_content.loc_body.loc_body_val);
	}

	gsh_free(layouts);
}

/**
 *
 * @brief Grant and add one layout segment
 *
 * This is a wrapper around the FSAL call that populates one entry in
 * the logr_layout array and adds one segment to the state list.
 *
 * @param[in]     handle  File handle
 * @param[in]     arg     Input arguments to the FSAL
 * @param[in,out] res     Input/output and output arguments to the FSAL
 * @param[out]    current Current entry in the logr_layout array.
 *
 * @return NFS4_OK if successfull, other values show an error.
 */

static nfsstat4 one_segment(cache_entry_t *entry,
			    state_t *layout_state,
			    const struct fsal_layoutget_arg *arg,
			    struct fsal_layoutget_res *res, layout4 *current)
{
	/* The initial position of the XDR stream after creation, so we
	   can find the total length of encoded data. */
	size_t start_position = 0;
	/* XDR stream to encode loc_body of the current segment */
	XDR loc_body;
	/* Return code from this function */
	nfsstat4 nfs_status = 0;
	/* Return from state calls */
	state_status_t state_status = 0;
	/* Size of a loc_body buffer */
	size_t loc_body_size = MIN(
	    op_ctx->fsal_export->ops->fs_loc_body_size(op_ctx->fsal_export),
	    arg->maxcount);

	if (loc_body_size == 0) {
		LogCrit(COMPONENT_PNFS,
			"The FSAL must specify a non-zero loc_body_size.");
		nfs_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	/* Initialize the layout_content4 structure, allocate a buffer,
	   and create an XDR stream for the FSAL to encode to. */

	current->lo_content.loc_type = arg->type;
	current->lo_content.loc_body.loc_body_val = gsh_malloc(loc_body_size);

	xdrmem_create(&loc_body,
		      current->lo_content.loc_body.loc_body_val,
		      loc_body_size, XDR_ENCODE);

	start_position = xdr_getpos(&loc_body);

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	++layout_state->state_data.layout.granting;
	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	nfs_status = entry->obj_handle->ops->layoutget(entry->obj_handle,
						      op_ctx,
						      &loc_body,
						      arg,
						      res);

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	--layout_state->state_data.layout.granting;

	current->lo_content.loc_body.loc_body_len =
	    xdr_getpos(&loc_body) - start_position;

	xdr_destroy(&loc_body);

	if (nfs_status != NFS4_OK) {
		PTHREAD_RWLOCK_unlock(&entry->state_lock);
		goto out;
	}

	current->lo_offset = res->segment.offset;
	current->lo_length = res->segment.length;
	current->lo_iomode = res->segment.io_mode;

	state_status = state_add_segment(layout_state,
					 &res->segment,
					 res->fsal_seg_data,
					 res->return_on_close);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	if (state_status != STATE_SUCCESS) {
		nfs_status = nfs4_Errno_state(state_status);
		goto out;
	}

	/**
	 * @todo This is where you would want to record layoutget
	 * operation.  You can get the details of every segment added
	 * here, including the segment description in
	 * res->fsal_seg_data and clientid in *req_ctx->clientid.
	 */

 out:

	if (nfs_status != NFS4_OK) {
		if (current->lo_content.loc_body.loc_body_val)
			gsh_free(current->lo_content.loc_body.loc_body_val);
	}

	return nfs_status;
}

/**
 * @brief The NFS4_OP_LAYOUTGET operation
 *
 * This function implements the NFS4_OP_LAYOUTGET operation.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661 pp. 366-7
 *
 * @see nfs4_Compound
 */

int nfs4_op_layoutget(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *resp)
{
	/* Convenience alias for arguments */
	LAYOUTGET4args * const arg_LAYOUTGET4 = &op->nfs_argop4_u.oplayoutget;
	/* Convenience alias for response */
	LAYOUTGET4res * const res_LAYOUTGET4 = &resp->nfs_resop4_u.oplayoutget;
	/* NFSv4.1 status code */
	nfsstat4 nfs_status = 0;
	/* Pointer to state governing layouts */
	state_t *layout_state = NULL;
	/* Pointer to the array of layouts */
	layout4 *layouts = NULL;
	/* Total number of layout segments returned by the FSAL */
	uint32_t numlayouts = 0;
	/* Tag supplied to SAL functions for debugging messages */
	const char *tag = "LAYOUTGET";
	/* Input arguments of layoutget */
	struct fsal_layoutget_arg arg;
	/* Input/output and output arguments of layoutget */
	struct fsal_layoutget_res res;
	/* Maximum number of segments this FSAL will ever return for a
	   single LAYOUTGET */
	int max_segment_count = 0;

	resp->resop = NFS4_OP_LAYOUTGET;

	if (data->minorversion == 0) {
		res_LAYOUTGET4->logr_status = NFS4ERR_INVAL;
		return res_LAYOUTGET4->logr_status;
	}

	nfs_status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (nfs_status != NFS4_OK)
		goto out;

	/* max_segment_count is also an indication of if fsal supports pnfs */
	max_segment_count = op_ctx->fsal_export->ops->
			fs_maximum_segments(op_ctx->fsal_export);

	if (max_segment_count == 0) {
		LogWarn(COMPONENT_PNFS,
			"The FSAL must specify a non-zero "
			"fs_maximum_segments.");
		nfs_status = NFS4ERR_LAYOUTUNAVAILABLE;
		goto out;
	}

	nfs_status = acquire_layout_state(data,
					  &arg_LAYOUTGET4->loga_stateid,
					  arg_LAYOUTGET4->loga_layout_type,
					  &layout_state,
					  tag);

	if (nfs_status != NFS4_OK)
		goto out;

	/*
	 * Blank out argument structures and get the filehandle.
	 */

	memset(&arg, 0, sizeof(struct fsal_layoutget_arg));
	memset(&res, 0, sizeof(struct fsal_layoutget_res));

	/*
	 * Initialize segment array and fill out input-only arguments
	 */
	layouts = gsh_calloc(max_segment_count, sizeof(layout4));

	if (layouts == NULL) {
		nfs_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	arg.type = arg_LAYOUTGET4->loga_layout_type;
	arg.minlength = arg_LAYOUTGET4->loga_minlength;
	arg.export_id = op_ctx->export->export_id;
	arg.maxcount = arg_LAYOUTGET4->loga_maxcount;

	/* Guaranteed on the first call */
	res.context = NULL;

	/**
	 * @todo ACE: Currently we have no callbacks, so it makes no
	 * sense to pass the client-supplied value to the FSAL.  When
	 * we get callbacks, it will.
	 */
	res.signal_available = false;

	do {
		/* Since the FSAL writes to tis structure with every
		   call, we re-initialize it with the operation's
		   arguments */
		res.segment.io_mode = arg_LAYOUTGET4->loga_iomode;
		res.segment.offset = arg_LAYOUTGET4->loga_offset;
		res.segment.length = arg_LAYOUTGET4->loga_length;

		/* Clear anything from a previous segment */
		res.fsal_seg_data = NULL;

		nfs_status = one_segment(data->current_entry,
					 layout_state,
					 &arg,
					 &res,
					 layouts + numlayouts);

		if (nfs_status != NFS4_OK)
			goto out;

		arg.maxcount -=
		    layouts[numlayouts].lo_content.loc_body.loc_body_len;
		numlayouts++;

		if ((numlayouts == max_segment_count) && !res.last_segment) {
			nfs_status = NFS4ERR_SERVERFAULT;
			goto out;
		}
	} while (!res.last_segment);

	/* Update stateid.seqid and copy to current */
	update_stateid(layout_state,
		       &res_LAYOUTGET4->LAYOUTGET4res_u.logr_resok4.
		       logr_stateid, data, tag);

	res_LAYOUTGET4->LAYOUTGET4res_u.logr_resok4.logr_return_on_close =
	    layout_state->state_data.layout.state_return_on_close;

	/* Now the layout specific information */
	res_LAYOUTGET4->LAYOUTGET4res_u.logr_resok4.logr_layout.
	    logr_layout_len = numlayouts;
	res_LAYOUTGET4->LAYOUTGET4res_u.logr_resok4.logr_layout.
	    logr_layout_val = layouts;

	nfs_status = NFS4_OK;

 out:

	if (res_LAYOUTGET4->logr_status != NFS4_OK || nfs_status != NFS4_OK) {
		if (layouts != NULL)
			free_layouts(layouts, numlayouts);

		if ((layout_state) && (layout_state->state_seqid == 0)) {
			state_del(layout_state, false);
			layout_state = NULL;
		}

		/* Poison the current stateid */
		data->current_stateid_valid = false;
	}

	res_LAYOUTGET4->logr_status = nfs_status;

	return res_LAYOUTGET4->logr_status;
}				/* nfs4_op_layoutget */

/**
 * @brief Free memory allocated for LAYOUTGET result
 *
 * This function frees any memory allocated for the NFS4_OP_LAYOUTGET
 * result.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_layoutget_Free(nfs_resop4 *res)
{
	LAYOUTGET4res *resp = &res->nfs_resop4_u.oplayoutget;

	if (resp->logr_status == NFS4_OK)
		free_layouts(resp->LAYOUTGET4res_u.logr_resok4.
					logr_layout.logr_layout_val,
			     resp->LAYOUTGET4res_u.logr_resok4.logr_layout.
					logr_layout_len);

}				/* nfs41_op_layoutget_Free */
