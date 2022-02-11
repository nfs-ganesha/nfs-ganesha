// SPDX-License-Identifier: LGPL-3.0-or-later
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
/*#include "nlm_util.h"*/
#include "export_mgr.h"

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
static struct glist_head state_all_locks = GLIST_HEAD_INIT(state_all_locks);
/**
 * @brief All locks mutex
 */
pthread_mutex_t all_locks_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * @brief All locks blocked in FSAL
 */
struct glist_head state_blocked_locks = GLIST_HEAD_INIT(state_blocked_locks);

/**
 * @brief Mutex to protect lock lists
 */
pthread_mutex_t blocked_locks_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Owner of state with no defined owner
 */
state_owner_t unknown_owner = {
	.so_owner_val = "ganesha_unknown_owner",
	.so_type = STATE_LOCK_OWNER_UNKNOWN,
	.so_refcount = 1,
	.so_owner_len = 21,
	.so_lock_list = GLIST_HEAD_INIT(unknown_owner.so_lock_list),
	.so_mutex = PTHREAD_MUTEX_INITIALIZER
};

static state_status_t do_lock_op(struct fsal_obj_handle *obj,
				 state_t *state,
				 fsal_lock_op_t lock_op,
				 state_owner_t *owner,
				 fsal_lock_param_t *lock,
				 state_owner_t **holder,
				 fsal_lock_param_t *conflict,
				 bool overlap);
/**
 * @brief Blocking lock cookies
 */

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
#ifdef _USE_NLM
	return lock_entry->sle_owner->so_type == STATE_LOCK_OWNER_NLM;
#else /* _USE_NLM */
	return false;
#endif /* _USE_NLM */
}

/******************************************************************************
 *
 * Functions to display various aspects of a lock
 *
 ******************************************************************************/
/**
 * @brief Find the end of lock range
 *
 * @param[in] lock The lock to check
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
 * @param[in] blocking The blocking status
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

const char *str_block_type(state_block_type_t btype)
{
	switch (btype) {
	case STATE_BLOCK_NONE:
		return "STATE_BLOCK_NONE    ";
	case STATE_BLOCK_INTERNAL:
		return "STATE_BLOCK_INTERNAL";
	case STATE_BLOCK_ASYNC:
		return "STATE_BLOCK_ASYNC   ";
	case STATE_BLOCK_POLL:
		return "STATE_BLOCK_POLL    ";
	}

	return "unknown             ";
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
 * @brief Log a lock entry with a passed refcount
 *
 * @param[in] reason Arbitrary string
 * @param[in] le     Entry to log
 */
static void
log_entry_ref_count(const char *reason, state_lock_entry_t *le,
		    int32_t refcount, char *file, int line, char *function)
{
	if (isFullDebug(COMPONENT_STATE)) {
		char owner[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(owner), owner, owner};

		display_owner(&dspbuf, le->sle_owner);

		DisplayLogComponentLevel(COMPONENT_STATE, file, line, function,
			NIV_FULL_DEBUG,
			"%s Entry: %p obj=%p, fileid=%" PRIu64
			", export=%u, type=%s, start=0x%"PRIx64
			", end=0x%"PRIx64
			", blocked=%s/%p/%s, state=%p, refcount=%"PRIu32
			", owner={%s}",
			reason, le, le->sle_obj,
			(uint64_t) le->sle_obj->fileid,
			(unsigned int)le->sle_export->export_id,
			str_lockt(le->sle_lock.lock_type),
			le->sle_lock.lock_start,
			lock_end(&le->sle_lock),
			str_blocked(le->sle_blocked), le->sle_block_data,
			le->sle_block_data
			    ? str_block_type(le->sle_block_data->sbd_block_type)
			    : str_block_type(STATE_BLOCK_NONE),
			le->sle_state, refcount, owner);
	}
}

#define LogEntryRefCount(reason, le, refcount) \
	log_entry_ref_count(reason, le, refcount, \
	(char *) __FILE__, __LINE__, (char *) __func__)

/**
 * @brief Log a lock entry
 *
 * @param[in] reason Arbitrary string
 * @param[in] le     Entry to log
 */
#define LogEntry(reason, le) \
	log_entry_ref_count(reason, le, \
	atomic_fetch_int32_t(&le->sle_ref_count), \
	(char *) __FILE__, __LINE__, (char *) __func__)

/**
 * @brief Log a list of locks
 *
 * @param[in] reason Arbitrary string
 * @param[in] obj  FSAL object (mostly unused)
 * @param[in] list   List of lock entries
 *
 * @retval true if list is empty.
 * @retval false if list is non-empty.
 */
static bool LogList(const char *reason, struct fsal_obj_handle *obj,
		    struct glist_head *list)
{
	if (isFullDebug(COMPONENT_STATE)) {
		struct glist_head *glist;
		state_lock_entry_t *found_entry;

		if (glist_empty(list)) {
			if (obj != NULL)
				LogFullDebug(COMPONENT_STATE,
					     "%s for %p is empty", reason,
					     obj);
			else
				LogFullDebug(COMPONENT_STATE, "%s is empty",
					     reason);
			return true;
		}

		glist_for_each(glist, list) {
			found_entry = glist_entry(glist,
						  state_lock_entry_t,
						  sle_list);

			LogEntry(reason, found_entry);
			if (found_entry->sle_obj == NULL)
				break;
		}
	}

	return false;
}

/**
 * @brief Log blocked locks on list
 *
 * Must hold blocked_locks_mutex.
 *
 * @param[in] reason Arbitrary string
 * @param[in] obj  File
 * @param[in] list   List of lock entries
 *
 * @retval true if list is empty.
 * @retval false if list is non-empty.
 */
static bool LogBlockedList(const char *reason, struct fsal_obj_handle *obj,
			   struct glist_head *list)
{
	if (isFullDebug(COMPONENT_STATE)) {
		struct glist_head *glist;
		state_lock_entry_t *found_entry;
		state_block_data_t *block_entry;

		if (glist_empty(list)) {
			if (obj != NULL)
				LogFullDebug(COMPONENT_STATE,
					     "%s for %p is empty", reason,
					     obj);
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
			if (found_entry->sle_obj == NULL)
				break;
		}
	}

	return false;
}

/**
 * @brief Log a lock
 *
 * @param[in] component The component to log to
 * @param[in] debug     Log level
 * @param[in] reason    Arbitrary string
 * @param[in] obj	File
 * @param[in] owner     Lock owner
 * @param[in] lock      Lock description
 */
void log_lock(log_components_t component,
	      log_levels_t debug,
	      const char *reason,
	      struct fsal_obj_handle *obj,
	      state_owner_t *owner,
	      fsal_lock_param_t *lock,
	      char *file,
	      int line,
	      char *function)
{
	if (isLevel(component, debug)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		if (owner != NULL)
			display_owner(&dspbuf, owner);
		else
			display_cat(&dspbuf, "NONE");

		DisplayLogComponentLevel(component, file, line, function, debug,
			"%s Lock: obj=%p, fileid=%" PRIu64
			", type=%s, start=0x%"PRIx64", end=0x%"PRIx64
			", owner={%s}",
			reason, obj,
			(uint64_t) obj->fileid,
			str_lockt(lock->lock_type),
			lock->lock_start,
			lock_end(lock), str);
	}
}

/**
 * @brief Log a lock description
 *
 * @param[in] component The component to log to
 * @param[in] debug     Log level
 * @param[in] reason    Arbitrary string
 * @param[in] obj       FSAL obj handle
 * @param[in] owner     Lock owner
 * @param[in] lock      Lock description
 */
void log_lock_desc(log_components_t component, log_levels_t debug,
		   const char *reason, struct fsal_obj_handle *obj, void *owner,
		   fsal_lock_param_t *lock, char *file, int line,
		   char *function)
{
	if (isLevel(component, debug)) {
		DisplayLogComponentLevel(component, file, line, function, debug,
			"%s Lock: obj=%p, owner=%p, type=%s, start=0x%llx, end=0x%llx",
			reason, obj, owner, str_lockt(lock->lock_type),
			(unsigned long long)lock->lock_start,
			(unsigned long long)lock_end(lock));
	}
}

#define LogLockDesc(component, debug, reason, obj, owner, lock) \
	log_lock_desc(component, debug, reason, obj, owner, lock, \
		      (char *) __FILE__, __LINE__, (char *) __func__)

/**
 * @brief Log all locks
 *
 * @param[in] label Arbitrary string
 */
void dump_all_locks(const char *label)
{
#ifdef DEBUG_SAL
	struct glist_head *glist;

	PTHREAD_MUTEX_lock(&all_locks_mutex);

	if (glist_empty(&state_all_locks)) {
		LogFullDebug(COMPONENT_STATE, "All Locks are freed");
		PTHREAD_MUTEX_unlock(&all_locks_mutex);
		return;
	}

	glist_for_each(glist, &state_all_locks)
	    LogEntry(label,
		     glist_entry(glist, state_lock_entry_t, sle_all_locks));

	PTHREAD_MUTEX_unlock(&all_locks_mutex);
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
 * This function aborts (in gsh_malloc) if no memory is available.
 *
 * @param[in] obj      File to lock
 * @param[in] export   Export being accessed
 * @param[in] blocked  Blocking status
 * @param[in] owner    Lock owner
 * @param[in] state    State associated with lock
 * @param[in] lock     Lock description
 *
 * @return The new entry.
 */
static state_lock_entry_t *create_state_lock_entry(struct fsal_obj_handle *obj,
						   struct gsh_export *export,
						   state_blocking_t blocked,
						   state_owner_t *owner,
						   state_t *state,
						   fsal_lock_param_t *lock)
{
	state_lock_entry_t *new_entry;

	new_entry = gsh_calloc(1, sizeof(*new_entry));

	LogFullDebug(COMPONENT_STATE, "new_entry = %p owner %p", new_entry,
		     owner);

	PTHREAD_MUTEX_init(&new_entry->sle_mutex, NULL);

	/* sle_block_data will be filled in later if necessary */
	new_entry->sle_block_data = NULL;
	new_entry->sle_ref_count = 1;
	new_entry->sle_obj = obj;
	new_entry->sle_blocked = blocked;
	new_entry->sle_owner = owner;
	new_entry->sle_state = state;
	new_entry->sle_lock = *lock;
	new_entry->sle_export = export;

#ifdef _USE_NLM
	if (owner->so_type == STATE_LOCK_OWNER_NLM) {
		/* Add to list of locks owned by client that owner belongs to */
		state_nlm_client_t *client =
			owner->so_owner.so_nlm_owner.so_client;

		inc_nsm_client_ref(client->slc_nsm_client);

		PTHREAD_MUTEX_lock(&client->slc_nsm_client->ssc_mutex);
		glist_add_tail(&client->slc_nsm_client->ssc_lock_list,
			       &new_entry->sle_client_locks);

		PTHREAD_MUTEX_unlock(&client->slc_nsm_client->ssc_mutex);
	}
#endif /* _USE_NLM */

	/* Add to list of locks owned by export */
	PTHREAD_RWLOCK_wrlock(&new_entry->sle_export->lock);
	glist_add_tail(&new_entry->sle_export->exp_lock_list,
		       &new_entry->sle_export_locks);
	PTHREAD_RWLOCK_unlock(&new_entry->sle_export->lock);
	get_gsh_export_ref(new_entry->sle_export);

	/* Get ref for sle_obj */
	obj->obj_ops->get_ref(obj);

	/* Add to list of locks owned by owner */
	inc_state_owner_ref(owner);

	PTHREAD_MUTEX_lock(&owner->so_mutex);

	if (state != NULL) {
		glist_add_tail(&state->state_data.lock.state_locklist,
			       &new_entry->sle_state_locks);

		inc_state_t_ref(state);
	}

	glist_add_tail(&owner->so_lock_list, &new_entry->sle_owner_locks);

	PTHREAD_MUTEX_unlock(&owner->so_mutex);

#ifdef DEBUG_SAL
	PTHREAD_MUTEX_lock(&all_locks_mutex);

	glist_add_tail(&state_all_locks, &new_entry->sle_all_locks);

	PTHREAD_MUTEX_unlock(&all_locks_mutex);
#endif

	return new_entry;
}

/**
 * @brief Duplicate a lock entry
 *
 * @param[in] orig_entry Entry to duplicate
 *
 * @return New entry.
 */
static inline state_lock_entry_t *
state_lock_entry_t_dup(state_lock_entry_t *orig_entry)
{
	return create_state_lock_entry(orig_entry->sle_obj,
				       orig_entry->sle_export,
				       orig_entry->sle_blocked,
				       orig_entry->sle_owner,
				       orig_entry->sle_state,
				       &orig_entry->sle_lock);
}

/**
 * @brief Take a reference on a lock entry
 *
 * @param[in,out] lock_entry Entry to reference
 */
static inline void lock_entry_inc_ref(state_lock_entry_t *lock_entry)
{
	int32_t refcount = atomic_inc_int32_t(&lock_entry->sle_ref_count);

	LogEntryRefCount("Increment refcount", lock_entry, refcount);
}

/**
 * @brief Relinquish a reference on a lock entry
 *
 * @param[in,out] lock_entry Entry to release
 */
static void lock_entry_dec_ref(state_lock_entry_t *lock_entry)
{
	int32_t refcount = atomic_dec_int32_t(&lock_entry->sle_ref_count);

	LogEntryRefCount(refcount != 0
			 ? "Decrement refcount"
			 : "Decrement refcount and freeing",
			 lock_entry, refcount);

	if (refcount == 0) {
		/* Release block data if present */
		if (lock_entry->sle_block_data != NULL) {
			/* need to remove from the state_blocked_locks list */
			PTHREAD_MUTEX_lock(&blocked_locks_mutex);
			glist_del(&lock_entry->sle_block_data->sbd_list);
			PTHREAD_MUTEX_unlock(&blocked_locks_mutex);
			gsh_free(lock_entry->sle_block_data);
		}
#ifdef DEBUG_SAL
		PTHREAD_MUTEX_lock(&all_locks_mutex);
		glist_del(&lock_entry->sle_all_locks);
		PTHREAD_MUTEX_unlock(&all_locks_mutex);
#endif

		lock_entry->sle_obj->obj_ops->put_ref(lock_entry->sle_obj);
		put_gsh_export(lock_entry->sle_export);
		PTHREAD_MUTEX_destroy(&lock_entry->sle_mutex);
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
#ifdef _USE_NLM
		if (owner->so_type == STATE_LOCK_OWNER_NLM) {
			/* Remove from list of locks owned
			 * by client that owner belongs to
			 */
			state_nlm_client_t *client =
				owner->so_owner.so_nlm_owner.so_client;

			PTHREAD_MUTEX_lock(&client->slc_nsm_client->ssc_mutex);

			glist_del(&lock_entry->sle_client_locks);

			PTHREAD_MUTEX_unlock(
					&client->slc_nsm_client->ssc_mutex);

			dec_nsm_client_ref(client->slc_nsm_client);
		}
#endif /* _USE_NLM */

		/* Remove from list of locks owned by export */
		PTHREAD_RWLOCK_wrlock(&lock_entry->sle_export->lock);
		glist_del(&lock_entry->sle_export_locks);
		PTHREAD_RWLOCK_unlock(&lock_entry->sle_export->lock);

		/* Remove from list of locks owned by owner */
		PTHREAD_MUTEX_lock(&owner->so_mutex);

		glist_del(&lock_entry->sle_state_locks);
		glist_del(&lock_entry->sle_owner_locks);

		PTHREAD_MUTEX_unlock(&owner->so_mutex);

		dec_state_owner_ref(owner);
		if (lock_entry->sle_state != NULL)
			dec_state_t_ref(lock_entry->sle_state);
	}

	lock_entry->sle_owner = NULL;
	glist_del(&lock_entry->sle_list);
	lock_entry_dec_ref(lock_entry);
}

/**
 * @brief Find a conflicting entry
 *
 * @note The st_lock MUST be held
 *
 * @param[in] ostate File state to search
 * @param[in] owner The lock owner
 * @param[in] lock  Lock to check
 *
 * @return A conflicting entry or NULL.
 */
static state_lock_entry_t *get_overlapping_entry(struct state_hdl *ostate,
						 state_owner_t *owner,
						 fsal_lock_param_t *lock)
{
	struct glist_head *glist;
	state_lock_entry_t *found_entry = NULL;
	uint64_t found_entry_end, range_end = lock_end(lock);

	glist_for_each(glist, &ostate->file.lock_list) {
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
 * @note The st_lock MUST be held
 *
 * @param[in,out] ostate     File state to operate on
 * @param[in]     lock_entry Lock to add
 */
static void merge_lock_entry(struct state_hdl *ostate,
			     state_lock_entry_t *lock_entry)
{
	state_lock_entry_t *check_entry;
	state_lock_entry_t *check_entry_right;
	uint64_t check_entry_end;
	uint64_t lock_entry_end;
	struct glist_head *glist;
	struct glist_head *glistn;

	/* lock_entry might be STATE_NON_BLOCKING or STATE_GRANTING */

	glist_for_each_safe(glist, glistn, &ostate->file.lock_list) {
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
				glist_add_tail(&ostate->file.lock_list,
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
 * @param[in] list The list of locks to free
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
 * @param[in,out] found_entry Lock being modified
 * @param[in]     lock        Lock being removed
 * @param[out]    split_list  Remaining fragments of found_entry
 * @param[out]    remove_list Removed lock entries
 * @param[out]    removed     True if lock is removed
 *
 * @return State status.
 */
static state_status_t subtract_lock_from_entry(state_lock_entry_t *found_entry,
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

		found_entry_left->sle_lock.lock_length =
		    lock->lock_start - found_entry->sle_lock.lock_start;
		LogEntry("Left split", found_entry_left);
		glist_add_tail(split_list, &(found_entry_left->sle_list));
	}

	if (range_end < found_entry_end) {
		found_entry_right = state_lock_entry_t_dup(found_entry);

		found_entry_right->sle_lock.lock_start = range_end + 1;

		/* found_entry_end being UINT64_MAX indicates that the
		 * sle_lock.lock_length is zero and the lock is held till
		 * the end of the file. In such case assign the split lock
		 * length too to zero to indicate the file end.
		 */
		if (found_entry_end == UINT64_MAX)
			found_entry_right->sle_lock.lock_length = 0;
		else
			found_entry_right->sle_lock.lock_length =
			    found_entry_end - range_end;
		LogEntry("Right split", found_entry_right);
		glist_add_tail(split_list, &(found_entry_right->sle_list));
	}

 complete_remove:

	/* Remove the lock from the list it's
	 * on and put it on the remove_list
	 */
	glist_move_tail(remove_list, &(found_entry->sle_list));

	*removed = true;
	return status;
}

/**
 * @brief Subtract a lock from a list of locks
 *
 * This function possibly splits entries in the list.
 *
 * @param[in]     owner   Lock owner
 * @param[in]     state   Associated lock state
 * @param[in]     lock    Lock to remove
 * @param[out]    removed True if an entry was removed
 * @param[in,out] list    List of locks to modify
 *
 * @return State status.
 */
static state_status_t subtract_lock_from_list(state_owner_t *owner,
					      bool state_applies,
					      int32_t state,
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
		if (state_applies &&
		    found_entry->sle_state->state_seqid == state)
			continue;

		/* We have matched owner. Even though we are taking a reference
		 * to found_entry, we don't inc the ref count because we want
		 * to drop the lock entry.
		 */
		status =
		    subtract_lock_from_entry(found_entry, lock,
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
			glist_move_tail(list, &(found_entry->sle_list));
		}
	} else {
		/* free the enttries on the remove_list */
		free_list(&remove_list);

		/* now add the split lock list */
		glist_add_list_tail(list, &split_lock_list);
	}

	LogFullDebug(COMPONENT_STATE,
		     "List of all locks for list=%p returning %d", list,
		     status);

	return status;
}

/******************************************************************************
 *
 * Implement hash table to hash blocked lock entries by cookie
 *
 ******************************************************************************/

static void grant_blocked_locks(struct state_hdl *);

static inline int display_lock_cookie(struct display_buffer *dspbuf,
				      struct gsh_buffdesc *buff)
{
	return display_opaque_value(dspbuf, buff->addr, buff->len);
}

/**
 * @brief Display lock cookie in hash table
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff   Key to display
 */
int display_lock_cookie_key(struct display_buffer *dspbuf,
			    struct gsh_buffdesc *buff)
{
	return display_lock_cookie(dspbuf, buff);
}

/**
 * @brief Display lock cookie entry
 *
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     he     Cookie entry to display
 *
 * @return the bytes remaining in the buffer.
 */
int display_lock_cookie_entry(struct display_buffer *dspbuf,
			      state_cookie_entry_t *he)
{
	int b_left = display_printf(dspbuf, "%p: cookie ", he);

	if (b_left <= 0)
		return b_left;

	b_left = display_opaque_value(dspbuf,
				      he->sce_cookie,
				      he->sce_cookie_size);

	if (b_left <= 0)
		return b_left;

	b_left = display_printf(dspbuf, " obj {%p fileid=%" PRIu64 "} lock {",
				he->sce_obj, he->sce_obj->fileid);

	if (b_left <= 0)
		return b_left;

	if (he->sce_lock_entry != NULL) {
		b_left = display_printf(dspbuf, "%p owner {",
					he->sce_lock_entry);

		if (b_left <= 0)
			return b_left;

		b_left = display_owner(dspbuf, he->sce_lock_entry->sle_owner);

		if (b_left <= 0)
			return b_left;

		b_left = display_printf(
			dspbuf,
			"} type=%s start=0x%"PRIx64" end=0x%"
			PRIx64" blocked=%s}",
			str_lockt(he->sce_lock_entry->sle_lock.lock_type),
			he->sce_lock_entry->sle_lock.lock_start,
			lock_end(&he->sce_lock_entry->sle_lock),
			str_blocked(he->sce_lock_entry->sle_blocked));
	} else {
		b_left = display_printf(dspbuf, "<NULL>}");
	}

	return b_left;
}

/**
 * @brief Display lock cookie entry in hash table
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff   Value to display
 */
int display_lock_cookie_val(struct display_buffer *dspbuf,
			    struct gsh_buffdesc *buff)
{
	return display_lock_cookie_entry(dspbuf, buff->addr);
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
		char str1[LOG_BUFF_LEN / 2] = "\0";
		char str2[LOG_BUFF_LEN / 2] = "\0";
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_lock_cookie(&dspbuf1, buff1);
		display_lock_cookie(&dspbuf2, buff2);
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
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	void *cookie = cookie_entry->sce_cookie;
	state_lock_entry_t *lock_entry = cookie_entry->sce_lock_entry;

	if (isFullDebug(COMPONENT_STATE)) {
		display_lock_cookie_entry(&dspbuf, cookie_entry);
		str_valid = true;
	}

	/* Since the cookie is not in the hash table,
	 * we can just free the memory
	 */
	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Free Lock Cookie {%s}", str);

	/* If block data is still attached to lock entry, remove it */
	if (lock_entry != NULL && unblock) {
		if (lock_entry->sle_block_data != NULL)
			lock_entry->sle_block_data->sbd_blocked_cookie = NULL;

		lock_entry_dec_ref(lock_entry);
	}

	/* Free the memory for the cookie and the cookie entry */
	gsh_free(cookie);
	gsh_free(cookie_entry);
}

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
	.display_key = display_lock_cookie_key,
	.display_val = display_lock_cookie_val,
	.flags = HT_FLAG_NONE,
};

static hash_table_t *ht_lock_cookies;

/**
 * @brief Add a grant cookie to a blocked lock
 *
 * @param[in]  obj          File to operate on
 * @param[in]  cookie       Cookie to add
 * @param[in]  cookie_size  Cookie length
 * @param[in]  lock_entry   Lock entry
 * @param[out] cookie_entry New cookie entry
 *
 * @return State status.
 */
state_status_t state_add_grant_cookie(struct fsal_obj_handle *obj,
				      void *cookie, int cookie_size,
				      state_lock_entry_t *lock_entry,
				      state_cookie_entry_t **cookie_entry)
{
	struct gsh_buffdesc buffkey, buffval;
	state_cookie_entry_t *hash_entry;
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	state_status_t status = 0;

	*cookie_entry = NULL;

	if (lock_entry->sle_block_data == NULL || cookie == NULL
	    || cookie_size == 0) {
		/* Something's wrong with this entry */
		status = STATE_INCONSISTENT_ENTRY;
		return status;
	}

	if (isFullDebug(COMPONENT_STATE)) {
		display_opaque_value(&dspbuf, cookie, cookie_size);
		str_valid = true;
	}

	hash_entry = gsh_calloc(1, sizeof(*hash_entry));

	buffkey.addr = gsh_malloc(cookie_size);

	hash_entry->sce_obj = obj;
	hash_entry->sce_lock_entry = lock_entry;
	hash_entry->sce_cookie = buffkey.addr;
	hash_entry->sce_cookie_size = cookie_size;

	memcpy(buffkey.addr, cookie, cookie_size);
	buffkey.len = cookie_size;
	buffval.addr = (void *)hash_entry;
	buffval.len = sizeof(*hash_entry);

	if (isFullDebug(COMPONENT_STATE)) {
		display_lock_cookie_entry(&dspbuf, hash_entry);
		str_valid = true;
	}

	if (hashtable_test_and_set(ht_lock_cookies,
				   &buffkey,
				   &buffval,
				   HASHTABLE_SET_HOW_SET_NO_OVERWRITE)
	    != HASHTABLE_SUCCESS) {
		gsh_free(hash_entry);
		if (str_valid)
			LogFullDebug(COMPONENT_STATE,
				     "Lock Cookie {%s} HASH TABLE ERROR", str);
		status = STATE_HASH_TABLE_ERROR;
		return status;
	}

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Lock Cookie {%s} Added", str);

	switch (lock_entry->sle_block_data->sbd_grant_type) {
	case STATE_GRANT_NONE:
		/* Shouldn't get here */
		status = STATE_INCONSISTENT_ENTRY;
		break;

	case STATE_GRANT_POLL:
	case STATE_GRANT_FSAL_AVAILABLE:
		/* A poll triggered by the polling thread actually looks
		 * exactly like a poll triggered by an FSAL upcall...
		 *
		 * Now that we are sure we can continue, acquire the FSAL lock.
		 * If we get STATE_LOCK_BLOCKED we need to return...
		 */
		status = do_lock_op(obj,
				    lock_entry->sle_state,
				    FSAL_OP_LOCKB,
				    lock_entry->sle_owner,
				    &lock_entry->sle_lock,
				    NULL, NULL, false);
		break;

	case STATE_GRANT_INTERNAL:
		/* Now that we are sure we can continue, acquire the FSAL lock.
		 * If we get STATE_LOCK_BLOCKED we need to return...
		 */
		status = do_lock_op(obj,
				    lock_entry->sle_state,
				    FSAL_OP_LOCK,
				    lock_entry->sle_owner,
				    &lock_entry->sle_lock,
				    NULL, NULL, false);
		break;

	case STATE_GRANT_FSAL:
		/* No need to go to FSAL for lock */
		status = STATE_SUCCESS;
		break;
	}

	if (status != STATE_SUCCESS) {
		struct gsh_buffdesc buffused_key;

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

		/* Remove the hashtable entry */
		HashTable_Del(ht_lock_cookies, &buffkey, &buffused_key,
			      &buffval);

		/* And release the cookie without unblocking the lock.
		 * grant_blocked_locks() will decide whether to keep or
		 * free the block.
		 */
		free_cookie(hash_entry, false);

		return status;
	}

	/* Increment lock entry reference count and link it to the cookie */
	lock_entry_inc_ref(lock_entry);
	lock_entry->sle_block_data->sbd_blocked_cookie = hash_entry;
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
	status = do_lock_op(cookie_entry->sce_obj,
			    cookie_entry->sce_lock_entry->sle_state,
			    FSAL_OP_UNLOCK,
			    cookie_entry->sce_lock_entry->sle_owner,
			    &cookie_entry->sce_lock_entry->sle_lock,
			    NULL,	/* no conflict expected */
			    NULL,
			    false);

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
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	state_status_t status = 0;

	buffkey.addr = cookie;
	buffkey.len = cookie_size;

	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		display_lock_cookie(&dspbuf, &buffkey);

		LogFullDebug(COMPONENT_STATE, "KEY {%s}", str);

		str_valid = true;
	}

	if (HashTable_Del(ht_lock_cookies,
			  &buffkey,
			  &buffused_key,
			  &buffval) != HASHTABLE_SUCCESS) {
		if (str_valid)
			LogFullDebug(COMPONENT_STATE,
				     "KEY {%s} NOTFOUND", str);
		status = STATE_BAD_COOKIE;
		return status;
	}

	*cookie_entry = buffval.addr;

	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		display_lock_cookie_entry(&dspbuf, *cookie_entry);
		LogFullDebug(COMPONENT_STATE,
			     "Found Lock Cookie {%s}", str);
	}

	status = STATE_SUCCESS;
	return status;
}

/**
 * @brief Grant a blocked lock
 *
 * @param[in] ostate     File state on which to grant it
 * @param[in] lock_entry Lock entry
 */
void grant_blocked_lock_immediate(struct state_hdl *ostate,
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

	merge_lock_entry(ostate, lock_entry);
	LogEntry("Immediate Granted entry", lock_entry);

	/* A lock downgrade could unblock blocked locks */
	grant_blocked_locks(ostate);
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
	struct fsal_obj_handle *obj;

	lock_entry = cookie_entry->sce_lock_entry;
	obj = cookie_entry->sce_obj;

	/* This routine does not call obj->obj_ops->get_ref() because there
	 * MUST be at least one lock present for there to be a cookie_entry
	 * to even allow this routine to be called, and therefor the cache
	 * entry MUST be protected from being recycled.
	 */

	STATELOCK_lock(obj);

	/* We need to make sure lock is ready to be granted */
	if (lock_entry->sle_blocked == STATE_GRANTING) {
		/* Mark lock as granted */
		lock_entry->sle_blocked = STATE_NON_BLOCKING;

		/* Merge any touching or overlapping locks into this one. */
		LogEntry("Granted, merging locks for", lock_entry);
		merge_lock_entry(obj->state_hdl, lock_entry);

		LogEntry("Granted entry", lock_entry);

		/* A lock downgrade could unblock blocked locks */
		grant_blocked_locks(obj->state_hdl);
	}

	/* Free cookie and unblock lock.
	 * If somehow the lock was unlocked/canceled while the GRANT
	 * was in progress, this will completely clean up the lock.
	 */
	free_cookie(cookie_entry, true);

	STATELOCK_unlock(obj);
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
	struct gsh_export *export = lock_entry->sle_export;
	const char *reason;

	/* Try to grant if not cancelled and has block data and we are able
	 * to get an export reference.
	 */
	if (lock_entry->sle_blocked == STATE_CANCELED) {
		reason = "Removing canceled blocked lock entry";
	} else if (lock_entry->sle_block_data == NULL) {
		reason = "Removing blocked lock entry with no block data";
	} else if (!export_ready(export)) {
		reason = "Removing blocked lock entry due to stale export";
	} else {
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

		status = call_back(lock_entry->sle_obj,
				   lock_entry);

		if (status == STATE_LOCK_BLOCKED) {
			/* The lock is still blocked, restore it's type and
			 * leave it in the list.
			 */
			lock_entry->sle_blocked = blocked;
			lock_entry->sle_block_data->sbd_grant_type =
							STATE_GRANT_NONE;
			return;
		}

		/* At this point, we no longer need the entry on the
		 * blocked lock list.
		 */
		PTHREAD_MUTEX_lock(&blocked_locks_mutex);

		glist_del(&lock_entry->sle_block_data->sbd_list);

		PTHREAD_MUTEX_unlock(&blocked_locks_mutex);

		if (status == STATE_SUCCESS)
			return;

		reason = "Removing unsucessfully granted blocked lock";
	}

	/* There was no call back data, the call back failed,
	 * or the block was cancelled.
	 * Remove lock from list.
	 */
	LogEntry(reason, lock_entry);
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

	lock_entry_inc_ref(lock_entry);
	STATELOCK_lock(lock_entry->sle_obj);

	try_to_grant_lock(lock_entry);

	STATELOCK_unlock(lock_entry->sle_obj);
	lock_entry_dec_ref(lock_entry);
}

/**
 * @brief Attempt to grant all blocked locks on a file
 *
 * @param[in] ostate File state
 */

static void grant_blocked_locks(struct state_hdl *ostate)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist, *glistn;
	struct fsal_export *export = op_ctx->ctx_export->fsal_export;

	if (!ostate)
		return;

	/* If FSAL supports async blocking locks,
	 * allow it to grant blocked locks.
	 */
	if (export->exp_ops.fs_supports(export, fso_lock_support_async_block))
		return;

	glist_for_each_safe(glist, glistn, &ostate->file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		if (found_entry->sle_blocked != STATE_NLM_BLOCKING
		    && found_entry->sle_blocked != STATE_NFSV4_BLOCKING)
			continue;

		/* Found a blocked entry for this file,
		 * see if we can place the lock.
		 */
		if (get_overlapping_entry(ostate, found_entry->sle_owner,
					  &found_entry->sle_lock) != NULL)
			continue;

		/* Found an entry that might work, try to grant it. */
		try_to_grant_lock(found_entry);
	}
}

/**
 * @brief Cancel a blocked lock
 *
 * @param[in] obj      File on which to cancel the lock
 * @param[in] lock_entry Lock to cancel
 *
 * @return State status.
 */
static
void cancel_blocked_lock(struct fsal_obj_handle *obj,
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
		state_status = do_lock_op(obj,
					  lock_entry->sle_state,
					  FSAL_OP_CANCEL,
					  lock_entry->sle_owner,
					  &lock_entry->sle_lock,
					  NULL,	/* no conflict expected */
					  NULL,
					  false); /* overlap not relevant */

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
 * @param[in,out] ostate File state on which to operate
 * @param[in]     owner  The state owner for the lock
 * @param[in]     state  Associated state
 * @param[in]     lock   Lock description
 */
void cancel_blocked_locks_range(struct state_hdl *ostate,
				state_owner_t *owner,
				bool state_applies,
				int32_t state,
				fsal_lock_param_t *lock)
{
	struct glist_head *glist, *glistn;
	state_lock_entry_t *found_entry = NULL;
	uint64_t found_entry_end, range_end = lock_end(lock);

	glist_for_each_safe(glist, glistn, &ostate->file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		/* Skip locks not owned by owner */
		if (owner != NULL
		    && different_owners(found_entry->sle_owner, owner))
			continue;

		/* Skip locks owned by this NLM state.
		 * This protects NLM locks from the current iteration of an NLM
		 * client from being released by SM_NOTIFY.
		 */
		if (state_applies &&
		    found_entry->sle_state->state_seqid == state)
			continue;

		/* Skip granted locks */
		if (found_entry->sle_blocked == STATE_NON_BLOCKING)
			continue;

		LogEntry("Checking", found_entry);

		found_entry_end = lock_end(&found_entry->sle_lock);

		if ((found_entry_end >= lock->lock_start)
		    && (found_entry->sle_lock.lock_start <= range_end)) {
			/* lock overlaps, cancel it. */
			cancel_blocked_lock(ostate->file.obj, found_entry);
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
	struct fsal_obj_handle *obj;
	state_status_t status = STATE_SUCCESS;
	bool release;

	lock_entry = cookie_entry->sce_lock_entry;
	obj = cookie_entry->sce_obj;

	STATELOCK_lock(obj);

	/* We need to make sure lock is only "granted" once...
	 * It's (remotely) possible that due to latency, we might end up
	 * processing two GRANTED_RSP calls at the same time.
	 */
	if (lock_entry->sle_blocked == STATE_GRANTING) {
		/* Mark lock as canceled */
		lock_entry->sle_blocked = STATE_CANCELED;

		/* We had acquired an FSAL lock, need to release it. */
		status = do_lock_op(obj,
				    lock_entry->sle_state,
				    FSAL_OP_UNLOCK,
				    lock_entry->sle_owner,
				    &lock_entry->sle_lock,
				    NULL, /* no conflict expected */
				    NULL,
				    false);

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
	grant_blocked_locks(obj->state_hdl);

	/* In case all locks have wound up free,
	 * we must release the object reference.
	 */
	release = glist_empty(&obj->state_hdl->file.lock_list);

	STATELOCK_unlock(obj);

	if (release)
		obj->obj_ops->put_ref(obj);


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
static inline const char *fsal_lock_op_str(fsal_lock_op_t op)
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
 * @brief Perform a lock operation
 *
 * We do state management and call down to the FSAL as appropriate, so
 * that the caller has a single entry point.
 *
 * @note The st_lock MUST be held
 *
 * @param[in]  obj      File on which to operate
 * @param[in]  state    state_t associated with lock if any
 * @param[in]  lock_op  Operation to perform
 * @param[in]  owner    Lock operation
 * @param[in]  lock     Lock description
 * @param[out] holder   Owner of conflicting lock
 * @param[out] conflict Description of conflicting lock
 * @param[in]  overlap  Hint that lock overlaps
 *
 * @return State status.
 */
state_status_t do_lock_op(struct fsal_obj_handle *obj,
			  state_t *state,
			  fsal_lock_op_t lock_op,
			  state_owner_t *owner,
			  fsal_lock_param_t *lock,
			  state_owner_t **holder,
			  fsal_lock_param_t *conflict,
			  bool overlap)
{
	fsal_status_t fsal_status;
	state_status_t status = STATE_SUCCESS;
	fsal_lock_param_t conflicting_lock;
	struct fsal_export *fsal_export = op_ctx->fsal_export;
	fsal_lock_op_t fsal_lock_op = lock_op;
	struct gsh_client *saved_client = op_ctx->client;
	struct gsh_client *owner_client = NULL;
#ifdef _USE_NLM
	struct state_nlm_client_t *nlm_client;
#endif
	struct nfs_client_id_t *nfs4_clientid;

	lock->lock_sle_type = FSAL_POSIX_LOCK;

	/* Quick exit if:
	 * Locks are not supported by FSAL
	 * Async blocking locks are not supported and this is a cancel
	 * Lock owners are not supported and hint tells us that lock fully
	 *   overlaps a lock we already have (no need to make another FSAL
	 *   call in that case)
	 *
	 * We do NOT need to quick exit if async blocking locks are not
	 * supported and there is an overlap because we won't get here if
	 * the overlap includes a write lock (which would cause a block).
	 */
	LogFullDebug(COMPONENT_STATE,
		     "Reasons to quick exit fso_lock_support=%s fso_lock_support_async_block=%s overlap=%s",
		     fsal_export->exp_ops.fs_supports(
				fsal_export, fso_lock_support)
				? "yes" : "no",
		     fsal_export->exp_ops.fs_supports(
				fsal_export, fso_lock_support_async_block)
				? "yes" : "no",
		     overlap ? "yes" : "no");
	if (!fsal_export->exp_ops.fs_supports(fsal_export, fso_lock_support)
	    || (!fsal_export->exp_ops.fs_supports(fsal_export,
						  fso_lock_support_async_block)
	    && lock_op == FSAL_OP_CANCEL))
		return STATE_SUCCESS;

	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, fsal_lock_op_str(lock_op),
		obj, owner, lock);

	memset(&conflicting_lock, 0, sizeof(conflicting_lock));

	if (lock_op == FSAL_OP_LOCKB &&
	    !fsal_export->exp_ops.fs_supports(fsal_export,
					      fso_lock_support_async_block)) {
		fsal_lock_op = FSAL_OP_LOCK;
	}

	/* Fetch the gsh_client associated with the lock owner if available */
	switch (owner->so_type) {
	case STATE_LOCK_OWNER_UNKNOWN:
		/* Should never get here. */
		break;

#ifdef _USE_NLM
	case STATE_LOCK_OWNER_NLM:
		nlm_client = owner->so_owner.so_nlm_owner.so_client;
		owner_client = nlm_client->slc_nsm_client->ssc_client;
		break;
#endif

#ifdef _USE_9P
	case STATE_LOCK_OWNER_9P:
		/* OOPS - 9P won't work with FSALs that need
		 * op_ctx->client because it doesn't set it...
		 * They will crash soon enough...
		 */
		LogDebug(COMPONENT_STATE,
			 "9P doesn't set op_ctx->client...");
		break;
#endif

	case STATE_OPEN_OWNER_NFSV4:
	case STATE_LOCK_OWNER_NFSV4:
	case STATE_CLIENTID_OWNER_NFSV4:
		nfs4_clientid = owner->so_owner.so_nfs4_owner.so_clientrec;
		owner_client = nfs4_clientid->gsh_client;
		break;
	}

	/* If the owner gsh_client doesn't match the op_ctx and is not NULL
	 * then save the op_ctx->client and switch to owner_client.
	 */
	if (owner_client != op_ctx->client && owner_client != NULL)
		op_ctx->client = owner_client;

	/* Perform this lock operation using the support_ex lock op. */
	fsal_status = obj->obj_ops->lock_op2(obj, state, owner,
					    fsal_lock_op, lock,
					    &conflicting_lock);

	op_ctx->client = saved_client;

	status = state_error_convert(fsal_status);

	LogFullDebug(COMPONENT_STATE, "FSAL_lock_op returned %s",
		     state_err_str(status));

	if (status == STATE_LOCK_BLOCKED
	    && fsal_lock_op != FSAL_OP_LOCKB) {
		/* This is an unexpected return code,
		 * make sure caller reports an error
		 */
		LogMajor(COMPONENT_STATE,
			 "FSAL returned unexpected STATE_LOCK_BLOCKED result");
		status = STATE_FSAL_ERROR;
	} else if (status == STATE_LOCK_CONFLICT
		   && lock_op == FSAL_OP_LOCKB) {
		/* This must be a non-async blocking lock that was
		 * blocked, where we actually made a non-blocking
		 * call. In that case, actually return
		 * STATE_LOCK_BLOCKED.
		 */
		status = STATE_LOCK_BLOCKED;
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
 * @param[in]  obj      File to test
 * @param[in]  state    Optional state_t to relate this test to a fsal_fd
 * @param[in]  owner    Lock owner making the test
 * @param[in]  lock     Lock description
 * @param[out] holder   Owner that holds conflicting lock
 * @param[out] conflict Description of conflicting lock
 *
 * @return State status.
 */
state_status_t state_test(struct fsal_obj_handle *obj,
			  state_t *state,
			  state_owner_t *owner,
			  fsal_lock_param_t *lock, state_owner_t **holder,
			  fsal_lock_param_t *conflict)
{
	state_lock_entry_t *found_entry;
	state_status_t status = 0;

	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, "TEST", obj, owner, lock);

	STATELOCK_lock(obj);

	found_entry = get_overlapping_entry(obj->state_hdl, owner, lock);

	if (found_entry != NULL) {
		/* found a conflicting lock, return it */
		LogEntry("Found conflict", found_entry);
		copy_conflict(found_entry, holder, conflict);
		status = STATE_LOCK_CONFLICT;
	} else {
		/* Prepare to make call to FSAL for this lock */
		status = do_lock_op(obj, state, FSAL_OP_LOCKT, owner,
				    lock, holder, conflict, false);

		switch (status) {
		case STATE_SUCCESS:
			LogFullDebug(COMPONENT_STATE, "Lock success");
			break;
		case STATE_LOCK_CONFLICT:
			LogLock(COMPONENT_STATE, NIV_FULL_DEBUG,
				"Conflict from FSAL",
				obj, *holder, conflict);
			break;
		case STATE_ESTALE:
			LogDebug(COMPONENT_STATE,
				 "Got error %s from FSAL lock operation",
				 state_err_str(status));
			break;
		default:
			LogMajor(COMPONENT_STATE,
				 "Got error from FSAL lock operation, error=%s",
				 state_err_str(status));
			break;
		}
	}

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
		LogList("Lock List", obj, &obj->state_hdl->file.lock_list);

	STATELOCK_unlock(obj);

	return status;
}

/**
 * @brief Attempt to acquire a lock
 *
 * Must hold the st_lock
 *
 * @param[in]     obj        File to lock
 * @param[in]     owner      Lock owner
 * @param[in]     state      Associated state for the lock
 * @param[in]     blocking   Blocking type
 * @param[in,out] bdata      Blocking lock data
 * @param[in]     lock       Lock description
 * @param[out]    holder     Holder of conflicting lock
 * @param[out]    conflict   Conflicting lock description
 *
 * @return State status.
 */
state_status_t state_lock(struct fsal_obj_handle *obj,
			  state_owner_t *owner,
			  state_t *state,
			  state_blocking_t blocking,
			  state_block_data_t **bdata,
			  fsal_lock_param_t *lock,
			  state_owner_t **holder,
			  fsal_lock_param_t *conflict)
{
	bool allow = true, overlap = false;
	struct glist_head *glist;
	state_lock_entry_t *found_entry;
	uint64_t found_entry_end;
	uint64_t range_end = lock_end(lock);
	struct fsal_export *fsal_export = op_ctx->fsal_export;
	fsal_lock_op_t lock_op;
	state_status_t status = 0;
	bool async;
	state_block_data_t *block_data;

	if (blocking != STATE_NON_BLOCKING) {
		/* First search for a blocked request. Client can ignore the
		 * blocked request and keep sending us new lock request again
		 * and again. So if we have a mapping blocked request return
		 * that
		 */
		glist_for_each(glist, &obj->state_hdl->file.lock_list) {
			found_entry =
			    glist_entry(glist, state_lock_entry_t, sle_list);

			if (different_owners(found_entry->sle_owner, owner))
				continue;

			/* Need to reject lock request if this lock owner
			 * already has a lock on this file via a different
			 * export.
			 */
			if (found_entry->sle_export != op_ctx->ctx_export) {
				struct tmp_export_paths tmp = {NULL, NULL};

				tmp_get_exp_paths(&tmp,
						  found_entry->sle_export);

				LogEvent(COMPONENT_STATE,
					 "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
					 found_entry->sle_export->export_id,
					 op_ctx_tmp_export_path(op_ctx, &tmp),
					 op_ctx->ctx_export->export_id,
					 op_ctx_export_path(op_ctx));
				LogEntry(
					"Found lock entry belonging to another export",
					found_entry);

				tmp_put_exp_paths(&tmp);

				status = STATE_INVALID_ARGUMENT;
				return status;
			}

			if (found_entry->sle_blocked != blocking)
				continue;

			if (different_lock(&found_entry->sle_lock, lock))
				continue;

			/* We have matched all atribute of the existing lock.
			 * Just return with blocked status. Client may be
			 * polling.
			 */
			LogEntry("Found blocked", found_entry);
			status = STATE_LOCK_BLOCKED;
			return status;
		}
	}

	glist_for_each(glist, &obj->state_hdl->file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);
		/* Need to reject lock request if this lock owner already has
		 * a lock on this file via a different export.
		 */
		if (found_entry->sle_export != op_ctx->ctx_export
		    && !different_owners(found_entry->sle_owner, owner)) {
			struct tmp_export_paths tmp = {NULL, NULL};

			tmp_get_exp_paths(&tmp, found_entry->sle_export);

			LogEvent(COMPONENT_STATE,
				 "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
				 found_entry->sle_export->export_id,
				 op_ctx_tmp_export_path(op_ctx, &tmp),
				 op_ctx->ctx_export->export_id,
				 op_ctx_export_path(op_ctx));

			LogEntry("Found lock entry belonging to another export",
				 found_entry);

			tmp_put_exp_paths(&tmp);

			status = STATE_INVALID_ARGUMENT;
			return status;
		}

		/* Don't skip blocked locks for fairness */
		found_entry_end = lock_end(&found_entry->sle_lock);

		if (!(lock->lock_reclaim)
		    && (found_entry_end >= lock->lock_start)
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
				LogList("Locks", obj,
					&obj->state_hdl->file.lock_list);
				if (blocking != STATE_NLM_BLOCKING) {
					copy_conflict(found_entry, holder,
						      conflict);
				}
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
						obj->state_hdl, found_entry);
				}

				LogEntry("Found existing", found_entry);

				status = STATE_SUCCESS;
				return status;
			}

			/* Found a compatible lock with a different lock owner
			 * that fully overlaps, set hint.
			 */
			LogEntry("Found overlapping", found_entry);
			overlap = true;
		}
	}

	/* Decide how to proceed */
	if (blocking == STATE_NLM_BLOCKING) {
		/* do_lock_op will handle FSAL_OP_LOCKB for those FSALs that
		 * do not support async blocking locks. It will make a
		 * non-blocking call in that case, and it will return
		 * STATE_LOCK_BLOCKED if the lock was not granted.
		 */
		lock_op = FSAL_OP_LOCKB;
	} else if (allow) {
		/* No conflict found in Ganesha, and this is not a blocking
		 * request.
		 */
		lock_op = FSAL_OP_LOCK;
	} else {
		/* This is not a blocking lock and has a conflict.
		 * Return it.
		 */
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
	found_entry = create_state_lock_entry(obj,
					op_ctx->ctx_export,
					STATE_NON_BLOCKING,
					owner,
					state,
					lock);

	/* Check for async blocking lock support. */
	async = fsal_export->exp_ops.fs_supports(fsal_export,
						 fso_lock_support_async_block);

	/* If no conflict in lock list, or FSAL supports async blocking locks,
	 * make FSAL call. Don't ask for conflict if we know about a conflict.
	 */
	if (allow || async) {
		/* Prepare to make call to FSAL for this lock */
		status = do_lock_op(obj,
				    state,
				    lock_op,
				    owner,
				    lock,
				    allow ? holder : NULL,
				    allow ? conflict : NULL,
				    overlap);
	} else {
		/* FSAL does not support async blocking locks and we have
		 * a blocking lock within Ganesha, no need to make an
		 * FSAL call that will just return denied.
		 */
		status = STATE_LOCK_BLOCKED;
	}

	if (status == STATE_SUCCESS) {
		/* Merge any touching or overlapping locks into this one */
		LogEntry("FSAL lock acquired, merging locks for",
			 found_entry);

		if (glist_empty(&obj->state_hdl->file.lock_list)) {
			/* List was empty, get ref for list. Check before
			 * mergining, because that can remove the last entry
			 * from the list if we are merging with it.
			 */
			obj->obj_ops->get_ref(obj);
		}

		merge_lock_entry(obj->state_hdl, found_entry);

		/* Insert entry into lock list */
		LogEntry("New lock", found_entry);

		glist_add_tail(&obj->state_hdl->file.lock_list,
			       &found_entry->sle_list);

		/* A lock downgrade could unblock blocked locks */
		grant_blocked_locks(obj->state_hdl);
	} else if (status == STATE_LOCK_CONFLICT) {
		LogEntry("Conflict in FSAL for", found_entry);

		/* Discard lock entry */
		remove_from_locklist(found_entry);
	} else if (status == STATE_LOCK_BLOCKED) {
		/* We are going to use the bdata, set it to NULL so that
		 * the caller doesn't free it!
		 */
		block_data = *bdata;
		*bdata = NULL;

		/* Mark entry as blocking and attach block_data */
		found_entry->sle_block_data = block_data;
		found_entry->sle_blocked = blocking;
		block_data->sbd_lock_entry = found_entry;
		if (async) {
			/* Allow FSAL to signal when lock is granted or
			 * available for retry.
			 */
			block_data->sbd_block_type = STATE_BLOCK_ASYNC;
		} else if (allow) {
			/* Actively poll for the lock. */
			block_data->sbd_block_type = STATE_BLOCK_POLL;
		} else {
			/* Ganesha will attempt to grant the lock when
			 * a conflicting lock is released.
			 */
			block_data->sbd_block_type = STATE_BLOCK_INTERNAL;
		}

		/* Insert entry into lock list */
		LogEntry("FSAL block for", found_entry);

		glist_add_tail(&obj->state_hdl->file.lock_list,
			       &found_entry->sle_list);

		PTHREAD_MUTEX_lock(&blocked_locks_mutex);

		glist_add_tail(&state_blocked_locks, &block_data->sbd_list);

		PTHREAD_MUTEX_unlock(&blocked_locks_mutex);
	} else {
		if (status == STATE_ESTALE)
			LogDebug(COMPONENT_STATE,
				 "Unable to lock FSAL, error=%s",
				 state_err_str(status));
		else
			LogMajor(COMPONENT_STATE,
				 "Unable to lock FSAL, error=%s",
				 state_err_str(status));

		/* Discard lock entry */
		remove_from_locklist(found_entry);
	}

	return status;
}

/**
 * @brief Release a lock
 *
 * @param[in] obj      File to unlock
 * @param[in] state         Associated state_t (if any)
 * @param[in] owner         Owner of lock
 * @param[in] state_applies Indicator if nsm_state is relevant
 * @param[in] nsm_state     NSM state number
 * @param[in] lock          Lock description
 */
state_status_t state_unlock(struct fsal_obj_handle *obj,
			    state_t *state,
			    state_owner_t *owner,
			    bool state_applies,
			    int32_t nsm_state,
			    fsal_lock_param_t *lock)
{
	bool empty = false;
	bool removed = false;
	state_status_t status = 0;

	if (obj->type != REGULAR_FILE) {
		LogLock(COMPONENT_STATE, NIV_DEBUG, "Bad Unlock", obj, owner,
			lock);
		return STATE_BAD_TYPE;
	}

	STATELOCK_lock(obj);

	/* If lock list is empty, there really isn't any work for us to do. */
	if (glist_empty(&obj->state_hdl->file.lock_list)) {
		LogDebug(COMPONENT_STATE,
			 "Unlock success on file with no locks");

		status = STATE_SUCCESS;
		goto out_unlock;
	}

	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");
	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, "Subtracting", obj, owner,
		lock);
	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");

	/* First cancel any blocking locks that might
	 * overlap the unlocked range.
	 */
	cancel_blocked_locks_range(obj->state_hdl, owner, state_applies,
				   nsm_state, lock);

	/* Release the lock from lock list for entry */
	status = subtract_lock_from_list(owner, state_applies, nsm_state, lock,
					 &removed,
					 &obj->state_hdl->file.lock_list);

	/* If the lock list has become zero; decrement the pin ref count pt
	 * placed. Do this here just in case subtract_lock_from_list has made
	 * list empty even if it failed.
	 */
	if (glist_empty(&obj->state_hdl->file.lock_list))
		obj->obj_ops->put_ref(obj);

	if (status != STATE_SUCCESS) {
		/* The unlock has not taken affect (other than canceling any
		 * blocking locks.
		 */
		LogMajor(COMPONENT_STATE,
			 "Unable to remove lock from list for unlock, error=%s",
			 state_err_str(status));

		goto out_unlock;
	}

	/* Unlocking the entire region will remove any FSAL locks we held,
	 * whether from fully granted locks, or from blocking locks that were
	 * in the process of being granted.
	 */
	status = do_lock_op(obj,
			    state,
			    FSAL_OP_UNLOCK,
			    owner,
			    lock,
			    NULL, /* no conflict expected */
			    NULL,
			    false);

	if (status != STATE_SUCCESS)
		LogMajor(COMPONENT_STATE, "Unable to unlock FSAL, error=%s",
			 state_err_str(status));

	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");
	LogLock(COMPONENT_STATE, NIV_FULL_DEBUG, "Done", obj, owner, lock);
	LogFullDebug(COMPONENT_STATE,
		     "----------------------------------------------------------------------");

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS)
	    && lock->lock_start == 0 && lock->lock_length == 0)
		empty =
		    LogList("Lock List", obj, &obj->state_hdl->file.lock_list);

	grant_blocked_locks(obj->state_hdl);


	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS)
	    && lock->lock_start == 0 && lock->lock_length == 0 && empty)
		dump_all_locks("All locks (after unlock)");

 out_unlock:
	STATELOCK_unlock(obj);

	return status;
}

/**
 * @brief Cancel a blocking lock
 *
 * @param[in] obj    File on which to cancel the lock
 * @param[in] owner  Lock owner
 * @param[in] lock   Lock description
 *
 * @return State status.
 */
state_status_t state_cancel(struct fsal_obj_handle *obj,
			    state_owner_t *owner, fsal_lock_param_t *lock)
{
	struct glist_head *glist;
	state_lock_entry_t *found_entry;

	if (obj->type != REGULAR_FILE) {
		LogLock(COMPONENT_STATE, NIV_DEBUG,
			"Bad Cancel",
			obj, owner, lock);
		return STATE_BAD_TYPE;
	}

	STATELOCK_lock(obj);

	/* If lock list is empty, there really isn't any work for us to do. */
	if (glist_empty(&obj->state_hdl->file.lock_list)) {
		LogDebug(COMPONENT_STATE,
			 "Cancel success on file with no locks");

		goto out_unlock;
	}

	glist_for_each(glist, &obj->state_hdl->file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		if (different_owners(found_entry->sle_owner, owner))
			continue;

		/* Can not cancel a lock once it is granted */
		if (found_entry->sle_blocked == STATE_NON_BLOCKING)
			continue;

		if (different_lock(&found_entry->sle_lock, lock))
			continue;

		/* Cancel the blocked lock */
		cancel_blocked_lock(obj, found_entry);

		/* Check to see if we can grant any blocked locks. */
		grant_blocked_locks(obj->state_hdl);

		break;
	}

 out_unlock:
	STATELOCK_unlock(obj);

	return STATE_SUCCESS;
}

#ifdef _USE_NLM
/**
 * @brief Handle an SM_NOTIFY from NLM
 *
 * Also used to handle NLM_FREE_ALL
 *
 * @param[in] nsmclient     NSM client data
 * @param[in] state_applies Indicates if nsm_state is valid
 * @param[in] nsm_state     NSM state value
 *
 * @return State status.
 */
state_status_t state_nlm_notify(state_nsm_client_t *nsmclient,
				bool state_applies,
				int32_t nsm_state)
{
	state_owner_t *owner;
	state_lock_entry_t *found_entry;
	fsal_lock_param_t lock;
	struct fsal_obj_handle *obj;
	int errcnt = 0;
	struct glist_head newlocks;
	state_t *found_share;
	state_status_t status = 0;
	struct req_op_context op_context;
	struct gsh_export *export;
	state_t *state;

	/* Initialize a context */
	init_op_context_simple(&op_context, NULL, NULL);

	if (isFullDebug(COMPONENT_STATE)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_nsm_client(&dspbuf, nsmclient);

		LogFullDebug(COMPONENT_STATE, "Notify for %s", str);
	}

	glist_init(&newlocks);

	/* First remove byte range locks.
	 * Only accept so many errors before giving up.
	 */
	while (errcnt < STATE_ERR_MAX) {
		PTHREAD_MUTEX_lock(&nsmclient->ssc_mutex);

		/* We just need to find any file this client has locks on.
		 * We pick the first lock the client holds, and use it's file.
		 */
		found_entry = glist_first_entry(&nsmclient->ssc_lock_list,
						state_lock_entry_t,
						sle_client_locks);

		/* If we don't find any entries, then we are done. */
		if (found_entry == NULL) {
			PTHREAD_MUTEX_unlock(&nsmclient->ssc_mutex);
			break;
		}

		/* Remove from the client lock list */
		glist_del(&found_entry->sle_client_locks);

		/* If called as a result of SM_NOTIFY, we don't drop the
		 * locks belonging to the "current" NSM state counter passed
		 * in the SM_NOTIFTY. Otherwise, we drop all locks.
		 * We expect never to get an SM_NOTIFY from a client that uses
		 * non-monitored locks (it will send a FREE_ALL instead (which
		 * will not set the state_applies flag).
		 */
		if (state_applies &&
		    found_entry->sle_state->state_seqid == nsm_state) {
			/* This is a new lock acquired since the client
			 * rebooted, retain it.
			 *
			 * Note that although this entry is temporarily
			 * not on the ssc_lock_list, we don't need to hold
			 * an extra refrence to the entry since the list
			 * still effectively protected by the ssc_mutex.
			 * If something happens to remove this entry, it will
			 * actually be removed from the newlocks list, which
			 * in the end is correct. So we don't need to do
			 * anything special.
			 */
			/** @todo FSF: vulnerability - if a SECOND SM_NOTIFY
			 * comes in while this one is still being processed
			 * the locks put on the newlocks list would not be
			 * cleaned up. This list REALLY should be attached
			 * to the nsm_client in some way, or we should block
			 * handling of the 2nd SM_NOTIFY until this one is
			 * complete.
			 */
			LogEntry("Don't release new lock", found_entry);
			glist_add_tail(&newlocks,
				       &found_entry->sle_client_locks);
			PTHREAD_MUTEX_unlock(&nsmclient->ssc_mutex);
			continue;
		}

		LogEntry("Release client locks based on", found_entry);

		/* Extract the bits from the lock entry that we will need
		 * to proceed with the operation (file, owner, and
		 * export).
		 */
		obj = found_entry->sle_obj;
		owner = found_entry->sle_owner;
		export = found_entry->sle_export;
		state = found_entry->sle_state;

		/* Get a reference to the export while we still hold the
		 * ssc_mutex. This assures that the export definitely can
		 * not have had it's last refcount released. We will check
		 * later to see if the export is being removed.
		 */
		get_gsh_export_ref(export);
		set_op_context_export(export);

		/* Get a reference to the owner */
		inc_state_owner_ref(owner);

		/* Get a reference to the state_t */
		inc_state_t_ref(state);

		/* Move this entry to the end of the list
		 * (this will help if errors occur)
		 */
		glist_add_tail(&nsmclient->ssc_lock_list,
			       &found_entry->sle_client_locks);

		/* Now we are done with this specific entry, release the mutex.
		 */
		PTHREAD_MUTEX_unlock(&nsmclient->ssc_mutex);

		/* Make lock that covers the whole file.
		 * type doesn't matter for unlock
		 */
		lock.lock_type = FSAL_LOCK_R;
		lock.lock_start = 0;
		lock.lock_length = 0;

		if (export_ready(export)) {
			/* Remove all locks held by this NLM Client on
			 * the file.
			 */
			status = state_unlock(obj, state, owner, state_applies,
					      nsm_state, &lock);
		} else {
			/* The export is being removed, we didn't bother
			 * calling state_unlock() because export cleanup
			 * will remove all the state. This is assured by
			 * the call to put_gsh_export from
			 * clear_op_context_export. Pretend succes.
			 */
			status = STATE_SUCCESS;
		}

		/* Release the refcounts we took above. */
		dec_state_owner_ref(owner);
		obj->obj_ops->put_ref(obj);
		clear_op_context_export();

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next lock,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogFullDebug(COMPONENT_STATE,
				     "state_unlock returned %s",
				     state_err_str(status));
		}
	}

	/* Now remove NLM_SHARE reservations.
	 * Only accept so many errors before giving up.
	 * We always drop all NLM_SHARE reservations since they are
	 * non-monitored.
	 */
	while (errcnt < STATE_ERR_MAX) {
		PTHREAD_MUTEX_lock(&nsmclient->ssc_mutex);

		/* We just need to find any file this client has locks on.
		 * We pick the first lock the client holds, and use it's file.
		 */
		found_share =
			glist_first_entry(&nsmclient->ssc_share_list,
					  state_t,
					  state_data.nlm_share.share_perclient);

		/* If we don't find any entries, then we are done. */
		if (found_share == NULL) {
			PTHREAD_MUTEX_unlock(&nsmclient->ssc_mutex);
			break;
		}

		/* Extract the bits from the lock entry that we will need
		 * to proceed with the operation (object, owner, and
		 * export).
		 */
		obj = get_state_obj_ref(found_share);

		if (obj == NULL) {
			LogDebug(COMPONENT_STATE,
				 "Entry for state is stale");
			PTHREAD_MUTEX_unlock(&nsmclient->ssc_mutex);
			break;
		}

		owner = found_share->state_owner;
		export = found_share->state_export;

		/* Get a reference to the export while we still hold the
		 * ssc_mutex. This assures that the export definitely can
		 * not have had it's last refcount released. We will check
		 * later to see if the export is being removed.
		 */
		get_gsh_export_ref(export);
		set_op_context_export(export);

		/* Get a reference to the owner */
		inc_state_owner_ref(owner);

		/* Get a reference to the state_t */
		inc_state_t_ref(found_share);

		/* Move this entry to the end of the list
		 * (this will help if errors occur)
		 */
		glist_move_tail(&nsmclient->ssc_share_list,
			&found_share->state_data.nlm_share.share_perclient);

		PTHREAD_MUTEX_unlock(&nsmclient->ssc_mutex);

		if (export_ready(export)) {
			/* Remove all shares held by this NSM Client and
			 * Owner on the file (on all exports)
			 */
			status = state_nlm_share(obj,
						 OPEN4_SHARE_ACCESS_ALL,
						 OPEN4_SHARE_DENY_ALL,
						 owner,
						 found_share,
						 false,
						 true);
		} else {
			/* The export is being removed, we didn't bother
			 * calling state_unlock() because export cleanup
			 * will remove all the state. This is assured by
			 * the call to put_gsh_export from
			 * clear_op_context_export. Pretend succes.
			 */
			status = STATE_SUCCESS;
		}

		/* Release the refcounts we took above. */
		dec_state_owner_ref(owner);
		obj->obj_ops->put_ref(obj);
		dec_state_t_ref(found_share);
		clear_op_context_export();

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next share,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogFullDebug(COMPONENT_STATE,
				     "state_nlm_unshare returned %s",
				     state_err_str(status));
		}
	}

	if (errcnt == STATE_ERR_MAX) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_nsm_client(&dspbuf, nsmclient);

		LogFatal(COMPONENT_STATE,
			 "Could not complete NLM notify for %s",
			 str);
	}

	/* Put locks from current client incarnation onto end of list.
	 *
	 * Note that it's possible one or more of the locks we originally
	 * put on the newlocks list have been removed. This is actually
	 * just fine. Since everything is protected by the ssc_mutex,
	 * even the fact that the newlocks list head is a stack local
	 * variable is local will actually work just fine.
	 */
	PTHREAD_MUTEX_lock(&nsmclient->ssc_mutex);
	glist_add_list_tail(&nsmclient->ssc_lock_list, &newlocks);
	PTHREAD_MUTEX_unlock(&nsmclient->ssc_mutex);
	LogFullDebug(COMPONENT_STATE, "DONE");

	release_op_context();
	return status;
}
#endif /* _USE_NLM */

/**
 * @brief Release all locks held by an NFS v4 lock owner
 *
 * @param[in] owner Lock owner
 *
 */
void state_nfs4_owner_unlock_all(state_owner_t *owner)
{
	fsal_lock_param_t lock;
	struct fsal_obj_handle *obj;
	int errcnt = 0;
	state_status_t status = 0;
	struct saved_export_context saved;
	struct gsh_export *export;
	state_t *state;
	bool ok;

	/* Only accept so many errors before giving up. */
	while (errcnt < STATE_ERR_MAX) {
		PTHREAD_MUTEX_lock(&owner->so_mutex);

		/* We just need to find any file this owner has locks on.
		 * We pick the first lock the owner holds, and use it's file.
		 */
		state = glist_first_entry(
				&owner->so_owner.so_nfs4_owner.so_state_list,
				state_t,
				state_owner_list);

		/* If we don't find any entries, then we are done. */
		if (state == NULL) {
			PTHREAD_MUTEX_unlock(&owner->so_mutex);
			break;
		}

		inc_state_t_ref(state);

		/* Move this state to the end of the list
		 * (this will help if errors occur)
		 */
		glist_move_tail(&owner->so_owner.so_nfs4_owner.so_state_list,
				&state->state_owner_list);

		/* Get references to the obj and export */
		ok = get_state_obj_export_owner_refs(state, &obj, &export,
						     NULL);

		PTHREAD_MUTEX_unlock(&owner->so_mutex);

		if (!ok) {
			/* Entry and/or export is dying, skip this state,
			 * it will be cleaned up soon enough.
			 */
			continue;
		}

		/* Set up the op_context with the proper export */
		save_op_context_export_and_set_export(&saved, export);

		/* Make lock that covers the whole file.
		 * type doesn't matter for unlock
		 */
		lock.lock_type = FSAL_LOCK_R;
		lock.lock_start = 0;
		lock.lock_length = 0;

		/* Remove all locks held by this owner on the file */
		status = state_unlock(obj, state, owner, false, 0, &lock);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next lock,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogCrit(COMPONENT_STATE, "state_unlock failed %s",
				state_err_str(status));
			errcnt++;
		} else if (status == STATE_SUCCESS) {
			/* Delete the state_t */
			state_del(state);
		}

		dec_state_t_ref(state);

		/* Release the obj ref and export ref. */
		obj->obj_ops->put_ref(obj);
		restore_op_context_export(&saved);
	}

	if (errcnt == STATE_ERR_MAX) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_owner(&dspbuf, owner);

		LogFatal(COMPONENT_STATE,
			 "Could not complete cleanup of lock state for lock owner %s",
			 str);
	}
}

/**
 * @brief Release all locks held on an export
 *
 */

void state_export_unlock_all(void)
{
	state_lock_entry_t *found_entry;
	fsal_lock_param_t lock;
	struct fsal_obj_handle *obj;
	int errcnt = 0;
	state_status_t status = 0;
	state_owner_t *owner;
	state_t *state;

	/* Only accept so many errors before giving up. */
	while (errcnt < STATE_ERR_MAX) {
		PTHREAD_RWLOCK_wrlock(&op_ctx->ctx_export->lock);

		/* We just need to find any file this owner has locks on.
		 * We pick the first lock the owner holds, and use it's file.
		 */
		found_entry = glist_first_entry(
			&op_ctx->ctx_export->exp_lock_list,
			state_lock_entry_t,
			sle_export_locks);

		/* If we don't find any entries, then we are done. */
		if (found_entry == NULL) {
			PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);
			break;
		}

		/* Extract the bits from the lock entry that we will need
		 * to proceed with the operation (obj, owner, and
		 * export).
		 */
		obj = found_entry->sle_obj;
		owner = found_entry->sle_owner;
		state = found_entry->sle_state;

		/* take a reference on the state_t */
		inc_state_t_ref(state);

		/* Get a reference to the file object while we still hold
		 * the ssc_mutex (since we hold this mutex, any other function
		 * that might be cleaning up this lock CAN NOT have released
		 * the last LRU reference, thus it is safe to grab another. */
		obj->obj_ops->get_ref(obj);

		/* Get a reference to the owner */
		inc_state_owner_ref(owner);

		/* Move this entry to the end of the list
		 * (this will help if errors occur)
		 */
		glist_move_tail(&op_ctx->ctx_export->exp_lock_list,
				&found_entry->sle_export_locks);

		/* Now we are done with this specific entry, release the lock.
		 */
		PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

		/* Make lock that covers the whole file.
		 * type doesn't matter for unlock
		 */
		lock.lock_type = FSAL_LOCK_R;
		lock.lock_start = 0;
		lock.lock_length = 0;

		/* Remove all locks held by this NLM Client on
		 * the file.
		 */
		status = state_unlock(obj, state, owner, false, 0, &lock);

		/* call state_del regardless of status as we're killing
		 * the export
		 */
		if (owner->so_type == STATE_LOCK_OWNER_NFSV4)
			state_del(state);

		/* Release the refcounts we took above. */
		dec_state_t_ref(state);
		dec_state_owner_ref(owner);
		obj->obj_ops->put_ref(obj);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next lock,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogDebug(COMPONENT_STATE, "state_unlock failed %s",
				 state_err_str(status));
		}
	}

	if (errcnt == STATE_ERR_MAX) {
		LogFatal(COMPONENT_STATE,
			 "Could not complete cleanup of locks for %s",
			 op_ctx_export_path(op_ctx));
	}
}

/**
 * @brief Poll any blocked locks of type STATE_BLOCK_POLL
 *
 * @param[in] ctx Fridge Thread Context
 *
 */

void blocked_lock_polling(struct fridgethr_context *ctx)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist;
	state_block_data_t *pblock;

	SetNameFunction("lk_poll");

	PTHREAD_MUTEX_lock(&blocked_locks_mutex);

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
		LogBlockedList("Blocked Lock List",
			       NULL, &state_blocked_locks);

	glist_for_each(glist, &state_blocked_locks) {
		pblock = glist_entry(glist, state_block_data_t, sbd_list);

		found_entry = pblock->sbd_lock_entry;

		/* Check if got an entry */
		if (found_entry == NULL)
			continue;

		/* Check if right type */
		if (pblock->sbd_block_type != STATE_BLOCK_POLL)
			continue;

		/* Schedule async processing, leave the lock on the blocked
		 * lock list since we might not succeed in granting this lock.
		 */
		pblock->sbd_grant_type = STATE_GRANT_POLL;

		if (state_block_schedule(pblock) != STATE_SUCCESS) {
			LogMajor(COMPONENT_STATE,
				 "Unable to schedule lock notification.");
		}

		LogEntry("Blocked Lock found", found_entry);
	}			/* glist_for_each_safe */

	PTHREAD_MUTEX_unlock(&blocked_locks_mutex);
}

/**
 * @brief Find a lock and add to grant list
 *
 * @param[in] obj      File to search
 * @param[in] owner      Lock owner
 * @param[in] lock       Lock description
 * @param[in] grant_type Grant type
 */
static void find_blocked_lock_upcall(struct fsal_obj_handle *obj, void *owner,
			      fsal_lock_param_t *lock,
			      state_grant_type_t grant_type)
{
	state_lock_entry_t *found_entry;
	struct glist_head *glist;
	state_block_data_t *pblock;

	PTHREAD_MUTEX_lock(&blocked_locks_mutex);

	glist_for_each(glist, &state_blocked_locks) {
		pblock = glist_entry(glist, state_block_data_t, sbd_list);

		found_entry = pblock->sbd_lock_entry;

		/* Check if got an entry */
		if (found_entry == NULL)
			continue;

		/* Check if for same file */
		if (found_entry->sle_obj != obj)
			continue;

		/* Check if for same owner */
		if (found_entry->sle_owner != owner)
			continue;

		/* Check if same lock */
		if (different_lock(&found_entry->sle_lock, lock))
			continue;

		/* Schedule async processing, leave the lock on the blocked
		 * lock list for now (if we get multiple upcalls because our
		 * async processing is slow for some reason, we don't want to
		 * not find this lock entry and hit the LogFatal below...
		 */
		pblock->sbd_grant_type = grant_type;
		if (state_block_schedule(pblock) != STATE_SUCCESS) {
			LogMajor(COMPONENT_STATE,
				 "Unable to schedule lock notification.");
		}

		LogEntry("Blocked Lock found", found_entry);

		PTHREAD_MUTEX_unlock(&blocked_locks_mutex);

		return;
	}			/* glist_for_each_safe */

	if (isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
		LogBlockedList("Blocked Lock List",
			       NULL, &state_blocked_locks);

	PTHREAD_MUTEX_unlock(&blocked_locks_mutex);

	/* We must be out of sync with FSAL, this is fatal */
	LogLockDesc(COMPONENT_STATE, NIV_MAJ, "Blocked Lock Not Found for",
		    obj, owner, lock);
	LogFatal(COMPONENT_STATE, "Locks out of sync with FSAL");
}

/**
 * @brief Handle upcall for granted lock
 *
 * @param[in] obj File on which lock is granted
 * @param[in] owner Lock owner
 * @param[in] lock  Lock description
 */
void grant_blocked_lock_upcall(struct fsal_obj_handle *obj, void *owner,
			       fsal_lock_param_t *lock)
{
	LogLockDesc(COMPONENT_STATE, NIV_DEBUG, "Grant Upcall for", obj,
		    owner, lock);

	find_blocked_lock_upcall(obj, owner, lock, STATE_GRANT_FSAL);
}

/**
 * @brief Handle upcall for available lock
 *
 * @param[in] obj File on which lock has become available
 * @param[in] owner Lock owner
 * @param[in] lock  Lock description
 */
void available_blocked_lock_upcall(struct fsal_obj_handle *obj, void *owner,
				   fsal_lock_param_t *lock)
{
	LogLockDesc(COMPONENT_STATE, NIV_DEBUG, "Available Upcall for", obj,
		    owner, lock);

	find_blocked_lock_upcall(obj, owner, lock,
				 STATE_GRANT_FSAL_AVAILABLE);
}

/**
 * @brief Free all locks on a file
 *
 * @param[in] obj File to free
 * @return true if locks were removed, false if list was empty
 */
bool state_lock_wipe(struct state_hdl *hstate)
{
	if (glist_empty(&hstate->file.lock_list))
		return false;

	free_list(&hstate->file.lock_list);
	return true;
}

void cancel_all_nlm_blocked(void)
{
	state_lock_entry_t *found_entry;
	state_block_data_t *pblock;
	struct req_op_context op_context;

	/* Initialize context */
	init_op_context(&op_context, NULL, NULL, NULL, 0, 0, NFS_REQUEST);

	LogDebug(COMPONENT_STATE, "Cancel all blocked locks");

	PTHREAD_MUTEX_lock(&blocked_locks_mutex);

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

		PTHREAD_MUTEX_unlock(&blocked_locks_mutex);

		get_gsh_export_ref(found_entry->sle_export);
		set_op_context_export(found_entry->sle_export);

		/** @todo also look at the LRU ref for pentry */

		LogEntry("Blocked Lock found", found_entry);

		cancel_blocked_lock(found_entry->sle_obj, found_entry);

		gsh_free(pblock->sbd_blocked_cookie);
		gsh_free(found_entry->sle_block_data);
		found_entry->sle_block_data = NULL;

		LogEntry("Canceled Lock", found_entry);

		lock_entry_dec_ref(found_entry);

		clear_op_context_export();

		PTHREAD_MUTEX_lock(&blocked_locks_mutex);

		/* Get next item off list */
		pblock = glist_first_entry(&state_blocked_locks,
					   state_block_data_t,
					   sbd_list);
	}

out:

	PTHREAD_MUTEX_unlock(&blocked_locks_mutex);
	release_op_context();
}

/**
 * @brief Initialize locking
 *
 * @return State status.
 */
state_status_t state_lock_init(void)
{
	state_status_t status = STATE_SUCCESS;

	ht_lock_cookies = hashtable_init(&cookie_param);
	if (ht_lock_cookies == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init NLM Client cache");
		status = STATE_INIT_ENTRY_FAILED;
		return status;
	}

	status = state_async_init();

	state_owner_pool =
		pool_basic_init("NFSv4 state owners", sizeof(state_owner_t));

	return status;
}

/** @} */
