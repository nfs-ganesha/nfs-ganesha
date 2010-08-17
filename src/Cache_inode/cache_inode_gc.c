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
 * \file    cache_inode_gc.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/20 10:51:49 $
 * \version $Revision: 1.21 $
 * \brief   Do garbage collection on a cache inode client. 
 *
 * cache_inode_gc.c: do garbage collection on a cache inode client. 
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
#include "LRU_List.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

static cache_inode_gc_policy_t cache_inode_gc_policy;   /*<< the policy to be used by the garbage collector */

/**
 * @defgroup Cache_inode_gc_internal Cache Inode GC internal functions.
 *
 * These functions are used for Garbage collector internal management. 
 *
 * @{
 */

/**
 *
 * cache_inode_gc_clean_entry: cleans a entry in the cache_inode.
 *
 * cleans an entry in the cache_inode.
 *
 * @param pentry [INOUT] entry to be cleaned. 
 * @param addparam [IN] additional parameter used for cleaning.
 *
 * @return  LRU_LIST_SET_INVALID if ok,  LRU_LIST_DO_NOT_SET_INVALID otherwise
 *
 */
static int cache_inode_gc_clean_entry(cache_entry_t * pentry,
                                      cache_inode_param_gc_t * pgcparam)
{
  fsal_handle_t *pfsal_handle = NULL;
  cache_inode_parent_entry_t *parent_iter = NULL;
  cache_inode_parent_entry_t *parent_iter_next = NULL;
  cache_inode_fsal_data_t fsaldata;
  cache_inode_status_t status;
  fsal_status_t fsal_status;
  hash_buffer_t key, old_key, old_value;
  int rc;

  LogFullDebug(COMPONENT_CACHE_INODE_GC, "(pthread_self=%p): About to remove pentry=%p, type=%d\n", pthread_self(),
         pentry, pentry->internal_md.type);

  /* sanity check */
  if((pentry->gc_lru_entry != NULL) &&
     ((cache_entry_t *) pentry->gc_lru_entry->buffdata.pdata) != pentry)
    {
      LogCrit(COMPONENT_CACHE_INODE_GC,
                        "cache_inode_gc_clean_entry: LRU entry pointed by this pentry doesn't match the GC LRU");
    }

  /* Get the FSAL handle */
  if((pfsal_handle = cache_inode_get_fsal_handle(pentry, &status)) == NULL)
    {
      LogCrit(COMPONENT_CACHE_INODE_GC,
                        "cache_inode_gc_clean_entry: unable to retrieve pentry's specific filesystem info");
      return LRU_LIST_DO_NOT_SET_INVALID;
    }

  fsaldata.handle = *pfsal_handle;

  if(pentry->internal_md.type != DIR_CONTINUE)
    fsaldata.cookie = DIR_START;
  else
    fsaldata.cookie = pentry->object.dir_cont.dir_cont_pos;

  /* Use the handle to build the key */
  if(cache_inode_fsaldata_2_key(&key, &fsaldata, pgcparam->pclient))
    {
      LogCrit(COMPONENT_CACHE_INODE_GC,
                        "cache_inode_gc_clean_entry: could not build hashtable key");

      cache_inode_release_fsaldata_key(&key, pgcparam->pclient);

      return LRU_LIST_DO_NOT_SET_INVALID;
    }

  /* use the key to delete the entry */
  rc = HashTable_Del(pgcparam->ht, &key, &old_key, &old_value);

  if((rc != HASHTABLE_SUCCESS) && (rc != HASHTABLE_ERROR_NO_SUCH_KEY))
    {
      LogCrit(COMPONENT_CACHE_INODE_GC,
                        "cache_inode_gc_clean_entry: entry could not be deleted, status = %d",
                        rc);

      cache_inode_release_fsaldata_key(&key, pgcparam->pclient);

      return LRU_LIST_DO_NOT_SET_INVALID;
    }
  else if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      LogEvent(COMPONENT_CACHE_INODE_GC,
                        "cache_inode_gc_clean_entry: entry already deleted, type=%d, status=%d",
                        pentry->internal_md.type, rc);

      cache_inode_release_fsaldata_key(&key, pgcparam->pclient);
      return LRU_LIST_SET_INVALID;
    }

  /* Clean up the associated ressources in the FSAL */
  if(FSAL_IS_ERROR(fsal_status = FSAL_CleanObjectResources(pfsal_handle)))
    {
      LogCrit(COMPONENT_CACHE_INODE_GC,
                        "cache_inode_gc_clean_entry: Could'nt free FSAL ressources fsal_status.major=%u",
                        fsal_status.major);
    }
  LogFullDebug(COMPONENT_CACHE_INODE_GC, "++++> pentry %p deleted from HashTable\n", pentry);

  /* Release the hash key data */
  cache_inode_release_fsaldata_key(&old_key, pgcparam->pclient);

  /* Sanity check: old_value.pdata is expected to be equal to pentry,
   * and is released later in this function */
  if((cache_entry_t *) old_value.pdata != pentry)
    {
      LogCrit(COMPONENT_CACHE_INODE_GC,
                        "cache_inode_gc_clean_entry: unexpected pdata %p from hash table (pentry=%p)",
                        old_value.pdata, pentry);
    }

  cache_inode_release_fsaldata_key(&key, pgcparam->pclient);

  /* Recover the parent list entries */
  parent_iter = pentry->parent_list;
  while(parent_iter != NULL)
    {
      parent_iter_next = parent_iter->next_parent;

      RELEASE_PREALLOC(parent_iter, pgcparam->pclient->pool_parent, next_alloc);

      parent_iter = parent_iter_next;
    }

  LogFullDebug(COMPONENT_CACHE_INODE_GC, "++++> parent directory sent back to pool\n");

  /* If entry is a DIR_CONTINUE or a DIR_BEGINNING, release pdir_data */
  if(pentry->internal_md.type == DIR_BEGINNING)
    {
      /* Put the pentry back to the pool */
      RELEASE_PREALLOC(pentry->object.dir_begin.pdir_data,
                       pgcparam->pclient->pool_dir_data, next_alloc);
    }

  if(pentry->internal_md.type == DIR_CONTINUE)
    {
      /* Put the pentry back to the pool */
      RELEASE_PREALLOC(pentry->object.dir_cont.pdir_data,
                       pgcparam->pclient->pool_dir_data, next_alloc);
    }
  LogFullDebug(COMPONENT_CACHE_INODE_GC,"++++> pdir_data (if needed) sent back to pool\n");

  /* Free and Destroy the mutex associated with the pentry */
  V_w(&pentry->lock);

  cache_inode_mutex_destroy(pentry);

  /* Put the pentry back to the pool */
  RELEASE_PREALLOC(pentry, pgcparam->pclient->pool_entry, next_alloc);

  /* Regular exit */
  pgcparam->nb_to_be_purged = pgcparam->nb_to_be_purged - 1;

  LogFullDebug(COMPONENT_CACHE_INODE_GC, "++++> pentry %p: clean entry is ok\n", pentry);

  return LRU_LIST_SET_INVALID;  /* Cleaning ok */
}

/**
 *
 * cache_inode_gc_invalidate_related_dirent: sets the related directory entries as invalid.
 *
 * sets the related directory entries as invalid. /!\ the parent entry is supposed to be locked.
 *
 * @param pentry [INOUT] entry to be managed
 * @param addparam [IN] additional parameter used for cleaning.
 *
 * @return  LRU_LIST_SET_INVALID if ok,  LRU_LIST_DO_NOT_SET_INVALID otherwise
 *
 */
static int cache_inode_gc_invalidate_related_dirent(cache_entry_t * pentry,
                                                    cache_inode_param_gc_t * pgcparam)
{
  cache_inode_parent_entry_t *parent_iter = NULL;

  /* Set the cache status as INVALID in the directory entries */
  for(parent_iter = pentry->parent_list; parent_iter != NULL;
      parent_iter = parent_iter->next_parent)
    {
      if(parent_iter->parent == NULL)
        {
          LogDebug(COMPONENT_CACHE_INODE_GC,
                            "cache_inode_gc_invalidate_related_dirent: pentry %p has no parent, no dirent to be removed...",
                            pentry);
          continue;
        }

      /* If I reached this point, then parent_iter->parent is not null and is a valid cache_inode pentry */
      P_w(&parent_iter->parent->lock);

      /* Check for type of the parent */
      if(parent_iter->parent->internal_md.type != DIR_BEGINNING &&
         parent_iter->parent->internal_md.type != DIR_CONTINUE)
        {
          V_w(&parent_iter->parent->lock);
          /* Major parent incoherency: parent is no directory */
          LogDebug(COMPONENT_CACHE_INODE_GC,
                            "cache_inode_gc_invalidate_related_dirent: major inconcistency. Found an entry whose parent is not a directory");
          return LRU_LIST_DO_NOT_SET_INVALID;
        }

      /* Set the entry as invalid in the dirent array */
      if(parent_iter->parent->internal_md.type == DIR_BEGINNING)
        {
          if(parent_iter->subdirpos > CHILDREN_ARRAY_SIZE)
            {
              V_w(&parent_iter->parent->lock);
              LogCrit(COMPONENT_CACHE_INODE_GC,
                  "A known bug occured line %d file %s: pentry=%p type=%u parent_iter->subdirpos=%d, should never exceed %d, entry not removed",
                   __LINE__, __FILE__, pentry, pentry->internal_md.type,
                   parent_iter->subdirpos, CHILDREN_ARRAY_SIZE);
              return LRU_LIST_DO_NOT_SET_INVALID;
            }
          else
            {
              parent_iter->parent->object.dir_begin.pdir_data->dir_entries[parent_iter->
                                                                           subdirpos].
                  active = INVALID;
              /* Garbage invalidates the effet of the readdir previously made */
              parent_iter->parent->object.dir_begin.has_been_readdir = CACHE_INODE_NO;
              parent_iter->parent->object.dir_begin.nbactive -= 1;
            }
        }
      else
        {
          if(parent_iter->subdirpos > CHILDREN_ARRAY_SIZE)
            {
              V_w(&parent_iter->parent->lock);
              LogCrit(COMPONENT_CACHE_INODE_GC,
                  "A known bug occured line %d file %s: pentry=%p type=%u parent_iter->subdirpos=%d, should never exceed %d, entry not removed",
                   __LINE__, __FILE__, pentry, pentry->internal_md.type,
                   parent_iter->subdirpos, CHILDREN_ARRAY_SIZE);
              return LRU_LIST_DO_NOT_SET_INVALID;
            }
          else
            {
              parent_iter->parent->object.dir_cont.pdir_data->dir_entries[parent_iter->
                                                                          subdirpos].
                  active = INVALID;
              parent_iter->parent->object.dir_cont.nbactive -= 1;
            }
        }

      V_w(&parent_iter->parent->lock);
    }

  return LRU_LIST_SET_INVALID;
}                               /* cache_inode_gc_invalidate_related_dirent */

/**
 *
 * cache_inode_gc_suppress_file: suppress a file entry from the cache inode.
 *
 * Suppress a file entry from the cache inode.
 *
 * @param pentry [IN] pointer to the entry to be suppressed. 
 *
 * @return LRU_LIST_SET_INVALID if entry is successfully suppressed, LRU_LIST_DO_NOT_SET_INVALID otherwise
 *
 * @see LRU_invalidate_by_function
 * @see LRU_gc_invalid
 *
 */
int cache_inode_gc_suppress_file(cache_entry_t * pentry,
                                 cache_inode_param_gc_t * pgcparam)
{
  P_w(&pentry->lock);

  LogFullDebug(COMPONENT_CACHE_INODE_GC,
                    "Entry %p (REGULAR_FILE/SYMBOLIC_LINK) will be garbaged");

  /* Set the entry as invalid */
  pentry->internal_md.valid_state = INVALID;

  LogFullDebug(COMPONENT_CACHE_INODE_GC,"****> cache_inode_gc_suppress_file on %p\n", pentry);

  /* Remove refences in the parent entries */
  if(cache_inode_gc_invalidate_related_dirent(pentry, pgcparam) != LRU_LIST_SET_INVALID)
    {
      V_w(&pentry->lock);
      return LRU_LIST_DO_NOT_SET_INVALID;
    }

  /* Clean the entry */
  if(cache_inode_gc_clean_entry(pentry, pgcparam) != LRU_LIST_SET_INVALID)
    return LRU_LIST_DO_NOT_SET_INVALID;

  /* Mutex has already been freed at destruction time */

  return LRU_LIST_SET_INVALID;
}                               /* cache_inode_gc_suppress_file */

/**
 *
 * cache_inode_gc_suppress_directory: suppress a directory entry from the cache inode.
 *
 * Suppress a file entry from the cache inode.
 *
 * @param pentry [IN] pointer to the entry to be suppressed. 
 *
 * @return 1 if entry is successfully suppressed, 0 otherwise
 *
 * @see LRU_invalidate_by_function
 * @see LRU_gc_invalid
 *
 */
int cache_inode_gc_suppress_directory(cache_entry_t * pentry,
                                      cache_inode_param_gc_t * pgcparam)
{
  cache_entry_t *pentry_iter = NULL;
  cache_entry_t *pentry_iter_save = NULL;

  P_w(&pentry->lock);
  pentry->internal_md.valid_state = INVALID;

  if(cache_inode_is_dir_empty(pentry) != CACHE_INODE_SUCCESS)
    {
      V_w(&pentry->lock);

      LogFullDebug(COMPONENT_CACHE_INODE_GC,
                        "Entry %p (DIR_BEGINNING) is not empty. The dir_chain will not be garbaged now",
                        pentry);

      return LRU_LIST_DO_NOT_SET_INVALID;       /* entry is not to be suppressed */
    }

  /* If we reached this point, the directory contains no active entry, it should be removed from the cache */
  LogFullDebug(COMPONENT_CACHE_INODE_GC,
                    "Entry %p (DIR_BEGINNING) and its associated dir_chain will be garbaged",
                    pentry);

  LogFullDebug(COMPONENT_CACHE_INODE_GC,"****> cache_inode_gc_suppress_directory on %p\n", pentry);

  /* Remove refences in the parent entries */
  if(cache_inode_gc_invalidate_related_dirent(pentry, pgcparam) != LRU_LIST_SET_INVALID)
    {
      V_w(&pentry->lock);
      return LRU_LIST_DO_NOT_SET_INVALID;
    }

  /* Remove the whole dir_chain from the cache */
  pentry_iter = pentry->object.dir_begin.pdir_cont;
  while(pentry_iter != NULL)
    {
      pentry_iter_save = pentry_iter->object.dir_cont.pdir_cont;

      if(cache_inode_gc_clean_entry(pentry_iter, pgcparam) != LRU_LIST_SET_INVALID)
        {
          V_w(&pentry->lock);
          return LRU_LIST_DO_NOT_SET_INVALID;
        }

      pentry_iter = pentry_iter_save;
    }

  if(cache_inode_gc_clean_entry(pentry, pgcparam) != LRU_LIST_SET_INVALID)
    {
      V_w(&pentry->lock);
      return LRU_LIST_DO_NOT_SET_INVALID;
    }

  /* Mutex has already been freed at destruction time */

  return LRU_LIST_SET_INVALID;
}                               /* cache_inode_gc_suppress_directory */

/**
 *
 * cache_inode_gc_function: Tests is an entry in cache inode is to be set invalid (has expired).
 *
 * Tests is an entry in cache inode is to be set invalid (has expired).
 * If entry is invalidated, does the cleaning stuff on it. 
 *
 * @param pentry [IN] pointer to the entry to test
 *
 * @return 1 if entry must be set invalid, 0 if not.
 *
 * @see LRU_invalidate_by_function
 * @see LRU_gc_invalid
 *
 */
int cache_inode_gc_function(LRU_entry_t * plru_entry, void *addparam)
{
  time_t entry_time = 0;
  time_t current_time = time(NULL);
  cache_entry_t *pentry = NULL;
  cache_inode_param_gc_t *pgcparam = (cache_inode_param_gc_t *) addparam;
  cache_inode_status_t status;

  time_t allocated;

  /* Get the entry */
  pentry = (cache_entry_t *) (plru_entry->buffdata.pdata);

  /* Get the entry time (the larger value in read_time and mod_time ) */
  if(pentry->internal_md.read_time > pentry->internal_md.mod_time)
    entry_time = pentry->internal_md.read_time;
  else
    entry_time = pentry->internal_md.mod_time;

  allocated = pentry->internal_md.alloc_time;

  if(pgcparam->nb_to_be_purged != 0)
    {
      LogFullDebug(COMPONENT_CACHE_INODE_GC,
                        "We still need %d entries to be garbaged",
                        pgcparam->nb_to_be_purged);

      /* Should we get ride of this entry ? */
      if((pentry->internal_md.type == DIR_BEGINNING) &&
         (cache_inode_gc_policy.directory_expiration_delay > 0))
        {
          if(current_time - entry_time > cache_inode_gc_policy.directory_expiration_delay)
            {
              /* Entry should be tagged invalid */
              LogDebug(COMPONENT_CACHE_INODE_GC,
                                "----->>>>>>>> DIR GC : Garbage collection on dir entry %p",
                                pentry);
              return cache_inode_gc_suppress_directory(pentry, pgcparam);
            }
          else
            LogFullDebug(COMPONENT_CACHE_INODE_GC,
                              "No garbage on dir entry %p used:%d allocated:%d %d",
                              pentry, current_time - entry_time, current_time - allocated,
                              cache_inode_gc_policy.directory_expiration_delay);
        }
      else if((pentry->internal_md.type == REGULAR_FILE
               || pentry->internal_md.type == SYMBOLIC_LINK)
              && (cache_inode_gc_policy.file_expiration_delay > 0))
        {
          if(current_time - entry_time > cache_inode_gc_policy.file_expiration_delay)
            {
              /* Entry should be suppress and tagged invalid */
              LogDebug(COMPONENT_CACHE_INODE_GC,
                                "----->>>>>> REGULAR/SYMLINK GC : Garbage collection on regular/symlink entry %p",
                                pentry);
              return cache_inode_gc_suppress_file(pentry, pgcparam);
            }
          else
            LogFullDebug(COMPONENT_CACHE_INODE_GC,
                              "No garbage on regular/symlink entry %p used:%d allocated:%d %d",
                              pentry, current_time - entry_time, current_time - allocated,
                              cache_inode_gc_policy.file_expiration_delay);
        }
    }

  /* Default return, entry is not to be set invalid */
  return LRU_LIST_DO_NOT_SET_INVALID;
}                               /* cache_inode_gc_function */

/* @} */

/**
 * @defgroup Cache_inode_gc_interface Cache Inode GC interface. 
 *
 * These functions are used to run and configure the Cache inode GC 
 *
 * @{
 */

/**
 *
 * cache_inode_set_gc_policy: Set the cache_inode garbage collecting policy. 
 *
 * @param policy [IN] policy to be set.
 *
 * @return nothing (void function)
 *
 */
void cache_inode_set_gc_policy(cache_inode_gc_policy_t policy)
{
  cache_inode_gc_policy = policy;
}                               /* cache_inode_set_gc_policy */

/**
 *
 * cache_inode_get_gc_policy: Set the cache_inode garbage collecting policy. 
 *
 * @return the current policy.
 *
 */
cache_inode_gc_policy_t cache_inode_get_gc_policy(void)
{
  return cache_inode_gc_policy;
}                               /* cache_inode_get_gc_policy */

/**
 *
 * cache_inode_gc: Perform garbbage collection on the ressources managed by a client.
 *
 * Perform garbbage collection on the ressources managed by a client.
 *
 * @param ht      [INOUT] the hashtable used to stored the cache_inode entries. 
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 * @see HashTable_GetSize
 * @see LRU_invalidate_by_function
 * @see LRU_gc_invalid
 *
 */
cache_inode_status_t cache_inode_gc(hash_table_t * ht,
                                    cache_inode_client_t * pclient,
                                    cache_inode_status_t * pstatus)
{
  cache_inode_param_gc_t gcparam;
  unsigned int hash_size;
  unsigned int invalid_before_gc = 0;
  unsigned int invalid_after_gc = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Is this time to gc ? */
  if(pclient->call_since_last_gc < cache_inode_gc_policy.nb_call_before_gc)
    return *pstatus;

  if(time(NULL) - pclient->time_of_last_gc < (int)(cache_inode_gc_policy.run_interval))
    return *pstatus;

  /* Actual GC will be made */
  pclient->call_since_last_gc = 0;
  pclient->time_of_last_gc = time(NULL);

  LogEvent(COMPONENT_CACHE_INODE_GC,
                    "Checking if garbage collection is needed");

  /* 1st ; we get the hash table size to see if garbage is required */
  hash_size = HashTable_GetSize(ht);

  if(hash_size > cache_inode_gc_policy.hwmark_nb_entries)
    {
      /*
       * Garbage collection is made in several steps
       *    1- Set the oldest entry as invalid and garbage their contents
       *    2- Free the invalid entry in the LRU
       *         
       *    Behaviour: - A DIR_BEGINNING is garbaged with all its DIR_CONTINUE associated
       *               - A directory is garbaged when all its entries are garbaged
       *
       */

      gcparam.ht = ht;
      gcparam.pclient = pclient;
      gcparam.nb_to_be_purged = hash_size - cache_inode_gc_policy.lwmark_nb_entries;    /* try to purge until lw mark is reached */

      LogEvent(COMPONENT_CACHE_INODE_GC,
                        "Garbage collection started (to be purged=%u, LRU size=%u)",
                        pclient->lru_gc->nb_entry, gcparam.nb_to_be_purged);

      invalid_before_gc = pclient->lru_gc->nb_invalid;
      if(LRU_invalidate_by_function
         (pclient->lru_gc, cache_inode_gc_function, (void *)&gcparam) != LRU_LIST_SUCCESS)
        {
          *pstatus = CACHE_INODE_LRU_ERROR;
          return *pstatus;
        }

      invalid_after_gc = pclient->lru_gc->nb_invalid;

      /* Removes the LRU entries and put them back to the pool */
      if(LRU_gc_invalid(pclient->lru_gc, NULL) != LRU_LIST_SUCCESS)
        {
          *pstatus = CACHE_INODE_LRU_ERROR;
          return *pstatus;
        }

      LogEvent(COMPONENT_CACHE_INODE_GC,
                        "Garbage collection finished, %u entries removed",
                        invalid_after_gc - invalid_before_gc);

      *pstatus = CACHE_INODE_SUCCESS;
    }
  else
    {
      /* no garbage is required, just gets ride of the invalid in tyhe LRU list */
      /* Removes the LRU entries and put them back to the pool */
      if(LRU_gc_invalid(pclient->lru_gc, NULL) != LRU_LIST_SUCCESS)
        {
          *pstatus = CACHE_INODE_LRU_ERROR;
          return *pstatus;
        }
      else
        *pstatus = CACHE_INODE_SUCCESS;
    }

  return *pstatus;
}                               /* cache_inode_gc */

int cache_inode_gc_fd_func(LRU_entry_t * plru_entry, void *addparam)
{
  time_t entry_time = 0;
  time_t current_time = time(NULL);
  cache_entry_t *pentry = NULL;
  cache_inode_param_gc_t *pgcparam = (cache_inode_param_gc_t *) addparam;
  cache_inode_status_t status;

  time_t allocated;

  /* Get the entry */
  pentry = (cache_entry_t *) (plru_entry->buffdata.pdata);

  /* check if a file descriptor is opened on the file for a long time */

  if((pentry->internal_md.type == REGULAR_FILE)
     && (pentry->object.file.open_fd.fileno != 0)
     && (time(NULL) - pentry->object.file.open_fd.last_op > pgcparam->pclient->retention))
    {
      P_w(&pentry->lock);
      cache_inode_close(pentry, pgcparam->pclient, &status);
      V_w(&pentry->lock);

      pgcparam->nb_to_be_purged--;
    }

  /* return true for continuing */
  if(pgcparam->nb_to_be_purged == 0)
    return FALSE;
  else
    return TRUE;
}

/**
 * Garbagge opened file descriptors
 */
cache_inode_status_t cache_inode_gc_fd(cache_inode_client_t * pclient,
                                       cache_inode_status_t * pstatus)
{
  cache_inode_param_gc_t gcparam;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* nothing to do if there is no fd cache */
  if(!pclient->use_cache)
    return *pstatus;

  /* do not garbage FD too frequently (wait at least for fd retention) */
  if(time(NULL) - pclient->time_of_last_gc_fd < pclient->retention)
    return *pstatus;

  gcparam.ht = NULL;            /* not used */
  gcparam.pclient = pclient;
  gcparam.nb_to_be_purged = pclient->max_fd_per_thread;

  if(LRU_apply_function(pclient->lru_gc, cache_inode_gc_fd_func, (void *)&gcparam) !=
     LRU_LIST_SUCCESS)
    {
      *pstatus = CACHE_INODE_LRU_ERROR;
      return *pstatus;
    }

  LogDebug(COMPONENT_CACHE_INODE_GC,
                    "File descriptor GC: %u files closed",
                    pclient->max_fd_per_thread - gcparam.nb_to_be_purged);
  pclient->time_of_last_gc_fd = time(NULL);

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}

/* @} */
