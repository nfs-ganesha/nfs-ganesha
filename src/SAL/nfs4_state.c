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
#include "fsal_up.h"

pool_t *state_v4_pool;		/*< Pool for NFSv4 files's states */

#ifdef DEBUG_SAL
struct glist_head state_v4_all;
pthread_mutex_t all_state_v4_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * @brief Checks for a conflict between an existing delegation state and a
 * candidate state.
 *
 * @param[in] state       Existing delegation
 * @param[in] state_type  Type of candidate state
 * @param[in] state_data  Data for the candidate state
 * @param[in] state_owner Owner of the candidate state
 *
 * @retval true if there is a conflict.
 * @retval false if no conflict has been found
 */
static bool check_deleg_conflict(struct deleg_data *deleg_entry,
				 state_type_t candidate_type,
				 state_data_t *candidate_data,
				 state_owner_t *candidate_owner)
{
	state_deleg_t *deleg_data = &deleg_entry->dd_state->state_data.deleg;

	LogFullDebug(COMPONENT_STATE, "Checking for conflict!!");

	/* We are getting a new share, checking if delegations conflict. */
	switch (candidate_type) {
	case STATE_TYPE_DELEG:
		/* All open conflicts and delegation recalls are handled
		 * while adding open state (STATE_TYPE_SHARE). There
		 * should NOT be any conflicts while adding delegation
		 * state.
		 */
		break;
	case STATE_TYPE_SHARE:
		if (deleg_data->sd_type == OPEN_DELEGATE_READ &&
		    candidate_data->share.share_access &
			    OPEN4_SHARE_ACCESS_WRITE) {
			LogDebug(COMPONENT_STATE,
				 "Write access requested but a read delegation exists.");
			return true;
		}
		if (deleg_data->sd_type == OPEN_DELEGATE_WRITE) {
			LogDebug(COMPONENT_STATE,
				 "Write delegation exists.");
			return true;
		}
		break;
	case STATE_TYPE_LOCK:
		/* The FSAL layer will have to pick this up. */
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
	state_t *pnew_state, *piter_state;
	struct deleg_data *iter_deleg;
	char debug_str[OTHERSIZE * 2 + 1];
	struct glist_head *glist;
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

	/* Check conflicting delegations and recall if necessary */
	if (entry->type == REGULAR_FILE) {
		glist_for_each(glist, &entry->object.file.deleg_list) {
			iter_deleg = glist_entry(glist, struct deleg_data,
						 dd_list);
			piter_state = iter_deleg->dd_state;
			assert(piter_state->state_type == STATE_TYPE_DELEG);

			if (check_deleg_conflict(iter_deleg, state_type,
						 state_data, owner_input)) {
				(void)async_delegrecall(general_fridge, entry);

				if (got_pinned)
					cache_inode_dec_pin_ref(entry, false);

				status = STATE_FSAL_DELAY;
				return status;
			}
		}
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

	if (pnew_state->state_type == STATE_TYPE_DELEG &&
	    pnew_state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE)
		entry->object.file.write_delegated = true;

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

	/* Reset write delegated if this is a write delegation */
	if (state->state_type == STATE_TYPE_DELEG &&
	    state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE)
		entry->object.file.write_delegated = false;

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
 * @brief Revoke delagtions belonging to the client owner.
 *
 * @param[in,out] client owner
 */
void revoke_owner_delegs(state_owner_t *client_owner)
{
	struct glist_head *glist, *glistn;
	state_t *state;
	cache_entry_t *entry;

	glist_for_each_safe(glist, glistn,
			&client_owner->so_owner.so_nfs4_owner.so_state_list) {
		state = glist_entry(glist, state_t, state_owner_list);
		entry = state->state_entry;

		if (state->state_type != STATE_TYPE_DELEG)
			continue;

		/* state_deleg_revoke will remove the delegation state.
		 * If that happens to be the last state on the cache
		 * inode entry, a ref is decremented on it. So entry may
		 * cease to exist after the call to state_deleg_revoke.
		 * To prevent this, we place a ref count on the entry
		 * here.
		 */
		cache_inode_lru_ref(entry, LRU_FLAG_NONE);

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);
		state_deleg_revoke(state, entry);
		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Close the file in FSAL through the cache inode */
		cache_inode_close(entry, 0);

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
	cache_entry_t *entry;

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

		entry = state->state_entry;
		PTHREAD_RWLOCK_wrlock(&entry->state_lock);
		if (state->state_type == STATE_TYPE_DELEG)
			/* this deletes the state too */
			state_deleg_revoke(state, entry);
		else
			state_del_locked(state, entry);
		PTHREAD_RWLOCK_unlock(&entry->state_lock);
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
