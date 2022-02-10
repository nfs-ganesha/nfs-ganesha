/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
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
 * ---------------------------------------
 */

/* GPFS methods for handles
 */

#ifndef GPFS_METHODS_H
#define GPFS_METHODS_H

#include <fcntl.h>
#include "include/gpfs_nfs.h"

/* private helpers from export
 */


/* method proto linkage to handle.c for export
 */

fsal_status_t gpfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle,
			       struct fsal_attrlist *attrs_out);

fsal_status_t gpfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out);

/*
 * GPFS internal export
 */

struct gpfs_fsal_export {
	struct fsal_export export;
	struct fsal_filesystem *root_fs;
	struct glist_head filesystems;
	int export_fd;
	bool pnfs_ds_enabled;
	bool pnfs_mds_enabled;
	bool use_acl;
	bool ignore_mode_change;
};

/*
 * GPFS internal filesystem
 */
struct gpfs_filesystem {
	struct fsal_filesystem *fs;
	int root_fd;
	struct glist_head exports;
	bool stop_thread;
	pthread_t up_thread; /* upcall thread */

	/* we have an upcall thread for each file system. We use upvector_mutex
	 * to get an export from the list of exports in a file system. The
	 * following up_vector points to up_ops in such an export.
	 */
	pthread_mutex_t upvector_mutex;
	struct fsal_up_vector *up_vector;
};

/*
 * Link GPFS file systems and exports
 * Supports a many-to-many relationship
 */
struct gpfs_filesystem_export_map {
	struct gpfs_fsal_export *exp;
	struct gpfs_filesystem *fs;
	struct glist_head on_exports;
	struct glist_head on_filesystems;
};

void gpfs_extract_fsid(struct gpfs_file_handle *fh, struct fsal_fsid__ *fsid);

void gpfs_unexport_filesystems(struct gpfs_fsal_export *exp);

fsal_status_t gpfs_merge(struct fsal_obj_handle *orig_hdl,
			 struct fsal_obj_handle *dupe_hdl);

/**
 * @brief GPFS internal object handle
 *
 * The handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 * wrt locks, should this be a lock counter??
 * AF_UNIX sockets are strange ducks.  I personally cannot see why they
 * are here except for the ability of a client to see such an animal with
 * an 'ls' or get rid of one with an 'rm'.  You can't open them in the
 * usual file way so open_by_handle_at leads to a deadend.  To work around
 * this, we save the args that were used to mknod or lookup the socket.
 */

struct gpfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct gpfs_file_handle *handle;
	union {
		struct {
			struct fsal_share share;
			struct gpfs_fd fd;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
	} u;
};

	/* I/O management */
struct gpfs_fsal_obj_handle *alloc_handle(struct gpfs_file_handle *fh,
					  struct fsal_filesystem *fs,
					  struct fsal_attrlist *attributes,
					  const char *link_content,
					  struct fsal_export *exp_hdl);
fsal_status_t gpfs_open2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state,
			 fsal_openflags_t openflags,
			 enum fsal_create_mode createmode,
			 const char *name,
			 struct fsal_attrlist *attrib_set,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **new_obj,
			 struct fsal_attrlist *attrs_out,
			 bool *caller_perm_check);
fsal_status_t gpfs_reopen2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   fsal_openflags_t openflags);
void gpfs_read2(struct fsal_obj_handle *obj_hdl,
		bool bypass,
		fsal_async_cb done_cb,
		struct fsal_io_arg *read_arg,
		void *caller_arg);
void gpfs_write2(struct fsal_obj_handle *obj_hdl,
		 bool bypass,
		 fsal_async_cb done_cb,
		 struct fsal_io_arg *write_arg,
		 void *caller_arg);
fsal_status_t gpfs_commit2(struct fsal_obj_handle *obj_hdl,
			   off_t offset,
			   size_t len);
fsal_status_t gpfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state,
			    void *owner,
			    fsal_lock_op_t lock_op,
			    fsal_lock_param_t *request_lock,
			    fsal_lock_param_t *conflicting_lock);
fsal_status_t gpfs_close2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state);
fsal_status_t gpfs_setattr2(struct fsal_obj_handle *obj_hdl,
			    bool bypass,
			    struct state_t *state,
			    struct fsal_attrlist *attrib_set);
fsal_status_t gpfs_read_plus_fd(int my_fs,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file, struct io_info *info, int expfd);
fsal_status_t gpfs_seek(struct fsal_obj_handle *obj_hdl,
			struct io_info *info);
fsal_status_t gpfs_io_advise(struct fsal_obj_handle *obj_hdl,
			 struct io_hints *hints);
fsal_status_t gpfs_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			    fsal_share_param_t request_share);
fsal_status_t gpfs_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t gpfs_fallocate(struct fsal_obj_handle *obj_hdl, state_t *state,
			     uint64_t offset, uint64_t length, bool allocate);

/* Internal GPFS method linkage to export object */
fsal_status_t
gpfs_create_export(struct fsal_module *fsal_hdl, void *parse_node,
		   struct config_error_type *err_type,
		   const struct fsal_up_vector *up_ops);

#endif
