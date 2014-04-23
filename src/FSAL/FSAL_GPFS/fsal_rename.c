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
 *
 * \file    fsal_rename.c
 * \date    $Date: 2006/01/24 13:45:37 $
 * \brief   object renaming/moving function.
 *
 */
#include "config.h"

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"

/**
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_hdl (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_hdl (input):
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

fsal_status_t GPFSFSAL_rename(struct fsal_obj_handle *old_hdl,	/* IN */
			      const char *p_old_name,	/* IN */
			      struct fsal_obj_handle *new_hdl,	/* IN */
			      const char *p_new_name,	/* IN */
			      const struct req_op_context *p_context)
{				/* IN */

	fsal_status_t status;
	struct stat buffstat;
	struct gpfs_fsal_obj_handle *old_gpfs_hdl, *new_gpfs_hdl;
	struct gpfs_filesystem *gpfs_fs;

	/* sanity checks.
	 * note : src/tgt_dir_attributes are optional.
	 */
	if (!old_hdl || !new_hdl || !p_old_name || !p_new_name || !p_context)
		return fsalstat(ERR_FSAL_FAULT, 0);

	old_gpfs_hdl =
	    container_of(old_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	new_gpfs_hdl =
	    container_of(new_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = old_hdl->fs->private;

	/* build file paths */
	status = fsal_internal_stat_name(gpfs_fs->root_fd, old_gpfs_hdl->handle,
					 p_old_name, &buffstat);
	if (FSAL_IS_ERROR(status))
		return status;

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
	fsal_set_credentials(p_context->creds);

	status = fsal_internal_rename_fh(gpfs_fs->root_fd, old_gpfs_hdl->handle,
					 new_gpfs_hdl->handle, p_old_name,
					 p_new_name);

	fsal_restore_ganesha_credentials();

	if (FSAL_IS_ERROR(status))
		return status;

	/* OK */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
