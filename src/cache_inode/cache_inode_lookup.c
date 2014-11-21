/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file    cache_inode_lookup.c
 * @brief   Lookups through the cache
 */
#include "config.h"
#include "log.h"
#include "abstract_atomic.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"
#include "export_mgr.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

static inline bool trust_negative_cache(cache_entry_t *parent)
{
	return ((op_ctx->export->options &
		 EXPORT_OPTION_TRUST_READIR_NEGATIVE_CACHE) != 0) &&
		(parent->icreate_refcnt == 0) &&
	       ((parent->flags & CACHE_INODE_DIR_POPULATED) != 0);
}

/**
 *
 * @brief Do the work of looking up a name in a directory.
 *
 * This function looks up a filename in the given directory.  It
 * implements the functionality of cache_inode_lookup and expects the
 * directory content lock not to be held when it is called.
 *
 * If a cache entry is returned, its refcount is incremented by 1.
 *
 * @param[in]  parent  The directory to search
 * @param[in]  name    The name to be looked up
 * @param[out] entry   Found entry
 *
 * @return CACHE_INDOE_SUCCESS or error.
 */
cache_inode_status_t
cache_inode_lookup_impl(cache_entry_t *parent,
			const char *name,
			cache_entry_t **entry)
{
	cache_inode_dir_entry_t *dirent = NULL;
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_obj_handle *object_handle = NULL;
	struct fsal_obj_handle *dir_handle;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	if (parent->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		*entry = NULL;
		return status;
	}

	PTHREAD_RWLOCK_rdlock(&parent->content_lock);

	/* if name is ".", use the input value */
	if (strcmp(name, ".") == 0) {
		*entry = parent;
		/* Increment the refcount so the caller's decrementing it
		   doesn't take us below the sentinel count. */
		cache_inode_lru_ref(*entry, LRU_FLAG_NONE);
		goto out;
	} else if (strcmp(name, "..") == 0) {
		/* Directory do only have exactly one parent. This a limitation
		 * in all FS, which implies that hard link are forbidden on
		 * directories (so that they exists only in one dir).  Because
		 * of this, the parent list is always limited to one element for
		 * a dir.  Clients SHOULD never 'lookup( .. )' in something that
		 * is no dir. */
		status = cache_inode_lookupp_impl(parent, entry);
		goto out;
	} else {
		int write_locked = 0;
		/* We first try avltree_lookup by name.  If that fails, we
		 * dispatch to the FSAL. */
		/* XXX this ++write_locked idiom is not good style */
		for (write_locked = 0; write_locked < 2; ++write_locked) {
			/* If the dirent cache is untrustworthy, don't even ask
			 * it */
			if (parent->flags & CACHE_INODE_TRUST_CONTENT) {
				dirent =
				    cache_inode_avl_qp_lookup_s(parent, name,
								1);
				if (dirent) {
					*entry =
					    cache_inode_get_keyed(
						    &dirent->ckey,
						    CIG_KEYED_FLAG_NONE,
						    &status);
					if (*entry) {
						/* We have our entry and a
						 * valid reference.
						 * Declare victory. */
						status = CACHE_INODE_SUCCESS;
						goto out;
					}
				} else {	/* ! dirent */
					if (trust_negative_cache(parent)) {
						/* If the dirent cache is both
						 * fully populated and valid,
						 * it can serve negative
						 * lookups. */
						*entry = NULL;
						status = CACHE_INODE_NOT_FOUND;
						goto out;
					}
					/* XXX keep going? */
				}
			} else if (write_locked
				   && !(parent->
					flags & CACHE_INODE_TRUST_CONTENT)) {
				/* We have the write lock and the content is
				   still invalid.  Empty it out and mark it
				   * valid in preparation for caching the
				   * result of this lookup. */
				cache_inode_invalidate_all_cached_dirent
				    (parent);
			}
			if (!write_locked) {
				/* Get a write lock and do it again. */
				PTHREAD_RWLOCK_unlock(&parent->content_lock);
				PTHREAD_RWLOCK_wrlock(&parent->content_lock);
			}
		}
		assert(*entry == NULL);
		LogDebug(COMPONENT_CACHE_INODE, "Cache Miss detected");
	}

	dir_handle = parent->obj_handle;
	fsal_status =
	    dir_handle->ops->lookup(dir_handle, name, &object_handle);
	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE from a lookup.");
			cache_inode_kill_entry(parent);
		}
		status = cache_inode_error_convert(fsal_status);
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "FSAL %d %s returned %s",
			     (int) op_ctx->export->export_id,
			     op_ctx->export->fullpath,
			     cache_inode_err_str(status));
		*entry = NULL;
		goto out;
	}

	LogFullDebug(COMPONENT_CACHE_INODE, "Creating entry for %s", name);

	/* Allocation of a new entry in the cache */
	status = cache_inode_new_entry(object_handle, CACHE_INODE_FLAG_NONE,
				       entry);

	if (unlikely(!*entry))
		goto out;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Created entry %p FSAL %s for %s",
		     *entry, (*entry)->obj_handle->fsal->name, name);

	/* Entry was found in the FSAL, add this entry to the
	   parent directory */
	status = cache_inode_add_cached_dirent(parent, name, *entry, NULL);
	if (status == CACHE_INODE_ENTRY_EXISTS)
		status = CACHE_INODE_SUCCESS;

	if ((*entry)->type == DIRECTORY) {
		/* Insert Parent's key */
		cache_inode_key_dup(&(*entry)->object.dir.parent,
				    &parent->fh_hk.key);
	}

 out:

	PTHREAD_RWLOCK_unlock(&parent->content_lock);

	return status;
}

/**
 * @brief Public function for looking up a name in a directory
 *
 * Looks up for a name in a directory indicated by a cached entry. The
 * directory should have been cached before.
 *
 * If a cache entry is returned, the refcount on entry is +1.
 *
 * @param[in]  parent  Entry for the parent directory to be managed.
 * @param[in]  name    Name of the entry that we are looking up.
 * @param[out] entry   Found entry
 *
 * @return CACHE_INODE_SUCCESS or error.
 */

cache_inode_status_t
cache_inode_lookup(cache_entry_t *parent,
		   const char *name,
		   cache_entry_t **entry)
{
	fsal_accessflags_t access_mask =
	    (FSAL_MODE_MASK_SET(FSAL_X_OK) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE));
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	status = cache_inode_access(parent, access_mask);

	if (status != CACHE_INODE_SUCCESS) {
		*entry = NULL;
		return status;
	}

	status = cache_inode_lookup_impl(parent, name, entry);

	return status;
}

/** @} */
