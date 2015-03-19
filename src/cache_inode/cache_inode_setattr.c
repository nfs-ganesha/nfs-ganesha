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
	const struct user_cred *creds = op_ctx->creds;

	/* True if we have taken the content lock on 'entry' */
	bool content_locked = false;

	if ((attr->mask & (ATTR_SIZE | ATTR4_SPACE_RESERVED))
	     && (entry->type != REGULAR_FILE)) {
		LogWarn(COMPONENT_CACHE_INODE,
			"Attempt to truncate non-regular file: type=%d",
			entry->type);
		status = CACHE_INODE_BAD_TYPE;
		goto out;
	}

	/* Is it allowed to change times ? */
	if (!op_ctx->fsal_export->exp_ops.fs_supports(op_ctx->fsal_export,
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

	/* Test for the following condition from chown(2):
	 *
	 *     When the owner or group of an executable file are changed by an
	 *     unprivileged user the S_ISUID and S_ISGID mode bits are cleared.
	 *     POSIX does not specify whether this also should happen when
	 *     root does the chown(); the Linux behavior depends on the kernel
	 *     version.  In case of a non-group-executable file (i.e., one for
	 *     which the S_IXGRP bit is not set) the S_ISGID bit indicates
	 *     mandatory locking, and is not cleared by a chown().
	 *
	 */
	if (creds->caller_uid != 0 &&
	    (FSAL_TEST_MASK(attr->mask, ATTR_OWNER) ||
	     FSAL_TEST_MASK(attr->mask, ATTR_GROUP)) &&
	    ((entry->obj_handle->attributes.mode &
	      (S_IXOTH | S_IXUSR | S_IXGRP)) != 0) &&
	    ((entry->obj_handle->attributes.mode &
	      (S_ISUID | S_ISGID)) != 0)) {
		/* Non-priviledged user changing ownership on an executable
		 * file with S_ISUID or S_ISGID bit set, need to be cleared.
		 */
		if (!FSAL_TEST_MASK(attr->mask, ATTR_MODE)) {
			/* Mode wasn't being set, so set it now, start with
			 * the current attributes.
			 */
			attr->mode = entry->obj_handle->attributes.mode;
			FSAL_SET_MASK(attr->mask, ATTR_MODE);
		}

		/* Don't clear S_ISGID if the file isn't group executable.
		 * In that case, S_ISGID indicates mandatory locking and
		 * is not cleared by chown.
		 */
		if ((entry->obj_handle->attributes.mode & S_IXGRP) != 0)
			attr->mode &= ~S_ISGID;

		/* Clear S_ISUID. */
		attr->mode &= ~S_ISUID;
	}

	/* Test for the following condition from chmod(2):
	 *
	 *     If the calling process is not privileged (Linux: does not have
	 *     the CAP_FSETID capability), and the group of the file does not
	 *     match the effective group ID of the process or one of its
	 *     supplementary group IDs, the S_ISGID bit will be turned off,
	 *     but this will not cause an error to be returned.
	 *
	 * We test the actual mode being set before testing for group
	 * membership since that is a bit more expensive.
	 */
	if (creds->caller_uid != 0 &&
	    FSAL_TEST_MASK(attr->mask, ATTR_MODE) &&
	    (attr->mode & S_ISGID) != 0 &&
	    not_in_group_list(entry->obj_handle->attributes.group)) {
		/* Clear S_ISGID */
		attr->mode &= ~S_ISGID;
	}

	saved_acl = obj_handle->attributes.acl;
	before = obj_handle->attributes.change;
	fsal_status = obj_handle->obj_ops.setattrs(obj_handle, attr);
	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE from setattrs");
			cache_inode_kill_entry(entry);
		}
		goto unlock;
	}
	fsal_status = obj_handle->obj_ops.getattrs(obj_handle);
	*attr = obj_handle->attributes;
	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE from getattrs");
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
