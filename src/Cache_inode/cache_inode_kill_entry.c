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
 *
 * \File    cache_inode_kill_entry.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:51 $
 * \version $Revision: 1.63 $
 * \brief   Some routines for management of the cache_inode layer, shared by other calls.
 *
 * Some routines for management of the cache_inode layer, shared by
 * other calls.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"
#include "stuff_alloc.h"
#include "nfs4_acls.h"
#include "sal_functions.h"

/**
 *
 * @brief Forcibly remove an entry from the cache
 *
 * This function removes an entry from the cache immediately when it
 * has become unusable (for example, when the FSAL declares it to be
 * stale.)  This function removes only one reference.  The caller must
 * also un-reference any reference it holds above the sentinel.  This
 * function does not touch locks.  Since the entry isn't actually
 * removed until the refcount falls to 0, we just let the caller
 * remove locks as for any error.
 *
 * @param entry [in] The entry to be killed
 * @param client [in,out] Structure to manage per-thread resources
 */

void
cache_inode_kill_entry(cache_entry_t *entry,
                       cache_inode_client_t *client)
{
     cache_inode_fsal_data_t fsaldata;
     hash_buffer_t key;
     hash_buffer_t val;
     int rc = 0;

     memset(&fsaldata, 0, sizeof(fsaldata));

     LogInfo(COMPONENT_CACHE_INODE,
             "Using cache_inode_kill_entry for entry %p", entry);

     cache_inode_unpinnable(entry);
     state_wipe_file(entry, client);

     fsaldata.fh_desc = entry->fh_desc;
     FSAL_ExpandHandle(NULL,
                       FSAL_DIGEST_SIZEOF,
                       &fsaldata.fh_desc);

     /* Use the handle to build the key */
     key.pdata = fsaldata.fh_desc.start;
     key.len = fsaldata.fh_desc.len;

     val.pdata = entry;
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

     cache_inode_weakref_delete(&entry->weakref);

     /* return HashTable (sentinel) reference */
     cache_inode_lru_unref(entry, client, LRU_FLAG_NONE);
}                               /* cache_inode_kill_entry */
