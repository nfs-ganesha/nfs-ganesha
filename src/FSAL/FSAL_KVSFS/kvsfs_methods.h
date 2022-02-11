/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* KVSFS methods for handles
 */

/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributor : Philippe DENIEL   philippe.deniel@cea.fr
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
 * -------------
 */

void kvsfs_handle_ops_init(struct fsal_obj_ops *ops);

/* method proto linkage to handle.c for export
 */

fsal_status_t kvsfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle,
			       struct fsal_attrlist *attrs_out);

fsal_status_t kvsfs_create_handle(struct fsal_export *exp_hdl,
				  struct gsh_buffdesc *hdl_desc,
				  struct fsal_obj_handle **handle,
				  struct fsal_attrlist *attrs_out);

/* this needs to be refactored to put ipport inside sockaddr_in */
struct kvsfs_pnfs_ds_parameter {
	struct glist_head ds_list;
	struct sockaddr_in ipaddr;
	unsigned short ipport;
	unsigned int id;
};

/* KVSFS FSAL module private storage
 */

struct kvsfs_fsal_module {
	struct fsal_module fsal;
	struct fsal_obj_ops handle_ops;
};


/*
 * KVSFS internal export
 */

#define KVSFS_NB_DS 4
struct kvsfs_exp_pnfs_parameter {
	unsigned int stripe_unit;
	bool pnfs_enabled;
	unsigned int nb_ds;
	struct kvsfs_pnfs_ds_parameter ds_array[KVSFS_NB_DS];
};

struct kvsfs_fsal_export {
	struct fsal_export export;
	kvsns_ino_t root_inode;
	char *kvsns_config;
	bool pnfs_ds_enabled;
	bool pnfs_mds_enabled;
	struct kvsfs_exp_pnfs_parameter pnfs_param;
};

struct kvsfs_fd {
	fsal_openflags_t openflags;
	pthread_rwlock_t fdlock;
	kvsns_file_open_t fd;
};

struct kvsfs_state_fd {
	struct state_t state;
	struct kvsfs_fd kvsfs_fd;
};

/**
 * @brief KVSFS internal object handle
 *
 * The handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 * wrt locks, should this be a lock counter??
 */

struct kvsfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct kvsfs_file_handle *handle;
	union {
		struct {
			struct fsal_share share;
			kvsns_ino_t inode;
			struct kvsfs_fd fd;
			kvsns_cred_t cred;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
	} u;
};

struct kvsfs_fsal_obj_handle *kvsfs_alloc_handle(struct kvsfs_file_handle *fh,
						 struct fsal_attrlist *attr,
						 const char *link_content,
						 struct fsal_export *exp_hdl);
	/* I/O management */
/* OK */
fsal_status_t kvsfs_open2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state,
			  fsal_openflags_t openflags,
			  enum fsal_create_mode createmode,
			  const char *name,
			  struct fsal_attrlist *attr_set,
			  fsal_verifier_t verifier,
			  struct fsal_obj_handle **new_obj,
			  struct fsal_attrlist *attrs_out,
			  bool *caller_perm_check);
fsal_status_t kvsfs_reopen2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state,
			    fsal_openflags_t openflags);
fsal_status_t kvsfs_commit2(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset,
			    size_t len);
/* OK */
fsal_openflags_t kvsfs_status2(struct fsal_obj_handle *obj_hdl,
			       struct state_t *state);
void kvsfs_read2(struct fsal_obj_handle *obj_hdl,
		 bool bypass,
		 fsal_async_cb done_cb,
		 struct fsal_io_arg *read_arg,
		 void *caller_arg);
void kvsfs_write2(struct fsal_obj_handle *obj_hdl,
		  bool bypass,
		  fsal_async_cb done_cb,
		  struct fsal_io_arg *write_arg,
		  void *caller_arg);
/* OK */
fsal_status_t kvsfs_close2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state);
/* OK */
fsal_status_t kvsfs_create2(struct fsal_obj_handle *dir_hdl,
			    const char *filename,
			    const struct req_op_context *op_ctx,
			    mode_t unix_mode,
			    struct kvsfs_file_handle *kvsfs_fh,
			    int posix_flags,
			    struct fsal_attrlist *fsal_attr);

fsal_status_t kvsfs_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			    fsal_share_param_t request_share);

/* extended attributes management */
fsal_status_t kvsfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				  unsigned int cookie,
				  fsal_xattrent_t *xattrs_tab,
				  unsigned int xattrs_tabsize,
				  unsigned int *p_nb_returned,
				  int *end_of_list);
fsal_status_t kvsfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					 const char *xattr_name,
					 unsigned int *pxattr_id);
fsal_status_t kvsfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name,
					    void  *buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t kvsfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id,
					  void  *buffer_addr,
					  size_t buffer_size,
					  size_t *p_output_size);
fsal_status_t kvsfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				     const char *xattr_name,
				     void *buffer_addr,
				     size_t buffer_size,
				     int create);
fsal_status_t kvsfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id,
					  void  *buffer_addr,
					  size_t buffer_size);
fsal_status_t kvsfs_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int xattr_id,
				    struct fsal_attrlist *p_attrs);
fsal_status_t kvsfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					unsigned int xattr_id);
fsal_status_t kvsfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					  const char *xattr_name);
fsal_status_t kvsfs_lock_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock);


