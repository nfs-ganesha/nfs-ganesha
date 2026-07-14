// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "export_mgr.h"
#include "nfs_rpc_callback.h"
#include "server_stats.h"
#include "fsal_up.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "fsal_convert.h"

/* Keeps track of total number of files delegated */
int32_t g_total_num_files_delegated;
int32_t g_max_files_delegatable;

/* Initialize the global revoked list head */
static struct glist_head revoked_delegations_list =
	GLIST_HEAD_INIT(revoked_delegations_list);

/* Mutex to protect list */
static pthread_mutex_t revoked_delegations_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Initialize new delegation state as argument for state_add()
 *
 * Initialize delegation state struct. This is then given as an argument
 * to state_add()
 *
 * @param[in/out] deleg_state Delegation state struct to be init. Can't be NULL.
 * @param[in] sd_type Type of delegation, READ or WRITE.
 * @param[in] client The client that will own this delegation.
 */
void init_new_deleg_state(union state_data *deleg_state,
			  open_delegation_type4 deleg_type,
			  nfs_client_id_t *client)
{
	struct cf_deleg_stats *clfile_entry =
		&deleg_state->deleg.sd_clfile_stats;

	deleg_state->deleg.sd_type = deleg_type;
	deleg_state->deleg.sd_state = DELEG_GRANTED;

	clfile_entry->cfd_rs_time = 0;
	clfile_entry->cfd_r_time = 0;
}

/**
 * @brief Perform a lease lock operation
 *
 * We do state management and call down to the FSAL as appropriate, so
 * that the caller has a single entry point.
 *
 * @param[in]  obj      File on which to operate
 * @param[in]  lock_op  Operation to perform
 * @param[in]  owner    Lock operation
 * @param[in]  lock     Lock description
 *
 * @return State status.
 */
state_status_t do_lease_op(struct fsal_obj_handle *obj, state_t *state,
			   state_owner_t *owner, fsal_deleg_t deleg)
{
	fsal_status_t fsal_status;
	state_status_t status;

	/* Perform this delegation operation using the new
	 * multiple file-descriptors.
	 */
	fsal_status = obj->obj_ops->lease_op2(obj, state, owner, deleg);

	status = state_error_convert(fsal_status);

	LogFullDebug(COMPONENT_STATE, "FSAL lease_op2 returned %s",
		     state_err_str(status));

	return status;
}

/**
 * @brief Attempt to acquire a lease lock (delegation)
 *
 * @note The st_lock MUST be held
 *
 * @param[in]  ostate     File state to get lease lock on
 * @param[in]  owner      Owner for the lease lock
 * @param[in]  state      Associated state for the lock
 */
state_status_t acquire_lease_lock(struct state_hdl *ostate,
				  state_owner_t *owner, state_t *state)
{
	state_status_t status;
	fsal_deleg_t deleg = FSAL_DELEG_RD;

	/* Now recognizes OPEN_DELEGATE_WRITE_ATTRS_DELEG as a write
	 * delegation too for FSAL operations.
	 */
	if (state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE ||
	    state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE_ATTRS_DELEG)
		deleg = FSAL_DELEG_WR;

	/* Create a new deleg data object */
	status = do_lease_op(ostate->file.obj, state, owner, deleg);

	if (status == STATE_SUCCESS) {
		update_delegation_stats(ostate, owner);
		reset_cbgetattr_stats(ostate->file.obj);
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
 * st_lock must be held while calling this function
 */
state_status_t release_lease_lock(struct fsal_obj_handle *obj, state_t *state)
{
	state_status_t status;
	state_owner_t *owner = get_state_owner_ref(state);

	/* Something is going stale? */
	if (owner == NULL)
		return STATE_ESTALE;

	status = do_lease_op(obj, state, owner, FSAL_DELEG_NONE);
	if (status != STATE_SUCCESS)
		LogMajor(COMPONENT_STATE, "Unable to unlock FSAL, error=%s",
			 state_err_str(status));

	dec_state_owner_ref(owner);

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
void update_delegation_stats(struct state_hdl *ostate, state_owner_t *owner)
{
	nfs_client_id_t *client = owner->so_owner.so_nfs4_owner.so_clientrec;

	/* Update delegation stats for file. */
	struct file_deleg_stats *statistics = &ostate->file.fdeleg_stats;

	statistics->fds_curr_delegations++;
	statistics->fds_delegation_count++;
	statistics->fds_last_delegation = time(NULL);

	/* Update delegation stats for client. */
	inc_grants(client->gsh_client);
	client->curr_deleg_grants++;
}

/* Add a new delegation length to the average length stat. */
static int advance_avg(time_t prev_avg, time_t new_time, uint32_t prev_tot,
		       uint32_t curr_tot)
{
	return ((prev_tot * prev_avg) + new_time) / curr_tot;
}

/*
 * @brief reset cbgetattr struct args
 */
void reset_cbgetattr_stats(struct fsal_obj_handle *obj)
{
	cbgetattr_t *cbgetattr = &obj->state_hdl->file.cbgetattr;

	cbgetattr->state = CB_GETATTR_NONE;
	cbgetattr->modified = false;
}

/**
 * @brief Update statistics on successfully recalled delegation.
 *
 * Update statistics on successfully recalled delegation.
 * Note: This should be called only when a delegation is successfully recalled.
 *
 * @param[in] deleg Delegation state
 */
void deleg_heuristics_recall(struct fsal_obj_handle *obj, state_owner_t *owner,
			     struct state_t *deleg)
{
	nfs_client_id_t *client = owner->so_owner.so_nfs4_owner.so_clientrec;
	/* Update delegation stats for file. */
	struct file_deleg_stats *statistics =
		&obj->state_hdl->file.fdeleg_stats;

	statistics->fds_curr_delegations--;
	statistics->fds_recall_count++;

	/* Reset the delegation type if no active delegation present. */
	if (statistics->fds_curr_delegations == 0) {
		LogFullDebug(
			COMPONENT_STATE,
			"Resetting Deleg type (%d/%d) as file has no delegation",
			statistics->fds_curr_delegations,
			statistics->fds_deleg_type);
		statistics->fds_deleg_type = OPEN_DELEGATE_NONE;
		DEC_G_Total_Num_Files_Delegated(
			statistics->fds_curr_delegations);
	}

	/* Update delegation stats for client. */
	dec_grants(client->gsh_client);
	client->curr_deleg_grants--;

	/* Update delegation stats for file. */
	statistics->fds_avg_hold =
		advance_avg(statistics->fds_avg_hold,
			    time(NULL) - statistics->fds_last_delegation,
			    statistics->fds_recall_count - 1,
			    statistics->fds_recall_count);
}

/**
 * @brief Initialize the file-specific delegation statistics
 *
 * Initialize the file-specific delegation statistics used later for deciding
 * if a delegation should be granted on this file based on heuristics.
 *
 * @param[in] obj  File the delegation will be on.
 */
bool init_deleg_heuristics(struct fsal_obj_handle *obj)
{
	struct file_deleg_stats *statistics;

	if (obj->type != REGULAR_FILE) {
		LogCrit(COMPONENT_STATE,
			"Initialization of delegation stats for an obj that is NOT a regular file!");
		return false;
	}

	statistics = &obj->state_hdl->file.fdeleg_stats;
	statistics->fds_curr_delegations = 0;
	statistics->fds_deleg_type = OPEN_DELEGATE_NONE;
	statistics->fds_delegation_count = 0;
	statistics->fds_recall_count = 0;
	statistics->fds_last_delegation = 0;
	statistics->fds_last_recall = 0;
	statistics->fds_avg_hold = 0;
	statistics->fds_num_opens = 0;
	statistics->fds_first_open = 0;
	statistics->fds_num_write_opens = 0;

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
 * @note The st_lock MUST be held
 *
 * @param[in] ostate File state the delegation will be on.
 * @param[in] client The client that would own the delegation.
 * @param[in] open_state The open state for the inode to be delegated.
 * @param[in/out] resok pointer to resok (for setting ond_why, primarily)
 * @param[in] owner state owner
 * @param[out] prerecall flag for reclaims.
 */
bool should_we_grant_deleg(struct state_hdl *ostate, nfs_client_id_t *client,
			   state_t *open_state, OPEN4args *args,
			   OPEN4resok *resok, state_owner_t *owner,
			   bool *prerecall)
{
	/* specific file, all clients, stats */
	struct file_deleg_stats *file_stats = &ostate->file.fdeleg_stats;
	/* specific client, all files stats */
	open_claim_type4 claim = args->claim.claim;

	LogDebug(COMPONENT_STATE, "Checking if we should grant delegation.");

	assert(open_state->state_type == STATE_TYPE_SHARE);

	*prerecall = false;
	if (!nfs_param.nfsv4_param.allow_delegations ||
	    !op_ctx->fsal_export->exp_ops.fs_supports(op_ctx->fsal_export,
						      fso_delegations_r) ||
	    !(op_ctx->export_perms.options & EXPORT_OPTION_DELEGATIONS) ||
	    (!owner->so_owner.so_nfs4_owner.so_confirmed &&
	     claim == CLAIM_NULL) ||
	    claim == CLAIM_DELEGATE_CUR) {
		LogFullDebug(
			COMPONENT_STATE,
			"Not granting delegation: failed initial capability/claim checks");
		resok->delegation.open_delegation4_u.od_whynone.ond_why =
			WND4_NOT_SUPP_FTYPE;
		return false;
	}

	/* set the pre-recall flag for reclaims if the server does not want the
	 * delegation to remain in force */
	if (get_cb_chan_down(client)) {
		switch (claim) {
		case CLAIM_PREVIOUS:
			*prerecall = true;
			if (args->claim.open_claim4_u.delegate_type ==
			    OPEN_DELEGATE_NONE)
				LogFullDebug(
					COMPONENT_STATE,
					"Not granting delegation: callback channel down and CLAIM_PREVIOUS has OPEN_DELEGATE_NONE");
			return args->claim.open_claim4_u.delegate_type ==
					       OPEN_DELEGATE_NONE
				       ? false
				       : true;
		case CLAIM_DELEGATE_PREV:
			*prerecall = true;
			return true;
		default:
			LogFullDebug(
				COMPONENT_STATE,
				"Not granting delegation: callback channel down and claim=%u is not reclaim-compatible",
				(unsigned int)claim);
			resok->delegation.open_delegation4_u.od_whynone.ond_why =
				WND4_RESOURCE;
			return false;
		}
	} else {
		*prerecall = false;
		switch (claim) {
		case CLAIM_PREVIOUS:
			if (args->claim.open_claim4_u.delegate_type ==
			    OPEN_DELEGATE_NONE)
				LogFullDebug(
					COMPONENT_STATE,
					"Not granting delegation: CLAIM_PREVIOUS has OPEN_DELEGATE_NONE");
			return args->claim.open_claim4_u.delegate_type ==
					       OPEN_DELEGATE_NONE
				       ? false
				       : true;
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
	    time(NULL) - file_stats->fds_last_recall < RECALL2DELEG_TIME) {
		LogFullDebug(
			COMPONENT_STATE,
			"Not granting delegation: recent recall for file (last_recall=%ld, threshold=%d)",
			(long)file_stats->fds_last_recall, RECALL2DELEG_TIME);
		resok->delegation.open_delegation4_u.od_whynone.ond_why =
			WND4_CONTENTION;
		return false;
	}

	/* Check if this is a misbehaving or unreliable client */
	if (client->num_revokes > 2) { /* more than 2 revokes */
		LogFullDebug(COMPONENT_STATE,
			     "Client has too many revokes (num_revokes=%" PRIu32
			     ")",
			     client->num_revokes);
		resok->delegation.open_delegation4_u.od_whynone.ond_why =
			WND4_RESOURCE;
		return false;
	}

	/* If some client is holding a write file descriptor donot
	 * delegate. OPEN4_SHARE_ACCESS_READ and OPEN4_SHARE_ACCESS_WRITE
	 * are handled differently because if we are currently handling
	 * OPEN4_SHARE_ACCESS_WRITE then we would have incremented the
	 * counter before calling this function.
	 *
	 * Previously, clients requesting both READ and WRITE access were
	 * treated as readers and failed the read-only contention check,
	 * resulting in no delegation being granted. To correct this,
	 * we now exclude such mixed-access opens from the read-only
	 * contention path so that they are treated like WRITE opens and
	 * can receive a write delegation when eligible.
	 */
	if ((args->share_access & OPEN4_SHARE_ACCESS_READ &&
	     !(args->share_access & OPEN4_SHARE_ACCESS_WRITE) &&
	     file_stats->fds_num_write_opens > 0) ||
	    (args->share_access & OPEN4_SHARE_ACCESS_WRITE &&
	     file_stats->fds_num_write_opens > 1)) {
		LogFullDebug(COMPONENT_STATE,
			     "Conflicting write opens (share_access=0x%" PRIx32
			     ", write_opens=%" PRIu32 ")",
			     args->share_access,
			     file_stats->fds_num_write_opens);
		resok->delegation.open_delegation4_u.od_whynone.ond_why =
			WND4_CONTENTION;
		return false;
	}

	/* Deny delegations on this file if this is the first delegation
	 * and we have reached the maximum number of files we can delegate
	 * We have reached the maximum number of files we can delegate
	 * if atomic_add_unless below returns false
	 */
	if ((file_stats->fds_curr_delegations == 0) &&
	    !atomic_add_unless_int32_t(&g_total_num_files_delegated, 1,
				       g_max_files_delegatable)) {
		LogFullDebug(
			COMPONENT_STATE,
			"Can't delegate file since Files_Delegatable_Percent limit is hit");
		resok->delegation.open_delegation4_u.od_whynone.ond_why =
			WND4_RESOURCE;
		return false;
	}
	LogFullDebug(COMPONENT_STATE, "total_num_files_delegated is %d",
		     atomic_fetch_int32_t(&g_total_num_files_delegated));

	LogDebug(COMPONENT_STATE, "Let's delegate!!");
	return true;
}

/**
 * @brief Form the ACE mask for the delegated file.
 *
 * Form the ACE mask for the delegated file.
 *
 * @param[in,out] permissions ACE mask for delegated inode.
 * @param[in] type The type of delegation. Either READ or WRITE.
 */
void get_deleg_perm(nfsace4 *permissions, open_delegation_type4 type)
{
	/* We need to create an access_mask that shows who
	 * can OPEN this file.
	 *
	 * Now it also handles new ATTRS_DELEG types when
	 * setting permissions.
	 */
	if (type == OPEN_DELEGATE_WRITE ||
	    type == OPEN_DELEGATE_WRITE_ATTRS_DELEG)
		;
	else if (type == OPEN_DELEGATE_READ ||
		 type == OPEN_DELEGATE_READ_ATTRS_DELEG)
		;
	permissions->type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
	permissions->flag = 0;
	permissions->access_mask = 0;
	permissions->who.utf8string_len = 0;
	permissions->who.utf8string_val = NULL;
}

/**
 * @brief Check if revoked delegations exist for a client
 *
 * This function checks the global revoked delegation list to see if
 * there are any revoked delegations present.
 *
 * @param[in]  clientid  Pointer to the NFS client identifier
 *
 * @return true if revoked delegations exist in the list, false otherwise
 */
bool has_revoked_delegations_for_client(nfs_client_id_t *clientid)
{
	struct glist_head *glist;
	bool found = false;

	PTHREAD_MUTEX_lock(&revoked_delegations_lock);
	glist_for_each(glist, &revoked_delegations_list) {
		found = true;
		break;
	}
	PTHREAD_MUTEX_unlock(&revoked_delegations_lock);

	LogDebug(COMPONENT_STATE,
		 "Has revoked delegations for client: client 0x%llx, found=%s",
		 (unsigned long long)clientid->cid_clientid,
		 found ? "true" : "false");
	return found;
}

/**
 * @brief Helper function called in OP_FREE_STATEID for cleaning.
 *
 * Atomically remove a revoked stateid and clear session flags.
 *
 * This function removes a revoked delegation stateid from the global revoked
 * list while holding the revoked_delegations_lock to prevent races. If no
 * revoked delegations remain for the client, it also clears the revoked flag
 * on all sessions for that client under the client cid_mutex.
 *
 * For NFSv4.0 clients (no sessions), the stateid is just removed without
 * session flag handling.
 *
 * @param[in] stateid  The stateid to remove from the revoked delegation list.
 * @param[in] clientid The client for which to check and clear session flags.
 *
 * @return true if session flags were cleared, false otherwise.
 */
bool atomic_remove_revoked_and_clear_flags(const stateid4 *stateid,
					   nfs_client_id_t *clientid)
{
	bool has_revoked = false;
	bool flags_cleared = false;
	struct glist_head *glist, *glist_next;

	if (clientid != NULL && clientid->cid_minorversion == 0) {
		/* NFSv4.0 doesn't have sessions */
		remove_revoked_stateid(stateid);
		return false;
	}

	/* Acquire the lock and remove the stateid from the list */
	PTHREAD_MUTEX_lock(&revoked_delegations_lock);

	glist_for_each_safe(glist, glist_next, &revoked_delegations_list) {
		struct revoked_delegation *revoked =
			glist_entry(glist, struct revoked_delegation, list);
		if (memcmp(&revoked->stateid, stateid, sizeof(stateid4)) == 0) {
			glist_del(&revoked->list);
			gsh_free(revoked, MEM_COMP_STATE);
			LogDebug(COMPONENT_STATE, "Removed revoked stateid");
			break;
		}
	}

	/* Check if any revoked delegations remain */
	if (!glist_empty(&revoked_delegations_list))
		has_revoked = true;

	if (!has_revoked && clientid != NULL) {
		struct glist_head *session_glist;
		int session_count = 0;

		/* Acquire client mutex and clear the session flags */
		PTHREAD_MUTEX_lock(&clientid->cid_mutex);

		LogDebug(COMPONENT_STATE,
			 "Clearing session flags for client 0x%llx",
			 (unsigned long long)clientid->cid_clientid);

		glist_for_each(session_glist,
			       &clientid->cid_cb.v41.cb_session_list) {
			nfs41_session_t *session = glist_entry(session_glist,
							       nfs41_session_t,
							       session_link);
			session->has_revoked_delegations = false;
			session_count++;
			LogDebug(COMPONENT_STATE, "Cleared flag for session %p",
				 session);
		}

		LogDebug(COMPONENT_STATE,
			 "Cleared flag for %d sessions of client 0x%llx",
			 session_count,
			 (unsigned long long)clientid->cid_clientid);

		flags_cleared = true;
		PTHREAD_MUTEX_unlock(&clientid->cid_mutex);
	} else if (has_revoked && clientid != NULL) {
		LogDebug(COMPONENT_STATE,
			 "Keeping session flags for client 0x%llx",
			 (unsigned long long)clientid->cid_clientid);
	}

	PTHREAD_MUTEX_unlock(&revoked_delegations_lock);

	return flags_cleared;
}

/**
 * @brief Mark client sessions as having revoked delegations
 *
 * This function sets the has_revoked_delegations flag to true for all
 * sessions belonging to the specified client.
 *
 * @param[in] clientid  Pointer to the NFS client identifier
 *
 * @return void
 */
void mark_sessions_have_revoked_delegations(nfs_client_id_t *clientid)
{
	struct glist_head *glist;
	nfs41_session_t *session;

	if (clientid->cid_minorversion == 0) {
		/* NFSv4.0 doesn't have sessions */
		LogDebug(COMPONENT_STATE,
			 "NFSv4.0 client, no sessions to mark");
		return;
	}

	PTHREAD_MUTEX_lock(&clientid->cid_mutex);
	glist_for_each(glist, &clientid->cid_cb.v41.cb_session_list) {
		session = glist_entry(glist, nfs41_session_t, session_link);
		session->has_revoked_delegations = true;
	}
	PTHREAD_MUTEX_unlock(&clientid->cid_mutex);

	LogDebug(COMPONENT_STATE, "Marked the sessions for client 0x%llx",
		 (unsigned long long)clientid->cid_clientid);
}

/**
 * @brief Remove a revoked delegation stateid from the revoked list.
 *
 * This function searches the global revoked_delegations_list for the given
 * stateid and removes it if found, freeing the corresponding tracking entry.
 *
 * It is typically called when the client has acknowledged the revocation
 * and sent an OP_FREE_STATEID for the revoked delegation, meaning the
 * server no longer needs to track it as revoked.
 *
 * @param[in] stateid Pointer to the stateid to be removed from the list.
 */
void remove_revoked_stateid(const stateid4 *stateid)
{
	struct glist_head *pos, *tmp;
	struct revoked_delegation *entry;
	bool found = false;

	PTHREAD_MUTEX_lock(&revoked_delegations_lock);
	glist_for_each_safe(pos, tmp, &revoked_delegations_list) {
		entry = glist_entry(pos, struct revoked_delegation, list);
		if (memcmp(&entry->stateid, stateid, sizeof(stateid4)) == 0) {
			glist_del(pos);
			free(entry);
			found = true;
			LogDebug(COMPONENT_STATE,
				 "Removed revoked delegation stateid from list");
			break;
		}
	}
	if (!found) {
		LogDebug(
			COMPONENT_STATE,
			"Did not find revoked delegation stateid in revoked list");
	}
	PTHREAD_MUTEX_unlock(&revoked_delegations_lock);
}

/**
 * @brief Check if a given stateid corresponds to a revoked delegation.
 *
 * Searches the revoked_delegations_list to determine if the provided
 * stateid has previously been marked as revoked. This is primarily
 * used by stateid validation logic (nfs4_Check_Stateid) which gets
 * called in I/O paths to decide whether to return NFS4ERR_DELEG_REVOKED
 * to the client.
 *
 * @param[in] stateid Pointer to the stateid to check.
 *
 * @return true  If the stateid is found in the revoked list.
 * @return false Otherwise.
 */
bool is_stateid_revoked(const stateid4 *stateid)
{
	bool found = false;
	struct glist_head *pos;
	struct revoked_delegation *entry;

	PTHREAD_MUTEX_lock(&revoked_delegations_lock);
	glist_for_each(pos, &revoked_delegations_list) {
		entry = glist_entry(pos, struct revoked_delegation, list);
		if (memcmp(&entry->stateid, stateid, sizeof(stateid4)) == 0) {
			found = true;
			break;
		}
	}
	PTHREAD_MUTEX_unlock(&revoked_delegations_lock);

	return found;
}

/**
 * @brief Add a delegation state to the revoked delegations list.
 *
 * Allocates and populates a revoked_delegation entry from the provided
 * state object and appends it to the global revoked_delegations_list.
 *
 * This list is later used in is_stateid_revoked() to return
 * NFS4ERR_DELEG_REVOKED for any I/O or state ops referring to such a
 * revoked delegation. The entry remains until explicitly removed by
 * remove_revoked_stateid().
 *
 * @param[in] state Pointer to the delegation state to be marked revoked.
 */
static void add_to_revoked_delegations(state_t *state)
{
	struct revoked_delegation *entry =
		gsh_malloc(sizeof(*entry), MEM_COMP_STATE);

	if (!entry) {
		LogCrit(COMPONENT_NFS_V4,
			"Out of memory: cannot add revoked delegation");
		return;
	}

	entry->stateid.seqid = state->state_seqid;
	memcpy(entry->stateid.other, state->stateid_other,
	       sizeof(entry->stateid.other));

	PTHREAD_MUTEX_lock(&revoked_delegations_lock);
	glist_add(&revoked_delegations_list, &entry->list);
	PTHREAD_MUTEX_unlock(&revoked_delegations_lock);

	LogDebug(
		COMPONENT_NFS_V4,
		"Added revoked delegation stateid seqid=%u other=%*phN to revoked list",
		entry->stateid.seqid, (int)sizeof(entry->stateid.other),
		entry->stateid.other);
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
nfsstat4 deleg_revoke(struct fsal_obj_handle *obj, struct state_t *deleg_state)
{
	state_status_t state_status;
	struct nfs_client_id_t *clid;
	nfs_fh4 fhandle;
	struct req_op_context op_context;
	struct gsh_export *export;
	state_owner_t *owner;

	/* Get reference to owner and export. Onwer reference also protects
	 * the clientid.
	 */
	if (!get_state_obj_export_owner_refs(deleg_state, NULL, &export,
					     &owner)) {
		/* Something is going stale. */
		LogDebug(COMPONENT_STATE, "Stale state, owner, or export");
		return NFS4ERR_STALE;
	}

	clid = owner->so_owner.so_nfs4_owner.so_clientrec;

	/* Building a new fh ; Ignore return code, should not fail*/
	(void)nfs4_FSALToFhandle(true, &fhandle, obj, export);

	deleg_heuristics_recall(obj, owner, deleg_state);
	reset_cbgetattr_stats(obj);

	/* Build op_context for state_unlock_locked */
	init_op_context_simple(&op_context, export, export->fsal_export);
	op_ctx->clientid = &clid->cid_clientid;

	LogDebug(COMPONENT_STATE,
		 "Revoking delegation %p for client %p on object %p",
		 deleg_state, clid, obj);

	/* Adding the delegation state to a list that is being revoked
	 * which will be cleaned up when client sends OP_FREE_STATEID
	 */
	add_to_revoked_delegations(deleg_state);

	/* release_lease_lock() returns delegation to FSAL */
	state_status = release_lease_lock(obj, deleg_state);

	if (state_status != STATE_SUCCESS) {
		LogDebug(COMPONENT_STATE, "state unlock failed: %d",
			 state_status);
	}

	/* Put the revoked delegation on the stable storage. */
	nfs4_record_revoke(clid, &fhandle);

	LogFullDebug(
		COMPONENT_STATE,
		"Marking sessions for client 0x%llx as having revoked delegations",
		(unsigned long long)clid->cid_clientid);

	/* Mark sessions as having revoked delegations which will be
	 * cleaned up when client sends OP_FREE_STATEID
	 */
	mark_sessions_have_revoked_delegations(clid);

	state_del_locked(deleg_state);

	gsh_free(fhandle.nfs_fh4_val, MEM_COMP_PROTOCOL);

	/* Release references taken above */
	dec_state_owner_ref(owner);
	release_op_context();

	return NFS4_OK;
}

/**
 * @brief Mark the delegation revoked
 *
 * Mark the delegation state revoked, further ops on this state should
 * return NFS4ERR_REVOKED or NFS4ERR_EXPIRED
 *
 * @note The st_lock MUST be held
 *
 * @param[in] obj   File
 * @param[in] state Delegation state
 */
void state_deleg_revoke(struct fsal_obj_handle *obj, state_t *state)
{
	/* If we are already in the process of recalling or revoking
	 * this delegation from elsewhere, skip it here.
	 */
	if (state->state_data.deleg.sd_state != DELEG_GRANTED)
		return;

	state->state_data.deleg.sd_state = DELEG_RECALL_WIP;

	(void)deleg_revoke(obj, state);
}

/**
 * @brief Check if the file is write delegated under st_lock
 *
 * Check if the file is write delegated. If yes, take a ref and return
 * the client holding the delegation.
 *
 * @note: The caller should acquire st_lock before calling this
 * function.
 *
 * @param[in] obj File
 * @param[out] client holding the delegation
 *
 * @retval true if file is write delegated
 * @retval false otherwise
 */
bool is_write_delegated(struct fsal_obj_handle *obj, nfs_client_id_t **client)
{
	bool write_delegated = false;
	struct file_deleg_stats *deleg_stats;

	if (obj->type != REGULAR_FILE)
		return false;

	deleg_stats = &obj->state_hdl->file.fdeleg_stats;

	if (deleg_stats->fds_curr_delegations == 0)
		return false;

	write_delegated = obj->state_hdl->file.write_delegated;
	if (write_delegated && client) {
		*client = obj->state_hdl->file.write_deleg_client;
		inc_client_id_ref(*client);
	}

	return write_delegated;
}

/**
 * @brief Check if an operation is conflicting with delegations.
 *
 * Check if an operation will conflict with current delegations on a file.
 * Return TRUE if there is a conflict and the delegations have been recalled.
 * Return FALSE if there is no conflict.
 *
 * @note The st_lock MUST be held
 *
 * @param[in] obj   File
 * @param[in] write a boolean indicating whether the operation will read or
 *            change the file.
 *
 * @retval true if there is a conflict and the delegations have been recalled.
 * @retval false if there is no delegation conflict.
 */
bool state_deleg_conflict_impl(struct fsal_obj_handle *obj, bool write)
{
	/* Skip unnecessary checks if delegation is disabled
	 * in the NFSv4 stanza.
	 */
	if (!nfs_param.nfsv4_param.allow_delegations)
		return false;

	struct file_deleg_stats *deleg_stats;
	const uint64_t *current_clientid = NULL;
	struct glist_head *glist;
	state_t *state;
	/* true when current op’s client already holds a delegation */
	bool same_client_has_deleg = false;
	/* true when another client holds a WRITE delegation */
	bool other_client_has_write = false;
	/* count of delegations that are still active */
	unsigned int observed_delegations = 0;

	deleg_stats = &obj->state_hdl->file.fdeleg_stats;

	/* Cache the current request’s clientid if available. We use it
	 * to decide whether a delegation belongs to the same client or
	 * a different one.
	 */
	if (op_ctx != NULL && op_ctx->clientid != NULL)
		current_clientid = op_ctx->clientid;

	/* Scan every delegation state under the file. We consider both
	 * “granted” and “recall-in-progress” delegations as active
	 * because they still block conflicting opens until the client
	 * returns them.
	 */
	if (deleg_stats->fds_curr_delegations > 0) {
		glist_for_each(glist, &obj->state_hdl->file.list_of_states) {
			state = glist_entry(glist, state_t, state_list);

			if (state->state_type != STATE_TYPE_DELEG)
				continue;

			if (state->state_data.deleg.sd_state != DELEG_GRANTED &&
			    state->state_data.deleg.sd_state !=
				    DELEG_RECALL_WIP)
				continue;

			nfs_client_id_t *state_client =
				state->state_owner->so_owner.so_nfs4_owner
					.so_clientrec;
			bool same_client = false;

			if (state_client != NULL && current_clientid != NULL &&
			    state_client->cid_clientid == *current_clientid)
				same_client = true;

			if (same_client)
				same_client_has_deleg = true;
			else if (state->state_data.deleg.sd_type ==
					 OPEN_DELEGATE_WRITE ||
				 state->state_data.deleg.sd_type ==
					 OPEN_DELEGATE_WRITE_ATTRS_DELEG)
				other_client_has_write = true;

			observed_delegations++;
		}
	}

	LogFullDebug(
		COMPONENT_STATE,
		"write=%d same_client_has_deleg=%d other_client_has_write=%d total=%u",
		write, same_client_has_deleg, other_client_has_write,
		observed_delegations);

	/* No delegations still active so no conflict. */
	if (observed_delegations == 0)
		return false;

	/* Write requests: allow the same client to proceed only when
	 * it is the sole delegate (DELEG23 scenario).
	 */
	if (write) {
		if (observed_delegations == 1 && same_client_has_deleg)
			return false;

		LogFullDebug(COMPONENT_STATE,
			     "Write request conflicts with existing delegation");
		goto recall;
	}

	/* Read requests conflict only when another client already
	 * holds a WRITE delegation.
	 */
	if (other_client_has_write) {
		LogFullDebug(
			COMPONENT_STATE,
			"Read request conflicts with other client's WRITE delegation");
		goto recall;
	}

	/* No conflict detected. */
	return false;

recall:
	if (async_delegrecall(general_fridge, obj) != 0)
		LogCrit(COMPONENT_STATE,
			"Failed to start thread to recall delegation from conflicting operation.");

	return true;
}
/**
 * @brief Acquire st_lock and check if an operation is conflicting
 *        with delegations.
 *
 * @param[in] obj   File
 * @param[in] write a boolean indicating whether the operation will read or
 *            change the file.
 *
 * @retval true if there is a conflict and the delegations have been recalled.
 * @retval false if there is no delegation conflict.
 */
bool state_deleg_conflict(struct fsal_obj_handle *obj, bool write)
{
	bool status = false;

	if (!nfs_param.nfsv4_param.allow_delegations)
		return false;

	/*
	 * Check the type before grabbing the lock. Which lock in state_hdl is
	 * valid depends on the object's type.
	 */
	if (obj->type != REGULAR_FILE)
		return false;

	STATELOCK_lock(obj);
	status = state_deleg_conflict_impl(obj, write);
	STATELOCK_unlock(obj);
	return status;
}

/*
 * @brief: fetch getattr from the write_delegated client
 *
 * Send CB_GETATTR to the write_delegated client to fetch
 * right attributes. If not recall delegation.
 *
 * @note: should be called under st_lock.
 */
nfsstat4 handle_deleg_getattr(struct fsal_obj_handle *obj,
			      nfs_client_id_t *client)
{
	nfsstat4 status = NFS4ERR_DELAY;
	int rc = 0;
	enum cbgetattr_state cb_state;

	/* Check for delegation conflict.*/
	LogDebug(
		COMPONENT_STATE,
		"While trying to perform a GETATTR op, found a conflicting WRITE delegation");

	/*
	 * @todo: Provide an option for user to enable CB_GETATTR
	 */

	cb_state = obj->state_hdl->file.cbgetattr.state;
	LogDebug(COMPONENT_STATE, "CB_GETATTR cb_state=%d for obj %p", cb_state,
		 obj);

	switch (cb_state) {
	case CB_GETATTR_RSP_OK:
		/* got response for CB_GETATTR */
		LogDebug(COMPONENT_STATE,
			 "CB_GETATTR already completed successfully");
		status = NFS4_OK;
		goto out;
	case CB_GETATTR_WIP:
		/* wait for response */
		LogDebug(COMPONENT_STATE,
			 "CB_GETATTR in progress, waiting for completion");
		goto out;
	case CB_GETATTR_FAILED:
		LogDebug(COMPONENT_STATE,
			 "CB_GETATTR failed, recalling delegation");
		goto deleg_recall;
	default: /* CB_GETATTR_NONE */
		LogDebug(COMPONENT_STATE,
			 "No CB_GETATTR sent yet, sending now");
		goto send_request;
	}
send_request:
	LogDebug(COMPONENT_STATE, "sending CB_GETATTR");
	rc = async_cbgetattr(general_fridge, obj, client);
	if (rc != 0) {
		LogCrit(COMPONENT_STATE,
			"Failed to start thread to send cb_getattr.");
		goto deleg_recall;
	}
	/* Return EDELAY per RFC until CB_GETATTR completes */
	status = NFS4ERR_DELAY;
	goto out;
deleg_recall:
	LogDebug(COMPONENT_STATE, "CB_GETATTR is either not enabled or failed,"
				  " recalling write delegation");
	rc = async_delegrecall(general_fridge, obj);
	if (rc != 0) {
		LogCrit(COMPONENT_STATE,
			"Failed to start thread to recall delegation from conflicting operation.");
		goto out;
	}

out:
	if (rc != 0) {
		status = nfs4_Errno_status(fsalstat(posix2fsal_error(rc), rc));
	}
	return status;
}

bool deleg_supported(struct fsal_obj_handle *obj,
		     struct fsal_export *fsal_export,
		     struct export_perms *export_perms, uint32_t share_access)
{
	if (!nfs_param.nfsv4_param.allow_delegations) {
		LogFullDebug(
			COMPONENT_STATE,
			"Delegation not supported: delegations disabled by server config");
		return false;
	}
	if (obj->type != REGULAR_FILE) {
		LogFullDebug(
			COMPONENT_STATE,
			"Delegation not supported: object type %s is not regular file",
			object_file_type_to_str(obj->type));
		return false;
	}

	/* In a read-write case, we handle write delegation. So we should
	 * check for OPEN4_SHARE_ACCESS_WRITE bit first!
	 */
	if (share_access & OPEN4_SHARE_ACCESS_WRITE) {
		if (!fsal_export->exp_ops.fs_supports(fsal_export,
						      fso_delegations_w)) {
			LogFullDebug(
				COMPONENT_STATE,
				"Delegation not supported: FSAL export lacks write delegation support");
			return false;
		}
		if (!(export_perms->options & EXPORT_OPTION_WRITE_DELEG)) {
			LogFullDebug(
				COMPONENT_STATE,
				"Delegation not supported: export permissions disable write delegations");
			return false;
		}
	} else {
		assert(share_access & OPEN4_SHARE_ACCESS_READ);
		if (!fsal_export->exp_ops.fs_supports(fsal_export,
						      fso_delegations_r)) {
			LogFullDebug(
				COMPONENT_STATE,
				"Delegation not supported: FSAL export lacks read delegation support");
			return false;
		}
		if (!(export_perms->options & EXPORT_OPTION_READ_DELEG)) {
			LogFullDebug(
				COMPONENT_STATE,
				"Delegation not supported: export permissions disable read delegations");
			return false;
		}
	}

	return true;
}

/**
 * @brief Check to see if a delegation can be granted
 *
 * @note The st_lock MUST be held
 *
 * @param[in] ostate	State to check
 * @return true if can grant, false otherwise
 */
bool can_we_grant_deleg(struct state_hdl *ostate, state_t *open_state)
{
	struct glist_head *glist;
	state_lock_entry_t *lock_entry;
	const struct state_share *share = &open_state->state_data.share;

	/* Can't grant delegation if there is an anonymous operation
	 * in progress
	 */
	if (atomic_fetch_uint32_t(&ostate->file.anon_ops) != 0) {
		LogFullDebug(
			COMPONENT_STATE,
			"Anonymous op in progress, not granting delegation");
		return false;
	}

	/* Check for conflicting NLM locks. Write delegation would conflict
	 * with any kind of NLM lock, and NLM write lock would conflict
	 * with any kind of delegation.
	 */
	glist_for_each(glist, &ostate->file.lock_list) {
		lock_entry = glist_entry(glist, state_lock_entry_t, sle_list);
		if (lock_entry->sle_lock.lock_type == FSAL_NO_LOCK)
			continue; /* no lock, skip */
		if (share->share_access & OPEN4_SHARE_ACCESS_WRITE ||
		    lock_entry->sle_lock.lock_type == FSAL_LOCK_W) {
			LogFullDebug(
				COMPONENT_STATE,
				"Conflicting NLM lock. Not granting delegation");
			return false;
		}
	}

	return true;
}
