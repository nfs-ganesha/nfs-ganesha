/**
 *
 * \file    cache_inode_remove.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/31 10:18:58 $
 * \version $Revision: 1.32 $
 * \brief   Removes an entry of any type.
 */

/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_is_dir_empty: checks if a directory is empty or not. No mutex management. 
 *
 * Checks if a directory is empty or not. No mutex management 
 *
 * @param pentry [IN] entry to be checked (should be of type DIR_BEGINNING)
 *
 * @return CACHE_INODE_SUCCESS is directory is empty\n
 * @return CACHE_INODE_BAD_TYPE is pentry is not of type DIR_BEGINNING\n
 * @return CACHE_INODE_DIR_NOT_EMPTY if pentry is a non empty DIR_BEGINNING
 *
 */
cache_inode_status_t cache_inode_is_dir_empty(cache_entry_t * pentry)
{
  cache_inode_status_t status;
  cache_entry_t *pentry_iter;

  /* Sanity check */
  if(pentry->internal_md.type != DIR_BEGINNING)
    return CACHE_INODE_BAD_TYPE;

  /* Initialisation */
  status = CACHE_INODE_SUCCESS;
  pentry_iter = pentry;

  do
    {
      if(pentry_iter->internal_md.type == DIR_BEGINNING)
        {
          if(pentry_iter->object.dir_begin.nbactive != 0)
            {
              status = CACHE_INODE_DIR_NOT_EMPTY;
              break;
            }

          if(pentry_iter->object.dir_begin.end_of_dir == END_OF_DIR)
            break;

          pentry_iter = pentry_iter->object.dir_begin.pdir_cont;
        }
      else
        {
          if(pentry_iter->object.dir_cont.nbactive != 0)
            {
              status = CACHE_INODE_DIR_NOT_EMPTY;
              break;
            }

          if(pentry_iter->object.dir_cont.end_of_dir == END_OF_DIR)
            break;

          pentry_iter = pentry_iter->object.dir_cont.pdir_cont;
        }
    }
  while(pentry_iter != NULL);

  return status;
}                               /* cache_inode_is_dir_empty */

/**
 *
 * cache_inode_is_dir_empty_WithLock: checks if a directory is empty or not, BUT has lock management.
 *
 * Checks if a directory is empty or not, BUT has lock management.
 *
 * @param pentry [IN] entry to be checked (should be of type DIR_BEGINNING)
 *
 * @return CACHE_INODE_SUCCESS is directory is empty\n
 * @return CACHE_INODE_BAD_TYPE is pentry is not of type DIR_BEGINNING\n
 * @return CACHE_INODE_DIR_NOT_EMPTY if pentry is a non empty DIR_BEGINNING
 *
 */
cache_inode_status_t cache_inode_is_dir_empty_WithLock(cache_entry_t * pentry)
{
  cache_inode_status_t status;

  P_r(&pentry->lock);
  status = cache_inode_is_dir_empty(pentry);
  V_r(&pentry->lock);

  return status;
}                               /* cache_inode_is_dir_empty_WithLock */

/**
 * cache_inode_clean_internal: remove a pentry from cache and all LRUs,
 *                             and release related resources.
 *
 * @param pentry [IN] entry to be deleted from cache
 * @param hash_table_t [IN] The cache hash table
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 */
cache_inode_status_t cache_inode_clean_internal(cache_entry_t * to_remove_entry,
                                                hash_table_t * ht,
                                                cache_inode_client_t * pclient)
{
  fsal_handle_t *pfsal_handle_remove;
  cache_inode_parent_entry_t *parent_iter = NULL;
  cache_inode_parent_entry_t *parent_iter_next = NULL;
  cache_inode_fsal_data_t fsaldata;
  cache_inode_status_t status;
  hash_buffer_t key, old_key, old_value;
  int rc;

  if((pfsal_handle_remove =
      cache_inode_get_fsal_handle(to_remove_entry, &status)) == NULL)
    {
      return status;
    }

  /* Invalidate the related LRU gc entry (no more required) */
  if(to_remove_entry->gc_lru_entry != NULL)
    {
      if(LRU_invalidate(to_remove_entry->gc_lru, to_remove_entry->gc_lru_entry)
         != LRU_LIST_SUCCESS)
        {
          return CACHE_INODE_LRU_ERROR;
        }
    }

  /* delete the entry from the cache */
  fsaldata.handle = *pfsal_handle_remove;
  if(to_remove_entry->internal_md.type != DIR_CONTINUE)
    fsaldata.cookie = DIR_START;
  else
    fsaldata.cookie = to_remove_entry->object.dir_cont.dir_cont_pos;

  if(cache_inode_fsaldata_2_key(&key, &fsaldata, pclient))
    {
      return CACHE_INODE_INCONSISTENT_ENTRY;
    }

  /* use the key to delete the entry */
  rc = HashTable_Del(ht, &key, &old_key, &old_value);

  if(rc)
    LogCrit(COMPONENT_CACHE_INODE,
                      "HashTable_Del error %d in cache_inode_clean_internal", rc);

  if((rc != HASHTABLE_SUCCESS) && (rc != HASHTABLE_ERROR_NO_SUCH_KEY))
    {
      cache_inode_release_fsaldata_key(&key, pclient);
      return CACHE_INODE_INCONSISTENT_ENTRY;
    }

  /* release the key that was stored in hash table */
  if(rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      cache_inode_release_fsaldata_key(&old_key, pclient);

      /* Sanity check: old_value.pdata is expected to be equal to pentry,
       * and is released later in this function */
      if((cache_entry_t *) old_value.pdata != to_remove_entry)
        {
          LogCrit(COMPONENT_CACHE_INODE,
                            "cache_inode_remove: unexpected pdata %p from hash table (pentry=%p)",
                            old_value.pdata, to_remove_entry);
        }
    }

  /* release the key used for hash query */
  cache_inode_release_fsaldata_key(&key, pclient);

  /* Free the parent list entries */

  parent_iter = to_remove_entry->parent_list;
  while(parent_iter != NULL)
    {
      parent_iter_next = parent_iter->next_parent;

      RELEASE_PREALLOC(parent_iter, pclient->pool_parent, next_alloc);

      parent_iter = parent_iter_next;
    }

  /* If entry is a DIR_CONTINUE or a DIR_BEGINNING, release pdir_data */
  if(to_remove_entry->internal_md.type == DIR_BEGINNING)
    {
      /* Put the pentry back to the pool */
      RELEASE_PREALLOC(to_remove_entry->object.dir_begin.pdir_data,
                       pclient->pool_dir_data, next_alloc);
    }

  if(to_remove_entry->internal_md.type == DIR_CONTINUE)
    {
      /* Put the pentry back to the pool */
      RELEASE_PREALLOC(to_remove_entry->object.dir_cont.pdir_data,
                       pclient->pool_dir_data, next_alloc);
    }

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_clean_internal */

/**
 *
 * cache_inode_remove_sw: removes a pentry addressed by its parent pentry and its FSAL name. Mutex management is switched.
 * 
 * Removes a pentry addressed by its parent pentry and its FSAL name. Mutex management is switched.
 *
 * @param pentry  [IN]     entry for the parent directory to be managed.
 * @param name    [IN]     name of the entry that we are looking for in the cache.
 * @param pattr   [OUT]    attributes for the entry that we have found.
 * @param ht      [IN]     hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext   [IN]    FSAL credentials 
 * @param pstatus [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_remove_sw(cache_entry_t * pentry,             /**< Parent entry */
                                           fsal_name_t * pnode_name,
                                           fsal_attrib_list_t * pattr,
                                           hash_table_t * ht,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_inode_status_t * pstatus, int use_mutex)
{
  fsal_status_t fsal_status;
  cache_entry_t *parent_entry;
  cache_entry_t *pentry_iter;
  cache_entry_t *pentry_next;
  cache_entry_t *to_remove_entry;
  fsal_handle_t fsal_handle_parent;
  fsal_attrib_list_t remove_attr;
  fsal_attrib_list_t after_attr;
  cache_inode_status_t status;
  cache_content_status_t cache_content_status;
  int to_remove_numlinks = 0;
  int pnfs_status;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_REMOVE] += 1;

  /* pentry is a directory */
  if(use_mutex)
    P_w(&pentry->lock);

  /* Check if caller is allowed to perform the operation */
  if((status = cache_inode_access_sw(pentry,
                                     FSAL_W_OK,
                                     ht,
                                     pclient,
                                     pcontext, &status, FALSE)) != CACHE_INODE_SUCCESS)
    {
      *pstatus = status;

      /* pentry is a directory */
      if(use_mutex)
        V_w(&pentry->lock);

      return *pstatus;
    }

  /* Looks up for the entry to remove */
  if((to_remove_entry = cache_inode_lookup_sw(pentry,
                                              pnode_name,
                                              &remove_attr,
                                              ht,
                                              pclient, pcontext, &status, FALSE)) == NULL)
    {
      *pstatus = status;

      /* pentry is a directory */
      if(use_mutex)
        V_w(&pentry->lock);

      return *pstatus;
    }

  /* lock it */
  if(use_mutex)
    P_w(&to_remove_entry->lock);

  if(pentry->internal_md.type != DIR_BEGINNING
     && pentry->internal_md.type != DIR_CONTINUE)
    {
      if(use_mutex)
        {
          V_w(&to_remove_entry->lock);
          V_w(&pentry->lock);
        }

      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  LogFullDebug(COMPONENT_CACHE_INODE, "---> Cache_inode_remove : %s\n", pnode_name->name);

  /* Non-empty directories should not be removed. If entry is of type DIR_CONTINUE, then the directory is not empty */
  if(to_remove_entry->internal_md.type == DIR_CONTINUE)
    {
      if(use_mutex)
        {
          V_w(&to_remove_entry->lock);
          V_w(&pentry->lock);
        }

      *pstatus = CACHE_INODE_DIR_NOT_EMPTY;
      return *pstatus;
    }

  /* A directory is empty if none of its pdir_chain items contains something */
  if(to_remove_entry->internal_md.type == DIR_BEGINNING &&
     to_remove_entry->object.dir_begin.has_been_readdir == CACHE_INODE_YES)
    {
      if(cache_inode_is_dir_empty(to_remove_entry) != CACHE_INODE_SUCCESS)
        {
          if(use_mutex)
            {
              V_w(&to_remove_entry->lock);
              V_w(&pentry->lock);
            }

          *pstatus = CACHE_INODE_DIR_NOT_EMPTY;
          return *pstatus;
        }
    }

  /* We have to get parent's fsal handle */
  parent_entry = pentry;

  /* /!\ Possible deadlocks in this area: make sure to P(DIR_BEGIN)/P(DIR_CONT)/V(DIR_CONT)/V(DIR_BEGIN) */

  if(pentry->internal_md.type == DIR_BEGINNING)
    {
      fsal_handle_parent = pentry->object.dir_begin.handle;
    }
  else if(pentry->internal_md.type == DIR_CONTINUE)
    {
      if(use_mutex)
        P_r(&pentry->object.dir_cont.pdir_begin->lock);

      fsal_handle_parent = pentry->object.dir_cont.pdir_begin->object.dir_begin.handle;

      if(use_mutex)
        V_r(&pentry->object.dir_cont.pdir_begin->lock);
    }

  if(status == CACHE_INODE_SUCCESS)
    {
      /* Remove the file from FSAL */
      after_attr.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
      cache_inode_get_attributes(pentry, &after_attr);
      fsal_status = MFSL_unlink(&pentry->mobject,
                                pnode_name,
                                &to_remove_entry->mobject,
                                pcontext, &pclient->mfsl_context, &after_attr);
#else
      fsal_status = FSAL_unlink(&fsal_handle_parent, pnode_name, pcontext, &after_attr);
#endif

      /* Set the 'after' attr */
      if(pattr != NULL)
        *pattr = after_attr;

      if(FSAL_IS_ERROR(fsal_status))
        {
          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                  "cache_inode_remove: Stale FSAL FH detected for pentry %p", pentry);

              if(cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                    "cache_inode_remove: Could not kill entry %p, status = %u",
                     pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          *pstatus = cache_inode_error_convert(fsal_status);
          if(use_mutex)
            {
              V_w(&to_remove_entry->lock);
              V_w(&pentry->lock);
            }
          return *pstatus;
        }
    }
  else
    {
      if(use_mutex)
        {
          V_w(&to_remove_entry->lock);
          V_w(&pentry->lock);
        }
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_REMOVE] += 1;
      return status;
    }

  /* Remove the entry from parent dir_entries array */
  cache_inode_remove_cached_dirent(pentry, pnode_name, ht, pclient, &status);

  LogFullDebug(COMPONENT_CACHE_INODE,
                    "cache_inode_remove_cached_dirent: status=%d", status);

  /* Update the cached attributes */
  if(pentry->internal_md.type == DIR_BEGINNING)
    {
      pentry->object.dir_begin.attributes = after_attr;
    }
  else if(pentry->internal_md.type == DIR_CONTINUE)
    {
      if(use_mutex)
        P_r(&pentry->object.dir_cont.pdir_begin->lock);

      pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes = after_attr;

      if(use_mutex)
        V_r(&pentry->object.dir_cont.pdir_begin->lock);
    }

  /* Update the attributes for the removed entry */

  if(remove_attr.type != FSAL_TYPE_DIR)
    {
      if(remove_attr.numlinks > 1)
        {
          switch (to_remove_entry->internal_md.type)
            {
            case SYMBOLIC_LINK:
              to_remove_entry->object.symlink.attributes.numlinks -= 1;
              to_remove_entry->object.symlink.attributes.ctime.seconds = time(NULL);
              to_remove_entry->object.symlink.attributes.ctime.nseconds = 0;
              to_remove_numlinks = to_remove_entry->object.symlink.attributes.numlinks;
              break;

            case REGULAR_FILE:
              to_remove_entry->object.file.attributes.numlinks -= 1;
              to_remove_entry->object.file.attributes.ctime.seconds = time(NULL);
              to_remove_entry->object.file.attributes.ctime.nseconds = 0;
              to_remove_numlinks = to_remove_entry->object.file.attributes.numlinks;
              break;

            case CHARACTER_FILE:
            case BLOCK_FILE:
            case SOCKET_FILE:
            case FIFO_FILE:
              to_remove_entry->object.special_obj.attributes.numlinks -= 1;
              to_remove_entry->object.special_obj.attributes.ctime.seconds = time(NULL);
              to_remove_entry->object.special_obj.attributes.ctime.nseconds = 0;
              to_remove_numlinks =
                  to_remove_entry->object.special_obj.attributes.numlinks;
              break;

            default:
              /* Other objects should not be hard linked */
              if(use_mutex)
                {
                  V_w(&to_remove_entry->lock);
                  V_w(&pentry->lock);
                }
              *pstatus = CACHE_INODE_BAD_TYPE;
              return *pstatus;
              break;
            }
        }
    }
  else
    {
      /* No hardlink counter to be decremented for a directory: hardlink are not allowed for them */
    }

  /* Now, delete "to_remove_entry" from the cache inode and free its associated resources, but only if numlinks == 0 */
  if(to_remove_numlinks == 0)
    {

      /* If pentry is a regular file, data cached, the related data cache entry should be removed as well */
      if(to_remove_entry->internal_md.type == REGULAR_FILE)
        {
          if(to_remove_entry->object.file.pentry_content != NULL)
            {
              /* Something is to be deleted, release the cache data entry */
              if(cache_content_release_entry
                 ((cache_content_entry_t *) to_remove_entry->object.file.pentry_content,
                  (cache_content_client_t *) pclient->pcontent_client,
                  &cache_content_status) != CACHE_CONTENT_SUCCESS)
                {
                  LogEvent(COMPONENT_CACHE_INODE,
                                    "pentry %p, named %s could not be released from data cache, status=%d",
                                    to_remove_entry, pnode_name->name,
                                    cache_content_status);
                }
            }
#ifdef _USE_PNFS
          if(to_remove_entry->object.file.pnfs_file.ds_file.allocated == TRUE)
            {
              if((pnfs_status = pnfs_unlink_ds_file(&pclient->pnfsclient,
                                                    to_remove_entry->object.file.
                                                    attributes.fileid,
                                                    &to_remove_entry->object.file.
                                                    pnfs_file.ds_file)) != NFS4_OK)
                {
                  LogDebug(COMPONENT_CACHE_INODE, "OPEN PNFS CREATE DS FILE : Error %u",
                                  pnfs_status);

                  if(use_mutex)
                    {
                      V_w(&to_remove_entry->lock);
                      V_w(&pentry->lock);
                    }

                  *pstatus = CACHE_INODE_IO_ERROR;
                  return *pstatus;
                }

            }
#endif
        }

      /* browse and clean all DIR_CONTINUEs */
      pentry_iter = to_remove_entry;

      while(pentry_iter)
        {
          /* remove current entry from hash, clear resources and invalidate LRUs */

          if((*pstatus =
              cache_inode_clean_internal(pentry_iter, ht,
                                         pclient)) != CACHE_INODE_SUCCESS)
            {
              if(use_mutex)
                {
                  V_w(&pentry->lock);
                  V_w(&to_remove_entry->lock);
                }

              LogCrit(COMPONENT_CACHE_INODE,
                                "cache_inode_clean_internal ERROR %d", *pstatus);
              return *pstatus;
            }

          if(pentry_iter->internal_md.type == DIR_BEGINNING)
            {
              /* next step : don't stop at end of dir,
               * because it may stay dircont with inactive entries.
               */
              pentry_next = pentry_iter->object.dir_begin.pdir_cont;

            }
          else if(pentry_iter->internal_md.type == DIR_CONTINUE)
            {
              /* next step */
              pentry_next = pentry_iter->object.dir_cont.pdir_cont;

              /*  can destroy mutex and put back entry to memory pool */
              cache_inode_mutex_destroy(pentry_iter);

              RELEASE_PREALLOC(pentry_iter, pclient->pool_entry, next_alloc);
            }
          else                  /* not a directory, exiting loop */
            pentry_next = NULL;

          pentry_iter = pentry_next;
        }

      /* Finally put the main pentry back to pool */
      if(use_mutex)
        V_w(&to_remove_entry->lock);

      /* Destroy the mutex associated with the pentry */
      cache_inode_mutex_destroy(to_remove_entry);

      RELEASE_PREALLOC(to_remove_entry, pclient->pool_entry, next_alloc);
    }

  /* Validate the entries */
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_SET, pclient);

  /* Regular exit */
  if(use_mutex)
    {
      if(to_remove_numlinks != 0)
        V_w(&to_remove_entry->lock);    /* This was not release yet, it should be done here */

      V_w(&pentry->lock);
    }

  if(status == CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_success[CACHE_INODE_REMOVE] += 1;
  else
    pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_REMOVE] += 1;

  return status;
}                               /* cache_inode_remove */

/**
 *
 * cache_inode_remove_no_mutex: removes a pentry addressed by its parent pentry and its FSAL name. No mutex management.
 * 
 * Removes a pentry addressed by its parent pentry and its FSAL name.
 *
 * @param pentry  [IN]    entry for the parent directory to be managed.
 * @param name    [IN]    name of the entry that we are looking for in the cache.
 * @param pattr   [OUT]   attributes for the entry that we have found.
 * @param ht      [IN]    hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext   [IN]    FSAL credentials 
 * @param pstatus [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_remove_no_mutex(cache_entry_t * pentry,             /**< Parent entry */
                                                 fsal_name_t * pnode_name,
                                                 fsal_attrib_list_t * pattr,
                                                 hash_table_t * ht,
                                                 cache_inode_client_t * pclient,
                                                 fsal_op_context_t * pcontext,
                                                 cache_inode_status_t * pstatus)
{
  return cache_inode_remove_sw(pentry,
                               pnode_name, pattr, ht, pclient, pcontext, pstatus, FALSE);
}                               /* cache_inode_remove_no_mutex */

/**
 *
 * cache_inode_remove: removes a pentry addressed by its parent pentry and its FSAL name.
 * 
 * Removes a pentry addressed by its parent pentry and its FSAL name.
 *
 * @param pentry [IN] entry for the parent directory to be managed.
 * @param name [IN] name of the entry that we are looking for in the cache.
 * @param pattr [OUT] attributes for the entry that we have found.
 * @param ht      [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_remove(cache_entry_t * pentry,             /**< Parent entry */
                                        fsal_name_t * pnode_name,
                                        fsal_attrib_list_t * pattr,
                                        hash_table_t * ht,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus)
{
  return cache_inode_remove_sw(pentry,
                               pnode_name, pattr, ht, pclient, pcontext, pstatus, TRUE);
}                               /* cache_inode_remove_no_mutex */
