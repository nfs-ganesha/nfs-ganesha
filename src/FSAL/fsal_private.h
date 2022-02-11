/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 *
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

#ifndef FSAL_PRIVATE_H
#define FSAL_PRIVATE_H

/* Define some externals for FSAL */
extern struct fsal_ops def_fsal_ops;
extern struct export_ops def_export_ops; /* freebsd gcc workaround */
extern struct fsal_obj_ops def_handle_ops;
extern struct fsal_pnfs_ds_ops def_pnfs_ds_ops;

/* Global lock for fsal list.
 * kept in fsal_manager.c
 * Special checkpatch case here.  This is private between the two files.
 */

extern pthread_mutex_t fsal_lock;

extern struct glist_head fsal_list;

/* Definitions for static FSALs */
void pseudo_fsal_init(void);
void mdcache_fsal_init(void);

#endif /* FSAL_PRIVATE_H */
