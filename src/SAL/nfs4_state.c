/* vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file nfs4_state.c
 * @brief NFSv4 state functions.
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#include "log.h"
#include "hashtable.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "fsal.h"
#include "sal_functions.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"

pool_t *state_v4_pool;		/*< Pool for NFSv4 files's states */

#ifdef DEBUG_SAL
struct glist_head state_v4_all;
pthread_mutex_t all_state_v4_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * @brief Checks for a conflict between an existing delegation state and a
 * candidate state.
 *
 * @param[in] state      Existing state
 * @param[in] state_type Type of candidate state
 * @param[in] state_data Data for the candidate state
 *
 * @retval true if there is a conflict.
 * @retval false if no conflict has been found
 */
static bool check_deleg_conflict(state_t *state, state_type_t candidate_type,
				 state_data_t *candidate_data,
				 state_owner_t *candidate_owner)
{
	struct glist_head *glist;
	clientid4 candidate_clientid, deleg_clientid;
	state_lock_entry_t *found_lock;

	LogDebug(COMPONENT_STATE, "Checking for conflict!!");

	if (state == NULL || candidate_data == NULL)
		return true;

	deleg_clientid =
		state->state_data.deleg.clfile_stats.clientid->cid_clientid;
	candidate_clientid =
		candidate_owner->so_owner.so_nfs4_owner.so_clientid;

	if (state->state_type != STATE_TYPE_DELEG) {
		LogDebug(COMPONENT_STATE,
			 "ERROR: Non-delegation state found in delegation list!");
		return false;
	}

	/* We are getting a new share, checking if delegations conflict. */
	switch (candidate_type) {
	case STATE_TYPE_DELEG:
		/* This should not happen, but we'll see. */
		if (deleg_clientid == candidate_clientid) {
			LogDebug(COMPONENT_STATE,
				 "Requesting delegation for client that has a delegation on this file. no conflict");
			return false;
		}
		if (state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE) {
			LogDebug(COMPONENT_STATE,
				 "Getting a delegation when write delegation exists on different client conflict");
			return true;
		}
		if (candidate_data->deleg.sd_type
		    == OPEN_DELEGATE_WRITE) {
			LogDebug(COMPONENT_STATE,
				 "Getting a write delegation  when delegation exists on different client conflict");
			return true;
		}
		break;
	case STATE_TYPE_SHARE:
		if (deleg_clientid == candidate_clientid) {
			LogDebug(COMPONENT_STATE,
				 "New share state is for same client that owns delegation. no conflict.");
			return false;
		}

		if (state->state_data.deleg.sd_type == OPEN_DELEGATE_READ
		    && candidate_data->share.share_access
		    & OPEN4_SHARE_ACCESS_WRITE) {
			LogDebug(COMPONENT_STATE,
				 "Read delegation exists. New share is WRITE on different client. conflict");
			return true;
		}
		if (state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE) {
			LogDebug(COMPONENT_STATE,
				 "Write delegation exists. New share is with diff client. conflict.");
			return true;
		}
		break;
	case STATE_TYPE_LOCK:
		if (deleg_clientid == candidate_clientid) {
			LogDebug(COMPONENT_STATE,
				 "Creating lock for client that owns the delegation. no conflict.");
			return false;
		}

		/* Search for a posix lock that conflicts with delegation */
		glist_for_each(glist, &candidate_data->lock.state_locklist) {
			found_lock = glist_entry(glist, state_lock_entry_t,
						 sle_state_locks);
			if (found_lock->sle_type != POSIX_LOCK) {
				LogDebug(COMPONENT_STATE,
					 "non posix lock in lock list");
				continue;
			}
			if (found_lock->sle_lock.lock_type == FSAL_LOCK_R
			    && state->state_data.deleg.sd_type
			    == OPEN_DELEGATE_WRITE) {
				LogDebug(COMPONENT_STATE,
					 "Trying to get read lock. write delegation exists. conflict");
				return true; /*recall delegation*/
			}
			if (found_lock->sle_lock.lock_type == FSAL_LOCK_W) {
				LogDebug(COMPONENT_STATE,
					 "Trying to get write lock. delegation exists. conflict");
				return true;
			}
		}
	case STATE_TYPE_LAYOUT:
		return false;
		break;
	case STATE_TYPE_NONE:
	default:
		LogDebug(COMPONENT_STATE, "Shouldn't be here.");
		break;
	}

	return false;
}

/**
 * @brief Checks for a conflict between an existing state and a candidate state.
 *
 * @param[in] state      Existing state
 * @param[in] state_type Type of candidate state
 * @param[in] state_data Data for the candidate state
 *
 * @retval true if there is a conflict.
 * @retval false if no conflict has been found
 */
bool state_conflict(state_t *state, state_type_t state_type,
		    state_data_t *candidate_data)
{
	if (state == NULL || candidate_data == NULL)
		return true;

	switch (state_type) {
	case STATE_TYPE_NONE:
		return false;	/* STATE_NONE conflicts with nobody */

	case STATE_TYPE_SHARE:
		if (state->state_type == STATE_TYPE_SHARE) {
			if ((state->state_data.share.share_access &
			     candidate_data->share.share_deny)
			    || (state->state_data.share.share_deny &
				candidate_data->share.share_access)) {
				/* Conflicting share reservation */
				return true;
			}
		}
		return false;

	case STATE_TYPE_LOCK:
		/* lock conflict is managed in the NFS request */
		return false;

	case STATE_TYPE_LAYOUT:
		/** layout conflict is managed by the FSAL */
		return false;

	case STATE_TYPE_DELEG:
		/* This will appear during new OPEN share state
		 * We are getting a delegation and found a diff share entry.
		 */
		if (state->state_type == STATE_TYPE_SHARE) {
			/* */
			if (candidate_data->deleg.sd_type
			    == OPEN_DELEGATE_READ
			    && state->state_data.share.share_access
			    & OPEN4_SHARE_ACCESS_WRITE)
				return true;
			if (candidate_data->deleg.sd_type
			    == OPEN_DELEGATE_WRITE
			    && state->state_data.share.share_access
			    & OPEN4_SHARE_ACCESS_READ)
				return true;
			if (candidate_data->deleg.sd_type
			    == OPEN_DELEGATE_WRITE
			    && state->state_data.share.share_access
			    & OPEN4_SHARE_ACCESS_WRITE)
				return true;
		}
	}
	return false;
}

/**
 * @brief adds a new state to a cache entry
 *
 * This version of the function does not take the state lock on the
 * entry.  It exists to allow callers to integrate state into a larger
 * operation.
 *
 * @param[in,out] entry       Cache entry to operate on
 * @param[in]     state_type  State to be defined
 * @param[in]     state_data  Data related to this state
 * @param[in]     owner_input Related open_owner
 * @param[out]    state       The new state
 * @param[in]     refer       Reference to compound creating state
 *
 * @return Operation status
 */
state_status_t state_add_impl(cache_entry_t *entry, state_type_t state_type,
			      state_data_t *state_data,
			      state_owner_t *owner_input, state_t **state,
			      struct state_refer *refer)
{
	state_t *pnew_state, *piter_state = NULL;
	state_lock_entry_t *piter_lock;
	char debug_str[OTHERSIZE * 2 + 1];
	struct glist_head *glist = NULL, *glistn = NULL;
	cache_inode_status_t cache_status;
	bool got_pinned = false;
	state_status_t status = 0;

	if (glist_empty(&entry->state_list)) {
		cache_status = cache_inode_inc_pin_ref(entry);

		if (cache_status != CACHE_INODE_SUCCESS) {
			status =
			    cache_inode_status_to_state_status(cache_status);
			LogDebug(COMPONENT_STATE, "Could not pin file");
			return status;
		}

		got_pinned = true;
	}

	pnew_state = pool_alloc(state_v4_pool, NULL);

	if (pnew_state == NULL) {
		LogCrit(COMPONENT_STATE,
			"Can't allocate a new file state from cache pool");

		/* stat */
		status = STATE_MALLOC_ERROR;

		if (got_pinned)
			cache_inode_dec_pin_ref(entry, false);

		return status;
	}

	/* Check conflicting delegations and recall if necessary */
	if (entry->type == REGULAR_FILE
	    && (!glist_empty(&entry->object.file.deleg_list)))
		glist_for_each_safe(glist, glistn,
				    &entry->object.file.deleg_list) {
			piter_lock = glist_entry(glist, state_lock_entry_t,
						 sle_list);
			piter_state = piter_lock->sle_state;
			if (piter_lock->sle_type != LEASE_LOCK) {
				LogDebug(COMPONENT_STATE, "Wrong lock type");
				continue;
			}
			if (piter_state->state_type != STATE_TYPE_DELEG) {
				LogDebug(COMPONENT_STATE, "Wrong state type");
				continue;
			}
			if (check_deleg_conflict(piter_state, state_type,
						 state_data, owner_input)) {
				status = delegrecall(entry, true);
				if (status != STATE_SUCCESS) {
					LogDebug(COMPONENT_STATE,
						 "Failed to recall delegation");
				}
			}
		}

	/* Browse the state's list */
	glist_for_each(glist, &entry->state_list) {
		piter_state = glist_entry(glist, state_t, state_list);

		if (state_conflict(piter_state, state_type, state_data)) {
			LogDebug(COMPONENT_STATE,
				 "new state conflicts with another state for entry %p",
				 entry);

			/* stat */
			pool_free(state_v4_pool, pnew_state);

			status = STATE_STATE_CONFLICT;

			if (got_pinned)
				cache_inode_dec_pin_ref(entry, false);

			return status;
		}
	}

	/* Add the stateid.other, this will increment cid_stateid_counter */
	nfs4_BuildStateId_Other(owner_input->so_owner.so_nfs4_owner.
				so_clientrec, pnew_state->stateid_other);

	/* Set the type and data for this state */
	memcpy(&(pnew_state->state_data), state_data, sizeof(state_data_t));
	pnew_state->state_type = state_type;
	pnew_state->state_seqid = 0;	/* will be incremented to 1 later */
	pnew_state->state_entry = entry;
	pnew_state->state_owner = owner_input;

	if (refer)
		pnew_state->state_refer = *refer;

	if (isDebug(COMPONENT_STATE))
		sprint_mem(debug_str, (char *)pnew_state->stateid_other,
			   OTHERSIZE);

	glist_init(&pnew_state->state_list);
	glist_init(&pnew_state->state_owner_list);

	/* Add the state to the related hashtable */
	if (!nfs4_State_Set(pnew_state->stateid_other, pnew_state)) {
		sprint_mem(debug_str, (char *)pnew_state->stateid_other,
			   OTHERSIZE);

		LogCrit(COMPONENT_STATE,
			"Can't create a new state id %s for the entry %p (F)",
			debug_str, entry);

		pool_free(state_v4_pool, pnew_state);

		/* Return STATE_MALLOC_ERROR since most likely the
		 * nfs4_State_Set failed to allocate memory.
		 */
		status = STATE_MALLOC_ERROR;

		if (got_pinned)
			cache_inode_dec_pin_ref(entry, false);

		return status;
	}

	/* Add state to list for cache entry */
	glist_add_tail(&entry->state_list, &pnew_state->state_list);

	inc_state_owner_ref(owner_input);

	pthread_mutex_lock(&owner_input->so_mutex);

	glist_add_tail(&owner_input->so_owner.so_nfs4_owner.so_state_list,
		       &pnew_state->state_owner_list);

	pthread_mutex_unlock(&owner_input->so_mutex);

#ifdef DEBUG_SAL
	pthread_mutex_lock(&all_state_v4_mutex);

	glist_add_tail(&state_v4_all, &pnew_state->state_list_all);

	pthread_mutex_unlock(&all_state_v4_mutex);
#endif

	/* Copy the result */
	*state = pnew_state;

	LogFullDebug(COMPONENT_STATE, "Add State: %s", debug_str);

	/* Regular exit */
	status = STATE_SUCCESS;
	return status;
}				/* state_add */

/**
 * @brief Adds a new state to a cache entry
 *
 * @param[in,out] entry       Cache entry to operate on
 * @param[in]     state_type  State to be defined
 * @param[in]     state_data  Data related to this state
 * @param[in]     owner_input Related open_owner
 * @param[out]    state       The new state
 * @param[in]     refer       Reference to compound creating state
 *
 * @return Operation status
 */
state_status_t state_add(cache_entry_t *entry, state_type_t state_type,
			 state_data_t *state_data, state_owner_t *owner_input,
			 state_t **state, struct state_refer *refer)
{
	state_status_t status = 0;

	/* Ensure that states are are associated only with the appropriate
	   owners */

	if (((state_type == STATE_TYPE_SHARE)
	     && (owner_input->so_type != STATE_OPEN_OWNER_NFSV4))
	    || ((state_type == STATE_TYPE_LOCK)
		&& (owner_input->so_type != STATE_LOCK_OWNER_NFSV4))
	    ||
	    (((state_type == STATE_TYPE_DELEG)
	      || (state_type == STATE_TYPE_LAYOUT))
	     && (owner_input->so_type != STATE_CLIENTID_OWNER_NFSV4))) {
		return STATE_BAD_TYPE;
	}

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	status =
	    state_add_impl(entry, state_type, state_data, owner_input, state,
			   refer);
	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	return status;
}

/**
 * @brief Remove a state from a cache entry
 *
 * The caller must hold the state lock exclusively.
 *
 * @param[in]     state The state to remove
 * @param[in,out] entry The cache entry to modify
 *
 */

void state_del_locked(state_t *state, cache_entry_t *entry)
{
	char debug_str[OTHERSIZE * 2 + 1];

	if (isDebug(COMPONENT_STATE))
		sprint_mem(debug_str, (char *)state->stateid_other, OTHERSIZE);

	LogFullDebug(COMPONENT_STATE, "Deleting state %s", debug_str);

	/* Remove the entry from the HashTable, which can't fail */
	nfs4_State_Del(state->stateid_other);

	/* Remove from list of states owned by owner */

	/* Release the state owner reference */
	if (state->state_owner != NULL) {
		pthread_mutex_lock(&state->state_owner->so_mutex);
		glist_del(&state->state_owner_list);
		pthread_mutex_unlock(&state->state_owner->so_mutex);
		dec_state_owner_ref(state->state_owner);
	}

	/* Remove from the list of states for a particular cache entry */
	glist_del(&state->state_list);

	/* Remove from the list of lock states for a particular open state */
	if (state->state_type == STATE_TYPE_LOCK)
		glist_del(&state->state_data.lock.state_sharelist);

	/* Remove from list of states for a particular export */
	PTHREAD_RWLOCK_wrlock(&state->state_export->lock);
	glist_del(&state->state_export_list);
	PTHREAD_RWLOCK_unlock(&state->state_export->lock);

#ifdef DEBUG_SAL
	pthread_mutex_lock(&all_state_v4_mutex);

	glist_del(&state->state_list_all);

	pthread_mutex_unlock(&all_state_v4_mutex);
#endif

	pool_free(state_v4_pool, state);

	LogFullDebug(COMPONENT_STATE, "Deleted state %s", debug_str);

	if (glist_empty(&entry->state_list))
		cache_inode_dec_pin_ref(entry, false);
}

/**
 * @brief Delete a state
 *
 * @param[in] state     State to delete
 * @param[in] hold_lock If we already hold the lock
 *
 */
void state_del(state_t *state, bool hold_lock)
{
	cache_entry_t *entry = state->state_entry;

	if (!hold_lock)
		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	state_del_locked(state, state->state_entry);

	if (!hold_lock)
		PTHREAD_RWLOCK_unlock(&entry->state_lock);
}

/**
 * @brief Remove all state from a cache entry
 *
 * Used by cache_inode_kill_entry in the event that the FSAL says a
 * handle is stale.
 *
 * @param[in,out] entry The entry to wipe
 */
void state_nfs4_state_wipe(cache_entry_t *entry)
{
	struct glist_head *glist, *glistn;
	state_t *state = NULL;

	if (glist_empty(&entry->state_list))
		return;

	glist_for_each_safe(glist, glistn, &entry->state_list) {
		state = glist_entry(glist, state_t, state_list);
		state_del_locked(state, entry);
	}

	return;
}

/**
 * @brief Remove every state belonging to the lock owner.
 *
 * @param[in] lock_owner Lock owner to release
 */
void release_lockstate(state_owner_t *lock_owner)
{
	struct glist_head *glist, *glistn;

	glist_for_each_safe(glist, glistn,
			    &lock_owner->so_owner.so_nfs4_owner.so_state_list) {
		state_t *state_found = glist_entry(glist,
						   state_t,
						   state_owner_list);

		cache_entry_t *entry = state_found->state_entry;

		/* Make sure we hold an lru ref to the cache inode while calling
		 * state_del */
		cache_inode_lru_ref(state_found->state_entry, LRU_FLAG_NONE);

		state_del(state_found, false);

		/* Release the lru ref to the cache inode we held while
		 * calling state_del
		 */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
	}
}

/**
 * @brief Remove all state belonging to the open owner.
 *
 * @param[in,out] open_owner Open owner
 */
void release_openstate(state_owner_t *open_owner)
{
	state_status_t state_status;
	struct glist_head *glist, *glistn;

	glist_for_each_safe(glist, glistn,
			    &open_owner->so_owner.so_nfs4_owner.so_state_list) {
		state_t *state_found = glist_entry(glist,
						   state_t,
						   state_owner_list);

		cache_entry_t *entry = state_found->state_entry;

		/* Make sure we hold an lru ref to the cache inode while calling
		 * state_del */
		cache_inode_lru_ref(entry, LRU_FLAG_NONE);

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

		if (state_found->state_type == STATE_TYPE_SHARE) {
			op_ctx->export = state_found->state_export;
			op_ctx->fsal_export = op_ctx->export->fsal_export;

			state_status =
			    state_share_remove(state_found->state_entry,
					       open_owner, state_found);
			if (!state_unlock_err_ok(state_status)) {
				LogEvent(COMPONENT_CLIENTID,
					 "EXPIRY failed to release share stateid error %s",
					 state_err_str(state_status));
			}
		}

		state_del_locked(state_found, entry);

		/* Close the file in FSAL through the cache inode */
		cache_inode_close(entry, 0);

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Release the lru ref to the cache inode we held while
		 * calling state_del
		 */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
	}
}

/**
 * @brief Remove all state belonging to an export.
 *
 */

void state_export_release_nfs4_state(void)
{
	state_t *state;
	state_status_t state_status;

	while (1) {
		PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);

		state = glist_first_entry(&op_ctx->export->exp_state_list,
					  state_t,
					  state_export_list);

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

		if (state == NULL)
			break;

		if (state->state_type == STATE_TYPE_SHARE) {
			state_status = state_share_remove(state->state_entry,
							  state->state_owner,
							  state);

			if (!state_unlock_err_ok(state_status)) {
				LogEvent(COMPONENT_CLIENTID,
					 "EXPIRY failed to release share stateid error %s",
					 state_err_str(state_status));
			}
		}

		state_del(state, false);
	}
}

#ifdef DEBUG_SAL
void dump_all_states(void)
{
	if (!isDebug(COMPONENT_STATE))
		return;

	pthread_mutex_lock(&all_state_v4_mutex);

	if (!glist_empty(&state_v4_all)) {
		struct glist_head *glist;

		LogDebug(COMPONENT_STATE,
			 " =State List= ");

		glist_for_each(glist, &state_v4_all) {
			state_t *pstate =
			    glist_entry(glist, state_t, state_list_all);
			char *state_type = "unknown";
			char str[HASHTABLE_DISPLAY_STRLEN];

			switch (pstate->state_type) {
			case STATE_TYPE_NONE:
				state_type = "NONE";
				break;
			case STATE_TYPE_SHARE:
				state_type = "SHARE";
				break;
			case STATE_TYPE_DELEG:
				state_type = "DELEGATION";
				break;
			case STATE_TYPE_LOCK:
				state_type = "LOCK";
				break;
			case STATE_TYPE_LAYOUT:
				state_type = "LAYOUT";
				break;
			}

			DisplayOwner(pstate->state_owner, str);
			LogDebug(COMPONENT_STATE, "State %p type %s owner {%s}",
				 pstate, state_type, str);
		}

		LogDebug(COMPONENT_STATE,
			 " ----------------------");
	} else
		LogDebug(COMPONENT_STATE, "All states released");

	pthread_mutex_unlock(&all_state_v4_mutex);
}
#endif

/** @} */
