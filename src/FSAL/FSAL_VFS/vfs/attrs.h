/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohortFS LLC, 2015
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

/* attrs.h
 * VFS debug attribute handling
 */

#ifndef __VFS_ATTRS_H
#define __VFS_ATTRS_H

#include "../vfs_methods.h"

void vfs_acl_init(void);
fsal_status_t vfs_sub_getattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrs);
fsal_status_t vfs_sub_setattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrib_set);

#endif /* __VFS_ATTRS_H */
