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
 * @file    cache_inode_create.c
 * @brief   Creation of a file through the cache layer.
 *
 * Creation of an entry through the cache layer
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
 * @brief Creates an object in a directory
 *
 * This function creates an entry in the cache and underlying
 * filesystem.  If an entry is returned, its refcount charged to the
 * call path is +1.  An entry is returned for both CACHE_INODE_SUCCESS
 * and CACHE_INODE_ENTRY_EXISTS.
 *
 * @param[in]  parent     Parent directory
 * @param[in]  name       Name of the object to create
 * @param[in]  type       Type of the object to create
 * @param[in]  mode       Mode to be used at file creation
 * @param[in]  create_arg Additional argument for object creation
 * @param[out] attr       Attributes of the new object
 * @param[in]  context    FSAL credentials
 * @param[out] status     Returned status
 *
 * @return Cache entry for the file created or found.
 */

cache_entry_t *
cache_inode_create(cache_entry_t *parent,
                   const char *name,
                   object_file_type_t type,
                   uint32_t mode,
                   cache_inode_create_arg_t *create_arg,
                   struct attrlist *attr,
                   struct req_op_context *req_ctx,
                   cache_inode_status_t *status)
{
     cache_entry_t *entry = NULL;
     fsal_status_t fsal_status = {0, 0};
     struct fsal_obj_handle *object_handle;
     struct attrlist object_attributes;
     struct fsal_obj_handle *dir_handle;
     cache_inode_create_arg_t zero_create_arg;
     fsal_accessflags_t access_mask = 0;

     memset(&zero_create_arg, 0, sizeof(zero_create_arg));
     memset(&object_attributes, 0, sizeof(object_attributes));

     if (create_arg == NULL) {
          create_arg = &zero_create_arg;
     }

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     if ((type != REGULAR_FILE) && (type != DIRECTORY) &&
         (type != SYMBOLIC_LINK) && (type != SOCKET_FILE) &&
         (type != FIFO_FILE) && (type != CHARACTER_FILE) &&
         (type != BLOCK_FILE)) {
          *status = CACHE_INODE_BAD_TYPE;

          entry = NULL;
          goto out;
        }

    /*
     * Check if caller is allowed to perform the operation
     */
    access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK) |
                  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE |
                                     FSAL_ACE_PERM_ADD_SUBDIRECTORY);
    *status = cache_inode_access(parent,
                                access_mask,
                                req_ctx, status);
    if (*status != CACHE_INODE_SUCCESS)
        {
          entry = NULL;
          goto out;
        }

     /* Check if an entry of the same name exists */
     entry = cache_inode_lookup(parent,
                                name,
                                &object_attributes,
                                req_ctx,
                                status);
     if (entry != NULL) {
          *status = CACHE_INODE_ENTRY_EXISTS;
          if (entry->type != type) {
               /* Incompatible types, returns NULL */
               cache_inode_lru_unref(entry, LRU_FLAG_NONE);
               entry = NULL;
               goto out;
          } else {
               goto out;
          }
     }

     /* The entry doesn't exist, so we can create it. */

    dir_handle = parent->obj_handle;
/* we pass in attributes to the create.  We will get them back below */
    object_attributes.owner = req_ctx->creds->caller_uid;
    object_attributes.group = req_ctx->creds->caller_gid; /* be more selective? */
    object_attributes.mode = mode;

    switch (type) {
    case REGULAR_FILE:
            fsal_status = dir_handle->ops->create(dir_handle, req_ctx,
                                                  name,
                                                  &object_attributes,
                                                  &object_handle);
            break;

    case DIRECTORY:
            fsal_status = dir_handle->ops->mkdir(dir_handle, req_ctx,
                                                 name,
                                                 &object_attributes,
                                                 &object_handle);
            break;

    case SYMBOLIC_LINK:
            fsal_status = dir_handle->ops->symlink(dir_handle, req_ctx,
                                                   name,
                                                   create_arg->link_content,
                                                   &object_attributes,
                                                   &object_handle);
            break;

        case SOCKET_FILE:
        case FIFO_FILE:
            fsal_status = dir_handle->ops->mknode(dir_handle, req_ctx,
                                                  name,
                                                  type,
                                                  NULL, /* no dev_t needed */
                                                  &object_attributes,
                                                  &object_handle);
            break;

        case BLOCK_FILE:
        case CHARACTER_FILE:
            fsal_status = dir_handle->ops->mknode(dir_handle, req_ctx,
                                                  name,
                                                  type,
                                                  &create_arg->dev_spec,
                                                  &object_attributes,
                                                  &object_handle);
            break;

    default:
            /* we should never go there */
            *status = CACHE_INODE_INCONSISTENT_ENTRY;
            entry = NULL;
            goto out;
            break;
    }

     /* Check for the result */
     if (FSAL_IS_ERROR(fsal_status)) {
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(parent);
          }
          *status = cache_inode_error_convert(fsal_status);
          entry = NULL;
          goto out;
     }
     entry = cache_inode_new_entry(object_handle,
                                   CACHE_INODE_FLAG_CREATE,
                                   status);
     if (entry == NULL) {
          goto out;
     }

     pthread_rwlock_wrlock(&parent->content_lock);
     /* Add this entry to the directory (also takes an internal ref) */
     cache_inode_add_cached_dirent(parent,
                                   name, entry,
                                   NULL,
                                   status);
     pthread_rwlock_unlock(&parent->content_lock);
     if (*status != CACHE_INODE_SUCCESS) {
          cache_inode_lru_unref(entry, LRU_FLAG_NONE);
          entry = NULL;
          goto out;
     }

     pthread_rwlock_wrlock(&parent->attr_lock);
     /* Update the parent cached attributes */
     cache_inode_set_time_current(&parent->obj_handle->attributes.mtime);
     parent->obj_handle->attributes.ctime = parent->obj_handle->attributes.mtime;
     /* if the created object is a directory, it contains a link
        to its parent : '..'. Thus the numlink attr must be increased. */
     if (type == DIRECTORY) {
          ++(parent->obj_handle->attributes.numlinks);
     }
     /* Copy up the child attributes */
     if (attr) {
          *attr = entry->obj_handle->attributes;
     }
     pthread_rwlock_unlock(&parent->attr_lock);

     *status = CACHE_INODE_SUCCESS;

out:

     return entry;
}
