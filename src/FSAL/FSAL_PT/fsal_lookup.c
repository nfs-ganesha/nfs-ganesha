/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_lookup.c
 * Description: FSAL lookup operations implementation
 * Author:      FSI IPC dev team
 * ----------------------------------------------------------------------------
 */
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "pt_ganesha.h"
#include "pt_methods.h"

/**
 * FSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *
 */
fsal_status_t PTFSAL_lookup(const struct req_op_context *p_context,
			    struct fsal_obj_handle *parent,
			    const char *p_filename,
			    struct attrlist *p_object_attr,
			    ptfsal_handle_t *fh)
{
	fsal_status_t status;
	int parent_fd;
	struct attrlist *parent_dir_attrs;
	fsi_stat_struct buffstat;
	int rc;
	struct pt_fsal_obj_handle *parent_hdl;

	FSI_TRACE(FSI_DEBUG, "Begin##################################\n");
	if (p_filename != NULL)
		FSI_TRACE(FSI_DEBUG, "FSI - fsal_lookup file [%s]\n",
			  p_filename);

	if (parent != NULL)
		FSI_TRACE(FSI_DEBUG, "FSI - fsal_lookup parent dir\n");

	if (!parent || !p_filename)
		return fsalstat(ERR_FSAL_FAULT, 0);

	parent_hdl =
	    container_of(parent, struct pt_fsal_obj_handle, obj_handle);

	parent_dir_attrs = &parent_hdl->obj_handle.attributes;

	/* get directory metadata */
	parent_dir_attrs->mask = p_context->fsal_export->ops->
			fs_supported_attrs(p_context->fsal_export);
	status =
	    fsal_internal_handle2fd_at(p_context, parent_hdl, &parent_fd,
				       O_RDONLY);

	if (FSAL_IS_ERROR(status))
		return status;

	FSI_TRACE(FSI_DEBUG, "FSI - lookup parent directory type = %d\n",
		  parent_dir_attrs->type);

	/* Be careful about junction crossing, symlinks, hardlinks,... */
	switch (parent_dir_attrs->type) {
	case DIRECTORY:
		/* OK */
		break;

	case REGULAR_FILE:
	case SYMBOLIC_LINK:
		/* not a directory */
		pt_close(parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	/* get file handle, it it exists */
	/* This might be a race, but it's the best we can currently do */
	rc = ptfsal_stat_by_parent_name(p_context, parent_hdl, p_filename,
					&buffstat);
	if (rc < 0) {
		ptfsal_closedir_fd(p_context, p_context->fsal_export,
				   parent_fd);
		return fsalstat(ERR_FSAL_NOENT, errno);
	}
	memset(fh->data.handle.f_handle, 0, sizeof(fh->data.handle.f_handle));
	memcpy(&fh->data.handle.f_handle, &buffstat.st_persistentHandle.handle,
	       FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
	fh->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
	fh->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
	fh->data.handle.handle_version = OPENHANDLE_VERSION;
	fh->data.handle.handle_type = posix2fsal_type(buffstat.st_mode);

	/* get object attributes */
	if (p_object_attr) {
		p_object_attr->mask = p_context->fsal_export->ops->
				fs_supported_attrs(p_context->fsal_export);
		status = PTFSAL_getattrs(p_context->fsal_export, p_context,
					 fh, p_object_attr);
		if (FSAL_IS_ERROR(status)) {
			FSAL_CLEAR_MASK(p_object_attr->mask);
			FSAL_SET_MASK(p_object_attr->mask, ATTR_RDATTR_ERR);
		}
	}

	ptfsal_closedir_fd(p_context, p_context->fsal_export, parent_fd);

	FSI_TRACE(FSI_DEBUG, "End##################################\n");
	/* lookup complete ! */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
