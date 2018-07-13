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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "fsal_pnfs.h"
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
	LAYOUTCOMMIT4args * const args = &op->nfs_argop4_u.oplayoutcommit;
	/* Convenience alias for response */
	LAYOUTCOMMIT4res * const res_LAYOUTCOMMIT4 =
	    &resp->nfs_resop4_u.oplayoutcommit;
	/* Convenience alias for response */
	LAYOUTCOMMIT4resok *resok =
		&res_LAYOUTCOMMIT4->LAYOUTCOMMIT4res_u.locr_resok4;
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
	if (args->loca_last_write_offset.no_newoffset) {
		arg.new_offset = true;
		arg.last_write =
			args->loca_last_write_offset.newoffset4_u.no_offset;
	} else
		arg.new_offset = false;

	arg.reclaim = args->loca_reclaim;

	xdrmem_create(&lou_body,
		      args->loca_layoutupdate.lou_body.lou_body_val,
		      args->loca_layoutupdate.lou_body.lou_body_len,
		      XDR_DECODE);

	beginning = xdr_getpos(&lou_body);

	/* Suggest a new modification time if we have it */
	if (args->loca_time_modify.nt_timechanged) {
		arg.time_changed = true;
		arg.new_time.seconds =
			args->loca_time_modify.newtime4_u.nt_time.seconds;
		arg.new_time.nseconds =
			args->loca_time_modify.newtime4_u.nt_time.nseconds;
	}

	/* Retrieve state corresponding to supplied ID */

	nfs_status = nfs4_Check_Stateid(&args->loca_stateid,
					data->current_obj,
					&layout_state, data,
					STATEID_SPECIAL_CURRENT,
					0,
					false,
					tag);

	if (nfs_status != NFS4_OK)
		goto out;

	arg.type = layout_state->state_data.layout.state_layout_type;

	PTHREAD_RWLOCK_wrlock(&data->current_obj->state_hdl->state_lock);
	glist_for_each(glist, &layout_state->state_data.layout.state_segments) {
		segment = glist_entry(glist,
				      state_layout_segment_t,
				      sls_state_segments);

		arg.segment = segment->sls_segment;
		arg.fsal_seg_data = segment->sls_fsal_data;

		nfs_status = data->current_obj->obj_ops->layoutcommit(
						data->current_obj,
						op_ctx,
						&lou_body,
						&arg,
						&res);

		if (nfs_status != NFS4_OK) {
			PTHREAD_RWLOCK_unlock(
				&data->current_obj->state_hdl->state_lock);
			goto out;
		}

		if (res.commit_done)
			break;

		/* This really should work in all cases for an
		   in-memory decode stream. */
		xdr_setpos(&lou_body, beginning);
	}

	PTHREAD_RWLOCK_unlock(&data->current_obj->state_hdl->state_lock);

	resok->locr_newsize.ns_sizechanged = res.size_supplied;

	if (res.size_supplied) {
		resok->locr_newsize.newsize4_u.ns_size = res.new_size;
	}

	nfs_status = NFS4_OK;

 out:

	if (layout_state != NULL)
		dec_state_t_ref(layout_state);

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
	/* Nothing to be done */
}
