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
 * \file    cache_inode_setattr.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/14 11:47:40 $
 * \version $Revision: 1.19 $
 * \brief   Sets the attributes for an entry.
 *
 * cache_inode_setattr.c : Sets the attributes for an entry.
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
#include <assert.h>
#include "LRU_List.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"
#include "nfs4_acls.h"
#include "FSAL/access_check.h"


/**
 * @brief Set the attributes for a file.
 *
 * This function sets the attributes of a file, both in the cache and
 * in the underlying filesystem.
 *
 * @param entry [in] Entry whose attributes are to be set
 * @param attr [in,out] Attributes to set/result of set
 * @param client [in,out] Structure for per-thread resource
 *                         management
 * @param context [in] FSAL credentials
 * @param status [out] returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_LRU_ERROR if allocation error occured when
 *         validating the entry
 */

cache_inode_status_t
cache_inode_setattr(cache_entry_t *entry,
                    fsal_attrib_list_t *attr,
                    cache_inode_client_t *client,
		    struct user_cred *creds,
                    cache_inode_status_t *status)
{
     struct fsal_obj_handle *obj_handle = entry->obj_handle;
     fsal_status_t fsal_status = {0, 0};
#ifdef _USE_NFS4_ACL
     fsal_acl_t *saved_acl = NULL;
     fsal_acl_status_t acl_status = 0;
#endif /* _USE_NFS4_ACL */

     if ((entry->type == UNASSIGNED) ||
         (entry->type == RECYCLED)) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "WARNING: unknown source entry type: type=%d, "
                  "line %d in file %s", entry->type, __LINE__, __FILE__);
          *status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if ((attr->asked_attributes & FSAL_ATTR_SIZE) &&
         (entry->type != REGULAR_FILE)) {
          LogMajor(COMPONENT_CACHE_INODE,
                   "Attempt to truncate non-regular file: type=%d",
                   entry->type);
          *status = CACHE_INODE_BAD_TYPE;
     }

     /* Is it allowed to change times ? */
     if( !obj_handle->export->ops->fs_supports(obj_handle->export, cansettime) &&
	 (attr->asked_attributes & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION |
				     FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))) {
	     *status = CACHE_INODE_INVALID_ARGUMENT;
	     goto out;
     }

     pthread_rwlock_wrlock(&entry->attr_lock);

     /* Only superuser and the owner get a free pass.
      * Everybody else gets a full body scan
      * we do this here because this is an exception/extension to the usual access check.
      */
     if(creds->caller_uid != 0 &&
	creds->caller_uid != obj_handle->attributes.owner) {
	     if(FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_MODE)) {
		     LogFullDebug(COMPONENT_FSAL,
				  "Permission denied for CHMOD operation: "
				  "current owner=%d, credential=%d",
				  obj_handle->attributes.owner, creds->caller_uid);
		     *status = CACHE_INODE_FSAL_EACCESS;
		     goto unlock;
	     }
	     if(FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_OWNER)) {
		     LogFullDebug(COMPONENT_FSAL,
				  "Permission denied for CHOWN operation: "
				  "current owner=%d, credential=%d",
				  obj_handle->attributes.owner, creds->caller_uid);
		     *status = CACHE_INODE_FSAL_EACCESS;
		     goto unlock;
	     }
	     if(FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_GROUP)) {
		     int in_group = 0, i;

		     if(creds->caller_gid == obj_handle->attributes.group) {
			     in_group = 1;
		     } else { 
			     for(i = 0; i < creds->caller_glen; i++) {
				     if(creds->caller_garray[i] == obj_handle->attributes.group) {
					     in_group = 1;
					     break;
				     }
			     }
		     }
		     if( !in_group) {
			     LogFullDebug(COMPONENT_FSAL,
					  "Permission denied for CHOWN operation: "
					  "current group=%d, credential=%d, new group=%d",
					  obj_handle->attributes.group,
					  creds->caller_gid, attr->group);
			     *status = CACHE_INODE_FSAL_EACCESS;
			     goto unlock;
		     }
	     }
	     if(FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_ATIME)) {
		fsal_status = obj_handle->ops->test_access(obj_handle, creds, FSAL_R_OK);
		if(FSAL_IS_ERROR(fsal_status)) {
		     *status = cache_inode_error_convert(fsal_status);
		     goto unlock;
		}
	     }
	     if(FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_MTIME)) {
		fsal_status = obj_handle->ops->test_access(obj_handle, creds, FSAL_W_OK);
		if(FSAL_IS_ERROR(fsal_status)) {
		     *status = cache_inode_error_convert(fsal_status);
		     goto unlock;
		}
	     }
	     if(FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_SIZE)) {
		fsal_status = obj_handle->ops->test_access(obj_handle, creds, FSAL_W_OK);
		if(FSAL_IS_ERROR(fsal_status)) {
		     *status = cache_inode_error_convert(fsal_status);
		     goto unlock;
		}
	     }
     }
     if (attr->asked_attributes & FSAL_ATTR_SIZE) {
	  fsal_status = obj_handle->ops->truncate(obj_handle, attr->filesize);
          if (FSAL_IS_ERROR(fsal_status)) {
               *status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(entry, client);
               }
               goto unlock;
          }
     }

#ifdef _USE_NFS4_ACL
     saved_acl = entry->attributes.acl;
#endif /* _USE_NFS4_ACL */
     fsal_status = obj_handle->ops->setattrs(obj_handle, attr);
     if (FSAL_IS_ERROR(fsal_status)) {
          *status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry, client);
          }
          goto unlock;
     }
     fsal_status = obj_handle->ops->getattrs(obj_handle, attr);
     if (FSAL_IS_ERROR(fsal_status)) {
          *status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry, client);
          }
          goto unlock;
     }
#ifdef _USE_NFS4_ACL
     /* Decrement refcount on saved ACL */
     nfs4_acl_release_entry(saved_acl, &acl_status);
     if (acl_status != NFS_V4_ACL_SUCCESS) {
	     LogCrit(COMPONENT_CACHE_INODE,
		     "Failed to release old acl, status=%d",
		     acl_status);
     }
#endif /* _USE_NFS4_ACL */

     cache_inode_fixup_md(entry);

     /* Copy the complete set of new attributes out. */

     *attr = entry->obj_handle->attributes;

     *status = CACHE_INODE_SUCCESS;

unlock:
     pthread_rwlock_unlock(&entry->attr_lock);

out:

     return *status;
} /* cache_inode_setattr */
