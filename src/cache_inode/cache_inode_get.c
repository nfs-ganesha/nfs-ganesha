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
 * @brief Check the active export mapping for this entry and update if
 *        necessary.
 *
 * If the entry does not have a mapping for the active export, add one.
 *
 * @param[in]  entry     The cache inode
 * @param[in]  export    The active export
 *
 * @retval true if successful
 * @retval false if new mapping was necessary and memory alloc failed
 *
 */

bool check_mapping(cache_entry_t *entry,
		   struct gsh_export *export)
{
	struct glist_head *glist;
	struct entry_export_map *expmap;
	bool try_write = false;

	/* Fast path check to see if this export is already mapped */
	if (atomic_fetch_voidptr(&entry->first_export) == export)
		return true;

	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

again:
	(void)atomic_inc_uint64_t(&cache_stp->inode_mapping);

	glist_for_each(glist, &entry->export_list) {
		expmap = glist_entry(glist,
				     struct entry_export_map,
				     export_per_entry);

		/* Found active export on list */
		if (expmap->export == export) {
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			return true;
		}
	}

	if (!try_write) {
		/* Now take write lock and try again in
		 * case another thread has raced with us.
		 */
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
		try_write = true;
		goto again;
	}

	/* We have the write lock and did not find
	 * this export on the list, add it.
	 */

	expmap = gsh_calloc(1, sizeof(*expmap));

	if (expmap == NULL) {
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		LogCrit(COMPONENT_CACHE_INODE,
			 "Out of memory");
		return false;
	}

	PTHREAD_RWLOCK_wrlock(&export->lock);

	/* If export_list is empty, store this export as first */
	if (glist_empty(&entry->export_list))
		atomic_store_voidptr(&entry->first_export, export);

	expmap->export = export;
	expmap->entry = entry;

	glist_add_tail(&entry->export_list,
		       &expmap->export_per_entry);
	glist_add_tail(&export->entry_list,
		       &expmap->entry_per_export);

	PTHREAD_RWLOCK_unlock(&export->lock);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	return true;
}

/**
 *
 * @brief Cleans up the export mappings for this entry.
 *
 * @param[in]  entry     The cache inode
 * @param[in]  export    The active export
 *
 */

void clean_mapping(cache_entry_t *entry)
{
	struct glist_head *glist;
	struct glist_head *glistn;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	/* Entry is unreachable and not referenced so no need to hold attr_lock
	 * to cleanup the export map.
	 */
	glist_for_each_safe(glist, glistn, &entry->export_list) {
		struct entry_export_map *expmap;
		expmap = glist_entry(glist,
				     struct entry_export_map,
				     export_per_entry);

		PTHREAD_RWLOCK_wrlock(&expmap->export->lock);

		/* Remove from list of exports for this entry */
		glist_del(&expmap->export_per_entry);

		/* Remove from list of entries for this export */
		glist_del(&expmap->entry_per_export);

		PTHREAD_RWLOCK_unlock(&expmap->export->lock);

		gsh_free(expmap);
	}

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
}

/**
 *
 * @brief Gets an entry by using its fsdata as a key and caches it if needed.
 *
 * Gets an entry by using its fsdata as a key and caches it if needed.
 *
 * If a cache entry is returned, its refcount is incremented by one.
 *
 * @param[in]  fsdata     File system data
 * @param[out] entry      The entry
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */
cache_inode_status_t
cache_inode_get(cache_inode_fsal_data_t *fsdata,
		cache_entry_t **entry)
{
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_export *exp_hdl = NULL;
	struct fsal_obj_handle *new_hdl;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cih_latch_t latch;
	cache_inode_key_t key;

	key.fsal = fsdata->export->fsal;

	(void) cih_hash_key(&key, fsdata->export->fsal, &fsdata->fh_desc,
			    CIH_HASH_KEY_PROTOTYPE);

	(void)atomic_inc_uint64_t(&cache_stp->inode_req);
	/* Do lookup */
	*entry =
	    cih_get_by_key_latched(&key, &latch,
				  CIH_GET_RLOCK | CIH_GET_UNLOCK_ON_MISS,
				  __func__, __LINE__);
	if (*entry) {
		/* take an extra reference within the critical section */
		cache_inode_lru_ref(*entry, LRU_REQ_INITIAL);
		cih_latch_rele(&latch);

		if (!check_mapping(*entry, op_ctx->export)) {
			/* Return error instead of entry */
			cache_inode_put(*entry);
			*entry = NULL;
			return CACHE_INODE_MALLOC_ERROR;
		}
		(void)atomic_inc_uint64_t(&cache_stp->inode_hit);

		return CACHE_INODE_SUCCESS;
	}

	/* Cache miss, allocate a new entry */
	exp_hdl = fsdata->export;
	fsal_status =
	    exp_hdl->ops->create_handle(exp_hdl, &fsdata->fh_desc,
					&new_hdl);
	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		LogDebug(COMPONENT_CACHE_INODE,
			 "could not get create_handle object");
		*entry = NULL;
		return status;
	}

	LogFullDebug(COMPONENT_CACHE_INODE, "Creating entry");

	status = cache_inode_new_entry(new_hdl, CACHE_INODE_FLAG_NONE,
				       entry);

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
 * @param[in] flags   flags
 *
 * @return Pointer to a ref'd entry if found, else NULL.
 */
cache_entry_t *
cache_inode_get_keyed(cache_inode_key_t *key,
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

		if (!check_mapping(entry, op_ctx->export)) {
			/* Return error instead of entry */
			cache_inode_put(entry);
			return NULL;
		}

		goto out;
	}
	/* Cache miss, allocate a new entry */
	if (!(flags & CIG_KEYED_FLAG_CACHED_ONLY)) {
		struct fsal_obj_handle *new_hdl;
		struct fsal_export *exp_hdl;
		fsal_status_t fsal_status;

		exp_hdl = op_ctx->fsal_export;
		fsal_status =
		    exp_hdl->ops->create_handle(exp_hdl, &key->kv,
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

		*status = cache_inode_lock_trust_attrs(entry, false);
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
