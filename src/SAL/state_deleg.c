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
#include "nfs_rpc_callback.h"
#include "server_stats.h"

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

struct deleg_data *create_deleg_data(cache_entry_t *entry, state_t *state,
		state_owner_t *owner, struct gsh_export *export)
{
	struct deleg_data *deleg_data;

	deleg_data = gsh_malloc(sizeof(*deleg_data));
	memset(&deleg_data->dd_list, 0, sizeof(deleg_data->dd_list));
	deleg_data->dd_entry = entry;
	deleg_data->dd_state = state;
	deleg_data->dd_owner = owner;
	deleg_data->dd_export = export;
	deleg_data->dd_export_id = export->export_id;
	return deleg_data;
}

void destroy_deleg_data(struct deleg_data *deleg_data)
{
	assert(glist_null(&deleg_data->dd_list));
	gsh_free(deleg_data);
}

/**
 * @brief Remove a delegation from a list of delegations
 *
 * Remove a delegation from a list of delegations
 *
 * @param[in,out] entry   Cache entry on which to operate
 * @param[in]     owner   Client owner of delegation
 * @param[in]     state   Associated lock state
 *
 * entry's state_lock needs to be held in readwrite mode
 *
 * @return true iff the lock is removed.
 */
static bool remove_deleg_data(cache_entry_t *entry,
			      state_owner_t *owner,
			      state_t *state)
{
	struct deleg_data *deleg_data;
	struct glist_head *glist;

	glist_for_each(glist, &entry->object.file.deleg_list) {
		deleg_data = glist_entry(glist, struct deleg_data, dd_list);
		if (deleg_data->dd_state == state) {
			assert(deleg_data->dd_owner == owner);
			assert(state->state_owner == owner);
			glist_del(&deleg_data->dd_list);
			destroy_deleg_data(deleg_data);
			return true;
		}
	}

	return false;
}

/**
 * @brief Attempt to acquire a lease lock (delegation)
 *
 * @param[in]  entry      Entry to lock
 * @param[in]  owner      Lock owner
 * @param[in]  state      Associated state for the lock
 * @param[in]  lock       Lock description
 *
 * state_lock must be held while calling this function
 */
state_status_t acquire_lease_lock(cache_entry_t *entry, state_owner_t *owner,
				  state_t *state, fsal_lock_param_t *lock)
{
	struct deleg_data *deleg_data;
	state_status_t status;

	/* Create a new deleg data object */
	deleg_data = create_deleg_data(entry, state, owner, op_ctx->export);
	if (!deleg_data)
		return STATE_MALLOC_ERROR;

	status = do_lock_op(entry, FSAL_OP_LOCK, owner, lock, NULL, NULL, false,
			    LEASE_LOCK);

	if (status == STATE_SUCCESS) {
		/* Insert deleg data into delegation list */
		update_delegation_stats(deleg_data);
		glist_add_tail(&entry->object.file.deleg_list,
			       &deleg_data->dd_list);
	} else {
		LogDebug(COMPONENT_STATE, "Could not set lease, error=%s",
			 state_err_str(status));
		destroy_deleg_data(deleg_data);
	}

	return status;
}

/**
 * @brief Release a lease lock (delegation)
 *
 * @param[in] entry    File to unlock
 * @param[in] owner    Owner of lock
 * @param[in] state    Associated state
 * @param[in] lock     Lock description
 *
 * state_lock must be held while calling this function
 */
state_status_t release_lease_lock(cache_entry_t *entry, state_owner_t *owner,
				  state_t *state, fsal_lock_param_t *lock)
{
	state_status_t status;
	bool removed;

	assert(state && state->state_type == STATE_TYPE_DELEG);
	removed = remove_deleg_data(entry, owner, state);
	if (!removed) { /* Not found */
		LogWarn(COMPONENT_STATE,
			"Unlock success on delegation not found");
		return STATE_SUCCESS;
	}

	status = do_lock_op(entry, FSAL_OP_UNLOCK, owner, lock, NULL, NULL,
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
bool update_delegation_stats(struct deleg_data *deleg_entry)
{
	cache_entry_t *entry = deleg_entry->dd_entry;
	nfs_client_id_t *client = deleg_entry->dd_owner->so_owner.
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
 * @param[in] Delegation Lock Entry
 */
bool deleg_heuristics_recall(struct deleg_data *deleg_entry)
{
	cache_entry_t *entry = deleg_entry->dd_entry;
	nfs_client_id_t *client = deleg_entry->dd_owner->so_owner.
					so_nfs4_owner.so_clientrec;

	/* Update delegation stats for file. */
	struct file_deleg_stats *statistics = &entry->object.file.fdeleg_stats;

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
 * Note: Whether the export supports delegations should be checked before
 * calling this function.
 * The open_state->state_type will decide whether we attempt to get a READ or
 * WRITE delegation.
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
	    || !op_ctx->fsal_export->ops->fs_supports(
					op_ctx->fsal_export,
					fso_delegations)
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
state_status_t deleg_revoke(struct deleg_data *deleg_entry)
{
	state_status_t state_status;
	cache_entry_t *pentry;
	state_owner_t *clientowner;
	fsal_lock_param_t lock_desc;
	state_t *deleg_state;
	struct nfs_client_id_t *clid;
	nfs_fh4 fhandle;
	struct root_op_context root_op_context;

	clid = deleg_entry->dd_owner->so_owner.so_nfs4_owner.so_clientrec;
	deleg_state = deleg_entry->dd_state;
	clientowner = deleg_entry->dd_owner;
	pentry = deleg_entry->dd_entry;

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
	root_op_context.req_ctx.clientid = &deleg_entry->dd_owner->
				so_owner.so_nfs4_owner.so_clientid;
	root_op_context.req_ctx.export = deleg_state->state_export;
	root_op_context.req_ctx.fsal_export =
			deleg_state->state_export->fsal_export;

	/* release_lease_lock() returns delegation to FSAL */
	state_status = release_lease_lock(pentry, clientowner, deleg_state,
					  &lock_desc);

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
	struct deleg_data *deleg_data;

	/* If we are already in the process of recalling or revoking
	 * this delegation from elsewhere, skip it here.
	 */
	if (state->state_data.deleg.sd_state != DELEG_GRANTED)
		return;

	state->state_data.deleg.sd_state = DELEG_RECALL_WIP;

	/* Find the delegation lock and revoke it */
	glist_for_each(glist, &entry->object.file.deleg_list) {
		deleg_data = glist_entry(glist, struct deleg_data, dd_list);
		if (deleg_data->dd_state == state) {
			(void)deleg_revoke(deleg_data);
			return;
		}
	}
	LogFatal(COMPONENT_STATE,
		"Delegation state exists but not the delegation data object");
}
