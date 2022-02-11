// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file    fsal_lookup.c
 * @date    $Date: 2006/01/24 13:45:37 $
 * @brief   Lookup operations.
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#include "config.h"
#include <string.h>
#include "fsal.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_localfs.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"

uint64_t get_handle2inode(struct gpfs_file_handle *gfh)
{
	struct f_handle {
		char unused1[8];
		uint64_t inode;  /* inode for file */
		char unused2[8];
		uint64_t pinode; /* inode for parent */
	} *f_handle = (struct f_handle *)gfh->f_handle;

	return f_handle->inode;
}

/**
 *  @brief Looks up for an object into a directory.
 *
 *        if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 *  @param parent Handle of the parent directory to search the object in.
 *  @param filename The name of the object to find.
 *  @param fsal_attr Pointer to the attributes of the object we found.
 *  @param fh The handle of the object corresponding to filename.
 *  @param new_fs New FS
 *
 *  @return - ERR_FSAL_NO_ERROR, if no error.
 *          - Another error code else.
 */
#define GPFS_ROOT_INODE  3
fsal_status_t
GPFSFSAL_lookup(struct fsal_obj_handle *parent, const char *filename,
		struct fsal_attrlist *fsal_attr, struct gpfs_file_handle *fh,
		struct fsal_filesystem **new_fs)
{
	fsal_status_t status;
	int parent_fd;
	struct gpfs_fsal_obj_handle *parent_hdl;
	struct gpfs_filesystem *gpfs_fs;
	struct fsal_fsid__ fsid;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	if (!parent || !filename)
		return fsalstat(ERR_FSAL_FAULT, 0);

	assert(*new_fs == parent->fs);

	parent_hdl =
	    container_of(parent, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = parent->fs->private_data;
	status = fsal_internal_handle2fd(export_fd, parent_hdl->handle,
					 &parent_fd, O_RDONLY);

	if (FSAL_IS_ERROR(status))
		return status;

	/* Be careful about junction crossing, symlinks, hardlinks,... */
	switch (parent->type) {
	case DIRECTORY:
		/* OK */
		break;

	case REGULAR_FILE:
	case SYMBOLIC_LINK:
		/* not a directory */
		fsal_internal_close(parent_fd, NULL, 0);
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	default:
		fsal_internal_close(parent_fd, NULL, 0);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	status = fsal_internal_get_handle_at(parent_fd, filename, fh,
					     export_fd);

	/* After getting file handle 'fh' we won't be using parent_fd,
	 * hence we can close parent_fd here.
	 */
	fsal_internal_close(parent_fd, NULL, 0);

	if (status.major == ERR_FSAL_NOENT && strcmp(filename, "..") == 0) {
		unsigned long long pinode;

		pinode = get_handle2inode(parent_hdl->handle);
		if (pinode == GPFS_ROOT_INODE) {
			LogEvent(COMPONENT_FSAL,
				 "Lookup of DOTDOT failed in ROOT dir");
			*fh = *parent_hdl->handle;
			status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		} else {
			LogEvent(COMPONENT_FSAL,
				 "Lookup of DOTDOT failed in dirinode: %llu",
				 pinode);
		}
	}

	if (FSAL_IS_ERROR(status))
		return status;

	/* Sometimes GPFS sends us the same object as its parent with
	 * lookup of DOTDOT. This is incorrect and also results in ABBA
	 * deadlock with content_lock and attr_lock (readdirplus holds
	 * content_lock on the directory and then attr_lock on the
	 * direntry (which happens to be the same object for DOTDOT
	 * direntry with this bug). Other requests hold attr_lock
	 * followed by content_lock.
	 *
	 * If we detect this error, send DELAY error and hope it goes
	 * away on the retry!
	 */
	if (strcmp(filename, "..") == 0) {
		struct gpfs_file_handle *gfh;
		unsigned long long inode;

		gfh = parent_hdl->handle;
		inode = get_handle2inode(gfh);
		if (inode != GPFS_ROOT_INODE &&
		    gfh->handle_size == fh->handle_size &&
		    memcmp(gfh, fh, gfh->handle_size) == 0) {
			LogCrit(COMPONENT_FSAL,
				"DOTDOT error, inode: %llu", inode);
			return fsalstat(ERR_FSAL_DELAY, 0);
		}
	}

	/* In order to check XDEV, we need to get the fsid from the handle.
	 * We need to do this before getting attributes in order to have the
	 * correct gpfs_fs to pass to GPFSFSAL_getattrs. We also return
	 * the correct fs to the caller.
	 */
	gpfs_extract_fsid(fh, &fsid);

	if (fsid.major != parent_hdl->obj_handle.fsid.major) {
		/* XDEV */
		*new_fs = lookup_fsid(&fsid, GPFS_FSID_TYPE);
		if (*new_fs == NULL) {
			LogDebug(COMPONENT_FSAL,
				 "Lookup of %s crosses filesystem boundary to unknown file system fsid=0x%016"
				 PRIx64".0x%016"PRIx64,
				 filename, fsid.major, fsid.minor);
			return fsalstat(ERR_FSAL_XDEV, EXDEV);
		}

		if ((*new_fs)->fsal != parent->fsal) {
			LogDebug(COMPONENT_FSAL,
				 "Lookup of %s crosses filesystem boundary to file system %s into FSAL %s",
				 filename, (*new_fs)->path,
				 (*new_fs)->fsal != NULL
					? (*new_fs)->fsal->name
					: "(none)");
			return fsalstat(ERR_FSAL_XDEV, EXDEV);
		} else {
			LogDebug(COMPONENT_FSAL,
				 "Lookup of %s crosses filesystem boundary to file system %s",
				 filename, (*new_fs)->path);
		}
		gpfs_fs = (*new_fs)->private_data;
	}

	/* get object attributes */
	status = GPFSFSAL_getattrs(op_ctx->fsal_export, gpfs_fs, fh, fsal_attr);

	/* lookup complete ! */
	return status;
}
