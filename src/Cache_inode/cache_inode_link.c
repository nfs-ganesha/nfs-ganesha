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
 * @file    cache_inode_link.c
 * @brief   Creation of hard links
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
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
 * @brief Links a new name to a file
 *
 * This function hard links a new name to an existing file.
 *
 * @param[in]  entry    The file to which to add the new name.  Must
 *                      not be a directory.
 * @param[in]  dest_dir The directory in which to create the new name
 * @param[in]  name     The new name to add to the file
 * @param[in]  req_ctx  FSAL credentials
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_BAD_TYPE either source or destination have
 *                              incorrect type
 * @retval CACHE_INODE_ENTRY_EXISTS entry of that name already exists
 *                                  in destination.
 */
cache_inode_status_t cache_inode_link(cache_entry_t *entry,
				      cache_entry_t *dest_dir,
				      const char *name,
				      struct req_op_context *req_ctx)
{
     fsal_status_t fsal_status = {0, 0};
     bool srcattrlock = false;
     bool destattrlock = false;
     bool destdirlock = false;
     fsal_accessflags_t access_mask = 0;
     fsal_acl_t *saved_acl = NULL;
     fsal_acl_status_t acl_status = 0;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;


     /* The file to be hardlinked can't be a DIRECTORY */
     if (entry->type == DIRECTORY) {
          status = CACHE_INODE_BAD_TYPE;
          goto out;
     }


     /* Is the destination a directory? */
     if ((dest_dir->type != DIRECTORY) &&
         (dest_dir->type != FS_JUNCTION)) {
          status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     /* Acquire the attribute lock */
     PTHREAD_RWLOCK_wrlock(&dest_dir->attr_lock);
     destattrlock = true;

     /* Check if caller is allowed to perform the operation */
     access_mask = (FSAL_MODE_MASK_SET(FSAL_W_OK) |
                    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE));

     status = cache_inode_access_sw(dest_dir,
				    access_mask,
				    req_ctx,
				    false);

     if (status != CACHE_INODE_SUCCESS) {
          goto out;
     }

     /* Rather than performing a lookup first, just try to make the
        link and return the FSAL's error if it fails. */

     PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
     srcattrlock = true;

     /* Acquire the directory entry lock */
     PTHREAD_RWLOCK_wrlock(&dest_dir->content_lock);
     destdirlock = true;

     /* Do the link at FSAL level */
     saved_acl = entry->obj_handle->attributes.acl;
     fsal_status = entry->obj_handle->ops->link(entry->obj_handle, req_ctx,
                                                dest_dir->obj_handle, name);
     if(!FSAL_IS_ERROR(fsal_status))
             fsal_status =
                     entry->obj_handle->ops->getattrs(entry->obj_handle,
                                                      req_ctx);
     if (FSAL_IS_ERROR(fsal_status)) {
          status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               fsal_status
                       = entry->obj_handle->ops->getattrs(entry->obj_handle,
                                                          req_ctx);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(entry);
               }
               fsal_status
                       = dest_dir->obj_handle->ops
                       ->getattrs(dest_dir->obj_handle, req_ctx);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(dest_dir);
               }
          }
          goto out;
     } else {
          /* Decrement refcount on saved ACL */
         nfs4_acl_release_entry(saved_acl, &acl_status);
         if (acl_status != NFS_V4_ACL_SUCCESS) {
              LogCrit(COMPONENT_CACHE_INODE,
                      "Failed to release old acl, status=%d",
                      acl_status);
         }
     }

     cache_inode_fixup_md(entry);
     PTHREAD_RWLOCK_unlock(&entry->attr_lock);
     srcattrlock = false;

     /* Reload the destination directory's attributes so the caller
        will have an updated changeid. */
     cache_inode_refresh_attrs(dest_dir, req_ctx);
     PTHREAD_RWLOCK_unlock(&dest_dir->attr_lock);
     destattrlock = false;

     /* Add the new entry in the destination directory */
     status = cache_inode_add_cached_dirent(dest_dir,
					    name,
					    entry,
					    NULL);

     if (status  != CACHE_INODE_SUCCESS) {
          goto out;
     }

     PTHREAD_RWLOCK_unlock(&dest_dir->content_lock);
     destdirlock = false;

out:

     if (srcattrlock) {
          PTHREAD_RWLOCK_unlock(&entry->attr_lock);
     }

     if (destattrlock) {
          PTHREAD_RWLOCK_unlock(&dest_dir->attr_lock);
     }

     if (destdirlock) {
          PTHREAD_RWLOCK_unlock(&dest_dir->content_lock);
     }

     return status;
}
/** @} */
