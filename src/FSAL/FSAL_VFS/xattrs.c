/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* xattrs.c
 * VFS object (file|dir) handle object extended attributes
 */

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"

/* extended attributes management */
fsal_status_t vfs_getextattrs(struct fsal_obj_handle *obj_hdl,
			      fsal_extattrib_list_t * object_attributes)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				 unsigned int cookie,
				 fsal_xattrent_t * xattrs_tab,
				 unsigned int xattrs_tabsize,
				 unsigned int *p_nb_returned,
				 int *end_of_list)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					const char *xattr_name,
					unsigned int *pxattr_id)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   caddr_t buffer_addr,
					   size_t buffer_size,
					   size_t * p_output_size)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size,
					 size_t *p_output_size)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				   const char *xattr_name,
				   caddr_t buffer_addr,
				   size_t buffer_size,
				   int create)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				   unsigned int xattr_id,
				   fsal_attrib_list_t * p_attrs)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
				       unsigned int xattr_id)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t vfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					 const char *xattr_name)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0);
}

