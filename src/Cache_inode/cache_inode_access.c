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
 * @file    cache_inode_access.c
 * @brief   Check for object accessibility.
 *
 * Check for object accessibility.
 *
 */
#include "config.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include "log.h"
#include "HashTable.h"
#include "gsh_config.h"
#include "cache_inode.h"
#include "abstract_mem.h"

/**
 *
 * @brief Checks the permissions on an object
 *
 * This function returns success if the supplied credentials possess
 * permission required to meet the specified access.
 *
 * @param[in]  entry       The object to be checked
 * @param[in]  access_type The kind of access to be checked
 * @param[in]  req_ctx     Request context
 * @param[in]  use_mutex   Whether to acquire a read lock
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */
cache_inode_status_t
cache_inode_access_sw(cache_entry_t *entry,
                      fsal_accessflags_t access_type,
                      struct req_op_context *req_ctx,
                      bool use_mutex)
{
     fsal_status_t fsal_status;
     fsal_accessflags_t used_access_type;
     struct fsal_obj_handle *pfsal_handle = entry->obj_handle;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;

     LogFullDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_access_sw: access_type=0X%x",
                  access_type);

     /* Set the return default to CACHE_INODE_SUCCESS */
     status = CACHE_INODE_SUCCESS;

     /*
      * We do no explicit access test in FSAL for FSAL_F_OK: it is
      * considered that if an entry resides in the cache_inode, then a
      * FSAL_getattrs was successfully made to populate the cache entry,
      * this means that the entry exists. For this reason, F_OK is
      * managed internally
      */
     if(access_type != FSAL_F_OK) {
          used_access_type = access_type & ~FSAL_F_OK;

	  /* We actually need the lock here since we're using
	     the attribute cache, so get it if the caller didn't
	     acquire it.  */
	  if(use_mutex) {
	      if ((status
		   = cache_inode_lock_trust_attrs(entry, req_ctx, false))
		  != CACHE_INODE_SUCCESS) {
		      goto out;
	      }
	  }
	  fsal_status = pfsal_handle->ops->test_access(pfsal_handle,
						       req_ctx,
						       used_access_type);

	  if (use_mutex) {
		  PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	  }
          if(FSAL_IS_ERROR(fsal_status)) {
               status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    LogEvent(COMPONENT_CACHE_INODE,
                       "STALE returned by FSAL, calling kill_entry");
                    cache_inode_kill_entry(entry);
               }
          } else {
               status = CACHE_INODE_SUCCESS;
          }
     }

out:
     return status;
}

/**
 *
 * @brief Checks entry permissions without taking a lock
 *
 * This function checks whether the specified permissions are
 * available on the object.  This function may only be called if an
 * attribute lock is held.
 *
 * @param[in] entry       entry pointer for the fs object to be checked.
 * @param[in] access_type The kind of access to be checked
 * @param[in] req_ctx     Request context
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */
cache_inode_status_t
cache_inode_access_no_mutex(cache_entry_t *entry,
                            fsal_accessflags_t access_type,
			    struct req_op_context *req_ctx)
{
    return cache_inode_access_sw(entry, access_type,
                                 req_ctx, false);
}

/**
 *
 * @brief Checks permissions on an entry
 *
 * This function acquires the attribute lock on the supplied cach
 * entry then checks if the supplied credentials are sufficient to
 * gain the supplied access.
 *
 * @param[in] entry       The object to be checked
 * @param[in] access_type The kind of access to be checked
 * @param[in] req_ctx     Request context
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */
cache_inode_status_t
cache_inode_access(cache_entry_t *entry,
                   fsal_accessflags_t access_type,
		   struct req_op_context *req_ctx)
{
    return cache_inode_access_sw(entry, access_type,
                                 req_ctx, true);
}


bool not_in_group_list(gid_t gid, struct req_op_context *req_ctx)
{
    const struct user_cred *creds = req_ctx->creds;
    int i;
    if(creds->caller_gid == gid) {

        LogDebug(COMPONENT_CACHE_INODE,
                "User %d is member of group %d",
                (int) creds->caller_uid,
                (int) gid);
        return false;
    }
    for(i = 0; i < creds->caller_glen; i++) {
        if(creds->caller_garray[i] == gid) {
            LogDebug(COMPONENT_CACHE_INODE,
                    "User %d is member of group %d",
                    (int) creds->caller_uid,
                    (int) gid);
            return false;
        }
    }

    LogDebug(COMPONENT_CACHE_INODE,
            "User %d is NOT member of group %d",
            (int) creds->caller_uid,
            (int) gid);
    return true;
}

/**
 *
 * @brief Checks permissions on an entry for setattrs
 *
 * This function acquires the attribute lock on the supplied cache
 * entry then checks if the supplied credentials are sufficient to
 * perform the required setattrs.
 *
 * @param[in] entry   The object to be checked
 * @param[in] attr    Attributes to set/result of set
 * @param[in] req_ctx FSAL credentials
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */
cache_inode_status_t
cache_inode_check_setattr_perms(cache_entry_t        * entry,
                                struct attrlist *attr,
                                struct req_op_context *req_ctx)
{
     cache_inode_status_t status = CACHE_INODE_SUCCESS;
     fsal_accessflags_t access_check = 0;
     bool not_owner;
     char *             note = "";
     const struct user_cred *creds = req_ctx->creds;

     if(isDebug(COMPONENT_CACHE_INODE)) {
          char * setattr_size        = "";
          char * setattr_owner       = "";
          char * setattr_owner_group = "";
          char * setattr_mode        = "";
          char * setattr_acl         = "";
          char * setattr_mtime       = "";
          char * setattr_atime       = "";

          if(FSAL_TEST_MASK(attr->mask, ATTR_SIZE))
               setattr_size = " SIZE";

          if(FSAL_TEST_MASK(attr->mask, ATTR_OWNER))
               setattr_owner = " OWNER";

          if(FSAL_TEST_MASK(attr->mask, ATTR_GROUP))
               setattr_owner_group = " GROUP";

          if(FSAL_TEST_MASK(attr->mask, ATTR_MODE))
               setattr_mode = " MODE";

          if(FSAL_TEST_MASK(attr->mask, ATTR_ACL))
               setattr_acl = " ACL";

          if(FSAL_TEST_MASK(attr->mask, ATTR_ATIME))
               setattr_atime = " ATIME";
          else if(FSAL_TEST_MASK(attr->mask, ATTR_ATIME_SERVER))
               setattr_atime = " ATIME_SERVER";

          if(FSAL_TEST_MASK(attr->mask, ATTR_MTIME))
               setattr_mtime = " MTIME";
          else if(FSAL_TEST_MASK(attr->mask, ATTR_MTIME_SERVER))
                         setattr_atime = " MTIME_SERVER";

          LogDebug(COMPONENT_CACHE_INODE,
                   "SETATTR %s%s%s%s%s%s%s",
                   setattr_size,
                   setattr_owner,
                   setattr_owner_group,
                   setattr_mode,
                   setattr_acl,
                   setattr_mtime,
                   setattr_atime);
     }

     /* Shortcut, if current user is root, then we can just bail out with success. */
     if(creds->caller_uid == 0) {
          note = " (Ok for root user)";
          goto out;
     }

     not_owner = (creds->caller_uid != entry->obj_handle->attributes.owner);

     // Only ownership change need to be checked for owner
     if(FSAL_TEST_MASK(attr->mask, ATTR_OWNER)) {
          /* non-root is only allowed to "take ownership of file" */
          if(attr->owner != creds->caller_uid) {
               status = CACHE_INODE_FSAL_EPERM;
               note = " (new OWNER was not user)";
               goto out;
          }

          /* Owner of file will always be able to "change" the owner to himself. */
          if(not_owner) {
              access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_OWNER);
              LogDebug(COMPONENT_CACHE_INODE,
                      "Change OWNER requires FSAL_ACE_PERM_WRITE_OWNER");
          }
     }
     if(FSAL_TEST_MASK(attr->mask, ATTR_GROUP)) {
               /* non-root is only allowed to change group_owner to a group user is a
                * member of.
                */
         int not_in_group = not_in_group_list(attr->group, req_ctx);

         if (not_in_group)
         {
             status = CACHE_INODE_FSAL_EPERM;
             note = " (user is not member of new GROUP)";
             goto out;
         }
         /* Owner is always allowed to change the group_owner of a file to a group
         * they are a member of.
         */
         if(not_owner) {
             access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_OWNER);
             LogDebug(COMPONENT_CACHE_INODE,
                     "Change GROUP requires FSAL_ACE_PERM_WRITE_OWNER");
         }
     }
     if (!not_owner)
     {
         note = " (Ok for owner)";
         goto out;
     }

     if(FSAL_TEST_MASK(attr->mask, ATTR_MODE) ||
        FSAL_TEST_MASK(attr->mask, ATTR_ACL)) {
          /* Changing mode or ACL requires ACE4_WRITE_ACL */
         access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ACL);
         LogDebug(COMPONENT_CACHE_INODE,
                 "Change MODE or ACL requires FSAL_ACE_PERM_WRITE_ACL");
     }

     if(FSAL_TEST_MASK(attr->mask, ATTR_SIZE)) {
          /* Changing size requires owner or write permission */
          /** @todo: does FSAL_ACE_PERM_APPEND_DATA allow enlarging the file? */
         access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
         LogDebug(COMPONENT_CACHE_INODE,
                 "Change SIZE requires FSAL_ACE_PERM_WRITE_DATA");
     }

     /* Check if just setting atime and mtime to "now" */
     if((FSAL_TEST_MASK(attr->mask, ATTR_MTIME_SERVER) ||
        FSAL_TEST_MASK(attr->mask, ATTR_ATIME_SERVER)) &&
        !FSAL_TEST_MASK(attr->mask, ATTR_MTIME) &&
        !FSAL_TEST_MASK(attr->mask, ATTR_ATIME)) {
         /* If either atime and/or mtime are set to "now" then need only have
          * write permission.
          *
          * Technically, client should not send atime updates, but if they
          * really do, we'll let them to make the perm check a bit simpler.
          */
         access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
         LogDebug(COMPONENT_CACHE_INODE,
                 "Change ATIME and MTIME to NOW requires FSAL_ACE_PERM_WRITE_DATA");
     } else if(FSAL_TEST_MASK(attr->mask, ATTR_MTIME_SERVER) ||
               FSAL_TEST_MASK(attr->mask, ATTR_ATIME_SERVER) ||
               FSAL_TEST_MASK(attr->mask, ATTR_MTIME) ||
               FSAL_TEST_MASK(attr->mask, ATTR_ATIME)) {
          /* Any other changes to atime or mtime require owner, root, or
           * ACES4_WRITE_ATTRIBUTES.
           *
           * NOTE: we explicity do NOT check for update of atime only to "now".
           * Section 10.6 of both RFC 3530 and RFC 5661 document the reasons
           * clients should not do atime updates.
           */
         access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ATTR);
         LogDebug(COMPONENT_CACHE_INODE,
                 "Change ATIME and/or MTIME requires FSAL_ACE_PERM_WRITE_ATTR");
     }

     if(isDebug(COMPONENT_CACHE_INODE)) {
          char * need_write_owner = "";
          char * need_write_acl   = "";
          char * need_write_data  = "";
          char * need_write_attr  = "";

          if(access_check & FSAL_ACE_PERM_WRITE_OWNER)
            need_write_owner = " WRITE_OWNER";

          if(access_check & FSAL_ACE_PERM_WRITE_ACL)
            need_write_acl = " WRITE_ACL";

          if(access_check & FSAL_ACE_PERM_WRITE_DATA)
            need_write_data = " WRITE_DATA";

          if(access_check & FSAL_ACE_PERM_WRITE_ATTR)
            need_write_attr = " WRITE_ATTR";

          LogDebug(COMPONENT_CACHE_INODE,
                   "Requires %s%s%s%s",
                   need_write_owner,
                   need_write_acl,
                   need_write_data,
                   need_write_attr);
     }


     if(entry->obj_handle->attributes.acl) {
          status = cache_inode_access_no_mutex(entry,
                                      access_check,
                                      req_ctx);

          note = " (checked ACL)";
          goto out;
     }

     if(access_check != FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA)) {
          /* Without an ACL, this user is not allowed some operation */
          status = CACHE_INODE_FSAL_EPERM;
          note = " (no ACL to check)";
          goto out;
     }

     status = cache_inode_access_no_mutex(entry,
                                 FSAL_W_OK,
                                 req_ctx);

     note = " (checked mode)";

out:

     LogDebug(COMPONENT_CACHE_INODE,
              "Access check returned %s%s",
              cache_inode_err_str(status),
              note);

     return status;
}


/**
 *
 * @brief Sticky bit access check
 *
 * @param[in] parent      The directory to be checked
 * @param[in] entry       The object to be checked
 * @param[in] req_ctx     Request context
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */
cache_inode_status_t
cache_inode_check_sticky(cache_entry_t *parent,
                         cache_entry_t *entry,
                         struct req_op_context *req_ctx)
{
    cache_inode_status_t status = CACHE_INODE_SUCCESS;

    PTHREAD_RWLOCK_rdlock(&parent->attr_lock);
    PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

    if (!sticky_dir_allows(parent->obj_handle,
                           entry->obj_handle,
                           req_ctx->creds)) {
        status = CACHE_INODE_FSAL_EPERM;
    }

    PTHREAD_RWLOCK_unlock(&entry->attr_lock);
    PTHREAD_RWLOCK_unlock(&parent->attr_lock);

    return status;
}

/** @} */
