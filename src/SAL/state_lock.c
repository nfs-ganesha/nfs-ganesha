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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file    state_lock.c
 * @brief   Functions used in lock management.
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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"

/* Forward declaration */
static state_status_t do_lock_op(cache_entry_t *entry,
				 fsal_lock_op_t lock_op,
				 state_owner_t *owner,
				 fsal_lock_param_t *lock,
				 state_owner_t **holder,
				 fsal_lock_param_t *conflict,
				 bool_t overlap,
				 lock_type_t sle_type);

/**
 * @page state_lock_entry_locking state_lock_entry_t locking rule
 *
 * The value is always updated/read with @c nlm_lock_entry->lock held If
 * we have @c nlm_lock_list mutex held we can read it safely, because the
 * value is always updated while walking the list with @c entry->state_lock
 * held.
 *
 * The update happens as below:
 * @code{.c}
 *  pthread_rwlock_wrlock(&entry->state_mutex)
 *  pthread_mutex_lock(lock_entry->sle_mutex)
 *  update the lock_entry value
 *  ........
 * @endcode
 *
 * The value is ref counted with nlm_lock_entry->sle_ref_count so that
 * a parallel cancel/unlock won't endup freeing the datastructure. The
 * last release on the data structure ensure that it is freed.
 */

#ifdef DEBUG_SAL
/**
 * @brief All locks.
 */
static struct glist_head state_all_locks;
/**
 * @brief All locks mutex
 */
pthread_mutex_t all_locks_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * @brief All locks blocked in FSAL
 */
struct glist_head state_blocked_locks;

/**
 * @brief Mutex to protect lock lists
 */
pthread_mutex_t blocked_locks_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Owner of state with no defined owner
 */
state_owner_t unknown_owner;

/**
 * @brief Blocking lock cookies
 */

/**
 * Parameters used for lock cookie hash table initialization.
 *
 * @todo Switch the cookie table to something else and get rid
 * of this.
 */

static hash_parameter_t cookie_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = lock_cookie_value_hash_func,
	.hash_func_rbt = lock_cookie_rbt_hash_func,
	.compare_key = compare_lock_cookie_key,
	.key_to_str = display_lock_cookie_key,
	.val_to_str = display_lock_cookie_val,
	.flags = HT_FLAG_NONE,
};

static hash_table_t *ht_lock_cookies;

/**
 * @brief Initalize locking
 *
 * @return State status.
 */
state_status_t state_lock_init(void)
{
	state_status_t status = STATE_SUCCESS;

	memset(&unknown_owner, 0, sizeof(unknown_owner));
	unknown_owner.so_owner_val = "ganesha_unknown_owner";
	unknown_owner.so_type = STATE_LOCK_OWNER_UNKNOWN;
	unknown_owner.so_refcount = 1;
	unknown_owner.so_owner_len = strlen(unknown_owner.so_owner_val);

	glist_init(&unknown_owner.so_lock_list);

	if (pthread_mutex_init(&unknown_owner.so_mutex, NULL) == -1) {
		status = STATE_INIT_ENTRY_FAILED;
		return status;
	}

	ht_lock_cookies = hashtable_init(&cookie_param);
	if (ht_lock_cookies == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init NLM Client cache");
		status = STATE_INIT_ENTRY_FAILED;
		return status;
	}
#ifdef DEBUG_SAL
	glist_init(&state_all_locks);
	glist_init(&state_owners_all);
	glist_init(&state_v4_all);
#endif

	glist_init(&state_blocked_locks);

	status = state_async_init();

	state_owner_pool =
	    pool_init("NFSv4 state owners", sizeof(state_owner_t),
		      pool_basic_substrate, NULL, NULL, NULL);

	state_v4_pool =
	    pool_init("NFSv4 files states", sizeof(state_t),
		      pool_basic_substrate, NULL, NULL, NULL);
	return status;
}

/**
 * @brief Check whether a lock is from NLM
 *
 * @param[in] lock_entry Lock to check
 *
 * @retval true if the lock is from NLM.
 * @retval false if the lock is not from NLM.
 */
bool lock_owner_is_nlm(state_lock_entry_t *lock_entry)
{
	return lock_entry->sle_owner->so_type == STATE_LOCK_OWNER_NLM;
}

/******************************************************************************
 *
 * Functions to display various aspects of a lock
 *
 ******************************************************************************/
/**
 * @brief Find the end of lock range
 *
 * @param[in] lock Lock to check
 *
 * @return Last byte of lock.
 */
static inline uint64_t lock_end(fsal_lock_param_t *lock)
{
	if (lock->lock_length == 0)
		return UINT64_MAX;
	else
		return lock->lock_start + lock->lock_length - 1;
}

/**
 * @brief String for lock type
 *
 * @param[in] ltype Lock type
 *
 * @return Readable string.
 */
const char *str_lockt(fsal_lock_t ltype)
{
	switch (ltype) {
	case FSAL_LOCK_R:
		return "READ ";
	case FSAL_LOCK_W:
		return "WRITE";
	case FSAL_NO_LOCK:
		return "NO LOCK";
	}
	return "?????";
}

/**
 * @brief Return string for blocking status
 *
 * @param[in] blocking Blocking status
 *
 * @return String for blocking status.
 */
const char *str_blocking(state_blocking_t blocking)
{
	switch (blocking) {
	case STATE_NON_BLOCKING:
		return "NON_BLOCKING  ";
	case STATE_NLM_BLOCKING:
		return "NLM_BLOCKING  ";
	case STATE_NFSV4_BLOCKING:
		return "NFSV4_BLOCKING";
	case STATE_GRANTING:
		return "GRANTING      ";
	case STATE_CANCELED:
		return "CANCELED      ";
	}
	return "unknown       ";
}

/**
 * @brief Return string for blocking status
 *
 * @param[in] blocked Blocking status
 *
 * @return String for blocking status.
 */
const char *str_blocked(state_blocking_t blocked)
{
	switch (blocked) {
	case STATE_NON_BLOCKING:
		return "GRANTED       ";
	case STATE_NLM_BLOCKING:
		return "NLM_BLOCKING  ";
	case STATE_NFSV4_BLOCKING:
		return "NFSV4_BLOCKING";
	case STATE_GRANTING:
		return "GRANTING      ";
	case STATE_CANCELED:
		return "CANCELED      ";
	}
	return "unknown       ";
}

/******************************************************************************
 *
 * Function to compare lock parameters
 *
 ******************************************************************************/

/**
 * @brief Check if locks differ
 *
 * @note This is not complete, it doesn't check the owner's IP
 *       address.
 *
 * @param[in] lock1 A lock
 * @param[in] lock2 Another lock
 *
 * @retval true if locks differ.
 * @retval false if locks are the same.
 */
static inline bool different_lock(fsal_lock_param_t *lock1,
				  fsal_lock_param_t *lock2)
{
	return (lock1->lock_type != lock2->lock_type)
	    || (lock1->lock_start != lock2->lock_start)
	    || (lock1->lock_length != lock2->lock_length);
}

/******************************************************************************
 *
 * Functions to log locks in various ways
 *
 ******************************************************************************/
/**
 * @brief Log a lock entry
 *
 * @param[in] reason Arbitrary string
 * @param[in] le     Entry to log
 */
static void LogEntry(const char *reason, state_lock_entry_t *le)
{
	if (isFullDebug(COMPONENT_STATE)) {
		char owner[HASHTABLE_DISPLAY_STRLEN];

		DisplayOwner(le->sle_owner, owner);

		LogFullDebug(COMPONENT_STATE,
			     "%s Entry: %p entry=%p, fileid=%" PRIu64
			     ", export=%u, type=%s, start=0x%llx, end=0x%llx, blocked=%s/%p, state=%p, refcount=%d, type %d owner={%s}",
			     reason, le, le->sle_entry,
			     (uint64_t) le->sle_entry->obj_handle->attributes.
			     fileid, (unsigned int)le->sle_export->export_id,
			     str_lockt(le->sle_lock.lock_type),
			     (unsigned long long)le->sle_lock.lock_start,
			     (unsigned long long)lock_end(&le->sle_lock),
			     str_blocked(le->sle_blocked), le->sle_block_data,
			     le->sle_state, le->sle_ref_count, le->sle_type,
			     owner);
	}
}

/**
 * @brief Log a list of locks
 *
 * @param[in] reason Arbitrary string
 * @param[in] entry  Cache entry (mostly unused)
 * @param[in] list   List of lock entries
 *
 * @retval true if list is empty.
 * @retval false if list is non-empty.
 */
static bool LogList(const char *reason, cache_entry_t *entry,
		    struct glist_head *list)
{
	if (isFullDebug(COMPONENT_STATE)) {
		struct glist_head *glist;
		state_lock_entry_t *found_entry;

		if (glist_empty(list)) {
			if (entry != NULL)
				LogFullDebug(COMPONENT_STATE,
					     "%s for %p is empty", reason,
					     entry);
			else
				LogFullDebug(COMPONENT_STATE, "%s is empty",
					     reason);
			return true;
		}

		glist_for_each(glist, list) {
			found_entry =
				glist_entry(glist, state_lock_entry_t,
					    sle_list);
			LogEntry(reason, found_entry);
			if (found_entry->sle_entry == NULL)
				break;
		}
	}

	return false;
}

/**
 * @brief Log blocked locks on list
 *
 * @param[in] reason Arbitrary string
 * @param[in] entry  Cache entry
 * @param[in] list   List of lock entries
 *
 * @retval true if list is empty.
 * @retval false if list is non-empty.
 */
static bool LogBlockedList(const char *reason, cache_entry_t *entry,
			   struct glist_head *list)
{
	if (isFullDebug(COMPONENT_STATE)) {
		struct glist_head *glist;
		state_lock_entry_t *found_entry;
		state_block_data_t *block_entry;

		if (glist_empty(list)) {
			if (entry != NULL)
				LogFullDebug(COMPONENT_STATE,
					     "%s for %p is empty", reason,
					     entry);
			else
				LogFullDebug(COMPONENT_STATE, "%s is empty",
					     reason);
			return true;
		}

		glist_for_each(glist, list) {
			block_entry =
			    glist_entry(glist, state_block_data_t, sbd_list);
			found_entry = block_entry->sbd_lock_entry;
			LogEntry(reason, found_entry);
			if (found_entry->sle_entry == NULL)
				break;
		}
	}

	return false;
}

/**
 * @brief Log a lock
 *
 * @param[in] component Component to log to
 * @param[in] debug     Log level
 * @param[in] reason    Arbitrary string
 * @param[in] entry     Cache entry
 * @param[in] owner     Lock owner
 * @param[in] lock      Lock description
 */
void LogLock(log_components_t component, log_levels_t debug, const char *reason,
	     cache_entry_t *entry, state_owner_t *owner,
	     fsal_lock_param_t *lock)
{
	if (isLevel(component, debug)) {
		char owner_str[HASHTABLE_DISPLAY_STRLEN];

		if (owner != NULL)
			DisplayOwner(owner, owner_str);
		else
			sprintf(owner_str, "NONE");

		LogAtLevel(component, debug,
			   "%s Lock: entry=%p, fileid=%" PRIu64
			   ", type=%s, start=0x%llx, end=0x%llx, owner={%s}",
			   reason, entry,
			   (uint64_t) entry->obj_handle->attributes.fileid,
			   str_lockt(lock->lock_type),
			   (unsigned long long)lock->lock_start,
			   (unsigned long long)lock_end(lock), owner_str);
	}
}

/**
 * @brief Log a lock description
 *
 * @param[in] component Component to log to
 * @param[in] debug     Log level
 * @param[in] reason    Arbitrary string
 * @param[in] entry     Cache entry
 * @param[in] owner     Lock owner
 * @param[in] lock      Lock description
 */
void LogLockDesc(log_components_t component, log_levels_t debug,
		 const char *reason, cache_entry_t *entry, void *owner,
		 fsal_lock_param_t *lock)
{
	LogAtLevel(component, debug,
		   "%s Lock: entry=%p, owner=%p, type=%s, start=0x%llx, end=0x%llx",
		   reason, entry, owner, str_lockt(lock->lock_type),
		   (unsigned long long)lock->lock_start,
		   (unsigned long long)lock_end(lock));
}

/**
 * @brief Log all locks
 *
 * @param[in] label Arbitrary string
 */
void dump_all_locks(const char *label)
{
#ifdef DEBUG_SAL
	struct glist_head *glist;

	pthread_mutex_lock(&all_locks_mutex);

	if (glist_empty(&state_all_locks)) {
		LogFullDebug(COMPONENT_STATE, "All Locks are freed");
		pthread_mutex_unlock(&all_locks_mutex);
		return;
	}

	glist_for_each(glist, &state_all_locks)
	    LogEntry(label,
		     glist_entry(glist, state_lock_entry_t, sle_all_locks));

	pthread_mutex_unlock(&all_locks_mutex);
#else
	return;
#endif
}

/******************************************************************************
 *
 * Functions to manage lock entries and lock list
 *
 ******************************************************************************/
/**
 * @brief Create a lock entry
 *
 * @param[in] entry    Cache entry to lock
 * @param[in] export   Export being accessed
 * @param[in] blocked  Blocking status
 * @param[in] owner    Lock owner
 * @param[in] state    State associated with lock
 * @param[in] lock     Lock description
 * @param[in] sle_type Lock type
 *
 * @return The new entry or NULL.
 */
static state_lock_entry_t *create_state_lock_entry(cache_entry_t *entry,
						   struct gsh_export *export,
						   state_blocking_t blocked,
						   state_owner_t *owner,
						   state_t *state,
						   fsal_lock_param_t *lock,
						   lock_type_t sle_type)
{
	state_lock_entry_t *new_entry;

	new_entry = gsh_malloc(sizeof(*new_entry));
	if (!new_entry)
		return NULL;

	LogFullDebug(COMPONENT_STATE, "new_entry = %p owner %p", new_entry,
		     owner);

	memset(new_entry, 0, sizeof(*new_entry));

	if (pthread_mutex_init(&new_entry->sle_mutex, NULL) == -1) {
		gsh_free(new_entry);
		return NULL;
	}

	/* sle_block_data will be filled in later if necessary */
	new_entry->sle_block_data = NULL;
	new_entry->sle_ref_count = 1;
	new_entry->sle_entry = entry;
	new_entry->sle_type = sle_type;
	new_entry->sle_blocked = blocked;
	new_entry->sle_owner = owner;
	new_entry->sle_state = state;
	new_entry->sle_lock = *lock;
	new_entry->sle_export = export;

	if (owner->so_type == STATE_LOCK_OWNER_NLM) {
		/* Add to list of locks owned by client that owner belongs to */
		inc_nsm_client_ref(owner->so_owner.so_nlm_owner.so_client->
				   slc_nsm_client);

		pthread_mutex_lock(&owner->so_owner.so_nlm_owner.so_client
				   ->slc_nsm_client->ssc_mutex);
		glist_add_tail(&owner->so_owner.so_nlm_owner.so_client->
			       slc_nsm_client->ssc_lock_list,
			       &new_entry->sle_client_locks);

		pthread_mutex_unlock(&owner->so_owner.so_nlm_owner.so_client
				     ->slc_nsm_client->ssc_mutex);
	}

	/* Add to list of locks owned by export */
	PTHREAD_RWLOCK_wrlock(&export->lock);
	glist_add_tail(&export->exp_lock_list,
		       &new_entry->sle_export_locks);
	PTHREAD_RWLOCK_unlock(&export->lock);

	/* Add to list of locks owned by owner */
	inc_state_owner_ref(owner);

	pthread_mutex_lock(&owner->so_mutex);

	if (owner->so_type == STATE_LOCK_OWNER_NFSV4 && state != NULL) {
		glist_add_tail(&state->state_data.lock.state_locklist,
			       &new_entry->sle_state_locks);
	}

	glist_add_tail(&owner->so_lock_list, &new_entry->sle_owner_locks);

	pthread_mutex_unlock(&owner->so_mutex);

#ifdef DEBUG_SAL
	pthread_mutex_lock(&all_locks_mutex);

	glist_add_tail(&state_all_locks, &new_entry->sle_all_locks);

	pthread_mutex_unlock(&all_locks_mutex);
#endif

	return new_entry;
}

/**
 * @brief Duplicate a lock entry
 *
 * @param[in] orig_entry Entry to duplicate
 *
 * @return New entry or NULL.
 */
inline state_lock_entry_t *state_lock_entry_t_dup(state_lock_entry_t *
						  orig_entry)
{
	return create_state_lock_entry(orig_entry->sle_entry,
				       orig_entry->sle_export,
				       orig_entry->sle_blocked,
				       orig_entry->sle_owner,
				       orig_entry->sle_state,
				       &orig_entry->sle_lock,
				       orig_entry->sle_type);
}

/**
 * @brief Take a reference on a lock entry
 *
 * @param[in,out] lock_entry Entry to reference
 */
void lock_entry_inc_ref(state_lock_entry_t *lock_entry)
{
	pthread_mutex_lock(&lock_entry->sle_mutex);
	lock_entry->sle_ref_count++;
	LogEntry("Increment refcount", lock_entry);
	pthread_mutex_unlock(&lock_entry->sle_mutex);
}

/**
 * @brief Relinquish a reference on a lock entry
 *
 * @param[in,out] lock_entry Entry to release
 */
void lock_entry_dec_ref(state_lock_entry_t *lock_entry)
{
	bool to_free = false;

	pthread_mutex_lock(&lock_entry->sle_mutex);

	lock_entry->sle_ref_count--;

	LogEntry("Decrement refcount", lock_entry);

	if (!lock_entry->sle_ref_count) {
		/*
		 * We should already be removed from the lock_list
		 * So we can free the lock_entry without any locking
		 */
		to_free = true;
	}

	pthread_mutex_unlock(&lock_entry->sle_mutex);

	if (to_free) {
		LogEntry("Freeing", lock_entry);

		/* Release block data if present */
		if (lock_entry->sle_block_data != NULL) {
			/* need to remove from the state_blocked_locks list */
			glist_del(&lock_entry->sle_block_data->sbd_list);
			gsh_free(lock_entry->sle_block_data);
		}
#ifdef DEBUG_SAL
		pthread_mutex_lock(&all_locks_mutex);
		glist_del(&lock_entry->sle_all_locks);
		pthread_mutex_unlock(&all_locks_mutex);
#endif

		gsh_free(lock_entry);
	}
}

/**
 * @brief Remove an entry from the lock lists
 *
 * @param[in,out] lock_entry Entry to remove
 */
static void remove_from_locklist(state_lock_entry_t *lock_entry)
{
	state_owner_t *owner = lock_entry->sle_owner;

	LogEntry("Removing", lock_entry);

	/*
	 * If some other thread is holding a reference to this nlm_lock_entry
	 * don't free the structure. But drop from the lock list
	 */
	if (owner != NULL) {
		if (owner->so_type == STATE_LOCK_OWNER_NLM) {
			/* Remove from list of locks owned
			 * by client that owner belongs to
			 */
			pthread_mutex_lock(&owner->so_owner.so_nlm_owner
					   .so_client->slc_nsm_client
					   ->ssc_mutex);

			glist_del(&lock_entry->sle_client_locks);

			pthread_mutex_unlock(&owner->so_owner.so_nlm_owner
					     .so_client->slc_nsm_client
					     ->ssc_mutex);

			dec_nsm_client_ref(owner->so_owner.so_nlm_owner.
					   so_client->slc_nsm_client);
		}

		/* Remove from list of locks owned by export */
		PTHREAD_RWLOCK_wrlock(&lock_entry->sle_export->lock);
		glist_del(&lock_entry->sle_export_locks);
		PTHREAD_RWLOCK_unlock(&lock_entry->sle_export->lock);

		/* Remove from list of locks owned by owner */
		pthread_mutex_lock(&owner->so_mutex);

		if (owner->so_type == STATE_LOCK_OWNER_NFSV4)
			glist_del(&lock_entry->sle_state_locks);

		glist_del(&lock_entry->sle_owner_locks);

		pthread_mutex_unlock(&owner->so_mutex);

		dec_state_owner_ref(owner);
	}

	lock_entry->sle_owner = NULL;
	glist_del(&lock_entry->sle_list);
	lock_entry_dec_ref(lock_entry);
}

/**
 * @brief Find a conflicting entry
 *
 * @param[in] entry The file to search
 * @param[in] owner The lock owner
 * @param[in] lock  Lock to check
 *
 * @return A conflicting entry or NULL.
 */
static state_lock_entry_t *get_overlapping_entry(cache_entry_t *entry,
						 state_owner_t *owner,
						 fsal_lock_param_t *lock)
{
	struct glist_head *glist;
	state_lock_entry_t *found_entry = NULL;
	uint64_t found_entry_end, range_end = lock_end(lock);

	glist_for_each(glist, &entry->object.file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		LogEntry("Checking", found_entry);

		/* Skip blocked or cancelled locks */
		if (found_entry->sle_blocked == STATE_NLM_BLOCKING
		    || found_entry->sle_blocked == STATE_NFSV4_BLOCKING
		    || found_entry->sle_blocked == STATE_CANCELED)
			continue;

		found_entry_end = lock_end(&found_entry->sle_lock);

		if ((found_entry_end >= lock->lock_start)
		    && (found_entry->sle_lock.lock_start <= range_end)) {
			/* lock overlaps see if we can allow:
			 * allow if neither lock is exclusive or
			 * the owner is the same
			 */
			if ((found_entry->sle_lock.lock_type == FSAL_LOCK_W
			     || lock->lock_type == FSAL_LOCK_W)
			    && different_owners(found_entry->sle_owner, owner)
			    ) {
				/* found a conflicting lock, return it */
				return found_entry;
			}
		}
	}

	return NULL;
}

/**
 * @brief Add a lock, potentially merging with existing locks
 *
 * We need to iterate over the full lock list and remove
 * any mapping entry. And l_offset = 0 and sle_lock.lock_length = 0 lock_entry
 * implies remove all entries
 *
 * @param[in,out] entry      File to operate on
 * @param[in]     lock_entry Lock to add
 */
static void merge_lock_entry(cache_entry_t *entry,
			     state_lock_entry_t *lock_entry)
{
	state_lock_entry_t *check_entry;
	state_lock_entry_t *check_entry_right;
	uint64_t check_entry_end;
	uint64_t lock_entry_end;
	struct glist_head *glist;
	struct glist_head *glistn;

	/* lock_entry might be STATE_NON_BLOCKING or STATE_GRANTING */

	glist_for_each_safe(glist, glistn, &entry->object.file.lock_list) {
		check_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		/* Skip entry being merged - it could be in the list */
		if (check_entry == lock_entry)
			continue;

		if (different_owners
		    (check_entry->sle_owner, lock_entry->sle_owner))
			continue;

		/* Only merge fully granted locks */
		if (check_entry->sle_blocked != STATE_NON_BLOCKING)
			continue;

		check_entry_end = lock_end(&check_entry->sle_lock);
		lock_entry_end = lock_end(&lock_entry->sle_lock);

		if ((check_entry_end + 1) < lock_entry->sle_lock.lock_start)
			/* nothing to merge */
			continue;

		if ((lock_entry_end + 1) < check_entry->sle_lock.lock_start)
			/* nothing to merge */
			continue;

		/* Need to handle locks of different types differently, may
		 * split an old lock. If new lock totally overlaps old lock,
		 * the new lock will replace the old lock so no special work
		 * to be done.
		 */
		if ((check_entry->sle_lock.lock_type !=
		     lock_entry->sle_lock.lock_type)
		    && ((lock_entry_end < check_entry_end)
			|| (check_entry->sle_lock.lock_start <
			    lock_entry->sle_lock.lock_start))) {
			if (lock_entry_end < check_entry_end
			    && check_entry->sle_lock.lock_start <
			    lock_entry->sle_lock.lock_start) {
				/* Need to split old lock */
				check_entry_right =
				    state_lock_entry_t_dup(check_entry);
				if (check_entry_right == NULL) {
					/** @todo FSF: OOPS....
					 * Leave old lock in place, it may cause
					 * false conflicts, but should
					 * eventually be released
					 */
					LogMajor(COMPONENT_STATE,
						 "Memory allocation failure during lock upgrade/downgrade");
					continue;
				}
				glist_add_tail(&entry->object.file.lock_list,
					       &(check_entry_right->sle_list));
			} else {
				/* No split, just shrink, make the logic below
				 * work on original lock
				 */
				check_entry_right = check_entry;
			}
			if (lock_entry_end < check_entry_end) {
				/* Need to shrink old lock from beginning
				 * (right lock if split)
				 */
				LogEntry("Merge shrinking right",
					 check_entry_right);
				check_entry_right->sle_lock.lock_start =
				    lock_entry_end + 1;
				check_entry_right->sle_lock.lock_length =
				    check_entry_end - lock_entry_end;
				LogEntry("Merge shrunk right",
					 check_entry_right);
			}
			if (check_entry->sle_lock.lock_start <
			    lock_entry->sle_lock.lock_start) {
				/* Need to shrink old lock from end
				 * (left lock if split)
				 */
				LogEntry("Merge shrinking left", check_entry);
				check_entry->sle_lock.lock_length =
				    lock_entry->sle_lock.lock_start -
				    check_entry->sle_lock.lock_start;
				LogEntry("Merge shrunk left", check_entry);
			}
			/* Done splitting/shrinking old lock */
			continue;
		}

		/* check_entry touches or overlaps lock_entry, expand
		 * lock_entry
		 */
		if (lock_entry_end < check_entry_end)
			/* Expand end of lock_entry */
			lock_entry_end = check_entry_end;

		if (check_entry->sle_lock.lock_start <
		    lock_entry->sle_lock.lock_start)
			/* Expand start of lock_entry */
			lock_entry->sle_lock.lock_start =
			    check_entry->sle_lock.lock_start;

		/* Compute new lock length */
		lock_entry->sle_lock.lock_length =
		    lock_entry_end - lock_entry->sle_lock.lock_start + 1;

		/* Remove merged entry */
		LogEntry("Merged", lock_entry);
		LogEntry("Merging removing", check_entry);
		remove_from_locklist(check_entry);
	}
}

/**
 * @brief Free a list of lock entries
 *
 * @param[in] list List of locks to free
 */
static void free_list(struct glist_head *list)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist, *glistn;

	glist_for_each_safe(glist, glistn, list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		remove_from_locklist(found_entry);
	}
}

/**
 * @brief Subtract a lock from a lock entry.
 *
 * This function places any remaining bits into the split list.
 *
 * @param[in,out] entry       File on which to operate
 * @param[in,out] found_entry Lock being modified
 * @param[in]     lock        Lock being removed
 * @param[out]    split_list  Remaining fragments of found_entry
 * @param[out]    remove_list Removed lock entries
 * @param[out]    removed     True if lock is removed
 *
 * @return State status.
 */
static state_status_t subtract_lock_from_entry(cache_entry_t *entry,
					       state_lock_entry_t *found_entry,
					       fsal_lock_param_t *lock,
					       struct glist_head *split_list,
					       struct glist_head *remove_list,
					       bool *removed)
{
	uint64_t found_entry_end = lock_end(&found_entry->sle_lock);
	uint64_t range_end = lock_end(lock);
	state_lock_entry_t *found_entry_left = NULL;
	state_lock_entry_t *found_entry_right = NULL;
	state_status_t status = STATE_SUCCESS;

	if (range_end < found_entry->sle_lock.lock_start) {
		/* nothing to split */
		*removed = false;
		return status;
	}

	if (found_entry_end < lock->lock_start) {
		/* nothing to split */
		*removed = false;
		return status;
	}

	if ((lock->lock_start <= found_entry->sle_lock.lock_start)
	    && range_end >= found_entry_end) {
		/* Fully overlap */
		LogEntry("Remove Complete", found_entry);
		goto complete_remove;
	}

	LogEntry("Split", found_entry);

	/* Delete the old entry and add one or two new entries */
	if (lock->lock_start > found_entry->sle_lock.lock_start) {
		found_entry_left = state_lock_entry_t_dup(found_entry);
		if (found_entry_left == NULL) {
			free_list(split_list);
			*removed = false;
			status = STATE_MALLOC_ERROR;
			return status;
		}

		found_entry_left->sle_lock.lock_length =
		    lock->lock_start - found_entry->sle_lock.lock_start;
		LogEntry("Left split", found_entry_left);
		glist_add_tail(split_list, &(found_entry_left->sle_list));
	}

	if (range_end < found_entry_end) {
		found_entry_right = state_lock_entry_t_dup(found_entry);
		if (found_entry_right == NULL) {
			free_list(split_list);
			*removed = false;
			status = STATE_MALLOC_ERROR;
			return status;
		}

		found_entry_right->sle_lock.lock_start = range_end + 1;
		found_entry_right->sle_lock.lock_length =
		    found_entry_end - range_end;
		LogEntry("Right split", found_entry_right);
		glist_add_tail(split_list, &(found_entry_right->sle_list));
	}

 complete_remove:

	/* Remove the lock from the list it's
	 * on and put it on the remove_list
	 */
	glist_del(&found_entry->sle_list);
	glist_add_tail(remove_list, &(found_entry->sle_list));

	*removed = true;
	return status;
}

/**
 * @brief Subtract a delegation from a list of delegations
 *
 * Subtract a delegation from a list of delegations
 *
 * @param[in,out] entry   Cache entry on which to operate
 * @param[in]     owner   Client owner of delegation
 * @param[in]     state   Associated lock state
 * @param[in]     lock    Delegation to remove
 * @param[out]    removed True if an entry was removed
 * @param[in,out] list    List of locks to modify
 *
 * @return State status.
 */
static state_status_t subtract_deleg_from_list(cache_entry_t *entry,
					       state_owner_t *owner,
					       state_t *state,
					       bool *removed,
					       struct glist_head *list)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist, *glistn;
	state_status_t status = STATE_SUCCESS;

	*removed = false;

	glist_for_each_safe(glist, glistn, list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);
		if (owner != NULL
		    && different_owners(found_entry->sle_owner, owner))
			continue;

		if (found_entry->sle_type != LEASE_LOCK)
			continue;

		/* Tell GPFS delegation is returned then remove from list. */
		glist_del(&found_entry->sle_list);
		*removed = true;
	}
	return status;
}

/**
 * @brief Subtract a lock from a list of locks
 *
 * This function possibly splits entries in the list.
 *
 * @param[in,out] entry   Cache entry on which to operate
 * @param[in]     owner   Lock owner
 * @param[in]     state   Associated lock state
 * @param[in]     lock    Lock to remove
 * @param[out]    removed True if an entry was removed
 * @param[in,out] list    List of locks to modify
 *
 * @return State status.
 */
static state_status_t subtract_lock_from_list(cache_entry_t *entry,
					      state_owner_t *owner,
					      state_t *state,
					      fsal_lock_param_t *lock,
					      bool *removed,
					      struct glist_head *list)
{
	state_lock_entry_t *found_entry;
	struct glist_head split_lock_list, remove_list;
	struct glist_head *glist, *glistn;
	state_status_t status = STATE_SUCCESS;
	bool removed_one = false;

	*removed = false;

	glist_init(&split_lock_list);
	glist_init(&remove_list);

	glist_for_each_safe(glist, glistn, list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		if (owner != NULL
		    && different_owners(found_entry->sle_owner, owner))
			continue;

		/* Only care about granted locks */
		if (found_entry->sle_blocked != STATE_NON_BLOCKING)
			continue;

		/* Skip locks owned by this NLM state.
		 * This protects NLM locks from the current iteration of an NLM
		 * client from being released by SM_NOTIFY.
		 */
		if (state != NULL && lock_owner_is_nlm(found_entry)
		    && found_entry->sle_state == state)
			continue;

		/* We have matched owner. Even though we are taking a reference
		 * to found_entry, we don't inc the ref count because we want
		 * to drop the lock entry.
		 */
		status =
		    subtract_lock_from_entry(entry, found_entry, lock,
					     &split_lock_list, &remove_list,
					     &removed_one);
		*removed |= removed_one;

		if (status != STATE_SUCCESS) {
			/* We ran out of memory while splitting,
			 * deal with it outside loop
			 */
			break;
		}
	}

	if (status != STATE_SUCCESS) {
		/* We ran out of memory while splitting. split_lock_list
		 * has been freed. For each entry on the remove_list, put
		 * it back on the list.
		 */
		LogDebug(COMPONENT_STATE, "Failed %s", state_err_str(status));
		glist_for_each_safe(glist, glistn, &remove_list) {
			found_entry =
			    glist_entry(glist, state_lock_entry_t, sle_list);
			glist_del(&found_entry->sle_list);
			glist_add_tail(list, &(found_entry->sle_list));
		}
	} else {
		/* free the enttries on the remove_list */
		free_list(&remove_list);

		/* now add the split lock list */
		glist_add_list_tail(list, &split_lock_list);
	}

	LogFullDebug(COMPONENT_STATE,
		     "List of all locks for entry=%p returning %d", entry,
		     status);

	return status;
}

/**
 * @brief Remove locks in list from another list of locks
 *
 * @param[in]     entry  File to modify
 * @param[in,out] target List of locks to modify
 * @param[in]     source List of locks to subtract
 *
 * @return State status.
 */
static state_status_t subtract_list_from_list(cache_entry_t *entry,
					      struct glist_head *target,
					      struct glist_head *source)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist, *glistn;
	state_status_t status = STATE_SUCCESS;
	bool removed = false;

	glist_for_each_safe(glist, glistn, source) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		status =
		    subtract_lock_from_list(entry, NULL, NULL,
					    &found_entry->sle_lock, &removed,
					    target);
		if (status != STATE_SUCCESS)
			break;
	}

	return status;
}

/******************************************************************************
 *
 * Implement hash table to hash blocked lock entries by cookie
 *
 ******************************************************************************/

static void grant_blocked_locks(cache_entry_t *entry);

/**
 * @brief Display lock cookie in hash table
 *
 * @param[in]  buff Key to display
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_lock_cookie_key(struct gsh_buffdesc *buff, char *str)
{
	return DisplayOpaqueValue(buff->addr, buff->len, str);
}

/**
 * @brief Display lock cookie entry
 *
 * @param[in]  he  Cookie entry to display
 * @param[out] str Output buffer
 *
 * @return Length of output string.
 */
int display_lock_cookie_entry(state_cookie_entry_t *he, char *str)
{
	char *tmp = str;

	tmp += sprintf(tmp, "%p: cookie ", he);
	tmp += DisplayOpaqueValue(he->sce_cookie, he->sce_cookie_size, tmp);
	tmp += sprintf(tmp,
		" entry {%p fileid=%" PRIu64 "} lock {",
		he->sce_entry,
		(uint64_t) he->sce_entry->obj_handle->attributes.fileid);
	if (he->sce_lock_entry != NULL) {
		tmp += sprintf(tmp, "%p owner {", he->sce_lock_entry);

		tmp += DisplayOwner(he->sce_lock_entry->sle_owner, tmp);

		tmp += sprintf(tmp,
			"} type=%s start=0x%"PRIx64" end=0x%"PRIx64
			" blocked=%s}",
			str_lockt(he->sce_lock_entry->sle_lock.lock_type),
			he->sce_lock_entry->sle_lock.lock_start,
			lock_end(&he->sce_lock_entry->sle_lock),
			str_blocked(he->sce_lock_entry->sle_blocked));
	} else {
		tmp += sprintf(tmp, "<NULL>}");
	}

	return tmp - str;
}

/**
 * @brief Display lock cookie entry in hash table
 *
 * @param[in]  buff Value to display
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_lock_cookie_val(struct gsh_buffdesc *buff, char *str)
{
	return display_lock_cookie_entry(buff->addr, str);
}

/**
 * @brief Compare lock cookie in hash table
 *
 * @param[in] buff1 A key
 * @param[in] buff2 Another key
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_lock_cookie_key(struct gsh_buffdesc *buff1,
			    struct gsh_buffdesc *buff2)
{
	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[HASHTABLE_DISPLAY_STRLEN];
		char str2[HASHTABLE_DISPLAY_STRLEN];

		display_lock_cookie_key(buff1, str1);
		display_lock_cookie_key(buff2, str2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (buff1->addr == buff2->addr)
		return 0;

	if (buff1->len != buff2->len)
		return 1;

	if (buff1->addr == NULL || buff2->addr == NULL)
		return 1;

	return memcmp(buff1->addr, buff2->addr, buff1->len);
}

/**
 * @brief Hash index for lock cookie
 *
 * @todo Replace with a good hash function.
 *
 * @param[in] hparam Hash parameters
 * @param[in] key    Key to hash
 *
 * @return Hash index.
 */
uint32_t lock_cookie_value_hash_func(hash_parameter_t *hparam,
				     struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i;
	unsigned long res;
	unsigned char *addr = key->addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < key->len; i++)
		sum += (unsigned char)addr[i];

	res = (unsigned long)sum + (unsigned long)key->len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %lu",
			     res % hparam->index_size);

	return (unsigned long)(res % hparam->index_size);
}

/**
 * @brief RBT hash for lock cookie
 *
 * @todo Replace with a good hash function.
 *
 * @param[in] hparam Hash parameters
 * @param[in] key    Key to hash
 *
 * @return RBT hash.
 */
uint64_t lock_cookie_rbt_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i;
	unsigned long res;
	unsigned char *addr = key->addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < key->len; i++)
		sum += (unsigned char)addr[i];

	res = (unsigned long)sum + (unsigned long)key->len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

	return res;
}

/**
 * @brief Free a cookie entry
 *
 * @param[in] cookie_entry Entry to free
 * @param[in] unblock      Whether to remove block data
 */
void free_cookie(state_cookie_entry_t *cookie_entry, bool unblock)
{
	char str[HASHTABLE_DISPLAY_STRLEN];
	void *cookie = cookie_entry->sce_cookie;

	if (isFullDebug(COMPONENT_STATE))
		display_lock_cookie_entry(cookie_entry, str);

	/* Since the cookie is not in the hash table,
	 * we can just free the memory
	 */
	LogFullDebug(COMPONENT_STATE, "Free Lock Cookie {%s}", str);

	/* If block data is still attached to lock entry, remove it */
	if (cookie_entry->sce_lock_entry != NULL && unblock) {
		if (cookie_entry->sce_lock_entry->sle_block_data != NULL)
			cookie_entry->sce_lock_entry->sle_block_data->
			    sbd_blocked_cookie = NULL;

		lock_entry_dec_ref(cookie_entry->sce_lock_entry);
	}

	/* Free the memory for the cookie and the cookie entry */
	gsh_free(cookie);
	gsh_free(cookie_entry);
}

/**
 * @brief Add a grant cookie to a blocked lock
 *
 * @param[in]  entry        File to operate on
 * @param[in]  cookie       Cookie to add
 * @param[in]  cookie_size  Cookie length
 * @param[in]  lock_entry   Lock entry
 * @param[out] cookie_entry New cookie entry
 *
 * @return State status.
 */
state_status_t state_add_grant_cookie(cache_entry_t *entry,
				      void *cookie, int cookie_size,
				      state_lock_entry_t *lock_entry,
				      state_cookie_entry_t **cookie_entry)
{
	struct gsh_buffdesc buffkey, buffval;
	state_cookie_entry_t *hash_entry;
	char str[HASHTABLE_DISPLAY_STRLEN];
	state_status_t status = 0;

	*cookie_entry = NULL;

	if (lock_entry->sle_block_data == NULL || cookie == NULL
	    || cookie_size == 0) {
		/* Something's wrong with this entry */
		status = STATE_INCONSISTENT_ENTRY;
		return status;
	}

	if (isFullDebug(COMPONENT_STATE))
		DisplayOpaqueValue(cookie, cookie_size, str);

	hash_entry = gsh_malloc(sizeof(*hash_entry));
	if (hash_entry == NULL) {
		LogFullDebug(COMPONENT_STATE, "KEY {%s} NO MEMORY", str);
		status = STATE_MALLOC_ERROR;
		return status;
	}

	memset(hash_entry, 0, sizeof(*hash_entry));

	buffkey.addr = gsh_malloc(cookie_size);
	if (buffkey.addr == NULL) {
		LogFullDebug(COMPONENT_STATE, "KEY {%s} NO MEMORY", str);
		gsh_free(hash_entry);
		status = STATE_MALLOC_ERROR;
		return status;
	}

	hash_entry->sce_entry = entry;
	hash_entry->sce_lock_entry = lock_entry;
	hash_entry->sce_cookie = buffkey.addr;
	hash_entry->sce_cookie_size = cookie_size;

	memcpy(buffkey.addr, cookie, cookie_size);
	buffkey.len = cookie_size;
	buffval.addr = (void *)hash_entry;
	buffval.len = sizeof(*hash_entry);

	if (isFullDebug(COMPONENT_STATE))
		display_lock_cookie_entry(hash_entry, str);

	if (hashtable_test_and_set
	    (ht_lock_cookies, &buffkey, &buffval,
	     HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS) {
		gsh_free(hash_entry);
		LogFullDebug(COMPONENT_STATE,
			     "Lock Cookie {%s} HASH TABLE ERROR", str);
		status = STATE_HASH_TABLE_ERROR;
		return status;
	}

	/* Increment lock entry reference count and link it to the cookie */
	lock_entry_inc_ref(lock_entry);
	lock_entry->sle_block_data->sbd_blocked_cookie = hash_entry;

	LogFullDebug(COMPONENT_STATE, "Lock Cookie {%s} Added", str);

	switch (lock_entry->sle_block_data->sbd_grant_type) {
	case STATE_GRANT_NONE:
		/* Shouldn't get here */
		status = STATE_INCONSISTENT_ENTRY;
		break;

	case STATE_GRANT_FSAL_AVAILABLE:
		/* Now that we are sure we can continue, acquire the FSAL lock.
		 * If we get STATE_LOCK_BLOCKED we need to return...
		 */
		status = do_lock_op(entry, FSAL_OP_LOCKB,
				    lock_entry->sle_owner,
				    &lock_entry->sle_lock,
				    NULL, NULL, false, POSIX_LOCK);
		break;

	case STATE_GRANT_INTERNAL:
		/* Now that we are sure we can continue, acquire the FSAL lock.
		 * If we get STATE_LOCK_BLOCKED we need to return...
		 */
		status = do_lock_op(entry, FSAL_OP_LOCK,
				    lock_entry->sle_owner,
				    &lock_entry->sle_lock,
				    NULL, NULL, false, POSIX_LOCK);
		break;

	case STATE_GRANT_FSAL:
		/* No need to go to FSAL for lock */
		status = STATE_SUCCESS;
		break;
	}

	if (status != STATE_SUCCESS) {
		/* Lock will be returned to right blocking type if it is
		 * still blocking. We could lose a block if we failed for
		 * any other reason
		 */
		if (status == STATE_LOCK_BLOCKED)
			LogDebug(COMPONENT_STATE,
				 "Unable to lock FSAL for %s lock, error=%s",
				 str_blocked(lock_entry->sle_blocked),
				 state_err_str(status));
		else
			LogMajor(COMPONENT_STATE,
				 "Unable to lock FSAL for %s lock, error=%s",
				 str_blocked(lock_entry->sle_blocked),
				 state_err_str(status));

		LogEntry("Entry", lock_entry);

		/* And release the cookie without unblocking the lock.
		 * grant_blocked_locks() will decide whether to keep or
		 * free the block.
		 */
		free_cookie(hash_entry, false);

		return status;
	}

	*cookie_entry = hash_entry;
	return status;
}

/**
 * @brief Cancel a lock grant from the FSAL
 *
 * @param[in] cookie_entry Entry for the lock grant
 *
 * @return State status.
 */
state_status_t state_cancel_grant(state_cookie_entry_t *cookie_entry)
{
	state_status_t status = 0;
	/* We had acquired an FSAL lock, need to release it. */
	status = do_lock_op(cookie_entry->sce_entry,
			    FSAL_OP_UNLOCK,
			    cookie_entry->sce_lock_entry->sle_owner,
			    &cookie_entry->sce_lock_entry->sle_lock,
			    NULL,	/* no conflict expected */
			    NULL,
			    false,
			    POSIX_LOCK);

	if (status != STATE_SUCCESS)
		LogMajor(COMPONENT_STATE,
			 "Unable to unlock FSAL for canceled GRANTED lock, error=%s",
			 state_err_str(status));

	/* And release the cookie and unblock lock
	 * (because lock will be removed)
	 */
	free_cookie(cookie_entry, true);

	return status;
}

/**
 * @brief Find a grant matching a cookie
 *
 * @param[in]  cookie       Cookie to look up
 * @param[in]  cookie_size  Length of cookie
 * @param[out] cookie_entry Found entry
 *
 * @return State status.
 */
state_status_t state_find_grant(void *cookie, int cookie_size,
				state_cookie_entry_t **cookie_entry)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	struct gsh_buffdesc buffused_key;
	char str[HASHTABLE_DISPLAY_STRLEN];
	state_status_t status = 0;

	buffkey.addr = cookie;
	buffkey.len = cookie_size;

	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		display_lock_cookie_key(&buffkey, str);
		LogFullDebug(COMPONENT_STATE, "KEY {%s}", str);
	}

	if (HashTable_Del(ht_lock_cookies,
			  &buffkey,
			  &buffused_key,
			  &buffval) != HASHTABLE_SUCCESS) {
		LogFullDebug(COMPONENT_STATE, "KEY {%s} NOTFOUND", str);
		status = STATE_BAD_COOKIE;
		return status;
	}

	*cookie_entry = buffval.addr;

	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_lock_cookie_entry(*cookie_entry, str);
		LogFullDebug(COMPONENT_STATE, "Found Lock Cookie {%s}", str);
	}

	status = STATE_SUCCESS;
	return status;
}

/**
 * @brief Grant a blocked lock
 *
 * @param[in] entry      File on which to grant it
 * @param[in] lock_entry Lock entry
 */
void grant_blocked_lock_immediate(cache_entry_t *entry,
				  state_lock_entry_t *lock_entry)
{
	state_cookie_entry_t *cookie = NULL;
	state_status_t state_status;

	/* Try to clean up blocked lock. */
	if (lock_entry->sle_block_data != NULL) {
		if (lock_entry->sle_block_data->sbd_blocked_cookie != NULL) {
			/* Cookie is attached, try to get it */
			cookie = lock_entry->sle_block_data->sbd_blocked_cookie;

			state_status = state_find_grant(cookie->sce_cookie,
							cookie->sce_cookie_size,
							&cookie);
			if (state_status == STATE_SUCCESS) {
				/* We've got the cookie,
				 * free the cookie and the blocked lock
				 */
				free_cookie(cookie, true);
			} else {
				/* Otherwise, another thread has the cookie,
				 * let it do it's business.
				 */
				return;
			}
		} else {
			/* We have block data but no cookie,
			 * so we can just free the block data
			 */
			memset(lock_entry->sle_block_data, 0,
			       sizeof(*lock_entry->sle_block_data));
			gsh_free(lock_entry->sle_block_data);
			lock_entry->sle_block_data = NULL;
		}
	}

	/* Mark lock as granted */
	lock_entry->sle_blocked = STATE_NON_BLOCKING;

	/* Merge any touching or overlapping locks into this one. */
	LogEntry("Granted immediate, merging locks for", lock_entry);

	merge_lock_entry(entry, lock_entry);
	LogEntry("Immediate Granted entry", lock_entry);

	/* A lock downgrade could unblock blocked locks */
	grant_blocked_locks(entry);
}

/**
 * @brief Finish granting a lock
 *
 * Do bookkeeping and merge the lock into the lock list.
 *
 * @param[in] cookie_entry Entry describing the grant
 */

void state_complete_grant(state_cookie_entry_t *cookie_entry)
{
	state_lock_entry_t *lock_entry;
	cache_entry_t *entry;

	lock_entry = cookie_entry->sce_lock_entry;
	entry = cookie_entry->sce_entry;

	/* This routine does not call cache_inode_inc_pin_ref() because there
	 * MUST be at least one lock present for there to be a cookie_entry
	 * to even allow this routine to be called, and therefor the cache
	 * entry MUST be pinned.
	 */

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	/* We need to make sure lock is ready to be granted */
	if (lock_entry->sle_blocked == STATE_GRANTING) {
		/* Mark lock as granted */
		lock_entry->sle_blocked = STATE_NON_BLOCKING;

		/* Merge any touching or overlapping locks into this one. */
		LogEntry("Granted, merging locks for", lock_entry);
		merge_lock_entry(entry, lock_entry);

		LogEntry("Granted entry", lock_entry);

		/* A lock downgrade could unblock blocked locks */
		grant_blocked_locks(entry);
	}

	/* Free cookie and unblock lock.
	 * If somehow the lock was unlocked/canceled while the GRANT
	 * was in progress, this will completely clean up the lock.
	 */
	free_cookie(cookie_entry, true);

	/* In case all locks have wound up free,
	 * we must release the pin reference.
	 */
	if (glist_empty(&entry->object.file.lock_list))
		cache_inode_dec_pin_ref(entry, false);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);
}

/**
 * @brief Attempt to grant a blocked lock
 *
 * @param[in] lock_entry Lock entry to grant
 */

void try_to_grant_lock(state_lock_entry_t *lock_entry)
{
	granted_callback_t call_back;
	state_blocking_t blocked;
	state_status_t status;
	struct root_op_context root_op_context;
	struct gsh_export *export = lock_entry->sle_export;

	/* Try to grant if not cancelled and has block data */
	if (lock_entry->sle_blocked != STATE_CANCELED
	    && lock_entry->sle_block_data != NULL) {
		call_back = lock_entry->sle_block_data->sbd_granted_callback;
		/* Mark the lock_entry as provisionally granted and make the
		 * granted call back. The granted call back is responsible
		 * for acquiring a reference to the lock entry if needed.
		 */
		blocked = lock_entry->sle_blocked;
		lock_entry->sle_blocked = STATE_GRANTING;
		if (lock_entry->sle_block_data->sbd_grant_type ==
		    STATE_GRANT_NONE)
			lock_entry->sle_block_data->sbd_grant_type =
			    STATE_GRANT_INTERNAL;

		/* Initialize a root context */
		get_gsh_export_ref(export);

		init_root_op_context(&root_op_context,
				     export, export->fsal_export,
				     0, 0, UNKNOWN_REQUEST);

		status = call_back(lock_entry->sle_entry,
				   lock_entry);

		put_gsh_export(export);

		release_root_op_context();
		if (status == STATE_LOCK_BLOCKED) {
			/* The lock is still blocked,
			 * restore it's type and leave it in the list
			 */
			lock_entry->sle_blocked = blocked;
			return;
		}

		if (status == STATE_SUCCESS)
			return;
	}

	/* There was no call back data, the call back failed,
	 * or the block was cancelled.
	 * Remove lock from list.
	 */
	LogEntry("Removing blocked entry", lock_entry);
	remove_from_locklist(lock_entry);
}

/**
 * @brief Routine to be called from the FSAL upcall handler
 *
 * @param[in] block_data Data describing blocked lock
 */

void process_blocked_lock_upcall(state_block_data_t *block_data)
{
	state_lock_entry_t *lock_entry = block_data->sbd_lock_entry;
	cache_entry_t *entry = lock_entry->sle_entry;

	/* This routine does not call cache_inode_inc_pin_ref() because there
	 * MUST be at least one lock present for there to be a block_data to
	 * even allow this routine to be called, and therefor the cache entry
	 * MUST be pinned.
	 */

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	try_to_grant_lock(lock_entry);

	/* In case all locks have wound up free,
	 * we must release the pin reference.
	 */
	if (glist_empty(&entry->object.file.lock_list))
		cache_inode_dec_pin_ref(entry, false);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);
}

/**
 * @brief Attempt to grant all blocked locks on a file
 *
 * @param[in] entry Cache entry for the file
 */

static void grant_blocked_locks(cache_entry_t *entry)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist, *glistn;
	struct fsal_export *export = op_ctx->export->fsal_export;

	/* If FSAL supports async blocking locks,
	 * allow it to grant blocked locks.
	 */
	if (export->ops->fs_supports(export, fso_lock_support_async_block))
		return;

	glist_for_each_safe(glist, glistn, &entry->object.file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		if (found_entry->sle_blocked != STATE_NLM_BLOCKING
		    && found_entry->sle_blocked != STATE_NFSV4_BLOCKING)
			continue;

		/* Found a blocked entry for this file,
		 * see if we can place the lock.
		 */
		if (get_overlapping_entry
		    (entry, found_entry->sle_owner,
		     &found_entry->sle_lock) != NULL)
			continue;

		/* Found an entry that might work, try to grant it. */
		try_to_grant_lock(found_entry);
	}
}

/**
 * @brief Cancel a blocked lock
 *
 * @param[in] entry      File on which to cancel the lock
 * @param[in] lock_entry Lock to cancel
 *
 * @return State status.
 */
static
void cancel_blocked_lock(cache_entry_t *entry,
			 state_lock_entry_t *lock_entry)
{
	state_cookie_entry_t *cookie = NULL;
	state_status_t state_status;

	/* Mark lock as canceled */
	LogEntry("Cancelling blocked", lock_entry);
	lock_entry->sle_blocked = STATE_CANCELED;

	/* Unlocking the entire region will remove any FSAL locks we held,
	 * whether from fully granted locks, or from blocking locks that were
	 * in the process of being granted.
	 */

	/* Try to clean up blocked lock if a cookie is present */
	if (lock_entry->sle_block_data != NULL
	    && lock_entry->sle_block_data->sbd_blocked_cookie != NULL) {
		/* Cookie is attached, try to get it */
		cookie = lock_entry->sle_block_data->sbd_blocked_cookie;

		state_status = state_find_grant(cookie->sce_cookie,
						cookie->sce_cookie_size,
						&cookie);

		if (state_status == STATE_SUCCESS) {
			/* We've got the cookie,
			 * free the cookie and the blocked lock
			 */
			free_cookie(cookie, true);
		}
		/* otherwise, another thread has the cookie, let it do it's
		 * business, which won't be much, since we've already marked
		 * the lock CANCELED.
		 */
	} else {
		/* Otherwise, if block data is present, it will be freed when
		 * the lock entry is freed. If the cookie is held, the refcount
		 * it holds will prevent the lock entry from being released
		 * until the cookie is freed.
		 */

		/* Since a cookie was not found,
		 * the lock must still be in a state of needing cancelling.
		 */
		state_status = do_lock_op(entry,
					  FSAL_OP_CANCEL,
					  lock_entry->sle_owner,
					  &lock_entry->sle_lock,
					  NULL,	/* no conflict expected */
					  NULL,
					  false, /* overlap not relevant */
					  POSIX_LOCK);

		if (state_status != STATE_SUCCESS) {
			/* Unable to cancel,
			 * assume that granted upcall is on it's way.
			 */
			LogEntry("Unable to cancel (grant upcall expected)",
				 lock_entry);
			return;
		}
	}

	/* Remove the lock from the lock list */
	LogEntry("Removing", lock_entry);
	remove_from_locklist(lock_entry);
}

/**
 *
 * @brief Cancel blocked locks that overlap a lock
 *
 * Handle the situation where we have granted a lock and the client now
 * assumes it holds the lock, but we haven't received the GRANTED RSP, and
 * now the client is unlocking the lock.
 *
 * This will also handle the case of a client that uses UNLOCK to cancel
 * a blocked lock.
 *
 * Because this will release any blocked lock that was in the process of
 * being granted that overlaps the lock at all, we protect ourselves from
 * having a stuck lock at the risk of the client thinking it has a lock
 * it now doesn't.
 *
 * If the client unlock doesn't happen to fully overlap a blocked lock,
 * the blocked lock will be cancelled in full. Hopefully the client will
 * retry the remainder lock that should have still been blocking.
 *
 * @param[in,out] entry File on which to operate
 * @param[in]     owner The state owner for the lock
 * @param[in]     state Associated state
 * @param[in]     lock  Lock description
 */
void cancel_blocked_locks_range(cache_entry_t *entry,
				state_owner_t *owner, state_t *state,
				fsal_lock_param_t *lock)
{
	struct glist_head *glist, *glistn;
	state_lock_entry_t *found_entry = NULL;
	uint64_t found_entry_end, range_end = lock_end(lock);

	glist_for_each_safe(glist, glistn, &entry->object.file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		/* Skip locks not owned by owner */
		if (owner != NULL
		    && different_owners(found_entry->sle_owner, owner))
			continue;

		/* Skip locks owned by this NLM state.
		 * This protects NLM locks from the current iteration of an NLM
		 * client from being released by SM_NOTIFY.
		 */
		if (state != NULL && lock_owner_is_nlm(found_entry)
		    && found_entry->sle_state == state)
			continue;

		/* Skip granted locks */
		if (found_entry->sle_blocked == STATE_NON_BLOCKING)
			continue;

		LogEntry("Checking", found_entry);

		found_entry_end = lock_end(&found_entry->sle_lock);

		if ((found_entry_end >= lock->lock_start)
		    && (found_entry->sle_lock.lock_start <= range_end)) {
			/* lock overlaps, cancel it. */
			cancel_blocked_lock(entry, found_entry);
		}
	}
}

/**
 * @brief Release a lock grant
 *
 * @param[in] cookie_entry Grant entry
 *
 * @return State status.
 */
state_status_t state_release_grant(state_cookie_entry_t *cookie_entry)
{
	state_lock_entry_t *lock_entry;
	cache_entry_t *entry;
	state_status_t status = STATE_SUCCESS;

	lock_entry = cookie_entry->sce_lock_entry;
	entry = cookie_entry->sce_entry;

	/* This routine does not call cache_inode_inc_pin_ref() because there
	 * MUST be at least one lock present for there to be a cookie_entry
	 * to even allow this routine to be called, and therefor the cache
	 * entry MUST be pinned.
	 */

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	/* We need to make sure lock is only "granted" once...
	 * It's (remotely) possible that due to latency, we might end up
	 * processing two GRANTED_RSP calls at the same time.
	 */
	if (lock_entry->sle_blocked == STATE_GRANTING) {
		/* Mark lock as canceled */
		lock_entry->sle_blocked = STATE_CANCELED;

		/* We had acquired an FSAL lock, need to release it. */
		status = do_lock_op(entry,
				    FSAL_OP_UNLOCK,
				    lock_entry->sle_owner,
				    &lock_entry->sle_lock,
				    NULL, /* no conflict expected */
				    NULL,
				    false,
				    POSIX_LOCK);

		if (status != STATE_SUCCESS)
			LogMajor(COMPONENT_STATE,
				 "Unable to unlock FSAL for released GRANTED lock, error=%s",
				 state_err_str(status));
		else {
			/* Remove the lock from the lock list.
			 * Will not free yet because of cookie reference to
			 * lock entry.
			 */
			LogEntry("Release Grant Removing", lock_entry);
			remove_from_locklist(lock_entry);
		}
	}

	/* Free the cookie and unblock the lock. This will release our final
	 * reference on the lock entry and should free it. (Unless another
	 * thread has a reference for some reason.
	 */
	free_cookie(cookie_entry, true);

	/* Check to see if we can grant any blocked locks. */
	grant_blocked_locks(entry);

	/* In case all locks have wound up free,
	 * we must release the pin reference.
	 */
	if (glist_empty(&entry->object.file.lock_list))
		cache_inode_dec_pin_ref(entry, false);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	return status;
}

/******************************************************************************
 *
 * Functions to interract with FSAL
 *
 ******************************************************************************/

/**
 * @brief Human-readable string from the lock operation
 *
 * @param[in] op The lock operation
 *
 * @return The human-readable string.
 */
inline const char *fsal_lock_op_str(fsal_lock_op_t op)
{
	switch (op) {
	case FSAL_OP_LOCKT:
		return "FSAL_OP_LOCKT ";
	case FSAL_OP_LOCK:
		return "FSAL_OP_LOCK  ";
	case FSAL_OP_LOCKB:
		return "FSAL_OP_LOCKB ";
	case FSAL_OP_UNLOCK:
		return "FSAL_OP_UNLOCK";
	case FSAL_OP_CANCEL:
		return "FSAL_OP_CANCEL";
	}
	return "unknown";
}

/**
 * @brief Handle FSAL unlock when owner is not supported.
 *
 * When the FSAL doesn't support lock owners, we can't just
 * arbitrarily unlock the entire range in the FSAL, we might have
 * locks owned by other owners that still exist, either because there
 * were several lock owners with read locks, or the client unlocked a
 * larger range that is actually locked (some (most) clients will
 * actually unlock the entire file when closing a file or terminating
 * a process).
 *
 * Basically, we want to create a list of ranges to unlock. To do so
 * we create a dummy entry in a dummy list for the unlock range. Then
 * we subtract each existing lock from the dummy list.
 *
 * The list of unlock ranges will include ranges that the original
 * onwer didn't actually have locks in. This behavior is actually
 * helpful for some callers of FSAL_OP_UNLOCK.
 *
 * @param[in] entry    File on which to operate
 * @param[in] export   Export through which the file is accessed
 * @param[in] lock     Lock descriptor
 * @param[in] sle_type Lock type
 */
state_status_t do_unlock_no_owner(cache_entry_t *entry,
				  fsal_lock_param_t *lock,
				  lock_type_t sle_type)
{
	state_lock_entry_t *unlock_entry;
	struct glist_head fsal_unlock_list;
	struct glist_head *glist, *glistn;
	state_lock_entry_t *found_entry;
	fsal_status_t fsal_status;
	state_status_t status = STATE_SUCCESS, t_status;
	fsal_lock_param_t *punlock;

	unlock_entry = create_state_lock_entry(entry,
					       op_ctx->export,
					       STATE_NON_BLOCKING,
					       &unknown_owner,
					       NULL, /* no real state */
					       lock,
					       sle_type);

	if (unlock_entry == NULL)
		return STATE_MALLOC_ERROR;

	glist_init(&fsal_unlock_list);

	glist_add_tail(&fsal_unlock_list, &unlock_entry->sle_list);

	LogEntry("Generating FSAL Unlock List", unlock_entry);

	status =
	    subtract_list_from_list(entry, &fsal_unlock_list,
				    &entry->object.file.lock_list);
	if (status != STATE_SUCCESS) {
		/* We ran out of memory while trying to build the unlock list.
		 * We have already released the locks from cache inode lock
		 * list.
		 */

		/* @todo FSF: what do we do now? */
		LogMajor(COMPONENT_STATE,
			 "Error %s while trying to create unlock list",
			 state_err_str(status));
	}

	glist_for_each_safe(glist, glistn, &fsal_unlock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);
		punlock = &found_entry->sle_lock;

		LogEntry("FSAL Unlock", found_entry);

		fsal_status =
		    entry->obj_handle->ops->lock_op(entry->obj_handle,
						    NULL, FSAL_OP_UNLOCK,
						    punlock, NULL);

		if (fsal_status.major == ERR_FSAL_STALE)
			cache_inode_kill_entry(entry);

		t_status = state_error_convert(fsal_status);

		LogFullDebug(COMPONENT_STATE, "FSAL_lock_op returned %s",
			     state_err_str(t_status));

		if (t_status != STATE_SUCCESS) {
			/* @todo FSF: what do we do now? */
			LogMajor(COMPONENT_STATE,
				 "Error %s while trying to do FSAL Unlock",
				 state_err_str(t_status));
			status = t_status;
		}

		remove_from_locklist(found_entry);
	}

	return status;
}

/**
 * @brief Perform a lock operation
 *
 * We do state management and call down to the FSAL as appropriate, so
 * that the caller has a single entry point.
 *
 * @param[in]  entry    File on which to operate
 * @param[in]  export   Export holding file
 * @param[in]  lock_op  Operation to perform
 * @param[in]  owner    Lock operation
 * @param[in]  lock     Lock description
 * @param[out] holder   Owner of conflicting lock
 * @param[out] conflict Description of conflicting lock
 * @param[in]  overlap  Hint that lock overlaps
 * @param[in]  sle_type Lock type
 *
 * @return State status.
 */
static state_status_t do_lock_op(cache_entry_t *entry,
				 fsal_lock_op_t lock_op,
				 state_owner_t *owner,
				 fsal_lock_param_t *lock,
				 state_owner_t **holder,
				 fsal_lock_param_t *conflict,
				 bool_t overlap,
				 lock_type_t sle_type)
{
	fsal_status_t fsal_status;
	state_status_t status = STATE_SUCCESS;
	fsal_lock_param_t conflicting_lock;
	struct fsal_export *fsal_export = op_ctx->fsal_export;

	lock->lock_sle_type = sle_type;

	/* Quick exit if:
	 * Locks are not supported by FSAL
	 * Async blocking locks are not supported and this is a cancel
	 * Async blocking locks are not supported and this lock overlaps
	 * Lock owners are not supported and hint tells us that lock fully
	 *   overlaps a lock we already have (no need to make another FSAL
	 *   call in that case)
	 */
	if (!fsal_export->ops->fs_supports(fsal_export, fso_lock_support)
	    || (!fsal_export->ops->
		fs_supports(fsal_export, fso_lock_support_async_block)
		&& lock_op == FSAL_OP_CANCEL)
	    || (!fsal_export->ops->
		fs_supports(fsal_export, fso_lock_support_async_block)
		&& overlap)
	    || (!fsal_export->ops->
		fs_supports(fsal_export, fso_lock_support_owner) && overlap))
		return STATE_SUCCESS;

	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, fsal_lock_op_str(lock_op),
		entry, owner, lock);

	memset(&conflicting_lock, 0, sizeof(conflicting_lock));

	if (fsal_export->ops->fs_supports(fsal_export, fso_lock_support_owner)
	    || lock_op != FSAL_OP_UNLOCK) {
		if (lock_op == FSAL_OP_LOCKB &&
		    !fsal_export->ops->fs_supports(
				fsal_export,
				fso_lock_support_async_block))
			lock_op = FSAL_OP_LOCK;

		fsal_status = entry->obj_handle->ops->lock_op(
			entry->obj_handle,
			fsal_export->ops->fs_supports(
				fsal_export,
				fso_lock_support_owner)
				? owner : NULL, lock_op,
			lock,
			&conflicting_lock);

		status = state_error_convert(fsal_status);

		LogFullDebug(COMPONENT_STATE, "FSAL_lock_op returned %s",
			     state_err_str(status));

		if (status == STATE_LOCK_BLOCKED && lock_op != FSAL_OP_LOCKB) {
			/* This is an unexpected return code,
			 * make sure caller reports an error
			 */
			LogMajor(COMPONENT_STATE,
				 "FSAL returned unexpected STATE_LOCK_BLOCKED result");
			status = STATE_FSAL_ERROR;
		}
	} else {
		if (!LOCK_OWNER_9P(owner))
			status = do_unlock_no_owner(entry, lock, sle_type);
	}

	if (status == STATE_LOCK_CONFLICT) {
		if (holder != NULL) {
			*holder = &unknown_owner;
			inc_state_owner_ref(&unknown_owner);
		}

		if (conflict != NULL)
			*conflict = conflicting_lock;
	}

	return status;
}

/**
 * @brief Fill out conflict information
 *
 * @param[in]  found_entry Conflicting lock
 * @param[out] holder      Owner that holds conflicting lock
 * @param[out] conflict    Description of conflicting lock
 */
void copy_conflict(state_lock_entry_t *found_entry, state_owner_t **holder,
		   fsal_lock_param_t *conflict)
{
	if (found_entry == NULL)
		return;

	if (holder != NULL) {
		*holder = found_entry->sle_owner;
		inc_state_owner_ref(found_entry->sle_owner);
	}
	if (conflict != NULL)
		*conflict = found_entry->sle_lock;
}

/******************************************************************************
 *
 * Primary lock interface functions
 *
 ******************************************************************************/

/**
 * @brief Test for lock availability
 *
 * This function acquires the state lock on an entry and thus is only
 * suitable for operations like lockt.  If one wishes to use it as
 * part of a larger lock or state operation one would need to split it
 * out.
 *
 * @param[in]  entry    Entry to test
 * @param[in]  export   Export through which the entry is accessed
 * @param[in]  owner    Lock owner making the test
 * @param[in]  lock     Lock description
 * @param[out] holder   Owner that holds conflicting lock
 * @param[out] conflict Description of conflicting lock
 *
 * @return State status.
 */
state_status_t state_test(cache_entry_t *entry,
			  state_owner_t *owner,
			  fsal_lock_param_t *lock, state_owner_t **holder,
			  fsal_lock_param_t *conflict)
{
	state_lock_entry_t *found_entry;
	cache_inode_status_t cache_status;
	state_status_t status = 0;

	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, "TEST", entry, owner, lock);

	cache_status = cache_inode_inc_pin_ref(entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		status = cache_inode_status_to_state_status(cache_status);
		LogDebug(COMPONENT_STATE, "Could not pin file");
		return status;
	}

	cache_status = cache_inode_open(entry, FSAL_O_READ, 0);
	if (cache_status != CACHE_INODE_SUCCESS) {
		status = cache_inode_status_to_state_status(cache_status);
		LogFullDebug(COMPONENT_STATE, "Could not open file");

		cache_inode_dec_pin_ref(entry, false);

		return status;
	}

	PTHREAD_RWLOCK_rdlock(&entry->state_lock);

	found_entry = get_overlapping_entry(entry, owner, lock);

	if (found_entry != NULL) {
		/* found a conflicting lock, return it */
		LogEntry("Found conflict", found_entry);
		copy_conflict(found_entry, holder, conflict);
		status = STATE_LOCK_CONFLICT;
	} else {
		/* Prepare to make call to FSAL for this lock */
		status = do_lock_op(entry, FSAL_OP_LOCKT, owner,
				    lock, holder, conflict, false, POSIX_LOCK);

		if (status != STATE_SUCCESS && status != STATE_LOCK_CONFLICT) {
			LogMajor(COMPONENT_STATE,
				 "Got error from FSAL lock operation, error=%s",
				 state_err_str(status));
		}
		if (status == STATE_SUCCESS)
			LogFullDebug(COMPONENT_STATE, "No Conflict");
		else
			LogLock(COMPONENT_STATE, NIV_FULL_DEBUG,
				"Conflict from FSAL",
				entry, *holder, conflict);
	}

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
		LogList("Lock List", entry, &entry->object.file.lock_list);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_dec_pin_ref(entry, false);

	return status;
}

/**
 * @brief Attempt to acquire a lock
 *
 * @param[in]  entry      Entry to lock
 * @param[in]  export     Export through which entry is accessed
 * @param[in]  owner      Lock owner
 * @param[in]  state      Associated state for the lock
 * @param[in]  blocking   Blocking type
 * @param[in]  block_data Blocking lock data
 * @param[in]  lock       Lock description
 * @param[out] holder     Holder of conflicting lock
 * @param[out] conflict   Conflicting lock description
 * @param[in]  sle_type   Lock type
 *
 * @return State status.
 */
state_status_t state_lock(cache_entry_t *entry,
			  state_owner_t *owner, state_t *state,
			  state_blocking_t blocking,
			  state_block_data_t *block_data,
			  fsal_lock_param_t *lock, state_owner_t **holder,
			  fsal_lock_param_t *conflict, lock_type_t sle_type)
{
	bool allow = true, overlap = false;
	struct glist_head *glist;
	state_lock_entry_t *found_entry;
	uint64_t found_entry_end;
	uint64_t range_end = lock_end(lock);
	cache_inode_status_t cache_status;
	struct fsal_export *fsal_export = op_ctx->fsal_export;
	fsal_lock_op_t lock_op;
	state_status_t status = 0;
	fsal_openflags_t openflags;

	cache_status = cache_inode_inc_pin_ref(entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		status = cache_inode_status_to_state_status(cache_status);
		LogDebug(COMPONENT_STATE, "Could not pin file");
		return status;
	}

	/*
	 * If we already have a read lock, and then get a write lock
	 * request, we need to close the file that was already open for
	 * read, and then open the file for readwrite for the write lock
	 * request.  Closing the file loses all lock state, so we just
	 * open the file for readwrite for any kind of lock request.
	 *
	 * If the FSAL supports atomicaly updating the read only fd to
	 * readwrite fd, then we don't need to open a file for readwrite
	 * for read only lock request. This helps with delegations as
	 * well.
	 */
	if (lock->lock_type == FSAL_LOCK_R &&
	    fsal_export->ops->fs_supports(fsal_export, fso_reopen_method))
		openflags = FSAL_O_READ;
	else
		openflags = FSAL_O_RDWR;
	cache_status = cache_inode_open(entry,
					openflags,
					(lock->lock_reclaim) ?
						CACHE_INODE_FLAG_RECLAIM : 0);

	if (cache_status != CACHE_INODE_SUCCESS) {
		cache_inode_dec_pin_ref(entry, false);
		status = cache_inode_status_to_state_status(cache_status);
		LogFullDebug(COMPONENT_STATE, "Could not open file");
		return status;
	}

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	if (blocking != STATE_NON_BLOCKING) {
		/* First search for a blocked request. Client can ignore the
		 * blocked request and keep sending us new lock request again
		 * and again. So if we have a mapping blocked request return
		 * that
		 */
		glist_for_each(glist, &entry->object.file.lock_list) {
			found_entry =
			    glist_entry(glist, state_lock_entry_t, sle_list);

			if (different_owners(found_entry->sle_owner, owner))
				continue;

			/* Need to reject lock request if this lock owner
			 * already has a lock on this file via a different
			 * export.
			 */
			if (found_entry->sle_export != op_ctx->export) {
				PTHREAD_RWLOCK_unlock(&entry->state_lock);

				cache_inode_dec_pin_ref(entry, false);

				LogEvent(COMPONENT_STATE,
					 "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
					 found_entry->sle_export->export_id,
					 found_entry->sle_export->fullpath,
					 op_ctx->export->export_id,
					 op_ctx->export->fullpath);
				LogEntry(
					"Found lock entry belonging to another export",
					found_entry);
				status = STATE_INVALID_ARGUMENT;
				return status;
			}

			if (found_entry->sle_blocked != blocking)
				continue;

			if (different_lock(&found_entry->sle_lock, lock))
				continue;

			PTHREAD_RWLOCK_unlock(&entry->state_lock);

			cache_inode_dec_pin_ref(entry, false);

			/* We have matched all atribute of the existing lock.
			 * Just return with blocked status. Client may be
			 * polling.
			 */
			LogEntry("Found blocked", found_entry);
			status = STATE_LOCK_BLOCKED;
			return status;
		}
	}

	glist_for_each(glist, &entry->object.file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		/* Delegations owned by a client won't conflict with delegations
		   to that same client, but maybe we should just return
		   success. */
		if (found_entry->sle_type == LEASE_LOCK &&
		    lock->lock_sle_type == FSAL_LEASE_LOCK &&
		    owner->so_owner.so_nfs4_owner.so_clientid ==
		    found_entry->sle_owner->so_owner.so_nfs4_owner.so_clientid)
			continue;

		/* Need to reject lock request if this lock owner already has
		 * a lock on this file via a different export.
		 */
		if (found_entry->sle_export != op_ctx->export
		    && !different_owners(found_entry->sle_owner, owner)) {
			PTHREAD_RWLOCK_unlock(&entry->state_lock);

			cache_inode_dec_pin_ref(entry, false);

			LogEvent(COMPONENT_STATE,
				 "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
				 found_entry->sle_export->export_id,
				 found_entry->sle_export->fullpath,
				 op_ctx->export->export_id,
				 op_ctx->export->fullpath);

			LogEntry("Found lock entry belonging to another export",
				 found_entry);

			status = STATE_INVALID_ARGUMENT;
			return status;
		}

		/* Don't skip blocked locks for fairness */
		found_entry_end = lock_end(&found_entry->sle_lock);

		if ((found_entry_end >= lock->lock_start)
		    && (found_entry->sle_lock.lock_start <= range_end)) {
			/* lock overlaps see if we can allow:
			 * allow if neither lock is exclusive or
			 * the owner is the same
			 */
			if ((found_entry->sle_lock.lock_type == FSAL_LOCK_W
			     || lock->lock_type == FSAL_LOCK_W)
			    && different_owners(found_entry->sle_owner,
						owner)) {
				/* Found a conflicting lock, break out of loop.
				 * Also indicate overlap hint.
				 */
				LogEntry("Conflicts with", found_entry);
				LogList("Locks", entry,
					&entry->object.file.lock_list);
				copy_conflict(found_entry, holder, conflict);
				allow = false;
				overlap = true;
				break;
			}
		}

		if (found_entry_end >= range_end
		    && found_entry->sle_lock.lock_start <= lock->lock_start
		    && found_entry->sle_lock.lock_type == lock->lock_type
		    && (found_entry->sle_blocked == STATE_NON_BLOCKING
			|| found_entry->sle_blocked == STATE_GRANTING)) {
			/* Found an entry that entirely overlaps the new entry
			 * (and due to the preceding test does not prevent
			 * granting this lock - therefore there can't be any
			 * other locks that would prevent granting this lock
			 */
			if (!different_owners(found_entry->sle_owner, owner)) {
				/* The lock actually has the same owner, we're
				 * done, other than dealing with a lock in
				 * GRANTING state.
				 */
				if (found_entry->sle_blocked
				    == STATE_GRANTING) {
					/* Need to handle completion of granting
					 * of this lock because a GRANT was in
					 * progress. This could be a client
					 * retrying a blocked lock due to
					 * mis-trust of server. If the client
					 * also accepts the GRANT_MSG with a
					 * GRANT_RESP, that will be just fine.
					 */
					grant_blocked_lock_immediate(
						entry,
						found_entry);
				}

				PTHREAD_RWLOCK_unlock(&entry->state_lock);

				cache_inode_dec_pin_ref(entry, false);

				LogEntry("Found existing", found_entry);

				status = STATE_SUCCESS;
				return status;
			}

			/* Found a compatible lock with a different lock owner
			 * that fully overlaps, set hint.
			 */
			LogEntry("state_lock Found overlapping", found_entry);
			overlap = true;
		}
	}

	/* Decide how to proceed */
	if (fsal_export->ops->
	    fs_supports(fsal_export, fso_lock_support_async_block)
	    && blocking == STATE_NLM_BLOCKING) {
		/* FSAL supports blocking locks, and this is an NLM blocking
		 * lock request, request blocking lock from FSAL.
		 */
		lock_op = FSAL_OP_LOCKB;
	} else if (allow || blocking == STATE_NLM_BLOCKING) {
		/* No conflict found in Ganesha, or NLM blocking lock when FSAL
		 * doesn't support blocking locks. In either case, proceed with
		 * non-blocking request to FSAL.
		 */
		lock_op = FSAL_OP_LOCK;
	} else {
		/* Can't do async blocking lock in FSAL and have a conflict.
		 * Return it. This is true for conflicting delegations as well.
		 */
		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		cache_inode_dec_pin_ref(entry, false);

		status = STATE_LOCK_CONFLICT;
		return status;
	}

	/* We have already returned if:
	 * + we have found an identical blocking lock
	 * + we have found an entirely overlapping lock with the same lock owner
	 * + this was not a supported blocking lock and we found a conflict
	 *
	 * So at this point, we are either going to do one of the following (all
	 * descriptions below assume no problems occur):
	 *
	 * (1) FSAL supports async blocking locks, we know there is a conflict,
	 (     and this is a supported blocking lock request
	 *
	 *     Make FSAL_OP_LOCKB call anyway, we will rely on FSAL to grant
	 *     blocking locks. We will return the conflict we know about rather
	 *     than what the FSAL returns. Insert blocking lock into queue.
	 *
	 * (2) FSAL supports async blocking locks, we don't know about any
	 *     conflict, and this is a supported blocking lock request
	 *
	 *     Make FSAL_OP_LOCKB call, if it indicates block, insert blocking
	 *     lock into queue, and return the conflict the FSAL indicates. If
	 *     FSAL grants lock, then return granted lock and insert into lock
	 *     list, otherwise insert blocking lock into queue.
	 *
	 * (3) FSAL doesn't support async blocking locks, this is a supported
	 *     blocking lock and we know there is a conflict
	 *
	 *     Insert blocking lock into queue, we will grant lock when
	 *     possible.
	 *
	 * (4) FSAL doesn't support async blocking locks and we don't know
	 *     about any conflict
	 *
	 *     Make FSAL_OP_LOCK call, if it indicates conflict, return that.
	 *     Even if this is a supported blocking lock call, there is no way
	 *     to block. If lock is granted, return that and insert lock into
	 *     list.
	 *
	 * (5) FSAL supports async blocking locks, we don't know about any
	 *     conflict, and this is not a supported blocking lock request
	 *
	 *     Make FSAL_OP_LOCK call, if it indicates conflict, return that.
	 *     If lock is granted, return that and insert lock into list.
	 */

	/* Create the new lock entry.
	 * Provisionally mark this lock as granted.
	 */
	found_entry = create_state_lock_entry(entry,
					      op_ctx->export,
					      STATE_NON_BLOCKING,
					      owner,
					      state,
					      lock,
					      sle_type);
	if (!found_entry) {
		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		cache_inode_dec_pin_ref(entry, false);

		status = STATE_MALLOC_ERROR;
		return status;
	}

	/* If no conflict in lock list, or FSAL supports async blocking locks,
	 * make FSAL call. Don't ask for conflict if we know about a conflict.
	 */
	if (allow
	    || fsal_export->ops->fs_supports(fsal_export,
					     fso_lock_support_async_block)) {
		/* Prepare to make call to FSAL for this lock */
		status = do_lock_op(entry,
				    lock_op,
				    owner,
				    lock,
				    allow ? holder : NULL,
				    allow ? conflict : NULL,
				    overlap,
				    sle_type);
	} else
		status = STATE_LOCK_BLOCKED;

	if (status == STATE_SUCCESS && sle_type == LEASE_LOCK) {
		/* Insert entry into delegation list */
		LogEntry("New delegation", found_entry);
		update_delegation_stats(entry, state);
		glist_add_tail(&entry->object.file.deleg_list,
			       &found_entry->sle_list);
	} else if (status == STATE_SUCCESS && sle_type == POSIX_LOCK) {
		/* Merge any touching or overlapping locks into this one */
		LogEntry("FSAL lock acquired, merging locks for",
			 found_entry);

		merge_lock_entry(entry, found_entry);

		/* Insert entry into lock list */
		LogEntry("New lock", found_entry);

		/* if the list is empty to start with; increment the pin ref
		 * count before adding it to the list
		 */
		if (glist_empty(&entry->object.file.lock_list))
			cache_inode_inc_pin_ref(entry);

		glist_add_tail(&entry->object.file.lock_list,
			       &found_entry->sle_list);

		/* A lock downgrade could unblock blocked locks */
		grant_blocked_locks(entry);
		/* Don't need to unpin, we know there is state on file. */
	} else if (status == STATE_LOCK_CONFLICT) {
		LogEntry("Conflict in FSAL for", found_entry);

		/* Discard lock entry */
		remove_from_locklist(found_entry);
	} else if (status == STATE_LOCK_BLOCKED) {
		/* Mark entry as blocking and attach block_data */
		found_entry->sle_block_data = block_data;
		found_entry->sle_blocked = blocking;
		block_data->sbd_lock_entry = found_entry;

		/* Insert entry into lock list */
		LogEntry("FSAL block for", found_entry);

		/* if the list is empty to start with; increment the pin ref
		 * count before adding it to the list
		 */
		if (glist_empty(&entry->object.file.lock_list))
			cache_inode_inc_pin_ref(entry);

		glist_add_tail(&entry->object.file.lock_list,
			       &found_entry->sle_list);

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		cache_inode_dec_pin_ref(entry, false);

		pthread_mutex_lock(&blocked_locks_mutex);

		glist_add_tail(&state_blocked_locks, &block_data->sbd_list);

		pthread_mutex_unlock(&blocked_locks_mutex);

		return status;
	} else {
		LogMajor(COMPONENT_STATE, "Unable to lock FSAL, error=%s",
			 state_err_str(status));

		/* Discard lock entry */
		remove_from_locklist(found_entry);
	}

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_dec_pin_ref(entry, false);

	return status;
}

/**
 * @brief Release a lock
 *
 * @param[in] entry    File to unlock
 * @param[in] owner    Owner of lock
 * @param[in] state    Associated state
 * @param[in] lock     Lock description
 * @param[in] sle_type Lock type
 */
state_status_t state_unlock(cache_entry_t *entry,
			    state_owner_t *owner, state_t *state,
			    fsal_lock_param_t *lock, lock_type_t sle_type)
{
	bool empty = false;
	bool removed = false;
	cache_inode_status_t cache_status;
	state_status_t status = 0;

	cache_status = cache_inode_inc_pin_ref(entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		status = cache_inode_status_to_state_status(cache_status);
		LogDebug(COMPONENT_STATE, "Could not pin file");
		return status;
	}

	if (entry->type != REGULAR_FILE) {
		LogLock(COMPONENT_STATE, NIV_DEBUG, "Bad Unlock", entry, owner,
			lock);
		status = STATE_BAD_TYPE;
		return status;
	}

	/* We need to iterate over the full lock list and remove any mapping
	 * entry. And sle_lock.lock_start = 0 and sle_lock.lock_length = 0
	 * nlm_lock implies remove all entries
	 */
	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	if (sle_type == LEASE_LOCK) {
		assert(state && (state->state_type == STATE_TYPE_DELEG));
		if (glist_empty(&entry->object.file.deleg_list)) {
			PTHREAD_RWLOCK_unlock(&entry->state_lock);
			cache_inode_dec_pin_ref(entry, FALSE);
			LogDebug(COMPONENT_STATE,
				 "Unlock success on file with no delegations");
			status = STATE_SUCCESS;
			return status;
		} else {
			LogFullDebug(COMPONENT_STATE,
				     "Removing delegation from list");
			status = subtract_deleg_from_list(entry, owner, state,
					&removed,
					&entry->object.file.deleg_list);
		}
	} else {
	/* If lock list is empty, there really isn't any work for us to do. */
		if (glist_empty(&entry->object.file.lock_list)) {
			PTHREAD_RWLOCK_unlock(&entry->state_lock);

			cache_inode_dec_pin_ref(entry, false);
			LogDebug(COMPONENT_STATE,
				 "Unlock success on file with no locks");

			status = STATE_SUCCESS;
			return status;
		}
	}

	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");
	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, "Subtracting", entry, owner,
		lock);
	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");

	/* First cancel any blocking locks that might
	 * overlap the unlocked range.
	 */
	cancel_blocked_locks_range(entry, owner, state, lock);

	/* Release the lock from cache inode lock list for entry */
	status =
	    subtract_lock_from_list(entry, owner, state, lock, &removed,
				    &entry->object.file.lock_list);

	/* If the lock list has become zero; decrement the pin ref count pt
	 * placed. Do this here just in case subtract_lock_from_list has made
	 * list empty even if it failed.
	 */
	if (glist_empty(&entry->object.file.lock_list))
		cache_inode_dec_pin_ref(entry, false);

	if (status != STATE_SUCCESS) {
		/* The unlock has not taken affect (other than canceling any
		 * blocking locks.
		 */
		LogMajor(COMPONENT_STATE,
			 "Unable to remove lock from list for unlock, error=%s",
			 state_err_str(status));

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		cache_inode_dec_pin_ref(entry, false);

		return status;
	}

	/* Unlocking the entire region will remove any FSAL locks we held,
	 * whether from fully granted locks, or from blocking locks that were
	 * in the process of being granted.
	 */
	status = do_lock_op(entry,
			    FSAL_OP_UNLOCK,
			    owner,
			    lock,
			    NULL, /* no conflict expected */
			    NULL,
			    false,
			    sle_type);

	if (status != STATE_SUCCESS)
		LogMajor(COMPONENT_STATE, "Unable to unlock FSAL, error=%s",
			 state_err_str(status));

	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");
	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, "Done", entry, owner, lock);
	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS)
	    && lock->lock_start == 0 && lock->lock_length == 0)
		empty =
		    LogList("Lock List", entry, &entry->object.file.lock_list);

	grant_blocked_locks(entry);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_dec_pin_ref(entry, false);

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS)
	    && lock->lock_start == 0 && lock->lock_length == 0 && empty)
		dump_all_locks("All locks (after unlock)");

	return status;
}

/**
 * @brief Cancel a blocking lock
 *
 * @param[in] entry  File on which to cancel the lock
 * @param[in] owner  Lock owner
 * @param[in] lock   Lock description
 *
 * @return State status.
 */
state_status_t state_cancel(cache_entry_t *entry,
			    state_owner_t *owner, fsal_lock_param_t *lock)
{
	struct glist_head *glist;
	state_lock_entry_t *found_entry;
	cache_inode_status_t cache_status;

	if (entry->type != REGULAR_FILE) {
		LogLock(COMPONENT_STATE, NIV_DEBUG,
			"Bad Cancel",
			entry, owner, lock);
		return STATE_BAD_TYPE;
	}

	cache_status = cache_inode_inc_pin_ref(entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_STATE, "Could not pin file");
		return cache_inode_status_to_state_status(cache_status);
	}

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	/* If lock list is empty, there really isn't any work for us to do. */
	if (glist_empty(&entry->object.file.lock_list)) {
		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		cache_inode_dec_pin_ref(entry, false);
		LogDebug(COMPONENT_STATE,
			 "Cancel success on file with no locks");

		return STATE_SUCCESS;
	}

	glist_for_each(glist, &entry->object.file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		if (different_owners(found_entry->sle_owner, owner))
			continue;

		/* Can not cancel a lock once it is granted */
		if (found_entry->sle_blocked == STATE_NON_BLOCKING)
			continue;

		if (different_lock(&found_entry->sle_lock, lock))
			continue;

		/* Cancel the blocked lock */
		cancel_blocked_lock(entry, found_entry);

		/* Check to see if we can grant any blocked locks. */
		grant_blocked_locks(entry);

		break;
	}

	/* If the lock list has become zero; decrement
	 * the pin ref count pt placed
	 */
	if (glist_empty(&entry->object.file.lock_list))
		cache_inode_dec_pin_ref(entry, false);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_dec_pin_ref(entry, false);

	return STATE_SUCCESS;
}

/**
 * @brief Handle an SM_NOTIFY from NLM
 *
 * Also used to handle NLM_FREE_ALL
 *
 * @param[in] nsmclient NSM client data
 * @param[in] from_client true if from protocol, false from async events
 * @param[in] state     Associated state
 *
 * @return State status.
 */
state_status_t state_nlm_notify(state_nsm_client_t *nsmclient,
				bool from_client,
				state_t *state)
{
	state_owner_t *owner;
	state_lock_entry_t *found_entry;
	fsal_lock_param_t lock;
	cache_entry_t *entry;
	int errcnt = 0;
	struct glist_head newlocks;
	state_nlm_share_t *found_share;
	state_status_t status = 0;
	struct root_op_context root_op_context;

	/* Initialize a context */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	if (isFullDebug(COMPONENT_STATE)) {
		char client[HASHTABLE_DISPLAY_STRLEN];

		display_nsm_client(nsmclient, client);

		LogFullDebug(COMPONENT_STATE, "state_nlm_notify for %s",
			     client);
	}

	glist_init(&newlocks);

	/* First remove byte range locks.
	 * Only accept so many errors before giving up.
	 */
	while (errcnt < STATE_ERR_MAX) {
		pthread_mutex_lock(&nsmclient->ssc_mutex);

		/* We just need to find any file this client has locks on.
		 * We pick the first lock the client holds, and use it's file.
		 */
		found_entry =
		    glist_first_entry(&nsmclient->ssc_lock_list,
				      state_lock_entry_t, sle_client_locks);

		/* If we don't find any entries, then we are done. */
		if (found_entry == NULL) {
			pthread_mutex_unlock(&nsmclient->ssc_mutex);
			break;
		}

		/* Get a reference so the lock entry will still be valid when
		 * we release the ssc_mutex
		 */
		lock_entry_inc_ref(found_entry);

		/* Remove from the client lock list */
		glist_del(&found_entry->sle_client_locks);

		if (found_entry->sle_state == state && from_client) {
			/* This is a new lock acquired since the client
			 * rebooted, retain it.
			 */
			LogEntry("Don't release new lock", found_entry);
			glist_add_tail(&newlocks,
				       &found_entry->sle_client_locks);
			pthread_mutex_unlock(&nsmclient->ssc_mutex);
			continue;
		}

		LogEntry("Release client locks based on", found_entry);

		/* Move this entry to the end of the list
		 * (this will help if errors occur)
		 */
		glist_add_tail(&nsmclient->ssc_lock_list,
			       &found_entry->sle_client_locks);

		pthread_mutex_unlock(&nsmclient->ssc_mutex);

		/* Extract the cache inode entry from the lock entry and
		 * release the lock entry
		 */
		entry = found_entry->sle_entry;
		owner = found_entry->sle_owner;
		root_op_context.req_ctx.export = found_entry->sle_export;
		root_op_context.req_ctx.fsal_export =
			root_op_context.req_ctx.export->fsal_export;
		get_gsh_export_ref(root_op_context.req_ctx.export);

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

		lock_entry_dec_ref(found_entry);
		cache_inode_lru_ref(entry, LRU_FLAG_NONE);

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Make lock that covers the whole file.
		 * type doesn't matter for unlock
		 */
		lock.lock_type = FSAL_LOCK_R;
		lock.lock_start = 0;
		lock.lock_length = 0;

		/* Remove all locks held by this NLM Client on the file */
		status = state_unlock(entry,
				      owner, state,
				      &lock, POSIX_LOCK);

		put_gsh_export(root_op_context.req_ctx.export);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next lock,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogFullDebug(COMPONENT_STATE,
				     "state_unlock returned %s",
				     state_err_str(status));
			errcnt++;
		}

		/* Release the lru ref to the cache inode we held while
		 * calling unlock
		 */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
	}

	/* Now remove NLM_SHARE reservations.
	 * Only accept so many errors before giving up.
	 */
	while (errcnt < STATE_ERR_MAX) {
		pthread_mutex_lock(&nsmclient->ssc_mutex);

		/* We just need to find any file this client has locks on.
		 * We pick the first lock the client holds, and use it's file.
		 */
		found_share =
		    glist_first_entry(&nsmclient->ssc_share_list,
				      state_nlm_share_t, sns_share_per_client);

		/* If we don't find any entries, then we are done. */
		if (found_share == NULL) {
			pthread_mutex_unlock(&nsmclient->ssc_mutex);
			break;
		}

		/* Extract the cache inode entry from the share */
		entry = found_share->sns_entry;
		owner = found_share->sns_owner;

		root_op_context.req_ctx.export = found_share->sns_export;
		root_op_context.req_ctx.fsal_export =
			root_op_context.req_ctx.export->fsal_export;
		get_gsh_export_ref(root_op_context.req_ctx.export);

		/* get a reference to the owner */
		inc_state_owner_ref(owner);

		pthread_mutex_unlock(&nsmclient->ssc_mutex);

		/* Remove all shares held by this NSM Client and
		 * Owner on the file (on all exports)
		 */
		status = state_nlm_unshare(entry,
					   OPEN4_SHARE_ACCESS_NONE,
					   OPEN4_SHARE_DENY_NONE,
					   owner);

		put_gsh_export(root_op_context.req_ctx.export);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next share,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogFullDebug(COMPONENT_STATE,
				     "state_nlm_unshare returned %s",
				     state_err_str(status));
			errcnt++;
		}

		dec_state_owner_ref(owner);
	}

	/* Put locks from current client incarnation onto end of list */
	pthread_mutex_lock(&nsmclient->ssc_mutex);
	glist_add_list_tail(&nsmclient->ssc_lock_list, &newlocks);
	pthread_mutex_unlock(&nsmclient->ssc_mutex);
	LogFullDebug(COMPONENT_STATE, "DONE");

	release_root_op_context();
	return status;
}

/**
 * @brief Release all locks held by a lock owner
 *
 * @param[in] owner Lock owner
 * @param[in] state Associated state
 *
 * @return State status.
 */
state_status_t state_owner_unlock_all(state_owner_t *owner,
				      state_t *state)
{
	state_lock_entry_t *found_entry;
	fsal_lock_param_t lock;
	cache_entry_t *entry;
	int errcnt = 0;
	state_status_t status = 0;
	struct gsh_export *saved_export = op_ctx->export;

	/* Only accept so many errors before giving up. */
	while (errcnt < STATE_ERR_MAX) {
		pthread_mutex_lock(&owner->so_mutex);

		/* We just need to find any file this owner has locks on.
		 * We pick the first lock the owner holds, and use it's file.
		 */
		found_entry =
		    glist_first_entry(&owner->so_lock_list, state_lock_entry_t,
				      sle_owner_locks);

		/* If we don't find any entries, then we are done. */
		if ((found_entry == NULL) ||
		    (found_entry->sle_state != state)) {
			pthread_mutex_unlock(&owner->so_mutex);
			break;
		}

		lock_entry_inc_ref(found_entry);

		/* Move this entry to the end of the list
		 * (this will help if errors occur)
		 */
		glist_del(&found_entry->sle_owner_locks);
		glist_add_tail(&owner->so_lock_list,
			       &found_entry->sle_owner_locks);

		pthread_mutex_unlock(&owner->so_mutex);

		/* Extract the cache inode entry from the lock entry and
		 * release the lock entry
		 */
		entry = found_entry->sle_entry;
		op_ctx->export = found_entry->sle_export;
		op_ctx->fsal_export = op_ctx->export->fsal_export;

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

		lock_entry_dec_ref(found_entry);
		cache_inode_lru_ref(entry, LRU_FLAG_NONE);

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Make lock that covers the whole file.
		 * type doesn't matter for unlock
		 */
		lock.lock_type = FSAL_LOCK_R;
		lock.lock_start = 0;
		lock.lock_length = 0;

		/* Remove all locks held by this owner on the file */
		status =
		    state_unlock(entry, owner, state, &lock,
				 POSIX_LOCK);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next lock,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogDebug(COMPONENT_STATE, "state_unlock failed %s",
				 state_err_str(status));
			errcnt++;
		}

		/* Release the lru ref to the cache inode we held while
		 * calling unlock
		 */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
	}

	op_ctx->export = saved_export;
	if (saved_export != NULL)
		op_ctx->fsal_export = op_ctx->export->fsal_export;
	return status;
}

/**
 * @brief Release all locks held on an export
 *
 */

void state_export_unlock_all(void)
{
	state_lock_entry_t *found_entry;
	fsal_lock_param_t lock;
	cache_entry_t *entry;
	int errcnt = 0;
	state_status_t status = 0;
	state_owner_t *owner;
	state_t *state;
	lock_type_t lock_type;

	/* Only accept so many errors before giving up. */
	while (errcnt < STATE_ERR_MAX) {
		PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);

		/* We just need to find any file this owner has locks on.
		 * We pick the first lock the owner holds, and use it's file.
		 */
		found_entry = glist_first_entry(&op_ctx->export->exp_lock_list,
						state_lock_entry_t,
						sle_export_locks);

		/* If we don't find any entries, then we are done. */
		if (found_entry == NULL) {
			PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
			break;
		}

		lock_entry_inc_ref(found_entry);

		/* Move this entry to the end of the list
		 * (this will help if errors occur)
		 */
		glist_del(&found_entry->sle_export_locks);
		glist_add_tail(&op_ctx->export->exp_lock_list,
			       &found_entry->sle_export_locks);

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

		/* Extract the cache inode entry from the lock entry and
		 * release the lock entry
		 */
		entry = found_entry->sle_entry;
		owner = found_entry->sle_owner;
		state = found_entry->sle_state;
		lock_type = found_entry->sle_type;

		PTHREAD_RWLOCK_wrlock(&entry->state_lock);

		lock_entry_dec_ref(found_entry);
		cache_inode_lru_ref(entry, LRU_FLAG_NONE);

		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		/* Make lock that covers the whole file.
		 * type doesn't matter for unlock
		 */
		lock.lock_type = FSAL_LOCK_R;
		lock.lock_start = 0;
		lock.lock_length = 0;

		/* Remove all locks held by this owner on the file */
		status = state_unlock(entry, owner, state,
				      &lock, lock_type);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next lock,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogDebug(COMPONENT_STATE, "state_unlock failed %s",
				 state_err_str(status));
			errcnt++;
		}

		/* Release the lru ref to the cache inode we held while
		 * calling unlock
		 */
		cache_inode_put(entry);
	}
}

/**
 * @brief Find a lock and add to grant list
 *
 * @param[in] entry      File to search
 * @param[in] owner      Lock owner
 * @param[in] lock       Lock description
 * @param[in] grant_type Grant type
 */
void find_blocked_lock_upcall(cache_entry_t *entry, void *owner,
			      fsal_lock_param_t *lock,
			      state_grant_type_t grant_type)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist;
	state_block_data_t *pblock;

	pthread_mutex_lock(&blocked_locks_mutex);

	glist_for_each(glist, &state_blocked_locks) {
		pblock = glist_entry(glist, state_block_data_t, sbd_list);

		found_entry = pblock->sbd_lock_entry;

		/* Check if got an entry */
		if (found_entry == NULL)
			continue;

		/* Check if for same file */
		if (found_entry->sle_entry != entry)
			continue;

		/* Check if for same owner */
		if (found_entry->sle_owner != owner)
			continue;

		/* Check if same lock */
		if (different_lock(&found_entry->sle_lock, lock))
			continue;

		/* Put lock on list of locks granted by FSAL */
		glist_del(&pblock->sbd_list);
		pblock->sbd_grant_type = grant_type;
		if (state_block_schedule(pblock) != STATE_SUCCESS) {
			LogMajor(COMPONENT_STATE,
				 "Unable to schedule lock notification.");
		}

		LogEntry("Blocked Lock found", found_entry);

		pthread_mutex_unlock(&blocked_locks_mutex);

		return;
	}			/* glist_for_each_safe */

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
		LogBlockedList("Blocked Lock List",
			       NULL, &state_blocked_locks);

	pthread_mutex_unlock(&blocked_locks_mutex);

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS)) {
		PTHREAD_RWLOCK_rdlock(&entry->state_lock);

		LogList("File Lock List", entry, &entry->object.file.lock_list);

		PTHREAD_RWLOCK_unlock(&entry->state_lock);
	}

	/* We must be out of sync with FSAL, this is fatal */
	LogLockDesc(COMPONENT_STATE, NIV_MAJ, "Blocked Lock Not Found for",
		    entry, owner, lock);
	LogFatal(COMPONENT_STATE, "Locks out of sync with FSAL");
}

/**
 * @brief Handle upcall for granted lock
 *
 * @param[in] entry File on which lock is granted
 * @param[in] owner Lock owner
 * @param[in] lock  Lock description
 */
void grant_blocked_lock_upcall(cache_entry_t *entry, void *owner,
			       fsal_lock_param_t *lock)
{
	LogLockDesc(COMPONENT_STATE, NIV_DEBUG, "Grant Upcall for", entry,
		    owner, lock);

	find_blocked_lock_upcall(entry, owner, lock, STATE_GRANT_FSAL);
}

/**
 * @brief Handle upcall for available lock
 *
 * @param[in] entry File on which lock has become available
 * @param[in] owner Lock owner
 * @param[in] lock  Lock description
 */
void available_blocked_lock_upcall(cache_entry_t *entry, void *owner,
				   fsal_lock_param_t *lock)
{
	LogLockDesc(COMPONENT_STATE, NIV_DEBUG, "Available Upcall for", entry,
		    owner, lock);

	find_blocked_lock_upcall(entry, owner, lock,
				 STATE_GRANT_FSAL_AVAILABLE);
}

/**
 * @brief Free all locks on a file
 *
 * @param[in] entry File to free
 */
void state_lock_wipe(cache_entry_t *entry)
{
	if (glist_empty(&entry->object.file.lock_list))
		return;

	free_list(&entry->object.file.lock_list);

	cache_inode_dec_pin_ref(entry, false);
}

void cancel_all_nlm_blocked()
{
	state_lock_entry_t *found_entry;
	cache_entry_t *pentry;
	state_block_data_t *pblock;
	struct root_op_context root_op_context;

	/* Initialize context */
	init_root_op_context(&root_op_context, NULL, NULL, 0, 0, NFS_REQUEST);

	LogDebug(COMPONENT_STATE, "Cancel all blocked locks");

	pthread_mutex_lock(&blocked_locks_mutex);

	pblock = glist_first_entry(&state_blocked_locks,
				   state_block_data_t,
				   sbd_list);

	if (pblock == NULL) {
		LogFullDebug(COMPONENT_STATE, "No blocked locks");
		goto out;
	}

	while (pblock != NULL) {
		found_entry = pblock->sbd_lock_entry;

		/* Remove lock from blocked list */
		glist_del(&pblock->sbd_list);

		lock_entry_inc_ref(found_entry);

		pthread_mutex_unlock(&blocked_locks_mutex);

		root_op_context.req_ctx.export = found_entry->sle_export;
		root_op_context.req_ctx.fsal_export =
			root_op_context.req_ctx.export->fsal_export;
		get_gsh_export_ref(root_op_context.req_ctx.export);

		LogEntry("Blocked Lock found", found_entry);

		pentry = found_entry->sle_entry;

		cancel_blocked_lock(pentry,
				    found_entry);

		if (pblock->sbd_blocked_cookie != NULL)
			gsh_free(pblock->sbd_blocked_cookie);

		gsh_free(found_entry->sle_block_data);
		found_entry->sle_block_data = NULL;

		LogEntry("Canceled Lock", found_entry);

		put_gsh_export(root_op_context.req_ctx.export);

		lock_entry_dec_ref(found_entry);

		pthread_mutex_lock(&blocked_locks_mutex);

		/* Get next item off list */
		pblock = glist_first_entry(&state_blocked_locks,
					   state_block_data_t,
					   sbd_list);
	}

out:

	pthread_mutex_unlock(&blocked_locks_mutex);
	release_root_op_context();
	return;
}

/** @} */
