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
#include "gsh_rpc.h"
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
	/* Convenience alias for arguments */
	layoutreturn_file4 *lr_layout =
		&arg_LAYOUTRETURN4->lora_layoutreturn.layoutreturn4_u.lr_layout;
	/* Convenience alias for response */
	LAYOUTRETURN4res * const res_LAYOUTRETURN4 =
	    &resp->nfs_resop4_u.oplayoutreturn;
	/* Convenience alias for response */
	layoutreturn_stateid *lorr_stateid =
		&res_LAYOUTRETURN4->LAYOUTRETURN4res_u.lorr_stateid;
	/* Convenience alias for response */
	stateid4 *lrs_stateid =
		&lorr_stateid->layoutreturn_stateid_u.lrs_stateid;
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
	struct glist_head *state_list;
	/* Linked list node for iteration */
	struct glist_head *glist = NULL;
	/* Saved next node for safe iteration */
	struct glist_head *glistn = NULL;
	/* Tag to identify caller in tate log messages */
	const char *tag = "LAYOUTRETURN";
	/* Segment selecting which segments to return. */
	struct pnfs_segment spec = { 0, 0, 0 };
	/* Remember if we need to do fsid based return */
	bool return_fsid = false;
	/* Referenced file */
	struct fsal_obj_handle *obj = NULL;
	/* Referenced export */
	struct gsh_export *export = NULL;
	/* Root op context for returning fsid or all layouts */
	struct root_op_context root_op_context;
	/* Keep track of so_mutex */
	bool so_mutex_locked = false;
	state_t *first;

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
			break;
		}

		/* Retrieve state corresponding to supplied ID */
		if (!arg_LAYOUTRETURN4->lora_reclaim) {
			nfs_status = nfs4_Check_Stateid(
				&lr_layout->lrf_stateid,
				data->current_obj,
				&layout_state,
				data,
				STATEID_SPECIAL_CURRENT,
				0,
				false,
				tag);

			if (nfs_status != NFS4_OK) {
				res_LAYOUTRETURN4->lorr_status = nfs_status;
				break;
			}
		}

		spec.io_mode = arg_LAYOUTRETURN4->lora_iomode;
		spec.offset = lr_layout->lrf_offset;
		spec.length = lr_layout->lrf_length;

		PTHREAD_RWLOCK_wrlock(
			&data->current_obj->state_hdl->state_lock);

		res_LAYOUTRETURN4->lorr_status = nfs4_return_one_state(
			data->current_obj,
			arg_LAYOUTRETURN4->lora_layoutreturn.lr_returntype,
			arg_LAYOUTRETURN4->lora_reclaim
				? circumstance_reclaim
				: circumstance_client,
			layout_state,
			spec,
			lr_layout->lrf_body.lrf_body_len,
			lr_layout->lrf_body.lrf_body_val,
			&deleted);

		PTHREAD_RWLOCK_unlock(
			&data->current_obj->state_hdl->state_lock);

		if (res_LAYOUTRETURN4->lorr_status == NFS4_OK) {
			if (deleted) {
				/* Poison the current stateid */
				data->current_stateid_valid = false;

				lorr_stateid->lrs_present = 0;
			} else {
				lorr_stateid->lrs_present = 1;

				/* Update stateid.seqid and copy to current */
				update_stateid(layout_state, lrs_stateid,
					       data, tag);
			}
		}

		if (!arg_LAYOUTRETURN4->lora_reclaim)
			dec_state_t_ref(layout_state);

		break;

	case LAYOUTRETURN4_FSID:
		nfs_status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

		if (nfs_status != NFS4_OK) {
			res_LAYOUTRETURN4->lorr_status = nfs_status;
			break;
		}

		fsid = data->current_obj->fsid;
		return_fsid = true;

		/* FALLTHROUGH */

	case LAYOUTRETURN4_ALL:
		spec.io_mode = arg_LAYOUTRETURN4->lora_iomode;
		spec.offset = 0;
		spec.length = NFS4_UINT64_MAX;

		clientid_owner = &data->session->clientid_record->cid_owner;

		/* Initialize req_ctx */
		init_root_op_context(&root_op_context, NULL, NULL,
				     0, 0, UNKNOWN_REQUEST);

		/* We need the safe version because return_one_state
		 * can delete the current state.
		 *
		 * Since we can not hold so_mutex (which protects the list)
		 * the entire time, we will have to restart here after
		 * dropping the mutex.
		 *
		 * Since we push each entry to the end of the list, we will
		 * not need to continually examine entries that need to be
		 * skipped, except for one final pass.
		 *
		 * An example flow might be:
		 * skip 1
		 * skip 2
		 * do some work on 3
		 * restart
		 * skip 4
		 * do some work on 5
		 * restart
		 * skip 1
		 * skip 2
		 * skip 4
		 * done
		 */

 again:

		PTHREAD_MUTEX_lock(&clientid_owner->so_mutex);
		so_mutex_locked = true;
		first = NULL;

		state_list =
			&clientid_owner->so_owner.so_nfs4_owner.so_state_list;

		glist_for_each_safe(glist, glistn, state_list) {
			layout_state = glist_entry(glist,
						   state_t,
						   state_owner_list);
			if (first == NULL)
				first = layout_state;
			else if (first == layout_state)
				break;

			/* Move to end of list in case of error to ease
			 * retries and push off dealing with non-layout
			 * states (which should only be delegations).
			 */
			glist_del(&layout_state->state_owner_list);
			glist_add_tail(state_list,
				       &layout_state->state_owner_list);

			if (layout_state->state_type != STATE_TYPE_LAYOUT)
				continue;

			if (!get_state_obj_export_owner_refs(layout_state, &obj,
							     &export, NULL)) {
				/* This state is associated with a file or
				 * export that is going stale, skip it (it
				 * will be cleaned up as part of the stale
				 * entry or export processing. */
				continue;
			}

			/* Set up the root op context for this state */
			root_op_context.req_ctx.clientid =
			    &clientid_owner->so_owner.so_nfs4_owner.so_clientid;
			root_op_context.req_ctx.ctx_export = export;
			root_op_context.req_ctx.fsal_export =
							export->fsal_export;

			/* Take a reference on the state_t */
			inc_state_t_ref(layout_state);

			/* Now we need to drop so_mutex to continue the
			 * processing.
			 */
			PTHREAD_MUTEX_unlock(&clientid_owner->so_mutex);
			so_mutex_locked = false;

			if (return_fsid) {
				if (!memcmp(&fsid, &data->current_obj->fsid,
					    sizeof(fsid))) {
					obj->obj_ops->put_ref(obj);
					put_gsh_export(export);
					dec_state_t_ref(layout_state);

					/* Since we had to drop so_mutex, the
					 * list may have changed under us, we
					 * MUST start over.
					 */
					goto again;
				}
			}

			PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);

			res_LAYOUTRETURN4->lorr_status = nfs4_return_one_state(
			    obj,
			    arg_LAYOUTRETURN4->lora_layoutreturn.lr_returntype,
			    arg_LAYOUTRETURN4->lora_reclaim ?
				circumstance_reclaim : circumstance_client,
			    layout_state,
			    spec,
			    0,
			    NULL,
			    &deleted);

			PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

			/* Release the state_t reference */
			dec_state_t_ref(layout_state);

			if (res_LAYOUTRETURN4->lorr_status != NFS4_OK)
				break;

			/* Since we had to drop so_mutex, the list may have
			 * changed under us, we MUST start over.
			 */
			obj->obj_ops->put_ref(obj);
			put_gsh_export(export);
			goto again;
		}

		if (so_mutex_locked)
			PTHREAD_MUTEX_unlock(&clientid_owner->so_mutex);

		/* Poison the current stateid */
		data->current_stateid_valid = false;
		lorr_stateid->lrs_present = 0;
		break;

	default:
		res_LAYOUTRETURN4->lorr_status = NFS4ERR_INVAL;
	}

	if (arg_LAYOUTRETURN4->lora_layoutreturn.lr_returntype ==
							LAYOUTRETURN4_FSID
	    ||
	    arg_LAYOUTRETURN4->lora_layoutreturn.lr_returntype ==
							LAYOUTRETURN4_ALL
	    ) {
		/* Release the root op context we setup above */
		release_root_op_context();
	}

	if (obj != NULL) {
		/* Release object ref */
		obj->obj_ops->put_ref(obj);
	}

	if (export != NULL) {
		/* Release the export */
		put_gsh_export(export);
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
	/* Nothing to be done */
}

/**
 * @brief Handle recalls corresponding to one stateid
 *
 * @note the state_lock MUST be held for write
 *
 * @param[in]     args         Layout return args
 * @param[in]     ostate       File state
 * @param[in]     state        The state in question
 * @param[in]     segment      Segment specified in return
 *
 */

void handle_recalls(struct fsal_layoutreturn_arg *arg,
		    struct state_hdl *ostate,
		    state_t *state,
		    const struct pnfs_segment *segment)
{
	/* Iterator over the recall list */
	struct glist_head *recall_iter = NULL;
	/* Next recall for safe iteration */
	struct glist_head *recall_next = NULL;
	struct glist_head *state_segments =
		&state->state_data.layout.state_segments;

	glist_for_each_safe(recall_iter,
			    recall_next,
			    &ostate->file.layoutrecall_list) {
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

			glist_for_each(seg_iter, state_segments) {
				struct state_layout_segment *g;

				g = glist_entry(seg_iter,
						struct state_layout_segment,
						sls_state_segments);

				if (!pnfs_segments_overlap(&g->sls_segment,
							   segment)) {
					/* We don't even touch this */
					break;
				} else if (!pnfs_segment_contains(
							segment,
							&g->sls_segment)) {
					/* Not satisfied completely */
				} else
					satisfaction = true;
			}

			if (satisfaction && glist_length(state_segments) == 1) {
				dec_state_t_ref(s->state);
				glist_del(&s->link);
				arg->recall_cookies[arg->ncookies++]
				    = r->recall_cookie;
				gsh_free(s);
			}
		}

		if (glist_empty(&r->state_list)) {
			/* Remove from entry->layoutrecall_list */
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
 * @note The state_lock MUST be held for write
 *
 * @param[in]     obj          File whose layouts we return
 * @param[in]     return_type  Whether this is a file, fs, or server return
 * @param[in]     circumstance Why the layout is being returned
 * @param[in,out] state        State whose segments we return
 * @param[in]     spec_segment Segment specified in return
 * @param[in]     body_len     Length of type-specific layout return data
 * @param[in]     body_val     Type-specific layout return data
 * @param[out]    deleted      True if the layout state has been deleted
 *
 * @return NFSv4.1 status codes
 */

nfsstat4 nfs4_return_one_state(struct fsal_obj_handle *obj,
			       layoutreturn_type4 return_type,
			       enum fsal_layoutreturn_circumstance circumstance,
			       state_t *state,
			       struct pnfs_segment spec_segment,
			       size_t body_len, const void *body_val,
			       bool *deleted)
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
	/* Number of recalls currently on the entry */
	size_t recalls = 0;
	/* The current segment in iteration */
	state_layout_segment_t *g = NULL;
	struct glist_head *state_segments =
		&state->state_data.layout.state_segments;


	recalls = glist_length(&obj->state_hdl->file.layoutrecall_list);

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
		arg->lo_type = state->state_data.layout.state_layout_type;

		/* The _safe version of glist_for_each allows us to
		 * delete segments while we iterate.
		 */
		glist_for_each_safe(seg_iter, seg_next, state_segments) {
			/* The current segment in iteration */
			g = glist_entry(seg_iter, state_layout_segment_t,
					sls_state_segments);

			arg->cur_segment = g->sls_segment;
			arg->fsal_seg_data = g->sls_fsal_data;
			/* TODO: why this check does not work */
			arg->last_segment = (seg_next->next == seg_next);

			if (pnfs_segment_contains
			    (&spec_segment, &g->sls_segment)) {
				arg->dispose = true;
			} else if (pnfs_segments_overlap(&spec_segment,
							 &g->sls_segment))
				arg->dispose = false;
			else
				continue;

			handle_recalls(arg, obj->state_hdl, state,
				       &g->sls_segment);

			nfs_status = obj->obj_ops->layoutreturn(
						obj,
						op_ctx,
						body_val ? &lrf_body : NULL,
						arg);

			if (nfs_status != NFS4_OK)
				goto out;

			if (arg->dispose) {
				state_status = state_delete_segment(g);
				if (state_status != STATE_SUCCESS) {
					nfs_status =
					    nfs4_Errno_state(state_status);
					goto out;
				}
			} else {
				g->sls_segment =
				    pnfs_segment_difference(&spec_segment,
							    &g->sls_segment);
			}
		}

		if (body_val) {
			/* This really should work in all cases for an
			 * in-memory decode stream.
			 */
			xdr_setpos(&lrf_body, beginning);
		}

		if (glist_empty(state_segments)) {
			state_del_locked(state);
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

		nfs_status = obj->obj_ops->layoutreturn(
					obj,
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

	return nfs_status;
}
