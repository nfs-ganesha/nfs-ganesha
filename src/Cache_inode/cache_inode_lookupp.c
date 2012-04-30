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
 * \file    cache_inode_lookupp.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.5 $
 * \brief   Perform lookup through the cache to get the parent entry for a directory.
 *
 * cache_inode_lookupp.c : Perform lookup through the cache to get the parent entry for a directory.
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
#include "cache_inode_weakref.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief: Implements parent lookup functionality
 *
 * Looks up (and caches) the parent directory for a directory.  If an
 * entry is returned, that entry's refcount is incremented by one.
 * The caller must hold a reference on the entry.  It is expected that
 * the caller holds the directory lock on entry.  It is also expected
 * that the caller will relinquish the directory lock after return.
 * If result was not cached, the function will drop the read lock and
 * acquire a write lock so it can add the result to the cache.
 *
 * @param entry [IN] Entry whose parent is to be obtained
 * @param client [INOUT] Per-thread resource control structure
 * @param context [IN] FSAL operation context
 * @param status [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when
 *                               validating the entry
 */

cache_entry_t *
cache_inode_lookupp_impl(cache_entry_t *entry,
                         cache_inode_client_t *client,
                         fsal_op_context_t *context,
                         cache_inode_status_t *status)
{
     cache_entry_t *parent = NULL;
     fsal_status_t fsal_status;
     fsal_attrib_list_t object_attributes;
     cache_inode_fsal_data_t fsdata;

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     /* Never even think of calling FSAL_lookup on root/.. */

     if (entry->object.dir.root) {
          /* Bump the refcount on the current entry (so the caller's
             releasing decrementing it doesn't take us below the
             sentinel count */
          if (cache_inode_lru_ref(entry, client, 0) !=
              CACHE_INODE_SUCCESS) {
               /* This cannot actually happen */
               LogFatal(COMPONENT_CACHE_INODE,
                        "There has been a grave failure in consistency: "
                        "Unable to increment reference count on an entry that "
                        "on which we should have a referenced.");
          }
          return entry;
     }

     /* Try the weakref to the parent first.  This increments the
        refcount. */
     parent = cache_inode_weakref_get(&entry->object.dir.parent,
                                      client,
                                      LRU_REQ_INITIAL);
     if (!parent) {
          /* If we didn't find it, drop the read lock, get a write
             lock, and make sure nobody filled it in while we waited. */
          pthread_rwlock_unlock(&entry->content_lock);
          pthread_rwlock_wrlock(&entry->content_lock);
          parent = cache_inode_weakref_get(&entry->object.dir.parent,
                                           client,
                                           LRU_REQ_INITIAL);
     }

     if (!parent) {
          fsal_handle_t parent_handle;
          memset(&parent_handle, 0, sizeof(fsal_handle_t));

          memset(&object_attributes, 0, sizeof(fsal_attrib_list_t));
          object_attributes.asked_attributes = client->attrmask;
          fsal_status =
               FSAL_lookup(&entry->handle, (fsal_name_t *) &FSAL_DOT_DOT,
                           context, &parent_handle, &object_attributes);

          if(FSAL_IS_ERROR(fsal_status)) {
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(entry, client);
               }
               *status = cache_inode_error_convert(fsal_status);
               return NULL;
          }

          /* Call cache_inode_get to populate the cache with the parent entry */
          fsdata.fh_desc.start = (caddr_t) &parent_handle;
          fsdata.fh_desc.len = 0;
          FSAL_ExpandHandle(context->export_context,
                            FSAL_DIGEST_SIZEOF,
                            &fsdata.fh_desc);


          /* Call cache_inode_get to populate the cache with the
             parent entry.  This increments the refcount. */
          if((parent = cache_inode_get(&fsdata,
                                       &object_attributes,
                                       client,
                                       context,
                                       status)) == NULL) {
               return NULL;
          }

          /* Link in a weak reference */
          entry->object.dir.parent = parent->weakref;
     }

     return parent;
} /* cache_inode_lookupp_impl */

/**
 *
 * @brief Public function to look up a directory's parent
 *
 * This function looks up (and potentially caches) the parent of a
 * directory.
 *
 * If a cache entry is returned, its refcount is +1.
 *
 * @param pentry [IN] Entry whose parent is to be obtained.
 * @param pclient [INOUT] Structure for per-thread resouce allocation
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] Returned status
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when
 *                               validating the entry
 *
 */
cache_entry_t *cache_inode_lookupp(cache_entry_t *pentry,
                                   cache_inode_client_t *pclient,
                                   fsal_op_context_t *pcontext,
                                   cache_inode_status_t *pstatus)
{
     cache_entry_t *parent_entry = NULL;
     pthread_rwlock_rdlock(&pentry->content_lock);
     parent_entry = cache_inode_lookupp_impl(pentry, pclient, pcontext, pstatus);
     pthread_rwlock_unlock(&pentry->content_lock);
     return parent_entry;
} /* cache_inode_lookupp */
