/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohortFS LLC, 2014
 * Author: Daniel Gryniewicz dang@cohortfs.com
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

/* handle.c
 * PanFS (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include "fsal_convert.h"
#include "../vfs_methods.h"
#include "panfs.h"
#include "attrs.h"

static fsal_status_t panfs_getattrs(struct vfs_fsal_obj_handle *vfs_hdl,
		int fd, attrmask_t request_mask)
{
	struct panfs_fsal_obj_handle *panfs_hdl = OBJ_PANFS_FROM_VFS(vfs_hdl);
	struct attrlist *attrib = &vfs_hdl->attributes;
	fsal_status_t st;

	/*if (FSAL_TEST_MASK(request_mask, ATTR_ACL)) {*/
		FSAL_SET_MASK(attrib->mask, ATTR_ACL);
		st = PanFS_getattrs(panfs_hdl, fd, attrib);
		if (FSAL_IS_ERROR(st))
			return st;
	/*}*/

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t panfs_setattrs(struct vfs_fsal_obj_handle *vfs_hdl,
		int fd, attrmask_t request_mask,
		struct attrlist *attrib_set)
{
	struct panfs_fsal_obj_handle *panfs_hdl = OBJ_PANFS_FROM_VFS(vfs_hdl);
	fsal_status_t st;

	if (FSAL_TEST_MASK(request_mask, ATTR_ACL) && attrib_set->acl) {
		st = PanFS_setattrs(panfs_hdl, fd, attrib_set);
		if (FSAL_IS_ERROR(st))
			return st;
		FSAL_SET_MASK(attrib_set->valid_mask, ATTR_ACL);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void panfs_handle_ops_init(struct panfs_fsal_obj_handle *panfs_hdl)
{
	panfs_hdl->vfs_obj_handle.sub_ops = &panfs_hdl->panfs_ops;

	panfs_hdl->panfs_ops.getattrs = panfs_getattrs;
	panfs_hdl->panfs_ops.setattrs = panfs_setattrs;
}
