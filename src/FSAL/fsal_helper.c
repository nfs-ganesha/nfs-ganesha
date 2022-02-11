// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohrotFS Inc., 2015
 * Author: Daniel Gryniewicz <dang@cohortfs.com>
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
 * -------------
 */

/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file fsal_helper.c
 * @author Daniel Gryniewicz <dang@cohortfs.com>
 * @brief FSAL helper for clients
 */

#include "config.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "gsh_config.h"
#include "log.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "nfs_convert.h"
#include "nfs_exports.h"
#include "nfs4_acls.h"
#include "sal_data.h"
#include "sal_functions.h"
#include "FSAL/fsal_commonlib.h"
#include "sal_functions.h"

/**
 * This is a global counter of files opened.
 *
 * This is preliminary expected to go away.  Problems with this method are that
 * it overcounts file descriptors for FSALs that don't use them for open files,
 * and, under the Lieb Rearchitecture, FSALs will be responsible for caching
 * their own file descriptors, with interfaces for MDCACHE to interrogate
 * them as to usage or instruct them to close them.
 */

size_t open_fd_count;

static bool fsal_not_in_group_list(gid_t gid)
{
	int i;

	if (op_ctx->creds.caller_gid == gid) {

		LogDebug(COMPONENT_FSAL,
			 "User %u is has active group %u",
			 op_ctx->creds.caller_uid, gid);
		return false;
	}
	for (i = 0; i < op_ctx->creds.caller_glen; i++) {
		if (op_ctx->creds.caller_garray[i] == gid) {
			LogDebug(COMPONENT_FSAL,
				 "User %u is member of group %u",
				 op_ctx->creds.caller_uid, gid);
			return false;
		}
	}

	LogDebug(COMPONENT_FSAL,
		 "User %u IS NOT member of group %u",
		 op_ctx->creds.caller_uid, gid);
	return true;
}

/**
 * @brief Check permissions on opening a file.
 *
 * @param[in]  obj               The file being opened
 * @param[in]  openflags         The access reqested on opening the file
 * @param[in]  exclusive_create  Indicates the file is being exclusive create
 * @param[out] reason            Description of why the access failed.
 *
 * @returns Status of the permission check.
 */
static fsal_status_t check_open_permission(struct fsal_obj_handle *obj,
					   fsal_openflags_t openflags,
					   bool exclusive_create,
					   char **reason)
{
	fsal_status_t status = {0, 0};
	fsal_accessflags_t access_mask = 0;

	if (openflags & FSAL_O_READ)
		access_mask |= FSAL_READ_ACCESS;

	if (openflags & FSAL_O_WRITE)
		access_mask |= FSAL_WRITE_ACCESS;

	/* Ask for owner_skip on exclusive create (we will be checking the
	 * verifier later, so this allows a replay of
	 * open("foo", O_RDWR | O_CREAT | O_EXCL, 0) to succeed).
	 * For open reclaims ask for owner_skip.
	 */
	status = obj->obj_ops->test_access(obj, access_mask,
					   NULL, NULL, exclusive_create ||
					   (openflags & FSAL_O_RECLAIM));

	if (!FSAL_IS_ERROR(status)) {
		*reason = "";
		return status;
	}

	LogDebug(COMPONENT_FSAL,
		 "test_access got %s",
		 fsal_err_txt(status));

	/* If non-permission error, return it. */
	if (status.major != ERR_FSAL_ACCESS) {
		*reason = "fsal_access failed - ";
		return status;
	}

	/* If WRITE access is requested, return permission
	 * error
	 */
	if (openflags & FSAL_O_WRITE) {
		*reason = "fsal_access failed with WRITE_ACCESS - ";
		return status;
	}

	/* If just a permission error and file was opened read
	 * only, try execute permission.
	 *
	 * NOTE: We don't do anything special for exclusive create here, if an
	 *       exclusive create replay failed the above permission check, it
	 *       presumably is no longer exclusively the creator of the file
	 *       because somehow the owner changed.
	 *
	 */
	status = fsal_access(obj, FSAL_EXECUTE_ACCESS);

	LogDebug(COMPONENT_FSAL,
		 "fsal_access got %s",
		 fsal_err_txt(status));

	if (!FSAL_IS_ERROR(status))
		*reason = "";
	else
		*reason = "fsal_access failed with EXECUTE_ACCESS - ";

	return status;
}

/* When creating a file, we must check that the owner and group to be set is
 * OK for the caller to set.
 */
static fsal_status_t fsal_check_create_owner(struct fsal_attrlist *attr)
{
	fsal_status_t status = {0, 0};

	LogFullDebug(COMPONENT_FSAL,
		     "attr->owner %"PRIu64" caller_uid %"PRIu64
		     " attr->group %"PRIu64" caller_gid %"PRIu64"",
		     attr->owner, (uint64_t) op_ctx->creds.caller_uid,
		     attr->group, (uint64_t) op_ctx->creds.caller_gid);

	if (op_ctx->creds.caller_uid == 0) {
		/* No check for root. */
	} else if (FSAL_TEST_MASK(attr->valid_mask, ATTR_OWNER) &&
		   attr->owner != op_ctx->creds.caller_uid) {
		/* non-root is only allowed to set ownership of file to itself.
		 */
		status = fsalstat(ERR_FSAL_PERM, 0);
		LogDebug(COMPONENT_FSAL,
			 "Access check failed (specified OWNER was not user)");
	} else if (FSAL_TEST_MASK(attr->valid_mask, ATTR_GROUP) &&
		   (attr->group != op_ctx->creds.caller_gid)) {
		/* non-root is only allowed to set group_owner to a group the
		 * user is a member of.
		 */
		int not_in_group = fsal_not_in_group_list(attr->group);

		if (not_in_group) {
			status = fsalstat(ERR_FSAL_PERM, 0);
			LogDebug(COMPONENT_FSAL,
				 "Access check failed (user is not member of specified GROUP)");
		}
	}

	return status;
}

/**
 * @brief Checks permissions on an entry for setattrs
 *
 * This function checks if the supplied credentials are sufficient to perform
 * the required setattrs.
 *
 * @param[in] obj     The file to be checked
 * @param[in] attr    Attributes to set
 * @param[in] current The current attributes for object
 *
 * @return FSAL status
 */
static fsal_status_t fsal_check_setattr_perms(struct fsal_obj_handle *obj,
					      struct fsal_attrlist *attr,
					      struct fsal_attrlist *current)
{
	fsal_status_t status = {0, 0};
	fsal_accessflags_t access_check = 0;
	bool not_owner;
	char *note = "";

	/* Shortcut, if current user is root, then we can just bail out with
	 * success. */
	if (op_ctx->fsal_export->exp_ops.is_superuser(op_ctx->fsal_export,
						      &op_ctx->creds)) {
		note = " (Ok for root user)";
		goto out;
	}

	fsal_prepare_attrs(current,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
							op_ctx->fsal_export)
			   & (ATTRS_CREDS | ATTR_MODE | ATTR_ACL));

	status = obj->obj_ops->getattrs(obj, current);

	if (FSAL_IS_ERROR(status))
		return status;

	not_owner = (op_ctx->creds.caller_uid != current->owner);

	/* Only ownership change need to be checked for owner */
	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_OWNER)) {
		/* non-root is only allowed to "take ownership of file" */
		if (attr->owner != op_ctx->creds.caller_uid) {
			status = fsalstat(ERR_FSAL_PERM, 0);
			note = " (new OWNER was not user)";
			goto out;
		}

		/* Owner of file will always be able to "change" the owner to
		 * himself. */
		if (not_owner) {
			access_check |=
			    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_OWNER);
			LogDebug(COMPONENT_FSAL,
				    "Change OWNER requires FSAL_ACE_PERM_WRITE_OWNER");
		}
	}
	/* Check if we are changing the owner_group, if owner_group is passed,
	 * but is the current owner_group, then that will be considered a
	 * NO-OP and allowed IF the caller is the owner of the file.
	 */
	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_GROUP) &&
	    (attr->group != current->group || not_owner)) {
		/* non-root is only allowed to change group_owner to a group
		 * user is a member of. */
		int not_in_group = fsal_not_in_group_list(attr->group);

		if (not_in_group) {
			status = fsalstat(ERR_FSAL_PERM, 0);
			note = " (user is not member of new GROUP)";
			goto out;
		}
		/* Owner is always allowed to change the group_owner of a file
		 * to a group they are a member of.
		 */
		if (not_owner) {
			access_check |=
			    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_OWNER);
			LogDebug(COMPONENT_FSAL,
				    "Change GROUP requires FSAL_ACE_PERM_WRITE_OWNER");
		}
	}

	/* Any attribute after this is always changeable by the owner.
	 * And the above attributes have already been validated as a valid
	 * change for the file owner to make. Note that the owner may be
	 * setting ATTR_OWNER but at this point it MUST be to himself, and
	 * thus is no-op and does not need FSAL_ACE_PERM_WRITE_OWNER.
	 */
	if (!not_owner) {
		note = " (Ok for owner)";
		goto out;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_MODE)
	    || FSAL_TEST_MASK(attr->valid_mask, ATTR_ACL)) {
		/* Changing mode or ACL requires ACE4_WRITE_ACL */
		access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ACL);
		LogDebug(COMPONENT_FSAL,
			    "Change MODE or ACL requires FSAL_ACE_PERM_WRITE_ACL");
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_SIZE)) {
		/* Changing size requires owner or write permission */
	  /** @todo: does FSAL_ACE_PERM_APPEND_DATA allow enlarging the file? */
		access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
		LogDebug(COMPONENT_FSAL,
			    "Change SIZE requires FSAL_ACE_PERM_WRITE_DATA");
	}

	/* Check if just setting atime and mtime to "now" */
	if ((FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME_SERVER)
	     || FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME_SERVER))
	    && !FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME)
	    && !FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME)) {
		/* If either atime and/or mtime are set to "now" then need only
		 * have write permission.
		 *
		 * Technically, client should not send atime updates, but if
		 * they really do, we'll let them to make the perm check a bit
		 * simpler. */
		access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
		LogDebug(COMPONENT_FSAL,
			    "Change ATIME and MTIME to NOW requires FSAL_ACE_PERM_WRITE_DATA");
	} else if (FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME_SERVER)
		   || FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME_SERVER)
		   || FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME)
		   || FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME)) {
		/* Any other changes to atime or mtime require owner, root, or
		 * ACES4_WRITE_ATTRIBUTES.
		 *
		 * NOTE: we explicity do NOT check for update of atime only to
		 * "now". Section 10.6 of both RFC 3530 and RFC 5661 document
		 * the reasons clients should not do atime updates.
		 */
		access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ATTR);
		LogDebug(COMPONENT_FSAL,
			    "Change ATIME and/or MTIME requires FSAL_ACE_PERM_WRITE_ATTR");
	}

	if (isDebug(COMPONENT_FSAL) || isDebug(COMPONENT_NFS_V4_ACL)) {
		char *need_write_owner = "";
		char *need_write_acl = "";
		char *need_write_data = "";
		char *need_write_attr = "";

		if (access_check & FSAL_ACE_PERM_WRITE_OWNER)
			need_write_owner = " WRITE_OWNER";

		if (access_check & FSAL_ACE_PERM_WRITE_ACL)
			need_write_acl = " WRITE_ACL";

		if (access_check & FSAL_ACE_PERM_WRITE_DATA)
			need_write_data = " WRITE_DATA";

		if (access_check & FSAL_ACE_PERM_WRITE_ATTR)
			need_write_attr = " WRITE_ATTR";

		LogDebug(COMPONENT_FSAL,
			    "Requires %s%s%s%s", need_write_owner,
			    need_write_acl, need_write_data, need_write_attr);
	}

	if (current->acl) {
		status = obj->obj_ops->test_access(obj, access_check, NULL,
						  NULL, false);
		note = " (checked ACL)";
		goto out;
	}

	if (access_check != FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA)) {
		/* Without an ACL, this user is not allowed some operation */
		status = fsalstat(ERR_FSAL_PERM, 0);
		note = " (no ACL to check)";
		goto out;
	}

	status = obj->obj_ops->test_access(obj, FSAL_W_OK, NULL, NULL, false);

	note = " (checked mode)";

 out:

	if (FSAL_IS_ERROR(status)) {
		/* Done with the current attrs, caller will not expect them. */
		fsal_release_attrs(current);
	}

	LogDebug(COMPONENT_FSAL,
		    "Access check returned %s%s", fsal_err_txt(status),
		    note);

	return status;
}

fsal_status_t open2_by_name(struct fsal_obj_handle *in_obj,
			    struct state_t *state,
			    fsal_openflags_t openflags,
			    enum fsal_create_mode createmode,
			    const char *name,
			    struct fsal_attrlist *attr,
			    fsal_verifier_t verifier,
			    struct fsal_obj_handle **obj,
			    struct fsal_attrlist *attrs_out)
{
	fsal_status_t status = { 0, 0 };
	fsal_status_t close_status = { 0, 0 };
	bool caller_perm_check = false;
	char *reason;

	*obj = NULL;

	if (name == NULL)
		return fsalstat(ERR_FSAL_INVAL, 0);

	if (in_obj->type != DIRECTORY)
		return fsalstat(ERR_FSAL_INVAL, 0);

	if (strcmp(name, ".") == 0 ||
	    strcmp(name, "..") == 0) {
		/* Can't open "." or ".."... */
		return fsalstat(ERR_FSAL_ISDIR, 0);
	}

	/* Check directory permission for LOOKUP */
	status = fsal_access(in_obj, FSAL_EXECUTE_ACCESS);
	if (FSAL_IS_ERROR(status))
		return status;

	status = in_obj->obj_ops->open2(in_obj,
				       state,
				       openflags,
				       createmode,
				       name,
				       attr,
				       verifier,
				       obj,
				       attrs_out,
				       &caller_perm_check);
	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			     "FSAL %d %s returned %s",
			     (int) op_ctx->ctx_export->export_id,
			     CTX_FULLPATH(op_ctx),
			     fsal_err_txt(status));
		return status;
	}

	if (!state) {
		(void) atomic_inc_size_t(&open_fd_count);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Created entry %p FSAL %s for %s",
		     *obj, (*obj)->fsal->name, name);

	if (!caller_perm_check)
		return status;

	/* Do a permission check on the just opened file. */
	status = check_open_permission(*obj, openflags,
				       createmode >= FSAL_EXCLUSIVE, &reason);

	if (!FSAL_IS_ERROR(status))
		return status;

	LogDebug(COMPONENT_FSAL,
		 "Closing file check_open_permission failed %s-%s",
		 reason, fsal_err_txt(status));

	if (state != NULL)
		close_status = (*obj)->obj_ops->close2(*obj, state);
	else
		close_status = fsal_close(*obj);

	if (FSAL_IS_ERROR(close_status)) {
		/* Just log but don't return this error (we want to
		 * preserve the error that got us here).
		 */
		LogDebug(COMPONENT_FSAL,
			 "FSAL close2 failed with %s",
			 fsal_err_txt(close_status));
	}

	return status;
}

/**
 * @brief Set attributes on a file
 *
 * The new attributes are copied over @a attr on success.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * @param[in]     obj    File to set attributes on
 * @param[in]     bypass Indicate to bypass share reservation checking
 * @param[in]     state  Possible state associated with the entry
 * @param[in,out] attr   Attributes to set
 * @return FSAL status
 */
fsal_status_t fsal_setattr(struct fsal_obj_handle *obj, bool bypass,
			   struct state_t *state, struct fsal_attrlist *attr)
{
	fsal_status_t status = { 0, 0 };
	struct fsal_attrlist current;
	bool is_superuser;

	if ((attr->valid_mask & (ATTR_SIZE | ATTR4_SPACE_RESERVED))
	     && (obj->type != REGULAR_FILE)) {
		LogWarn(COMPONENT_FSAL,
			"Attempt to truncate non-regular file: type=%d",
			obj->type);
		return fsalstat(ERR_FSAL_BADTYPE, 0);
	}
	if ((attr->valid_mask & (ATTR_SIZE | ATTR_MODE))) {
		if (state_deleg_conflict(obj, true)) {
			return fsalstat(ERR_FSAL_DELAY, 0);
		}
	}

	/* Is it allowed to change times ? */
	if (!op_ctx->fsal_export->exp_ops.fs_supports(op_ctx->fsal_export,
						      fso_cansettime) &&
	    (FSAL_TEST_MASK
	     (attr->valid_mask,
	      (ATTR_ATIME | ATTR_CREATION | ATTR_CTIME | ATTR_MTIME))))
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* Do permission checks, which returns with the attributes for the
	 * object if the caller is not root.
	 */
	status = fsal_check_setattr_perms(obj, attr, &current);

	if (FSAL_IS_ERROR(status))
		return status;

	is_superuser = op_ctx->fsal_export->exp_ops.is_superuser(
					op_ctx->fsal_export, &op_ctx->creds);
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
	if (!is_superuser &&
	    (FSAL_TEST_MASK(attr->valid_mask, ATTR_OWNER) ||
	     FSAL_TEST_MASK(attr->valid_mask, ATTR_GROUP)) &&
	    ((current.mode & (S_IXOTH | S_IXUSR | S_IXGRP)) != 0) &&
	    ((current.mode & (S_ISUID | S_ISGID)) != 0)) {
		/* Non-priviledged user changing ownership on an executable
		 * file with S_ISUID or S_ISGID bit set, need to be cleared.
		 */
		if (!FSAL_TEST_MASK(attr->valid_mask, ATTR_MODE)) {
			/* Mode wasn't being set, so set it now, start with
			 * the current attributes.
			 */
			attr->mode = current.mode;
			FSAL_SET_MASK(attr->valid_mask, ATTR_MODE);
		}

		/* Don't clear S_ISGID if the file isn't group executable.
		 * In that case, S_ISGID indicates mandatory locking and
		 * is not cleared by chown.
		 */
		if ((current.mode & S_IXGRP) != 0)
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
	if (!is_superuser &&
	    FSAL_TEST_MASK(attr->valid_mask, ATTR_MODE) &&
	    (attr->mode & S_ISGID) != 0 &&
	    fsal_not_in_group_list(current.group)) {
		/* Clear S_ISGID */
		attr->mode &= ~S_ISGID;
	}

	status = obj->obj_ops->setattr2(obj, bypass, state, attr);
	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_FSAL,
				 "FSAL returned STALE from setattr2");
		}
		return status;
	}

	if (!is_superuser)  {
		/* Done with the current attrs */
		fsal_release_attrs(&current);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Read the contents of a symlink
 *
 * @param[in] obj	Symlink to read
 * @param[out] link_content	Buffer to fill with link contents
 * @return FSAL status
 */
fsal_status_t fsal_readlink(struct fsal_obj_handle *obj,
			    struct gsh_buffdesc *link_content)
{
	if (obj->type != SYMBOLIC_LINK)
		return fsalstat(ERR_FSAL_BADTYPE, 0);

	/* Never refresh.  FSAL_MDCACHE will override for cached FSALs. */
	return obj->obj_ops->readlink(obj, link_content, false);
}

/**
 *
 * @brief Links a new name to a file
 *
 * This function hard links a new name to an existing file.
 *
 * @param[in]  obj      The file to which to add the new name.  Must
 *                      not be a directory.
 * @param[in]  dest_dir The directory in which to create the new name
 * @param[in]  name     The new name to add to the file
 *
 * @return FSAL status
 *                                  in destination.
 */
fsal_status_t fsal_link(struct fsal_obj_handle *obj,
			struct fsal_obj_handle *dest_dir,
			const char *name)
{
	fsal_status_t status = { 0, 0 };

	/* The file to be hardlinked can't be a DIRECTORY */
	if (obj->type == DIRECTORY)
		return fsalstat(ERR_FSAL_BADTYPE, 0);

	/* Is the destination a directory? */
	if (dest_dir->type != DIRECTORY)
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	/* Must be the same FS */
	if (obj->fs != dest_dir->fs)
		return fsalstat(ERR_FSAL_XDEV, 0);

	if (!op_ctx->fsal_export->exp_ops.fs_supports(
			op_ctx->fsal_export,
			fso_link_supports_permission_checks)) {
		status = fsal_access(dest_dir,
			FSAL_MODE_MASK_SET(FSAL_W_OK) |
			FSAL_MODE_MASK_SET(FSAL_X_OK) |
			FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE) |
			FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE));

		if (FSAL_IS_ERROR(status))
			return status;
	}

	if (state_deleg_conflict(obj, true)) {
		LogDebug(COMPONENT_FSAL, "Found an existing delegation for %s",
			  name);
		return fsalstat(ERR_FSAL_DELAY, 0);
	}

	/* Rather than performing a lookup first, just try to make the
	   link and return the FSAL's error if it fails. */
	status = obj->obj_ops->link(obj, dest_dir, name);
	return status;
}

/**
 * @brief Look up a name in a directory
 *
 * @param[in]  parent  Handle for the parent directory to be managed.
 * @param[in]  name    Name of the file that we are looking up.
 * @param[out] obj     Found file
 *
 * @note On success, @a handle has been ref'd
 *
 * @return FSAL status
 */

fsal_status_t fsal_lookup(struct fsal_obj_handle *parent,
			  const char *name,
			  struct fsal_obj_handle **obj,
			  struct fsal_attrlist *attrs_out)
{
	fsal_status_t fsal_status = { 0, 0 };
	fsal_accessflags_t access_mask =
	    (FSAL_MODE_MASK_SET(FSAL_X_OK) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE));

	*obj = NULL;

	if (parent->type != DIRECTORY) {
		*obj = NULL;
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	fsal_status = fsal_access(parent, access_mask);
	if (FSAL_IS_ERROR(fsal_status))
		return fsal_status;

	if (strcmp(name, ".") == 0) {
		parent->obj_ops->get_ref(parent);
		*obj = parent;
		return get_optional_attrs(*obj, attrs_out);
	} else if (strcmp(name, "..") == 0)
		return fsal_lookupp(parent, obj, attrs_out);


	return parent->obj_ops->lookup(parent, name, obj, attrs_out);
}

/**
 * @brief Look up a directory using a fully qualified path that is contained
 *        within the export in op_ctx->ctx_export.
 *
 * NOTE: This is pretty efficient to use even if the path IS the export. Our
 *       caller would have to do about the same having found the export, so
 *       we might as well have that logic in common code. In fact, we do it
 *       without using strcmp (the function that found the export has already
 *       done that...).
 *
 * @param[in]  path    Relative path to the directory
 * @param[out] obj     Found directory
 *
 * @note On success, @a handle has been ref'd
 *
 * NOTE: Since this does the path walk through MDCACHE, any intermediary
 *       nodes will be in the cache, since there are not extraneous LRU events
 *       if the cache is full, the intermediary entries are likely to be reaped
 *       as we walk the path, reducing churn in the cache.
 *
 * @return FSAL status
 */

fsal_status_t fsal_lookup_path(const char *path,
			       struct fsal_obj_handle **dirobj)
{
	fsal_status_t fsal_status;
	struct fsal_obj_handle *parent;
	char *rest;
	const char *start, *exppath;
	int len;

	/* First we need to strip off the export path, paying heed to
	 * nfs_param.core_param.mount_path_pseudo. Since our callers have used
	 * get_gsh_export_by_pseudo or get_gsh_export_by_path to find the
	 * export, the path MUST be proper.
	 */
	exppath = ctx_export_path(op_ctx);
	len = strlen(exppath);

	/* For debug builds, assure the above statement is true. */
	assert(strncmp(path, exppath, len) == 0);

	/* So now we can point start at the portion of the path beyond the
	 * export path. This will point start at the '\0' or '/' following the
	 * export path. We will be nice and skip all '/' characters that follow
	 * the export path.
	 */
	start = path + len;

	while (*start == '/')
		start++;

	/* Now get the length of the remaining relative path */
	len = strlen(start);

	if (len > MAXPATHLEN) {
		LogDebug(COMPONENT_FSAL,
			 "Failed due path %s is too long",
			 path);
		return posix2fsal_status(EINVAL);
	}

	/* Initialize parent to root of export and get a ref to it. */
	fsal_status = nfs_export_get_root_entry(op_ctx->ctx_export, &parent);

	if (FSAL_IS_ERROR(fsal_status))
		return fsal_status;

	/* Strip terminating '/' by shrinking length */
	while (len > 0 && start[len-1] == '/')
		len--;

	if (len == 0) {
		/* The path we were passed is effectively the export path, so
		 * just return the export root object with a reference.
		 */
		LogDebug(COMPONENT_FSAL,
			 "Returning root of export %s", exppath);
		*dirobj = parent;
		return fsal_status;
	}

	/* Allocate space for duplicate */
	rest = alloca(len + 1);

	/* Copy the string without any extraneous '/' at begin or end. */
	memcpy(rest, start, len);

	/* Terminate it */
	rest[len] = '\0';

	while (*rest != '\0') {
		struct fsal_obj_handle *obj;
		char *next_slash;

		/* Skip extra '/'. Note that since we trimmed trailing '/' there
		 * MUST be a non-NUL character and thus another path component
		 * following ANY '/' character in the path, so by skipping any
		 * extraneous '/' characters, we advance to the start of the
		 * next path component.
		 */
		while (*rest == '/')
			rest++;

		/* Find the end of this path element */
		next_slash = strchr(rest, '/');

		/* NUL terminate element if not at end of string. */
		if (next_slash != NULL)
			*next_slash = '\0';

		/* Disallow .. elements... */
		if (strcmp(rest, "..") == 0) {
			parent->obj_ops->put_ref(parent);
			LogInfo(COMPONENT_FSAL,
				"Failed due to '..' element in path %s",
				path);
			return posix2fsal_status(EACCES);
		}

		/* Skip "." elements... */
		if (rest[0] == '.' && rest[1] == '\0')
			goto skip;

		/* Open the next directory in the path */
		fsal_status = parent->obj_ops->lookup(parent,
						      rest,
						      &obj,
						      NULL);

		/* No matter what, we're done with the parent reference */
		parent->obj_ops->put_ref(parent);

		if (FSAL_IS_ERROR(fsal_status)) {
			LogDebug(COMPONENT_FSAL,
				 "Failed due to %s element in path %s error %s",
				 rest, path, fsal_err_txt(fsal_status));
			return fsal_status;
		}

		if (obj->type != DIRECTORY) {
			obj->obj_ops->put_ref(obj);
			LogDebug(COMPONENT_FSAL,
				 "Failed due to %s element in path %s not a directory",
				 rest, path);
			return posix2fsal_status(ENOTDIR);
		}

		/* Set up for next lookup */
		parent = obj;

skip:

		/* Done, break out */
		if (next_slash == NULL)
			break;

		/* Skip the '/' */
		rest = next_slash + 1;
	}

	/* Now parent is the object we're looking for and we already knmow it's
	 * a directory. Return it with the reference we are holding.
	 */
	*dirobj = parent;

	return fsal_status;
}

/**
 * @brief Look up a directory's parent
 *
 * @param[in]  obj     File whose parent is to be obtained.
 * @param[out] parent  Parent directory
 *
 * @return FSAL status
 */
fsal_status_t fsal_lookupp(struct fsal_obj_handle *obj,
			   struct fsal_obj_handle **parent,
			   struct fsal_attrlist *attrs_out)
{
	*parent = NULL;

	/* Never even think of calling FSAL_lookup on root/.. */

	if (obj->type == DIRECTORY) {
		fsal_status_t status = {0, 0};
		struct fsal_obj_handle *root_obj = NULL;

		status = nfs_export_get_root_entry(op_ctx->ctx_export,
						   &root_obj);

		if (FSAL_IS_ERROR(status))
			return status;

		if (obj == root_obj) {
			/* This entry is the root of the current export, so if
			 * we get this far, return itself. Note that NFS v4
			 * LOOKUPP will not come here, it catches the root entry
			 * earlier.
			 */
			*parent = obj;
			if (attrs_out != NULL) {
				/* Need to return the attributes of the
				 * current object.
				 */
				return obj->obj_ops->getattrs(obj, attrs_out);
			} else {
				/* Success */
				return fsalstat(ERR_FSAL_NO_ERROR, 0);
			}
		} else {
			/* Return entry from nfs_export_get_root_entry */
			root_obj->obj_ops->put_ref(root_obj);
		}
	}

	return obj->obj_ops->lookup(obj, "..", parent, attrs_out);
}

/**
 * @brief Set the create verifier
 *
 * This function sets the mtime/atime attributes according to the create
 * verifier
 *
 * @param[in] sattr   fsal_attrlist to be managed.
 * @param[in] verf_hi High long of verifier
 * @param[in] verf_lo Low long of verifier
 *
 */
void
fsal_create_set_verifier(struct fsal_attrlist *sattr, uint32_t verf_hi,
			 uint32_t verf_lo)
{
	sattr->atime.tv_sec = verf_hi;
	sattr->atime.tv_nsec = 0;
	FSAL_SET_MASK(sattr->valid_mask, ATTR_ATIME);
	sattr->mtime.tv_sec = verf_lo;
	sattr->mtime.tv_nsec = 0;
	FSAL_SET_MASK(sattr->valid_mask, ATTR_MTIME);
}

/**
 * @brief Creates an object in a directory
 *
 * This function creates an entry in the FSAL.  If the @a name exists, the
 * returned error is ERR_FSAL_EXIST, and @a obj is set if the existing object
 * has the same type as the requested one.
 *
 * The caller is expected to set the mode. Any other specified attributes
 * will also be set.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * @param[in]  parent       Parent directory
 * @param[in]  name         Name of the object to create
 * @param[in]  type         Type of the object to create
 * @param[in]  attrs        Attributes to be used at file creation
 * @param[in]  link_content Contents for symlink
 * @param[out] obj          Created file
 *
 * @note On success, @a obj has been ref'd
 *
 * @return FSAL status
 */

fsal_status_t fsal_create(struct fsal_obj_handle *parent,
			  const char *name,
			  object_file_type_t type,
			  struct fsal_attrlist *attrs,
			  const char *link_content,
			  struct fsal_obj_handle **obj,
			  struct fsal_attrlist *attrs_out)
{
	fsal_status_t status = { 0, 0 };
	attrmask_t orig_mask = attrs->valid_mask;

	/* For support_ex API, turn off owner and/or group attr
	 * if they are the same as the credentials.
	 */
	if ((attrs->valid_mask & ATTR_OWNER) &&
	    attrs->owner == op_ctx->creds.caller_uid)
		FSAL_UNSET_MASK(attrs->valid_mask, ATTR_OWNER);

	if ((attrs->valid_mask & ATTR_GROUP) &&
	    attrs->group == op_ctx->creds.caller_gid)
		FSAL_UNSET_MASK(attrs->valid_mask, ATTR_GROUP);

	/* Permission checking will be done by the FSAL operation. */

	/* Try to create it first */

	switch (type) {
	case REGULAR_FILE:
		status = fsal_open2(parent, NULL, FSAL_O_RDWR, FSAL_UNCHECKED,
				    name, attrs, NULL, obj, attrs_out);
		if (FSAL_IS_SUCCESS(status)) {
			/* Close it again; this is just a create */
			(void)fsal_close(*obj);
		}
		break;

	case DIRECTORY:
		status = parent->obj_ops->mkdir(parent, name, attrs,
					       obj, attrs_out);
		break;

	case SYMBOLIC_LINK:
		status = parent->obj_ops->symlink(parent, name, link_content,
						 attrs, obj, attrs_out);
		break;

	case SOCKET_FILE:
	case FIFO_FILE:
	case BLOCK_FILE:
	case CHARACTER_FILE:
		status = parent->obj_ops->mknode(parent, name, type,
						attrs, obj, attrs_out);
		break;

	case NO_FILE_TYPE:
	case EXTENDED_ATTR:
		/* we should never go there */
		status = fsalstat(ERR_FSAL_BADTYPE, 0);
		*obj = NULL;
		LogFullDebug(COMPONENT_FSAL,
			     "create failed because of bad type");
		goto out;
	}

	/* Check for the result */
	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_FSAL,
				 "FSAL returned STALE on create type %d", type);
		} else if (status.major == ERR_FSAL_EXIST) {
			/* Already exists. Check if type if correct */
			status = fsal_lookup(parent, name, obj, attrs_out);
			if (*obj != NULL) {
				status = fsalstat(ERR_FSAL_EXIST, 0);
				LogFullDebug(COMPONENT_FSAL,
					     "create failed because it already exists");
				if ((*obj)->type != type) {
					/* Incompatible types, returns NULL */
					(*obj)->obj_ops->put_ref((*obj));
					*obj = NULL;
					goto out;
				}
				if ((type == REGULAR_FILE) &&
				    (attrs->valid_mask & ATTR_SIZE) &&
				    attrs->filesize == 0) {
					attrs->valid_mask &= ATTR_SIZE;
					goto out;
				}
			}
		} else {
			*obj = NULL;
		}
		goto out;
	}

 out:

	/* Restore original mask so caller isn't bamboozled... */
	attrs->valid_mask = orig_mask;

	LogFullDebug(COMPONENT_FSAL,
		     "Returning obj=%p status=%s for %s FSAL=%s", *obj,
		     fsal_err_txt(status), name, parent->fsal->name);

	return status;
}

/**
 * @brief Return true if create verifier matches
 *
 * This function returns true if the create verifier matches
 *
 * @param[in] obj     File to be managed.
 * @param[in] verf_hi High long of verifier
 * @param[in] verf_lo Low long of verifier
 *
 * @return true if verified, false otherwise
 *
 */
bool fsal_create_verify(struct fsal_obj_handle *obj, uint32_t verf_hi,
			uint32_t verf_lo)
{
	/* True if the verifier matches */
	bool verified = false;
	struct fsal_attrlist attrs;

	fsal_prepare_attrs(&attrs, ATTR_ATIME | ATTR_MTIME);

	obj->obj_ops->getattrs(obj, &attrs);
	if (FSAL_TEST_MASK(attrs.valid_mask, ATTR_ATIME)
	    && FSAL_TEST_MASK(attrs.valid_mask, ATTR_MTIME)
	    && attrs.atime.tv_sec == verf_hi
	    && attrs.mtime.tv_sec == verf_lo)
		verified = true;

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	return verified;
}

struct fsal_populate_cb_state {
	struct fsal_obj_handle *directory;
	fsal_status_t *status;
	helper_readdir_cb cb;
	fsal_cookie_t last_cookie;
	enum cb_state cb_state;
	unsigned int *cb_nfound;
	attrmask_t attrmask;
	struct fsal_readdir_cb_parms cb_parms;
};

static enum fsal_dir_result
populate_dirent(const char *name,
		struct fsal_obj_handle *obj,
		struct fsal_attrlist *attrs,
		void *dir_state,
		fsal_cookie_t cookie)
{
	struct fsal_populate_cb_state *state =
	    (struct fsal_populate_cb_state *)dir_state;
	fsal_status_t status = {0, 0};
	enum fsal_dir_result retval;

	retval = DIR_CONTINUE;
	state->cb_parms.name = name;

	status.major = state->cb(&state->cb_parms, obj, attrs, attrs->fileid,
				 cookie, state->cb_state);

	if (status.major == ERR_FSAL_CROSS_JUNCTION) {
		struct fsal_obj_handle *junction_obj;
		struct gsh_export *junction_export = NULL;
		struct saved_export_context saved;
		struct fsal_attrlist attrs2;

		PTHREAD_RWLOCK_rdlock(&obj->state_hdl->jct_lock);

		/* Get a reference to the junction_export and remember it
		 * only if the junction export is valid.
		 */
		if (obj->state_hdl->dir.junction_export != NULL &&
		    export_ready(obj->state_hdl->dir.junction_export)) {
			junction_export = obj->state_hdl->dir.junction_export;
			get_gsh_export_ref(junction_export);
		}

		PTHREAD_RWLOCK_unlock(&obj->state_hdl->jct_lock);

		/* Get the root of the export across the junction. */
		if (junction_export != NULL) {
			status = nfs_export_get_root_entry(junction_export,
							   &junction_obj);

			if (FSAL_IS_ERROR(status)) {
				struct gsh_refstr *ref_fullpath;

				rcu_read_lock();

				ref_fullpath = gsh_refstr_get(
				    rcu_dereference(junction_export->fullpath));

				rcu_read_unlock();

				LogMajor(COMPONENT_FSAL,
					 "Failed to get root for %s, id=%d, status = %s",
					 ref_fullpath
						? ref_fullpath->gr_val
						: "",
					 junction_export->export_id,
					 fsal_err_txt(status));

				gsh_refstr_put(ref_fullpath);

				/* Need to signal problem to callback */
				state->cb_state = CB_PROBLEM;
				(void) state->cb(&state->cb_parms, NULL, NULL,
						 0, cookie, state->cb_state);
				/* Protocol layers NEVER do readahead. */
				retval = DIR_TERMINATE;
				put_gsh_export(junction_export);
				goto out;
			}
		} else {
			LogMajor(COMPONENT_FSAL,
				 "A junction became stale");
			/* Need to signal problem to callback */
			state->cb_state = CB_PROBLEM;
			(void) state->cb(&state->cb_parms, NULL, NULL, 0,
					 cookie, state->cb_state);
			/* Protocol layers NEVER do readahead. */
			retval = DIR_TERMINATE;
			goto out;
		}

		/* Now we need to get the cross-junction attributes. */
		save_op_context_export_and_set_export(&saved, junction_export);

		fsal_prepare_attrs(&attrs2,
				   op_ctx->fsal_export->exp_ops
					.fs_supported_attrs(op_ctx->fsal_export)
					| ATTR_RDATTR_ERR);

		status = junction_obj->obj_ops->getattrs(junction_obj, &attrs2);

		if (!FSAL_IS_ERROR(status)) {
			/* Now call the callback again with that. */
			state->cb_state = CB_JUNCTION;
			status.major = state->cb(&state->cb_parms,
						 junction_obj,
						 &attrs2,
						 junction_export
						     ->exp_mounted_on_file_id,
						 cookie,
						 state->cb_state);

			state->cb_state = CB_ORIGINAL;
		}

		fsal_release_attrs(&attrs2);

		/* Release our refs and restore op_context */
		junction_obj->obj_ops->put_ref(junction_obj);
		restore_op_context_export(&saved);

		/* state->cb (nfs4_readdir_callback) saved op_ctx
		 * ctx_export and fsal_export. Restore them here
		 */
		(void)state->cb(&state->cb_parms, NULL, NULL,
				 0, 0, CB_PROBLEM);
	}

	if (!state->cb_parms.in_result) {
		/* Protocol layers NEVER do readahead. */
		retval = DIR_TERMINATE;
		goto out;
	}

	(*state->cb_nfound)++;

out:

	/* Put the ref on obj that readdir took */
	obj->obj_ops->put_ref(obj);

	return retval;
}

/**
 * @brief Reads a directory
 *
 * This function iterates over the directory entries  and invokes a supplied
 * callback function for each one.
 *
 * @param[in]  directory The directory to be read
 * @param[in]  cookie    Starting cookie for the readdir operation
 * @param[out] eod_met   Whether the end of directory was met
 * @param[in]  attrmask  Attributes requested, used for permission checking
 *                       really all that matters is ATTR_ACL and any attrs
 *                       at all, specifics never actually matter.
 * @param[in]  cb        The callback function to receive entries
 * @param[in]  opaque    A pointer passed to be passed in
 *                       fsal_readdir_cb_parms
 *
 * @return FSAL status
 */

fsal_status_t fsal_readdir(struct fsal_obj_handle *directory,
		    uint64_t cookie,
		    unsigned int *nbfound,
		    bool *eod_met,
		    attrmask_t attrmask,
		    helper_readdir_cb cb,
		    void *opaque)
{
	fsal_status_t fsal_status = {0, 0};
	fsal_status_t cb_status = {0, 0};
	struct fsal_populate_cb_state state;

	*nbfound = 0;

	/* The access mask corresponding to permission to list directory
	   entries */
	fsal_accessflags_t access_mask =
	    (FSAL_MODE_MASK_SET(FSAL_R_OK) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR));
	fsal_accessflags_t access_mask_attr =
	    (FSAL_MODE_MASK_SET(FSAL_R_OK) | FSAL_MODE_MASK_SET(FSAL_X_OK) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE));

	/* readdir can be done only with a directory */
	if (directory->type != DIRECTORY) {
		LogDebug(COMPONENT_NFS_READDIR, "Not a directory");
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	/* Adjust access mask if ACL is asked for.
	 * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
	 */
	if ((attrmask & ATTR_ACL) != 0) {
		access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
		access_mask_attr |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
	}

	fsal_status = fsal_access(directory, access_mask);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "permission check for directory status=%s",
			 fsal_err_txt(fsal_status));
		return fsal_status;
	}
	if (attrmask != 0) {
		/* Check for access permission to get attributes */
		fsal_status_t attr_status = fsal_access(directory,
							access_mask_attr);
		if (FSAL_IS_ERROR(attr_status))
			LogDebug(COMPONENT_NFS_READDIR,
				 "permission check for attributes status=%s",
				 fsal_err_txt(attr_status));
		state.cb_parms.attr_allowed = !FSAL_IS_ERROR(attr_status);
	} else {
		/* No attributes requested. */
		state.cb_parms.attr_allowed = false;
	}

	state.directory = directory;
	state.status = &cb_status;
	state.cb = cb;
	state.last_cookie = 0;
	state.cb_parms.opaque = opaque;
	state.cb_parms.in_result = true;
	state.cb_parms.name = NULL;
	state.cb_state = CB_ORIGINAL;
	state.cb_nfound = nbfound;
	state.attrmask = attrmask;

	fsal_status = directory->obj_ops->readdir(directory, &cookie,
						 (void *)&state,
						 populate_dirent,
						 attrmask,
						 eod_met);

	return fsal_status;
}

/**
 *
 * @brief Remove a name from a directory.
 *
 * @param[in] parent  Handle for the parent directory to be managed
 * @param[in] name    Name to be removed
 *
 * @retval fsal_status_t
 */

fsal_status_t
fsal_remove(struct fsal_obj_handle *parent, const char *name)
{
	struct fsal_obj_handle *to_remove_obj = NULL;
	fsal_status_t status = { 0, 0 };

	if (parent->type != DIRECTORY) {
		status = fsalstat(ERR_FSAL_NOTDIR, 0);
		goto out_no_obj;
	}

	/* Looks up for the entry to remove */
	status = fsal_lookup(parent, name, &to_remove_obj, NULL);
	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL, "lookup %s failure %s",
			     name, fsal_err_txt(status));
		return status;
	}

	/* Do not remove a junction node or an export root. */
	if (obj_is_junction(to_remove_obj)) {
		LogCrit(COMPONENT_FSAL, "Attempt to remove export %s", name);
		status = fsalstat(ERR_FSAL_NOTEMPTY, 0);
		goto out;
	}

	if (state_deleg_conflict(to_remove_obj, true)) {
		LogDebug(COMPONENT_FSAL, "Found an existing delegation for %s",
			  name);
		status = fsalstat(ERR_FSAL_DELAY, 0);
		goto out;
	}

	LogFullDebug(COMPONENT_FSAL, "%s", name);

	/* Make sure the to_remove_obj is closed since unlink of an
	 * open file results in 'silly rename' on certain platforms.
	 */
	status = fsal_close(to_remove_obj);

	if (FSAL_IS_ERROR(status)) {
		/* non-fatal error. log the warning and move on */
		LogCrit(COMPONENT_FSAL,
			"Error closing %s before unlink: %s.",
			name, fsal_err_txt(status));
	}

#ifdef ENABLE_RFC_ACL
	status = fsal_remove_access(parent, to_remove_obj,
				    (to_remove_obj->type == DIRECTORY));
	if (FSAL_IS_ERROR(status))
		goto out;
#endif /* ENABLE_RFC_ACL */

	status = parent->obj_ops->unlink(parent, to_remove_obj, name);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL, "unlink %s failure %s",
			     name, fsal_err_txt(status));
		goto out;
	}

out:

	to_remove_obj->obj_ops->put_ref(to_remove_obj);

out_no_obj:

	LogFullDebug(COMPONENT_FSAL, "remove %s: status=%s", name,
		     fsal_err_txt(status));

	return status;
}

/**
 * @brief Renames a file
 *
 * @param[in] dir_src  The source directory
 * @param[in] oldname  The current name of the file
 * @param[in] dir_dest The destination directory
 * @param[in] newname  The name to be assigned to the object
 *
 * @return FSAL status
 */
fsal_status_t fsal_rename(struct fsal_obj_handle *dir_src,
			  const char *oldname,
			  struct fsal_obj_handle *dir_dest,
			  const char *newname)
{
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_obj_handle *lookup_src = NULL;

	if ((dir_src->type != DIRECTORY) || (dir_dest->type != DIRECTORY))
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	/* Check for . and .. on oldname and newname. */
	if (oldname[0] == '\0' || newname[0] == '\0'
	    || !strcmp(oldname, ".") || !strcmp(oldname, "..")
	    || !strcmp(newname, ".") || !strcmp(newname, "..")) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Check for object existence in source directory */
	fsal_status = fsal_lookup(dir_src, oldname, &lookup_src, NULL);

	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_FSAL,
			 "Rename (%p,%s)->(%p,%s) : source doesn't exist",
			 dir_src, oldname, dir_dest, newname);
		goto out;
	}

	/* Do not rename a junction node or an export root. */
	if (obj_is_junction(lookup_src)) {
		LogCrit(COMPONENT_FSAL, "Attempt to rename export %s", oldname);
		fsal_status = fsalstat(ERR_FSAL_NOTEMPTY, 0);
		goto out;
	}

	/* Don't allow rename of an object as parent of itself */
	if (dir_dest == lookup_src) {
		fsal_status = fsalstat(ERR_FSAL_INVAL, 0);
		goto out;
	}
	/* *
	 * added conflicts check for destination in MDCACHE layer
	 */
	if (state_deleg_conflict(lookup_src, true)) {
		LogDebug(COMPONENT_FSAL, "Found an existing delegation for %s",
			  oldname);
		fsal_status = fsalstat(ERR_FSAL_DELAY, 0);
		goto out;
	}

	LogFullDebug(COMPONENT_FSAL, "about to call FSAL rename");

	fsal_status = dir_src->obj_ops->rename(lookup_src, dir_src, oldname,
					      dir_dest, newname);

	LogFullDebug(COMPONENT_FSAL, "returned from FSAL rename");

	if (FSAL_IS_ERROR(fsal_status)) {

		LogFullDebug(COMPONENT_FSAL,
			     "FSAL rename failed with %s",
			     fsal_err_txt(fsal_status));

		goto out;
	}

out:
	if (lookup_src) {
		/* Note that even with a junction, this object is in the same
		 * export since that would be the junction node, NOT the export
		 * root node on the other side of the junction.
		 */
		lookup_src->obj_ops->put_ref(lookup_src);
	}

	return fsal_status;
}

/**
 * @brief Opens a file by name or by handle.
 *
 * This function accomplishes both a LOOKUP if necessary and an open.
 *
 * Returns with an LRU reference held on the entry.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation).
 *
 * At least the mode attribute must be set if createmode is not FSAL_NO_CREATE.
 * Some FSALs may still have to pass a mode on a create call for exclusive,
 * and even with FSAL_NO_CREATE, and empty set of attributes MUST be passed.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * @param[in]     in_obj     Parent directory or obj
 * @param[in,out] state      state_t to operate on
 * @param[in]     openflags  Details of how to open the file
 * @param[in]     createmode Mode if creating
 * @param[in]     name       If name is not NULL, entry is the parent directory
 * @param[in]     attr       Attributes to set on the file
 * @param[in]     verifier   Verifier to use with exclusive create
 * @param[out]    obj        New entry for the opened file
 *
 * @return FSAL status
 */

fsal_status_t fsal_open2(struct fsal_obj_handle *in_obj,
			 struct state_t *state,
			 fsal_openflags_t openflags,
			 enum fsal_create_mode createmode,
			 const char *name,
			 struct fsal_attrlist *attr,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **obj,
			 struct fsal_attrlist *attrs_out)
{
	fsal_status_t status = { 0, 0 };
	bool caller_perm_check = false;
	char *reason;

	*obj = NULL;

	if (attr != NULL) {
		LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
			    "attrs ", attr, false);

		status = fsal_check_create_owner(attr);

		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_FSAL,
				 "Not opening file - %s",
				 fsal_err_txt(status));
			return status;
		}
	}

	/* Handle attribute size = 0 here, normalize to FSAL_O_TRUNC
	 * instead of setting ATTR_SIZE.
	 */
	if (attr != NULL &&
	    FSAL_TEST_MASK(attr->valid_mask, ATTR_SIZE) &&
	    attr->filesize == 0) {
		LogFullDebug(COMPONENT_FSAL, "Truncate");
		/* Handle truncate to zero on open */
		openflags |= FSAL_O_TRUNC;
		/* Don't set the size if we later set the attributes */
		FSAL_UNSET_MASK(attr->valid_mask, ATTR_SIZE);
	}

	if (createmode >= FSAL_EXCLUSIVE && verifier == NULL)
		return fsalstat(ERR_FSAL_INVAL, 0);
	if (name)
		return open2_by_name(in_obj, state, openflags, createmode,
				     name, attr, verifier, obj, attrs_out);

	/* No name, directories don't make sense */
	if (in_obj->type == DIRECTORY) {
		if (createmode != FSAL_NO_CREATE)
			return fsalstat(ERR_FSAL_INVAL, 0);

		return fsalstat(ERR_FSAL_ISDIR, 0);
	}

	if (in_obj->type != REGULAR_FILE)
		return fsalstat(ERR_FSAL_BADTYPE, 0);

	/* Do a permission check on the file before opening. */
	status = check_open_permission(in_obj, openflags,
				       createmode >= FSAL_EXCLUSIVE, &reason);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "Not opening file %s%s",
			 reason, fsal_err_txt(status));
		return status;
	}

	/* Open THIS entry, so name must be NULL. The attr are passed in case
	 * this is a create with size = 0. We pass the verifier because this
	 * might be an exclusive recreate replay and we want the FSAL to
	 * check the verifier.
	 */
	status = in_obj->obj_ops->open2(in_obj,
				       state,
				       openflags,
				       createmode,
				       NULL,
				       attr,
				       verifier,
				       obj,
				       attrs_out,
				       &caller_perm_check);

	if (!FSAL_IS_ERROR(status)) {
		/* Get a reference to the entry. */
		*obj = in_obj;
		in_obj->obj_ops->get_ref(in_obj);
	}

	return status;
}

/**
 * @brief Re-Opens a file by handle.
 *
 * This MAY be used to open a file the first time if there is no need for
 * open by name or create semantics.
 *
 * @param[in]     obj              File to operate on
 * @param[in,out] state            state_t to operate on
 * @param[in]     openflags        Details of how to open the file
 * @param[in]     check_permission Indicate if permission should be checked
 *
 * @return FSAL status
 */

fsal_status_t fsal_reopen2(struct fsal_obj_handle *obj,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   bool check_permission)
{
	fsal_status_t status = { 0, 0 };
	char *reason = "FSAL reopen failed - ";

	if (check_permission) {
		/* Do a permission check on the file before re-opening. */
		status = check_open_permission(obj, openflags, false, &reason);
		if (FSAL_IS_ERROR(status))
			goto out;
	}

	/* Re-open the entry in the FSAL.
	 */
	status = obj->obj_ops->reopen2(obj, state, openflags);

 out:

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "Not re-opening file %s%s",
			 reason, fsal_err_txt(status));
	}

	return status;
}


fsal_status_t fsal_statfs(struct fsal_obj_handle *obj,
			  fsal_dynamicfsinfo_t *dynamicinfo)
{
	fsal_status_t fsal_status;
	struct fsal_export *export;

	export = op_ctx->ctx_export->fsal_export;
	/* Get FSAL to get dynamic info */
	fsal_status =
	    export->exp_ops.get_fs_dynamic_info(export, obj, dynamicinfo);
	LogFullDebug(COMPONENT_FSAL,
		     "dynamicinfo: {total_bytes = %" PRIu64
		     ", free_bytes = %" PRIu64 ", avail_bytes = %" PRIu64
		     ", total_files = %" PRIu64 ", free_files = %" PRIu64
		     ", avail_files = %" PRIu64 "}", dynamicinfo->total_bytes,
		     dynamicinfo->free_bytes, dynamicinfo->avail_bytes,
		     dynamicinfo->total_files, dynamicinfo->free_files,
		     dynamicinfo->avail_files);
	return fsal_status;
}

/**
 * @brief Verify an exclusive create replay when the file is already open.
 *
 * This may not be necessary in real life, however, pynfs definitely has a
 * test case that walks this path.
 *
 * @param[in]     obj        File to verify
 * @param[in]     verifier   Verifier to use with exclusive create
 *
 * @return FSAL status
 */

fsal_status_t fsal_verify2(struct fsal_obj_handle *obj,
			   fsal_verifier_t verifier)
{
	if (!obj->obj_ops->check_verifier(obj, verifier)) {
		/* Verifier check failed. */
		return fsalstat(ERR_FSAL_EXIST, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Fetch optional attributes
 *
 * The request_mask should be set in attrs_out indicating which attributes
 * are desired. If ATTR_RDATTR_ERR is set, and the getattrs fails,
 * the error ERR_FSAL_NO_ERROR will be returned, however the attributes
 * valid_mask will be set to ATTR_RDATTR_ERR. Otherwise, if
 * ATTR_RDATTR_ERR is not set and the getattrs fails, the error returned
 * by getattrs will be returned.
 *
 * @param[in]     obj_hdl   Object to get attributes for.
 * @param[in,out] attrs_out Optional attributes for the object
 *
 * @return FSAL status.
 **/
fsal_status_t get_optional_attrs(struct fsal_obj_handle *obj_hdl,
				 struct fsal_attrlist *attrs_out)
{
	fsal_status_t status;

	if (attrs_out == NULL)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	status = obj_hdl->obj_ops->getattrs(obj_hdl, attrs_out);

	if (FSAL_IS_ERROR(status)) {
		if (attrs_out->request_mask & ATTR_RDATTR_ERR) {
			/* Indicate the failure of requesting attributes by
			 * marking the ATTR_RDATTR_ERR in the mask.
			 */
			attrs_out->valid_mask = ATTR_RDATTR_ERR;
			status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		} /* otherwise let the error stand. */
	}

	return status;
}

/**
 * @brief Callback to implement syncronous read and write
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] args		Args for read call
 * @param[in] caller_data	Data for caller
 */
static void sync_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			 void *args, void *caller_data)
{
	struct async_process_data *data = caller_data;

	/* Fixup FSAL_SHARE_DENIED status */
	if (ret.major == ERR_FSAL_SHARE_DENIED)
		ret = fsalstat(ERR_FSAL_LOCKED, 0);

	data->ret = ret;

	/* Let caller know we are done. */

	PTHREAD_MUTEX_lock(data->mutex);

	data->done = true;

	pthread_cond_signal(data->cond);

	PTHREAD_MUTEX_unlock(data->mutex);
}

void fsal_read(struct fsal_obj_handle *obj_hdl,
	       bool bypass,
	       struct fsal_io_arg *arg,
	       struct async_process_data *data)
{
	obj_hdl->obj_ops->read2(obj_hdl, bypass, sync_cb, arg, data);

	PTHREAD_MUTEX_lock(data->mutex);

	while (!data->done) {
		int rc = pthread_cond_wait(data->cond, data->mutex);

		assert(rc == 0);
	}

	PTHREAD_MUTEX_unlock(data->mutex);
}

void fsal_write(struct fsal_obj_handle *obj_hdl,
		bool bypass,
		struct fsal_io_arg *arg,
		struct async_process_data *data)
{
	obj_hdl->obj_ops->write2(obj_hdl, bypass, sync_cb, arg, data);

	PTHREAD_MUTEX_lock(data->mutex);

	while (!data->done) {
		int rc = pthread_cond_wait(data->cond, data->mutex);

		assert(rc == 0);
	}

	PTHREAD_MUTEX_unlock(data->mutex);
}

#define XATTR_USER_PREFIX	"user."
#define XATTR_USER_PREFIX_LEN	(sizeof(XATTR_USER_PREFIX) - 1)

/**
 * @brief Convert a flat list of xattr names to xattrlist4
 *
 * @param[in] buf		Populated buffer returned from listxattr()
 * @param[in] listlen		Length of "buf"
 * @param[in] maxbytes		Max size of the returned lxr_names array
 * @param[in,out] lxa_cookie	Cookie from client, and returned cookie
 * @param[out] lxr_eof		Is this is the end of the xattrs?
 * @param[out] lxr_names	pointer to xattrlist4 that should be populated
 *
 * Most listxattr() implementations hand back a buffer with a concatenated set
 * of NULL terminated names. This helper does the work of converting that into
 * an xattrlist4, and handles the gory details of vetting the cookie and size
 * limits.
 */
fsal_status_t fsal_listxattr_helper(const char *buf,
			     size_t listlen,
			     uint32_t maxbytes,
			     nfs_cookie4 *lxa_cookie,
			     bool_t *lxr_eof,
			     xattrlist4 *lxr_names)
{
	int i, count = 0;
	uint64_t cookie = 0;
	uint32_t bytes = 0;
	const char *name, *start;
	const char *end = buf + listlen;
	xattrkey4 *names = NULL;
	fsal_status_t status;

	/* Figure out how big an array we'll need, and vet the cookie */
	name = buf;
	start = NULL;
	while (name < end) {
		size_t len;

		/* Do we have enough for "user.?" ? */
		len = strnlen(name, end - name);
		if (len <= XATTR_USER_PREFIX_LEN)
			goto next_name1;

		/* Does it start with "user." ? */
		if (strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
			goto next_name1;

		/*
		 * Valid "user." xattr. Bump the cookie value and compare
		 * previous one to one passed in.
		 */
		if (cookie++ < *lxa_cookie)
			goto next_name1;

		/* Do we have room to encode this name? */
		bytes += 4 + len - XATTR_USER_PREFIX_LEN;
		if (bytes > maxbytes) {
			/* Decrement cookie since we can't use this after all */
			--cookie;
			break;
		}

		/* We have a usable entry! */
		++count;

		/* Save pointer to first usable entry */
		if (!start)
			start = name;
next_name1:
		name += (len + 1);
	}

	/* No entries found? */
	if (count == 0) {
		/* We couldn't encode the first entry */
		if (bytes > maxbytes)
			return fsalstat(ERR_FSAL_TOOSMALL, 0);

		/* Bogus cookie from client? */
		if (cookie < *lxa_cookie)
			return fsalstat(ERR_FSAL_BADCOOKIE, 0);

		/* Otherwise, there just weren't any */
		goto out;
	}

	names = gsh_calloc(count, sizeof(*names));

	assert(start);
	name = start;
	i = 0;
	while (name < end && i < count) {
		size_t len;

		len = strnlen(name, end - name);

		/* Make sure it's the min length */
		if (len < XATTR_USER_PREFIX_LEN + 1)
			goto next_name2;

		if (strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
			goto next_name2;

		/* advance past "user." prefix */
		name += XATTR_USER_PREFIX_LEN;
		len -= XATTR_USER_PREFIX_LEN;

		names[i].utf8string_val = gsh_memdup(name, len);
		names[i].utf8string_len = len;
		++i;
next_name2:
		name += (len + 1);
	}

	/* Did we get everything? */
	if (i != count) {
		LogWarn(COMPONENT_FSAL, "LISTXATTRS encoding error!");
		status = fsalstat(ERR_FSAL_SERVERFAULT, 0);
		goto out_error;
	}
out:
	*lxa_cookie = cookie;
	*lxr_eof = (bytes <= maxbytes);
	lxr_names->xl4_count = count;
	lxr_names->xl4_entries = names;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
out_error:
	for (i = 0; i < count; ++i)
		gsh_free(names[i].utf8string_val);
	gsh_free(names);
	return status;
}

/** @} */
