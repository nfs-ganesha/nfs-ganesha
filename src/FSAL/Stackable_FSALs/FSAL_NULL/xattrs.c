/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/* xattrs.c
 * NULL object (file|dir) handle object extended attributes
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include "os/xattr.h"
#include "gsh_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "nullfs_methods.h"

fsal_status_t nullfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int argcookie,
				    fsal_xattrent_t *xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
		     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->list_ext_attrs(
		handle->sub_handle, argcookie,
		xattrs_tab, xattrs_tabsize,
		p_nb_returned, end_of_list);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   unsigned int *pxattr_id)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->getextattr_id_by_name(
				handle->sub_handle, xattr_name, pxattr_id);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    void *buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
	handle->sub_handle->obj_ops->getextattr_value_by_id(
				handle->sub_handle,
				xattr_id, buffer_addr,
				buffer_size,
				p_output_size);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const char *xattr_name,
					      void *buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->getextattr_value_by_name(
				handle->sub_handle,
				xattr_name,
				buffer_addr,
				buffer_size,
				p_output_size);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      void *buffer_addr, size_t buffer_size,
				      int create)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status = handle->sub_handle->obj_ops->setextattr_value(
		handle->sub_handle, xattr_name,
		buffer_addr, buffer_size,
		create);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    void *buffer_addr,
					    size_t buffer_size)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->setextattr_value_by_id(
				handle->sub_handle,
				xattr_id, buffer_addr,
				buffer_size);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->remove_extattr_by_id(
						handle->sub_handle, xattr_id);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->remove_extattr_by_name(
				handle->sub_handle, xattr_name);
	op_ctx->fsal_export = &export->export;

	return status;
}
