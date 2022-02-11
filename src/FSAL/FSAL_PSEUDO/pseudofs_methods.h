/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* PSEUDOFS methods for handles
 */

#include "avltree.h"
#include "gsh_list.h"

#define PSEUDO_SUPPORTED_ATTRS ((const attrmask_t) (ATTRS_POSIX))

struct pseudo_fsal_obj_handle;

struct pseudo_fsal_module {
	struct fsal_module module;
	struct fsal_obj_ops handle_ops;
};

extern struct pseudo_fsal_module PSEUDOFS;

/*
 * PSEUDOFS internal export
 */
struct pseudofs_fsal_export {
	struct fsal_export export;
	char *export_path;
	struct pseudo_fsal_obj_handle *root_handle;
};

fsal_status_t pseudofs_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out);

fsal_status_t pseudofs_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle,
				   struct fsal_attrlist *attrs_out);

/**
 * @brief PSEUDOFS internal object handle
 *
 * The handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 */

struct pseudo_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct fsal_attrlist attributes;
	char *handle;
	struct pseudo_fsal_obj_handle *parent;
	struct avltree avl_name;
	struct avltree avl_index;
	struct avltree_node avl_n;
	struct avltree_node avl_i;
	uint32_t index; /* index in parent */
	uint32_t next_i; /* next child index */
	uint32_t numlinks;
	char *name;
	bool inavl;
};

static inline bool pseudofs_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

void pseudofs_handle_ops_init(struct fsal_obj_ops *ops);

/* Internal PSEUDOFS method linkage to export object
 */

fsal_status_t pseudofs_create_export(struct fsal_module *fsal_hdl,
				     void *parse_node,
				     struct config_error_type *err_type,
				     const struct fsal_up_vector *up_ops);
