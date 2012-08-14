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

#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "nfs4_acls.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Set the attributes for a file.
 *
 * This function sets the attributes of a file, both in the cache and
 * in the underlying filesystem.
 *
 * @param[in]     entry   Entry whose attributes are to be set
 * @param[in,out] attr    Attributes to set/result of set
 * @param[in]     context FSAL credentials
 * @param[out]    status  Returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_setattr(cache_entry_t *entry,
                    fsal_attrib_list_t *attr,
                    fsal_op_context_t *context,
                    cache_inode_status_t *status)
{
     fsal_status_t fsal_status = {0, 0};
#ifdef _USE_NFS4_ACL
     fsal_acl_t *saved_acl = NULL;
     fsal_acl_status_t acl_status = 0;
#endif /* _USE_NFS4_ACL */

     if ((entry->type == UNASSIGNED) ||
         (entry->type == RECYCLED)) {
          LogWarn(COMPONENT_CACHE_INODE,
                  "WARNING: unknown source entry type: type=%d, "
                  "line %d in file %s", entry->type, __LINE__, __FILE__);
          *status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if ((attr->asked_attributes & FSAL_ATTR_SIZE) &&
         (entry->type != REGULAR_FILE)) {
          LogWarn(COMPONENT_CACHE_INODE,
                   "Attempt to truncate non-regular file: type=%d",
                   entry->type);
          *status = CACHE_INODE_BAD_TYPE;
     }

     pthread_rwlock_wrlock(&entry->attr_lock);
     if (attr->asked_attributes & FSAL_ATTR_SIZE) {
          fsal_status = FSAL_truncate(&entry->handle,
                                      context, attr->filesize,
                                      NULL, NULL);
          if (FSAL_IS_ERROR(fsal_status)) {
               *status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    LogEvent(COMPONENT_CACHE_INODE,
                       "FSAL returned STALE from truncate");
                    cache_inode_kill_entry(entry);
               }
               goto unlock;
          }
     }

#ifdef _USE_NFS4_ACL
     saved_acl = entry->attributes.acl;
#endif /* _USE_NFS4_ACL */
     fsal_status = FSAL_setattrs(&entry->handle, context, attr,
                                 &entry->attributes);
     if (FSAL_IS_ERROR(fsal_status)) {
          *status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               LogEvent(COMPONENT_CACHE_INODE,
                       "FSAL returned STALE from setattrs");
               cache_inode_kill_entry(entry);
          }
          goto unlock;
     } else {
#ifdef _USE_NFS4_ACL
          /* Decrement refcount on saved ACL */
         nfs4_acl_release_entry(saved_acl, &acl_status);
         if (acl_status != NFS_V4_ACL_SUCCESS) {
              LogCrit(COMPONENT_CACHE_INODE,
                      "Failed to release old acl, status=%d",
                      acl_status);
         }
#endif /* _USE_NFS4_ACL */
     }
     cache_inode_fixup_md(entry);

     /* Copy the complete set of new attributes out. */

     *attr = entry->attributes;

     *status = CACHE_INODE_SUCCESS;

unlock:
     pthread_rwlock_unlock(&entry->attr_lock);

out:

     return *status;
} /* cache_inode_setattr */
