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

#include "fsal_api.h"
#include "sal_data.h"

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

void fsal_export_init(struct fsal_export *export);

void fsal_export_stack(struct fsal_export *sub_export,
		       struct fsal_export *super_export);

void free_export_ops(struct fsal_export *exp_hdl);

/* fsal_obj_handle common methods
 */

void fsal_default_obj_ops_init(struct fsal_obj_ops *obj_ops);
void fsal_obj_handle_init(struct fsal_obj_handle *, struct fsal_export *,
			  object_file_type_t);

void fsal_obj_handle_fini(struct fsal_obj_handle *obj);

/**
 * @brief Test handle type
 *
 * This function tests that a handle is of the specified type.
 *
 * @retval true if it is.
 * @retval false if it isn't.
 */
static inline bool fsal_obj_handle_is(struct fsal_obj_handle *obj_hdl,
					object_file_type_t type)
{
	return obj_hdl->type == type;
}

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

int populate_posix_file_systems(bool force);

int resolve_posix_filesystem(const char *path,
			     struct fsal_module *fsal,
			     struct fsal_export *exp,
			     claim_filesystem_cb claim,
			     unclaim_filesystem_cb unclaim,
			     struct fsal_filesystem **root_fs);

void release_posix_file_systems(void);

int re_index_fs_fsid(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type,
		     struct fsal_fsid__ *fsid);

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
fsal_status_t fsal_rename_access(struct fsal_obj_handle *old_dir_hdl,
				 struct fsal_obj_handle *src_obj_hdl,
				 struct fsal_obj_handle *new_dir_hdl,
				 struct fsal_obj_handle *dst_obj_hdl,
				 bool isdir);
fsal_status_t fsal_mode_to_acl(struct attrlist *attrs, fsal_acl_t *sacl);
fsal_status_t fsal_acl_to_mode(struct attrlist *attrs);

void set_common_verifier(struct attrlist *attrs, fsal_verifier_t verifier);

void update_share_counters(struct fsal_share *share,
			   fsal_openflags_t old_openflags,
			   fsal_openflags_t new_openflags);

fsal_status_t check_share_conflict(struct fsal_share *share,
				   fsal_openflags_t openflags,
				   bool bypass);

fsal_status_t merge_share(struct fsal_share *orig_share,
			  struct fsal_share *dupe_share);

/**
 * @brief Function to open an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   Mode for open
 * @param[out] fd          File descriptor that is to be used
 *
 * @return FSAL status.
 */

typedef fsal_status_t (*fsal_open_func)(struct fsal_obj_handle *obj_hdl,
					fsal_openflags_t openflags,
					struct fsal_fd *fd);

/**
 * @brief Function to close an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  fd          File handle to close
 *
 * @return FSAL status.
 */

typedef fsal_status_t (*fsal_close_func)(struct fsal_obj_handle *obj_hdl,
					 struct fsal_fd *fd);

fsal_status_t fsal_reopen_obj(struct fsal_obj_handle *obj_hdl,
			      bool check_share,
			      bool bypass,
			      fsal_openflags_t openflags,
			      struct fsal_fd *my_fd,
			      struct fsal_share *share,
			      fsal_open_func open_func,
			      fsal_close_func close_func,
			      struct fsal_fd **out_fd,
			      bool *has_lock,
			      bool *closefd);

fsal_status_t fsal_find_fd(struct fsal_fd **out_fd,
			   struct fsal_obj_handle *obj_hdl,
			   struct fsal_fd *my_fd,
			   struct fsal_share *share,
			   bool bypass,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   fsal_open_func open_func,
			   fsal_close_func close_func,
			   bool *has_lock,
			   bool *closefd,
			   bool open_for_locks,
			   bool *reusing_open_state_fd);

/**
 * @brief Initialize a state_t structure
 *
 * @param[in] state                 The state to initialize
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns the state structure for streamlined coding.
 */

static inline struct state_t *init_state(struct state_t *state,
					 struct fsal_export *exp_hdl,
					 enum state_type state_type,
					 struct state_t *related_state)
{
	state->state_exp = exp_hdl;
	state->state_type = state_type;

	if (state_type == STATE_TYPE_LOCK ||
	    state_type == STATE_TYPE_NLM_LOCK)
		state->state_data.lock.openstate = related_state;

	return state;
}

bool check_verifier_stat(struct stat *st, fsal_verifier_t verifier);

bool check_verifier_attrlist(struct attrlist *attrs, fsal_verifier_t verifier);

bool fsal_common_is_referral(struct fsal_obj_handle *obj_hdl,
			     struct attrlist *attrs, bool cache_attrs);
#endif				/* FSAL_COMMONLIB_H */
