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
 * @file    cache_inode_invalidate.c
 * @brief   Invalidate the cached data on a cache entry
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "abstract_atomic.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief invalidates an entry in the cache
 *
 * This function invalidates the related cache entry correponding to a
 * FSAL handle. It is designed to be called when an FSAL upcall is
 * triggered.
 *
 * @param[in]  handle FSAL handle for the entry to be invalidated
 * @param[out] status Returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_INVALID_ARGUMENT bad parameter(s) as input
 * @retval CACHE_INODE_NOT_FOUND if entry is not cached
 * @retval CACHE_INODE_STATE_CONFLICT if invalidating this entry would
 *                                    result is state conflict
 * @retval CACHE_INODE_INCONSISTENT_ENTRY if entry is not consistent
 * @retval Other errors shows a FSAL error.
 *
 */
cache_inode_status_t
cache_inode_invalidate(cache_inode_fsal_data_t *fsal_data,
                       cache_inode_status_t *status,
                       uint32_t flags)
{
     hash_buffer_t key, value;
     int rc = 0 ;
     cache_entry_t *entry;
     struct hash_latch latch;

     if (status == NULL || fsal_data == NULL) {
          *status = CACHE_INODE_INVALID_ARGUMENT;
          goto out;
     }

     /* Locate the entry in the cache */
     FSAL_ExpandHandle(NULL,  /* pcontext but not used... */
                       FSAL_DIGEST_SIZEOF,
                       &fsal_data->fh_desc);

     /* Turn the input to a hash key */
     key.pdata = fsal_data->fh_desc.start;
     key.len = fsal_data->fh_desc.len;

     if ((rc = HashTable_GetLatch(fh_to_cache_entry_ht,
                                  &key,
                                  &value,
                                  FALSE,
                                  &latch)) == HASHTABLE_ERROR_NO_SUCH_KEY) {
          /* Entry is not cached */
          HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
          *status = CACHE_INODE_NOT_FOUND;
          return *status;
     } else if (rc != HASHTABLE_SUCCESS) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "Unexpected error %u while calling HashTable_GetLatch", rc) ;
          *status = CACHE_INODE_INVALID_ARGUMENT;
          goto out;
     }

     entry = value.pdata;
     if (cache_inode_lru_ref(entry, 0) != CACHE_INODE_SUCCESS) {
          HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
          *status = CACHE_INODE_NOT_FOUND;
          return *status;
     }
     HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);

     PTHREAD_RWLOCK_WRLOCK(&entry->attr_lock);
     PTHREAD_RWLOCK_WRLOCK(&entry->content_lock);

     /* We can invalidate entries with state just fine.  We force
        Cache_inode to contact the FSAL for any use of content or
        attributes, and if the FSAL indicates the entry is stale, it
        can be disposed of then. */

     /* We should have a way to invalidate content and attributes
        separately.  Or at least a way to invalidate attributes
        without invalidating content (since any change in content
        really ought to modify mtime, at least.) */

     if ((flags & CACHE_INODE_INVALIDATE_CLEARBITS) != 0)
       atomic_clear_uint32_t_bits(&entry->flags,
                                  CACHE_INODE_TRUST_ATTRS |
                                  CACHE_INODE_DIR_POPULATED |
                                  CACHE_INODE_TRUST_CONTENT);

     /* The main reason for holding the lock at this point is so we
        don't clear the trust bits while someone is populating the
        directory or refreshing attributes. */

     if (((flags & CACHE_INODE_INVALIDATE_CLOSE) != 0) &&
         (entry->type == REGULAR_FILE)) {
          cache_inode_close(entry,
                            (CACHE_INODE_FLAG_REALLYCLOSE |
                             CACHE_INODE_FLAG_CONTENT_HAVE |
                             CACHE_INODE_FLAG_CONTENT_HOLD),
                            status);
     }

     PTHREAD_RWLOCK_UNLOCK(&entry->attr_lock);
     PTHREAD_RWLOCK_UNLOCK(&entry->content_lock);

     cache_inode_lru_unref(entry, 0);

out:

     /* Memory copying attributes with every call is expensive.
        Let's not do it.  */

     return (*status);
} /* cache_inode_invalidate */
