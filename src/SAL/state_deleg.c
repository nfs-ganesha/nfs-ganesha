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
#include "nfs4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"

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
void init_new_deleg_state(state_data_t *deleg_state,
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
 * @brief Update statistics on successfully granted delegation.
 *
 * Update statistics on successfully granted delegation.
 * Note: This should be called only when a delegation is successfully granted.
 *       So far this should only be called in state_lock().
 *
 * @param[in] Delegation Entry
 */
bool update_delegation_stats(state_lock_entry_t *deleg_entry)
{
	cache_entry_t *entry = deleg_entry->sle_entry;
	struct c_deleg_stats *cl_stats = &deleg_entry->sle_owner->so_owner.so_nfs4_owner.so_clientrec->cid_deleg_stats;

	/* Update delegation stats for file. */
	struct file_deleg_stats *statistics =
		&entry->object.file.fdeleg_stats;
	statistics->fds_curr_delegations++;
	statistics->fds_disabled = false;
	statistics->fds_delegation_count++;
	statistics->fds_last_delegation = time(NULL);

	/* Update delegation stats for client. */
	atomic_inc_uint32_t(&cl_stats->curr_deleg_grants);

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
 * @param[in] Delegation Lock Entry
 */
bool deleg_heuristics_recall(state_lock_entry_t *deleg_entry)
{
	cache_entry_t *entry = deleg_entry->sle_entry;
        nfs_client_id_t *client = deleg_entry->sle_owner->so_owner.so_nfs4_owner.so_clientrec;
	/* Update delegation stats for file. */
	struct file_deleg_stats *statistics =
		&entry->object.file.fdeleg_stats;
	statistics->fds_curr_delegations--;
	statistics->fds_disabled = false;
	statistics->fds_recall_count++;

	/* Update delegation stats for client. */
	atomic_dec_uint32_t(&client->cid_deleg_stats.curr_deleg_grants);

	/* Update delegation stats for client-file. */
	statistics->fds_avg_hold = advance_avg(statistics->fds_avg_hold,
					   time(NULL)
					   - statistics->fds_last_delegation,
					   statistics->fds_recall_count - 1,
					   statistics->fds_recall_count);
	return true;
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
	statistics->fds_disabled = false;
	statistics->fds_delegation_count = 0;
	statistics->fds_recall_count = 0;
	statistics->fds_last_delegation = 0;
	statistics->fds_last_recall = 0;
	statistics->fds_avg_hold = 0;
	statistics->fds_num_opens = 0;
	statistics->fds_first_open = 0;

	return true;
}

/**
 * @brief Decide if a delegation should be granted based on heuristics.
 *
 * Decide if a delegation should be granted based on heuristics.
 * Note: Whether the export supports delegations should be checked before
 * calling this function.
 * The open_state->state_type will decide whether we attempt to get a READ or
 * WRITE delegation.
 *
 * @param[in] entry Inode entry the delegation will be on.
 * @param[in] client Client that would own the delegation.
 * @param[in] open_state The open state for the inode to be delegated.
 */
bool should_we_grant_deleg(cache_entry_t *entry, nfs_client_id_t *client,
			   state_t *open_state)
{
	/* specific file, all clients, stats */
	struct file_deleg_stats *file_stats =
		&entry->object.file.fdeleg_stats;
	/* specific client, all files stats */
	struct c_deleg_stats *cl_stats = &client->cid_deleg_stats;
	/* specific client, specific file stats */
	float ACCEPTABLE_FAILS = 0.1; /* 10% */
	float ACCEPTABLE_OPEN_FREQUENCY = .01; /* per second */
	time_t spread;

	LogDebug(COMPONENT_STATE, "Checking if we should grant delegation.");
	return true;
	if (open_state->state_type != STATE_TYPE_SHARE) {
		LogDebug(COMPONENT_STATE,
			 "expects a SHARE open state and no other.");
		return false;
	}

	/* Check if this file is opened too frequently to delegate. */
	spread = time(NULL) - file_stats->fds_first_open;
	if (spread != 0 &&
	     (file_stats->fds_num_opens / spread) > ACCEPTABLE_OPEN_FREQUENCY) {
		LogDebug(COMPONENT_STATE, "This file is opened too frequently"
			 " to delegate.");
		return false;
	}

	/* Check if open state and requested delegation agree. */
	if (file_stats->fds_curr_delegations > 0) {
		if (file_stats->fds_deleg_type == OPEN_DELEGATE_READ &&
		    open_state->state_data.share.share_access &
		    OPEN4_SHARE_ACCESS_WRITE) {
			LogMidDebug(COMPONENT_STATE,
				    "READ delegate requested, but file is opened for WRITE.");
			return false;
		}
		if (file_stats->fds_deleg_type == OPEN_DELEGATE_WRITE &&
		    !(open_state->state_data.share.share_access &
		      OPEN4_SHARE_ACCESS_WRITE)) {
			LogMidDebug(COMPONENT_STATE,
				    "WRITE delegate requested, but file is not opened for WRITE.");
		}
	}

	/* Check if this is a misbehaving or unreliable client */
	if (cl_stats->tot_recalls > 0 &&
	    ((1.0 - (cl_stats->failed_recalls / cl_stats->tot_recalls))
	     > ACCEPTABLE_FAILS)) {
		LogDebug(COMPONENT_STATE,
			 "Client is %.0f unreliable during recalls. Allowed failure rate is %.0f. Denying delegation.",
			 1.0 - (cl_stats->failed_recalls
				/ cl_stats->tot_recalls),
			 ACCEPTABLE_FAILS);
		return false;
	}
	/* minimum average milliseconds that delegations should be held on a
	   file. if less, then this is not a good file for delegations. */
#define MIN_AVG_HOLD 1500
	if (file_stats->fds_avg_hold < MIN_AVG_HOLD
	    && file_stats->fds_avg_hold != 0) {
		LogDebug(COMPONENT_STATE, "Average length of delegation (%lld) "
			 "is less than minimum avg (%lld). Denying delegation.",
			 (long long) file_stats->fds_avg_hold,
			 (long long) MIN_AVG_HOLD);
		return false;
	}

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
state_status_t deleg_revoke(state_lock_entry_t *deleg_entry)
{
	state_status_t state_status;
	cache_entry_t *pentry;
	state_owner_t *clientowner;
	fsal_lock_param_t lock_desc;
	state_t *deleg_state;
	struct nfs_client_id_t *clid;
	nfs_fh4 fhandle;
	struct root_op_context root_op_context;

	clid = deleg_entry->sle_owner->so_owner.so_nfs4_owner.so_clientrec;
	deleg_state = deleg_entry->sle_state;
	clientowner = deleg_entry->sle_owner;
	pentry = deleg_entry->sle_entry;

	/* Allocation of a new file handle */
	if (nfs4_AllocateFH(&fhandle) != NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4_LOCK, "nfs4_AllocateFH failed");
		return NFS4ERR_SERVERFAULT;
	}

	/* Building a new fh ; Ignore return code, should not fail*/
	(void) nfs4_FSALToFhandle(&fhandle,
				  pentry->obj_handle,
				  deleg_state->state_export);

	lock_desc.lock_type = FSAL_LOCK_R;  /* doesn't matter for unlock */
	lock_desc.lock_start = 0;
	lock_desc.lock_length = 0;
	lock_desc.lock_sle_type = FSAL_LEASE_LOCK;

	deleg_heuristics_recall(deleg_entry);

	/* Build op_context for state_unlock_locked */
	init_root_op_context(&root_op_context, NULL, NULL, 0, 0,
			     UNKNOWN_REQUEST);
	root_op_context.req_ctx.clientid = &deleg_entry->sle_owner->
				so_owner.so_nfs4_owner.so_clientid;
	root_op_context.req_ctx.export = deleg_state->state_export;
	root_op_context.req_ctx.fsal_export =
			deleg_state->state_export->fsal_export;

	state_status = state_unlock_locked(pentry, clientowner, deleg_state,
					   &lock_desc, deleg_entry->sle_type);

	release_root_op_context();

	if (state_status != STATE_SUCCESS) {
		LogDebug(COMPONENT_NFS_V4_LOCK, "state unlock failed: %d",
			 state_status);
	}

	/* Put the revoked delegation on the stable storage. */
	nfs4_record_revoke(clid, &fhandle);
	state_del_locked(deleg_state, pentry);

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
void state_deleg_revoke(state_t *state, cache_entry_t *entry)
{
	struct glist_head *glist;
	state_lock_entry_t *deleg_lock;

	/* If we are already in the process of recalling or revoking
	 * this delegation from elsewhere, skip it here.
	 */
	if (state->state_data.deleg.sd_state != DELEG_GRANTED)
		return;

	state->state_data.deleg.sd_state = DELEG_RECALL_WIP;

	/* Find the delegation lock and revoke it */
	glist_for_each(glist, &entry->object.file.deleg_list) {
		deleg_lock = glist_entry(glist, state_lock_entry_t, sle_list);
		if (deleg_lock->sle_state == state) {
			(void)deleg_revoke(deleg_lock);
			return;
		}
	}

	/* delegation states and delegation locks have a one-to-one
	 * correspondence. They get created and destroyed at the same
	 * time. The exception is, while removing an export all locks
	 * including delegation locks are removed without their
	 * corresponding delegation states. So we get here, for sure,
	 * when an export is removed! Since the delegation lock is
	 * already removed, just remove the delegation state here!
	 *
	 * TODO:
	 * There is no reason for delegation lock structures. They can
	 * be completely abstracted out inside delegation state itself.
	 */
	state_del_locked(state, entry);
}
