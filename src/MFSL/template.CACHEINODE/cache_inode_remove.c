/**
 *
 * \file    cache_inode_remove.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/31 10:18:58 $
 * \version $Revision: 1.32 $
 * \brief   Removes an entry of any type.
 *
 * cache_inode_remove.c : Removes an entry of any type.
 *
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode_async.h"
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

  P(pentry->lock);
  status = cache_inode_is_dir_empty(pentry);
  V(pentry->lock);

  return status;
}                               /* cache_inode_is_dir_empty_WithLock */

/**
 *
 * cache_inode_async_remove: removes an entry (to be called from a synclet)
 *
 * Removes an entry.
 *
 * @param popasyncdesc [IN] Cache Inode Asynchonous Operation descriptor
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */

fsal_status_t cache_inode_async_remove(cache_inode_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_unlink(popasyncdesc->op_args.remove.pfsal_handle,
                            &popasyncdesc->op_args.remove.name,
                            &popasyncdesc->fsal_op_context,
                            &popasyncdesc->op_res.remove.attr);

  return fsal_status;
}                               /* cache_inode_aync_setattr */

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
  cache_entry_t *to_remove_entry;
  fsal_handle_t fsal_handle_parent;
  fsal_handle_t *pfsal_handle_remove;
  cache_inode_fsal_data_t fsaldata;
  hash_buffer_t key, old_key, old_value;
  int rc;
  fsal_attrib_list_t remove_attr;
  fsal_attrib_list_t *pparent_attr;
  cache_inode_status_t status;
  cache_content_status_t cache_content_status;
  int dir_is_empty;
  int to_remove_numlinks = 0;
  cache_inode_async_op_desc_t *pasyncopdesc = NULL;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_REMOVE] += 1;

  /* Looks up for the entry to remove */
  if((to_remove_entry = cache_inode_lookup_sw(pentry,
                                              pnode_name,
                                              &remove_attr,
                                              ht,
                                              pclient,
                                              pcontext, &status, use_mutex)) == NULL)
    {
      *pstatus = status;
      return *pstatus;
    }

  /* pentry is a directory */
  if(use_mutex)
    P(pentry->lock);

  if(pentry->internal_md.type != DIR_BEGINNING
     && pentry->internal_md.type != DIR_CONTINUE)
    {
      if(use_mutex)
        V(pentry->lock);

      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Non-empty directories should not be removed. If entry is of type DIR_CONTINUE, then the directory is not empty */
  if(to_remove_entry->internal_md.type == DIR_CONTINUE)
    {
      if(use_mutex)
        V(pentry->lock);

      *pstatus = CACHE_INODE_DIR_NOT_EMPTY;
      return *pstatus;
    }

  /* A directory is empty if none of its pdir_chain itemps contains something */
  if(to_remove_entry->internal_md.type == DIR_BEGINNING &&
     to_remove_entry->object.dir_begin.has_been_readdir == CACHE_INODE_YES)
    {
      if(cache_inode_is_dir_empty(to_remove_entry) != CACHE_INODE_SUCCESS)
        {
          if(use_mutex)
            V(pentry->lock);

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
      pparent_attr = &pentry->object.dir_begin.attributes;
    }
  else if(pentry->internal_md.type == DIR_CONTINUE)
    {
      if(use_mutex)
        P(pentry->object.dir_cont.pdir_begin->lock);

      fsal_handle_parent = pentry->object.dir_cont.pdir_begin->object.dir_begin.handle;
      pparent_attr = &pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes;

      if(use_mutex)
        V(pentry->object.dir_cont.pdir_begin->lock);
    }

  if(status == CACHE_INODE_SUCCESS)
    {
      /* Remove the file from FSAL */
      fsal_status = FSAL_unlink_access(pcontext, pparent_attr);

      if(FSAL_IS_ERROR(fsal_status))
        {
          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogCrit(COMPONENT_CACHE_INODE, "cache_inode_remove: Stale FSAL FH detected for pentry %p",
                         pentry);

              if(cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogEvent(COMPONENT_CACHE_INODE,"cache_inode_remove: Could not kill entry %p, status = %u",
                           pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          *pstatus = cache_inode_error_convert(fsal_status);
          if(use_mutex)
            V(pentry->lock);
          return *pstatus;
        }

      /* Post an asynchronous operation */
      P(pclient->pool_lock);
      GetFromPool(pasyncopdesc, pclient->pool_async_op, cache_inode_async_op_desc_t);
      V(pclient->pool_lock);

      if(pasyncopdesc == NULL)
        {
          if(use_mutex)
            V(pentry->lock);
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_REMOVE] += 1;
          *pstatus = CACHE_INODE_MALLOC_ERROR;

          return *pstatus;
        }

      pasyncopdesc->op_type = CACHE_INODE_ASYNC_OP_REMOVE;
      pasyncopdesc->op_args.remove.pfsal_handle = &fsal_handle_parent;
      pasyncopdesc->op_args.remove.name = *pnode_name;
      pasyncopdesc->op_res.remove.attr.asked_attributes = FSAL_ATTRS_POSIX;
      pasyncopdesc->op_func = cache_inode_async_remove;

      pasyncopdesc->fsal_op_context = *pcontext;
      pasyncopdesc->fsal_export_context = *(pcontext->export_context);
      pasyncopdesc->fsal_op_context.export_context = &pasyncopdesc->fsal_export_context;

      pasyncopdesc->ht = ht;
      pasyncopdesc->origine_pool = pclient->pool_async_op;
      pasyncopdesc->ppool_lock = &pclient->pool_lock;

      if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
        {
          /* Could'not get time of day... Stopping, this may need a major failure */
          LogCrit(COMPONENT_CACHE_INODE,"cache_inode_remove: cannot get time of day... exiting");
          exit(1);
        }

      /* Affect the operation to a synclet */
      if(cache_inode_post_async_op(pasyncopdesc, pentry, pstatus) != CACHE_INODE_SUCCESS)
        {
          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_REMOVE] += 1;

          LogCrit(COMPONENT_CACHE_INODE,"WARNING !!! cache_inode_remove could not post async op....");

          *pstatus = CACHE_INODE_ASYNC_POST_ERROR;
          if(use_mutex)
            V(pentry->lock);
          return status;
        }

    }
  else
    {
      if(use_mutex)
        V(pentry->lock);
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
      pentry->object.dir_begin.attributes.mtime.seconds = pasyncopdesc->op_time.tv_sec;
      pentry->object.dir_begin.attributes.mtime.nseconds = pasyncopdesc->op_time.tv_usec;
      pentry->object.dir_begin.attributes.ctime.seconds = pasyncopdesc->op_time.tv_sec;
      pentry->object.dir_begin.attributes.ctime.nseconds = pasyncopdesc->op_time.tv_usec;
    }
  else if(pentry->internal_md.type == DIR_CONTINUE)
    {
      if(use_mutex)
        P(pentry->object.dir_cont.pdir_begin->lock);

      pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime.seconds =
          pasyncopdesc->op_time.tv_sec;
      pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime.nseconds =
          pasyncopdesc->op_time.tv_usec;
      pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes.ctime.seconds =
          pasyncopdesc->op_time.tv_sec;
      pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes.ctime.nseconds =
          pasyncopdesc->op_time.tv_usec;

      if(use_mutex)
        V(pentry->object.dir_cont.pdir_begin->lock);
    }

  /* Update the attributes for the removed entry */
  if(use_mutex)
    P(to_remove_entry->lock);
  if(remove_attr.type != FSAL_TYPE_DIR)
    {
      if(remove_attr.numlinks > 1)
        {
          switch (to_remove_entry->internal_md.type)
            {
            case SYMBOLIC_LINK:
              to_remove_entry->object.symlink.attributes.numlinks -= 1;
              to_remove_entry->object.symlink.attributes.ctime.seconds =
                  pasyncopdesc->op_time.tv_sec;
              to_remove_entry->object.symlink.attributes.ctime.nseconds =
                  pasyncopdesc->op_time.tv_usec;
              to_remove_numlinks = to_remove_entry->object.symlink.attributes.numlinks;
              break;

            case REGULAR_FILE:
              to_remove_entry->object.file.attributes.numlinks -= 1;
              to_remove_entry->object.file.attributes.ctime.seconds =
                  pasyncopdesc->op_time.tv_sec;
              to_remove_entry->object.file.attributes.ctime.nseconds =
                  pasyncopdesc->op_time.tv_usec;
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
                V(to_remove_entry->lock);
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

      /* Set the pentry as dead */
      to_remove_entry->async_health = CACHE_INODE_ASYNC_DEAD;

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
        }

      if((pfsal_handle_remove =
          cache_inode_get_fsal_handle(to_remove_entry, pstatus)) == NULL)
        {
          if(use_mutex)
            V(to_remove_entry->lock);

          return *pstatus;
        }

      /* Invalidate the related LRU gc entry (no more required) */
      if(to_remove_entry->gc_lru_entry != NULL)
        {
          if(LRU_invalidate(to_remove_entry->gc_lru, to_remove_entry->gc_lru_entry) !=
             LRU_LIST_SUCCESS)
            {
              *pstatus = CACHE_INODE_LRU_ERROR;

              return *pstatus;
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
          if(use_mutex)
            {
              V(to_remove_entry->lock);
              V(pentry->lock);
            }

          *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;

          return *pstatus;
        }

      /* use the key to delete the entry */
      if((rc = HashTable_Del(ht, &key, &old_key, &old_value)) != HASHTABLE_SUCCESS)
        {
          if(use_mutex)
            {
              V(to_remove_entry->lock);
              V(pentry->lock);
            }
          cache_inode_release_fsaldata_key(&key, pclient);

          *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;

          return *pstatus;
        }

      /* release the key that was stored in hash table */
      cache_inode_release_fsaldata_key(&old_key, pclient);

      /* Sanity check: old_value.pdata is expected to be equal to pentry,
       * and is released later in this function */
      if((cache_entry_t *) old_value.pdata != to_remove_entry)
        {
          LogCrit(COMPONENT_CACHE_INODE, 
                            "cache_inode_remove: unexpected pdata %p from hash table (pentry=%p)",
                            old_value.pdata, to_remove_entry);
        }

      /* release the key used for hash query */
      cache_inode_release_fsaldata_key(&key, pclient);

      /* If entry is a DIR_CONTINUE or a DIR_BEGINNING, release pdir_data */
      if(to_remove_entry->internal_md.type == DIR_BEGINNING)
        {
          /* Put the pentry back to the pool */
          ReleaseToPool(to_remove_entry->object.dir_begin.pdir_data, &pclient->pool_dir_data);
        }

      if(to_remove_entry->internal_md.type == DIR_CONTINUE)
        {
          /* Put the pentry back to the pool */
          ReleaseToPool(to_remove_entry->object.dir_cont.pdir_data, &pclient->pool_dir_data);
        }

      /* Put the pentry back to pool */
      if(use_mutex)
        V(to_remove_entry->lock);

      /* Destroy the mutex associated with the pentry */
      cache_inode_mutex_destroy(to_remove_entry);

      ReleaseToPool(to_remove_entry, &pclient->pool_entry);
    }

  /* Set the 'after' attr */
  if(pattr != NULL)
    *pattr = *pparent_attr;

  /* Validate the entries */
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_SET, pclient);

  /* Regular exit */
  if(use_mutex)
    {
      if(to_remove_numlinks != 0)
        V(to_remove_entry->lock);       /* This was not release yet, it should be done here */

      V(pentry->lock);
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
