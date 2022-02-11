/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright 2018 VMware, Inc.
 * Contributor: Sriram Patil <sriramp@vmware.com>
 *
 * --------------------------
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 *
 */

#ifndef _NFS4_FS_LOCATIONS_H
#define _NFS4_FS_LOCATIONS_H

#include "fsal_types.h"

void nfs4_fs_locations_free(fsal_fs_locations_t *);
void nfs4_fs_locations_get_ref(fsal_fs_locations_t *);
void nfs4_fs_locations_release(fsal_fs_locations_t *);

fsal_fs_locations_t *nfs4_fs_locations_new(const char *fs_root,
					   const char *rootpath,
					   const unsigned int count);

#endif
