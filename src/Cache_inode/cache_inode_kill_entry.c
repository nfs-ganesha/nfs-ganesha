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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup Cache_inode Cache Inode
 * @{
 */

/**
 *
 * @File cache_inode_kill_entry.c
 * @brief Destroy stale entries
 */
#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "log.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs4_acls.h"
#include "sal_functions.h"

/**
 *
 * @brief Forcibly remove an entry from the cache (top half)
 *
 * This function is used to invalidate a cache entry when it
 * has become unusable (for example, when the FSAL declares it to be
 * stale).
 *
 * To simplify interaction with the SAL, this function no longer
 * calls finalizes the entry, but schedules the entry for out-of-line
 * cleanup, after first making it unreachable.
 *
 * The entry refcount is not decremented, logically the sentinel
 * ref is owned by the cleanup queue.
 *
 * @param[in] entry The entry to be killed
 */

void
cache_inode_kill_entry(cache_entry_t *entry)
{
     struct gsh_buffdesc key;
     struct gsh_buffdesc val;
     struct fsal_obj_handle *pfsal_handle = entry->obj_handle;
     struct gsh_buffdesc fh_desc;
     int rc = 0;

     LogInfo(COMPONENT_CACHE_INODE,
             "Using cache_inode_kill_entry for entry %p", entry);

     cache_inode_unpinnable(entry);
     cache_inode_lru_cleanup_push(entry);

     /* Use the handle to build the key */
     pfsal_handle->ops->handle_to_key(pfsal_handle, &fh_desc);
     key.addr = fh_desc.addr;
     key.len = fh_desc.len;

     val.addr = entry;
     val.len = sizeof(cache_entry_t);

     if ((rc = HashTable_DelSafe(fh_to_cache_entry_ht,
                                 &key,
                                 &val)) != HASHTABLE_SUCCESS) {
          if (rc != HASHTABLE_ERROR_NO_SUCH_KEY) {
               LogCrit(COMPONENT_CACHE_INODE,
                       "cache_inode_kill_entry: entry could not be deleted, "
                       " status = %d",
                       rc);
          }
     }

} /* cache_inode_kill_entry */
/** @} */

