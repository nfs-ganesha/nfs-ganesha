/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_rename.c
 * Description: Common FSI IPC Client and Server definitions
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
#include "pt_ganesha.h"
#include "pt_methods.h"

/**
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_hdle (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_hdle (input):
 *        Target parent directory for the object.
 * \param p_new_name (input):
 *        Pointer to the new name for the object.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t PTFSAL_rename(struct fsal_obj_handle *old_hdl,	/* IN */
			    const char *p_old_name,	/* IN */
			    struct fsal_obj_handle *new_hdl,	/* IN */
			    const char *p_new_name,	/* IN */
			    const struct req_op_context *p_context)
{				/* IN */

	int rc, errsv;
	struct stat st;
	struct pt_fsal_obj_handle *old_pt_hdl, *new_pt_hdl;
	int stat_rc;

	FSI_TRACE(FSI_DEBUG, "FSI Rename--------------\n");

	/* sanity checks.
	 * note : src/tgt_dir_attributes are optional.
	 */
	if (!old_hdl || !new_hdl || !p_old_name || !p_new_name || !p_context)
		return fsalstat(ERR_FSAL_FAULT, 0);

	old_pt_hdl =
	    container_of(old_hdl, struct pt_fsal_obj_handle, obj_handle);
	new_pt_hdl =
	    container_of(new_hdl, struct pt_fsal_obj_handle, obj_handle);

	/* build file paths */
	memset(&st, 0, sizeof(st));
	stat_rc =
	    ptfsal_stat_by_handle(p_context, p_context->fsal_export,
				  old_pt_hdl->handle, &st);

	errsv = errno;
	if (stat_rc)
		return fsalstat(posix2fsal_error(errsv), errsv);

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
	rc = ptfsal_rename(p_context, old_pt_hdl, p_old_name, new_pt_hdl,
			   p_new_name);
	errsv = errno;

	if (rc)
		return fsalstat(posix2fsal_error(errsv), errsv);

	/* OK */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
