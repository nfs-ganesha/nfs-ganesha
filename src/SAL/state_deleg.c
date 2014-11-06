/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright IBM  (2014)
 * contributeur : Jeremy Bongio   jbongio@us.ibm.com
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file state_deleg.c
 * @brief Delegation management
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"
#include "nfs_rpc_callback.h"
#include "server_stats.h"
#include "fsal_up.h"
#include "nfs_file_handle.h"

/**
 * @brief Check if exiting OPENs would conflict granting a delegation.
 *
 * cache_entry_t state lock must be held at least in read mode while
 * calling this function.
 *
 * @param[in] entry	Cache inode entry
 * @param[in] state     Open state of current OPEN operation.
 *
 * @return true if granting delegation would conflict with outstanding
 * OPENs.
 */
bool state_open_deleg_conflict(cache_entry_t *entry, const state_t *open_state)
{
	const struct state_share *share = &open_state->state_data.share;

	assert(open_state->state_type == STATE_TYPE_SHARE);

	switch (share->share_access & OPEN4_SHARE_ACCESS_BOTH) {
	case OPEN4_SHARE_ACCESS_BOTH:
		/* We would be granting a write delegation. If this is
		 * the only outstanding OPEN, then we can grant
		 * write delegation without a conflict.
		 */
		if (entry->object.file.share_state.share_access_read == 1 &&
		    entry->object.file.share_state.share_access_write == 1) {
			return false;
		}
		break;
	case OPEN4_SHARE_ACCESS_WRITE:
		/* We would be granting a write delegation. If this is
		 * the only outstanding OPEN, then we can grant
		 * write delegation without a conflict.
		 */
		if (entry->object.file.share_state.share_access_read == 0 &&
		    entry->object.file.share_state.share_access_write == 1) {
			return false;
		}
		break;
	case OPEN4_SHARE_ACCESS_READ:
		/* We would be granting a read delegation. If there is
		 * no write OPEN, then we can grant read delegation
		 * without a conflict.
		 */
		if (entry->object.file.share_state.share_access_write == 0)
			return false;
		break;
	}

	return true;
}

/**
 * @brief Initialize new delegation state as argument for state_add()
 *
 * Initialize delegation state struct. This is then given as an argument
 * to state_add()
 *
 * @param[in/out] deleg_state Delegation state struct to be init. Can't be NULL.
 * @param[in] sd_type Type of delegation, READ or WRITE.
 * @param[in] client Client that will own this delegation.
 */
void init_new_deleg_state(union state_data *deleg_state,
			  open_delegation_type4 deleg_type,
			  nfs_client_id_t *client)
{
	struct cf_deleg_stats *clfile_entry =
		&deleg_state->deleg.sd_clfile_stats;

	deleg_state->deleg.sd_type = deleg_type;
	deleg_state->deleg.sd_grant_time = time(NULL);
	deleg_state->deleg.sd_state = DELEG_GRANTED;

	clfile_entry->cfd_rs_time = 0;
	clfile_entry->cfd_r_time = 0;
}

/**
 * @brief Attempt to acquire a lease lock (delegation)
 *
 * @param[in]  state      Associated state for the lock
 * @param[in]  reclaim    Indicates if this is a delegation reclaim or not
 *
 * state_lock must be held while calling this function
 */
state_status_t acquire_lease_lock(state_t *state, bool reclaim)
{
	state_status_t status;
	fsal_lock_param_t lock_desc;

	lock_desc.lock_start = 0;
	lock_desc.lock_length = 0;
	lock_desc.lock_sle_type = FSAL_LEASE_LOCK;
	lock_desc.lock_reclaim = reclaim;

	if (state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE)
		lock_desc.lock_type = FSAL_LOCK_W;
	else
		lock_desc.lock_type = FSAL_LOCK_R;

	/* Create a new deleg data object */
	state->state_data.deleg.dd_export_id = op_ctx->export->export_id;

	status = do_lock_op(state->state_entry, FSAL_OP_LOCK,
			    state->state_owner, &lock_desc,
			    NULL, NULL, false, LEASE_LOCK);

	if (status == STATE_SUCCESS) {
		update_delegation_stats(state);
	} else {
		LogDebug(COMPONENT_STATE, "Could not set lease, error=%s",
			 state_err_str(status));
	}

	return status;
}

/**
 * @brief Release a lease lock (delegation)
 *
 * @param[in] state    Associated state
 *
 * state_lock must be held while calling this function
 */
state_status_t release_lease_lock(state_t *state)
{
	state_status_t status;
	fsal_lock_param_t lock_desc;

	lock_desc.lock_type = FSAL_LOCK_R;  /* doesn't matter for unlock */
	lock_desc.lock_start = 0;
	lock_desc.lock_length = 0;
	lock_desc.lock_sle_type = FSAL_LEASE_LOCK;
	lock_desc.lock_reclaim = false;

	LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG, "DELEGRETURN",
		state->state_entry, state->state_owner, &lock_desc);

	status = do_lock_op(state->state_entry, FSAL_OP_UNLOCK,
			    state->state_owner, &lock_desc, NULL, NULL,
			    false, LEASE_LOCK);

	if (status != STATE_SUCCESS)
		LogMajor(COMPONENT_STATE, "Unable to unlock FSAL, error=%s",
			 state_err_str(status));

	return status;
}

/**
 * @brief Update statistics on successfully granted delegation.
 *
 * Update statistics on successfully granted delegation.
 * Note: This should be called only when a delegation is successfully granted.
 *       So far this should only be called in state_lock().
 *
 * @param[in] Delegation Entry
 */
bool update_delegation_stats(struct state_t *deleg)
{
	cache_entry_t *entry = deleg->state_entry;
	nfs_client_id_t *client = deleg->state_owner->so_owner.
					so_nfs4_owner.so_clientrec;

	/* Update delegation stats for file. */
	struct file_deleg_stats *statistics = &entry->object.file.fdeleg_stats;

	statistics->fds_curr_delegations++;
	statistics->fds_delegation_count++;
	statistics->fds_last_delegation = time(NULL);

	/* Update delegation stats for client. */
	inc_grants(client->gsh_client);
	client->curr_deleg_grants++;

	return true;
}

/* Add a new delegation length to the average length stat. */
static int advance_avg(time_t prev_avg, time_t new_time,
		       uint32_t prev_tot, uint32_t curr_tot)
{
	return ((prev_tot * prev_avg) + new_time) / curr_tot;
}

/**
 * @brief Update statistics on successfully recalled delegation.
 *
 * Update statistics on successfully recalled delegation.
 * Note: This should be called only when a delegation is successfully recalled.
 *
 * @param[in] deleg Delegation state
 */
void deleg_heuristics_recall(struct state_t *deleg)
{
	nfs_client_id_t *client;
	/* Update delegation stats for file. */
	struct file_deleg_stats *statistics =
		&deleg->state_entry->object.file.fdeleg_stats;

	client = deleg->state_owner->so_owner.so_nfs4_owner.so_clientrec;

	statistics->fds_curr_delegations--;
	statistics->fds_recall_count++;

	/* Update delegation stats for client. */
	dec_grants(client->gsh_client);
	client->curr_deleg_grants--;

	/* Update delegation stats for file. */
	statistics->fds_avg_hold = advance_avg(statistics->fds_avg_hold,
					   time(NULL)
					   - statistics->fds_last_delegation,
					   statistics->fds_recall_count - 1,
					   statistics->fds_recall_count);
}

/**
 * @brief Initialize the file-specific delegation statistics
 *
 * Initialize the file-specific delegation statistics used later for deciding
 * if a delegation should be granted on this file based on heuristics.
 *
 * @param[in] entry Inode entry the delegation will be on.
 */
bool init_deleg_heuristics(cache_entry_t *entry)
{
	struct file_deleg_stats *statistics;

	if (entry->type != REGULAR_FILE) {
		LogCrit(COMPONENT_STATE,
			"Initialization of delegation stats for an entry that is NOT a regular file!");
		return false;
	}

	statistics = &entry->object.file.fdeleg_stats;
	statistics->fds_curr_delegations = 0;
	statistics->fds_deleg_type = OPEN_DELEGATE_NONE;
	statistics->fds_delegation_count = 0;
	statistics->fds_recall_count = 0;
	statistics->fds_last_delegation = 0;
	statistics->fds_last_recall = 0;
	statistics->fds_avg_hold = 0;
	statistics->fds_num_opens = 0;
	statistics->fds_first_open = 0;

	return true;
}

/* Most clients retry NFS operations after 5 seconds. The following
 * should be good enough to avoid starving a client's open
 */
#define RECALL2DELEG_TIME 10

/**
 * @brief Decide if a delegation should be granted based on heuristics.
 *
 * Decide if a delegation should be granted based on heuristics.
 *
 * @param[in] entry Inode entry the delegation will be on.
 * @param[in] client Client that would own the delegation.
 * @param[in] open_state The open state for the inode to be delegated.
 * @param[out] prerecall flag for reclaims.
 */
bool should_we_grant_deleg(cache_entry_t *entry, nfs_client_id_t *client,
			   state_t *open_state, OPEN4args *args,
			   state_owner_t *owner, bool *prerecall)
{
	/* specific file, all clients, stats */
	struct file_deleg_stats *file_stats = &entry->object.file.fdeleg_stats;
	/* specific client, all files stats */
	open_claim_type4 claim = args->claim.claim;

	LogDebug(COMPONENT_STATE, "Checking if we should grant delegation.");

	assert(open_state->state_type == STATE_TYPE_SHARE);

	*prerecall = false;
	if (!nfs_param.nfsv4_param.allow_delegations
	    || !op_ctx->fsal_export->exp_ops.fs_supports(
					op_ctx->fsal_export,
					fso_delegations_r)
	    || !(op_ctx->export_perms->options & EXPORT_OPTION_DELEGATIONS)
	    || (!owner->so_owner.so_nfs4_owner.so_confirmed
		&& claim == CLAIM_NULL)
	    || claim == CLAIM_DELEGATE_CUR)
		return false;

	/* set the pre-recall flag for reclaims if the server does not want the
	 * delegation to remain in force */
	if (get_cb_chan_down(client)) {
		switch (claim) {
		case CLAIM_PREVIOUS:
			*prerecall = true;
			return args->claim.open_claim4_u.delegate_type
					== OPEN_DELEGATE_NONE ? false : true;
		case CLAIM_DELEGATE_PREV:
			*prerecall = true;
			return true;
		default:
			return false;
		}
	} else {
		*prerecall = false;
		switch (claim) {
		case CLAIM_PREVIOUS:
			return args->claim.open_claim4_u.delegate_type
					== OPEN_DELEGATE_NONE ? false : true;
		case CLAIM_DELEGATE_PREV:
			return true;
		default:
			break;
		}
	}

	/* If there is a recent recall on this file, the client that made
	 * the conflicting open may retry the open later. Don't give out
	 * delegation to avoid starving the client's open that caused
	 * the recall.
	 */
	if (file_stats->fds_last_recall != 0 &&
	    time(NULL) - file_stats->fds_last_recall < RECALL2DELEG_TIME)
		return false;

	/* Check if this is a misbehaving or unreliable client */
	if (client->num_revokes > 2) /* more than 2 revokes */
		return false;

	LogDebug(COMPONENT_STATE, "Let's delegate!!");
	return true;
}

/**
 * @brief Form the ACE mask for the delegated file.
 *
 * Form the ACE mask for the delegated file.
 *
 * @param[in] entry Inode entry the delegation will be on.
 * @param[in/out] permissions ACE mask for delegated inode.
 * @param[in] type Type of delegation. Either READ or WRITE.
 */
void get_deleg_perm(cache_entry_t *entry, nfsace4 *permissions,
		    open_delegation_type4 type)
{
	/* We need to create an access_mask that shows who
	 * can OPEN this file. */
	if (type == OPEN_DELEGATE_WRITE)
		;
	else if (type == OPEN_DELEGATE_READ)
		;
	permissions->type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
	permissions->flag = 0;
	permissions->access_mask = 0;
	permissions->who.utf8string_len = 0;
	permissions->who.utf8string_val = NULL;
}

/**
 * @brief Mark a delegation revoked
 *
 * Mark the delegation state revoked, further ops on this state should return
 * NFS4ERR_REVOKED or NFS4ERR_EXPIRED
 *
 * @param[in] deleg state lock entry.
 * Should be called with state lock held.
 */
state_status_t deleg_revoke(struct state_t *deleg_state)
{
	state_status_t state_status;
	struct nfs_client_id_t *clid;
	nfs_fh4 fhandle;
	struct root_op_context root_op_context;

	clid = deleg_state->state_owner->so_owner.so_nfs4_owner.so_clientrec;

	/* Allocation of a new file handle */
	if (nfs4_AllocateFH(&fhandle) != NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4_LOCK, "nfs4_AllocateFH failed");
		return NFS4ERR_SERVERFAULT;
	}

	/* Building a new fh ; Ignore return code, should not fail*/
	(void) nfs4_FSALToFhandle(&fhandle,
				  deleg_state->state_entry->obj_handle,
				  deleg_state->state_export);

	deleg_heuristics_recall(deleg_state);

	/* Build op_context for state_unlock_locked */
	init_root_op_context(&root_op_context, NULL, NULL, 0, 0,
			     UNKNOWN_REQUEST);
	root_op_context.req_ctx.clientid = &deleg_state->state_owner->
				so_owner.so_nfs4_owner.so_clientid;
	root_op_context.req_ctx.export = deleg_state->state_export;
	root_op_context.req_ctx.fsal_export =
			deleg_state->state_export->fsal_export;

	/* release_lease_lock() returns delegation to FSAL */
	state_status = release_lease_lock(deleg_state);

	release_root_op_context();

	if (state_status != STATE_SUCCESS) {
		LogDebug(COMPONENT_NFS_V4_LOCK, "state unlock failed: %d",
			 state_status);
	}

	/* Put the revoked delegation on the stable storage. */
	nfs4_record_revoke(clid, &fhandle);
	state_del_locked(deleg_state);

	gsh_free(fhandle.nfs_fh4_val);
	return STATE_SUCCESS;
}

/**
 * @brief Mark the delegation revoked
 *
 * Mark the delegation state revoked, further ops on this state should
 * return NFS4ERR_REVOKED or NFS4ERR_EXPIRED
 *
 * @param[in] cache inode entry
 * @param[in] delegation state
 *
 * Must be called with cache inode entry's state lock held in read-write
 * mode.
 */
void state_deleg_revoke(state_t *state)
{
	/* If we are already in the process of recalling or revoking
	 * this delegation from elsewhere, skip it here.
	 */
	if (state->state_data.deleg.sd_state != DELEG_GRANTED)
		return;

	state->state_data.deleg.sd_state = DELEG_RECALL_WIP;

	(void)deleg_revoke(state);
}

/**
 * @brief Check if an operation is conflicting with delegations.
 *
 * Check if an operation will conflict with current delegations on a file.
 * Return TRUE if there is a conflict and the delegations have been recalled.
 * Return FALSE if there is no conflict.
 *
 * @param[in] entry cache inode entry
 * @param[in] write a boolean indicating whether the operation will read or
 *            change the file.
 *
 * @retval true if there is a conflict and the delegations have been recalled.
 * @retval false if there is no delegation conflict.
 *
 * Must be called with cache inode entry's state lock held in read or read-write
 * mode.
 */
bool state_deleg_conflict(cache_entry_t *entry, bool write)
{
	struct file_deleg_stats *deleg_stats;

	if (entry->type != REGULAR_FILE)
		return false;

	deleg_stats = &entry->object.file.fdeleg_stats;
	if (deleg_stats->fds_curr_delegations > 0
	    && ((deleg_stats->fds_deleg_type == OPEN_DELEGATE_READ
		 && write)
		|| (deleg_stats->fds_deleg_type == OPEN_DELEGATE_WRITE))
	    ) {
		LogDebug(COMPONENT_STATE,
			 "While trying to perform a %s op, found a "
			 "conflicting %s delegation",
			 write ? "write" : "read",
			 (deleg_stats->fds_deleg_type
			  == OPEN_DELEGATE_WRITE) ? "WRITE" : "READ");
		if (async_delegrecall(general_fridge, entry) != 0)
			LogCrit(COMPONENT_STATE,
				"Failed to start thread to recall "
				"delegation from conflicting "
				"operation.");
		return true;
	}
	return false;
}

bool deleg_supported(cache_entry_t *entry, struct fsal_export *fsal_export,
		     struct export_perms *export_perms, uint32_t share_access)
{
	if (!nfs_param.nfsv4_param.allow_delegations)
		return false;
	if (entry->type != REGULAR_FILE)
		return false;

	/* In a read-write case, we handle write delegation. So we should
	 * check for OPEN4_SHARE_ACCESS_WRITE bit first!
	 */
	if (share_access & OPEN4_SHARE_ACCESS_WRITE) {
		if (!fsal_export->exp_ops.fs_supports(fsal_export,
						   fso_delegations_w))
			return false;
		if (!(export_perms->options & EXPORT_OPTION_WRITE_DELEG))
			return false;
	} else {
		assert(share_access & OPEN4_SHARE_ACCESS_READ);
		if (!fsal_export->exp_ops.fs_supports(fsal_export,
						   fso_delegations_r))
			return false;
		if (!(export_perms->options & EXPORT_OPTION_READ_DELEG))
			return false;
	}

	return true;
}

bool can_we_grant_deleg(cache_entry_t *entry, state_t *open_state)
{
	struct glist_head *glist;
	state_lock_entry_t *lock_entry;
	const struct state_share *share = &open_state->state_data.share;

	/* Can't grant delegation if there is an anonymous operation
	 * in progress
	 */
	if (atomic_fetch_uint32_t(&entry->object.file.anon_ops) != 0) {
		LogFullDebug(COMPONENT_STATE,
			     "Anonymous op in progress, not granting delegation");
		return false;
	}

	/* Check for outstanding open state that may conflict with granting
	 * the delegation
	 */
	if (state_open_deleg_conflict(entry, open_state)) {
		LogFullDebug(COMPONENT_STATE,
			     "Conflicting exiting open state, not granting delegation");
		return false;
	}

	/* Check for conflicting NLM locks. Write delegation would conflict
	 * with any kind of NLM lock, and NLM write lock would conflict
	 * with any kind of delegation.
	 */
	glist_for_each(glist, &entry->object.file.lock_list) {
		lock_entry = glist_entry(glist, state_lock_entry_t, sle_list);
		if (lock_entry->sle_lock.lock_type == FSAL_NO_LOCK)
			continue; /* no lock, skip */
		if (share->share_access & OPEN4_SHARE_ACCESS_WRITE ||
		    lock_entry->sle_lock.lock_type == FSAL_LOCK_W) {
			LogFullDebug(COMPONENT_STATE,
				     "Conflicting NLM lock. Not granting delegation");
			return false;
		}
	}

	return true;
}
