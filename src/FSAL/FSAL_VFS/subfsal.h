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

void vfs_sub_init_handle_ops(struct vfs_fsal_export *myself,
			      struct fsal_obj_ops *ops);

void vfs_sub_init_export_ops(struct vfs_fsal_export *myself,
			      const char *export_path);

int vfs_sub_init_export(struct vfs_fsal_export *myself);

#endif /* SUBFSAL_H */
