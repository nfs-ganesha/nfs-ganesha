/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file    cache_inode_setattr.c
 * @brief   Sets the attributes for an entry
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
#include "nfs4_acls.h"
#include "FSAL/access_check.h"


/**
 * @brief Set the attributes for a file.
 *
 * This function sets the attributes of a file, both in the cache and
 * in the underlying filesystem.
 *
 * @param[in]     entry   Entry whose attributes are to be set
 * @param[in,out] attr    Attributes to set/result of set
 * @param[in]     context FSAL credentials
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 */
cache_inode_status_t
cache_inode_setattr(cache_entry_t *entry,
		    struct attrlist *attr,
		    struct req_op_context *req_ctx)
{
     struct fsal_obj_handle *obj_handle = entry->obj_handle;
     const struct user_cred *creds = req_ctx->creds;
     fsal_status_t fsal_status = {0, 0};
     fsal_acl_t *saved_acl = NULL;
     fsal_acl_status_t acl_status = 0;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;

     if ((attr->mask & ATTR_SIZE) &&
         (entry->type != REGULAR_FILE)) {
          LogMajor(COMPONENT_CACHE_INODE,
                   "Attempt to truncate non-regular file: type=%d",
                   entry->type);
          status = CACHE_INODE_BAD_TYPE;
     }

     /* Is it allowed to change times ? */
     if(!obj_handle->export->ops->fs_supports(obj_handle->export,
                                              fso_cansettime) &&
        (FSAL_TEST_MASK(attr->mask, (ATTR_ATIME | ATTR_CREATION |
                                     ATTR_CTIME | ATTR_MTIME)))) {
             status = CACHE_INODE_INVALID_ARGUMENT;
             goto out;
     }

     /* Get wrlock on attr_lock and verify attrs */
     status = cache_inode_lock_trust_attrs(entry, req_ctx, true);
     if(status != CACHE_INODE_SUCCESS)
           return status;


     /* Do permission checks */
     status = cache_inode_check_setattr_perms(entry,
             attr,
             req_ctx);
     if (status != CACHE_INODE_SUCCESS)
     {
         goto unlock;
     }


     if (attr->mask & ATTR_SIZE) {
             fsal_status = obj_handle->ops->truncate(obj_handle, req_ctx,
                                                     attr->filesize);
             if (FSAL_IS_ERROR(fsal_status)) {
                     status = cache_inode_error_convert(fsal_status);
                     if (fsal_status.major == ERR_FSAL_STALE) {
                             cache_inode_kill_entry(entry);
                     }
                     goto unlock;
             }
             attr->mask &= ~ATTR_SIZE;
     }

     saved_acl = obj_handle->attributes.acl;
     fsal_status = obj_handle->ops->setattrs(obj_handle, req_ctx, attr);
     if (FSAL_IS_ERROR(fsal_status)) {
          status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry);
          }
          goto unlock;
     }
     fsal_status = obj_handle->ops->getattrs(obj_handle, req_ctx);
     *attr = obj_handle->attributes;
     if (FSAL_IS_ERROR(fsal_status)) {
          status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry);
          }
          goto unlock;
     }
     /* Decrement refcount on saved ACL */
     nfs4_acl_release_entry(saved_acl, &acl_status);
     if (acl_status != NFS_V4_ACL_SUCCESS) {
	     LogCrit(COMPONENT_CACHE_INODE,
		     "Failed to release old acl, status=%d",
		     acl_status);
     }

     cache_inode_fixup_md(entry);

     /* Copy the complete set of new attributes out. */

     *attr = entry->obj_handle->attributes;

     status = CACHE_INODE_SUCCESS;

unlock:
     PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:

     return status;
}
/** @} */
