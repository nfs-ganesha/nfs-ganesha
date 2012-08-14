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

#include "LRU_List.h"
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
 *
 * @brief Renames an entry in the same directory.
 *
 * Renames an entry in the same directory.
 *
 * @param[in,out] parent The directory to be managed
 * @param[in]    oldname The name of the entry to rename
 * @param[in]    newname The new name for the entry
 * @param[out]   status  returned status.
 *
 * @return the same as *status
 *
 */

cache_inode_status_t
cache_inode_rename_cached_dirent(cache_entry_t *parent,
                                 fsal_name_t *oldname,
                                 fsal_name_t *newname,
                                 cache_inode_status_t *status)
{
  /* Set the return default to CACHE_INODE_SUCCESS */
  *status = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(parent->type != DIRECTORY)
    {
      *status = CACHE_INODE_BAD_TYPE;
      return *status;
    }

  *status = cache_inode_operate_cached_dirent(parent,
                                              oldname,
                                              newname,
                                              CACHE_INODE_DIRENT_OP_RENAME);

  return (*status);
}                               /* cache_inode_rename_cached_dirent */

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
    pthread_rwlock_wrlock(&src->content_lock);
  else
    {
      if(src < dest)
        {
          pthread_rwlock_wrlock(&src->content_lock);
          pthread_rwlock_wrlock(&dest->content_lock);
        }
      else
        {
          pthread_rwlock_wrlock(&dest->content_lock);
          pthread_rwlock_wrlock(&src->content_lock);
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
      pthread_rwlock_unlock(&src->content_lock);
    }
  else
    {
      if(src < dest)
        {
          pthread_rwlock_unlock(&dest->content_lock);
          pthread_rwlock_unlock(&src->content_lock);
        }
      else
        {
          pthread_rwlock_unlock(&src->content_lock);
          pthread_rwlock_unlock(&dest->content_lock);
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
 * @param[out] attr_src   Source directory attributes after operation
 * @param[out] pattr_dest Destination directory attributes after operation
 * @param[in]  context    FSAL credentials
 * @param[out] status     Returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success.
 * @retval CACHE_INODE_NOT_FOUND if source object does not exist
 * @retval CACHE_INODE_ENTRY_EXISTS on collision.
 * @retval CACHE_INODE_BAD_TYPE if dir_src or dir_dest is not a
 *                              directory.
 *
 */
cache_inode_status_t cache_inode_rename(cache_entry_t *dir_src,
                                        fsal_name_t *oldname,
                                        cache_entry_t *dir_dest,
                                        fsal_name_t *newname,
                                        fsal_attrib_list_t *attr_src,
                                        fsal_attrib_list_t *attr_dest,
                                        fsal_op_context_t *context,
                                        cache_inode_status_t *status)
{
  fsal_status_t fsal_status = {0, 0};
  cache_entry_t *lookup_src = NULL;
  cache_entry_t *lookup_dest = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *status = CACHE_INODE_SUCCESS;

  /* Are we working on directories ? */
  if ((dir_src->type != DIRECTORY) ||
      (dir_dest->type != DIRECTORY))
    {
      /* Bad type .... */
      *status = CACHE_INODE_BAD_TYPE;
      goto out;
    }

  /* Must take locks on directories now,
   * because if another thread checks source and destination existence
   * in the same time, it will try to do the same checks...
   * and it will have the same conclusion !!!
   */

  src_dest_lock(dir_src, dir_dest);

  /* Check for object existence in source directory */
  if((lookup_src
      = cache_inode_lookup_impl(dir_src,
                                oldname,
                                context,
                                status)) == NULL) {
    /* If FSAL FH is stale, then this was managed in cache_inode_lookup */
    if(*status != CACHE_INODE_FSAL_ESTALE)
      *status = CACHE_INODE_NOT_FOUND;

    src_dest_unlock(dir_src, dir_dest);

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

      /* If the already existing object is a directory, source object
         should be a directory */
      if(lookup_dest->type == DIRECTORY &&
         lookup_src->type != DIRECTORY)
        {
          src_dest_unlock(dir_src, dir_dest);
          /* Return EISDIR */
          *status = CACHE_INODE_IS_A_DIRECTORY;
          goto out;
        }

      if(lookup_dest->type != DIRECTORY &&
         lookup_src->type == DIRECTORY)
        {
          /* Return ENOTDIR */
          *status = CACHE_INODE_NOT_A_DIRECTORY;
          src_dest_unlock(dir_src, dir_dest);
          goto out;
        }

      /* If caller wants to rename a file on himself, let it do it:
         return CACHE_INODE_SUCCESS but do nothing */
      if(lookup_dest == lookup_src)
        {
          /* There is in fact only one file (may be one of the
             arguments is a hard link to the other) */

          src_dest_unlock(dir_src, dir_dest);
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename (%p,%s)->(%p,%s) : rename the object on itself",
                   dir_src, oldname->name, dir_dest, newname->name);

          goto out;
        }

      /* Entry with the newname exists, if it is a non-empty
         directory, operation cannot be performed */
      if ((lookup_dest->type == DIRECTORY) &&
          (cache_inode_is_dir_empty(lookup_dest)
           != CACHE_INODE_SUCCESS))
        {
          /* The entry is a non-empty directory */
          *status = CACHE_INODE_DIR_NOT_EMPTY;

          src_dest_unlock(dir_src, dir_dest);
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename (%p,%s)->(%p,%s) : destination is a non-empty "
                   "directory",
                   dir_src, oldname->name, dir_dest, newname->name);
          goto out;
        }

      /* get rid of this entry by trying removing it */

      cache_inode_remove_impl(dir_dest,
                              newname,
                              context,
                              status,
                              CACHE_INODE_FLAG_CONTENT_HAVE |
                              CACHE_INODE_FLAG_CONTENT_HOLD);
      if (*status != CACHE_INODE_SUCCESS)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename : unable to remove destination");

          src_dest_unlock(dir_src, dir_dest);
          goto out;
        }
    }
  else
    {
      if(*status == CACHE_INODE_FSAL_ESTALE)
        {
          LogEvent(COMPONENT_CACHE_INODE,
                   "Rename : stale destnation");

          src_dest_unlock(dir_src, dir_dest);
          goto out;
        }
    }

  if(dir_src->type != DIRECTORY)
    {
      *status = CACHE_INODE_BAD_TYPE;
      src_dest_unlock(dir_src, dir_dest);
      goto out;
    }

  if(dir_dest->type != DIRECTORY)
    {
      src_dest_unlock(dir_src, dir_dest);
      *status = CACHE_INODE_BAD_TYPE;
      goto out;
    }

  /* Perform the rename operation in FSAL,
   * before doing anything in the cache.
   * Indeed, if the FSAL_rename fails unexpectly,
   * the cache would be inconsistent!
   */
  fsal_status = FSAL_rename(&dir_src->handle, oldname,
                            &dir_dest->handle, newname,
                            context,
                            &dir_src->attributes,
                            &dir_dest->attributes);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *status = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE)
        {
          fsal_attrib_list_t attrs;
          attrs.asked_attributes = cache_inode_params.attrmask;
          fsal_status = FSAL_getattrs(&dir_src->handle,
                                      context,
                                      &attrs);
          if (fsal_status.major == ERR_FSAL_STALE)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                 "FSAL returned STALE on rename, source");
              cache_inode_kill_entry(dir_src);
            }
          attrs.asked_attributes = cache_inode_params.attrmask;
          fsal_status = FSAL_getattrs(&dir_dest->handle,
                                      context,
                                      &attrs);
          if (fsal_status.major == ERR_FSAL_STALE)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                 "FSAL returned STALE on rename, destination");
              cache_inode_kill_entry(dir_dest);
            }
        }
      src_dest_unlock(dir_src, dir_dest);
      goto out;
    }

  /* Manage the returned attributes */
  if(attr_src != NULL)
    *attr_src = dir_src->attributes;

  if(attr_dest != NULL)
    *attr_dest = dir_dest->attributes;

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

      cache_inode_rename_cached_dirent(dir_dest, oldname,
                                       newname, status);

      if(*status != CACHE_INODE_SUCCESS)
        {
          /* Unlock the pentry and exits */
          src_dest_unlock(dir_src, dir_dest);
          goto out;
        }
    }
  else
    {
      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : moving entry",
               dir_src, oldname->name,
               dir_dest, newname->name);

      /* Add the new entry */
      cache_inode_add_cached_dirent(dir_dest,
                                    newname,
                                    lookup_src,
                                    NULL,
                                    status);
      if(*status != CACHE_INODE_SUCCESS)
        {
          src_dest_unlock(dir_src, dir_dest);
          goto out;
        }

      /* Remove the old entry */
      if(cache_inode_remove_cached_dirent(dir_src,
                                          oldname,
                                          status)
         != CACHE_INODE_SUCCESS)
        {
          src_dest_unlock(dir_src, dir_dest);
          goto out;
        }
    }

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
