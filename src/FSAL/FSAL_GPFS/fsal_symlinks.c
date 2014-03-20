/*
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

/**
 * \file    fsal_symlinks.c
 * \date    $Date: 2005/07/29 09:39:04 $
 * \brief   symlinks operations.
 *
 */
#include "config.h"

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"
#include <string.h>
#include <unistd.h>

/**
 * FSAL_readlink:
 * Read the content of a symbolic link.
 *
 * \param dir_hdl (input):
 *        Handle of the link to be read.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_link_content (output):
 *        Pointer to an fsal path structure where
 *        the link content is to be stored..
 * \param link_len (input/output):
 *        In pointer to len of content buff.
 .        Out actual len of content.
 * \param link_attributes (optionnal input/output):
 *        The post operation attributes of the symlink link.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t GPFSFSAL_readlink(struct fsal_obj_handle *dir_hdl,	/* IN */
				const struct req_op_context *p_context,	/* IN */
				char *p_link_content,	/* OUT */
				size_t *link_len,	/* IN/OUT */
				struct attrlist *p_link_attributes)
{				/* IN/OUT */

/*   int errsv; */
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *gpfs_hdl;
	struct gpfs_filesystem *gpfs_fs;

	/* sanity checks.
	 * note : link_attributes is optional.
	 */
	if (!dir_hdl || !p_context || !p_link_content)
		return fsalstat(ERR_FSAL_FAULT, 0);

	gpfs_hdl =
	    container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = dir_hdl->fs->private;

	/* Read the link on the filesystem */
	status = fsal_readlink_by_handle(gpfs_fs->root_fd, gpfs_hdl->handle,
					 p_link_content, link_len);

	if (FSAL_IS_ERROR(status))
		return status;

	/* retrieves object attributes, if asked */

	if (p_link_attributes) {

		status = GPFSFSAL_getattrs(p_context->fsal_export, gpfs_fs,
					   p_context, gpfs_hdl->handle,
					   p_link_attributes);

		/* On error, we set a flag in the returned attributes */

		if (FSAL_IS_ERROR(status)) {
			FSAL_CLEAR_MASK(p_link_attributes->mask);
			FSAL_SET_MASK(p_link_attributes->mask, ATTR_RDATTR_ERR);
		}
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_symlink:
 * Create a symbolic link.
 *
 * \param dir_hdl (input):
 *        Handle of the parent directory where the link is to be created.
 * \param p_linkname (input):
 *        Name of the link to be created.
 * \param p_linkcontent (input):
 *        Content of the link to be created.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (ignored input):
 *        Mode of the link to be created.
 *        It has no sense in HPSS nor UNIX filesystems.
 * \param p_link_handle (output):
 *        Pointer to the handle of the created symlink.
 * \param link_attributes (optionnal input/output):
 *        Attributes of the newly created symlink.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t GPFSFSAL_symlink(struct fsal_obj_handle *dir_hdl,	/* IN */
			       const char *p_linkname,	/* IN */
			       const char *p_linkcontent,	/* IN */
			       const struct req_op_context *p_context,	/* IN */
			       uint32_t accessmode,	/* IN (ignored) */
			       struct gpfs_file_handle *p_link_handle, /* OUT */
			       struct attrlist *p_link_attributes)
{				/* IN/OUT */

	int rc, errsv;
	fsal_status_t status;
	int fd;
	struct gpfs_fsal_obj_handle *gpfs_hdl;
	struct gpfs_filesystem *gpfs_fs;

	/* sanity checks.
	 * note : link_attributes is optional.
	 */
	if (!dir_hdl || !p_context || !p_link_handle || !p_linkname
	    || !p_linkcontent)
		return fsalstat(ERR_FSAL_FAULT, 0);

	gpfs_hdl =
	    container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	gpfs_fs = dir_hdl->fs->private;

	/* Tests if symlinking is allowed by configuration. */

	if (!p_context->fsal_export->ops->
	    fs_supports(p_context->fsal_export,
			fso_symlink_support))
		return fsalstat(ERR_FSAL_NOTSUPP, 0);

	status = fsal_internal_handle2fd(gpfs_fs->root_fd, gpfs_hdl->handle,
					 &fd, O_RDONLY | O_DIRECTORY, 0);

	if (FSAL_IS_ERROR(status))
		return status;

	/* build symlink path */

	/* create the symlink on the filesystem using the credentials
	 * for proper ownership assignment.
	 */

	fsal_set_credentials(p_context->creds);

	rc = symlinkat(p_linkcontent, fd, p_linkname);
	errsv = errno;

	fsal_restore_ganesha_credentials();

	if (rc) {
		close(fd);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	/* now get the associated handle, while there is a race, there is
	   also a race lower down  */
	status = fsal_internal_get_handle_at(fd, p_linkname, p_link_handle);

	if (FSAL_IS_ERROR(status)) {
		close(fd);
		return status;
	}

	/* get attributes if asked */

	if (p_link_attributes) {

		status = GPFSFSAL_getattrs(p_context->fsal_export, gpfs_fs,
					   p_context, p_link_handle,
					   p_link_attributes);

		/* On error, we set a flag in the returned attributes */

		if (FSAL_IS_ERROR(status)) {
			FSAL_CLEAR_MASK(p_link_attributes->mask);
			FSAL_SET_MASK(p_link_attributes->mask, ATTR_RDATTR_ERR);
		}

	}

	/* OK */
	close(fd);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
