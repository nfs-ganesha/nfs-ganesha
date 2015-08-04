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
#include "log.h"
#include "fsal.h"
#include "nfs_convert.h"
#include "nfs_exports.h"
#include "nfs4_acls.h"
#include "sal_data.h"
#include "FSAL/fsal_commonlib.h"

/**
 * This is a global counter of files opened.
 *
 * This is preliminary expected to go away.  Problems with this method are that
 * it overcounts file descriptors for FSALs that don't use them for open files,
 * and, under the Lieb Rearchitecture, FSALs will be responsible for caching
 * their own file descriptors, with interfaces for Cache_Inode to interrogate
 * them as to usage or instruct them to close them.
 */

size_t open_fd_count = 0;

/* XXX dang locking
 * - FD locking (open, close, is_open) - was content lock
 *   will be fixed in conversion to support_ex
 */


bool fsal_is_open(struct fsal_obj_handle *obj)
{
	if ((obj == NULL) || (obj->type != REGULAR_FILE))
		return false;
	return (obj->obj_ops.status(obj) != FSAL_O_CLOSED);
}

static bool fsal_not_in_group_list(gid_t gid)
{
	const struct user_cred *creds = op_ctx->creds;
	int i;

	if (creds->caller_gid == gid) {

		LogDebug(COMPONENT_FSAL,
			 "User %u is has active group %u", creds->caller_uid,
			 gid);
		return false;
	}
	for (i = 0; i < creds->caller_glen; i++) {
		if (creds->caller_garray[i] == gid) {
			LogDebug(COMPONENT_FSAL,
				 "User %u is member of group %u",
				 creds->caller_uid, gid);
			return false;
		}
	}

	LogDebug(COMPONENT_FSAL,
		 "User %u IS NOT member of group %u", creds->caller_uid,
		 gid);
	return true;
}

static fsal_status_t check_open_permission(struct fsal_obj_handle *obj,
					   fsal_openflags_t openflags,
					   char **reason)
{
	fsal_status_t status = {0, 0};
	fsal_accessflags_t access_mask = 0;

	if (openflags & FSAL_O_READ)
		access_mask |= FSAL_READ_ACCESS;

	if (openflags & FSAL_O_WRITE)
		access_mask |= FSAL_WRITE_ACCESS;

	status = fsal_access(obj, access_mask, NULL, NULL);

	if (!FSAL_IS_ERROR(status)) {
		*reason = "";
		return status;
	}

	/* If non-permission error, return it. */
	if (status.major != ERR_FSAL_PERM) {
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
	 */
	status = fsal_access(obj, FSAL_EXECUTE_ACCESS, NULL, NULL);

	if (!FSAL_IS_ERROR(status))
		*reason = "";
	else
		*reason = "fsal_access failed with EXECUTE_ACCESS - ";

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
 * @param[in] current Current attributes for object
 *
 * @return FSAL status
 */
static fsal_status_t fsal_check_setattr_perms(struct fsal_obj_handle *obj,
					      struct attrlist *attr,
					      struct attrlist *current)
{
	fsal_status_t status = {0, 0};
	fsal_accessflags_t access_check = 0;
	bool not_owner;
	char *note = "";
	const struct user_cred *creds = op_ctx->creds;

	/* Shortcut, if current user is root, then we can just bail out with
	 * success. */
	if (creds->caller_uid == 0) {
		note = " (Ok for root user)";
		goto out;
	}

	fsal_prepare_attrs(current,
			   ATTR_MODE | ATTR_OWNER | ATTR_GROUP | ATTR_ACL);

	status = obj->obj_ops.getattrs(obj, current);

	if (FSAL_IS_ERROR(status))
		return status;

	not_owner = (creds->caller_uid != current->owner);

	/* Only ownership change need to be checked for owner */
	if (FSAL_TEST_MASK(attr->mask, ATTR_OWNER)) {
		/* non-root is only allowed to "take ownership of file" */
		if (attr->owner != creds->caller_uid) {
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
	if (FSAL_TEST_MASK(attr->mask, ATTR_GROUP)) {
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

	if (FSAL_TEST_MASK(attr->mask, ATTR_MODE)
	    || FSAL_TEST_MASK(attr->mask, ATTR_ACL)) {
		/* Changing mode or ACL requires ACE4_WRITE_ACL */
		access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ACL);
		LogDebug(COMPONENT_FSAL,
			    "Change MODE or ACL requires FSAL_ACE_PERM_WRITE_ACL");
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_SIZE)) {
		/* Changing size requires owner or write permission */
	  /** @todo: does FSAL_ACE_PERM_APPEND_DATA allow enlarging the file? */
		access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
		LogDebug(COMPONENT_FSAL,
			    "Change SIZE requires FSAL_ACE_PERM_WRITE_DATA");
	}

	/* Check if just setting atime and mtime to "now" */
	if ((FSAL_TEST_MASK(attr->mask, ATTR_MTIME_SERVER)
	     || FSAL_TEST_MASK(attr->mask, ATTR_ATIME_SERVER))
	    && !FSAL_TEST_MASK(attr->mask, ATTR_MTIME)
	    && !FSAL_TEST_MASK(attr->mask, ATTR_ATIME)) {
		/* If either atime and/or mtime are set to "now" then need only
		 * have write permission.
		 *
		 * Technically, client should not send atime updates, but if
		 * they really do, we'll let them to make the perm check a bit
		 * simpler. */
		access_check |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
		LogDebug(COMPONENT_FSAL,
			    "Change ATIME and MTIME to NOW requires FSAL_ACE_PERM_WRITE_DATA");
	} else if (FSAL_TEST_MASK(attr->mask, ATTR_MTIME_SERVER)
		   || FSAL_TEST_MASK(attr->mask, ATTR_ATIME_SERVER)
		   || FSAL_TEST_MASK(attr->mask, ATTR_MTIME)
		   || FSAL_TEST_MASK(attr->mask, ATTR_ATIME)) {
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
		status = obj->obj_ops.test_access(obj, access_check, NULL,
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

	status = obj->obj_ops.test_access(obj, FSAL_W_OK, NULL, NULL, false);

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
			    struct attrlist *attr,
			    fsal_verifier_t verifier,
			    struct fsal_obj_handle **obj,
			    struct attrlist *attrs_out)
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
	status = fsal_access(in_obj, FSAL_EXECUTE_ACCESS, NULL, NULL);
	if (FSAL_IS_ERROR(status))
		return status;

	status = in_obj->obj_ops.open2(in_obj,
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
			     op_ctx->ctx_export->fullpath,
			     fsal_err_txt(status));
		return status;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Created entry %p FSAL %s for %s",
		     *obj, (*obj)->fsal->name, name);

	if (!caller_perm_check)
		return status;

	/* Do a permission check on the just opened file. */
	status = check_open_permission(*obj, openflags, &reason);

	if (!FSAL_IS_ERROR(status))
		return status;

	LogDebug(COMPONENT_FSAL,
		 "Closing file check_open_permission failed %s-%s",
		 reason, fsal_err_txt(status));

	close_status = (*obj)->obj_ops.close2(*obj, state);

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
 * @param[in]     bypass Bypass share reservation checking
 * @param[in]     state  Possible state associated with the entry
 * @param[in,out] attr   Attributes to set
 * @return FSAL status
 */
fsal_status_t fsal_setattr(struct fsal_obj_handle *obj, bool bypass,
			   struct state_t *state, struct attrlist *attr)
{
	fsal_status_t status = { 0, 0 };
	const struct user_cred *creds = op_ctx->creds;
	struct attrlist current;

	if ((attr->mask & (ATTR_SIZE | ATTR4_SPACE_RESERVED))
	     && (obj->type != REGULAR_FILE)) {
		LogWarn(COMPONENT_FSAL,
			"Attempt to truncate non-regular file: type=%d",
			obj->type);
		return fsalstat(ERR_FSAL_BADTYPE, 0);
	}

	/* Is it allowed to change times ? */
	if (!op_ctx->fsal_export->exp_ops.fs_supports(op_ctx->fsal_export,
						      fso_cansettime) &&
	    (FSAL_TEST_MASK
	     (attr->mask,
	      (ATTR_ATIME | ATTR_CREATION | ATTR_CTIME | ATTR_MTIME))))
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* Do permission checks, which returns with the attributes for the
	 * object if the caller is not root.
	 */
	status = fsal_check_setattr_perms(obj, attr, &current);

	if (FSAL_IS_ERROR(status))
		return status;

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
	    ((current.mode & (S_IXOTH | S_IXUSR | S_IXGRP)) != 0) &&
	    ((current.mode & (S_ISUID | S_ISGID)) != 0)) {
		/* Non-priviledged user changing ownership on an executable
		 * file with S_ISUID or S_ISGID bit set, need to be cleared.
		 */
		if (!FSAL_TEST_MASK(attr->mask, ATTR_MODE)) {
			/* Mode wasn't being set, so set it now, start with
			 * the current attributes.
			 */
			attr->mode = current.mode;
			FSAL_SET_MASK(attr->mask, ATTR_MODE);
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
	if (creds->caller_uid != 0 &&
	    FSAL_TEST_MASK(attr->mask, ATTR_MODE) &&
	    (attr->mode & S_ISGID) != 0 &&
	    fsal_not_in_group_list(current.group)) {
		/* Clear S_ISGID */
		attr->mode &= ~S_ISGID;
	}

	if (obj->fsal->m_ops.support_ex(obj)) {
		status = obj->obj_ops.setattr2(obj, bypass, state, attr);
		if (FSAL_IS_ERROR(status)) {
			if (status.major == ERR_FSAL_STALE) {
				LogEvent(COMPONENT_FSAL,
					 "FSAL returned STALE from setattr2");
			}
			return status;
		}
	} else {
		status = obj->obj_ops.setattrs(obj, attr);
		if (FSAL_IS_ERROR(status)) {
			if (status.major == ERR_FSAL_STALE) {
				LogEvent(COMPONENT_FSAL,
					 "FSAL returned STALE from setattrs");
			}
			return status;
		}
	}

	if (creds->caller_uid != 0) {
		/* Done with the current attrs */
		fsal_release_attrs(&current);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *
 * @brief Checks the permissions on an object
 *
 * This function returns success if the supplied credentials possess
 * permission required to meet the specified access.
 *
 * @param[in]  obj         The object to be checked
 * @param[in]  access_type The kind of access to be checked
 *
 * @return FSAL status
 *
 */
fsal_status_t fsal_access(struct fsal_obj_handle *obj,
			  fsal_accessflags_t access_type,
			  fsal_accessflags_t *allowed,
			  fsal_accessflags_t *denied)
{
	return
	    obj->obj_ops.test_access(obj, access_type, allowed, denied, false);
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
	return obj->obj_ops.readlink(obj, link_content, false);
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

	if (!op_ctx->fsal_export->exp_ops.fs_supports(
			op_ctx->fsal_export,
			fso_link_supports_permission_checks)) {
		status = fsal_access(dest_dir,
			FSAL_MODE_MASK_SET(FSAL_W_OK) |
			FSAL_MODE_MASK_SET(FSAL_X_OK) |
			FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE) |
			FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE),
			NULL, NULL);

		if (FSAL_IS_ERROR(status))
			return status;
	}

	/* Rather than performing a lookup first, just try to make the
	   link and return the FSAL's error if it fails. */
	status = obj->obj_ops.link(obj, dest_dir, name);
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
			  struct attrlist *attrs_out)
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

	fsal_status = fsal_access(parent, access_mask, NULL, NULL);
	if (FSAL_IS_ERROR(fsal_status))
		return fsal_status;

	if (strcmp(name, ".") == 0) {
		parent->obj_ops.get_ref(parent);
		*obj = parent;
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	} else if (strcmp(name, "..") == 0)
		return fsal_lookupp(parent, obj, attrs_out);


	return parent->obj_ops.lookup(parent, name, obj, attrs_out);
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
			   struct attrlist *attrs_out)
{
	*parent = NULL;

	/* Never even think of calling FSAL_lookup on root/.. */

	if (obj->type == DIRECTORY) {
		fsal_status_t status = {0, 0};
		struct fsal_obj_handle *root_obj = NULL;

		status = nfs_export_get_root_entry(op_ctx->ctx_export, &root_obj);
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
				return obj->obj_ops.getattrs(obj, attrs_out);
			} else {
				/* Success */
				return fsalstat(ERR_FSAL_NO_ERROR, 0);
			}
		} else {
			/* Return entry from nfs_export_get_root_entry */
			root_obj->obj_ops.put_ref(root_obj);
		}
	}

	return obj->obj_ops.lookup(obj, "..", parent, attrs_out);
}

/**
 * @brief Set the create verifier
 *
 * This function sets the mtime/atime attributes according to the create
 * verifier
 *
 * @param[in] sattr   attrlist to be managed.
 * @param[in] verf_hi High long of verifier
 * @param[in] verf_lo Low long of verifier
 *
 */
void
fsal_create_set_verifier(struct attrlist *sattr, uint32_t verf_hi,
			 uint32_t verf_lo)
{
	sattr->atime.tv_sec = verf_hi;
	sattr->atime.tv_nsec = 0;
	FSAL_SET_MASK(sattr->mask, ATTR_ATIME);
	sattr->mtime.tv_sec = verf_lo;
	sattr->mtime.tv_nsec = 0;
	FSAL_SET_MASK(sattr->mask, ATTR_MTIME);
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
			  struct attrlist *attrs,
			  const char *link_content,
			  struct fsal_obj_handle **obj,
			  struct attrlist *attrs_out)
{
	fsal_status_t status = { 0, 0 };
	attrmask_t orig_mask = attrs->mask;
	uint64_t owner = attrs->owner;
	uint64_t group = attrs->group;
	bool support_ex = parent->fsal->m_ops.support_ex(parent);

	if ((type != REGULAR_FILE) && (type != DIRECTORY)
	    && (type != SYMBOLIC_LINK) && (type != SOCKET_FILE)
	    && (type != FIFO_FILE) && (type != CHARACTER_FILE)
	    && (type != BLOCK_FILE)) {
		status = fsalstat(ERR_FSAL_BADTYPE, 0);

		LogFullDebug(COMPONENT_FSAL,
			     "create failed because of bad type");
		*obj = NULL;
		goto out;
	}

	if (!support_ex) {
		/* For old API, make sure owner/group attr matches op_ctx */
		attrs->mask |= ATTR_OWNER | ATTR_GROUP;
		attrs->owner = op_ctx->creds->caller_uid;
		attrs->group = op_ctx->creds->caller_gid;
	} else {
		/* For support_ex API, turn off owner and/or group attr
		 * if they are the same as the credentials.
		 */
		if ((attrs->mask & ATTR_OWNER) &&
		    attrs->owner == op_ctx->creds->caller_uid)
			FSAL_UNSET_MASK(attrs->mask, ATTR_OWNER);
		if ((attrs->mask & ATTR_GROUP) &&
		    attrs->group == op_ctx->creds->caller_gid)
			FSAL_UNSET_MASK(attrs->mask, ATTR_GROUP);
	}

	/* Permission checking will be done by the FSAL operation. */

	/* Try to create it first */

	switch (type) {
	case REGULAR_FILE:
		/* NOTE: will not be called here if support_ex */
		status = parent->obj_ops.create(parent, name, attrs,
						obj, attrs_out);
		break;

	case DIRECTORY:
		status = parent->obj_ops.mkdir(parent, name, attrs,
					       obj, attrs_out);
		break;

	case SYMBOLIC_LINK:
		status = parent->obj_ops.symlink(parent, name, link_content,
						 attrs, obj, attrs_out);
		break;

	case SOCKET_FILE:
	case FIFO_FILE:
		status = parent->obj_ops.mknode(parent, name, type,
						NULL, /* dev_t !needed */
						attrs, obj, attrs_out);
		break;

	case BLOCK_FILE:
	case CHARACTER_FILE:
		status = parent->obj_ops.mknode(parent, name, type,
						&attrs->rawdev,
						attrs, obj, attrs_out);
		break;

	case NO_FILE_TYPE:
	case EXTENDED_ATTR:
		/* we should never go there */
		status = fsalstat(ERR_FSAL_BADTYPE, 0);
		*obj = NULL;
		LogFullDebug(COMPONENT_FSAL,
			     "create failed because inconsistent entry");
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
					(*obj)->obj_ops.put_ref((*obj));
					*obj = NULL;
					goto out;
				}
				if ((type == REGULAR_FILE) &&
				    (attrs->mask & ATTR_SIZE) &&
				    attrs->filesize == 0) {
					attrs->mask &= ATTR_SIZE;
					goto setattrs;
				}
			}
		} else {
			*obj = NULL;
		}
		goto out;
	}

setattrs:
	if (!support_ex) {
		/* Handle setattr for old API */
		attrs->mask = orig_mask;
		attrs->owner = owner;
		attrs->group = group;

		/* We already handled mode */
		FSAL_UNSET_MASK(attrs->mask, ATTR_MODE);

		/* Check if attrs->owner was same as creds */
		if ((attrs->mask & ATTR_OWNER) &&
		    (op_ctx->creds->caller_uid == attrs->owner))
			FSAL_UNSET_MASK(attrs->mask, ATTR_OWNER);

		/* Check if attrs->group was same as creds */
		if ((attrs->mask & ATTR_GROUP) &&
		    (op_ctx->creds->caller_gid == attrs->group))
			FSAL_UNSET_MASK(attrs->mask, ATTR_GROUP);

		if (attrs->mask) {
			/* If any attributes were left to set, set them now. */
			status = fsal_setattr(*obj, false, NULL, attrs);
		}
	}

 out:

	/* Restore original mask so caller isn't bamboozled... */
	attrs->mask = orig_mask;

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
	struct attrlist attrs;

	fsal_prepare_attrs(&attrs, ATTR_ATIME | ATTR_MTIME);

	obj->obj_ops.getattrs(obj, &attrs);
	if (FSAL_TEST_MASK(attrs.mask, ATTR_ATIME)
	    && FSAL_TEST_MASK(attrs.mask, ATTR_MTIME)
	    && attrs.atime.tv_sec == verf_hi
	    && attrs.mtime.tv_sec == verf_lo)
		verified = true;

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	return verified;
}

/**
 * @brief New style reads
 *
 * @param[in]     obj          File to be read or written
 * @param[in]     bypass       If state doesn't indicate a share reservation,
 *                             bypass any deny read
 * @param[in]     state        state_t associated with the operation
 * @param[in]     offset       Absolute file position for I/O
 * @param[in]     io_size      Amount of data to be read or written
 * @param[out]    bytes_moved  The length of data successfuly read
 * @param[in,out] buffer       Where in memory to read data
 * @param[out]    eof          Whether a READ encountered the end of file
 * @param[in]     info         io_info for READ_PLUS
 *
 * @return FSAL status
 */

fsal_status_t fsal_read2(struct fsal_obj_handle *obj,
			 bool bypass,
			 struct state_t *state,
			 uint64_t offset,
			 size_t io_size,
			 size_t *bytes_moved,
			 void *buffer,
			 bool *eof,
			 struct io_info *info)
{
	/* Error return from FSAL calls */
	fsal_status_t status = { 0, 0 };

	status = obj->obj_ops.read2(obj, bypass, state, offset, io_size, buffer,
				    bytes_moved, eof, info);

	/* Fixup FSAL_SHARE_DENIED status */
	if (status.major == ERR_FSAL_SHARE_DENIED)
		status = fsalstat(ERR_FSAL_LOCKED, 0);

	LogFullDebug(COMPONENT_FSAL,
		     "FSAL READ operation returned %s, asked_size=%zu, effective_size=%zu",
		     fsal_err_txt(status), io_size, *bytes_moved);

	if (FSAL_IS_ERROR(status)) {
		*bytes_moved = 0;
		return status;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "inode/direct: io_size=%zu, bytes_moved=%zu, offset=%"
		     PRIu64, io_size, *bytes_moved, offset);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief New style writes
 *
 * @param[in]     obj          File to be read or written
 * @param[in]     bypass       If state doesn't indicate a share reservation,
 *                             bypass any non-mandatory deny write
 * @param[in]     state        state_t associated with the operation
 * @param[in]     offset       Absolute file position for I/O
 * @param[in]     io_size      Amount of data to be written
 * @param[out]    bytes_moved  The length of data successfuly written
 * @param[in,out] buffer       Where in memory to write data
 * @param[in]     sync         Whether the write is synchronous or not
 * @param[in]     info         io_info for WRITE_PLUS
 *
 * @return FSAL status
 */

fsal_status_t fsal_write2(struct fsal_obj_handle *obj,
			  bool bypass,
			  struct state_t *state,
			  uint64_t offset,
			  size_t io_size,
			  size_t *bytes_moved,
			  void *buffer,
			  bool *sync,
			  struct io_info *info)
{
	/* Error return from FSAL calls */
	fsal_status_t status = { 0, 0 };

	if (op_ctx->export_perms->options & EXPORT_OPTION_COMMIT) {
		/* Force sync if export requires it */
		*sync = true;
	}

	status = obj->obj_ops.write2(obj,
				     bypass,
				     state,
				     offset,
				     io_size,
				     buffer,
				     bytes_moved,
				     sync,
				     info);

	/* Fixup ERR_FSAL_SHARE_DENIED status */
	if (status.major == ERR_FSAL_SHARE_DENIED)
		status = fsalstat(ERR_FSAL_LOCKED, 0);

	LogFullDebug(COMPONENT_FSAL,
		     "FSAL WRITE operation returned %s, asked_size=%zu, effective_size=%zu",
		     fsal_err_txt(status), io_size, *bytes_moved);

	if (FSAL_IS_ERROR(status)) {
		*bytes_moved = 0;

		return status;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "inode/direct: io_size=%zu, bytes_moved=%zu, offset=%"
		     PRIu64, io_size, *bytes_moved, offset);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Read/Write
 *
 * @param[in]     obj          File to be read or written
 * @param[in]     io_direction Whether this is a read or a write
 * @param[in]     offset       Absolute file position for I/O
 * @param[in]     io_size      Amount of data to be read or written
 * @param[out]    bytes_moved  The length of data successfuly read or written
 * @param[in,out] buffer       Where in memory to read or write data
 * @param[out]    eof          Whether a READ encountered the end of file.  May
 *                             be NULL for writes.
 * @param[in]     sync         Whether the write is synchronous or not
 *
 * @return FSAL status
 */

fsal_status_t fsal_rdwr(struct fsal_obj_handle *obj,
		      fsal_io_direction_t io_direction,
		      uint64_t offset, size_t io_size,
		      size_t *bytes_moved, void *buffer,
		      bool *eof,
		      bool *sync, struct io_info *info)
{
	/* Error return from FSAL calls */
	fsal_status_t fsal_status = { 0, 0 };
	/* Required open mode to successfully read or write */
	fsal_openflags_t openflags = FSAL_O_CLOSED;
	fsal_openflags_t loflags;
	/* TRUE if we opened a previously closed FD */
	bool opened = false;

	/* Set flags for a read or write, as appropriate */
	if (io_direction == FSAL_IO_READ ||
	    io_direction == FSAL_IO_READ_PLUS) {
		openflags = FSAL_O_READ;
	} else {
		/* Pretent that the caller requested sync (stable write)
		 * if the export has COMMIT option. Note that
		 * FSAL_O_SYNC is not always honored, so just setting
		 * FSAL_O_SYNC has no guaranty that this write will be
		 * a stable write.
		 */
		if (op_ctx->export_perms->options & EXPORT_OPTION_COMMIT)
			*sync = true;
		openflags = FSAL_O_WRITE;
		if (*sync)
			openflags |= FSAL_O_SYNC;
	}

	assert(obj != NULL);

	/* IO is done only on REGULAR_FILEs */
	if (obj->type != REGULAR_FILE) {
		fsal_status = fsalstat(
		    obj->type ==
		    DIRECTORY ? ERR_FSAL_ISDIR :
		    ERR_FSAL_BADTYPE, 0);
		goto out;
	}

	loflags = obj->obj_ops.status(obj);
	while ((!fsal_is_open(obj))
	       || (loflags && loflags != FSAL_O_RDWR && loflags != openflags)) {
		loflags = obj->obj_ops.status(obj);
		if ((!fsal_is_open(obj))
		    || (loflags && loflags != FSAL_O_RDWR
			&& loflags != openflags)) {
			fsal_status = fsal_open(obj, openflags);
			if (FSAL_IS_ERROR(fsal_status))
				goto out;
			opened = true;
		}
		loflags = obj->obj_ops.status(obj);
	}

	/* Call FSAL_read or FSAL_write */
	if (io_direction == FSAL_IO_READ) {
		fsal_status = obj->obj_ops.read(obj, offset, io_size, buffer,
						bytes_moved, eof);
	} else if (io_direction == FSAL_IO_READ_PLUS) {
		fsal_status = obj->obj_ops.read_plus(obj, offset, io_size,
						     buffer, bytes_moved, eof,
						     info);
	} else {
		bool fsal_sync = *sync;

		if (io_direction == FSAL_IO_WRITE)
			fsal_status = obj->obj_ops.write(obj, offset, io_size,
							 buffer, bytes_moved,
							 &fsal_sync);
		else
			fsal_status = obj->obj_ops.write_plus(obj, offset,
							      io_size, buffer,
							      bytes_moved,
							      &fsal_sync, info);
		/* Alright, the unstable write is complete. Now if it was
		   supposed to be a stable write we can sync to the hard
		   drive. */

		if (*sync && !(loflags & FSAL_O_SYNC) && !fsal_sync &&
		    !FSAL_IS_ERROR(fsal_status)) {
			fsal_status = obj->obj_ops.commit(obj, offset, io_size);
		} else {
			*sync = fsal_sync;
		}
	}

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_rdwr_plus: FSAL IO operation returned %s, asked_size=%zu, effective_size=%zu",
		     fsal_err_txt(fsal_status), io_size, *bytes_moved);

	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_DELAY) {
			LogEvent(COMPONENT_FSAL,
				 "fsal_rdwr_plus: FSAL_write  returned EBUSY");
		} else {
			LogDebug(COMPONENT_FSAL,
				 "fsal_rdwr_plus: fsal_status = %s",
				 fsal_err_txt(fsal_status));
		}

		*bytes_moved = 0;

		if (fsal_status.major == ERR_FSAL_STALE)
			goto out;

		if ((fsal_status.major != ERR_FSAL_NOT_OPENED)
		    && (obj->obj_ops.status(obj) != FSAL_O_CLOSED)) {
			LogFullDebug(COMPONENT_FSAL,
				     "fsal_rdwr_plus: CLOSING file %p",
				     obj);

			fsal_status = obj->obj_ops.close(obj);
			if (FSAL_IS_ERROR(fsal_status)) {
				LogCrit(COMPONENT_FSAL,
					"Error closing file in fsal_rdwr_plus: %s.",
					fsal_err_txt(fsal_status));
			}
		}

		goto out;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_rdwr_plus: inode/direct: io_size=%zu, bytes_moved=%zu, offset=%"
		     PRIu64, io_size, *bytes_moved, offset);

	if (opened) {
		fsal_status = obj->obj_ops.close(obj);
		if (FSAL_IS_ERROR(fsal_status)) {
			LogEvent(COMPONENT_FSAL,
				 "fsal_rdwr_plus: close = %s",
				 fsal_err_txt(fsal_status));
			goto out;
		}
	}

	fsal_status = fsalstat(0, 0);

 out:

	return fsal_status;
}

struct fsal_populate_cb_state {
	struct fsal_obj_handle *directory;
	fsal_status_t *status;
	fsal_getattr_cb_t cb;
	void *opaque;
	enum cb_state cb_state;
	unsigned int *cb_nfound;
	attrmask_t attrmask;
};

static bool
populate_dirent(const char *name,
		struct fsal_obj_handle *obj,
		struct attrlist *attrs,
		void *dir_state,
		fsal_cookie_t cookie)
{
	struct fsal_populate_cb_state *state =
	    (struct fsal_populate_cb_state *)dir_state;
	struct fsal_readdir_cb_parms cb_parms = { state->opaque, name,
		true, true };
	fsal_status_t status = {0, 0};

	status.major = state->cb(&cb_parms, obj, attrs, attrs->fileid,
				 cookie, state->cb_state);

	if (status.major == ERR_FSAL_CROSS_JUNCTION) {
		struct fsal_obj_handle *junction_obj;
		struct gsh_export *junction_export = NULL;
		struct fsal_export *saved_export;
		struct attrlist attrs2;

		PTHREAD_RWLOCK_rdlock(&obj->state_hdl->state_lock);

		/* Get a reference to the junction_export and remember it
		 * only if the junction export is valid.
		 */
		if (obj->state_hdl->dir.junction_export != NULL &&
		    export_ready(obj->state_hdl->dir.junction_export)) {
			get_gsh_export_ref(obj->state_hdl->dir.junction_export);
			junction_export = obj->state_hdl->dir.junction_export;
		}

		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

		/* Get the root of the export across the junction. */
		if (junction_export != NULL) {
			status = nfs_export_get_root_entry(junction_export,
							   &junction_obj);

			if (FSAL_IS_ERROR(status)) {
				LogMajor(COMPONENT_FSAL,
					 "Failed to get root for %s, id=%d, status = %s",
					 junction_export->fullpath,
					 junction_export->export_id,
					 fsal_err_txt(status));
				/* Need to signal problem to callback */
				state->cb_state = CB_PROBLEM;
				(void) state->cb(&cb_parms, NULL, NULL, 0,
						 cookie, state->cb_state);
				return false;
			}
		} else {
			LogMajor(COMPONENT_FSAL,
				 "A junction became stale");
			/* Need to signal problem to callback */
			state->cb_state = CB_PROBLEM;
			(void) state->cb(&cb_parms, NULL, NULL, 0, cookie,
					 state->cb_state);
			return false;
		}

		/* Now we need to get the cross-junction attributes. */
		saved_export = op_ctx->fsal_export;
		op_ctx->fsal_export = junction_export->fsal_export;

		fsal_prepare_attrs(&attrs2,
				   op_ctx->fsal_export->exp_ops
					.fs_supported_attrs(op_ctx->fsal_export)
					| ATTR_RDATTR_ERR);

		status = junction_obj->obj_ops.getattrs(junction_obj, &attrs2);

		if (!FSAL_IS_ERROR(status)) {
			/* Now call the callback again with that. */
			state->cb_state = CB_JUNCTION;
			status.major = state->cb(&cb_parms,
						 junction_obj,
						 &attrs2,
						 junction_export
						     ->exp_mounted_on_file_id,
						 cookie,
						 state->cb_state);

			state->cb_state = CB_ORIGINAL;
		}

		fsal_release_attrs(&attrs2);

		/* Release our refs */
		op_ctx->fsal_export = saved_export;

		junction_obj->obj_ops.put_ref(junction_obj);
		put_gsh_export(junction_export);
	}

	if (!cb_parms.in_result)
		return false;

	(*state->cb_nfound)++;

	return true;
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
		    fsal_getattr_cb_t cb,
		    void *opaque)
{
	fsal_status_t fsal_status = {0, 0};
	fsal_status_t attr_status = {0, 0};
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
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "Not a directory");
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	/* Adjust access mask if ACL is asked for.
	 * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
	 */
	if ((attrmask & ATTR_ACL) != 0) {
		access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
		access_mask_attr |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
	}

	fsal_status = fsal_access(directory, access_mask, NULL, NULL);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "permission check for directory status=%s",
			     fsal_err_txt(fsal_status));
		return fsal_status;
	}
	if (attrmask != 0) {
		/* Check for access permission to get attributes */
		attr_status = fsal_access(directory, access_mask_attr, NULL,
					  NULL);
		if (FSAL_IS_ERROR(attr_status))
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "permission check for attributes status=%s",
				     fsal_err_txt(fsal_status));
	}

	state.directory = directory;
	state.status = &cb_status;
	state.cb = cb;
	state.opaque = opaque;
	state.cb_state = CB_ORIGINAL;
	state.cb_nfound = nbfound;
	state.attrmask = attrmask;

	fsal_status = directory->obj_ops.readdir(directory, &cookie,
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
#ifdef ENABLE_RFC_ACL
	bool isdir = false;
#endif /* ENABLE_RFC_ACL */

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
	if (to_remove_obj->type == DIRECTORY) {
#ifdef ENABLE_RFC_ACL
		isdir = true;
#endif /* ENABLE_RFC_ACL */

		PTHREAD_RWLOCK_rdlock(&to_remove_obj->state_hdl->state_lock);
		if (to_remove_obj->state_hdl->dir.junction_export != NULL ||
		    atomic_fetch_int32_t(
			&to_remove_obj->state_hdl->dir.exp_root_refcount)
		    != 0) {
			/* Trying to remove an export mount point */
			LogCrit(COMPONENT_FSAL, "Attempt to remove export %s",
				name);

			PTHREAD_RWLOCK_unlock(
					&to_remove_obj->state_hdl->state_lock);
			status = fsalstat(ERR_FSAL_NOTEMPTY, 0);
			goto out;
		}
		PTHREAD_RWLOCK_unlock(&to_remove_obj->state_hdl->state_lock);
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
	status = fsal_remove_access(parent, to_remove_obj, isdir);
	if (FSAL_IS_ERROR(status))
		goto out;
#endif /* ENABLE_RFC_ACL */

	status = parent->obj_ops.unlink(parent, to_remove_obj, name);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL, "unlink %s failure %s",
			     name, fsal_err_txt(status));
		goto out;
	}

out:

	to_remove_obj->obj_ops.put_ref(to_remove_obj);

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
	if (!strcmp(oldname, ".") || !strcmp(oldname, "..")
	    || !strcmp(newname, ".") || !strcmp(newname, "..")) {
		return fsalstat(ERR_FSAL_BADNAME, 0);
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
	if (lookup_src->type == DIRECTORY) {
		PTHREAD_RWLOCK_rdlock(&lookup_src->state_hdl->state_lock);

		if ((lookup_src->state_hdl->dir.junction_export != NULL ||
		     atomic_fetch_int32_t(
			&lookup_src->state_hdl->dir.exp_root_refcount) != 0)) {
			/* Trying to rename an export mount point */
			PTHREAD_RWLOCK_unlock(
					&lookup_src->state_hdl->state_lock);
			LogCrit(COMPONENT_FSAL, "Attempt to rename export %s",
				oldname);
			fsal_status = fsalstat(ERR_FSAL_NOTEMPTY, 0);
			goto out;
		}
		PTHREAD_RWLOCK_unlock(&lookup_src->state_hdl->state_lock);
	}

	LogFullDebug(COMPONENT_FSAL, "about to call FSAL rename");

	fsal_status = dir_src->obj_ops.rename(lookup_src, dir_src, oldname,
					      dir_dest, newname);

	LogFullDebug(COMPONENT_FSAL, "returned from FSAL rename");

	if (FSAL_IS_ERROR(fsal_status)) {

		LogFullDebug(COMPONENT_FSAL,
			     "FSAL rename failed with %s",
			     fsal_err_txt(fsal_status));

		goto out;
	}

out:
	if (lookup_src)
		lookup_src->obj_ops.put_ref(lookup_src);

	return fsal_status;
}

/**
 * @brief Open a file
 *
 * @param[in] obj	File to open
 * @param[in] openflags	The type of access for which to open
 * @return FSAL status
 */
fsal_status_t fsal_open(struct fsal_obj_handle *obj_hdl,
			fsal_openflags_t openflags)
{
	fsal_openflags_t current_flags;
	fsal_status_t status = {0, 0};

	if (obj_hdl->type != REGULAR_FILE)
		return fsalstat(ERR_FSAL_BADTYPE, 0);

	current_flags = obj_hdl->obj_ops.status(obj_hdl);

	/* Filter out overloaded FSAL_O_RECLAIM */
	openflags &= ~FSAL_O_RECLAIM;

	/* Make sure current state meet requirements */
	if ((current_flags != FSAL_O_RDWR) && (current_flags != FSAL_O_CLOSED)
	    && (current_flags != openflags)) {
		bool closed;
		/* Flags are insufficient; need to re-open */
		if (op_ctx->fsal_export->exp_ops.fs_supports(
			op_ctx->fsal_export, fso_reopen_method)) {
			/* FSAL has re-open; use that */
			status = obj_hdl->obj_ops.reopen(obj_hdl,
							   openflags);
			closed = false;
		} else {
			status = obj_hdl->obj_ops.close(obj_hdl);
			closed = true;
		}
		if (FSAL_IS_ERROR(status)
		    && (status.major != ERR_FSAL_NOT_OPENED))
			return status;
		if (!FSAL_IS_ERROR(status) && closed)
			(void) atomic_dec_size_t(&open_fd_count);

		/* Potentially force re-openning */
		current_flags = obj_hdl->obj_ops.status(obj_hdl);
	}

	if (current_flags == FSAL_O_CLOSED) {
		status = obj_hdl->obj_ops.open(obj_hdl, openflags);
		if (FSAL_IS_ERROR(status))
			return status;

		(void) atomic_inc_size_t(&open_fd_count);

		LogDebug(COMPONENT_FSAL,
			 "obj %p: openflags = %d, open_fd_count = %zd",
			 obj_hdl, openflags,
			 atomic_fetch_size_t(&open_fd_count));
	}
	status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	return status;
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
			 struct attrlist *attr,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **obj,
			 struct attrlist *attrs_out)
{
	fsal_status_t status = { 0, 0 };
	bool caller_perm_check = false;
	char *reason;

	*obj = NULL;

	if (attr != NULL)
		LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
			    "attrs ", attr, false);

	/* Handle attribute size = 0 here, normalize to FSAL_O_TRUNC
	 * instead of setting ATTR_SIZE.
	 */
	if (attr != NULL &&
	    FSAL_TEST_MASK(attr->mask, ATTR_SIZE) &&
	    attr->filesize == 0) {
		LogFullDebug(COMPONENT_FSAL, "Truncate");
		/* Handle truncate to zero on open */
		openflags |= FSAL_O_TRUNC;
		/* Don't set the size if we later set the attributes */
		FSAL_UNSET_MASK(attr->mask, ATTR_SIZE);
	}

	if (createmode >= FSAL_EXCLUSIVE && verifier == NULL)
		return fsalstat(ERR_FSAL_INVAL, 0);
	if (name)
		return open2_by_name(in_obj, state, openflags, createmode, name,
				     attr, verifier, obj, attrs_out);

	if (createmode != FSAL_NO_CREATE)
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* Check type if not a create. */
	if (in_obj->type == DIRECTORY)
		return fsalstat(ERR_FSAL_ISDIR, 0);
	else if (in_obj->type != REGULAR_FILE)
		return fsalstat(ERR_FSAL_BADTYPE, 0);

	/* Do a permission check on the file before opening. */
	status = check_open_permission(in_obj, openflags, &reason);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "Not opening file file %s%s",
			 reason, fsal_err_txt(status));
		return status;
	}

	/* Open THIS entry, so name must be NULL. The attr are passed in case
	 * this is a create with size = 0. We pass the verifier because this
	 * might be an exclusive recreate replay and we want the FSAL to
	 * check the verifier.
	 */
	status = in_obj->obj_ops.open2(in_obj,
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
		in_obj->obj_ops.get_ref(in_obj);
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
		status = check_open_permission(obj, openflags, &reason);
		if (FSAL_IS_ERROR(status))
			goto out;
	}

	/* Re-open the entry in the FSAL.
	 */
	status = obj->obj_ops.reopen2(obj, state, openflags);

 out:

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "Not re-opening file file %s%s",
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
		     "fsal_statfs: dynamicinfo: {total_bytes = %" PRIu64
		     ", free_bytes = %" PRIu64 ", avail_bytes = %" PRIu64
		     ", total_files = %" PRIu64 ", free_files = %" PRIu64
		     ", avail_files = %" PRIu64 "}", dynamicinfo->total_bytes,
		     dynamicinfo->free_bytes, dynamicinfo->avail_bytes,
		     dynamicinfo->total_files, dynamicinfo->free_files,
		     dynamicinfo->avail_files);
	return fsal_status;
}

/**
 * @brief Commit a section of a file to storage
 *
 * @param[in] obj	File to commit
 * @param[in] offset	Offset for start of commit
 * @param[in] len	Length of commit
 * @return FSAL status
 */
fsal_status_t fsal_commit(struct fsal_obj_handle *obj, off_t offset,
			 size_t len)
{
	/* Error return from FSAL calls */
	fsal_status_t fsal_status = { 0, 0 };
	bool opened = false;

	if ((uint64_t) len > ~(uint64_t) offset)
		return fsalstat(ERR_FSAL_INVAL, 0);

	if (obj->fsal->m_ops.support_ex(obj))
		return obj->obj_ops.commit2(obj, offset, len);

	if (!fsal_is_open(obj)) {
		LogFullDebug(COMPONENT_FSAL, "need to open");
		fsal_status = fsal_open(obj, FSAL_O_WRITE);
		if (FSAL_IS_ERROR(fsal_status))
			return fsal_status;
		opened = true;
	}

	fsal_status = obj->obj_ops.commit(obj, offset, len);

	if (opened)
		obj->obj_ops.close(obj);

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
	if (!obj->obj_ops.check_verifier(obj, verifier)) {
		/* Verifier check failed. */
		return fsalstat(ERR_FSAL_EXIST, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** @} */
