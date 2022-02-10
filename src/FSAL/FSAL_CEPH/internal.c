/*
 * Copyright © 2012-2014, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * contributeur : William Allen Simpson <bill@cohortfs.com>
 *		  Marcus Watts <mdw@cohortfs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
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
 * @file   internal.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @author William Allen Simpson <bill@cohortfs.com>
 * @date Wed Oct 22 13:24:33 2014
 *
 * @brief Internal definitions for the Ceph FSAL
 *
 * This file includes internal function definitions, constants, and
 * variable declarations used to impelment the Ceph FSAL, but not
 * exposed as part of the API.
 */

#include <sys/stat.h>
#ifdef CEPHFS_POSIX_ACL
#include <sys/acl.h>
#include <acl/libacl.h>
#endif				/* CEPHFS_POSIX_ACL */
#include <cephfs/libcephfs.h>
#include "fsal_types.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "statx_compat.h"
#include "nfs_exports.h"
#include "internal.h"
#ifdef CEPHFS_POSIX_ACL
#include "posix_acls.h"
#endif				/* CEPHFS_POSIX_ACL */

/**
 * @brief Construct a new filehandle
 *
 * This function constructs a new Ceph FSAL object handle and attaches
 * it to the export.  After this call the attributes have been filled
 * in and the handdle is up-to-date and usable.
 *
 * @param[in]  stx    ceph_statx data for the file
 * @param[in]  export The export on which the object lives
 * @param[out] obj    Object created
 *
 * @return 0 on success, negative error codes on failure.
 */

void construct_handle(const struct ceph_statx *stx, struct Inode *i,
	struct ceph_export *export, struct ceph_handle **obj)
{
	/* Pointer to the handle under construction */
	struct ceph_handle *constructing = NULL;

	assert(i);

	constructing = gsh_calloc(1, sizeof(struct ceph_handle));

	constructing->key.hhdl.chk_ino = stx->stx_ino;
#ifdef CEPH_NOSNAP
	constructing->key.hhdl.chk_snap = stx->stx_dev;
#endif /* CEPH_NOSNAP */
	constructing->key.hhdl.chk_fscid = export->fscid;
	constructing->key.export_id = export->export.export_id;
	constructing->i = i;
	constructing->up_ops = export->export.up_ops;

	fsal_obj_handle_init(&constructing->handle, &export->export,
			     posix2fsal_type(stx->stx_mode));
	constructing->handle.obj_ops = &CephFSM.handle_ops;
	constructing->handle.fsid = posix2fsal_fsid(stx->stx_dev);
	constructing->handle.fileid = stx->stx_ino;

	*obj = constructing;
}

/**
 * @brief Release all resrouces for a handle
 *
 * @param[in] obj Handle to release
 */

void deconstruct_handle(struct ceph_handle *obj)
{
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);

	assert(op_ctx->fsal_export->export_id == obj->key.export_id);

	ceph_ll_put(export->cmount, obj->i);
	fsal_obj_handle_fini(&obj->handle);
	gsh_free(obj);
}

unsigned int
attrmask2ceph_want(attrmask_t mask)
{
	unsigned int want = 0;

	if (mask & ATTR_MODE)
		want |= CEPH_STATX_MODE;
	if (mask & ATTR_OWNER)
		want |= CEPH_STATX_UID;
	if (mask & ATTR_GROUP)
		want |= CEPH_STATX_GID;
	if (mask & ATTR_SIZE)
		want |= CEPH_STATX_SIZE;
	if (mask & ATTR_NUMLINKS)
		want |= CEPH_STATX_NLINK;
	if (mask & ATTR_SPACEUSED)
		want |= CEPH_STATX_BLOCKS;
	if (mask & ATTR_ATIME)
		want |= CEPH_STATX_ATIME;
	if (mask & ATTR_CTIME)
		want |= CEPH_STATX_CTIME;
	if (mask & ATTR_MTIME)
		want |= CEPH_STATX_MTIME;
	if (mask & ATTR_CREATION)
		want |= CEPH_STATX_BTIME;
	if (mask & ATTR_CHANGE)
		want |= CEPH_STATX_VERSION;

	return want;
}

void ceph2fsal_attributes(const struct ceph_statx *stx,
			  struct fsal_attrlist *fsalattr)
{
	/* These are always considered to be available */
	fsalattr->valid_mask |= ATTR_TYPE|ATTR_FSID|ATTR_RAWDEV|ATTR_FILEID;
	fsalattr->supported = CEPH_SUPPORTED_ATTRS;
	fsalattr->type = posix2fsal_type(stx->stx_mode);
	fsalattr->rawdev = posix2fsal_devt(stx->stx_rdev);
	fsalattr->fsid = posix2fsal_fsid(stx->stx_dev);
	fsalattr->fileid = stx->stx_ino;

	/* Disable seclabels if not enabled in config */
	if (!op_ctx_export_has_option(EXPORT_OPTION_SECLABEL_SET))
		fsalattr->supported &= ~ATTR4_SEC_LABEL;

	if (stx->stx_mask & CEPH_STATX_MODE) {
		fsalattr->valid_mask |= ATTR_MODE;
		fsalattr->mode = unix2fsal_mode(stx->stx_mode);
	}
	if (stx->stx_mask & CEPH_STATX_UID) {
		fsalattr->valid_mask |= ATTR_OWNER;
		fsalattr->owner = stx->stx_uid;
	}
	if (stx->stx_mask & CEPH_STATX_GID) {
		fsalattr->valid_mask |= ATTR_GROUP;
		fsalattr->group = stx->stx_gid;
	}
	if (stx->stx_mask & CEPH_STATX_SIZE) {
		fsalattr->valid_mask |= ATTR_SIZE;
		fsalattr->filesize = stx->stx_size;
	}
	if (stx->stx_mask & CEPH_STATX_NLINK) {
		fsalattr->valid_mask |= ATTR_NUMLINKS;
		fsalattr->numlinks = stx->stx_nlink;
	}

	if (stx->stx_mask & CEPH_STATX_BLOCKS) {
		fsalattr->valid_mask |= ATTR_SPACEUSED;
		fsalattr->spaceused = stx->stx_blocks * S_BLKSIZE;
	}

	/* Use full timer resolution */
	if (stx->stx_mask & CEPH_STATX_ATIME) {
		fsalattr->valid_mask |= ATTR_ATIME;
		fsalattr->atime = stx->stx_atime;
	}
	if (stx->stx_mask & CEPH_STATX_CTIME) {
		fsalattr->valid_mask |= ATTR_CTIME;
		fsalattr->ctime = stx->stx_ctime;
	}
	if (stx->stx_mask & CEPH_STATX_MTIME) {
		fsalattr->valid_mask |= ATTR_MTIME;
		fsalattr->mtime = stx->stx_mtime;
	}
	if (stx->stx_mask & CEPH_STATX_BTIME) {
		fsalattr->valid_mask |= ATTR_CREATION;
		fsalattr->creation = stx->stx_btime;
	}

	if (stx->stx_mask & CEPH_STATX_VERSION) {
		fsalattr->valid_mask |= ATTR_CHANGE;
		fsalattr->change = stx->stx_version;
	}
}

#ifdef CEPHFS_POSIX_ACL
/*
 * @brief Get posix acl from cephfs
 * @param[in]  export        Export on which the object lives
 * @param[in]  objhandle     Object
 * @param[in]  name          Name of the extended attribute
 * @param[out] p_acl         Posix ACL
 *
 * @return 0 on success, negative error codes on failure.
 */

int ceph_get_posix_acl(struct ceph_export *export,
	struct ceph_handle *objhandle, const char *name, acl_t *p_acl)
{
	char *value = NULL;
	int rc = 0, size;
	acl_t acl_tmp = NULL;

	LogFullDebug(COMPONENT_FSAL, "get POSIX ACL");

	/* Get extended attribute size */
	size = fsal_ceph_ll_getxattr(export->cmount, objhandle->i, name,
				NULL, 0, &op_ctx->creds);
	if (size <= 0) {
		LogFullDebug(COMPONENT_FSAL, "getxattr returned %d", size);
		return 0;
	}

	value = gsh_malloc(size);

	/* Read extended attribute's value */
	rc = fsal_ceph_ll_getxattr(export->cmount, objhandle->i, name,
				value, size, &op_ctx->creds);
	if (rc < 0) {
		LogMajor(COMPONENT_FSAL, "getxattr returned %d", rc);
		if (rc == -ENODATA) {
			rc = 0;
		}

		goto out;
	}

	/* Convert extended attribute to posix acl */
	acl_tmp = xattr_2_posix_acl((struct acl_ea_header *)value, size);
	if (!acl_tmp) {
		LogMajor(COMPONENT_FSAL,
				"failed to convert xattr to posix acl");
		rc = -EFAULT;
		goto out;
	}

	*p_acl = acl_tmp;

out:
	gsh_free(value);
	return rc;
}

/*
 * @brief Set Posix ACL
 * @param[in]  export        Export on which the object lives
 * @param[in]  objhandle     Object
 * @param[in]  is_dir        True when object type is directory
 * @param[in]  attrs         Attributes
 *
 * @return FSAL status.
 */

fsal_status_t ceph_set_acl(struct ceph_export *export,
	struct ceph_handle *objhandle, bool is_dir, struct fsal_attrlist *attrs)
{
	int size = 0, count, rc;
	acl_t acl = NULL;
	acl_type_t type;
	char *name = NULL;
	void *value = NULL;
	fsal_status_t status = {0, 0};

	if (!attrs->acl) {
		LogWarn(COMPONENT_FSAL, "acl is empty");
		status = fsalstat(ERR_FSAL_FAULT, 0);
		goto out;
	}

	if (is_dir) {
		type = ACL_TYPE_DEFAULT;
		name = ACL_EA_DEFAULT;
	} else {
		type = ACL_TYPE_ACCESS;
		name = ACL_EA_ACCESS;
	}

	acl = fsal_acl_2_posix_acl(attrs->acl, type);
	if (acl_valid(acl) != 0) {
		LogWarn(COMPONENT_FSAL,
				"failed to convert fsal acl to posix acl");
		status = fsalstat(ERR_FSAL_FAULT, 0);
		goto out;
	}

	count = acl_entries(acl);
	if (count > 0) {
		size = posix_acl_xattr_size(count);
		value = gsh_malloc(size);

		rc = posix_acl_2_xattr(acl, value, size);
		if (rc < 0) {
			LogMajor(COMPONENT_FSAL,
					"failed to convert posix acl to xattr");
			status = fsalstat(ERR_FSAL_FAULT, 0);
			goto out;
		}
	}

	rc = fsal_ceph_ll_setxattr(export->cmount, objhandle->i,
				name, value, size, 0, &op_ctx->creds);
	if (rc < 0) {
		status = ceph2fsal_error(rc);
	}

out:
	if (acl) {
		acl_free((void *)acl);
	}

	if (value) {
		gsh_free(value);
	}

	return status;
}

/*
 * @brief Get FSAL ACL
 * @param[in]  export        Export on which the object lives
 * @param[in]  objhandle     Object
 * @param[in]  is_dir        True when object type is directory
 * @param[out] attrs         Attributes
 *
 * @return 0 on success, negative error codes on failure.
 */

int ceph_get_acl(struct ceph_export *export, struct ceph_handle *objhandle,
	bool is_dir, struct fsal_attrlist *attrs)
{
	acl_t e_acl = NULL, i_acl = NULL;
	fsal_acl_data_t acldata;
	fsal_ace_t *pace = NULL;
	fsal_acl_status_t aclstatus;
	int e_count = 0, i_count = 0, new_count = 0, new_i_count = 0;
	int rc = 0;

	rc = ceph_get_posix_acl(export, objhandle, ACL_EA_ACCESS, &e_acl);
	if (rc < 0) {
		LogMajor(COMPONENT_FSAL,
				"failed to get posix acl: %s", ACL_EA_ACCESS);
		goto out;
	}
	e_count = ace_count(e_acl);

	if (is_dir) {
		rc = ceph_get_posix_acl(export,
					objhandle, ACL_EA_DEFAULT, &i_acl);
		if (rc < 0) {
			LogMajor(COMPONENT_FSAL,
				"failed to get posix acl: %s", ACL_EA_DEFAULT);
		} else {
			i_count = ace_count(i_acl);
		}
	}

	acldata.naces = 2 * (e_count + i_count);
	LogDebug(COMPONENT_FSAL,
			"No of aces present in fsal_acl_t = %d", acldata.naces);
	if (!acldata.naces) {
		rc = 0;
		goto out;
	}

	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);
	pace = acldata.aces;

	if (e_count > 0) {
		new_count = posix_acl_2_fsal_acl(e_acl, is_dir, false, &pace);
	} else {
		LogDebug(COMPONENT_FSAL,
			"effective acl is not set for this object");
	}

	if (i_count > 0) {
		new_i_count = posix_acl_2_fsal_acl(i_acl, true, true, &pace);
		new_count += new_i_count;
	} else {
		LogDebug(COMPONENT_FSAL,
			"Inherit acl is not set for this directory");
	}

	/* Reallocating acldata into the required size */
	acldata.aces = (fsal_ace_t *) gsh_realloc(acldata.aces,
					new_count*sizeof(fsal_ace_t));
	acldata.naces = new_count;

	attrs->acl = nfs4_acl_new_entry(&acldata, &aclstatus);
	if (attrs->acl == NULL) {
		LogCrit(COMPONENT_FSAL, "failed to create a new acl entry");
		rc = -EFAULT;
		goto out;
	}

	rc = 0;
	attrs->valid_mask |= ATTR_ACL;

out:
	if (e_acl) {
		acl_free((void *)e_acl);
	}

	if (i_acl) {
		acl_free((void *)i_acl);
	}

	return rc;
}
#endif				/* CEPHFS_POSIX_ACL */
