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
 * \file    cache_inode_init.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:44:40 $
 * \version $Revision: 1.21 $
 * \brief   Init the cache_inode.
 *
 * cache_inode_init.c : Initialization routines for the cache_inode.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_init: Init the ressource necessary for the cache inode management.
 * 
 * Init the ressource necessary for the cache inode management.
 * 
 * @param param [IN] the parameter for this cache. 
 * @param pstatus [OUT] pointer to buffer used to store the status for the operation.
 *
 * @return NULL if operation failed, other value is a pointer to the hash table used for the cache.
 *
 */
hash_table_t *cache_inode_init(cache_inode_parameter_t param,
                               cache_inode_status_t * pstatus)
{
  hash_table_t *ht = NULL;

  ht = HashTable_Init(param.hparam);

  if(ht != NULL)
    *pstatus = CACHE_INODE_SUCCESS;
  else
    *pstatus = CACHE_INODE_INVALID_ARGUMENT;

  LogEvent(COMPONENT_CACHE_INODE, "Using write-back (asynchronous) metadata cache");
  /* Return the hashtable */
  return ht;
}                               /* cache_inode_init */

/**
 *
 * cache_inode_client_init: Init the ressource necessary for the cache inode management on the client handside.
 * 
 * Init the ressource necessary for the cache inode management on the client handside.
 * 
 * @param pclient      [OUT] the pointer to the client to be initiated.
 * @param param        [IN]  the parameter for this cache client. 
 * @param thread_index [IN]  an integer related to the 'position' of the thread, from 0 to Nb_Workers -1 
 *
 * @return 0 if successful, 1 if failed. 
 *
 */
int cache_inode_client_init(cache_inode_client_t * pclient,
                            cache_inode_client_parameter_t param,
                            int thread_index, void *pworker_data)
{
  LRU_status_t lru_status;
  pthread_mutexattr_t mutexattr;

  pclient->attrmask = param.attrmask;
  pclient->nb_prealloc = param.nb_prealloc_entry;
  pclient->nb_pre_dir_data = param.nb_pre_dir_data;
  pclient->nb_pre_parent = param.nb_pre_parent;
  pclient->nb_pre_state_v4 = param.nb_pre_state_v4;
  pclient->nb_pre_async_op_desc = param.nb_pre_async_op_desc;
  pclient->nb_pre_create_dirs = param.nb_pre_create_dirs;
  pclient->nb_pre_create_files = param.nb_pre_create_files;
  pclient->grace_period_attr = param.grace_period_attr;
  pclient->grace_period_link = param.grace_period_link;
  pclient->grace_period_dirent = param.grace_period_dirent;
  pclient->use_test_access = param.use_test_access;
  pclient->getattr_dir_invalidation = param.getattr_dir_invalidation;
  pclient->call_since_last_gc = 0;
  pclient->time_of_last_gc = time(NULL) + thread_index * 20;    /* All the thread will not gc at the same time */
  pclient->pworker = pworker_data;
  pclient->use_cache = param.use_cache;
  pclient->retention = param.retention;
  pclient->max_fd_per_thread = param.max_fd_per_thread;

  strncpy(pclient->pre_create_obj_dir, param.pre_create_obj_dir, MAXPATHLEN);

  pclient->avail_precreated_dirs = 0;
  pclient->avail_precreated_files = 0;
  pclient->dir_pool_fileid = NULL;
  pclient->dir_pool_handle = NULL;
  pclient->file_pool_fileid = NULL;
  pclient->file_pool_handle = NULL;

  if(pthread_mutexattr_init(&mutexattr) != 0)
    return 1;

  if(pthread_mutex_init(&pclient->pool_lock, &mutexattr) != 0)
    return 1;

  MakePool(&pclient->pool_entry, pclient->nb_prealloc, cache_entry_t, NULL, NULL);
  NamePool(&pclient->pool_entry, "Cache Inode Client Entry Pool for Worker %d", thread_index);
  if(!IsPoolPreallocated(&pclient->pool_entry))
    {
      LogCrit(COMPONENT_CACHE_INODE, "Error : can't init cache_inode client entry pool for Worker %d", thread_index);
      return 1;
    }

  MakePool(&pclient->pool_dir_data, pclient->nb_pre_dir_data, cache_inode_dir_data_t, NULL, NULL);
  NamePool(&pclient->pool_dir_data, "Cache Inode Client Dir Data Pool for Worker %d", thread_index);
  if(!IsPoolPreallocated(&pclient->pool_dir_data))
    {
      LogCrit(COMPONENT_CACHE_INODE,
                   "Error : can't init cache_inode client dir data pool for Worker %d", thread_index);
      return 1;
    }

  MakePool(&pclient->pool_parent, pclient->nb_pre_parent, cache_inode_parent_entry_t, NULL, NULL);
  NamePool(&pclient->pool_parent, "Cache Inode Client Parent Link Pool for Worker %d", thread_index);
  if(!IsPoolPreallocated(&pclient->pool_parent))
    {
      LogCrit(COMPONENT_CACHE_INODE,
                   "Error : can't init cache_inode client parent link pool for Worker %d", thread_index);
      return 1;
    }

  MakePool(&pclient->pool_state_v4, pclient->nb_pre_state_v4, cache_inode_state_t, NULL, NULL);
  NamePool(&pclient->pool_state_v4, "Cache Inode Client State V4 Pool for Worker %d", thread_index);
  if(!IsPoolPreallocated(&pclient->pool_state_v4))
    {
      LogCrit(COMPONENT_CACHE_INODE,
                   "Error : can't init cache_inode client state v4 pool for Worker %d", thread_index);
      return 1;
    }

  /* TODO: this can't possibly compile... */
  MakePool(&pclient->pool_async_op, pclient->nb_pre_async_op_desc, cache_inode_async_op_desc_t, NULL, NULL);
  NamePool(&pclient->pool_async_op, "Cache Inode Client Async Op Pool for Worker %d", thread_index);
  if(!IsPoolPreallocated(&pclient->pool_async_op))
    {
      LogCrit(COMPONENT_CACHE_INODE, "Error : can't init cache_inode async op pool for Worker %d", thread_index);
      return 1;
    }

  MakePool(&pclient->pool_key, pclient->nb_prealloc, cache_inode_fsal_data_t, NULL, NULL);
  NamePool(&pclient->pool_key, "Cache Inode Client Key Pool for Worker %d", thread_index);
  if(!IsPoolPreallocated(&pclient->pool_key))
    {
      LogCrit(COMPONENT_CACHE_INODE,
                   "Error : can't init cache_inode client key pool for Worker %d", thread_index);
      return 1;
    }

  if((pclient->lru_gc = LRU_Init(param.lru_param, &lru_status)) == NULL)
    {
      LogCrit(COMPONENT_CACHE_INODE, "Error : can't init cache_inode client lru gc for Worker %d", thread_index);
      return 1;
    }

  /* Everything was ok, return 0 */
  return 0;
}                               /* cache_inode_client_init */
