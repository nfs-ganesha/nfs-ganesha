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
 * @file    nfs4_op_layoutreturn.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include "hashtable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "fsal.h"
#include "pnfs_utils.h"
#include "sal_data.h"
#include "sal_functions.h"

/**
 *
 * @brief The NFS4_OP_LAYOUTRETURN operation.
 *
 * This function implements the NFS4_OP_LAYOUTRETURN operation.
 *
 * @param[in]     op    Arguments fo nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661 p. 367
 *
 * @see nfs4_Compound
 */

int nfs4_op_layoutreturn(struct nfs_argop4 *op, compound_data_t *data,
			 struct nfs_resop4 *resp)
{
	/* Convenience alias for arguments */
	LAYOUTRETURN4args * const arg_LAYOUTRETURN4 =
	    &op->nfs_argop4_u.oplayoutreturn;
	/* Convenience alias for response */
	LAYOUTRETURN4res * const res_LAYOUTRETURN4 =
	    &resp->nfs_resop4_u.oplayoutreturn;
	/* Return code from cache_inode operations */
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	/* Return code from state operations */
	state_status_t state_status = STATE_SUCCESS;
	/* NFS4 status code */
	nfsstat4 nfs_status = 0;
	/* FSID of candidate file to return */
	fsal_fsid_t fsid = { 0, 0 };
	/* True if the supplied layout state was deleted */
	bool deleted = false;
	/* State specified in the case of LAYOUTRETURN4_FILE */
	state_t *layout_state = NULL;
	/* State owner associated with this clientid, for bulk returns */
	state_owner_t *clientid_owner = NULL;
	/* Linked list node for iteration */
	struct glist_head *glist = NULL;
	/* Saved next node for safe iteration */
	struct glist_head *glistn = NULL;
	/* Tag to identify caller in tate log messages */
	const char *tag = "LAYOUTRETURN";
	/* Segment selecting which segments to return. */
	struct pnfs_segment spec = { 0, 0, 0 };

	resp->resop = NFS4_OP_LAYOUTRETURN;

	if (data->minorversion == 0) {
		res_LAYOUTRETURN4->lorr_status = NFS4ERR_INVAL;
		return res_LAYOUTRETURN4->lorr_status;
	}

	switch (arg_LAYOUTRETURN4->lora_layoutreturn.lr_returntype) {
	case LAYOUTRETURN4_FILE:
		nfs_status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

		if (nfs_status  != NFS4_OK) {
			res_LAYOUTRETURN4->lorr_status = nfs_status;
			return res_LAYOUTRETURN4->lorr_status;
		}

		/* Retrieve state corresponding to supplied ID */
		if (arg_LAYOUTRETURN4->lora_reclaim) {
			layout_state = NULL;
		} else {
			nfs_status = nfs4_Check_Stateid(
				&arg_LAYOUTRETURN4->lora_layoutreturn.
					layoutreturn4_u.lr_layout.lrf_stateid,
				data->current_entry,
				&layout_state,
				data,
				STATEID_SPECIAL_CURRENT,
				0,
				FALSE,
				tag);

			if (nfs_status != NFS4_OK) {
				res_LAYOUTRETURN4->lorr_status = nfs_status;
				return res_LAYOUTRETURN4->lorr_status;
			}
		}

		spec.io_mode = arg_LAYOUTRETURN4->lora_iomode;
		spec.offset = arg_LAYOUTRETURN4->lora_layoutreturn.
					layoutreturn4_u.lr_layout.lrf_offset;
		spec.length = arg_LAYOUTRETURN4->lora_layoutreturn.
					layoutreturn4_u.lr_layout.lrf_length;

		res_LAYOUTRETURN4->lorr_status = nfs4_return_one_state(
			data->current_entry,
			arg_LAYOUTRETURN4->lora_layoutreturn.lr_returntype,
			arg_LAYOUTRETURN4->lora_reclaim ?
				circumstance_reclaim :
				circumstance_client,
			layout_state,
			spec,
			arg_LAYOUTRETURN4->lora_layoutreturn.layoutreturn4_u.
				lr_layout.lrf_body.lrf_body_len,
			arg_LAYOUTRETURN4->lora_layoutreturn.layoutreturn4_u.
				lr_layout.lrf_body.lrf_body_val,
				&deleted,
				false);

		if (res_LAYOUTRETURN4->lorr_status == NFS4_OK) {
			if (deleted) {
				memset(data->current_stateid.other,
				       0,
				       sizeof(data->current_stateid.other));

				data->current_stateid.seqid = NFS4_UINT32_MAX;
				res_LAYOUTRETURN4->LAYOUTRETURN4res_u.
				    lorr_stateid.lrs_present = 0;
			} else {
				res_LAYOUTRETURN4->LAYOUTRETURN4res_u.
					lorr_stateid.lrs_present = 1;

				/* Update stateid.seqid and copy to current */
				update_stateid(layout_state,
					       &res_LAYOUTRETURN4->
					       LAYOUTRETURN4res_u.lorr_stateid.
					       layoutreturn_stateid_u.
					       lrs_stateid,
					       data,
					       tag);
			}
		}
		break;

	case LAYOUTRETURN4_FSID:
		nfs_status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

		if (nfs_status != NFS4_OK) {
			res_LAYOUTRETURN4->lorr_status = nfs_status;
			return res_LAYOUTRETURN4->lorr_status;
		}

		cache_status =
		    cache_inode_fsid(data->current_entry, &fsid);

		if (cache_status != CACHE_INODE_SUCCESS) {
			res_LAYOUTRETURN4->lorr_status =
			    nfs4_Errno(cache_status);
			return res_LAYOUTRETURN4->lorr_status;
		}
		break;

	case LAYOUTRETURN4_ALL:
		spec.io_mode = arg_LAYOUTRETURN4->lora_iomode;
		spec.offset = 0;
		spec.length = NFS4_UINT64_MAX;

		state_status = get_clientid_owner(data->session->clientid,
						  &clientid_owner);

		if (state_status != STATE_SUCCESS) {
			res_LAYOUTRETURN4->lorr_status =
			    nfs4_Errno_state(state_status);
			return res_LAYOUTRETURN4->lorr_status;
		}

		/* We need the safe version because return_one_state
		   can delete the current state. */

		glist_for_each_safe(glist, glistn,
				    &clientid_owner->so_owner.so_nfs4_owner.
				     so_state_list) {
			state_t *candidate_state;

			candidate_state = glist_entry(glist,
						      state_t,
						      state_owner_list);

			if (candidate_state->state_type != STATE_TYPE_LAYOUT)
				continue;
			else
				layout_state = candidate_state;

			if (arg_LAYOUTRETURN4->lora_layoutreturn.
			    lr_returntype == LAYOUTRETURN4_FSID) {
				fsal_fsid_t this_fsid;
				cache_status =
				    cache_inode_fsid(layout_state->state_entry,
						     &this_fsid);

				if (cache_status != CACHE_INODE_SUCCESS) {
					res_LAYOUTRETURN4->lorr_status =
					    nfs4_Errno(cache_status);
					return res_LAYOUTRETURN4->lorr_status;
				}

				if (memcmp(&fsid,
					   &this_fsid,
					   sizeof(fsal_fsid_t)))
					continue;
			}

			res_LAYOUTRETURN4->lorr_status = nfs4_return_one_state(
			    layout_state->state_entry,
			    arg_LAYOUTRETURN4->lora_layoutreturn.lr_returntype,
			    arg_LAYOUTRETURN4->lora_reclaim ?
				circumstance_reclaim : circumstance_client,
			    layout_state,
			    spec,
			    0,
			    NULL,
			    &deleted,
			    false);

			if (res_LAYOUTRETURN4->lorr_status != NFS4_OK)
				break;
		}

		memset(data->current_stateid.other,
		       0,
		       sizeof(data->current_stateid.other));

		data->current_stateid.seqid = NFS4_UINT32_MAX;
		res_LAYOUTRETURN4->LAYOUTRETURN4res_u.lorr_stateid.lrs_present =
		    0;
		break;

	default:
		res_LAYOUTRETURN4->lorr_status = NFS4ERR_INVAL;
		return res_LAYOUTRETURN4->lorr_status;
	}

	return res_LAYOUTRETURN4->lorr_status;
}				/* nfs41_op_layoutreturn */

/**
 * @brief Free memory allocated for LAYOUTRETURN result
 *
 * This function frees any memory allocated for the result from
 * the NFS4_OP_LAYOUTRETURN operation.
 *
 * @param[in] resp nfs4_op results
 *
 */

void nfs4_op_layoutreturn_Free(nfs_resop4 *resp)
{
	return;
}				/* nfs41_op_layoutreturn_Free */

void handle_recalls(struct fsal_layoutreturn_arg *arg, state_t *state,
		    const struct pnfs_segment *segment)
{
	/* Iterator over the recall list */
	struct glist_head *recall_iter = NULL;
	/* Next recall for safe iteration */
	struct glist_head *recall_next = NULL;

	glist_for_each_safe(recall_iter, recall_next,
			    &state->state_entry->layoutrecall_list) {
		/* The current recall state */
		struct state_layout_recall_file *r;
		/* Iteration on states */
		struct glist_head *state_iter = NULL;
		/* Next entry in state list */
		struct glist_head *state_next = NULL;

		r = glist_entry(recall_iter,
				struct state_layout_recall_file,
				entry_link);

		glist_for_each_safe(state_iter, state_next, &r->state_list) {
			struct recall_state_list *s;
			/* Iteration on segments */
			struct glist_head *seg_iter = NULL;
			/* We found a segment that satisfies the
			   recall */
			bool satisfaction = false;

			s = glist_entry(state_iter,
					struct recall_state_list,
					link);

			if (s->state != state)
				continue;

			glist_for_each(seg_iter,
				       &s->state->state_data.layout.
				       state_segments) {
				/** @todo this code looks very suspicieous */
			}
			struct state_layout_segment *g;

			g = glist_entry(seg_iter,
					struct state_layout_segment,
					sls_state_segments);

			if (!pnfs_segments_overlap(&g->sls_segment, segment)) {
				/* We don't even touch this */
				break;
			} else if (!pnfs_segment_contains(segment,
							  &g->sls_segment)) {
				/* Not satisfied completely */
			} else
				satisfaction = true;

			if (satisfaction
			    && glist_length(&s->state->state_data.layout.
					    state_segments) == 1) {
				glist_del(&s->link);
				arg->recall_cookies[arg->ncookies++]
				    = r->recall_cookie;
				gsh_free(s);
			}
		}

		if (glist_empty(&r->state_list)) {
			glist_del(&r->entry_link);
			gsh_free(r);
		}
	}
}

/**
 * @brief Return layouts corresponding to one stateid
 *
 * This function returns one or more layouts corresponding to a layout
 * stateid, calling FSAL_layoutreturn for each layout falling within
 * the specified range and iomode.  If all layouts have been returned,
 * it deletes the state.
 *
 * @param[in]     entry        Cache entry whose layouts we return
 * @param[in]     return_type  Whether this is a file, fs, or server return
 * @param[in]     circumstance Why the layout is being returned
 * @param[in,out] layout_state State whose segments we return
 * @param[in]     spec_segment Segment specified in return
 * @param[in]     body_len     Length of type-specific layout return data
 * @param[in]     body_val     Type-specific layout return data
 * @param[out]    deleted      True if the layout state has been deleted
 * @param[in]     hold_lock    Whether to retain the state lock
 *
 * @return NFSv4.1 status codes
 */

nfsstat4 nfs4_return_one_state(cache_entry_t *entry,
			       layoutreturn_type4 return_type,
			       enum fsal_layoutreturn_circumstance circumstance,
			       state_t *layout_state,
			       struct pnfs_segment spec_segment,
			       size_t body_len, const void *body_val,
			       bool *deleted, bool hold_lock)
{
	/* Return from SAL calls */
	state_status_t state_status = 0;
	/* Return from this function */
	nfsstat4 nfs_status = 0;
	/* Iterator along segment list */
	struct glist_head *seg_iter = NULL;
	/* Saved 'next' pointer for iterating over segment list */
	struct glist_head *seg_next = NULL;
	/* Input arguments to FSAL_layoutreturn */
	struct fsal_layoutreturn_arg *arg;
	/* XDR stream holding the lrf_body opaque */
	XDR lrf_body;
	/* The beginning of the stream */
	unsigned int beginning = 0;
	/* If we have a lock on the segment */
	bool seg_locked = false;
	/* Number of recalls currently on the entry */
	size_t recalls = 0;
	/* The current segment in iteration */
	state_layout_segment_t *g = NULL;

	recalls = glist_length(&entry->layoutrecall_list);

	if (body_val) {
		xdrmem_create(&lrf_body,
			      (char *)body_val, /* Decoding won't modify */
			      body_len,
			      XDR_DECODE);

		beginning = xdr_getpos(&lrf_body);
	}

	arg = alloca(sizeof(struct fsal_layoutreturn_arg) +
		     sizeof(void *) * (recalls - 1));

	memset(arg, 0, sizeof(struct fsal_layoutreturn_arg));

	arg->circumstance = circumstance;
	arg->lo_type = layout_state->state_data.layout.state_layout_type;
	arg->return_type = return_type;
	arg->spec_segment = spec_segment;
	arg->ncookies = 0;

	/**
	 * @todo This is where you would want to record
	 * layoutreturns.  There are lots of things that are
	 * effectively layoutreturns that don't go through the
	 * nfs4_op_layoutreturn function, but they do all go through
	 * here.  For circumstance values of circumstance_client,
	 * circumstance_roc, and circumstance_forgotten, it should
	 * count as a legitimate client operation.
	 * circumstance_revoke means that we attempted a recall and
	 * the client misbehaved.  circumstance_shutdown and
	 * circumstance_reclaim are probably not worth dealing with.
	 */

	if (circumstance != circumstance_reclaim) {
		/* The _safe version of glist_for_each allows us to
		 * delete segments while we iterate.
		 */
		glist_for_each_safe(seg_iter, seg_next,
				    &layout_state->state_data.layout.
				    state_segments) {
			/* The current segment in iteration */
			g = glist_entry(seg_iter, state_layout_segment_t,
					sls_state_segments);

			pthread_mutex_lock(&g->sls_mutex);
			seg_locked = true;

			arg->cur_segment = g->sls_segment;
			arg->fsal_seg_data = g->sls_fsal_data;
			/* TODO: why this check does not work */
			arg->last_segment = (seg_next->next == seg_next);

			if (pnfs_segment_contains
			    (&spec_segment, &g->sls_segment)) {
				arg->dispose = true;
			} else if (pnfs_segments_overlap(&spec_segment,
							 &g->sls_segment)) {
				arg->dispose = false;
			} else {
				seg_locked = false;
				pthread_mutex_unlock(&g->sls_mutex);
				continue;
			}

			handle_recalls(arg, layout_state, &g->sls_segment);

			nfs_status = entry->obj_handle->ops->layoutreturn(
						entry->obj_handle,
						op_ctx,
						body_val ? &lrf_body : NULL,
						arg);

			if (nfs_status != NFS4_OK)
				goto out;

			if (arg->dispose) {
				state_status = state_delete_segment(g);
				seg_locked = false;
				if (state_status != STATE_SUCCESS) {
					nfs_status =
					    nfs4_Errno_state(state_status);
					goto out;
				}
			} else {
				g->sls_segment =
				    pnfs_segment_difference(&spec_segment,
							    &g->sls_segment);
				seg_locked = false;
				pthread_mutex_unlock(&g->sls_mutex);
			}
		}

		if (body_val) {
			/* This really should work in all cases for an
			 * in-memory decode stream.
			 */
			xdr_setpos(&lrf_body, beginning);
		}

		if (glist_empty
		    (&layout_state->state_data.layout.state_segments)) {
			state_del(layout_state, hold_lock);
			*deleted = true;
		} else
			*deleted = false;
	} else {
		/* For a reclaim return, there are no recorded segments in
		 * state.
		 */
		arg->cur_segment.io_mode = 0;
		arg->cur_segment.offset = 0;
		arg->cur_segment.length = 0;
		arg->fsal_seg_data = NULL;
		arg->last_segment = false;
		arg->dispose = false;

		nfs_status = entry->obj_handle->ops->layoutreturn(
					entry->obj_handle,
					op_ctx, body_val ? &lrf_body : NULL,
					arg);

		if (nfs_status != NFS4_OK)
			goto out;

		*deleted = true;
	}

	nfs_status = NFS4_OK;

 out:
	if (body_val)
		xdr_destroy(&lrf_body);

	if (seg_locked)
		pthread_mutex_unlock(&g->sls_mutex);

	return nfs_status;
}
