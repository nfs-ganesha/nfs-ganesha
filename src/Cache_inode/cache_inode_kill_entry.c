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
 *
 * \File    cache_inode_kill_entry.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:51 $
 * \version $Revision: 1.63 $
 * \brief   Some routines for management of the cache_inode layer, shared by other calls.
 *
 * cache_inode_kill_entry.c : Some routines for management of the cache_inode layer, shared by other calls.
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
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"
#include "nfs4_acls.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>


/**
 *
 * cache_inode_kill_entry: force removing an entry from the cache_inode. This is used in case of a 'stale' entry.
 *
 * Force removing an entry from the cache_inode. This is used in case of a 'stale' entry.
 *
 * @param pentry  [IN] the input pentry (supposed to be staled).
 * @param locked  [IN] tells in the entry was previously locked and how it was locked
 * @param ht      [INOUT] the related hash table for the cache_inode cache.
 * @param pclient [INOUT] related cache_inode client.
 * @param pstatus [OUT] status for the operation.
 *
 * @return CACHE_INODE_BAD_TYPE if pentry is not related a REGULAR_FILE or
 * DIRECTORY
 * @return CACHE_INODE_SUCCESS if operation succeded.
 *
 */
static void free_lock( cache_entry_t          * pentry,
                       cache_inode_lock_how_t   lock_how )
{
  switch( lock_how )
    {
      case RD_LOCK:
        V_r( &pentry->lock ) ;
        break ;
 
      case WT_LOCK:
        V_w( &pentry->lock ) ;
        break ;

      case NO_LOCK:
      default:
        break ;
    }
}

cache_inode_status_t cache_inode_kill_entry( cache_entry_t          * pentry,
                                             cache_inode_lock_how_t   lock_how,  
                                             hash_table_t           * ht,
                                             cache_inode_client_t   * pclient,
                                             cache_inode_status_t   * pstatus )
{
  fsal_handle_t *pfsal_handle = NULL;
  cache_inode_fsal_data_t fsaldata;
  cache_inode_parent_entry_t *parent_iter = NULL;
  cache_inode_parent_entry_t *parent_iter_next = NULL;
  hash_buffer_t key, old_key;
  hash_buffer_t old_value;
  int rc;
  fsal_status_t fsal_status;

  memset( (char *)&fsaldata, 0, sizeof( fsaldata ) ) ;

  LogInfo(COMPONENT_CACHE_INODE,
          "Using cache_inode_kill_entry for entry %p", pentry);

  /* Invalidation is not for junctions or special files */
  if( ( pentry->internal_md.type == FS_JUNCTION )    ||
      ( pentry->internal_md.type == SOCKET_FILE )    ||
      ( pentry->internal_md.type == FIFO_FILE )      ||
      ( pentry->internal_md.type == CHARACTER_FILE ) ||
      ( pentry->internal_md.type == BLOCK_FILE ) )
   {
     free_lock( pentry, lock_how ) ; 

     *pstatus = CACHE_INODE_SUCCESS;
     return *pstatus;
   }

#if 0
  /** @todo: BUGAZOMEU : directory invalidation seems quite tricky, temporarily avoid it */
  if( pentry->internal_md.type == DIRECTORY )
   {
     free_lock( pentry, lock_how ) ; 

     *pstatus = CACHE_INODE_SUCCESS;
     return *pstatus;
   }

  /** @todo: BUGAZOMEU : file invalidation seems quite tricky, temporarily avoid it */
  /* We need to know how to manage how to deal with "files with states"  */
  if( pentry->internal_md.type == REGULAR_FILE )
   {
     free_lock( pentry, lock_how ) ; 

     *pstatus = CACHE_INODE_SUCCESS;
     return *pstatus;
   }
#endif

  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pentry == NULL || pclient == NULL || ht == NULL)
    {
      free_lock( pentry, lock_how ) ; 

      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Get the FSAL handle */
  if((pfsal_handle = cache_inode_get_fsal_handle(pentry, pstatus)) == NULL)
    {
      free_lock( pentry, lock_how ) ; 

      LogCrit(COMPONENT_CACHE_INODE,
              "cache_inode_kill_entry: unable to retrieve pentry's specific filesystem info");
      return *pstatus;
    }

  /* Invalidate the related LRU gc entry (no more required) */
  if(pentry->gc_lru_entry != NULL)
    {
      if(LRU_invalidate(pentry->gc_lru, pentry->gc_lru_entry) != LRU_LIST_SUCCESS)
        {
          free_lock( pentry, lock_how ) ; 

          *pstatus = CACHE_INODE_LRU_ERROR;
          return *pstatus;
        }
    }

  fsaldata.handle = *pfsal_handle;
  fsaldata.cookie = DIR_START;

  /* Use the handle to build the key */
  if(cache_inode_fsaldata_2_key(&key, &fsaldata, pclient))
    {
      free_lock( pentry, lock_how ) ; 

      LogCrit(COMPONENT_CACHE_INODE,
              "cache_inode_kill_entry: could not build hashtable key");

      cache_inode_release_fsaldata_key(&key, pclient);
      *pstatus = CACHE_INODE_NOT_FOUND;
      return *pstatus;
    }

  /* use the key to delete the entry */
  if((rc = HashTable_Del(ht, &key, &old_key, &old_value)) != HASHTABLE_SUCCESS)
    {
      if( rc != HASHTABLE_ERROR_NO_SUCH_KEY) /* rc=3 => Entry was previously removed */
        LogCrit( COMPONENT_CACHE_INODE,
                 "cache_inode_kill_entry: entry could not be deleted, status = %d",
                 rc);

      cache_inode_release_fsaldata_key(&key, pclient);

      *pstatus = CACHE_INODE_NOT_FOUND;
      return *pstatus;
    }

  /* Release the hash key data */
  cache_inode_release_fsaldata_key(&old_key, pclient);

  /* Clean up the associated ressources in the FSAL */
  if(FSAL_IS_ERROR(fsal_status = FSAL_CleanObjectResources(pfsal_handle)))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "cache_inode_kill_entry: Couldn't free FSAL ressources fsal_status.major=%u",
              fsal_status.major);
    }

  /* Sanity check: old_value.pdata is expected to be equal to pentry,
   * and is released later in this function */
  if((cache_entry_t *) old_value.pdata != pentry)
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "cache_inode_kill_entry: unexpected pdata %p from hash table (pentry=%p)",
              old_value.pdata, pentry);
    }

  /* Release the current key */
  cache_inode_release_fsaldata_key(&key, pclient);

  /* Recover the parent list entries */
  parent_iter = pentry->parent_list;
  while(parent_iter != NULL)
    {
      parent_iter_next = parent_iter->next_parent;

      ReleaseToPool(parent_iter, &pclient->pool_parent);

      parent_iter = parent_iter_next;
    }

  /* If entry is datacached, remove it from the cache */
  if(pentry->internal_md.type == REGULAR_FILE)
    {
      cache_content_status_t cache_content_status;

      if(pentry->object.file.pentry_content != NULL)
        if(cache_content_release_entry
           ((cache_content_entry_t *) pentry->object.file.pentry_content,
            (cache_content_client_t *) pclient->pcontent_client,
            &cache_content_status) != CACHE_CONTENT_SUCCESS)
          LogCrit(COMPONENT_CACHE_INODE,
                  "Could not removed datacached entry for pentry %p", pentry);
    }

  /* If entry is a DIRECTORY, invalidate dirents */
  if(pentry->internal_md.type == DIRECTORY)
    {
	cache_inode_invalidate_related_dirents(pentry, pclient);
    }

  // free_lock( pentry, lock_how ) ; /* Really needed ? The pentry is unaccessible now and will be destroyed */

  /* Destroy the mutex associated with the pentry */
  cache_inode_mutex_destroy(pentry);

  /* Put the pentry back to the pool */
  ReleaseToPool(pentry, &pclient->pool_entry);

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}                               /* cache_inode_kill_entry */
