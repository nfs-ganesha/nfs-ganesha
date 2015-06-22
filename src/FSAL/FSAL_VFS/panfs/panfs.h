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

/* panfs.h
 * PanFS FSAL API
 */

#ifndef PANFS_H
#define PANFS_H

#include "../vfs_methods.h"

struct panfs_fsal_export {
	struct vfs_fsal_export vfs_export;
	bool pnfs_enabled;
	void *pnfs_data;
};

#define EXPORT_PANFS_FROM_VFS(vfs) \
	container_of((vfs), struct panfs_fsal_export, vfs_export)

#define EXPORT_PANFS_FROM_FSAL(fsal) \
	EXPORT_PANFS_FROM_VFS(EXPORT_VFS_FROM_FSAL((fsal)))

struct panfs_fsal_obj_handle {
	struct vfs_fsal_obj_handle vfs_obj_handle;
	struct vfs_subfsal_obj_ops panfs_ops;
};

#define OBJ_PANFS_FROM_VFS(vfs) \
	container_of((vfs), struct panfs_fsal_obj_handle, vfs_obj_handle)

#define OBJ_PANFS_FROM_FSAL(fsal) \
	OBJ_PANFS_FROM_VFS(OBJ_VFS_FROM_FSAL((fsal)))

void panfs_handle_ops_init(struct panfs_fsal_obj_handle *panfs_hdl);

#endif /* PANFS_H */
