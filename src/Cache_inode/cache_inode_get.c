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
 * \file    cache_inode_get.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.26 $
 * \brief   Get and eventually cache an entry.
 *
 * cache_inode_get.c : Get and eventually cache an entry.
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
#include "cache_inode_lru.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief Gets an entry by using its fsdata as a key and caches it if needed.
 *
 * Gets an entry by using its fsdata as a key and caches it if needed.
 *
 * If a cache entry is returned, its refcount is incremented by one.
 *
 * It turns out we do need cache_inode_get_located functionality for
 * cases like lookupp on an entry returning itself when it isn't a
 * root.  Therefore, if the 'associated' parameter is equal to the got
 * cache entry, a reference count is incremented but the structure
 * pointed to by attr is NOT filled in.
 *
 * @param[in]     fsdata     File system data
 * @param[out]    attr       The attributes of the got entry
 * @param[in,out] client     Resource management structure
 * @param[in]     context    FSAL credentials
 * @param[in]     associated Entry that may be equal to the got entry
 * @param[out]    status     Returned status
 *
 * @return If successful, the pointer to the entry; NULL otherwise
 *
 */
cache_entry_t *
cache_inode_get(cache_inode_fsal_data_t *fsdata,
                fsal_attrib_list_t *attr,
                cache_inode_client_t *client,
                fsal_op_context_t *context,
                cache_entry_t *associated,
                cache_inode_status_t *status)
{
     hash_buffer_t key, value;
     cache_entry_t *entry = NULL;
     fsal_status_t fsal_status = {0, 0};
     cache_inode_create_arg_t create_arg = {
          .newly_created_dir = FALSE
     };
     cache_inode_file_type_t type = UNASSIGNED;
     hash_error_t hrc = 0;
     fsal_attrib_list_t fsal_attributes;
     fsal_handle_t *file_handle;
     struct hash_latch latch;

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     /* Turn the input to a hash key on our own.
      */
     key.pdata = fsdata->fh_desc.start;
     key.len = fsdata->fh_desc.len;

     hrc = HashTable_GetLatch(fh_to_cache_entry_ht, &key, &value,
                              FALSE,
                              &latch);

     if ((hrc != HASHTABLE_SUCCESS) &&
         (hrc != HASHTABLE_ERROR_NO_SUCH_KEY)) {
          /* This should not happened */
          *status = CACHE_INODE_HASH_TABLE_ERROR;
          LogCrit(COMPONENT_CACHE_INODE,
                  "Hash access failed with code %d"
                  " - this should not have happened",
                  hrc);
          return NULL;
     }

     if (hrc == HASHTABLE_SUCCESS) {
          /* Entry exists in the cache and was found */
          entry = value.pdata;
          /* take an extra reference within the critical section */
          if (cache_inode_lru_ref(entry, client, LRU_REQ_INITIAL) !=
              CACHE_INODE_SUCCESS) {
               /* Dead entry.  Treat like a lookup failure. */
               entry = NULL;
          } else {
               if (entry == associated) {
                    /* Take a quick exit so we don't invert lock
                       ordering. */
                    HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
                    return entry;
               }
          }
     }
     HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);

     if (!client && !context) {
          /* Upcalls have no access to a cache_inode_client_t,
             so just return the entry without revalidating it or
             creating a new one. */
          if (entry == NULL) {
               *status = CACHE_INODE_NOT_FOUND;
          }
          return entry;
     }

     if (!entry) {
          /* Cache miss, allocate a new entry */
          file_handle = (fsal_handle_t *) fsdata->fh_desc.start;
          /* First, call FSAL to know what the object is */
          fsal_attributes.asked_attributes = client->attrmask;
          fsal_status
               = FSAL_getattrs(file_handle, context, &fsal_attributes);
          if (FSAL_IS_ERROR(fsal_status)) {
               *status = cache_inode_error_convert(fsal_status);
               LogDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_get: cache_inode_status=%u "
                        "fsal_status=%u,%u ", *status,
                        fsal_status.major,
                        fsal_status.minor);
               return NULL;
          }

          /* The type has to be set in the attributes */
          if (!FSAL_TEST_MASK(fsal_attributes.supported_attributes,
                              FSAL_ATTR_TYPE)) {
               *status = CACHE_INODE_FSAL_ERROR;
               return NULL;
          }

          /* Get the cache_inode file type */
          type = cache_inode_fsal_type_convert(fsal_attributes.type);
          if (type == SYMBOLIC_LINK) {
               fsal_attributes.asked_attributes = client->attrmask;
               fsal_status =
                    FSAL_readlink(file_handle, context,
                                  &create_arg.link_content,
                                  &fsal_attributes);

               if (FSAL_IS_ERROR(fsal_status)) {
                    *status = cache_inode_error_convert(fsal_status);
                    return NULL;
               }
          }
          if ((entry
               = cache_inode_new_entry(fsdata,
                                       &fsal_attributes,
                                       type,
                                       &create_arg,
                                       client,
                                       context,
                                       CACHE_INODE_FLAG_NONE,
                                       status)) == NULL) {
               return NULL;
          }

     }

     *status = CACHE_INODE_SUCCESS;

     /* This is the replacement for cache_inode_renew_entry.  Rather
        than calling that function at the start of every cache_inode
        call with the inode locked, we call cache_inode_check trust to
        perform 'heavyweight' (timed expiration of cached attributes,
        getattr-based directory trust) checks the first time after
        getting an inode.  It does all of the checks read-locked and
        only acquires a write lock if there's something requiring a
        change.

        There is a second light-weight check done before use of cached
        data that checks whether the bits saying that inode attributes
        or inode content are trustworthy have been cleared by, for
        example, FSAL_CB.

        To summarize, the current implementation is that policy-based
        trust of validity is checked once per logical series of
        operations at cache_inode_get, and asynchronous trust is
        checked with use (when the attributes are locked for reading,
        for example.) */

     if ((*status = cache_inode_check_trust(entry,
                                            context,
                                            client))
         != CACHE_INODE_SUCCESS) {
       goto out_put;
     }

     /* Set the returned attributes */
     *status = cache_inode_lock_trust_attrs(entry, context, client);

     /* cache_inode_lock_trust_attrs may fail, in that case, the
        attributes are wrong and pthread_rwlock_unlock can't be called
        again */
     if(*status != CACHE_INODE_SUCCESS)
       {
         goto out_put;
       }
     *attr = entry->attributes;
     pthread_rwlock_unlock(&entry->attr_lock);

     return entry;

 out_put:
     cache_inode_put(entry, client);
     entry = NULL;
     return entry;
} /* cache_inode_get */

/**
 *
 * cache_inode_put:  release logical reference to a cache entry conferred by
 * a previous call to cache_inode_get (cache_inode_get_located).
 *
 * The result is typically to decrement the reference count on entry, but
 * additional side effects include LRU adjustment, movement to/from the
 * protected LRU partition, or recyling if the caller has raced an operation
 * which made entry unreachable (and this current caller has the last
 * reference).  Caller MUST NOT make further accesses to the memory pointed
 * to by entry.
 *
 * @param entry [IN] cache entry being returned
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 *
 * @return status.
 *
 */
cache_inode_status_t cache_inode_put(cache_entry_t *entry,
                                     cache_inode_client_t *pclient)
{
  return (cache_inode_lru_unref(entry, pclient, LRU_FLAG_NONE));
}
