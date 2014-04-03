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

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal_commomnlib.h
 * @brief Miscelaneous FSAL common library routines
 */

#ifndef FSAL_COMMONLIB_H
#define FSAL_COMMONLIB_H

/*
 * fsal common utility functions
 */

/* fsal_module to fsal_export helpers
 */

int fsal_attach_export(struct fsal_module *fsal_hdl,
		       struct glist_head *obj_link);
void fsal_detach_export(struct fsal_module *fsal_hdl,
			struct glist_head *obj_link);

/* fsal_export common methods
 */

struct exportlist;

int fsal_export_init(struct fsal_export *, struct exportlist *);

void free_export_ops(struct fsal_export *exp_hdl);

/* fsal_obj_handle common methods
 */

void fsal_obj_handle_init(struct fsal_obj_handle *, struct fsal_export *,
			  object_file_type_t);

int fsal_obj_handle_uninit(struct fsal_obj_handle *obj);

/*
 * pNFS DS Helpers
 */

void fsal_ds_handle_init(struct fsal_ds_handle *, struct fsal_ds_ops *,
			 struct fsal_module *);
int fsal_ds_handle_uninit(struct fsal_ds_handle *ds);

int open_dir_by_path_walk(int first_fd, const char *path, struct stat *stat);

#endif				/* FSAL_COMMONLIB_H */
