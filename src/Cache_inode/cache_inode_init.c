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

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "sal_data.h"
#include "stuff_alloc.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"

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

  ht = HashTable_Init(&param.hparam);

  if(ht != NULL)
    *pstatus = CACHE_INODE_SUCCESS;
  else
    *pstatus = CACHE_INODE_INVALID_ARGUMENT;

  cache_inode_weakref_init();

  LogInfo(COMPONENT_CACHE_INODE, "Hash Table initiated");

  return ht;
}                               /* cache_inode_init */

/**
 *
 * cache_inode_client_init: Init the ressource necessary for the cache inode management on the client handside.
 *
 * Init the ressource necessary for the cache inode management on the client handside.
 *
 * @param client      [OUT] the pointer to the client to be initiated.
 * @param paramp        [IN]  the parameter for this cache client.
 * @param thread_index [IN]  an integer related to the 'position' of the thread, from 0 to Nb_Workers -1
 *
 * @return 0 if successful, 1 if failed.
 *
 */
int cache_inode_client_init(cache_inode_client_t *client,
                            cache_inode_client_parameter_t *param,
                            int thread_index,
                            struct nfs_worker_data__ *pworker_data)
{
  char name[256];

  if(thread_index < SMALL_CLIENT_INDEX)
    sprintf(name, "Cache Inode Worker #%d", thread_index);
  else if(thread_index == SMALL_CLIENT_INDEX)
    sprintf(name, "Cache Inode Small Client");
  else
    sprintf(name, "Cache Inode NLM Async #%d", thread_index - NLM_THREAD_INDEX);

  client->attrmask = param->attrmask;
  client->nb_prealloc = param->nb_prealloc_entry;
  client->nb_pre_state_v4 = param->nb_pre_state_v4;
  client->expire_type_attr = param->expire_type_attr;
  client->expire_type_link = param->expire_type_link;
  client->expire_type_dirent = param->expire_type_dirent;
  client->grace_period_attr = param->grace_period_attr;
  client->grace_period_link = param->grace_period_link;
  client->grace_period_dirent = param->grace_period_dirent;
  client->use_test_access = param->use_test_access;
  client->getattr_dir_invalidation = param->getattr_dir_invalidation;
  client->pworker = pworker_data;

  MakePool(&client->pool_entry, client->nb_prealloc, cache_entry_t, NULL, NULL);
  NamePool(&client->pool_entry, "%s Entry Pool", name);
  if(!IsPoolPreallocated(&client->pool_entry))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init %s Entry Pool", name);
      return 1;
    }

  MakePool(&client->pool_dir_entry, client->nb_prealloc, cache_inode_dir_entry_t, NULL, NULL);
  NamePool(&client->pool_dir_entry, "%s Dir Entry Pool", name);
  if(!IsPoolPreallocated(&client->pool_dir_entry))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init %s Dir Entry Pool", name);
      return 1;
    }

  MakePool(&client->pool_state_v4, client->nb_pre_state_v4, state_t, NULL, NULL);
  NamePool(&client->pool_state_v4, "%s State V4 Pool", name);
  if(!IsPoolPreallocated(&client->pool_state_v4))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init %s State V4 Pool", name);
      return 1;
    }

  /* TODO: warning - entries in this pool are never released! */
  MakePool(&client->pool_state_owner, client->nb_pre_state_v4, state_owner_t, NULL, NULL);
  NamePool(&client->pool_state_owner, "%s Open Owner Pool", name);
  if(!IsPoolPreallocated(&client->pool_state_owner))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init %s Open Owner Pool", name);
      return 1;
    }

  /* TODO: warning - entries in this pool are never released! */
  MakePool(&client->pool_nfs4_owner_name, client->nb_pre_state_v4, state_nfs4_owner_name_t, NULL, NULL);
  NamePool(&client->pool_nfs4_owner_name, "%s Open Owner Name Pool", name);
  if(!IsPoolPreallocated(&client->pool_nfs4_owner_name))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init %s Open Owner Name Pool", name);
      return 1;
    }
#ifdef _USE_NFS4_1
  /* TODO: warning - entries in this pool are never released! */
  MakePool(&client->pool_session, client->nb_pre_state_v4, nfs41_session_t, NULL, NULL);
  NamePool(&client->pool_session, "%s Session Pool", name);
  if(!IsPoolPreallocated(&client->pool_session))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init %s Session Pool", name);
      return 1;
    }
#endif                          /* _USE_NFS4_1 */

  MakePool(&client->pool_key, client->nb_prealloc, cache_inode_fsal_data_t, NULL, NULL);
  NamePool(&client->pool_key, "%s Key Pool", name);
  if(!IsPoolPreallocated(&client->pool_key))
    {
      LogFatal(COMPONENT_CACHE_INODE,
              "Can't init %s Key Pool", name);
      return 1;
    }

  /* Everything was ok, return 0 */
  return 0;
}                               /* cache_inode_client_init */
