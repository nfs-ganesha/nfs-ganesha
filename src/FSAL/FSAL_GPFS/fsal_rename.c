/**
 * @file    fsal_rename.c
 * @date    $Date: 2006/01/24 13:45:37 $
 * @brief   object renaming/moving function.
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
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"

/**
 *  @brief Change name and/or parent dir of a filesystem object.
 *
 *  @param old_hdl Source parent directory of the object is to be moved/renamed.
 *  @param old_name Current name of the object to be moved/renamed.
 *  @param new_hdl Target parent directory for the object.
 *  @param new_name New name for the object.
 *  @param op_ctx Authentication context for the operation (user,...).
 *
 *  @return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t
GPFSFSAL_rename(struct fsal_obj_handle *old_hdl, const char *old_name,
		struct fsal_obj_handle *new_hdl, const char *new_name,
		const struct req_op_context *op_ctx)
{

	fsal_status_t status;
	struct stat buffstat;
	struct gpfs_fsal_obj_handle *old_gpfs_hdl, *new_gpfs_hdl;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	old_gpfs_hdl =
	    container_of(old_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	new_gpfs_hdl =
	    container_of(new_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	/* build file paths */
	status = fsal_internal_stat_name(export_fd, old_gpfs_hdl->handle,
					 old_name, &buffstat);
	if (FSAL_IS_ERROR(status))
		return status;

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
	status = fsal_internal_rename_fh(export_fd, old_gpfs_hdl->handle,
					 new_gpfs_hdl->handle, old_name,
					 new_name);

	if (FSAL_IS_ERROR(status))
		return status;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
