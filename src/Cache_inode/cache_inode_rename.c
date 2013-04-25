/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    cache_inode_rename.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.20 $
 * \brief   Renames an entry.
 *
 * cache_inode_rename.c : Renames an entry.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "HashData.h"
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
    PTHREAD_RWLOCK_WRLOCK(&src->content_lock);
  else
    {
      if(src < dest)
        {
          PTHREAD_RWLOCK_WRLOCK(&src->content_lock);
          PTHREAD_RWLOCK_WRLOCK(&dest->content_lock);
        }
      else
        {
          PTHREAD_RWLOCK_WRLOCK(&dest->content_lock);
          PTHREAD_RWLOCK_WRLOCK(&src->content_lock);
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
      PTHREAD_RWLOCK_UNLOCK(&src->content_lock);
    }
  else
    {
      if(src < dest)
        {
          PTHREAD_RWLOCK_UNLOCK(&dest->content_lock);
          PTHREAD_RWLOCK_UNLOCK(&src->content_lock);
        }
      else
        {
          PTHREAD_RWLOCK_UNLOCK(&src->content_lock);
          PTHREAD_RWLOCK_UNLOCK(&dest->content_lock);
        }
    }
}

/**
 *
 * @brief Renames an entry
 *
 * This function calls the FSAL to rename a file, then mirrors the
 * operation in the cache.
 *
 * @param[in]  dir_src    The source directory
 * @param[in]  oldname    The current name of the file
 * @param[in]  dir_dest   The destination directory
 * @param[in]  newname    The name to be assigned to the object
 * @param[in]  context    FSAL credentials
 * @param[out] status     Returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success.
 * @retval CACHE_INODE_NOT_FOUND if source object does not exist
 * @retval CACHE_INODE_ENTRY_EXISTS on collision.
 * @retval CACHE_INODE_NOT_A_DIRECTORY if dir_src or dir_dest is not a
 *                              directory.
 *
 */
cache_inode_status_t cache_inode_rename(cache_entry_t *dir_src,
                                        fsal_name_t *oldname,
                                        cache_entry_t *dir_dest,
                                        fsal_name_t *newname,
                                        fsal_op_context_t *context,
                                        cache_inode_status_t *status)
{
  fsal_status_t fsal_status = {0, 0};
  cache_entry_t *lookup_src = NULL;
  cache_entry_t *lookup_dest = NULL;
  cache_inode_status_t status_ref_dir_src = CACHE_INODE_SUCCESS;
  cache_inode_status_t status_ref_dir_dst = CACHE_INODE_SUCCESS;
  cache_inode_status_t status_ref_dst = CACHE_INODE_SUCCESS;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *status = CACHE_INODE_SUCCESS;

  /* Are we working on directories ? */
  if ((dir_src->type != DIRECTORY) ||
      (dir_dest->type != DIRECTORY))
    {
      /* Bad type .... */
      *status = CACHE_INODE_NOT_A_DIRECTORY;
      goto out;
    }

  /* Check for . and .. on oldname and newname. */
  if(!FSAL_namecmp(oldname, (fsal_name_t *) &FSAL_DOT) ||
     !FSAL_namecmp(oldname, (fsal_name_t *) &FSAL_DOT_DOT) ||
     !FSAL_namecmp(newname, (fsal_name_t *) &FSAL_DOT) ||
     !FSAL_namecmp(newname, (fsal_name_t *) &FSAL_DOT_DOT))
    {
      *status = CACHE_INODE_BADNAME;
      goto out;
    }

  /* Check for object existence in source directory */
  if((lookup_src
      = cache_inode_lookup_impl(dir_src,
                                oldname,
                                context,
                                status)) == NULL) {
    /* If FSAL FH is stale, then this was managed in cache_inode_lookup */
    if(*status != CACHE_INODE_FSAL_ESTALE)
      *status = CACHE_INODE_NOT_FOUND;

    LogDebug(COMPONENT_CACHE_INODE,
             "Rename (%p,%s)->(%p,%s) : source doesn't exist",
             dir_src, oldname->name,
             dir_dest, newname->name);
    goto out;
  }

  /* Check if an object with the new name exists in the destination
     directory */
  if((lookup_dest
      = cache_inode_lookup_impl(dir_dest,
                                newname,
                                context,
                                status)) != NULL)
    {
      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : destination already exists",
               dir_src, oldname->name, dir_dest, newname->name);
    }
  else
    {
      if(*status == CACHE_INODE_FSAL_ESTALE)
        {
          LogEvent(COMPONENT_CACHE_INODE,
                   "Rename : stale destnation");

          goto out;
        }
    }

  LogFullDebug(COMPONENT_CACHE_INODE,
               "about to call FSAL_rename");

  /* Perform the rename operation in FSAL before doing anything in the cache.
   * Indeed, if the FSAL_rename fails unexpectly, the cache would be
   * inconsistent!
   *
   * We do almost no checking before making call because we want to return
   * error based on the files actually present in the directories, not what
   * we have in our cache.
   */
  fsal_status = FSAL_rename(&dir_src->handle, oldname,
                            &dir_dest->handle, newname,
                            context,
                            NULL,
                            NULL);

  LogFullDebug(COMPONENT_CACHE_INODE,
               "returned from FSAL_rename");

  /* Always refresh the attributes. */
  status_ref_dir_src = cache_inode_refresh_attrs_locked(dir_src, context);

  if(dir_src != dir_dest) {
      status_ref_dir_dst = cache_inode_refresh_attrs_locked(dir_dest, context);
  }

  LogFullDebug(COMPONENT_CACHE_INODE,
               "rdone refreshing attributes");

  if (FSAL_IS_ERROR(fsal_status)) {
      *status = cache_inode_error_convert(fsal_status);

      LogFullDebug(COMPONENT_CACHE_INODE,
                   "FSAL_rename failed with %s",
                   cache_inode_err_str(*status));

      goto out;
  }

  if (lookup_dest) {
      /* Force a refresh of the overwritten inode */
      status_ref_dst = cache_inode_refresh_attrs_locked(lookup_dest, context);
      if (status_ref_dst == CACHE_INODE_FSAL_ESTALE) {
          status_ref_dst = CACHE_INODE_SUCCESS;
      }
  }

  if (((*status = status_ref_dir_src) != CACHE_INODE_SUCCESS) ||
      ((*status = status_ref_dir_dst) != CACHE_INODE_SUCCESS) ||
      ((*status = status_ref_dst) != CACHE_INODE_SUCCESS)) {
      goto out;
  }

  /* Must take locks on directories now,
   * because if another thread checks source and destination existence
   * in the same time, it will try to do the same checks...
   * and it will have the same conclusion !!!
   */

  src_dest_lock(dir_src, dir_dest);

  if (lookup_dest) {
       /* Remove the entry from parent dir_entries avl */
       if(cache_inode_remove_cached_dirent(dir_dest,
                                           newname,
                                           &status_ref_dir_dst)
          != CACHE_INODE_SUCCESS)
         {
           LogDebug(COMPONENT_CACHE_INODE,
                    "remove entry failed with status %s",
                    cache_inode_err_str(status_ref_dir_dst));
           cache_inode_invalidate_all_cached_dirent(dir_dest,
                                                    &status_ref_dir_dst);
         }
  }

  if(dir_src == dir_dest)
    {
      /* if the rename operation is made within the same dir, then we
       * use an optimization: cache_inode_rename_dirent is used
       * instead of adding/removing dirent. This limits the use of
       * resource in this case */

      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : source and target directory are "
               "the same",
               dir_src, oldname->name, dir_dest, newname->name);

      *status = cache_inode_operate_cached_dirent(dir_src,
                                                  oldname,
                                                  newname,
                                                  CACHE_INODE_DIRENT_OP_RENAME);

      if(*status != CACHE_INODE_SUCCESS)
        {
          /* Unlock the pentry and exits */
          cache_inode_invalidate_all_cached_dirent(dir_src,
                                                   &status_ref_dir_src);
          goto out_unlock;
        }
    }
  else
    {
      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : moving entry",
               dir_src, oldname->name,
               dir_dest, newname->name);

      /* We may have a cache entry for the destination filename.
       * If we do, we must delete it, it is stale.
       */
      if(cache_inode_remove_cached_dirent(dir_dest,
                                          newname,
                                          &status_ref_dir_dst)
         != CACHE_INODE_SUCCESS)
        {
          cache_inode_invalidate_all_cached_dirent(dir_dest,
                                                   &status_ref_dir_dst);
          goto out_unlock;
        }

      /* Add the new entry */
      cache_inode_add_cached_dirent(dir_dest,
                                    newname,
                                    lookup_src,
                                    NULL,
                                    &status_ref_dir_dst);
      if(status_ref_dir_dst != CACHE_INODE_SUCCESS)
        {
          LogCrit(COMPONENT_CACHE_INODE,
                  "Add dirent returned %s",
                  cache_inode_err_str(*status));
          goto out_unlock;
        }

      /* Remove the old entry */
      if(cache_inode_remove_cached_dirent(dir_src,
                                          oldname,
                                          &status_ref_dir_src)
         != CACHE_INODE_SUCCESS)
        {
          cache_inode_invalidate_all_cached_dirent(dir_src,
                                                   &status_ref_dir_src);
          goto out_unlock;
        }
    }

out_unlock:

  /* unlock entries */
  src_dest_unlock(dir_src, dir_dest);

out:

  if (lookup_dest)
    {
      cache_inode_put(lookup_dest);
    }
  if (lookup_src)
    {
      cache_inode_put(lookup_src);
    }

  return *status;
} /* cache_inode_rename */
