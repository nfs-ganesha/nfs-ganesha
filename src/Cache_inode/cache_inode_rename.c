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
 * @param pentry_parent [INOUT] cache entry representing the directory to be managed.
 * @param oldname [IN] name of the entry to rename.
 * @param newname [IN] new name for the entry
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */

cache_inode_status_t cache_inode_rename_cached_dirent(cache_entry_t * pentry_parent,
                                                      fsal_name_t * oldname,
                                                      fsal_name_t * newname,
                                                      cache_inode_client_t * pclient,
                                                      cache_inode_status_t * pstatus)
{
  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  *pstatus = cache_inode_operate_cached_dirent(pentry_parent,
                                               oldname,
                                               newname,
                                               pclient,
                                               CACHE_INODE_DIRENT_OP_RENAME);

  return (*pstatus);
}                               /* cache_inode_rename_cached_dirent */

static inline void src_dest_lock(cache_entry_t *pentry_dirsrc,
                                 cache_entry_t *pentry_dirdest)
{
  /* Get the locks on both entries. If src and dest are the same, take
   * only one lock.  Locks are acquired with lowest cache_entry first
   * to avoid deadlocks. */

  if(pentry_dirsrc == pentry_dirdest)
    pthread_rwlock_wrlock(&pentry_dirsrc->content_lock);
  else
    {
      if(pentry_dirsrc < pentry_dirdest)
        {
          pthread_rwlock_wrlock(&pentry_dirsrc->content_lock);
          pthread_rwlock_wrlock(&pentry_dirdest->content_lock);
        }
      else
        {
          pthread_rwlock_wrlock(&pentry_dirdest->content_lock);
          pthread_rwlock_wrlock(&pentry_dirsrc->content_lock);
        }
    }
}

static inline void src_dest_unlock(cache_entry_t * pentry_dirsrc,
                                   cache_entry_t * pentry_dirdest)
{
  if(pentry_dirsrc == pentry_dirdest)
    {
      pthread_rwlock_unlock(&pentry_dirsrc->content_lock);
    }
  else
    {
      if(pentry_dirsrc < pentry_dirdest)
        {
          pthread_rwlock_unlock(&pentry_dirdest->content_lock);
          pthread_rwlock_unlock(&pentry_dirsrc->content_lock);
        }
      else
        {
          pthread_rwlock_unlock(&pentry_dirsrc->content_lock);
          pthread_rwlock_unlock(&pentry_dirdest->content_lock);
        }
    }
}

/**
 *
 * cache_inode_rename: renames an entry in the cache.
 *
 * Renames an entry in the cache. This operation is used for moving an object into a different directory.
 *
 * @param pentry_dirsrc [IN] entry pointer for the source directory
 * @param newname [IN] name of the object in the source directory
 * @param pentry_dirdest [INOUT] entry pointer for the destination directory in which the object will be moved.
 * @param newname [IN] name of the object in the destination directory
 * @param pattr_src [OUT] contains the source directory attributes if not NULL
 * @param pattr_dst [OUT] contains the destination directory attributes if not NULL
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param creds [IN] client user credentials 
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS  operation is a success \n
 * @return CACHE_INODE_LRU_ERROR allocation error occured when validating the entry\n
 * @return CACHE_INODE_NOT_FOUND source object does not exist
 *
 */
cache_inode_status_t cache_inode_rename(cache_entry_t * pentry_dirsrc,
                                        fsal_name_t * poldname,
                                        cache_entry_t * pentry_dirdest,
                                        fsal_name_t * pnewname,
                                        fsal_attrib_list_t * pattr_src,
                                        fsal_attrib_list_t * pattr_dst,
                                        cache_inode_client_t * pclient,
                                        struct user_cred *creds,
                                        cache_inode_status_t * pstatus)
{
  cache_inode_status_t status;
  fsal_status_t fsal_status;
  cache_entry_t *pentry_lookup_src = NULL;
  cache_entry_t *pentry_lookup_dest = NULL;
  fsal_attrib_list_t *pattrsrc;
  fsal_attrib_list_t *pattrdest;
  struct fsal_obj_handle *phandle_dirsrc = pentry_dirsrc->obj_handle;
  struct fsal_obj_handle *phandle_dirdest = pentry_dirdest->obj_handle;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Are we working on directories ? */
  if ((pentry_dirsrc->type != DIRECTORY)
      || (pentry_dirdest->type != DIRECTORY))
    {
      /* Bad type .... */
      *pstatus = CACHE_INODE_BAD_TYPE;
      goto out;
    }

  /* we must be able to both scan and write to both directories before we can proceed
   * sticky bit also applies to both files after looking them up.
   */
  fsal_status = phandle_dirsrc->ops->test_access(phandle_dirsrc,
						 creds,
						 FSAL_W_OK | FSAL_X_OK);
  if( !FSAL_IS_ERROR(fsal_status))
    fsal_status = phandle_dirdest->ops->test_access(phandle_dirdest,
						    creds,
						    FSAL_W_OK | FSAL_X_OK);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      goto out;
    }

  /* Must take locks on directories now,
   * because if another thread checks source and destination existence
   * in the same time, it will try to do the same checks...
   * and it will have the same conclusion !!!
   */

  src_dest_lock(pentry_dirsrc, pentry_dirdest);

  /* Check for object existence in source directory */
  if((pentry_lookup_src
      = cache_inode_lookup_impl(pentry_dirsrc,
                                poldname,
                                pclient,
                                pcontext,
                                pstatus)) == NULL) {
    /* If FSAL FH is staled, then this was managed in cache_inode_lookup */
    if(*pstatus != CACHE_INODE_FSAL_ESTALE)
      *pstatus = CACHE_INODE_NOT_FOUND;

    src_dest_unlock(pentry_dirsrc, pentry_dirdest);

    if(*pstatus != CACHE_INODE_FSAL_ESTALE)
      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : source doesn't exist",
               pentry_dirsrc, poldname->name,
               pentry_dirdest, pnewname->name);
    else
      LogDebug(COMPONENT_CACHE_INODE, "Rename : stale source");

    goto out;
  }
  if( !sticky_dir_allows(phandle_dirsrc,
			 pentry_lookup_src->obj_handle,
			 creds))
    {
      src_dest_unlock(pentry_dirsrc, pentry_dirdest);
      *pstatus = CACHE_INODE_FSAL_EPERM;
      goto out;
    }

  /* Check if an object with the new name exists in the destination
     directory */
  pentry_lookup_dest = cache_inode_lookup_impl(pentry_dirdest,
					       pnewname,
					       pclient,
					       pcontext,
					       pstatus);
  if( !sticky_dir_allows(phandle_dirdest,
			 (pentry_lookup_dest != NULL) ?
			 pentry_lookup_src->obj_handle : NULL,
			 creds))
    {
      src_dest_unlock(pentry_dirsrc, pentry_dirdest);
      *pstatus = CACHE_INODE_FSAL_EPERM;
      goto out;
    }
  if(pentry_lookup_dest  != NULL)
    {
      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : destination already exists",
               pentry_dirsrc, poldname->name, pentry_dirdest, pnewname->name);

      /* If the already existing object is a directory, source object
         should be a directory */
      if(pentry_lookup_dest->type == DIRECTORY &&
         pentry_lookup_src->type != DIRECTORY)
        {
          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          /* Return EISDIR */
          *pstatus = CACHE_INODE_IS_A_DIRECTORY;
          goto out;
        }

      if(pentry_lookup_dest->type != DIRECTORY &&
         pentry_lookup_src->type == DIRECTORY)
        {
          /* Return ENOTDIR */
          *pstatus = CACHE_INODE_NOT_A_DIRECTORY;
          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          goto out;
        }

      /* If caller wants to rename a file on himself, let it do it:
         return CACHE_INODE_SUCCESS but do nothing */
      if(pentry_lookup_dest == pentry_lookup_src)
        {
          /* There is in fact only one file (may be one of the
             arguments is a hard link to the other) */

          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename (%p,%s)->(%p,%s) : rename the object on itself",
                   pentry_dirsrc, poldname->name, pentry_dirdest,
                   pnewname->name);

          goto out;
        }

      /* Entry with the newname exists, if it is a non-empty
         directory, operation cannot be performed */
      if ((pentry_lookup_dest->type == DIRECTORY) &&
          (cache_inode_is_dir_empty(pentry_lookup_dest)
           != CACHE_INODE_SUCCESS))
        {
          /* The entry is a non-empty directory */
          *pstatus = CACHE_INODE_DIR_NOT_EMPTY;

          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename (%p,%s)->(%p,%s) : destination is a non-empty directory",
                   pentry_dirsrc, poldname->name, pentry_dirdest,
                   pnewname->name);
          goto out;
        }

      /* get rid of this entry by trying removing it */

      status = cache_inode_remove_impl(pentry_dirdest,
                                       pnewname,
                                       pclient,
                                       pcontext,
                                       pstatus,
                                       CACHE_INODE_FLAG_CONTENT_HAVE|CACHE_INODE_FLAG_CONTENT_HOLD);
      if (status != CACHE_INODE_SUCCESS)
        {
          *pstatus = status;
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename : unable to remove destination");

          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          goto out;
        }
    }                           /* if( pentry_lookup_dest != NULL ) */
  else
    {
      if(*pstatus == CACHE_INODE_FSAL_ESTALE)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename : stale destination");

          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          goto out;
        }
    }

  /* Get the handle for the dirsrc pentry */

  if(pentry_dirsrc->type == DIRECTORY)
    {
      phandle_dirsrc = pentry_dirsrc->obj_handle;
      pattrsrc = &pentry_dirsrc->obj_handle->attributes;
    }
  else
    {
      *pstatus = CACHE_INODE_BAD_TYPE;

      src_dest_unlock(pentry_dirsrc, pentry_dirdest);
      goto out;
    }

  /* Get the handle for the dirdest pentry */

  if(pentry_dirdest->type == DIRECTORY)
    {
      phandle_dirdest = pentry_dirdest->obj_handle;
      pattrdest = &pentry_dirdest->obj_handle->attributes;
    }
  else
    {
      src_dest_unlock(pentry_dirsrc, pentry_dirdest);
      *pstatus = CACHE_INODE_BAD_TYPE;

      goto out;
    }

  /* Perform the rename operation in FSAL,
   * before doing anything in the cache.
   * Indeed, if the FSAL_rename fails unexpectly,
   * the cache would be inconsistent !
   */
  fsal_status = phandle_dirsrc->ops->rename(phandle_dirsrc,
					    poldname,
					    phandle_dirdest,
					    pnewname);
  if( !FSAL_IS_ERROR(fsal_status))
	  fsal_status = phandle_dirsrc->ops->getattrs(phandle_dirsrc, pattrsrc);
  if( !FSAL_IS_ERROR(fsal_status))
	  fsal_status = phandle_dirdest->ops->getattrs(phandle_dirdest, pattrdest);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
           fsal_attrib_list_t attrs;

           attrs.asked_attributes = pclient->attrmask;
	   fsal_status = phandle_dirsrc->ops->getattrs(phandle_dirsrc, &attrs);
           if (fsal_status.major == ERR_FSAL_STALE) {
                cache_inode_kill_entry(pentry_dirsrc,
                                       pclient);
           }
           attrs.asked_attributes = pclient->attrmask;
           fsal_status = phandle_dirdest->ops->getattrs(phandle_dirdest, &attrs);
           if (fsal_status.major == ERR_FSAL_STALE) {
                cache_inode_kill_entry(pentry_dirdest,
                                       pclient);
           }
      }
      src_dest_unlock(pentry_dirsrc, pentry_dirdest);
      goto out;
    }

  /* Manage the returned attributes */
  if(pattr_src != NULL)
    *pattr_src = *pattrsrc;

  if(pattr_dst != NULL)
    *pattr_dst = *pattrdest;

  /* At this point, we know that:
   *  - both pentry_dir_src and pentry_dir_dest are directories
   *  - pentry_dir_src/oldname exists
   *  - pentry_dir_dest/newname does not exists or has just been removed */

  if(pentry_dirsrc == pentry_dirdest)
    {
      /* if the rename operation is made within the same dir, then we
       * use an optimization: cache_inode_rename_dirent is used
       * instead of adding/removing dirent. This limits the use of
       * resource in this case */

      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : source and target directory are the same",
               pentry_dirsrc, poldname->name, pentry_dirdest, pnewname->name);

      status = cache_inode_rename_cached_dirent(pentry_dirdest,
                                                poldname, pnewname, pclient, pstatus);

      if(status != CACHE_INODE_SUCCESS)
        {
          *pstatus = status;
          /* Unlock the pentry and exits */
          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          goto out;
        }
    }
  else
    {
      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : moving entry",
               pentry_dirsrc, poldname->name,
               pentry_dirdest, pnewname->name);

      /* Add the new entry */
      status = cache_inode_add_cached_dirent(pentry_dirdest,
                                             pnewname,
                                             pentry_lookup_src,
                                             NULL,
                                             pclient, pcontext, pstatus);
      if(status != CACHE_INODE_SUCCESS)
        {
          *pstatus = status;
          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          goto out;
        }

      /* Remove the old entry */
      if(cache_inode_remove_cached_dirent(pentry_dirsrc,
                                          poldname,
                                          pclient, &status)
         != CACHE_INODE_SUCCESS)
        {
          *pstatus = status;
          src_dest_unlock(pentry_dirsrc, pentry_dirdest);
          goto out;
        }
    }

  /* unlock entries */
  src_dest_unlock(pentry_dirsrc, pentry_dirdest);

out:

  if (pentry_lookup_dest)
    {
      cache_inode_put(pentry_lookup_dest, pclient);
    }
  if (pentry_lookup_src)
    {
      cache_inode_put(pentry_lookup_src, pclient);
    }

  return *pstatus;
}                               /* cache_inode_rename */
