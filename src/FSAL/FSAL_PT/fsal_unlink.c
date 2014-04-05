/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_unlink.c
 * Description: FSAL unlink operations implementation
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

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <unistd.h>

#include "pt_ganesha.h"
#include "pt_methods.h"

/**
 * FSAL_unlink:
 * Remove a filesystem object .
 *
 * \param dir_hdl (input):
 *        Handle of the parent directory of the object to be deleted.
 * \param p_object_name (input):
 *        Name of the object to be removed.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_parentdir_attributes (optionnal input/output):
 *        Post operation attributes of the parent directory.
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
fsal_status_t PTFSAL_unlink(struct fsal_obj_handle *dir_hdl,	/* IN */
			    const char *p_object_name,	/* IN */
			    const struct req_op_context *p_context,	/* IN */
			    struct attrlist *p_parent_attributes)
{				/* IN/OUT */

	fsal_status_t status;
	int rc, errsv;
	fsi_stat_struct buffstat;
	struct pt_fsal_obj_handle *pt_hdl;

	/* sanity checks. */
	if (!dir_hdl || !p_context || !p_object_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	pt_hdl = container_of(dir_hdl, struct pt_fsal_obj_handle, obj_handle);

	FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL_unlink [%s] entry\n", p_object_name);

	/* build the child path */

	FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL_unlink [%s] build child path\n",
		  p_object_name);

	/* get file metadata */
	rc = ptfsal_stat_by_parent_name(p_context, pt_hdl, p_object_name,
					&buffstat);
	if (rc) {
		FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL_unlink stat [%s] rc %d\n",
			  p_object_name, rc);
		return fsalstat(posix2fsal_error(errno), errno);
	}

  /******************************
   * DELETE FROM THE FILESYSTEM *
   ******************************/

	/* If the object to delete is a directory, use 'rmdir' to delete
	 * the object else use 'unlink'
	 */
	if (S_ISDIR(buffstat.st_mode)) {
		FSI_TRACE(FSI_DEBUG, "Deleting directory %s", p_object_name);
		rc = ptfsal_rmdir(p_context, pt_hdl, p_object_name);

	} else {
		FSI_TRACE(FSI_DEBUG, "Deleting file %s", p_object_name);
		rc = ptfsal_unlink(p_context, pt_hdl, p_object_name);
	}
	if (rc) {
		errsv = errno;
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

  /***********************
   * FILL THE ATTRIBUTES *
   ***********************/

	if (p_parent_attributes) {
		status = PTFSAL_getattrs(p_context->fsal_export, p_context,
					pt_hdl->handle, p_parent_attributes);
		if (FSAL_IS_ERROR(status)) {
			FSAL_CLEAR_MASK(p_parent_attributes->mask);
			FSAL_SET_MASK(p_parent_attributes->mask,
				      ATTR_RDATTR_ERR);
		}
	}
	/* OK */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
