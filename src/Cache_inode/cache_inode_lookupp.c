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

#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief Implements parent lookup functionality
 *
 * Looks up (and caches) the parent directory for a directory.  If an
 * entry is returned, that entry's refcount is incremented by one.
 * The caller must hold a reference on the entry.  It is expected that
 * the caller holds the directory lock on entry.  It is also expected
 * that the caller will relinquish the directory lock after return.
 * If result was not cached, the function will drop the read lock and
 * acquire a write lock so it can add the result to the cache.
 *
 * @param[in]  entry   Entry whose parent is to be obtained
 * @param[in]  context FSAL operation context
 * @param[out] status  Returned status
 * NOTE: this can behave differently than a local Linux user of the
 * filesystem if the path to get here is a symlink.  In the local case,
 * the path is consistent because the kernel's CWD gets the expected
 * "..".  We don't have that so the ".." is the directory in which
 * the name resides even though the symlink allowed us to skip around
 * the real (non symlink resolved) path.
 *
 * @return the found entry or NULL on error.
 */

cache_entry_t *
cache_inode_lookupp_impl(cache_entry_t *entry,
                         struct req_op_context *req_ctx,
                         cache_inode_status_t *status)
{
     cache_entry_t *parent = NULL;
     fsal_status_t fsal_status;
     struct attrlist object_attributes;
     cache_inode_fsal_data_t fsdata;

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     /* Never even think of calling FSAL_lookup on root/.. */

     if (entry->type == DIRECTORY && entry->object.dir.root) {
          /* Bump the refcount on the current entry (so the caller's
             releasing decrementing it doesn't take us below the
             sentinel count */
          if (cache_inode_lru_ref(entry, 0) !=
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
                                      LRU_REQ_INITIAL);
     if (!parent) {
          /* If we didn't find it, drop the read lock, get a write
             lock, and make sure nobody filled it in while we waited. */
          pthread_rwlock_unlock(&entry->content_lock);
          pthread_rwlock_wrlock(&entry->content_lock);
          parent = cache_inode_weakref_get(&entry->object.dir.parent,
                                           LRU_REQ_INITIAL);
     }

     if (!parent) {
	  struct fsal_obj_handle *parent_handle;

	  fsal_status = entry->obj_handle->ops->lookup(entry->obj_handle,
                                                       req_ctx, "..",
						       &parent_handle);
          if(FSAL_IS_ERROR(fsal_status)) {
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(entry);
               }
               *status = cache_inode_error_convert(fsal_status);
               return NULL;
          }

          /* Call cache_inode_get to populate the cache with the
             parent entry.  This increments the refcount. */
          parent_handle->ops->handle_to_key(parent_handle, &fsdata.fh_desc);
          fsdata.export = parent_handle->export;

          if((parent = cache_inode_get(&fsdata,
                                       &object_attributes,
                                       entry,
                                       req_ctx,
                                       status)) == NULL) {
               return NULL;
          }
/** @TODO  Danger Will Robinson!  cache_inode_get should consume the parent_handle
 *  but this may be a leak!
 */
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
 * @param[in]  entry   Entry whose parent is to be obtained.
 * @param[in]  context FSAL credentials
 * @param[out] status  Returned status
 *
 * @return the found entry or NULL on error.
 */

cache_entry_t *
cache_inode_lookupp(cache_entry_t *entry,
                    struct req_op_context *req_ctx,
                    cache_inode_status_t *status)
{
     cache_entry_t *parent = NULL;
     pthread_rwlock_rdlock(&entry->content_lock);
     parent = cache_inode_lookupp_impl(entry, req_ctx, status);
     pthread_rwlock_unlock(&entry->content_lock);
     return parent;
} /* cache_inode_lookupp */
