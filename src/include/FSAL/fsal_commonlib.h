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

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal_commomnlib.h
 * @brief Miscellaneous FSAL common library routines
 */

#ifndef FSAL_COMMONLIB_H
#define FSAL_COMMONLIB_H

#include "fsal_api.h"
#include "sal_data.h"
#include "sal_functions.h"

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

void fsal_export_init(struct fsal_export *);

void fsal_export_stack(struct fsal_export *sub_export,
		       struct fsal_export *super_export);

void free_export_ops(struct fsal_export *exp_hdl);

/* fsal_obj_handle common methods
 */

void fsal_default_obj_ops_init(struct fsal_obj_ops *obj_ops);

/*
 * @param[in]  add_to_fsal_list   Whether FSAL would like the handle to be in
 * the global list. FSAL which have no usecase of storing the obj handles
 * within fsal handlers, can pass this flag as false.
 */

void fsal_obj_handle_init(struct fsal_obj_handle *obj, struct fsal_export *exp,
			  object_file_type_t type, bool add_to_fsal_handle);

void fsal_obj_handle_fini(struct fsal_obj_handle *obj,
			  bool added_to_fsal_handle);

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

int encode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type);

int decode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type);

fsal_errors_t fsal_inherit_acls(struct fsal_attrlist *attrs, fsal_acl_t *sacl,
			       fsal_aceflag_t inherit);
fsal_status_t fsal_remove_access(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *rem_hdl,
				 bool isdir);
fsal_status_t fsal_rename_access(struct fsal_obj_handle *old_dir_hdl,
				 struct fsal_obj_handle *src_obj_hdl,
				 struct fsal_obj_handle *new_dir_hdl,
				 struct fsal_obj_handle *dst_obj_hdl,
				 bool isdir);
bool fsal_can_reuse_mode_to_acl(const fsal_acl_t *sacl);
fsal_status_t fsal_mode_to_acl(struct fsal_attrlist *attrs, fsal_acl_t *sacl);
fsal_status_t fsal_acl_to_mode(struct fsal_attrlist *attrs);

void set_common_verifier(struct fsal_attrlist *attrs,
			 fsal_verifier_t verifier,
			 bool trunc_verif);

void update_share_counters(struct fsal_share *share,
			   fsal_openflags_t old_openflags,
			   fsal_openflags_t new_openflags);

static inline void update_share_counters_locked(struct fsal_obj_handle *obj_hdl,
						struct fsal_share *share,
						fsal_openflags_t old_openflags,
						fsal_openflags_t new_openflags)
{
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	update_share_counters(share, old_openflags, new_openflags);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
}

fsal_status_t check_share_conflict(struct fsal_share *share,
				   fsal_openflags_t openflags,
				   bool bypass);

static inline
fsal_status_t check_share_conflict_and_update(struct fsal_obj_handle *obj_hdl,
					      struct fsal_share *share,
					      fsal_openflags_t old_openflags,
					      fsal_openflags_t new_openflags,
					      bool bypass)
{
	fsal_status_t status;

	status = check_share_conflict(share, new_openflags, bypass);

	if (!FSAL_IS_ERROR(status)) {
		/* Take the share reservation now by updating the counters. */
		update_share_counters(share, old_openflags, new_openflags);
	}

	return status;
}

static inline fsal_status_t
check_share_conflict_and_update_locked(struct fsal_obj_handle *obj_hdl,
				       struct fsal_share *share,
				       fsal_openflags_t old_openflags,
				       fsal_openflags_t new_openflags,
				       bool bypass)
{
	fsal_status_t status;

	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	status = check_share_conflict_and_update(obj_hdl, share, old_openflags,
						 new_openflags, bypass);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

fsal_status_t merge_share(struct fsal_obj_handle *orig_hdl,
			  struct fsal_share *orig_share,
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

/**
 * @brief Function to close a fsal_fd.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  fd          File handle to close
 *
 * @return FSAL status.
 */

static inline
fsal_status_t fsal_close_fd(struct fsal_obj_handle *obj_hdl,
			    struct fsal_fd *fd)
{
	return obj_hdl->obj_ops->close_func(obj_hdl, fd);
}

/**
 * @brief Function to open or reopen a fsal_fd.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   New mode for open
 * @param[out] fd          File descriptor that is to be used
 *
 * @return FSAL status.
 */

static inline
fsal_status_t fsal_reopen_fd(struct fsal_obj_handle *obj_hdl,
			     fsal_openflags_t openflags,
			     struct fsal_fd *fd)
{
	return obj_hdl->obj_ops->reopen_func(obj_hdl, openflags, fd);
}

fsal_status_t close_fsal_fd(struct fsal_obj_handle *obj_hdl,
			    struct fsal_fd *fsal_fd,
			    bool is_reclaiming);

fsal_status_t fsal_start_global_io(struct fsal_fd **out_fd,
				   struct fsal_obj_handle *obj_hdl,
				   struct fsal_fd *my_fd,
				   struct fsal_fd *tmp_fd,
				   fsal_openflags_t openflags,
				   bool bypass,
				   struct fsal_share *share);

fsal_status_t fsal_start_io(struct fsal_fd **out_fd,
			    struct fsal_obj_handle *obj_hdl,
			    struct fsal_fd *obj_fd,
			    struct fsal_fd *tmp_fd,
			    struct state_t *state,
			    fsal_openflags_t openflags,
			    bool open_for_locks,
			    bool *reusing_open_state_fd,
			    bool bypass,
			    struct fsal_share *share);

fsal_status_t fsal_complete_io(struct fsal_obj_handle *obj_hdl,
			       struct fsal_fd *fsal_fd);

fsal_status_t fsal_start_fd_work(struct fsal_fd *fsal_fd, bool is_reclaiming);

static inline void fsal_start_fd_work_no_reclaim(struct fsal_fd *fsal_fd)
{
	fsal_status_t rc;

	rc = fsal_start_fd_work(fsal_fd, false);

	if (rc.major != ERR_FSAL_NO_ERROR)
		LogFatal(COMPONENT_FSAL, "Unexpected failure.");
}

void fsal_complete_fd_work(struct fsal_fd *fsal_fd);
void insert_fd_lru(struct fsal_fd *fsal_fd);
void bump_fd_lru(struct fsal_fd *fsal_fd);
void remove_fd_lru(struct fsal_fd *fsal_fd);

/**
 * @brief Initialize a state_t structure
 *
 * @param[in] state                 The state to initialize
 * @param[in] state_free            An optional function to manage freeing state
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns the state structure for streamlined coding.
 *
 * NOTE: state_free MUST free the state.
 */

static inline struct state_t *init_state(struct state_t *state,
					 state_free_t state_free,
					 enum state_type state_type,
					 struct state_t *related_state)
{
	state->state_type = state_type;
	state->state_free = state_free;

	if (related_state) {
		memcpy(state->state_data.lock.openstate_key,
		       related_state->stateid_other,
		       OTHERSIZE);
	}

	return state;
}

bool check_verifier_stat(struct stat *st,
			 fsal_verifier_t verifier,
			 bool trunc_verif);

bool check_verifier_attrlist(struct fsal_attrlist *attrs,
			     fsal_verifier_t verifier,
			     bool trunc_verif);

bool fsal_common_is_referral(struct fsal_obj_handle *obj_hdl,
			     struct fsal_attrlist *attrs, bool cache_attrs);

fsal_status_t update_export(struct fsal_module *fsal_hdl,
			    void *parse_node,
			    struct config_error_type *err_type,
			    struct fsal_export *original,
			    struct fsal_module *updated_super);

#endif				/* FSAL_COMMONLIB_H */
