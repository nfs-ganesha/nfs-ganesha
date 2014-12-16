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
#include <assert.h>

#include "log.h"
#include "hashtable.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "fsal.h"
#include "sal_functions.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"
#include "fsal_up.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"

pool_t *state_v4_pool;		/*< Pool for NFSv4 files's states */

#ifdef DEBUG_SAL
struct glist_head state_v4_all = GLIST_HEAD_INIT(state_v4_all);
pthread_mutex_t all_state_v4_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

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
state_status_t state_add_impl(cache_entry_t *entry, enum state_type state_type,
			      union state_data *state_data,
			      state_owner_t *owner_input, state_t **state,
			      struct state_refer *refer)
{
	state_t *pnew_state = NULL;
	char str[DISPLAY_STATEID_OTHER_SIZE];
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	cache_inode_status_t cache_status;
	bool got_pinned = false;
	bool got_export_ref = false;
	state_status_t status = 0;
	bool mutex_init = false;

	/* Take a cache inode reference for the state */
	cache_status = cache_inode_lru_ref(entry, LRU_FLAG_NONE);

	if (cache_status != CACHE_INODE_SUCCESS) {
		status = cache_inode_status_to_state_status(cache_status);
		LogDebug(COMPONENT_STATE, "Could not ref file");
		return status;
	}

	/* Attempt to get a reference to the export. */
	if (!get_gsh_export_ref(op_ctx->export, false)) {
		/* If we could not get a reference, return stale.
		 * Release attr_lock
		 */
		LogDebug(COMPONENT_STATE, "Stale export");
		status = STATE_ESTALE;
		goto errout;
	}

	got_export_ref = true;

	if (glist_empty(&entry->list_of_states)) {
		cache_status = cache_inode_inc_pin_ref(entry);

		if (cache_status != CACHE_INODE_SUCCESS) {
			status =
			    cache_inode_status_to_state_status(cache_status);
			LogDebug(COMPONENT_STATE, "Could not pin file");
			goto errout;
		}

		got_pinned = true;
	}

	pnew_state = pool_alloc(state_v4_pool, NULL);

	if (pnew_state == NULL) {
		LogCrit(COMPONENT_STATE,
			"Can't allocate a new file state from cache pool");

		/* stat */
		status = STATE_MALLOC_ERROR;
		goto errout;
	}

	assert(pthread_mutex_init(&pnew_state->state_mutex, NULL) == 0);

	mutex_init = true;

	/* Add the stateid.other, this will increment cid_stateid_counter */
	nfs4_BuildStateId_Other(owner_input->so_owner.so_nfs4_owner.
				so_clientrec, pnew_state->stateid_other);

	/* Set the type and data for this state */
	memcpy(&(pnew_state->state_data), state_data, sizeof(*state_data));
	pnew_state->state_type = state_type;
	pnew_state->state_seqid = 0;	/* will be incremented to 1 later */
	pnew_state->state_refcount = 2; /* sentinel plus returned ref */

	if (refer)
		pnew_state->state_refer = *refer;

	if (isDebug(COMPONENT_STATE)) {
		display_stateid_other(&dspbuf, pnew_state->stateid_other);
		str_valid = true;
	}

	glist_init(&pnew_state->state_list);

	/* Add the state to the related hashtable */
	if (!nfs4_State_Set(pnew_state)) {
		if (!str_valid)
			display_stateid_other(&dspbuf,
					      pnew_state->stateid_other);

		LogCrit(COMPONENT_STATE,
			"Can't create a new state id %s for the entry %p (F)",
			str, entry);

		/* Return STATE_MALLOC_ERROR since most likely the
		 * nfs4_State_Set failed to allocate memory.
		 */
		status = STATE_MALLOC_ERROR;
		goto errout;
	}

	/* Each of the following blocks takes the state_mutex and releases it
	 * because we always want state_mutex to be the last lock taken.
	 *
	 * NOTE: We don't have to worry about state_del/state_del_locked being
	 *       called in the midst of things because the state_lock is held.
	 */

	/* Attach this to an export */
	PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
	pthread_mutex_lock(&pnew_state->state_mutex);
	pnew_state->state_export = op_ctx->export;
	glist_add_tail(&op_ctx->export->exp_state_list,
		       &pnew_state->state_export_list);
	pthread_mutex_unlock(&pnew_state->state_mutex);
	PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

	/* Add state to list for cache entry */
	pthread_mutex_lock(&pnew_state->state_mutex);
	glist_add_tail(&entry->list_of_states, &pnew_state->state_list);
	pnew_state->state_entry = entry;
	pthread_mutex_unlock(&pnew_state->state_mutex);

	/* Add state to list for owner */
	pthread_mutex_lock(&owner_input->so_mutex);
	pthread_mutex_lock(&pnew_state->state_mutex);

	pnew_state->state_owner = owner_input;
	inc_state_owner_ref(owner_input);

	glist_add_tail(&owner_input->so_owner.so_nfs4_owner.so_state_list,
		       &pnew_state->state_owner_list);

	pthread_mutex_unlock(&pnew_state->state_mutex);
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

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Add State: %s", str);

	/* Regular exit */
	status = STATE_SUCCESS;
	return status;

errout:

	if (mutex_init)
		assert(pthread_mutex_destroy(&pnew_state->state_mutex) == 0);

	if (pnew_state != NULL)
		pool_free(state_v4_pool, pnew_state);

	if (got_pinned)
		cache_inode_dec_pin_ref(entry, false);

	if (got_export_ref)
		put_gsh_export(op_ctx->export);

	cache_inode_lru_unref(entry, LRU_UNREF_STATE_LOCK_HELD);

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
state_status_t state_add(cache_entry_t *entry, enum state_type state_type,
			 union state_data *state_data,
			 state_owner_t *owner_input,
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

void state_del_locked(state_t *state)
{
	char str[LOG_BUFF_LEN];
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	cache_entry_t *entry;
	struct gsh_export *export;
	state_owner_t *owner;

	if (isDebug(COMPONENT_STATE)) {
		display_stateid(&dspbuf, state);
		str_valid = true;
	}

	/* Remove the entry from the HashTable. If it fails, we have lost the
	 * race with another caller of state_del/state_del_locked.
	 */
	if (!nfs4_State_Del(state->stateid_other)) {
		if (str_valid)
			LogDebug(COMPONENT_STATE,
				 "Racing to delete %s", str);
		return;
	}

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Deleting %s", str);

	/* Protect extraction of all the referenced objects, we don't
	 * actually need to test them or take references because we assure
	 * that there is exactly one state_del_locked call that proceeds
	 * this far, and thus if the refereces were non-NULL, they must still
	 * be good. Holding the mutex is not strictly necessary for this
	 * reason, however, static and dynamic code analysis have no way of
	 * knowing this reference is safe.
	 */
	pthread_mutex_lock(&state->state_mutex);
	entry = state->state_entry;
	export = state->state_export;
	owner = state->state_owner;
	pthread_mutex_unlock(&state->state_mutex);

	if (owner != NULL) {
		/* Remove from list of states owned by owner and
		 * release the state owner reference.
		 */
		pthread_mutex_lock(&owner->so_mutex);
		pthread_mutex_lock(&state->state_mutex);
		glist_del(&state->state_owner_list);
		state->state_owner = NULL;
		pthread_mutex_unlock(&state->state_mutex);
		pthread_mutex_unlock(&owner->so_mutex);
		dec_state_owner_ref(owner);
	}

	/* Remove from the list of states for a particular cache entry */
	pthread_mutex_lock(&state->state_mutex);
	glist_del(&state->state_list);
	state->state_entry = NULL;
	pthread_mutex_unlock(&state->state_mutex);
	cache_inode_lru_unref(entry, LRU_UNREF_STATE_LOCK_HELD);

	/* Remove from the list of lock states for a particular open state.
	 * This is safe to do without any special checks. If we are not on
	 * the list, glist_del does nothing, and the state_lock protects the
	 * open state's state_sharelist.
	 */
	if (state->state_type == STATE_TYPE_LOCK)
		glist_del(&state->state_data.lock.state_sharelist);

	/* Reset write delegated if this is a write delegation */
	if (state->state_type == STATE_TYPE_DELEG &&
	    state->state_data.deleg.sd_type == OPEN_DELEGATE_WRITE)
		entry->object.file.write_delegated = false;

	/* Remove from list of states for a particular export.
	 * In this case, it is safe to look at state_export without yet
	 * holding the state_mutex because this is the only place where it
	 * is removed, and we have guaranteed we are the only thread
	 * proceeding with state deletion.
	 */
	PTHREAD_RWLOCK_wrlock(&export->lock);
	pthread_mutex_lock(&state->state_mutex);
	glist_del(&state->state_export_list);
	state->state_export = NULL;
	pthread_mutex_unlock(&state->state_mutex);
	PTHREAD_RWLOCK_unlock(&export->lock);
	put_gsh_export(export);

#ifdef DEBUG_SAL
	pthread_mutex_lock(&all_state_v4_mutex);

	glist_del(&state->state_list_all);

	pthread_mutex_unlock(&all_state_v4_mutex);
#endif

	if (glist_empty(&entry->list_of_states))
		cache_inode_dec_pin_ref(entry, false);

	/* Remove the sentinel reference */
	dec_state_t_ref(state);
}

/**
 * @brief Delete a state
 *
 * @param[in] state     State to delete
 *
 */
void state_del(state_t *state)
{
	cache_entry_t *entry = get_state_entry_ref(state);

	if (entry == NULL) {
		LogDebug(COMPONENT_STATE,
			 "Entry for state is stale");
		return;
	}

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	state_del_locked(state);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_lru_unref(entry, LRU_FLAG_NONE);
}

/**
 * @brief Get references to the various objects a state_t points to.
 *
 * @param[in] state The state_t to get references from
 * @param[in,out] entry Place to return the cache entry (NULL if not desired)
 * @param[in,out] export Place to return the export (NULL if not desired)
 * @param[in,out] owner Place to return the owner (NULL if not desired)
 *
 * @retval true if all desired references were taken
 * @retval flase otherwise (in which case no references are taken)
 *
 * For convenience, returns false if state is NULL which helps simplify
 * code for some callers.
 */
bool get_state_entry_export_owner_refs(state_t *state,
				       cache_entry_t **entry,
				       struct gsh_export **export,
				       state_owner_t **owner)
{
	if (entry != NULL)
		*entry = NULL;

	if (export != NULL)
		*export = NULL;

	if (owner != NULL)
		*owner = NULL;

	if (state == NULL)
		return false;

	pthread_mutex_lock(&state->state_mutex);

	LogFullDebug(COMPONENT_STATE,
		     "state %p state_entry %p state_export %p state_owner %p",
		     state, state->state_entry, state->state_export,
		     state->state_owner);

	if (entry != NULL) {
		if (state->state_entry != NULL &&
		    cache_inode_lru_ref(state->state_entry,
					LRU_FLAG_NONE) == CACHE_INODE_SUCCESS)
			*entry = state->state_entry;
		else
			goto fail;
	}

	if (export != NULL) {
		if (state->state_export != NULL &&
		    get_gsh_export_ref(state->state_export, false))
			*export = state->state_export;
		else
			goto fail;
	}

	if (owner != NULL) {
		if (state->state_owner != NULL) {
			*owner = state->state_owner;
			inc_state_owner_ref(*owner);
		} else {
			goto fail;
		}
	}

	pthread_mutex_unlock(&state->state_mutex);

	return true;

fail:

	pthread_mutex_unlock(&state->state_mutex);

	if (entry != NULL && *entry != NULL) {
		cache_inode_lru_unref(*entry, LRU_FLAG_NONE);
		*entry = NULL;
	}

	if (export != NULL && *export != NULL) {
		put_gsh_export(*export);
		*export = NULL;
	}

	if (owner != NULL && *owner != NULL) {
		dec_state_owner_ref(*owner);
		*owner = NULL;
	}

	return false;
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

	if (glist_empty(&entry->list_of_states))
		return;

	glist_for_each_safe(glist, glistn, &entry->list_of_states) {
		state = glist_entry(glist, state_t, state_list);
		state_del_locked(state);
	}

	return;
}

/**
 * @brief Remove every state belonging to the lock owner.
 *
 * @param[in] lock_owner Lock owner to release
 */
enum nfsstat4 release_lock_owner(state_owner_t *owner)
{
	pthread_mutex_lock(&owner->so_mutex);

	if (!glist_empty(&owner->so_lock_list)) {
		pthread_mutex_unlock(&owner->so_mutex);

		return NFS4ERR_LOCKS_HELD;
	}

	while (true) {
		state_t *state;

		state = glist_first_entry(&owner->so_owner.so_nfs4_owner
								.so_state_list,
					  state_t,
					  state_owner_list);

		if (state == NULL) {
			pthread_mutex_unlock(&owner->so_mutex);
			return NFS4_OK;
		}

		/* Make sure the state doesn't go away on us... */
		inc_state_t_ref(state);

		pthread_mutex_unlock(&owner->so_mutex);

		state_del(state);

		dec_state_t_ref(state);

		pthread_mutex_lock(&owner->so_mutex);
	}
}

/**
 * @brief Remove all state belonging to the open owner.
 *
 * @param[in,out] open_owner Open owner
 */
void release_openstate(state_owner_t *owner)
{
	state_status_t state_status;
	int errcnt = 0;
	bool ok;

	/* Only accept so many errors before giving up. */
	while (errcnt < STATE_ERR_MAX) {
		state_t *state;
		cache_entry_t *entry = NULL;
		struct gsh_export *export = NULL;

		pthread_mutex_lock(&owner->so_mutex);

		state = glist_first_entry(&owner->so_owner.so_nfs4_owner
								.so_state_list,
					  state_t,
					  state_owner_list);

		if (state == NULL) {
			pthread_mutex_unlock(&owner->so_mutex);
			return;
		}

		/* Move to end of list in case of error to ease retries */
		glist_del(&state->state_owner_list);
		glist_add_tail(&owner->so_owner.so_nfs4_owner.so_state_list,
			       &state->state_owner_list);

		/* Get references to the cache entry and export */
		ok = get_state_entry_export_owner_refs(state,
						       &entry,
						       &export,
						       NULL);

		if (!ok) {
			/* The cache entry, export, or state must be about to
			 * die, skip for now.
			 */
			pthread_mutex_unlock(&owner->so_mutex);
			continue;
		}

		/* Make sure the state doesn't go away on us... */
		inc_state_t_ref(state);

		pthread_mutex_unlock(&owner->so_mutex);

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

		if (state->state_type == STATE_TYPE_SHARE) {
			op_ctx->export = export;
			op_ctx->fsal_export = export->fsal_export;

			state_status = state_share_remove(entry, owner, state);

			if (!state_unlock_err_ok(state_status)) {
				errcnt++;
				LogEvent(COMPONENT_CLIENTID,
					 "EXPIRY failed to release share stateid error %s",
					 state_err_str(state_status));
			}
		}

		state_del_locked(state);

		dec_state_t_ref(state);

		/* Close the file in FSAL through the cache inode */
		cache_inode_close(entry, CACHE_INODE_FLAG_NONE);

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Release the lru ref to the cache inode we held while
		 * calling state_del
		 */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
	}

	if (errcnt == STATE_ERR_MAX) {
		char str[LOG_BUFF_LEN];
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_owner(&dspbuf, owner);

		LogFatal(COMPONENT_STATE,
			 "Could not complete cleanup of lock state for lock owner %s",
			 str);
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
	bool so_mutex_held;

 again:

	pthread_mutex_lock(&client_owner->so_mutex);
	so_mutex_held = true;

	glist_for_each_safe(glist, glistn,
			&client_owner->so_owner.so_nfs4_owner.so_state_list) {
		state = glist_entry(glist, state_t, state_owner_list);

		/* Move entry to end of list to handle errors and skipping of
		 * non-delegation states.
		 */
		glist_del(&state->state_owner_list);
		glist_add_tail(
			&client_owner->so_owner.so_nfs4_owner.so_state_list,
			&state->state_owner_list);

		/* Skip non-delegation states. */
		if (state->state_type != STATE_TYPE_DELEG)
			continue;

		/* Safely access the cache inode associated with the state.
		 * This will get an LRU reference protecting our access
		 * even after state_deleg_revoke releases the reference it
		 * holds.
		 */
		entry = get_state_entry_ref(state);

		if (entry == NULL) {
			LogDebug(COMPONENT_STATE,
				 "Stale state or cache entry");
			continue;
		}

		pthread_mutex_unlock(&client_owner->so_mutex);
		so_mutex_held = false;

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);
		state_deleg_revoke(entry, state);
		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Close the file in FSAL through the cache inode */
		cache_inode_close(entry, 0);

		cache_inode_lru_unref(entry, LRU_FLAG_NONE);

		/* Since we dropped so_mutex, we must restart the loop. */
		goto again;
	}

	if (so_mutex_held)
		pthread_mutex_unlock(&client_owner->so_mutex);
}

/**
 * @brief Remove all state belonging to an export.
 *
 */

void state_export_release_nfs4_state(void)
{
	state_t *state;
	state_status_t state_status;
	int errcnt = 0;
	struct glist_head *glist, *glistn;
	bool hold_export_lock;

	/* Revoke layouts first (so that open states are still present).
	 * Because we have to drop the export lock, when we cycle around agin
	 * we MUST restart.
	 */

 again:

	PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
	hold_export_lock = true;

	glist_for_each_safe(glist, glistn, &op_ctx->export->exp_state_list) {
		cache_entry_t *entry = NULL;
		state_owner_t *owner = NULL;
		bool deleted = false;
		struct pnfs_segment entire = {
			.io_mode = LAYOUTIOMODE4_ANY,
			.offset = 0,
			.length = NFS4_UINT64_MAX
		};

		state = glist_entry(glist, state_t, state_export_list);

		/* Move state to the end of the list in case an error
		 * occurs or the state is going stale. This also keeps us
		 * from continually re-examining non-layout states when
		 * we restart the loop.
		 */
		glist_del(&state->state_export_list);
		glist_add_tail(&op_ctx->export->exp_state_list,
			       &state->state_export_list);

		if (state->state_type != STATE_TYPE_LAYOUT) {
			/* Skip non-layout states. */
			continue;
		}

		if (!get_state_entry_export_owner_refs(state,
						       &entry,
						       NULL,
						       &owner)) {
			/* This state_t is in the process of being destroyed,
			 * skip it.
			 */
			continue;
		}

		inc_state_t_ref(state);

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
		hold_export_lock = false;

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

		/* this deletes the state too */

		(void) nfs4_return_one_state(entry,
					     LAYOUTRETURN4_FILE,
					     circumstance_revoke,
					     state,
					     entire,
					     0,
					     NULL,
					     &deleted);

		if (!deleted) {
			LogCrit(COMPONENT_PNFS,
				"Layout state not destroyed during export cleanup.");
			errcnt++;
		}

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Release the references taken above */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
		dec_state_owner_ref(owner);
		dec_state_t_ref(state);
		if (errcnt < STATE_ERR_MAX) {
			/* Loop again, but since we droped the export lock, we
			 * must restart.
			 */
			goto again;
		}

		/* Too many errors, quit. */
		break;
	}

	while (errcnt < STATE_ERR_MAX) {
		cache_entry_t *entry = NULL;
		state_owner_t *owner = NULL;

		if (!hold_export_lock) {
			PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
			hold_export_lock = true;
		}

		state = glist_first_entry(&op_ctx->export->exp_state_list,
					  state_t,
					  state_export_list);

		if (state == NULL)
			break;

		/* Move state to the end of the list in case an error
		 * occurs or the state is going stale.
		 */
		glist_del(&state->state_export_list);
		glist_add_tail(&op_ctx->export->exp_state_list,
			       &state->state_export_list);

		if (!get_state_entry_export_owner_refs(state,
						       &entry,
						       NULL,
						       &owner)) {
			/* This state_t is in the process of being destroyed,
			 * skip it.
			 */
			continue;
		}

		inc_state_t_ref(state);

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
		hold_export_lock = false;

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

		if (state->state_type == STATE_TYPE_SHARE) {
			state_status = state_share_remove(entry, owner, state);

			if (!state_unlock_err_ok(state_status)) {
				PTHREAD_RWLOCK_unlock(&entry->state_lock);

				LogEvent(COMPONENT_CLIENTID,
					 "EXPIRY failed to release share stateid error %s",
					 state_err_str(state_status));
				errcnt++;

				/* Release the references taken above */
				cache_inode_lru_unref(entry, LRU_FLAG_NONE);
				dec_state_owner_ref(owner);
				dec_state_t_ref(state);
				continue;
			}
		}

		if (state->state_type == STATE_TYPE_DELEG) {
			/* this deletes the state too */
			state_deleg_revoke(entry, state);
		} else {
			state_del_locked(state);
		}

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Release the references taken above */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
		dec_state_owner_ref(owner);
		dec_state_t_ref(state);
	}

	if (hold_export_lock)
		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

	if (errcnt == STATE_ERR_MAX) {
		LogFatal(COMPONENT_STATE,
			 "Could not complete cleanup of layouts for export %s",
			 op_ctx->export->fullpath);
	}
}

#ifdef DEBUG_SAL
void dump_all_states(void)
{
	state_t *state;
	state_owner_t *owner;

	if (!isFullDebug(COMPONENT_STATE))
		return;

	pthread_mutex_lock(&all_state_v4_mutex);

	if (!glist_empty(&state_v4_all)) {
		struct glist_head *glist;

		LogFullDebug(COMPONENT_STATE, " =State List= ");

		glist_for_each(glist, &state_v4_all) {
			char str1[LOG_BUFF_LEN / 2];
			char str2[LOG_BUFF_LEN / 2];
			struct display_buffer dspbuf1 = {
						sizeof(str1), str1, str1};
			struct display_buffer dspbuf2 = {
						sizeof(str2), str2, str2};

			state = glist_entry(glist, state_t, state_list_all);
			owner = get_state_owner_ref(state);

			display_owner(&dspbuf1, owner);
			display_stateid(&dspbuf2, state);

			LogFullDebug(COMPONENT_STATE,
				     "State {%s} owner {%s}",
				     str2, str1);

			if (owner != NULL)
				dec_state_owner_ref(owner);
		}

		LogFullDebug(COMPONENT_STATE, " ----------------------");
	} else
		LogFullDebug(COMPONENT_STATE, "All states released");

	pthread_mutex_unlock(&all_state_v4_mutex);
}
#endif

/** @} */
