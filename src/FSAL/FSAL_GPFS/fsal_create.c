// SPDX-License-Identifier: LGPL-3.0-or-later
/** @file fsal_create
 *  @brief GPFS FSAL Filesystem objects creation functions
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
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 */

#include "config.h"
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"
#include <unistd.h>
#include <fcntl.h>
#include <fsal_api.h>
#include "FSAL/access_check.h"
#include "FSAL/fsal_localfs.h"

/**
 *  @brief Create a regular file.
 *
 *  @param dir_hdl Handle of parent directory where the file is to be created.
 *  @param filename Pointer to the name of the file to be created.
 *  @param accessmode Mode for the file to be created.
 *  @param gpfs_fh Pointer to the handle of the created file.
 *  @param fsal_attr Attributes of the created file.
 *  @return ERR_FSAL_NO_ERROR on success, otherwise error
 *
 */
fsal_status_t
GPFSFSAL_create(struct fsal_obj_handle *dir_hdl, const char *filename,
		uint32_t accessmode,
		struct gpfs_file_handle *gpfs_fh,
		struct fsal_attrlist *fsal_attr)
{
	fsal_status_t status;
	mode_t unix_mode;

	/* note : fsal_attr is optional. */
	if (!dir_hdl || !op_ctx || !gpfs_fh || !filename)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* convert fsal mode to unix mode. */
	unix_mode = fsal2unix_mode(accessmode);

	/* Apply umask */
	unix_mode = unix_mode &
		~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	LogFullDebug(COMPONENT_FSAL, "Creation mode: 0%o", accessmode);

	/* call to filesystem */

	fsal_set_credentials(&op_ctx->creds);
	status = fsal_internal_create(dir_hdl, filename, unix_mode | S_IFREG,
				      0, gpfs_fh, NULL);
	fsal_restore_ganesha_credentials();
	if (FSAL_IS_ERROR(status))
		return status;

	/* retrieve file attributes */
	return GPFSFSAL_getattrs(op_ctx->fsal_export, dir_hdl->fs->private_data,
				 gpfs_fh, fsal_attr);
}

fsal_status_t
GPFSFSAL_create2(struct fsal_obj_handle *dir_hdl, const char *filename,
		mode_t unix_mode,
		struct gpfs_file_handle *gpfs_fh, int posix_flags,
		struct fsal_attrlist *fsal_attr)
{
	fsal_status_t status;

	/* note : fsal_attr is optional. */
	if (!dir_hdl || !op_ctx || !gpfs_fh || !filename)
		return fsalstat(ERR_FSAL_FAULT, 0);

	LogFullDebug(COMPONENT_FSAL, "Creation mode: 0%o", unix_mode);

	/* call to filesystem */

	fsal_set_credentials(&op_ctx->creds);
	status = fsal_internal_create(dir_hdl, filename, unix_mode | S_IFREG,
				      posix_flags, gpfs_fh, NULL);
	fsal_restore_ganesha_credentials();

	if (!FSAL_IS_ERROR(status) && fsal_attr != NULL) {
		/* retrieve file attributes */
		status = GPFSFSAL_getattrs(op_ctx->fsal_export,
					   dir_hdl->fs->private_data,
					   gpfs_fh, fsal_attr);
	}

	return status;
}

/**
 *  @brief Create a directory.
 *
 *  @param dir_hdl Handle of the parent directory
 *  @param dir_name Pointer to the name of the directory to be created.
 *  @param accessmode Mode for the directory to be created.
 *  @param gpfs_fh Pointer to the handle of the created directory.
 *  @param fsal_attr Attributes of the created directory.
 *  @return ERR_FSAL_NO_ERROR on success, error otherwise
 *
 */
fsal_status_t
GPFSFSAL_mkdir(struct fsal_obj_handle *dir_hdl, const char *dir_name,
	       uint32_t accessmode,
	       struct gpfs_file_handle *gpfs_fh, struct fsal_attrlist *obj_attr)
{
	mode_t unix_mode;
	fsal_status_t status;

	/* note : obj_attr is optional. */
	if (!dir_hdl || !op_ctx || !gpfs_fh || !dir_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* convert FSAL mode to unix mode. */
	unix_mode = fsal2unix_mode(accessmode);

	/* Apply umask */
	unix_mode = unix_mode &
		~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	/* build new entry path */

	/* creates the directory and get its handle */

	fsal_set_credentials(&op_ctx->creds);
	status = fsal_internal_create(dir_hdl, dir_name, unix_mode | S_IFDIR,
				      0, gpfs_fh, NULL);
	fsal_restore_ganesha_credentials();

	if (FSAL_IS_ERROR(status))
		return status;

	/* retrieve file attributes */
	return GPFSFSAL_getattrs(op_ctx->fsal_export,
				dir_hdl->fs->private_data,
				gpfs_fh, obj_attr);
}

/**
 *  @brief Create a hardlink.
 *
 *  @param dir_hdl Handle of the target object.
 *  @param gpfs_fh Pointer to the dire handle where hardlink is to be created.
 *  @param linkname Pointer to the name of the hardlink to be created.
 *  @return ERR_FSAL_NO_ERROR on success, error otherwise
 *
 */
fsal_status_t
GPFSFSAL_link(struct fsal_obj_handle *dir_hdl, struct gpfs_file_handle *gpfs_fh,
	      const char *linkname)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *dest_dir;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	dest_dir =
		container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	/* Tests if hardlinking is allowed by configuration. */

	if (!op_ctx->fsal_export->exp_ops.fs_supports(op_ctx->fsal_export,
						      fso_link_support))
		return fsalstat(ERR_FSAL_NOTSUPP, 0);

	/* Create the link on the filesystem */

	fsal_set_credentials(&op_ctx->creds);
	status = fsal_internal_link_fh(export_fd, gpfs_fh,
				       dest_dir->handle, linkname);

	fsal_restore_ganesha_credentials();

	return status;
}

/**
 *  @brief Create a special object in the filesystem.
 *
 *  @param dir_hdl Handle of the parent dir where the file is to be created.
 *  @param node_name Pointer to the name of the file to be created.
 *  @param accessmode Mode for the file to be created.
 *  @param node_type Type of file to create.
 *  @param dev Device id of file to create.
 *  @param gpfs_fh Pointer to the handle of the created file.
 *  @param fsal_attr Attributes of the created file.
 *  @return ERR_FSAL_NO_ERROR on success, error otherwise
 *
 */
fsal_status_t
GPFSFSAL_mknode(struct fsal_obj_handle *dir_hdl, const char *node_name,
		uint32_t accessmode,
		mode_t nodetype, fsal_dev_t *dev,
		struct gpfs_file_handle *gpfs_fh,
		struct fsal_attrlist *fsal_attr)
{
	fsal_status_t status;
	mode_t unix_mode = 0;
	dev_t unix_dev = 0;

	/* note : fsal_attr is optional. */
	if (!dir_hdl || !op_ctx || !node_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	unix_mode = fsal2unix_mode(accessmode);

	/* Apply umask */
	unix_mode = unix_mode &
		~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	switch (nodetype) {
	case BLOCK_FILE:
		if (!dev)
			return fsalstat(ERR_FSAL_FAULT, 0);
		unix_mode |= S_IFBLK;
		unix_dev = (dev->major << 20) | (dev->minor & 0xFFFFF);
		break;

	case CHARACTER_FILE:
		if (!dev)
			return fsalstat(ERR_FSAL_FAULT, 0);
		unix_mode |= S_IFCHR;
		unix_dev = (dev->major << 20) | (dev->minor & 0xFFFFF);
		break;

	case SOCKET_FILE:
		unix_mode |= S_IFSOCK;
		break;

	case FIFO_FILE:
		unix_mode |= S_IFIFO;
		break;

	default:
		LogMajor(COMPONENT_FSAL, "Invalid node type in FSAL_mknode: %d",
			 nodetype);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	fsal_set_credentials(&op_ctx->creds);
	status = fsal_internal_mknode(dir_hdl, node_name, unix_mode, unix_dev,
				      gpfs_fh, NULL);

	fsal_restore_ganesha_credentials();

	if (FSAL_IS_ERROR(status))
		return status;

	/* Fills the attributes */
	return GPFSFSAL_getattrs(op_ctx->fsal_export,
				dir_hdl->fs->private_data,
				gpfs_fh, fsal_attr);
}
