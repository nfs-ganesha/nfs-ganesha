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
 * @file    cache_inode_rename.c
 * @brief   Renames an entry.
 *
 * Renames an entry.
 *
 */
#include "config.h"
#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief Renames an entry in the same directory.
 *
 * Renames an entry in the same directory.
 *
 * @param[in,out] parent The directory to be managed
 * @param[in]    oldname The name of the entry to rename
 * @param[in]    newname The new name for the entry
 *
 * @return the same as *status
 */

cache_inode_status_t
cache_inode_rename_cached_dirent(cache_entry_t *parent,
				 const char *oldname,
				 const char *newname)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	/* Sanity check */
	if (parent->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		return status;
	}

	status =
	    cache_inode_operate_cached_dirent(parent, oldname, newname,
					      CACHE_INODE_DIRENT_OP_RENAME);

	return status;
}

/**
 * @brief Lock two directories in order
 *
 * This function gets the locks on both entries. If src and dest are
 * the same, it takes only one lock.  Locks are acquired with lowest
 * cache_entry first to avoid deadlocks.
 *
 * @param[in] src  Source directory to lock
 * @param[in] dest Destination directory to lock
 */

static inline void
src_dest_lock(cache_entry_t *src, cache_entry_t *dest)
{

	if (src == dest)
		PTHREAD_RWLOCK_wrlock(&src->content_lock);
	else {
		if (src < dest) {
			PTHREAD_RWLOCK_wrlock(&src->content_lock);
			PTHREAD_RWLOCK_wrlock(&dest->content_lock);
		} else {
			PTHREAD_RWLOCK_wrlock(&dest->content_lock);
			PTHREAD_RWLOCK_wrlock(&src->content_lock);
		}
	}
}

/**
 * @brief Unlock two directories in order
 *
 * This function releases the locks on both entries. If src and dest
 * are the same, it releases the lock and returns.  Locks are released
 * with lowest cache_entry first.
 *
 * @param[in] src  Source directory to lock
 * @param[in] dest Destination directory to lock
 */

static inline void
src_dest_unlock(cache_entry_t *src, cache_entry_t *dest)
{
	if (src == dest)
		PTHREAD_RWLOCK_unlock(&src->content_lock);
	else {
		if (src < dest) {
			PTHREAD_RWLOCK_unlock(&dest->content_lock);
			PTHREAD_RWLOCK_unlock(&src->content_lock);
		} else {
			PTHREAD_RWLOCK_unlock(&src->content_lock);
			PTHREAD_RWLOCK_unlock(&dest->content_lock);
		}
	}
}

/**
 * @brief Renames an entry
 *
 * This function calls the FSAL to rename a file, then mirrors the
 * operation in the cache.
 *
 * @param[in] dir_src  The source directory
 * @param[in] oldname  The current name of the file
 * @param[in] dir_dest The destination directory
 * @param[in] newname  The name to be assigned to the object
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success.
 * @retval CACHE_INODE_NOT_FOUND if source object does not exist
 * @retval CACHE_INODE_ENTRY_EXISTS on collision.
 * @retval CACHE_INODE_NOT_A_DIRECTORY if dir_src or dir_dest is not a
 *                              directory.
 * @retval CACHE_INODE_BADNAME if either name is "." or ".."
 */
cache_inode_status_t
cache_inode_rename(cache_entry_t *dir_src,
		   const char *oldname,
		   cache_entry_t *dir_dest,
		   const char *newname)
{
	fsal_status_t fsal_status = { 0, 0 };
	cache_entry_t *lookup_src = NULL;
	cache_entry_t *lookup_dst = NULL;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cache_inode_status_t status_ref_dir_src = CACHE_INODE_SUCCESS;
	cache_inode_status_t status_ref_dir_dst = CACHE_INODE_SUCCESS;
	cache_inode_status_t status_ref_dst = CACHE_INODE_SUCCESS;

	if ((dir_src->type != DIRECTORY) || (dir_dest->type != DIRECTORY)) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		goto out;
	}

	/* Check for . and .. on oldname and newname. */
	if (!strcmp(oldname, ".") || !strcmp(oldname, "..")
	    || !strcmp(newname, ".") || !strcmp(newname, "..")) {
		status = CACHE_INODE_BADNAME;
		goto out;
	}

	/* Check for object existence in source directory */
	status =
	    cache_inode_lookup_impl(dir_src, oldname, &lookup_src);

	if (lookup_src == NULL) {
		/* If FSAL FH is stale, then this was managed in
		 * cache_inode_lookup */
		if (status != CACHE_INODE_FSAL_ESTALE)
			status = CACHE_INODE_NOT_FOUND;

		LogEvent(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : source doesn't exist",
			 dir_src, oldname, dir_dest, newname);
		goto out;
	}

	/* Do not rename a junction node or an export root. */
	if (lookup_src->type == DIRECTORY) {
		/* Get attr_lock for looking at junction_export */
		PTHREAD_RWLOCK_rdlock(&lookup_src->attr_lock);

		if (lookup_src->object.dir.junction_export != NULL ||
		    atomic_fetch_int32_t(&lookup_src->exp_root_refcount)
		    != 0) {
			/* Trying to rename an export mount point */
			LogCrit(COMPONENT_CACHE_INODE,
				 "Attempt to rename export %s",
				 oldname);

			/* Release attr_lock */
			PTHREAD_RWLOCK_unlock(&lookup_src->attr_lock);

			status = CACHE_INODE_DIR_NOT_EMPTY;
			goto out;
		}

		/* Release attr_lock */
		PTHREAD_RWLOCK_unlock(&lookup_src->attr_lock);
	}


	/* Check if an object with the new name exists in the destination
	   directory */
	status =
	    cache_inode_lookup_impl(dir_dest, newname, &lookup_dst);

	if (status == CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : destination already exists",
			 dir_src, oldname, dir_dest, newname);
	}
	if (status == CACHE_INODE_NOT_FOUND)
		status = CACHE_INODE_SUCCESS;
	if (status == CACHE_INODE_FSAL_ESTALE) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : stale destination", dir_src,
			 oldname, dir_dest, newname);
	}

	if (lookup_src == lookup_dst) {
		/* Nothing to do according to POSIX and NFS3/4
		 * If from and to both refer to the same file (they might be
		 * hard links of each other), then RENAME should perform no
		 * action and return success */
		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : same file so skipping out",
			 dir_src, oldname, dir_dest, newname);
		goto out;
	}

	/* Perform the rename operation in FSAL before doing anything in the
	 * cache. Indeed, if the FSAL_rename fails unexpectly, the cache would
	 * be inconsistent!
	 *
	 * We do almost no checking before making call because we want to return
	 * error based on the files actually present in the directories, not
	 * what we have in our cache.
	 */
	LogFullDebug(COMPONENT_CACHE_INODE, "about to call FSAL rename");

	fsal_status =
	    dir_src->obj_handle->ops->rename(dir_src->obj_handle,
					     oldname, dir_dest->obj_handle,
					     newname);

	LogFullDebug(COMPONENT_CACHE_INODE, "returned from FSAL rename");

	status_ref_dir_src = cache_inode_refresh_attrs_locked(dir_src);

	if (dir_src != dir_dest)
		status_ref_dir_dst =
			cache_inode_refresh_attrs_locked(dir_dest);

	LogFullDebug(COMPONENT_CACHE_INODE, "done refreshing attributes");

	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "FSAL rename failed with %s",
			     cache_inode_err_str(status));

		goto out;
	}

	if (lookup_dst) {
		/* Force a refresh of the overwritten inode */
		status_ref_dst =
		    cache_inode_refresh_attrs_locked(lookup_dst);
		if (status_ref_dst == CACHE_INODE_FSAL_ESTALE)
			status_ref_dst = CACHE_INODE_SUCCESS;
	}

	status = status_ref_dir_src;

	if (status == CACHE_INODE_SUCCESS)
		status = status_ref_dir_dst;

	if (status == CACHE_INODE_SUCCESS)
		status = status_ref_dst;

	if (status != CACHE_INODE_SUCCESS)
		goto out;

	/* Must take locks on directories now,
	 * because if another thread checks source and destination existence
	 * in the same time, it will try to do the same checks...
	 * and it will have the same conclusion !!!
	 */
	src_dest_lock(dir_src, dir_dest);

	if (lookup_dst) {
		/* Remove the entry from parent dir_entries avl */
		status_ref_dir_dst =
		    cache_inode_remove_cached_dirent(dir_dest, newname);

		if (status_ref_dir_dst != CACHE_INODE_SUCCESS) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "remove entry failed with status %s",
				 cache_inode_err_str(status_ref_dir_dst));
			cache_inode_invalidate_all_cached_dirent(dir_dest);
		}
	}

	if (dir_src == dir_dest) {
		/* if the rename operation is made within the same dir, then we
		 * use an optimization: cache_inode_rename_dirent is used
		 * instead of adding/removing dirent. This limits the use of
		 * resource in this case */

		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : source and target "
			 "directory  the same", dir_src, oldname, dir_dest,
			 newname);

		cache_inode_status_t tmp_status =
		    cache_inode_rename_cached_dirent(dir_dest,
						     oldname, newname);
		if (tmp_status != CACHE_INODE_SUCCESS) {
			/* We're obviously out of date.  Throw out the cached
			   directory */
			cache_inode_invalidate_all_cached_dirent(dir_dest);
		}
	} else {
		cache_inode_status_t tmp_status = CACHE_INODE_SUCCESS;

		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : moving entry", dir_src,
			 oldname, dir_dest, newname);

		/* We may have a cache entry for the destination
		 * filename.  If we do, we must delete it : it is stale. */
		tmp_status =
		    cache_inode_remove_cached_dirent(dir_dest, newname);

		if (tmp_status != CACHE_INODE_SUCCESS
		    && tmp_status != CACHE_INODE_NOT_FOUND) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "Remove stale dirent returned %s",
				 cache_inode_err_str(tmp_status));
			cache_inode_invalidate_all_cached_dirent(dir_dest);
		}

		tmp_status =
		    cache_inode_add_cached_dirent(dir_dest, newname, lookup_src,
						  NULL);
		if (tmp_status != CACHE_INODE_SUCCESS) {
			/* We're obviously out of date.  Throw out the cached
			   directory */
			LogCrit(COMPONENT_CACHE_INODE, "Add dirent returned %s",
				cache_inode_err_str(tmp_status));
			cache_inode_invalidate_all_cached_dirent(dir_dest);
		}

		/* Remove the old entry */
		tmp_status =
		    cache_inode_remove_cached_dirent(dir_src, oldname);
		if (tmp_status != CACHE_INODE_SUCCESS
		    && tmp_status != CACHE_INODE_NOT_FOUND) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "Remove old dirent returned %s",
				 cache_inode_err_str(tmp_status));
			cache_inode_invalidate_all_cached_dirent(dir_src);
		}
	}

	/* unlock entries */
	src_dest_unlock(dir_src, dir_dest);

out:
	if (lookup_src)
		cache_inode_put(lookup_src);

	if (lookup_dst)
		cache_inode_put(lookup_dst);

	return status;
}

/** @} */
