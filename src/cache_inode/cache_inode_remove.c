/*
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
 *
 * @file cache_inode_remove.c
 * @brief Removes an entry of any type.
 */

#include "config.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_hash.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "hashtable.h"
#include "nfs4_acls.h"
#include "sal_functions.h"
#include "nfs_core.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

/**
 *
 * @brief Remove a name from a directory.
 *
 * Removes a name from the supplied directory.  The caller should hold
 * no locks on the directory.
 *
 * @param[in] entry   Entry for the parent directory to be managed
 * @param[in] name    Name to be removed
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_remove(cache_entry_t *entry, const char *name)
{
	cache_entry_t *to_remove_entry = NULL;
	fsal_status_t fsal_status = { 0, 0 };
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cache_inode_status_t status_ref_entry = CACHE_INODE_SUCCESS;

	if (entry->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		goto out;
	}

	/* Factor this somewhat.  In the case where the directory hasn't
	   been populated, the entry may not exist in the cache and we'd
	   be bringing it in just to dispose of it. */

	/* Looks up for the entry to remove */
	status =
	    cache_inode_lookup_impl(entry, name, &to_remove_entry);

	if (to_remove_entry == NULL) {
		LogFullDebug(COMPONENT_CACHE_INODE, "lookup %s failure %s",
			     name, cache_inode_err_str(status));
		goto out;
	}

	/* Do not remove a junction node or an export root. */
	if (to_remove_entry->type == DIRECTORY) {
		/* Get attr_lock for looking at junction_export */
		PTHREAD_RWLOCK_rdlock(&to_remove_entry->attr_lock);

		if (to_remove_entry->object.dir.junction_export != NULL ||
		    atomic_fetch_int32_t(&to_remove_entry->exp_root_refcount)
		    != 0) {
			/* Trying to remove an export mount point */
			LogCrit(COMPONENT_CACHE_INODE,
				 "Attempt to remove export %s",
				 name);

			/* Release attr_lock */
			PTHREAD_RWLOCK_unlock(&to_remove_entry->attr_lock);

			status = CACHE_INODE_DIR_NOT_EMPTY;
			goto out;
		}

		/* Release attr_lock */
		PTHREAD_RWLOCK_unlock(&to_remove_entry->attr_lock);
	}

	LogDebug(COMPONENT_CACHE_INODE, "%s", name);

	if (is_open(to_remove_entry)) {
		/* entry is not locked and seems to be open for fd caching
		 * purpose.
		 * candidate for closing since unlink of an open file results
		 * in 'silly rename' on certain platforms */
		status =
		    cache_inode_close(to_remove_entry,
				      CACHE_INODE_FLAG_REALLYCLOSE);
		if (status != CACHE_INODE_SUCCESS) {
			/* non-fatal error. log the warning and move on */
			LogCrit(COMPONENT_CACHE_INODE,
				"Error closing %s before unlink: %s.", name,
				cache_inode_err_str(status));
		}
	}

	fsal_status =
	    entry->obj_handle->ops->unlink(entry->obj_handle, name);

	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_STALE)
			cache_inode_kill_entry(entry);

		status = cache_inode_error_convert(fsal_status);

		LogFullDebug(COMPONENT_CACHE_INODE, "unlink %s failure %s",
			     name, cache_inode_err_str(status));

		if (to_remove_entry->type == DIRECTORY
		    && status == CACHE_INODE_DIR_NOT_EMPTY) {
			/* its dirent tree is probably stale, flush it
			 * to try and make things right again */
			PTHREAD_RWLOCK_wrlock(&to_remove_entry->content_lock);
			(void)
			    cache_inode_invalidate_all_cached_dirent
			    (to_remove_entry);
			PTHREAD_RWLOCK_unlock(&to_remove_entry->content_lock);
		}
		goto out;
	}

	/* Remove the entry from parent dir_entries avl */
	PTHREAD_RWLOCK_wrlock(&entry->content_lock);
	status_ref_entry = cache_inode_remove_cached_dirent(entry, name);
	LogDebug(COMPONENT_CACHE_INODE,
		 "cache_inode_remove_cached_dirent %s status %s", name,
		 cache_inode_err_str(status_ref_entry));
	PTHREAD_RWLOCK_unlock(&entry->content_lock);

	status_ref_entry = cache_inode_refresh_attrs_locked(entry);

	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "not sure this code makes sense %s failure %s",
			     name, cache_inode_err_str(status));
		goto out;
	}

	/* Update the attributes for the removed entry */
	(void)cache_inode_refresh_attrs_locked(to_remove_entry);

	status = status_ref_entry;
	if (status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "cache_inode_refresh_attrs_locked(entry %p %s) "
			 "returned %s", entry, name,
			 cache_inode_err_str(status_ref_entry));
	}

out:
	LogFullDebug(COMPONENT_CACHE_INODE, "remove %s: status=%s", name,
		     cache_inode_err_str(status));

	/* This is for the reference taken by lookup */
	if (to_remove_entry)
		cache_inode_put(to_remove_entry);

	return status;
}

/** @} */
