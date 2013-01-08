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
 * @defgroup Cache_inode Cache Inode
 * @{
 */

/**
 * @file cache_inode_init.c
 * @brief Init the cache_inode.
 */

#include "config.h"
#include "log.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "sal_data.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * @brief Initialize the caching layer
 *
 * This function initializes the memory pools, hash table, and weakref
 * table used for cache management.
 *
 * @param[in]  param  The parameters for this cache
 * @param[out] ht     The cache inode hash table
 *
 * @return CACHE_INODE_SUCCESS or errors.
 *
 */
cache_inode_status_t cache_inode_init(cache_inode_parameter_t param,
				      hash_table_t **ht)
{
  cache_inode_status_t status = CACHE_INODE_SUCCESS;
  *ht = NULL;

  cache_inode_entry_pool = pool_init("Entry Pool",
                                     sizeof(cache_entry_t),
                                     pool_basic_substrate,
                                     NULL, NULL, NULL);
  if(!(cache_inode_entry_pool))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init Entry Pool");
      status = CACHE_INODE_INVALID_ARGUMENT;
    }

  *ht = HashTable_Init(&param.hparam);

  if(*ht != NULL)
    status = CACHE_INODE_SUCCESS;
  else
    status = CACHE_INODE_INVALID_ARGUMENT;
  LogInfo(COMPONENT_CACHE_INODE, "Hash Table initiated");

  cache_inode_weakref_init();

  return status;
}                               /* cache_inode_init */
/** @} */
