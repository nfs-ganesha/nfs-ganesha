/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* PSEUDOFS methods for handles
 */

#include "fsal_handle_syscalls.h"
#include "nlm_list.h"

struct pseudo_fsal_obj_handle;

/*
 * PSEUDOFS internal export
 */
struct pseudofs_fsal_export {
	struct fsal_export export;
	char *export_path;
	struct pseudo_fsal_obj_handle *root_handle;
};

fsal_status_t pseudofs_lookup_path(struct fsal_export *exp_hdl,
				 const struct req_op_context *opctx,
				 const char *path,
				 struct fsal_obj_handle **handle);

fsal_status_t pseudofs_create_handle(struct fsal_export *exp_hdl,
				   const struct req_op_context *opctx,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle);

/*
 * PSEUDOFS internal object handle
 * handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 */

struct pseudo_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	char *handle;
	struct pseudo_fsal_obj_handle *parent;
	struct glist_head contents;
	struct glist_head me;
	uint32_t numlinks;
	char *name;
};

int pseudofs_fsal_open(struct pseudo_fsal_obj_handle *, int, fsal_errors_t *);
int pseudofs_fsal_readlink(struct pseudo_fsal_obj_handle *, fsal_errors_t *);

static inline bool pseudofs_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

	/* I/O management */
fsal_status_t pseudofs_open(struct fsal_obj_handle *obj_hdl,
			  const struct req_op_context *opctx,
			  fsal_openflags_t openflags);
fsal_openflags_t pseudofs_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t pseudofs_read(struct fsal_obj_handle *obj_hdl,
			  const struct req_op_context *opctx, uint64_t offset,
			  size_t buffer_size, void *buffer,
			  size_t *read_amount, bool *end_of_file);
fsal_status_t pseudofs_write(struct fsal_obj_handle *obj_hdl,
			   const struct req_op_context *opctx, uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable);
fsal_status_t pseudofs_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len);
fsal_status_t pseudofs_lock_op(struct fsal_obj_handle *obj_hdl,
			     const struct req_op_context *opctx, void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock);
fsal_status_t pseudofs_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			      fsal_share_param_t request_share);
fsal_status_t pseudofs_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t pseudofs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
				 lru_actions_t requests);

/* extended attributes management */
fsal_status_t pseudofs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    const struct req_op_context *opctx,
				    unsigned int cookie,
				    fsal_xattrent_t *xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list);
fsal_status_t pseudofs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const struct req_op_context *opctx,
					   const char *xattr_name,
					   unsigned int *pxattr_id);
fsal_status_t pseudofs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const struct req_op_context
					      *opctx, const char *xattr_name,
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size);
fsal_status_t pseudofs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    const struct req_op_context *opctx,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t pseudofs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const struct req_op_context *opctx,
				      const char *xattr_name,
				      caddr_t buffer_addr, size_t buffer_size,
				      int create);
fsal_status_t pseudofs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    const struct req_op_context *opctx,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size);
fsal_status_t pseudofs_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				      const struct req_op_context *opctx,
				      unsigned int xattr_id,
				      struct attrlist *p_attrs);
fsal_status_t pseudofs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  const struct req_op_context *opctx,
					  unsigned int xattr_id);
fsal_status_t pseudofs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const struct req_op_context *opctx,
					    const char *xattr_name);

void pseudofs_handle_ops_init(struct fsal_obj_ops *ops);

/* Internal PSEUDOFS method linkage to export object
 */

fsal_status_t pseudofs_create_export(struct fsal_module *fsal_hdl,
				     const char *export_path,
				     const char *fs_specific,
				     struct exportlist *exp_entry,
				     struct fsal_module *next_fsal,
				     const struct fsal_up_vector *up_ops,
				     struct fsal_export **export);
