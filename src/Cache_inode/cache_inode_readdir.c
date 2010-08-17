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
 * \file    cache_inode_readdir.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:05 $
 * \version $Revision: 1.50 $
 * \brief   Reads the content of a directory. 
 *
 * cache_inode_readdir.c : Reads the content of a directory. Contains also the needed function for directory browsing.
 *
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
#include "stuff_alloc.h"
#include "fsal.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_operate_cached_dirent: locates a dirent in the cached dirent, and perform an operation on it.
 *
 * Looks up for an dirent in the cached dirent. Thus function searches only in the entries
 * listed in the dir_entries array. Some entries may be missing but existing and not be cached (it no 
 * readdir was never performed on the entry for example. This function provides a way to operate on the dirent.
 *
 * @param pentry_parent [IN] directory entry to be looked. 
 * @param name [IN] name for the searched entry.
 * @param newname [IN] newname if function is used to rename a dirent
 * @param dirent_op [IN] operation (ADD, LOOKUP or REMOVE) to do on the dirent if found. 
 * @pstatus [OUT] returned status. 
 * 
 * @return the found entry if its exists and NULL if it is not in the dirent arrays. REMOVE always returns NULL.
 *
 */
cache_entry_t *cache_inode_operate_cached_dirent(cache_entry_t * pentry_parent,
                                                 fsal_name_t * pname,
                                                 fsal_name_t * newname,
                                                 cache_inode_dirent_op_t dirent_op,
                                                 cache_inode_status_t * pstatus)
{
  cache_entry_t *pdir_chain = NULL;
  cache_entry_t *pentry = NULL;
  fsal_status_t fsal_status;
  int i = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIR_BEGINNING &&
     pentry_parent->internal_md.type != DIR_CONTINUE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;

      return NULL;
    }

  /* Try to look into the dir and its dir_cont. At this point, it must be said than lock on dir_cont are
   *  taken when a lock is previously acquired on the related dir_begin */
  pdir_chain = pentry_parent;

  do
    {
      /* Is this entry known ? */
      if(pdir_chain->internal_md.type == DIR_BEGINNING)
        {
          for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
            {

              LogFullDebug(COMPONENT_CACHE_INODE, "DIR_BEGINNING %d | %d | %s | %s\n",
                     pdir_chain->object.dir_begin.pdir_data->dir_entries[i].active,
                     pdir_chain->object.dir_begin.pdir_data->dir_entries[i].pentry->
                     internal_md.valid_state, pname->name,
                     pdir_chain->object.dir_begin.pdir_data->dir_entries[i].name.name);

              if(pdir_chain->object.dir_begin.pdir_data->dir_entries[i].active == VALID
                 && pdir_chain->object.dir_begin.pdir_data->dir_entries[i].pentry->
                 internal_md.valid_state == VALID
                 && !FSAL_namecmp(pname,
                                  &(pdir_chain->object.dir_begin.pdir_data->
                                    dir_entries[i].name)))
                {
                  /* Entry was found */
                  pentry = pdir_chain->object.dir_begin.pdir_data->dir_entries[i].pentry;
                  *pstatus = CACHE_INODE_SUCCESS;
                  break;
                }
            }

          if(pentry != NULL)
            break;              /* Exit the do...while loop */

          /* Do we have to go on browsing the cache_inode ? */
          /* We have to check if eod is reached and no pentry found */
          if(pdir_chain->object.dir_begin.end_of_dir == END_OF_DIR)
            {
              pentry = NULL;
              *pstatus = CACHE_INODE_NOT_FOUND;
              break;
            }

          /* Next step, release the lock and acquire a new one */
          pdir_chain = pdir_chain->object.dir_begin.pdir_cont;
        }
      else
        {
          /* Entry is no DIR_BEGINNING, it is of type DIR_CONTINUE */
          for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
            {
              /*
                 printf( "DIR_CONTINUE %d | %d | %s | %s\n", 
                 pdir_chain->object.dir_cont.pdir_data->dir_entries[i].active, 
                 pdir_chain->object.dir_cont.pdir_data->dir_entries[i].pentry->internal_md.valid_state, 
                 name.name, 
                 pdir_chain->object.dir_cont.pdir_data->dir_entries[i].name.name ) ; */

              if(pdir_chain->object.dir_cont.pdir_data->dir_entries[i].active == VALID &&
                 pdir_chain->object.dir_cont.pdir_data->dir_entries[i].pentry->
                 internal_md.valid_state == VALID
                 && !FSAL_namecmp(pname,
                                  &(pdir_chain->object.dir_cont.pdir_data->
                                    dir_entries[i].name)))
                {
                  /* Entry was found */
                  pentry = pdir_chain->object.dir_cont.pdir_data->dir_entries[i].pentry;
                  *pstatus = CACHE_INODE_SUCCESS;
                  break;
                }
            }

          if(pentry != NULL)
            break;              /* Exit the do...while loop */

          /* Do we have to go on browsing the cache_inode ? */
          if(pdir_chain->object.dir_cont.end_of_dir == END_OF_DIR)
            {
              pentry = NULL;
              *pstatus = CACHE_INODE_NOT_FOUND;
              break;
            }
          /* Next step */
          pdir_chain = pdir_chain->object.dir_cont.pdir_cont;
        }

    }
  while(pentry == NULL);

  /* Did we find something */
  if(pentry != NULL)
    {
      /* Yes, we did ! */
      switch (dirent_op)
        {
        case CACHE_INODE_DIRENT_OP_REMOVE:
          /* Related DIR_BEGINNING or DIR_CONTINUE is pointed by pdir_chain, entry is the i-th is dir_entries 
           * The dirent entry is removed by being set invalid */

          if(pdir_chain->internal_md.type == DIR_BEGINNING)
            {
              pdir_chain->object.dir_begin.pdir_data->dir_entries[i].active = INVALID;
              pdir_chain->object.dir_begin.nbactive -= 1;
              *pstatus = CACHE_INODE_SUCCESS;
            }
          else if(pdir_chain->internal_md.type == DIR_CONTINUE)
            {
              pdir_chain->object.dir_cont.pdir_data->dir_entries[i].active = INVALID;
              pdir_chain->object.dir_cont.nbactive -= 1;
              *pstatus = CACHE_INODE_SUCCESS;
            }
          else
            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
          break;

        case CACHE_INODE_DIRENT_OP_RENAME:
          /* Entry to rename is the i-th in pdir_chain */
          if(pdir_chain->internal_md.type == DIR_BEGINNING)
            {
              fsal_status =
                  FSAL_namecpy(&
                               (pdir_chain->object.dir_begin.pdir_data->dir_entries[i].
                                name), newname);
            }
          else
            {
              fsal_status =
                  FSAL_namecpy(&
                               (pdir_chain->object.dir_cont.pdir_data->dir_entries[i].
                                name), newname);
            }

          if(FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);
            }
          else
            {
              *pstatus = CACHE_INODE_SUCCESS;
            }
          break;

        default:
          /* Should never occurs, in any case, it cost nothing to handle this situation */
          *pstatus = CACHE_INODE_INVALID_ARGUMENT;
          break;

        }                       /* switch */
    }

  /* Last lock released */

  return pentry;
}                               /* cache_inode_operate_cached_dirent */

#ifdef _TOTO
/**
 *
 * cache_inode_lookup_cached_dirent: looks up for an dirent in the cached dirent.
 *
 * Looks up for an dirent in the cached dirent. Thus function searches only in the entries
 * listed in the dir_entries array. Some entries may be missing but existing and not be cached (it no 
 * readdir was never performed on the entry for example.
 *
 * @param pentry_parent [IN] directory entry to be looked. 
 * @param name [IN] name for the searched entry.
 * @pstatus [OUT] returned status. CACHE_INODE_SUCCESS=entry found, CACHE_INODE_NOT_FOUND=entry is not in the dirent
 * 
 * @return the found entry if its exists and NULL if it is not in the dirent arrays.
 *
 */
cache_entry_t *cache_inode_lookup_cached_dirent(cache_entry_t * pentry_parent,
                                                fsal_name_t * pname,
                                                cache_inode_status_t * pstatus)
{
  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIR_BEGINNING &&
     pentry_parent->internal_md.type != DIR_CONTINUE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return NULL;
    }
  return cache_inode_operate_cached_dirent(pentry_parent, *pname, NULL,
                                           CACHE_INODE_DIRENT_OP_LOOKUP, pstatus);
}                               /* cache_inode_lookup_cached_dirent */
#endif

/**
 *
 * cache_inode_add_cached_dirent: Adds a directory entry to a cached directory.
 *
 * Adds a directory entry to a cached directory. This is use when creating a new entry
 * through nfs and keep it to the cache. It also allocates and caches the entry.
 * This function can be call iteratively, within a loop (like what is done in cache_inode_readdir_populate).
 * In this case, pentry_parent should be set to the value returned in *pentry_next. 
 * This function should never be used for managing a junction.
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be managed.
 * @param name          [IN]    name of the entry to add.
 * @param pentry_added  [IN]    the pentry added to the dirent array
 * @param pentry_next   [OUT]   the next pentry to use for next call. 
 * @param ht            [IN]    hash table used for the cache, unused in this call.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus       [OUT]   returned status.
 *
 * @return the DIR_CONTINUE or DIR_BEGINNING that contain this entry in its array_dirent\n
 * @return NULL if failed, see *pstatus for error's meaning.
 *
 */
cache_inode_status_t cache_inode_add_cached_dirent(cache_entry_t * pentry_parent,
                                                   fsal_name_t * pname,
                                                   cache_entry_t * pentry_added,
                                                   cache_entry_t ** ppentry_next,
                                                   hash_table_t * ht,
                                                   cache_inode_client_t * pclient,
                                                   fsal_op_context_t * pcontext,
                                                   cache_inode_status_t * pstatus)
{
  cache_entry_t *pdir_chain = NULL;
  cache_entry_t *pentry = NULL;
  fsal_status_t fsal_status;
  cache_inode_fsal_data_t fsdata;
  cache_inode_parent_entry_t *next_parent_entry = NULL;

  int i = 0;
  int slot_index = 0;

  /* For the moment, we add no error...
   * Get ride of former *pstatus value */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIR_BEGINNING &&
     pentry_parent->internal_md.type != DIR_CONTINUE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* We don't known where to write, we have to seek for an empty place */
  /* Search loop. We look for an empty slot in a dirent array */
  pdir_chain = pentry_parent;
  do
    {

      if(pdir_chain->internal_md.type == DIR_BEGINNING)
        {
          /* DIR_BEGINNING management */

          if(pdir_chain->object.dir_begin.nbactive != CHILDREN_ARRAY_SIZE)
            {
              for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
                {
                  if(pdir_chain->object.dir_begin.pdir_data->dir_entries[i].active ==
                     INVALID
                     || pdir_chain->object.dir_begin.pdir_data->dir_entries[i].pentry ==
                     NULL)
                    {
                      /* slot found */
                      pentry = pdir_chain;
                      slot_index = i;
                      break;
                    }
                }
            }

          /* Next step */
          if(pdir_chain->object.dir_begin.end_of_dir == END_OF_DIR)
            {
              /* Entry was not found, break the main loop */
              break;
            }
          else
            {
              pdir_chain = pdir_chain->object.dir_begin.pdir_cont;
            }

        }
      else if(pdir_chain->internal_md.type == DIR_CONTINUE)
        {
          /* DIR_CONTINUE management */

          if(pdir_chain->object.dir_cont.nbactive != CHILDREN_ARRAY_SIZE)
            {
              for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
                {
                  if(pdir_chain->object.dir_cont.pdir_data->dir_entries[i].active ==
                     INVALID
                     || pdir_chain->object.dir_cont.pdir_data->dir_entries[i].pentry ==
                     NULL)
                    {
                      pentry = pdir_chain;
                      slot_index = i;
                      break;
                    }
                }
            }

          /* Next step */
          if(pdir_chain->object.dir_cont.end_of_dir == END_OF_DIR)
            {
              /* Entry was not found, break the main loop */
              break;
            }
          else
            {
              pdir_chain = pdir_chain->object.dir_cont.pdir_cont;
            }
        }
      else
        {
          /* entry not found */
	  LogCrit(COMPONENT_CACHE_INODE, 
              "cache_inode_add_cached_dirent: Critical Warning: a non-directory type has been detected in a dir_chain !!!");
          *pstatus = CACHE_INODE_BAD_TYPE;
          return *pstatus;
        }

    }
  while(pentry == NULL);

  /* If pentry is NULL, all the dirent are full and a new entry is needed */
  if(pentry == NULL)
    {
      /* Pointer pentry is NULL, a new entry DIR_CONTINUE is required */

      /* There may be previously invalidated dirents, in this case pdir_cont already exists
       * we won't allocate new things in this case and reuse the old ones 
       * This case is identified by pdir_chain->object.*.pdir_cont != NULL 
       */
      switch (pdir_chain->internal_md.type)
        {
        case DIR_BEGINNING:
          fsdata.handle = pdir_chain->object.dir_begin.handle;
          fsdata.cookie = 1;
          pentry = pdir_chain->object.dir_begin.pdir_cont;
          break;

        case DIR_CONTINUE:
          fsdata.handle = pdir_chain->object.dir_cont.pdir_begin->object.dir_begin.handle;
          fsdata.cookie = pdir_chain->object.dir_cont.dir_cont_pos + 1;
          pentry = pdir_chain->object.dir_cont.pdir_cont;
          break;
        }

      /* Allocate a new DIR_CONTINUE to the dir chain if needed */
      if(pentry == NULL)

        if((pentry = cache_inode_new_entry(&fsdata, NULL, DIR_CONTINUE, NULL, pdir_chain, ht, pclient, pcontext, FALSE, /* this is population, no creation */
                                           pstatus)) == NULL)
          {
            return *pstatus;
          }
        else
          {
            /* reset status in case it already exists,
               so it is not propagated to caller */

            *pstatus = 0;
          }

      /* Chain the new entry with the pdir_chain */
      switch (pdir_chain->internal_md.type)
        {
        case DIR_BEGINNING:
          pdir_chain->object.dir_begin.pdir_cont = pentry;
          pdir_chain->object.dir_begin.pdir_last = pentry;
          pdir_chain->object.dir_begin.end_of_dir = TO_BE_CONTINUED;
          pdir_chain->object.dir_begin.nbdircont += 1;
          break;

        case DIR_CONTINUE:
          pdir_chain->object.dir_cont.pdir_cont = pentry;

          pdir_chain->object.dir_cont.end_of_dir = TO_BE_CONTINUED;

          pdir_chain->object.dir_cont.pdir_begin->object.dir_begin.pdir_last = pentry;
          pdir_chain->object.dir_cont.pdir_begin->object.dir_begin.nbdircont += 1;
          break;
        }

      /* slot to be used in the dirent array will be the first */
      slot_index = 0;
    }

  /* pentry is not NULL, if it was NULL a new DIR_CONTINUE has just been allocated */

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("cache_inode_parent_entry_t");
#endif

  GET_PREALLOC(next_parent_entry,
               pclient->pool_parent,
               pclient->nb_pre_parent, cache_inode_parent_entry_t, next_alloc);

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

  if(next_parent_entry == NULL)
    {
      *pstatus = CACHE_INODE_MALLOC_ERROR;
      pentry = NULL;
      return *pstatus;
    }

  /* Init the next_parent_entry variable */
  next_parent_entry->subdirpos = 0;
  next_parent_entry->parent = NULL;
  next_parent_entry->next_parent = NULL;

  if(pentry->internal_md.type == DIR_BEGINNING)
    {
      pentry->object.dir_begin.nbactive += 1;

      pentry->object.dir_begin.pdir_data->dir_entries[slot_index].active = VALID;
      pentry->object.dir_begin.pdir_data->dir_entries[slot_index].pentry = pentry_added;

      fsal_status =
          FSAL_namecpy(&pentry->object.dir_begin.pdir_data->dir_entries[slot_index].name,
                       pname);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = CACHE_INODE_FSAL_ERROR;
          pentry->object.dir_begin.nbactive -= 1;
          pentry = NULL;
          return *pstatus;
        }
    }
  else
    {
      /* DIR_CONTINUE */
      pentry->object.dir_cont.nbactive += 1;

      pentry->object.dir_cont.pdir_data->dir_entries[slot_index].active = VALID;
      pentry->object.dir_cont.pdir_data->dir_entries[slot_index].pentry = pentry_added;

      fsal_status =
          FSAL_namecpy(&pentry->object.dir_cont.pdir_data->dir_entries[slot_index].name,
                       pname);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = CACHE_INODE_FSAL_ERROR;
          pentry->object.dir_cont.nbactive -= 1;
          pentry = NULL;
          return *pstatus;
        }

    }

  /* link with the parent entry (insert as first entry) */
  next_parent_entry->subdirpos = slot_index;
  next_parent_entry->parent = pentry;
  next_parent_entry->next_parent = pentry_added->parent_list;
  pentry_added->parent_list = next_parent_entry;

  if(ppentry_next != NULL)
    {
      *ppentry_next = pentry;
    }

  return *pstatus;
}                               /* cache_inode_add_cached_dirent */

/*
 * cache_inode_invalidate_all_cached_dirent: Invalidates all the entries for a cached directory and its DIR_CONTINUE.
 *
 * Invalidates all the entries for a cached directory and its DIR_CONTINUE. No MT Safety managed here !!
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be managed.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_invalidate_all_cached_dirent(cache_entry_t * pentry_dir,
                                                              hash_table_t * ht,
                                                              cache_inode_client_t *
                                                              pclient,
                                                              cache_inode_status_t *
                                                              pstatus)
{
  int i;
  cache_entry_t *pentry = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Only DIR_BEGINNING entries are concerned */
  if(pentry_dir->internal_md.type != DIR_BEGINNING)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Get ride of entries cached in the DIR_BEGINNING */
  pentry = pentry_dir;

  for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
    pentry->object.dir_begin.pdir_data->dir_entries[i].active = INVALID;
  pentry->object.dir_begin.nbactive = 0;

  /* Loop on the next DIR_CONTINUE */
  pentry = pentry->object.dir_begin.pdir_cont;

  while(pentry != NULL)
    {
      for(i = 0; i < CHILDREN_ARRAY_SIZE; i++)
        pentry->object.dir_cont.pdir_data->dir_entries[i].active = INVALID;
      pentry->object.dir_cont.nbactive = 0;

      pentry = pentry->object.dir_cont.pdir_cont;
    }

  /* Reinit the fields */
  pentry_dir->object.dir_begin.has_been_readdir = CACHE_INODE_NO;
  pentry_dir->object.dir_begin.end_of_dir = END_OF_DIR;
  *pstatus = CACHE_INODE_SUCCESS;

  return *pstatus;
}                               /* cache_inode_invalidate_all_cached_dirent */

/**
 *
 * cache_inode_remove_cached_dirent: Removes a directory entry to a cached directory.
 *
 * Removes a directory entry to a cached directory. No MT safety managed here !!
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be managed.
 * @param name [IN] name of the entry to remove.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_remove_cached_dirent(cache_entry_t * pentry_parent,
                                                      fsal_name_t * pname,
                                                      hash_table_t * ht,
                                                      cache_inode_client_t * pclient,
                                                      cache_inode_status_t * pstatus)
{
  cache_entry_t *removed_pentry = NULL;
  cache_inode_parent_entry_t *parent_iter = NULL;
  cache_inode_parent_entry_t *previous_iter = NULL;
  int found = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIR_BEGINNING &&
     pentry_parent->internal_md.type != DIR_CONTINUE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* BUGAZOMEU: Ne pas oublier de jarter un dir_cont dont toutes les entrees sont inactives */
  if((removed_pentry = cache_inode_operate_cached_dirent(pentry_parent,
                                                         pname,
                                                         NULL,
                                                         CACHE_INODE_DIRENT_OP_REMOVE,
                                                         pstatus)) == NULL)
    return *pstatus;

  /* Remove the parent entry from the entry whose dirent is removed */
  for(previous_iter = NULL, parent_iter = removed_pentry->parent_list;
      (parent_iter != NULL) && (parent_iter->parent != NULL);
      previous_iter = parent_iter, parent_iter = parent_iter->next_parent)
    {
      if(parent_iter->parent == pentry_parent ||
         (parent_iter->parent->internal_md.type == DIR_CONTINUE &&
          parent_iter->parent->object.dir_cont.pdir_begin == pentry_parent))
        {
          found = 1;
          break;
        }
    }

  /* Check for pentry cache inconsistency */
  if(!found)
    {
      *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
    }
  else
    {
      if(previous_iter == NULL)
        {
          /* this is the first parent */
          removed_pentry->parent_list = parent_iter->next_parent;
        }
      else
        {
          /* This is not the first parent */
          previous_iter->next_parent = parent_iter->next_parent;
        }

      /* It is now time to put parent_iter back to its pool */
      RELEASE_PREALLOC(parent_iter, pclient->pool_parent, next_alloc);

    }
  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_remove_cached_dirent */

/**
 *
 * cache_inode_readdir_populate: fully reads a directory in FSAL and caches the related entries.
 *
 * fully reads a directory in FSAL and caches the related entries. No MT safety managed here !!
 *
 * @param pentry [IN]  entry for the parent directory to be read. This must be a DIR_BEGINNING 
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 *
 */
cache_inode_status_t cache_inode_readdir_populate(cache_entry_t * pentry_dir,
                                                  hash_table_t * ht,
                                                  cache_inode_client_t * pclient,
                                                  fsal_op_context_t * pcontext,
                                                  cache_inode_status_t * pstatus)
{
  fsal_dir_t fsal_dirhandle;
  fsal_status_t fsal_status;
  fsal_attrib_list_t dir_attributes;

  fsal_cookie_t begin_cookie;
  fsal_cookie_t end_cookie;
  fsal_count_t nbfound;
  fsal_count_t iter;
  fsal_boolean_t fsal_eod;

  cache_entry_t *pentry = NULL;
  cache_entry_t *next_pentry_parent = NULL;
  cache_entry_t *pentry_parent = pentry_dir;
  fsal_attrib_list_t object_attributes;

  cache_inode_create_arg_t create_arg;
  cache_inode_file_type_t type;
  cache_inode_status_t cache_status;
  fsal_dirent_t array_dirent[FSAL_READDIR_SIZE + 20];
  cache_inode_fsal_data_t new_entry_fsdata;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Only DIR_BEGINNING entries are concerned */
  if(pentry_dir->internal_md.type != DIR_BEGINNING)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }
#ifdef _USE_MFSL_ASYNC
  /* If entry is asynchronous (via MFSL), it should not be repopulated until it is synced */
  if(MFSL_ASYNC_is_synced(&pentry_dir->mobject) == FALSE)
    {
      /* Directory is asynchronous, do not repopulate it and let it
       * in the state 'has_been_readdir == FALSE' */
      *pstatus = CACHE_INODE_SUCCESS;
      return *pstatus;
    }
#endif

  /* If directory is already populated , there is no job to do */
  if(pentry_dir->object.dir_begin.has_been_readdir == CACHE_INODE_YES)
    {
      *pstatus = CACHE_INODE_SUCCESS;
      return *pstatus;
    }

  /* Invalidate all the dirents */
  if(cache_inode_invalidate_all_cached_dirent(pentry_dir,
                                              ht,
                                              pclient, pstatus) != CACHE_INODE_SUCCESS)
    return *pstatus;

  /* Open the directory */
  dir_attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
  fsal_status = MFSL_opendir(&pentry_dir->mobject,
                             pcontext,
                             &pclient->mfsl_context, &fsal_dirhandle, &dir_attributes);
#else
  fsal_status = FSAL_opendir(&pentry_dir->object.dir_begin.handle,
                             pcontext, &fsal_dirhandle, &dir_attributes);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;

          LogEvent(COMPONENT_CACHE_INODE,
              "cache_inode_readdir: Stale FSAL File Handle detected for pentry = %p",
               pentry_dir);

          if(cache_inode_kill_entry(pentry_dir, ht, pclient, &kill_status) !=
             CACHE_INODE_SUCCESS)
            LogCrit(COMPONENT_CACHE_INODE,"cache_inode_readdir: Could not kill entry %p, status = %u",
                       pentry_dir, kill_status);

          *pstatus = CACHE_INODE_FSAL_ESTALE;
        }
      return *pstatus;
    }

  /* Loop for readding the directory */
  FSAL_SET_COOKIE_BEGINNING(begin_cookie);
  FSAL_SET_COOKIE_BEGINNING(end_cookie);
  fsal_eod = FALSE;

  do
    {
#ifdef _USE_MFSL
      fsal_status = MFSL_readdir(&fsal_dirhandle,
                                 begin_cookie,
                                 pclient->attrmask,
                                 FSAL_READDIR_SIZE * sizeof(fsal_dirent_t),
                                 array_dirent,
                                 &end_cookie,
                                 &nbfound, &fsal_eod, &pclient->mfsl_context);
#else
      fsal_status = FSAL_readdir(&fsal_dirhandle,
                                 begin_cookie,
                                 pclient->attrmask,
                                 FSAL_READDIR_SIZE * sizeof(fsal_dirent_t),
                                 array_dirent, &end_cookie, &nbfound, &fsal_eod);
#endif

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          return *pstatus;
        }

      for(iter = 0; iter < nbfound; iter++)
        {
          LogFullDebug(COMPONENT_CACHE_INODE,
                            "cache readdir populate found entry %s",
                            array_dirent[iter].name.name);

          /* It is not needed to cache '.' and '..' */
          if(!FSAL_namecmp(&(array_dirent[iter].name), (fsal_name_t *) & FSAL_DOT) ||
             !FSAL_namecmp(&(array_dirent[iter].name), (fsal_name_t *) & FSAL_DOT_DOT))
            {
              LogFullDebug(COMPONENT_CACHE_INODE,
                                "cache readdir populate : do not cache . and ..");
              continue;
            }

          /* If dir entry is a symbolic link, its content has to be read */
          if((type =
              cache_inode_fsal_type_convert(array_dirent[iter].attributes.type)) ==
             SYMBOLIC_LINK)
            {
#ifdef _USE_MFSL
              mfsl_object_t tmp_mfsl;
#endif
              /* Let's read the link for caching its value */
              object_attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
              tmp_mfsl.handle = array_dirent[iter].handle;
              fsal_status = MFSL_readlink(&tmp_mfsl,
                                          pcontext,
                                          &pclient->mfsl_context,
                                          &create_arg.link_content, &object_attributes);
#else
              fsal_status = FSAL_readlink(&array_dirent[iter].handle,
                                          pcontext,
                                          &create_arg.link_content, &object_attributes);
#endif
              if(FSAL_IS_ERROR(fsal_status))
                {
                  *pstatus = cache_inode_error_convert(fsal_status);

                  if(fsal_status.major == ERR_FSAL_STALE)
                    {
                      cache_inode_status_t kill_status;

		      LogEvent(COMPONENT_CACHE_INODE, "cache_inode_readdir: Stale FSAL File Handle detected for pentry = %p",
                           pentry_dir);

                      if(cache_inode_kill_entry(pentry_dir, ht, pclient, &kill_status) !=
                         CACHE_INODE_SUCCESS)
                        LogCrit(COMPONENT_CACHE_INODE,
                            "cache_inode_readdir: Could not kill entry %p, status = %u",
                             pentry_dir, kill_status);

                      *pstatus = CACHE_INODE_FSAL_ESTALE;
                    }

                  return *pstatus;
                }
            }

          /* Try adding the entry, if it exists then this existing entry is returned */
          new_entry_fsdata.handle = array_dirent[iter].handle;
          new_entry_fsdata.cookie = 0;

          if((pentry = cache_inode_new_entry(&new_entry_fsdata, &array_dirent[iter].attributes, type, &create_arg, NULL, ht, pclient, pcontext, FALSE,  /* This is population and no creation */
                                             pstatus)) == NULL)
            return *pstatus;

          cache_status = cache_inode_add_cached_dirent(pentry_parent,
                                                       &(array_dirent[iter].name),
                                                       pentry,
                                                       &next_pentry_parent,
                                                       ht, pclient, pcontext, pstatus);

          if(cache_status != CACHE_INODE_SUCCESS
             && cache_status != CACHE_INODE_ENTRY_EXISTS)
            return *pstatus;

        }

      /* Step to next item in dir_chain */
      pentry_parent = next_pentry_parent;

      /* Get prepared for next step */
      begin_cookie = end_cookie;
    }
  while(fsal_eod != TRUE);

  /* Close the directory */
#ifdef _USE_MFSL
  fsal_status = MFSL_closedir(&fsal_dirhandle, &pclient->mfsl_context);
#else
  fsal_status = FSAL_closedir(&fsal_dirhandle);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      return *pstatus;
    }

  /* End of work */
  pentry_dir->object.dir_begin.has_been_readdir = CACHE_INODE_YES;
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}                               /* cache_inode_readdir_populate */

/**
 *
 * cache_inode_readdir: Reads partially a directory.
 * 
 * Looks up for a name in a directory indicated by a cached entry. The directory should have been cached before.
 * This is the only function in the cache_inode_readdir.c file that manages MT safety on a dir chain.
 *
 * @param pentry [IN] entry for the parent directory to be read.
 * @param cookie [IN] cookie for the readdir operation (basically the offset).
 * @param nbwanted [IN] Maximum number of directory entries wanted.
 * @param peod_met [OUT] A flag to know if end of directory was met during this call.
 * @param dirent_array [OUT] the resulting array of found directory entries. 
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_BAD_TYPE if entry is not related to a directory\n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_readdir(cache_entry_t * dir_pentry,
                                         unsigned int cookie,
                                         unsigned int nbwanted,
                                         unsigned int *pnbfound,
                                         unsigned int *pend_cookie,
                                         cache_inode_endofdir_t * peod_met,
                                         cache_inode_dir_entry_t * dirent_array,
                                         unsigned int *cookie_array,
                                         hash_table_t * ht,
                                         cache_inode_client_t * pclient,
                                         fsal_op_context_t * pcontext,
                                         cache_inode_status_t * pstatus)
{
  cache_inode_flag_t tstflag;
  cache_entry_t *pentry_iter;
  cache_entry_t *pentry_to_read;
  unsigned int first_pentry_cookie = 0;
  unsigned int i = 0;
  unsigned int cookie_iter = 0;
  unsigned int nbdirchain = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* end cookie initial value is the begin cookie */
  *pend_cookie = cookie;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_READDIR] += 1;

  LogFullDebug(COMPONENT_NFSPROTO, "--> Cache_inode_readdir: parameters are cookie=%u nbwanted=%u\n", cookie,
         nbwanted);

  /* Sanity check */
  if(nbwanted == 0)
    {
      /* Asking for nothing is not a crime !!!!! 
       * build a 'dummy' return in this case */
      *pstatus = CACHE_INODE_SUCCESS;
      *pnbfound = 0;
      *peod_met = TO_BE_CONTINUED;

      /* stats */
      pclient->stat.func_stats.nb_success[CACHE_INODE_READDIR] += 1;

      return *pstatus;
    }

  P_w(&dir_pentry->lock);

  /* Renew the entry (to avoid having it being garbagged */
  if(cache_inode_renew_entry(dir_pentry, NULL, ht, pclient, pcontext, pstatus) !=
     CACHE_INODE_SUCCESS)
    {
      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR] += 1;
      V_w(&dir_pentry->lock);
      return *pstatus;
    }

  /* readdir can be done only with a directory */
  if(dir_pentry->internal_md.type != DIR_BEGINNING &&
     dir_pentry->internal_md.type != DIR_CONTINUE)
    {
      V_w(&dir_pentry->lock);
      *pstatus = CACHE_INODE_BAD_TYPE;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READDIR] += 1;

      return *pstatus;
    }

  /* Check is user (as specified by the credentials) is authorized to read the directory or not */
  if(cache_inode_access_no_mutex(dir_pentry,
                                 FSAL_R_OK,
                                 ht, pclient, pcontext, pstatus) != CACHE_INODE_SUCCESS)
    {
      V_w(&dir_pentry->lock);

      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_READDIR] += 1;
      return *pstatus;
    }

  /* Is the directory fully cached (this is done if a readdir call if done on the directory) */
  if(dir_pentry->internal_md.type == DIR_BEGINNING)
    {
      if(dir_pentry->object.dir_begin.has_been_readdir != CACHE_INODE_YES)
        {

          /* populate the cache */
          if(cache_inode_readdir_populate(dir_pentry,
                                          ht,
                                          pclient,
                                          pcontext, pstatus) != CACHE_INODE_SUCCESS)
            {
              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READDIR] += 1;

              V_w(&dir_pentry->lock);
              return *pstatus;
            }

        }

      /* Compute fist cookie in readdir */
      first_pentry_cookie = 0;
    }
  else
    {
      /* DIR_CONTINUE */
      tstflag = dir_pentry->object.dir_cont.pdir_begin->object.dir_begin.has_been_readdir;

      /* This test should see that "tstflag" is not CACHE_INODE_YES for a DIR_CONTINUE (if a DIR_CONTINUE exists, then 
       * it means that dir_chain was populated and this means a former call to cache_inode_readdir_populate. Later evolution
       * of the code could make 'has_been_readdir' returned from CACHE_INODE_YES to CACHE_INODE_NO during garbagge collection.
       * In this case, the following lines wil become necessary, but for now they are just here as 'defensive coding' */
      if(tstflag != CACHE_INODE_YES)
        {

          /* populate the cache */
          if(cache_inode_readdir_populate(dir_pentry->object.dir_cont.pdir_begin,
                                          ht,
                                          pclient,
                                          pcontext, pstatus) != CACHE_INODE_SUCCESS)
            {
              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READDIR] += 1;

              V_w(&dir_pentry->lock);
              return *pstatus;
            }

        }

      /* Compute fist cookie in readdir */
      first_pentry_cookie =
          dir_pentry->object.dir_cont.dir_cont_pos * CHILDREN_ARRAY_SIZE;
    }

  /* Downgrade Writer lock to a reader one */
  rw_lock_downgrade(&dir_pentry->lock);

  /* Now go through the pdir_chain for filling in dirent_array 
   * the first cookie is parameter 'cookie'
   * number of entries queried is set by parameter 'nbwanted'
   * number of found entries before eod is return is '*pnbfound' 
   * '*peod_met' is set if end of directory is encountered */

  /* Now, we can fill in the dirent array */
  *pnbfound = 0;
  *peod_met = TO_BE_CONTINUED;

  /* Do we start to read the directory from the beginning ? */
  if(cookie == 0)
    {
      /* First call: the two first entries should be '.' and '..' */
    }

  /* loop into the dirent array to locate the pdir_chain item related to the input cookie */
  nbdirchain = 0;
  pentry_to_read = dir_pentry;

  while(cookie - first_pentry_cookie >= CHILDREN_ARRAY_SIZE)    /* ????? */
    {
      nbdirchain += 1;

      if(pentry_to_read->internal_md.type == DIR_BEGINNING)
        {
          /* if cookie - first_pentry_cookie is bigger than  CHILDREN_ARRAY_SIZE
           * then the provided cookie was far too big for this pdir_chain. The 
           * client to cache_inode tried to read beyond the end of directory.
           * In this case, return that EOD was met, but no entries found. */

          if(pentry_to_read->object.dir_begin.end_of_dir == END_OF_DIR)
            {
              /* stats */
              pclient->stat.func_stats.nb_success[CACHE_INODE_READDIR] += 1;

              if(pentry_to_read->internal_md.type == DIR_BEGINNING)
                *pstatus = cache_inode_valid(pentry_to_read, CACHE_INODE_OP_GET, pclient);
              else
                *pstatus = CACHE_INODE_SUCCESS;

              V_r(&dir_pentry->lock);

              LogFullDebug(COMPONENT_NFS_READDIR,
                  "Big input cookie found in cache_inode_readdir (DIR_BEGIN) : pentry=%p cookie=%d first_pentry_cookie=%d nbdirchain=%d\n",
                   pentry_to_read, cookie, first_pentry_cookie, nbdirchain);

              /* Set the returned values */
              *pnbfound = 0;
              *pend_cookie = cookie;
              *peod_met = END_OF_DIR;

              return *pstatus;
            }
          pentry_iter = pentry_to_read->object.dir_begin.pdir_cont;
        }
      else
        {
          if(pentry_to_read->object.dir_cont.end_of_dir == END_OF_DIR)
            {
              V_r(&dir_pentry->lock);

              /* stats */
              pclient->stat.func_stats.nb_success[CACHE_INODE_READDIR] += 1;

              /* OPeration is a success */
              *pstatus = CACHE_INODE_SUCCESS;
              LogFullDebug(COMPONENT_NFS_READDIR, 
                  "Trouble found in cache_inode_readdir (DIR_CONTINUE) : pentry=%p cookie=%d first_pentry_cookie=%d nbdirchain=%d\n",
                   pentry_to_read, cookie, first_pentry_cookie, nbdirchain);
              /* Set the returned values */
              *pnbfound = 0;
              *pend_cookie = cookie;
              *peod_met = END_OF_DIR;

              return *pstatus;
            }
          pentry_iter = pentry_to_read->object.dir_cont.pdir_cont;
        }

      pentry_to_read = pentry_iter;

      /* At this step, pentry can't be something different of a DIR_CONTINUE,
       * becase  cookie - first_pentry_cookie >= CHILDREN_ARRAY_SIZE */
      first_pentry_cookie =
          pentry_to_read->object.dir_cont.dir_cont_pos * CHILDREN_ARRAY_SIZE;

    }                           /* while */

  LogFullDebug(COMPONENT_NFS_READDIR, 
      "About to readdir in  cache_inode_readdir: pentry=%p cookie=%d first_pentry_cookie=%d nbdirchain=%d\n",
       pentry_to_read, cookie, first_pentry_cookie, nbdirchain);

  /* Get prepaired for readdir */

  cookie_iter = cookie;

  for(i = 0; i < nbwanted;)     /* i is incremented when something is found */
    {
      if(pentry_to_read->internal_md.type == DIR_BEGINNING)
        {
          if(pentry_to_read->object.dir_begin.pdir_data->
             dir_entries[cookie_iter % CHILDREN_ARRAY_SIZE].active == VALID)
            {
              /* another entry was add to the result array */
              dirent_array[i] =
                  pentry_to_read->object.dir_begin.pdir_data->dir_entries[cookie_iter %
                                                                          CHILDREN_ARRAY_SIZE];
              cookie_array[i] = cookie_iter;

              LogFullDebug(COMPONENT_CACHE_INODE,"--> Cache_inode_readdir: Found slot with file named %s\n",
                     pentry_to_read->object.dir_begin.pdir_data->dir_entries[cookie_iter %
                                                                             CHILDREN_ARRAY_SIZE].
                     name.name);

              /* Step to next iter */
              *pnbfound += 1;
              i += 1;

            }
        }
      else
        {
          if(pentry_to_read->object.dir_cont.pdir_data->
             dir_entries[cookie_iter % CHILDREN_ARRAY_SIZE].active == VALID)
            {
              /* another entry was add to the result array */
              dirent_array[i] =
                  pentry_to_read->object.dir_cont.pdir_data->dir_entries[cookie_iter %
                                                                         CHILDREN_ARRAY_SIZE];
              cookie_array[i] = cookie_iter;

              LogFullDebug(COMPONENT_CACHE_INODE,"--> Cache_inode_readdir: Found slot with file named %s\n",
                     pentry_to_read->object.dir_cont.pdir_data->dir_entries[cookie_iter %
                                                                            CHILDREN_ARRAY_SIZE].
                     name.name);
	      //              fflush(stdout);

              /* Step to next iter */
              *pnbfound += 1;
              i += 1;

            }
        }

      /* Loop at next entry in dirent array */
      cookie_iter += 1;
      *pend_cookie = cookie_iter;

      if((cookie_iter % CHILDREN_ARRAY_SIZE) == 0)
        {
          /* It's time to step to the next dir_cont */
          if(pentry_to_read->internal_md.type == DIR_BEGINNING)
            {
              if(pentry_to_read->object.dir_begin.end_of_dir == END_OF_DIR)
                {
                  /* End of dir is reached */
                  *peod_met = END_OF_DIR;

                  *pstatus = CACHE_INODE_SUCCESS;
                  V_r(&dir_pentry->lock);

                  /* stats */
                  pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READDIR] += 1;

                  return *pstatus;
                }
              pentry_iter = pentry_to_read->object.dir_begin.pdir_cont;
              pentry_to_read = pentry_iter;
              /* cookie_iter = 0 ; */
            }
          else
            {
              if(pentry_to_read->object.dir_cont.end_of_dir == END_OF_DIR)
                {
                  /* End of dir is reached */
                  *peod_met = END_OF_DIR;

                  *pstatus = CACHE_INODE_SUCCESS;
                  V_r(&dir_pentry->lock);

                  /* stats */
                  pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READDIR] += 1;

                  return *pstatus;
                }
              pentry_iter = pentry_to_read->object.dir_cont.pdir_cont;
              pentry_to_read = pentry_iter;
              /* cookie_iter = 0 ; */
            }
        }
      /* if( cookie_iter == CHILDREN_ARRAY_SIZE ) */
    }                           /* for( i = 0 ; i < nbwanted ; i ++ ) */

  if(pentry_to_read->internal_md.type == DIR_BEGINNING)
    *pstatus = cache_inode_valid(pentry_to_read, CACHE_INODE_OP_GET, pclient);
  else
    *pstatus = CACHE_INODE_SUCCESS;

  V_r(&dir_pentry->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_READDIR] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_READDIR] += 1;

  return *pstatus;
}                               /* cache_inode_readdir */
