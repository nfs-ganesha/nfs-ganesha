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

#include "log.h"
#include "HashData.h"
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
 * @param[out] status Operation status
 *
 * @return NULL if operation failed, other value is a pointer to the hash table used for the cache.
 *
 */
hash_table_t *cache_inode_init(cache_inode_parameter_t param,
                               cache_inode_status_t *status)
{
  hash_table_t *ht = NULL;

  cache_inode_entry_pool = pool_init("Entry Pool",
                                     sizeof(cache_entry_t),
                                     pool_basic_substrate,
                                     NULL, NULL, NULL);
  if(!(cache_inode_entry_pool))
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Can't init Entry Pool");
      *status = CACHE_INODE_INVALID_ARGUMENT;
      return NULL;
    }

  ht = HashTable_Init(&param.hparam);

  if(ht != NULL)
    *status = CACHE_INODE_SUCCESS;
  else
    *status = CACHE_INODE_INVALID_ARGUMENT;
  LogInfo(COMPONENT_CACHE_INODE, "Hash Table initiated");

  cache_inode_weakref_init();

  return ht;
}                               /* cache_inode_init */
