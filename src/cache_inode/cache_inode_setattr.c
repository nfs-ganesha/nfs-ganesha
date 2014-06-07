/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file    cache_inode_setattr.c
 * @brief   Sets the attributes for an entry
 */

#include "config.h"
#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "nfs4_acls.h"
#include "FSAL/access_check.h"
#include "nfs_exports.h"
#include "export_mgr.h"

/**
 * @brief Set the attributes for a file.
 *
 * This function sets the attributes of a file, both in the cache and
 * in the underlying filesystem.
 *
 * @param[in]     entry   Entry whose attributes are to be set
 * @param[in,out] attr    Attributes to set/result of set
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 */
cache_inode_status_t
cache_inode_setattr(cache_entry_t *entry,
		    struct attrlist *attr,
		    bool is_open_write)
{
	struct fsal_obj_handle *obj_handle = entry->obj_handle;
	fsal_status_t fsal_status = { 0, 0 };
	fsal_acl_t *saved_acl = NULL;
	fsal_acl_status_t acl_status = 0;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	uint64_t before;

	/* True if we have taken the content lock on 'entry' */
	bool content_locked = false;

	if ((attr->mask & (ATTR_SIZE | ATTR4_SPACE_RESERVED))
	     && (entry->type != REGULAR_FILE)) {
		LogWarn(COMPONENT_CACHE_INODE,
			"Attempt to truncate non-regular file: type=%d",
			entry->type);
		status = CACHE_INODE_BAD_TYPE;
	}

	/* Is it allowed to change times ? */
	if (!op_ctx->fsal_export->ops->fs_supports(op_ctx->fsal_export,
						    fso_cansettime)
	    &&
	    (FSAL_TEST_MASK
	     (attr->mask,
	      (ATTR_ATIME | ATTR_CREATION | ATTR_CTIME | ATTR_MTIME)))) {
		status = CACHE_INODE_INVALID_ARGUMENT;
		goto out;
	}

	/* Get wrlock on attr_lock and verify attrs */
	status = cache_inode_lock_trust_attrs(entry, true);
	if (status != CACHE_INODE_SUCCESS)
		return status;

	/* Do permission checks */
	status = cache_inode_check_setattr_perms(entry, attr, is_open_write);
	if (status != CACHE_INODE_SUCCESS)
		goto unlock;

	if (attr->mask & (ATTR_SIZE | ATTR4_SPACE_RESERVED)) {
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		content_locked = true;
	}

	saved_acl = obj_handle->attributes.acl;
	before = obj_handle->attributes.change;
	fsal_status = obj_handle->ops->setattrs(obj_handle, attr);
	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE from truncate");
			cache_inode_kill_entry(entry);
		}
		goto unlock;
	}
	fsal_status = obj_handle->ops->getattrs(obj_handle);
	*attr = obj_handle->attributes;
	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE from setattrs");
			cache_inode_kill_entry(entry);
		}
		goto unlock;
	}
	if (before == obj_handle->attributes.change)
		obj_handle->attributes.change++;
	/* Decrement refcount on saved ACL */
	nfs4_acl_release_entry(saved_acl, &acl_status);
	if (acl_status != NFS_V4_ACL_SUCCESS)
		LogCrit(COMPONENT_CACHE_INODE,
			"Failed to release old acl, status=%d", acl_status);

	cache_inode_fixup_md(entry);

	/* Copy the complete set of new attributes out. */

	*attr = entry->obj_handle->attributes;

	status = CACHE_INODE_SUCCESS;

unlock:
	if (content_locked)
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:
	return status;
}

/** @} */
