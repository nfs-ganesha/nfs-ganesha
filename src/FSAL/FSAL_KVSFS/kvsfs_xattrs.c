// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CEA, 2020
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

/* xattrs.c
 * KVSFS object (file|dir) handle object extended attributes
 */

#include "config.h"

#include <assert.h>

#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include <ctype.h>
#include "gsh_list.h"
#include "kvsfs_fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include <stdbool.h>

typedef int (*xattr_getfunc_t) (struct fsal_obj_handle *, /* object handle */
				caddr_t,	/* output buff */
				size_t,	/* output buff size */
				size_t *,	/* output size */
				void *arg);	/* optional argument */

typedef int (*xattr_setfunc_t) (struct fsal_obj_handle *, /* object handle */
				caddr_t,	/* input buff */
				size_t,	/* input size */
				int,	/* creation flag */
				void *arg);	/* optional argument */

typedef struct fsal_xattr_def__ {
	char xattr_name[MAXNAMLEN + 1];
	xattr_getfunc_t get_func;
	xattr_setfunc_t set_func;
	int flags;
	void *arg;
} fsal_xattr_def_t;

/*
 * DEFINE GET/SET FUNCTIONS
 */

int print_vfshandle(struct fsal_obj_handle *obj_hdl, caddr_t buffer_addr,
		    size_t buffer_size, size_t *p_output_size, void *arg)
{
	*p_output_size = snprintf(buffer_addr, buffer_size,
				  "(not yet implemented)");

	return 0;
}				/* print_fid */

fsal_status_t kvsfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				  unsigned int argcookie,
				  fsal_xattrent_t *xattrs_tab,
				  unsigned int xattrs_tabsize,
				  unsigned int *p_nb_returned, int *end_of_list)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					 const char *xattr_name,
					 unsigned int *pxattr_id)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id,
					  caddr_t buffer_addr,
					  size_t buffer_size,
					  size_t *p_output_size)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				    const char *xattr_name, caddr_t buffer_addr,
				    size_t buffer_size, int create)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id,
					  caddr_t buffer_addr,
					  size_t buffer_size)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int xattr_id,
				    struct fsal_attrlist *p_attrs)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					unsigned int xattr_id)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					  const char *xattr_name)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
