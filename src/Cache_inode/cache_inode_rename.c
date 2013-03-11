/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * @defgroup Cache_inode Cache Inode
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
#include "HashTable.h"
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
 * @param[in]    req_ctx   Request context
 *
 * @return the same as *status
 */

cache_inode_status_t
cache_inode_rename_cached_dirent(cache_entry_t *parent,
                                 const char *oldname,
                                 const char *newname,
				 const struct req_op_context *req_ctx)
{
  cache_inode_status_t status = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(parent->type != DIRECTORY)
    {
      status = CACHE_INODE_NOT_A_DIRECTORY;
      return status;
    }

  status = cache_inode_operate_cached_dirent(parent,
                                              oldname,
                                              newname,
                                              req_ctx,
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

static inline void src_dest_lock(cache_entry_t *src,
				 cache_entry_t *dest)
{

  if(src == dest)
    PTHREAD_RWLOCK_wrlock(&src->content_lock);
  else
    {
      if(src < dest)
        {
          PTHREAD_RWLOCK_wrlock(&src->content_lock);
          PTHREAD_RWLOCK_wrlock(&dest->content_lock);
        }
      else
        {
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

static inline void src_dest_unlock(cache_entry_t *src,
				   cache_entry_t *dest)
{
  if(src == dest)
    {
      PTHREAD_RWLOCK_unlock(&src->content_lock);
    }
  else
    {
      if(src < dest)
        {
          PTHREAD_RWLOCK_unlock(&dest->content_lock);
          PTHREAD_RWLOCK_unlock(&src->content_lock);
        }
      else
        {
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
 * @param[in] req_ctx  Request context
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success.
 * @retval CACHE_INODE_NOT_FOUND if source object does not exist
 * @retval CACHE_INODE_ENTRY_EXISTS on collision.
 * @retval CACHE_INODE_BAD_TYPE if dir_src or dir_dest is not a
 *                              directory.
 */
cache_inode_status_t cache_inode_rename(cache_entry_t *dir_src,
					const char *oldname,
					cache_entry_t *dir_dest,
					const char *newname,
					struct req_op_context *req_ctx)
{
  fsal_status_t fsal_status = {0, 0};
  cache_entry_t *lookup_src = NULL;
  cache_entry_t *lookup_dst = NULL;
  cache_inode_status_t status = CACHE_INODE_SUCCESS;
  cache_inode_status_t status_ref_dir_src = CACHE_INODE_SUCCESS;
  cache_inode_status_t status_ref_dir_dst = CACHE_INODE_SUCCESS;
  cache_inode_status_t status_ref_dst = CACHE_INODE_SUCCESS;
  fsal_accessflags_t access_mask = 0;
  bool dir_src_access = false;

  if ((dir_src->type != DIRECTORY) ||
      (dir_dest->type != DIRECTORY)) {
      status = CACHE_INODE_NOT_A_DIRECTORY;
      goto out;
  }

  /* we must be able to both scan and write to both directories before we can proceed
   * sticky bit also applies to both files after looking them up.
   */
  access_mask = (FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                 FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD));

  status = cache_inode_access(dir_src,
                              access_mask,
                              req_ctx);
  if (status != CACHE_INODE_SUCCESS && status != CACHE_INODE_FSAL_EACCESS) {
       goto out;
  }
  if (status == CACHE_INODE_SUCCESS) {
       dir_src_access = true;
  }

  /* Check for object existence in source directory */
  PTHREAD_RWLOCK_rdlock(&dir_src->content_lock);
  status = cache_inode_lookup_impl(dir_src,
				   oldname,
				   req_ctx,
				   &lookup_src);
  PTHREAD_RWLOCK_unlock(&dir_src->content_lock);
  if (lookup_src == NULL) {
    /* If FSAL FH is stale, then this was managed in cache_inode_lookup */
    if(status != CACHE_INODE_FSAL_ESTALE)
      status = CACHE_INODE_NOT_FOUND;

    LogDebug(COMPONENT_CACHE_INODE,
             "Rename (%p,%s)->(%p,%s) : source doesn't exist",
             dir_src, oldname, dir_dest, newname);
    goto out;
  }

  if (!dir_src_access) {
      access_mask = FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE);

      status = cache_inode_access(lookup_src,
                                  access_mask,
                                  req_ctx);
      if (status != CACHE_INODE_SUCCESS) {
          goto out;
      }
  }

  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK) |
                    FSAL_ACE4_MASK_SET(lookup_src->type == DIRECTORY ?
                    FSAL_ACE_PERM_ADD_SUBDIRECTORY : FSAL_ACE_PERM_ADD_FILE);
  status = cache_inode_access(dir_dest,
                        access_mask,
                        req_ctx);
  if (status != CACHE_INODE_SUCCESS) {
      goto out;
  }

  /* Check for object existence in destination directory */
  PTHREAD_RWLOCK_rdlock(&dir_dest->content_lock);
  status = cache_inode_lookup_impl(dir_dest,
                                   newname,
                                   req_ctx,
                                   &lookup_dst);
  PTHREAD_RWLOCK_unlock(&dir_dest->content_lock);
  if(status == CACHE_INODE_NOT_FOUND)
    status = CACHE_INODE_SUCCESS;
  if (status != CACHE_INODE_SUCCESS) {
      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : dest error",
               dir_src, oldname, dir_dest, newname);
      goto out;
  }

  if (lookup_src == lookup_dst) {
      /* Nothing to do according to POSIX and NFS3/4
         If from and to both refer to the same file (they might be hard links
         of each other), then RENAME should perform no action and return success */
      goto out;
  }

  status = cache_inode_check_sticky(dir_src, lookup_src, req_ctx);
  if (status != CACHE_INODE_SUCCESS) {
      goto out;
  }

  if (lookup_dst) {
      status = cache_inode_check_sticky(dir_dest, lookup_dst, req_ctx);
      if (status != CACHE_INODE_SUCCESS) {
          goto out;
      }
  }

  /* Perform the rename operation in FSAL,
   * before doing anything in the cache.
   * Indeed, if the FSAL_rename fails unexpectly,
   * the cache would be inconsistent!
   */
  fsal_status = dir_src->obj_handle->ops->rename(dir_src->obj_handle, req_ctx,
					         oldname,
					         dir_dest->obj_handle,
					         newname);

  status_ref_dir_src = cache_inode_refresh_attrs_locked(dir_src, req_ctx);
  status_ref_dir_dst = cache_inode_refresh_attrs_locked(dir_dest, req_ctx);

  if (FSAL_IS_ERROR(fsal_status)) {
      status = cache_inode_error_convert(fsal_status);
      goto out;
  }

  if (lookup_dst) {
      /* Force a refresh of the overwritten inode */
      status_ref_dst = cache_inode_refresh_attrs_locked(lookup_dst, req_ctx);
      if (status_ref_dst == CACHE_INODE_FSAL_ESTALE) {
          status_ref_dst = CACHE_INODE_SUCCESS;
      }
  }

  if (((status = status_ref_dir_src) != CACHE_INODE_SUCCESS) ||
      ((status = status_ref_dir_dst) != CACHE_INODE_SUCCESS) ||
      ((status = status_ref_dst) != CACHE_INODE_SUCCESS)) {
      goto out;
  }

  src_dest_lock(dir_src, dir_dest);

  if(dir_src == dir_dest) {
      cache_inode_status_t tmp_status =
	      cache_inode_rename_cached_dirent(dir_dest, oldname, newname,
		      req_ctx);
      if(tmp_status != CACHE_INODE_SUCCESS) {
	  /* We're obviously out of date.  Throw out the cached
	     directory */
	  cache_inode_invalidate_all_cached_dirent(dir_dest);
      }
  } else {
      cache_inode_status_t tmp_status = CACHE_INODE_SUCCESS;

      /* We may have a cache entry for the destination
       * filename. 
       * If we do, we must delete it : it is stale.
       */
      cache_inode_remove_cached_dirent(dir_dest, newname, req_ctx);

      tmp_status = cache_inode_add_cached_dirent(dir_dest,
						 newname,
						 lookup_src,
						 NULL);
      if(tmp_status != CACHE_INODE_SUCCESS) {
	  /* We're obviously out of date.  Throw out the cached
	     directory */
	  cache_inode_invalidate_all_cached_dirent(dir_dest);
      }

      /* Remove the old entry */
      tmp_status = cache_inode_remove_cached_dirent(dir_src, oldname, req_ctx);
      if(tmp_status != CACHE_INODE_SUCCESS) {
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
