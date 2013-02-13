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
  if(parent->type != DIRECTORY)
    {
      status = CACHE_INODE_BAD_TYPE;
      return status;
    }

  status = cache_inode_operate_cached_dirent(parent,
                                              oldname,
                                              newname,
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
  struct fsal_obj_handle *handle_dirsrc = dir_src->obj_handle;
  struct fsal_obj_handle *handle_dirdest = dir_dest->obj_handle;
  struct fsal_obj_handle *handle_lookup = NULL;
  cache_inode_status_t status = CACHE_INODE_SUCCESS;

  /* Are we working on directories ? */
  if ((dir_src->type != DIRECTORY) ||
      (dir_dest->type != DIRECTORY))
    {
      /* Bad type .... */
      status = CACHE_INODE_BAD_TYPE;
      goto out;
    }

  /* we must be able to both scan and write to both directories before we can proceed
   * sticky bit also applies to both files after looking them up.
   */
  fsal_status = handle_dirsrc->ops->test_access(handle_dirsrc,
						req_ctx,
						FSAL_W_OK | FSAL_X_OK);
  if (!FSAL_IS_ERROR(fsal_status))
    fsal_status = handle_dirdest->ops->test_access(handle_dirdest,
						   req_ctx,
						   FSAL_W_OK | FSAL_X_OK);
  if(FSAL_IS_ERROR(fsal_status))
    {
      status = cache_inode_error_convert(fsal_status);
      goto out;
    }

  /* Must take locks on directories now,
   * because if another thread checks source and destination existence
   * in the same time, it will try to do the same checks...
   * and it will have the same conclusion !!!
   */

  src_dest_lock(dir_src, dir_dest);

  /* Check for object existence in source directory */
  status = cache_inode_lookup_impl(dir_src,
				   oldname,
				   req_ctx,
				   &lookup_src);
  if (lookup_src == NULL) {
    /* If FSAL FH is stale, then this was managed in cache_inode_lookup */
    if(status != CACHE_INODE_FSAL_ESTALE)
      status = CACHE_INODE_NOT_FOUND;

    src_dest_unlock(dir_src, dir_dest);

    LogDebug(COMPONENT_CACHE_INODE,
             "Rename (%p,%s)->(%p,%s) : source doesn't exist",
             dir_src, oldname, dir_dest, newname);
    goto out;
  }

  handle_lookup = lookup_src->obj_handle;

  if (!sticky_dir_allows(handle_dirsrc,
                         lookup_src->obj_handle,
                         req_ctx->creds))
    {
      src_dest_unlock(dir_src, dir_dest);
      status = CACHE_INODE_FSAL_EPERM;
      goto out;
    }

  /* Perform the rename operation in FSAL,
   * before doing anything in the cache.
   * Indeed, if the FSAL_rename fails unexpectly,
   * the cache would be inconsistent!
   */
  fsal_status = handle_dirsrc->ops->rename(handle_dirsrc, req_ctx,
					   oldname,
					   handle_dirdest,
					   newname);

  if (!FSAL_IS_ERROR(fsal_status))
    fsal_status = handle_dirsrc->ops->getattrs(handle_dirsrc, req_ctx);
  if (!FSAL_IS_ERROR(fsal_status))
    fsal_status = handle_dirdest->ops->getattrs(handle_dirdest, req_ctx);
  if (!FSAL_IS_ERROR(fsal_status))
    {
      /* Force a refresh of the link count in the case of renaming
	 one hardlink to another */
      PTHREAD_RWLOCK_wrlock(&lookup_src->attr_lock);
      fsal_status = handle_lookup->ops->getattrs(handle_lookup, req_ctx);
      PTHREAD_RWLOCK_unlock(&lookup_src->attr_lock);
    }
  
  if (FSAL_IS_ERROR(fsal_status))
    {
      status = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
           fsal_status = handle_dirsrc->ops->getattrs(handle_dirsrc, req_ctx);
           if (fsal_status.major == ERR_FSAL_STALE) {
                cache_inode_kill_entry(dir_src);
           }
           fsal_status = handle_dirdest->ops->getattrs(handle_dirdest,
						       req_ctx);
           if (fsal_status.major == ERR_FSAL_STALE) {
                cache_inode_kill_entry(dir_dest);
           }
      }
      src_dest_unlock(dir_src, dir_dest);
      goto out;
    }

  if(dir_src == dir_dest)
    {
      cache_inode_status_t tmp_status = cache_inode_rename_cached_dirent(dir_dest, oldname,
									 newname);
      if(tmp_status != CACHE_INODE_SUCCESS)
        {
	  /* We're obviously out of date.  Throw out the cached
	     directory */
	  cache_inode_invalidate_all_cached_dirent(dir_dest);
	}
    }
  else
    {
      cache_inode_status_t tmp_status = CACHE_INODE_SUCCESS;

      /* We may have a cache entry for the destination
       * filename. 
       * If we do, we must delete it : it is stale.
       */
      cache_inode_remove_cached_dirent(dir_dest, newname);

      tmp_status = cache_inode_add_cached_dirent(dir_dest,
						 newname,
						 lookup_src,
						 NULL);
      if(tmp_status != CACHE_INODE_SUCCESS)
        {
	  /* We're obviously out of date.  Throw out the cached
	     directory */
	  cache_inode_invalidate_all_cached_dirent(dir_dest);
        }

      /* Remove the old entry */
      tmp_status = cache_inode_remove_cached_dirent(dir_src,
						    oldname);
      if(tmp_status != CACHE_INODE_SUCCESS)
        {
	  cache_inode_invalidate_all_cached_dirent(dir_src);
        }
    }

  /* unlock entries */
  src_dest_unlock(dir_src, dir_dest);

out:

  if (lookup_src)
    {
      cache_inode_put(lookup_src);
    }

  return status;
}
/** @} */
