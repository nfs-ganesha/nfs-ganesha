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
 * @file cache_inode_lookupp.c
 * @brief Parent lookups through cache
 */

#include "config.h"
#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "export_mgr.h"

/**
 *
 * @brief Implements parent lookup functionality
 *
 * Looks up (and caches) the parent directory for a directory.  If an
 * entry is returned, that entry's refcount is incremented by one.
 * The caller must hold a reference on the entry.  It is expected that
 * the caller holds the directory lock on entry.  It is also expected
 * that the caller will relinquish the directory lock after return.
 * If result was not cached, the function will drop the read lock and
 * acquire a write lock so it can add the result to the cache.
 *
 * @note this can behave differently than a local Linux user of the
 * filesystem if the path to get here is a symlink.  In the local
 * case, the path is consistent because the kernel's CWD gets the
 * expected "..".  We don't have that so the ".." is the directory in
 * which the name resides even though the symlink allowed us to skip
 * around the real (non symlink resolved) path.
 *
 * @param[in]  entry   Entry whose parent is to be obtained
 * @param[out] parent  Parent directory
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t
cache_inode_lookupp_impl(cache_entry_t *entry,
			 cache_entry_t **parent)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	/* Never even think of calling FSAL_lookup on root/.. */

	if (entry->type == DIRECTORY) {
		PTHREAD_RWLOCK_rdlock(&op_ctx->export->lock);

		if (entry == op_ctx->export->exp_root_cache_inode) {
			/* This entry is the root of the current export, so if
			 * we get this far, return itself. Note that NFS v4
			 * LOOKUPP will not come here, it catches the root entry
			 * earlier.
			 *
			 * Bump the refcount on the current entry (so the
			 * caller's releasing decrementing it doesn't take us
			 * below the sentinel count)
			 */
			PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
			cache_inode_lru_ref(entry, LRU_FLAG_NONE);
			*parent = entry;
			return CACHE_INODE_SUCCESS;
		}

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
	}

	/* Try to lookup by key (fh) */
	*parent =
	    cache_inode_get_keyed(&entry->object.dir.parent,
				  CIG_KEYED_FLAG_NONE, &status);
	if (!(*parent)) {
		/* If we didn't find it, drop the read lock, get a write
		   lock, and got to FSAL. */
		struct fsal_obj_handle *parent_handle;
		fsal_status_t fsal_status;

		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);

		fsal_status =
		    entry->obj_handle->ops->lookup(entry->obj_handle,
						   "..", &parent_handle);
		if (FSAL_IS_ERROR(fsal_status)) {
			if (fsal_status.major == ERR_FSAL_STALE) {
				LogEvent(COMPONENT_CACHE_INODE,
					 "FSAL returned STALE from lookup.");
				cache_inode_kill_entry(entry);
			}
			status = cache_inode_error_convert(fsal_status);
			*parent = NULL;
			return status;
		}

		LogFullDebug(COMPONENT_CACHE_INODE, "Creating entry");

		/* Allocation of a new entry in the cache */
		status =
		    cache_inode_new_entry(parent_handle, CACHE_INODE_FLAG_NONE,
					  parent);
		if (*parent == NULL)
			return status;

		/* Dup keys */
		cache_inode_key_dup(&entry->object.dir.parent,
				    &((*parent)->fh_hk.key));
	}

	return status;
}

/**
 * @brief Public function to look up a directory's parent
 *
 * This function looks up (and potentially caches) the parent of a
 * directory.
 *
 * If a cache entry is returned, its refcount is +1.
 *
 * @param[in]  entry   Entry whose parent is to be obtained.
 * @param[out] parent  Parent directory
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t
cache_inode_lookupp(cache_entry_t *entry,
		    cache_entry_t **parent)
{
	cache_inode_status_t status;
	PTHREAD_RWLOCK_rdlock(&entry->content_lock);
	status = cache_inode_lookupp_impl(entry, parent);
	PTHREAD_RWLOCK_unlock(&entry->content_lock);
	return status;
}

/** @} */
