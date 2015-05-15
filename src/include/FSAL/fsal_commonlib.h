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

int fsal_export_init(struct fsal_export *export);

void free_export_ops(struct fsal_export *exp_hdl);

/* fsal_obj_handle common methods
 */

void fsal_obj_handle_init(struct fsal_obj_handle *, struct fsal_export *,
			  object_file_type_t);

void fsal_obj_handle_fini(struct fsal_obj_handle *obj);

/*
 * pNFS DS Helpers
 */

void fsal_pnfs_ds_init(struct fsal_pnfs_ds *pds, struct fsal_module *fsal);
void fsal_pnfs_ds_fini(struct fsal_pnfs_ds *pds);

void fsal_ds_handle_init(struct fsal_ds_handle *dsh, struct fsal_pnfs_ds *pds);
void fsal_ds_handle_fini(struct fsal_ds_handle *dsh);

int open_dir_by_path_walk(int first_fd, const char *path, struct stat *stat);

struct avltree avl_fsid;
struct avltree avl_dev;

struct glist_head posix_file_systems;

pthread_rwlock_t fs_lock;

void free_fs(struct fsal_filesystem *fs);

int populate_posix_file_systems(void);

void release_posix_file_systems(void);

int re_index_fs_fsid(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type,
		     uint64_t major,
		     uint64_t minor);

int re_index_fs_dev(struct fsal_filesystem *fs,
		    struct fsal_dev__ *dev);

int change_fsid_type(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type);

struct fsal_filesystem *lookup_fsid_locked(struct fsal_fsid__ *fsid,
					   enum fsid_type fsid_type);
struct fsal_filesystem *lookup_dev_locked(struct fsal_dev__ *dev);
struct fsal_filesystem *lookup_fsid(struct fsal_fsid__ *fsid,
				    enum fsid_type fsid_type);
struct fsal_filesystem *lookup_dev(struct fsal_dev__ *dev);

void unclaim_fs(struct fsal_filesystem *this);

int claim_posix_filesystems(const char *path,
			    struct fsal_module *fsal,
			    struct fsal_export *exp,
			    claim_filesystem_cb claim,
			    unclaim_filesystem_cb unclaim,
			    struct fsal_filesystem **root_fs);

int encode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type);

int decode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type);

fsal_errors_t fsal_inherit_acls(struct attrlist *attrs, fsal_acl_t *sacl,
			       fsal_aceflag_t inherit);
fsal_status_t fsal_remove_access(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *rem_hdl,
				 bool isdir);
fsal_status_t fsal_mode_to_acl(struct attrlist *attrs, fsal_acl_t *sacl);
fsal_status_t fsal_acl_to_mode(struct attrlist *attrs);
#endif				/* FSAL_COMMONLIB_H */
