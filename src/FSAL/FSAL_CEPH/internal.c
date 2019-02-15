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
#include <cephfs/libcephfs.h>
#include "fsal_types.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "statx_compat.h"
#include "nfs_exports.h"
#include "internal.h"

/**
 * @brief Construct a new filehandle
 *
 * This function constructs a new Ceph FSAL object handle and attaches
 * it to the export.  After this call the attributes have been filled
 * in and the handdle is up-to-date and usable.
 *
 * @param[in]  stx    ceph_statx data for the file
 * @param[in]  export Export on which the object lives
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

	constructing->key.chk_vi.ino.val = stx->stx_ino;
#ifdef CEPH_NOSNAP
	constructing->key.chk_vi.snapid.val = stx->stx_dev;
#endif /* CEPH_NOSNAP */
	constructing->key.chk_fscid = export->fscid;
	constructing->i = i;
	constructing->up_ops = export->export.up_ops;

	fsal_obj_handle_init(&constructing->handle, &export->export,
			     posix2fsal_type(stx->stx_mode));
	constructing->handle.obj_ops = &CephFSM.handle_ops;
	constructing->handle.fsid = posix2fsal_fsid(stx->stx_dev);
	constructing->handle.fileid = stx->stx_ino;

	constructing->export = export;

	*obj = constructing;
}

/**
 * @brief Release all resrouces for a handle
 *
 * @param[in] obj Handle to release
 */

void deconstruct_handle(struct ceph_handle *obj)
{
	ceph_ll_put(obj->export->cmount, obj->i);
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
			  struct attrlist *fsalattr)
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
