/*
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
 * @file cache_inode_remove.c
 * @brief Removes an entry of any type.
 */

#include "config.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_hash.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "HashTable.h"
#include "nfs4_acls.h"
#include "sal_functions.h"
#include "nfs_core.h"
#include "nfs_tools.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

/**
 *
 * @brief Remove a name from a directory.
 *
 * Removes a name from the supplied directory.  The caller should hold
 * no locks on the directory.
 *
 * @param[in] entry   Entry for the parent directory to be managed
 * @param[in] name    Name to be removed
 * @param[in] req_ctx Request context
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_remove(cache_entry_t *entry,
		   const char *name,
		   struct req_op_context *req_ctx)
{
     cache_entry_t *to_remove_entry = NULL;
     fsal_status_t fsal_status = {0, 0};
     cache_inode_status_t status = CACHE_INODE_SUCCESS;
     fsal_accessflags_t access_mask = 0;
     bool to_remove_entry_locked = false;
     bool sticky_status;

     if(entry->type != DIRECTORY) {
         status = CACHE_INODE_BAD_TYPE;
         goto out;
     }

     /* Check if caller is allowed to perform the operation */
     access_mask = (FSAL_MODE_MASK_SET(FSAL_W_OK) |
		    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD));

     status = cache_inode_access(entry,
                                 access_mask,
                                 req_ctx);
     if (status != CACHE_INODE_SUCCESS) {
          goto out;
     }

     /* Factor this somewhat.  In the case where the directory hasn't
        been populated, the entry may not exist in the cache and we'd
        be bringing it in just to dispose of it. */

     /* Looks up for the entry to remove */
     PTHREAD_RWLOCK_rdlock(&entry->content_lock);
     status = cache_inode_lookup_impl(entry,
				      name,
				      req_ctx,
				      &to_remove_entry);
     PTHREAD_RWLOCK_unlock(&entry->content_lock);

     if (to_remove_entry == NULL) {
	 goto out;
     }

     PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

     PTHREAD_RWLOCK_wrlock(&to_remove_entry->attr_lock);
     to_remove_entry_locked = true;

     sticky_status = sticky_dir_allows(entry->obj_handle,
			   to_remove_entry->obj_handle,
			   req_ctx->creds);

     PTHREAD_RWLOCK_unlock(&entry->attr_lock);

     if (!sticky_status) {
         status = CACHE_INODE_FSAL_EPERM;
         goto out;
     }

     LogDebug(COMPONENT_CACHE_INODE,
              "---> Cache_inode_remove : %s", name);


     fsal_status = entry->obj_handle->ops->unlink(entry->obj_handle, req_ctx,
                                                  name);

     if(FSAL_IS_ERROR(fsal_status)) {
         status = cache_inode_error_convert(fsal_status);
         if (fsal_status.major == ERR_FSAL_STALE) {
             cache_inode_kill_entry(entry);
         }
         goto out;
     }

     /* Update the attributes for the removed entry */
     status = cache_inode_refresh_attrs(to_remove_entry, req_ctx);
     if (status == CACHE_INODE_FSAL_ESTALE) {
         status = CACHE_INODE_SUCCESS;
     }

     if (status != CACHE_INODE_SUCCESS) {
         goto out;
     }

     PTHREAD_RWLOCK_unlock(&to_remove_entry->attr_lock);
     to_remove_entry_locked = false;

     status = cache_inode_refresh_attrs_locked(entry, req_ctx);
     if (status != CACHE_INODE_SUCCESS) {
         goto out;
     }

     PTHREAD_RWLOCK_wrlock(&entry->content_lock);

     /* Remove the entry from parent dir_entries avl */
     cache_inode_remove_cached_dirent(entry, name, req_ctx);

     PTHREAD_RWLOCK_unlock(&entry->content_lock);

out:
     LogFullDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_remove_cached_dirent: status=%d", status);

     if (to_remove_entry_locked) {
         PTHREAD_RWLOCK_unlock(&to_remove_entry->attr_lock);
     }

     /* This is for the reference taken by lookup */
     if (to_remove_entry) {
         cache_inode_put(to_remove_entry);
     }

     return status;
}
/** @} */
