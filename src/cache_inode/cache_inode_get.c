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
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file    cache_inode_get.c
 * @brief   Get and eventually cache an entry.
 *
 * Get and eventually cache an entry.
 *
 *
 */
#include "config.h"
#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_hash.h"
#include "cache_inode_lru.h"
#include "nfs_core.h"		/* exportlist_t */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "export_mgr.h"

/**
 *
 * @brief Gets an entry by using its fsdata as a key and caches it if needed.
 *
 * Gets an entry by using its fsdata as a key and caches it if needed.
 *
 * If a cache entry is returned, its refcount is incremented by one.
 *
 * @param[in]  fsdata     File system data
 * @param[in]  req_ctx    Request context (user creds, client address etc)
 * @param[out] entry      The entry
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */
cache_inode_status_t
cache_inode_get(cache_inode_fsal_data_t *fsdata,
		const struct req_op_context *req_ctx,
		cache_entry_t **entry)
{
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_export *exp_hdl = NULL;
	struct fsal_obj_handle *new_hdl;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cih_latch_t latch;

	/* Do lookup */
	*entry =
	    cih_get_by_fh_latched(&fsdata->fh_desc, &latch,
				  CIH_GET_RLOCK | CIH_GET_UNLOCK_ON_MISS,
				  __func__, __LINE__);
	if (*entry) {
		/* take an extra reference within the critical section */
		cache_inode_lru_ref(*entry, LRU_REQ_INITIAL);
		cih_latch_rele(&latch);

		/* This is the replacement for cache_inode_renew_entry.
		   Rather than calling that function at the start of
		   every cache_inode call with the inode locked, we call
		   cache_inode_check trust to perform 'heavyweight'
		   (timed expiration of cached attributes, getattr-based
		   directory trust) checks the first time after getting
		   an inode.  It does all of the checks read-locked and
		   only acquires a write lock if there's something
		   requiring a change.

		   There is a second light-weight check done before use
		   of cached data that checks whether the bits saying
		   that inode attributes or inode content are trustworthy
		   have been cleared by, for example, FSAL_CB.

		   To summarize, the current implementation is that
		   policy-based trust of validity is checked once per
		   logical series of operations at cache_inode_get, and
		   asynchronous trust is checked with use (when the
		   attributes are locked for reading, for example.) */

		status = cache_inode_lock_trust_attrs(*entry, req_ctx, false);
		if (status != CACHE_INODE_SUCCESS) {
			cache_inode_put(*entry);
			*entry = NULL;
		} else
			PTHREAD_RWLOCK_unlock(&((*entry)->attr_lock));

		return status;
	}

	/* Cache miss, allocate a new entry */
	exp_hdl = fsdata->export;
	fsal_status =
	    exp_hdl->ops->create_handle(exp_hdl, req_ctx, &fsdata->fh_desc,
					&new_hdl);
	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		LogDebug(COMPONENT_CACHE_INODE,
			 "could not get create_handle object");
		*entry = NULL;
		return status;
	}

	LogFullDebug(COMPONENT_CACHE_INODE, "Creating entry");

	status = cache_inode_new_entry(new_hdl, CACHE_INODE_FLAG_NONE, entry);
	if (*entry == NULL)
		return status;

	/* If we have an entry, we succeeded.  Don't propagate any
	   ENTRY_EXISTS errors upward. */
	return CACHE_INODE_SUCCESS;
}				/* cache_inode_get */

/**
 * @brief Get an initial reference to a cache entry by its key.
 *
 * Lookup a cache entry by key, given an associated entry sharing the
 * same export (e.g..
 *
 * @param[in] key     [in] Cache key to use for lookup
 * @param[in] req_ctx FSAL operation context
 * @param[in] flags   flags
 *
 * @return Pointer to a ref'd entry if found, else NULL.
 */
cache_entry_t *
cache_inode_get_keyed(cache_inode_key_t *key,
		      const struct req_op_context *req_ctx,
		      uint32_t flags,
		      cache_inode_status_t *status)
{
	cache_entry_t *entry = NULL;
	cih_latch_t latch;

	if (key->kv.addr == NULL) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Attempt to use NULL key");
		return NULL;
	}

	/* Check if the entry already exists */
	entry =
	    cih_get_by_key_latched(key, &latch,
				   CIH_GET_RLOCK | CIH_GET_UNLOCK_ON_MISS,
				   __func__, __LINE__);
	if (likely(entry)) {
		/* Ref entry */
		cache_inode_lru_ref(entry, LRU_FLAG_NONE);
		/* Release the subtree hash table lock */
		cih_latch_rele(&latch);
		goto out;
	}
	/* Cache miss, allocate a new entry */
	if (!(flags & CIG_KEYED_FLAG_CACHED_ONLY)) {
		struct fsal_obj_handle *new_hdl;
		struct fsal_export *exp_hdl;
		fsal_status_t fsal_status;

		/* Assert that we don't have to lookup export */
		assert(key->exportid == req_ctx->export->export.id);

		exp_hdl = req_ctx->export->export.export_hdl;
		fsal_status =
		    exp_hdl->ops->create_handle(exp_hdl, req_ctx, &key->kv,
						&new_hdl);

		if (unlikely(FSAL_IS_ERROR(fsal_status))) {
			*status = cache_inode_error_convert(fsal_status);
			LogDebug(COMPONENT_CACHE_INODE,
				 "could not get create_handle object %s",
				 cache_inode_err_str(*status));
			goto out;
		}

		LogFullDebug(COMPONENT_CACHE_INODE, "Creating entry");

		/* if all else fails, create a new entry */
		*status =
		    cache_inode_new_entry(new_hdl, CACHE_INODE_FLAG_NONE,
					  &entry);
		if (unlikely(!entry))
			goto out;

		*status =
			cache_inode_lock_trust_attrs(entry, req_ctx, false);
		if (unlikely(*status != CACHE_INODE_SUCCESS)) {
			cache_inode_put(entry);
			entry = NULL;
			goto out;
		}

		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	}			/* ! cached only */
 out:
	return entry;
}

/**
 * @brief Get a reference to a cache inode via some source pointer, verifying
 * that the inode is still reachable.
 *
 * Uses get_entry to fetch a cache inode entry pointer while holding the passed
 * lock, and then while under the lock, copies the key, then outside the lock,
 * looks up the entry by key.
 *
 * The lock and the source MUST be involved somehow in cleaning a cache inode
 * entry that is being killed or recycled to insure the source is valid long
 * enough to copy the key. I.e. cache inode entry cleanup MUST take the lock,
 * and under that lock, remove the pointer to the entry from the source.
 *
 * @param entry     [IN/OUT] call by ref pointer to store cache entry
 * @param lock      [IN] rwlock that protects the source of the inode pointer
 * @param get_entry [IN] routine that gets the inode pointer from the source
 * @param source    [IN] opaque param to pass to get_entry
 *
 * @return status cache inode status code
 */

cache_inode_status_t
cache_inode_get_protected(cache_entry_t **entry,
			  pthread_rwlock_t *lock,
			  cache_inode_status_t get_entry(cache_entry_t **,
							 void *),
			  void *source)
{
	cih_latch_t latch;
	cache_inode_key_t key;
	cache_inode_status_t status;

	PTHREAD_RWLOCK_rdlock(lock);

	status = get_entry(entry, source);

	if (unlikely(status != CACHE_INODE_SUCCESS)) {
		PTHREAD_RWLOCK_unlock(lock);
		LogDebug(COMPONENT_CACHE_INODE,
			 "get_entry failed with %s",
			 cache_inode_err_str(status));
		return status;
	}

	/* Duplicate the entry's key so we can get by key to make
	 * sure this is not a killed entry.
	 */
	if (unlikely(cache_inode_key_dup(&key, &(*entry)->fh_hk.key) != 0)) {
		PTHREAD_RWLOCK_unlock(lock);
		LogDebug(COMPONENT_CACHE_INODE,
			 "cache_inode_key_dup failed with CACHE_INODE_MALLOC_ERROR");
		return CACHE_INODE_MALLOC_ERROR;
	}

	PTHREAD_RWLOCK_unlock(lock);

	*entry = cih_get_by_key_latched(&key, &latch,
					CIH_GET_RLOCK | CIH_GET_UNLOCK_ON_MISS,
					__func__, __LINE__);

	/* Done with the key */
	cache_inode_key_delete(&key);

	if (unlikely((*entry) == NULL)) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "cih_get_by_key_latched failed returning CACHE_INODE_FSAL_ESTALE");
		return CACHE_INODE_FSAL_ESTALE;
	}

	/* Ref entry */
	cache_inode_lru_ref(*entry, LRU_FLAG_NONE);

	/* Release the subtree hash table lock */
	cih_latch_rele(&latch);

	return CACHE_INODE_SUCCESS;
}

/**
 *
 * @brief Release logical reference to a cache entry
 *
 * This function releases a logical reference to a cache entry
 * acquired by a previous call to cache_inode_get.
 *
 * The result is typically to decrement the reference count on entry,
 * but additional side effects include LRU adjustment, movement
 * to/from the protected LRU partition, or recyling if the caller has
 * raced an operation which made entry unreachable (and this current
 * caller has the last reference).  Caller MUST NOT make further
 * accesses to the memory pointed to by entry.
 *
 * @param[in] entry Cache entry being returned
 */
void
cache_inode_put(cache_entry_t *entry)
{
	cache_inode_lru_unref(entry, LRU_FLAG_NONE);
}

/** @} */
