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
 * @file  nfs4_op_close.c
 *
 * @brief Implementation of the NFS4_OP_CLOSE operation
 *
 */
#include "config.h"
#include <pthread.h>
#include "log.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"

/* Tag passed to state functions */
static const char *close_tag = "CLOSE";

void cleanup_layouts(compound_data_t *data)
{
	struct glist_head *glist = NULL;
	struct glist_head *glistn = NULL;

	/* We can't simply grab a pointer to a layout state
	 * and free it later, since a client could have
	 * multiple layout states (since a layout state covers
	 * layouts of only one layout type) each marked
	 * return_on_close.
	 */

	glist_for_each(glist, &data->current_entry->state_list) {
		state_t *state = glist_entry(glist, state_t,
					     state_list);

		if ((state->state_type == STATE_TYPE_SHARE) &&
		    (state->state_owner->so_type == STATE_OPEN_OWNER_NFSV4) &&
		    (state->state_owner->so_owner.so_nfs4_owner.so_clientid
		     == data->session->clientid)) {
			return;
		}
	}

	glist_for_each_safe(glist,
			    glistn,
			    &data->current_entry->state_list) {
		state_t *state = glist_entry(glist, state_t,
					     state_list);
		bool deleted = false;
		struct pnfs_segment entire = {
			.io_mode = LAYOUTIOMODE4_ANY,
			.offset = 0,
			.length = NFS4_UINT64_MAX
		};

		if ((state->state_type == STATE_TYPE_LAYOUT) &&
		    (state->state_owner->so_owner.so_nfs4_owner.so_clientrec
		      == data->session->clientid_record) &&
		    state->state_data.layout.state_return_on_close) {
			nfs4_return_one_state(data->current_entry,
					      LAYOUTRETURN4_FILE,
					      circumstance_roc,
					      state,
					      entire,
					      0,
					      NULL,
					      &deleted,
					      true);
			if (!deleted) {
				LogCrit(COMPONENT_PNFS,
					"Layout state not destroyed on last close return.");
			}
		}
	}
}

/**
 *
 * Brief Implemtation of NFS4_OP_CLOSE
 *
 * This function implemtats the NFS4_OP_CLOSE
 * operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 362
 */

int nfs4_op_close(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	/* Short alias for arguments */
	CLOSE4args * const arg_CLOSE4 = &op->nfs_argop4_u.opclose;
	/* Short alias for response */
	CLOSE4res * const res_CLOSE4 = &resp->nfs_resop4_u.opclose;
	/* Status for NFS protocol functions */
	nfsstat4 nfs_status = NFS4_OK;
	/* The state for the open to be closed */
	state_t *state_found = NULL;
	/* Status for Cache inode operations */
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	/* Status for state operations */
	state_status_t state_status = STATE_SUCCESS;
	/* The open owner of the open state being closed */
	state_owner_t *open_owner = NULL;
	/* Iterator over the state list */
	struct glist_head *glist = NULL;
	/* Secondary safe iterator to continue traversal on delete */
	struct glist_head *glistn = NULL;

	LogDebug(COMPONENT_STATE,
		 "Entering NFS v4 CLOSE handler ----------------------------");

	memset(res_CLOSE4, 0, sizeof(CLOSE4res));
	resp->resop = NFS4_OP_CLOSE;
	res_CLOSE4->status = NFS4_OK;

	/* Do basic checks on a filehandle Object should be a file */
	res_CLOSE4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (res_CLOSE4->status != NFS4_OK)
		return res_CLOSE4->status;

	/* Check stateid correctness and get pointer to state */
	nfs_status = nfs4_Check_Stateid(&arg_CLOSE4->open_stateid,
					data->current_entry,
					&state_found,
					data,
					data->minorversion == 0 ?
					    STATEID_SPECIAL_FOR_CLOSE_40 :
					    STATEID_SPECIAL_FOR_CLOSE_41,
					0,
					FALSE, /* don't check owner seqid */
					close_tag);

	if (nfs_status != NFS4_OK) {
		res_CLOSE4->status = nfs_status;
		LogDebug(COMPONENT_STATE, "CLOSE failed nfs4_Check_Stateid");
		return res_CLOSE4->status;
	}

	if (state_found == NULL) {
		/* Assume this is a replayed close */
		res_CLOSE4->status = NFS4_OK;
		memcpy(res_CLOSE4->CLOSE4res_u.open_stateid.other,
		       arg_CLOSE4->open_stateid.other,
		       OTHERSIZE);

		res_CLOSE4->CLOSE4res_u.open_stateid.seqid =
		    arg_CLOSE4->open_stateid.seqid + 1;

		if (res_CLOSE4->CLOSE4res_u.open_stateid.seqid == 0)
			res_CLOSE4->CLOSE4res_u.open_stateid.seqid = 1;

		LogDebug(COMPONENT_STATE,
			 "CLOSE failed nfs4_Check_Stateid must have already been closed."
			 " But treating it as replayed close and returning NFS4_OK");

		return res_CLOSE4->status;
	}

	open_owner = state_found->state_owner;

	pthread_mutex_lock(&open_owner->so_mutex);

	/* Check seqid */
	if (data->minorversion == 0) {
		if (!Check_nfs4_seqid(open_owner,
				      arg_CLOSE4->seqid,
				      op,
				      data->current_entry,
				      resp,
				      close_tag)) {
			/* Response is all setup for us and LogDebug
			 * told what was wrong
			 */
			pthread_mutex_unlock(&open_owner->so_mutex);
			return res_CLOSE4->status;
		}
	}

	pthread_mutex_unlock(&open_owner->so_mutex);

	inc_state_owner_ref(open_owner);

	PTHREAD_RWLOCK_wrlock(&data->current_entry->state_lock);

	/* Check is held locks remain */
	glist_for_each(glist, &state_found->state_data.share.share_lockstates) {
		state_t *lock_state = glist_entry(glist,
						  state_t,
						  state_data.lock.
						  state_sharelist);

		if (!glist_empty(&lock_state->state_data.lock.state_locklist)) {
			/* Is this actually what we want to do, rather
			 * than freeing all locks on close?
			 * Especially since the next thing we do is
			 * go through ane release any lock states.
			 */
			res_CLOSE4->status = NFS4ERR_LOCKS_HELD;

			PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
			LogDebug(COMPONENT_STATE,
				 "NFS4 Close with existing locks");
			goto out;
		}
	}

	/* Handle stateid/seqid for success */
	update_stateid(state_found,
		       &res_CLOSE4->CLOSE4res_u.open_stateid,
		       data,
		       close_tag);

	/* File is closed, release the corresponding lock states */
	glist_for_each_safe(glist, glistn,
			    &state_found->state_data.share.share_lockstates) {
		state_t *lock_state = glist_entry(glist, state_t,
						  state_data.lock.
						  state_sharelist);

		state_del_locked(lock_state, data->current_entry);
	}

	/* File is closed, release the share state */
	if (state_found->state_type == STATE_TYPE_SHARE) {
		state_status = state_share_remove(state_found->state_entry,
						  open_owner,
						  state_found);

		if (state_status != STATE_SUCCESS) {
			LogEvent(COMPONENT_STATE,
				 "CLOSE failed to release share state "
				 "error: %s", state_err_str(state_status));
		}
	}

	/* File is closed, release the corresponding state */
	state_del_locked(state_found, data->current_entry);

	/* Poison the current stateid */
	data->current_stateid_valid = false;

	if (data->minorversion > 0)
		cleanup_layouts(data);

	/* Fill in the clientid for NFSv4.0 */
	if (data->minorversion == 0) {
		op_ctx->clientid =
		    &open_owner->so_owner.so_nfs4_owner.so_clientid;
	}

	/* Close the file in FSAL through the cache inode */
	cache_status = cache_inode_close(data->current_entry,
					 CACHE_INODE_FLAG_REALLYCLOSE);

	if (cache_status != CACHE_INODE_SUCCESS) {
		res_CLOSE4->status = nfs4_Errno(cache_status);
		PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
		goto out;
	}

	if (data->minorversion == 0)
		op_ctx->clientid = NULL;

	PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
	res_CLOSE4->status = NFS4_OK;

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS)) {
		nfs_State_PrintAll();
		nfs4_owner_PrintAll();
	}

 out:

	/* Save the response in the open owner */
	if (data->minorversion == 0) {
		Copy_nfs4_state_req(open_owner, arg_CLOSE4->seqid, op,
				    data->current_entry, resp, close_tag);
	}

	dec_state_owner_ref(open_owner);

	return res_CLOSE4->status;
}				/* nfs4_op_close */

/**
 * @brief Free memory allocated for CLOSE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_CLOSE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_close_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_close_Free */

void nfs4_op_close_CopyRes(CLOSE4res *res_dst, CLOSE4res *res_src)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_close_CopyRes */
