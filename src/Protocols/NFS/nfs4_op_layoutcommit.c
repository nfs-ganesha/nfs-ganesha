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
 * @file nfs4_op_layoutcommit.c
 * @brief The NFSv4.1 LAYOUTCOMMIT operation.
 */

#include "config.h"
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "fsal_pnfs.h"
#include "sal_data.h"
#include "sal_functions.h"

/**
 *
 * @brief The NFS4_OP_LAYOUTCOMMIT operation
 *
 * This function implements the NFS4_OP_LAYOUTCOMMIT operation.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661 p. 366
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_layoutcommit(struct nfs_argop4 *op, compound_data_t *data,
			 struct nfs_resop4 *resp)
{
	/* Convenience alias for arguments */
	LAYOUTCOMMIT4args * const arg_LAYOUTCOMMIT4 =
	    &op->nfs_argop4_u.oplayoutcommit;
	/* Convenience alias for response */
	LAYOUTCOMMIT4res * const res_LAYOUTCOMMIT4 =
	    &resp->nfs_resop4_u.oplayoutcommit;
	/* Return from cache_inode calls */
	cache_inode_status_t cache_status = 0;
	/* NFS4 return code */
	nfsstat4 nfs_status = 0;
	/* State indicated by client */
	state_t *layout_state = NULL;
	/* Tag for logging in state operations */
	const char *tag = "LAYOUTCOMMIT";
	/* Iterator along linked list */
	struct glist_head *glist = NULL;
	/* Input arguments of FSAL_layoutcommit */
	struct fsal_layoutcommit_arg arg;
	/* Input/output and output arguments of FSAL_layoutcommit */
	struct fsal_layoutcommit_res res;
	/* The segment being traversed */
	state_layout_segment_t *segment;
	/* XDR stream holding the lrf_body opaque */
	XDR lou_body;
	/* The beginning of the stream */
	unsigned int beginning = 0;

	resp->resop = NFS4_OP_LAYOUTCOMMIT;

	if (data->minorversion == 0) {
		res_LAYOUTCOMMIT4->locr_status = NFS4ERR_INVAL;
		return res_LAYOUTCOMMIT4->locr_status;
	}

	nfs_status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (nfs_status != NFS4_OK) {
		res_LAYOUTCOMMIT4->locr_status = nfs_status;
		return res_LAYOUTCOMMIT4->locr_status;
	}


	memset(&arg, 0, sizeof(struct fsal_layoutcommit_arg));
	memset(&res, 0, sizeof(struct fsal_layoutcommit_res));

	/* Suggest a new size, if we have it */
	if (arg_LAYOUTCOMMIT4->loca_last_write_offset.no_newoffset) {
		arg.new_offset = true;
		arg.last_write = arg_LAYOUTCOMMIT4->loca_last_write_offset.
					newoffset4_u.no_offset;
	} else
		arg.new_offset = false;

	arg.reclaim = arg_LAYOUTCOMMIT4->loca_reclaim;

	xdrmem_create(&lou_body,
		      arg_LAYOUTCOMMIT4->loca_layoutupdate.lou_body.
		      lou_body_val,
		      arg_LAYOUTCOMMIT4->loca_layoutupdate.lou_body.
		      lou_body_len, XDR_DECODE);

	beginning = xdr_getpos(&lou_body);

	/* Suggest a new modification time if we have it */
	if (arg_LAYOUTCOMMIT4->loca_time_modify.nt_timechanged) {
		arg.time_changed = true;
		arg.new_time.seconds = arg_LAYOUTCOMMIT4->loca_time_modify.
					newtime4_u.nt_time.seconds;
		arg.new_time.nseconds = arg_LAYOUTCOMMIT4->loca_time_modify.
					newtime4_u.nt_time.nseconds;
	}

	/* Retrieve state corresponding to supplied ID */

	nfs_status = nfs4_Check_Stateid(&arg_LAYOUTCOMMIT4->loca_stateid,
					data->current_entry,
					&layout_state, data,
					STATEID_SPECIAL_CURRENT,
					0,
					false,
					tag);

	if (nfs_status != NFS4_OK)
		goto out;

	arg.type = layout_state->state_data.layout.state_layout_type;

	glist_for_each(glist, &layout_state->state_data.layout.state_segments) {
		segment = glist_entry(glist,
				      state_layout_segment_t,
				      sls_state_segments);

		pthread_mutex_lock(&segment->sls_mutex);
		arg.segment = segment->sls_segment;
		arg.fsal_seg_data = segment->sls_fsal_data;
		pthread_mutex_unlock(&segment->sls_mutex);

		nfs_status = data->current_entry->obj_handle->ops->layoutcommit(
						data->current_entry->obj_handle,
						op_ctx,
						&lou_body,
						&arg,
						&res);

		if (nfs_status != NFS4_OK)
			goto out;

		if (res.commit_done)
			break;

		/* This really should work in all cases for an
		   in-memory decode stream. */
		xdr_setpos(&lou_body, beginning);
	}

	if (arg_LAYOUTCOMMIT4->loca_time_modify.nt_timechanged
	    || arg_LAYOUTCOMMIT4->loca_last_write_offset.no_newoffset
	    || res.size_supplied) {
		cache_status =
		     cache_inode_invalidate(data->current_entry,
					    CACHE_INODE_INVALIDATE_ATTRS);

		if (cache_status != CACHE_INODE_SUCCESS) {
			nfs_status = nfs4_Errno(cache_status);
			goto out;
		}
	}

	res_LAYOUTCOMMIT4->LAYOUTCOMMIT4res_u.locr_resok4.locr_newsize.
		ns_sizechanged = res.size_supplied;

	if (res.size_supplied) {
		(res_LAYOUTCOMMIT4->LAYOUTCOMMIT4res_u.locr_resok4.locr_newsize.
		 newsize4_u.ns_size) = res.new_size;
	}

	nfs_status = NFS4_OK;

 out:

	xdr_destroy(&lou_body);

	res_LAYOUTCOMMIT4->locr_status = nfs_status;

	return res_LAYOUTCOMMIT4->locr_status;
}				/* nfs41_op_layoutcommit */

/**
 * @brief free memory allocated for response of LAYOUTCOMMIT
 *
 * This function frees the memory allocated for response of
 * the NFS4_OP_LAYOUTCOMMIT operation.
 *
 * @param[in,out] resp  Results for nfs4_op
 *
 */
void nfs4_op_layoutcommit_Free(nfs_resop4 *resp)
{
	return;
}				/* nfs41_op_layoutcommit_Free */
