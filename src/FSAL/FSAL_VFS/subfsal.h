/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

/* subfsal.h
 * VFS Sub-FSAL API
 */

#ifndef SUBFSAL_H
#define SUBFSAL_H

#include "config_parsing.h"

/* Export parameters */
extern struct config_block *vfs_sub_export_param;


/** Routines for sub-FSALS
 */

void vfs_sub_fini(struct vfs_fsal_export *myself);

void vfs_sub_init_export_ops(struct vfs_fsal_export *myself,
			      const char *export_path);

int vfs_sub_init_export(struct vfs_fsal_export *myself);

/**
 * @brief Allocate the SubFSAL object handle
 *
 * Allocate the SubFSAL object handle.  It must be large enough to hold a
 * vfs_file_handle_t after the end of the normal handle, and the @a handle field
 * of the vfs_fsal_obj_handle must point to the correct location for the
 * vfs_file_handle_t.
 *
 * @return VFS object handle on success, NULL on failure
 */
struct vfs_fsal_obj_handle *vfs_sub_alloc_handle(void);

int vfs_sub_init_handle(struct vfs_fsal_export *myself,
		struct vfs_fsal_obj_handle *hdl,
		const char *path);
#endif /* SUBFSAL_H */
