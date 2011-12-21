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
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_rename_cached_dirent: renames an entry in the same directory.
 *
 * Renames an entry in the same directory.
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be managed.
 * @param oldname [IN] name of the entry to rename.
 * @param newname [IN] new name for the entry
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */

cache_inode_status_t cache_inode_rename_cached_dirent(cache_entry_t * pentry_parent,
                                                      fsal_name_t * oldname,
                                                      fsal_name_t * newname,
                                                      hash_table_t * ht,
                                                      cache_inode_client_t * pclient,
                                                      cache_inode_status_t * pstatus)
{
  cache_entry_t *removed_pentry = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* BUGAZOMEU: Ne pas oublier de jarter un dir dont toutes les entrees sont inactives */
  if((removed_pentry = cache_inode_operate_cached_dirent(pentry_parent,
                                                         oldname,
                                                         newname,
                                                         pclient,
                                                         CACHE_INODE_DIRENT_OP_RENAME,
                                                         pstatus)) == NULL)
    return *pstatus;

  return *pstatus;
}                               /* cache_inode_rename_cached_dirent */

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
 * @param ht [INOUT] hash table used for the cache.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
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
                                        hash_table_t * ht,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus)
{
  cache_inode_status_t status;
  fsal_status_t fsal_status;
  cache_inode_dir_entry_t *new_dir_entry;
  cache_entry_t *pentry_lookup_src = NULL;
  cache_entry_t *pentry_lookup_dest = NULL;
  fsal_attrib_list_t attrlookup;
  fsal_attrib_list_t *pattrsrc;
  fsal_attrib_list_t *pattrdest;
  fsal_handle_t *phandle_dirsrc;
  fsal_handle_t *phandle_dirdest;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_RENAME] += 1;

  /* Are we working on directories ? */
  if((pentry_dirsrc->internal_md.type != DIRECTORY)
     || (pentry_dirdest->internal_md.type != DIRECTORY))
    {
      /* Bad type .... */
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;

      return *pstatus;
    }

  /* Must take locks on directories now,
   * because if another thread checks source and destination existence
   * in the same time, it will try to do the same checks...
   * and it will have the same conclusion !!!
   */

  /* Get the locks on bot pentry. If the same if used twice (as src and dest), take only one lock.
   * Lock are acquired has their related pentry are allocated (to avoid deadlocks) */
  if(pentry_dirsrc == pentry_dirdest)
    P_w(&pentry_dirsrc->lock);
  else
    {
      if(pentry_dirsrc < pentry_dirdest)
        {
          P_w(&pentry_dirsrc->lock);
          P_w(&pentry_dirdest->lock);
        }
      else
        {
          P_w(&pentry_dirdest->lock);
          P_w(&pentry_dirsrc->lock);
        }
    }

  /* Check for object existence in source directory */
  if((pentry_lookup_src = cache_inode_lookup_no_mutex(pentry_dirsrc,
                                                      poldname,
                                                      CACHE_INODE_JOKER_POLICY,
                                                      &attrlookup,
                                                      ht,
                                                      pclient,
                                                      pcontext, pstatus)) == NULL)
    {
      /* Source object does not exist */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;

      /* If FSAL FH is staled, then this was managed in cache_inode_lookup */
      if(*pstatus != CACHE_INODE_FSAL_ESTALE)
        *pstatus = CACHE_INODE_NOT_FOUND;

      V_w(&pentry_dirsrc->lock);
      if(pentry_dirsrc != pentry_dirdest)
        {
          V_w(&pentry_dirdest->lock);
        }

      if(*pstatus != CACHE_INODE_FSAL_ESTALE)
        LogDebug(COMPONENT_CACHE_INODE,
                 "Rename (%p,%s)->(%p,%s) : source doesn't exist",
                 pentry_dirsrc, poldname->name,
                 pentry_dirdest, pnewname->name);
      else
        LogDebug(COMPONENT_CACHE_INODE, "Rename : stale source");

      return *pstatus;
    }

  /* Check if an object with the new name exists in the destination directory */
  if((pentry_lookup_dest = cache_inode_lookup_no_mutex(pentry_dirdest,
                                                       pnewname,
                                                       CACHE_INODE_JOKER_POLICY,
                                                       &attrlookup,
                                                       ht,
                                                       pclient,
                                                       pcontext, pstatus)) != NULL)
    {

      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : destination already exists",
               pentry_dirsrc, poldname->name, pentry_dirdest, pnewname->name);

      /* If the already existing object is a directory, source object should ne a directory */
      if(pentry_lookup_dest->internal_md.type == DIRECTORY &&
         pentry_lookup_src->internal_md.type != DIRECTORY)
        {
          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          /* Return EISDIR */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;
          *pstatus = CACHE_INODE_IS_A_DIRECTORY;

          return *pstatus;
        }

      if(pentry_lookup_dest->internal_md.type != DIRECTORY &&
         pentry_lookup_src->internal_md.type == DIRECTORY)
        {
          /* Return ENOTDIR */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;
          *pstatus = CACHE_INODE_NOT_A_DIRECTORY;

          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          return *pstatus;
        }

      /* If caller wants to rename a file on himself, let it do it: return CACHE_INODE_SUCCESS but do nothing */
      if(pentry_lookup_dest == pentry_lookup_src)
        {
          /* There is in fact only one file (may be one of the arguments is a hard link to the other) */

          /* Return SUCCESS */
          pclient->stat.func_stats.nb_success[CACHE_INODE_RENAME] += 1;
          *pstatus = cache_inode_valid(pentry_dirdest, CACHE_INODE_OP_SET, pclient);

          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename (%p,%s)->(%p,%s) : rename the object on itself",
                   pentry_dirsrc, poldname->name, pentry_dirdest,
                   pnewname->name);

          return *pstatus;
        }

      /* Entry with the newname exists, if it is a non-empty directory, operation cannot be performed */
      if((pentry_lookup_dest->internal_md.type == DIRECTORY) &&
         (cache_inode_is_dir_empty(pentry_lookup_dest) != CACHE_INODE_SUCCESS))
        {
          /* The entry is a non-empty directory */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;
          *pstatus = CACHE_INODE_DIR_NOT_EMPTY;

          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename (%p,%s)->(%p,%s) : destination is a non-empty directory",
                   pentry_dirsrc, poldname->name, pentry_dirdest,
                   pnewname->name);
          return *pstatus;
        }

      /* get ride of this entry by trying removing it */

      status = cache_inode_remove_no_mutex(pentry_dirdest,
                                           pnewname,
                                           &attrlookup, ht, pclient, pcontext, pstatus);
      if(status != CACHE_INODE_SUCCESS)
        {
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;
          *pstatus = status;

          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          return *pstatus;
        }

    }                           /* if( pentry_lookup_dest != NULL ) */
  else
    {
      if(*pstatus == CACHE_INODE_FSAL_ESTALE)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "Rename : stale destnation");

          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          return *pstatus;
        }
    }

  /* Get the handle for the dirsrc pentry */

  if(pentry_dirsrc->internal_md.type == DIRECTORY)
    {
      phandle_dirsrc = &pentry_dirsrc->object.dir.handle;
      pattrsrc = &pentry_dirsrc->object.dir.attributes;
    }
  else
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;

      V_w(&pentry_dirsrc->lock);
      if(pentry_dirsrc != pentry_dirdest)
        {
          V_w(&pentry_dirdest->lock);
        }

      return *pstatus;
    }

  /* Get the handle for the dirdest pentry */

  if(pentry_dirdest->internal_md.type == DIRECTORY)
    {
      phandle_dirdest = &pentry_dirdest->object.dir.handle;
      pattrdest = &pentry_dirdest->object.dir.attributes;
    }
  else
    {
      V_w(&pentry_dirsrc->lock);
      if(pentry_dirsrc != pentry_dirdest)
        {
          V_w(&pentry_dirdest->lock);
        }

      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;

      V_w(&pentry_dirsrc->lock);
      if(pentry_dirsrc != pentry_dirdest)
        V_w(&pentry_dirdest->lock);

      return *pstatus;
    }

  /* Perform the rename operation in FSAL,
   * before doing anything in the cache.
   * Indeed, if the FSAL_rename fails unexpectly,
   * the cache would be inconsistent !
   */
#ifdef _USE_MFSL
  fsal_status = MFSL_rename(&pentry_dirsrc->mobject,
                            poldname,
                            &pentry_dirdest->mobject,
                            pnewname,
                            pcontext, &pclient->mfsl_context, pattrsrc, pattrdest, NULL);
#else
  fsal_status = FSAL_rename(phandle_dirsrc,
                            poldname,
                            phandle_dirdest, pnewname, pcontext, pattrsrc, pattrdest);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;

      V_w(&pentry_dirsrc->lock);
      if(pentry_dirsrc != pentry_dirdest)
        {
          V_w(&pentry_dirdest->lock);
        }

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;
          fsal_status_t getattr_status;

          LogEvent(COMPONENT_CACHE_INODE,
                   "cache_inode_rename: Stale FSAL File Handle detected for at least one in  pentry = %p and pentry = %p",
                   pentry_dirsrc, pentry_dirdest);

          /* Use FSAL_getattrs to find which entry is staled */
          getattr_status = FSAL_getattrs(phandle_dirsrc, pcontext, &attrlookup);
          if(getattr_status.major == ERR_FSAL_ACCESS)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_rename: Stale FSAL File Handle detected for pentry = %p",
                       pentry_dirsrc);

              if(cache_inode_kill_entry(pentry_dirsrc, WT_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_rename: Could not kill entry %p, status = %u",
                        pentry_dirsrc, kill_status);
            }

          getattr_status = FSAL_getattrs(phandle_dirdest, pcontext, &attrlookup);
          if( ( getattr_status.major == ERR_FSAL_ACCESS ) && ( pentry_dirdest != pentry_dirsrc ) )
            {
              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_rename: Stale FSAL File Handle detected for pentry = %p",
                       pentry_dirdest);

              if(cache_inode_kill_entry(pentry_dirdest, WT_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_rename: Could not kill entry %p, status = %u",
                        pentry_dirdest, kill_status);

            }

          *pstatus = CACHE_INODE_FSAL_ESTALE;

        }

      return *pstatus;
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
      /* if the rename operation is made within the same dir, then we use an optimization:
       * cache_inode_rename_dirent is used instead of adding/removing dirent. This limits
       * the use of resource in this case */

      LogDebug(COMPONENT_CACHE_INODE,
               "Rename (%p,%s)->(%p,%s) : source and target directory are the same",
               pentry_dirsrc, poldname->name, pentry_dirdest, pnewname->name);

      status = cache_inode_rename_cached_dirent(pentry_dirdest,
                                                poldname, pnewname, ht, pclient, pstatus);

      if(status != CACHE_INODE_SUCCESS)
        {
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;
          *pstatus = status;

          /* Unlock the pentry and exits */
          V_w(&pentry_dirsrc->lock);

          return *pstatus;
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
                                             ht, 
					     &new_dir_entry,
					     pclient, pcontext, pstatus);
      if(status != CACHE_INODE_SUCCESS)
        {
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;
          *pstatus = status;

          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          return *pstatus;
        }

      /* Remove the old entry */
      if(cache_inode_remove_cached_dirent(pentry_dirsrc,
                                          poldname,
                                          ht, pclient, &status) != CACHE_INODE_SUCCESS)
        {
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENAME] += 1;
          *pstatus = status;

          V_w(&pentry_dirsrc->lock);
          if(pentry_dirsrc != pentry_dirdest)
            {
              V_w(&pentry_dirdest->lock);
            }

          return *pstatus;
        }
    }

  /* Validate the entries */
  *pstatus = cache_inode_valid(pentry_dirsrc, CACHE_INODE_OP_SET, pclient);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_RENAME] += 1;
  else
    {
      *pstatus = cache_inode_valid(pentry_dirdest, CACHE_INODE_OP_SET, pclient);

      if(*pstatus != CACHE_INODE_SUCCESS)
        pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_RENAME] += 1;
      else
        pclient->stat.func_stats.nb_success[CACHE_INODE_RENAME] += 1;
    }

  /* unlock entries */

  V_w(&pentry_dirsrc->lock);
  if(pentry_dirsrc != pentry_dirdest)
    {
      V_w(&pentry_dirdest->lock);
    }

  return *pstatus;
}                               /* cache_inode_rename */
