/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 *
 * ---------------------------------------
 */

#ifndef FSAL_INTERNAL_H
#define FSAL_INTERNAL_H

#include "nfs_core.h"
#include "nfs_exports.h"
#include  "fsal.h"
#include <sys/stat.h>
#include "fsal_types.h"
#include "fcntl.h"
#include "pt_methods.h"

/* defined the set of attributes supported with POSIX */
#define PT_SUPPORTED_ATTRIBUTES (                                       \
	ATTR_TYPE     | ATTR_SIZE      | \
	ATTR_FSID     | ATTR_FILEID  | \
	ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
	ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
	ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
	ATTR_CHGTIME  | ATTR_ACL)

/**
 * Gets a fd from a handle
 */
fsal_status_t fsal_internal_handle2fd(const struct req_op_context *p_context,
				      struct pt_fsal_obj_handle *myself,
				      int *pfd, int oflags);

fsal_status_t fsal_internal_handle2fd_at(const struct req_op_context *p_context,
					 struct pt_fsal_obj_handle *myself,
					 int *pfd, int oflags);

fsal_status_t
fsal_internal_get_handle_at(const struct req_op_context *p_context,
			    struct fsal_export *export,
			    int dfd,
			    const char *p_fsalname,
			    ptfsal_handle_t *p_handle);

fsal_status_t
fsal_internal_get_handle(const struct req_op_context *p_context,
			 struct fsal_export *export,
			 const char *p_fsalpath,
			 ptfsal_handle_t *p_handle);
/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_fd2handle(int fd,
				      ptfsal_handle_t *p_handle);

fsal_status_t fsal_internal_link_at(int srcfd, int dfd, char *name);

fsal_status_t fsal_stat_by_handle(int dirfd, ptfsal_handle_t *p_handle,
				  struct stat *buf);

fsal_status_t PTFSAL_getattrs(struct fsal_export *export,
			      const struct req_op_context *p_context,
			      ptfsal_handle_t *p_filehandle,
			      struct attrlist *p_object_attributes);

fsal_status_t PTFSAL_setattrs(struct fsal_obj_handle *dir_hdl,
			      const struct req_op_context *p_context,
			      struct attrlist *p_attrib_set,
			      struct attrlist *p_object_attributes);


fsal_status_t PTFSAL_create(struct fsal_obj_handle *dir_hdl,
			    const char *p_filename,
			    const struct req_op_context *p_context,
			    uint32_t accessmode,
			    ptfsal_handle_t *p_object_handle,
			    struct attrlist *p_object_attributes);
fsal_status_t PTFSAL_mkdir(struct fsal_obj_handle *dir_hdl,
			   const char *p_dirname,
			   const struct req_op_context *p_context,
			   uint32_t accessmode,
			   ptfsal_handle_t *p_object_handle,
			   struct attrlist *p_object_attributes);
fsal_status_t PTFSAL_mknode(struct fsal_obj_handle *dir_hdl,
			    const char *p_node_name,
			    const struct req_op_context *p_context,
			    uint32_t accessmode,
			    mode_t nodetype,
			    fsal_dev_t *dev,
			    ptfsal_handle_t *p_object_handle,
			    struct attrlist *node_attributes);
fsal_status_t PTFSAL_link(struct fsal_obj_handle *destdir_hdl,
			  ptfsal_handle_t *target_handle,
			  const char *p_link_name,
			  const struct req_op_context *p_context,
			  struct attrlist *p_attributes);

fsal_status_t PTFSAL_open(struct fsal_obj_handle *obj_hdl,
			  const struct req_op_context *p_context,
			  fsal_openflags_t openflags,
			  int *p_file_descriptor,
			  struct attrlist *p_file_attributes);

fsal_status_t PTFSAL_read(struct pt_fsal_obj_handle *p_file_descriptor,
			  const struct req_op_context *opctx,
			  uint64_t offset,
			  size_t buffer_size,
			  caddr_t buffer,
			  size_t *p_read_amount,
			  bool *p_end_of_file);

fsal_status_t
PTFSAL_write(struct pt_fsal_obj_handle *p_file_descriptor,
	     const struct req_op_context *opctx,
	     uint64_t offset,
	     size_t buffer_size,
	     caddr_t buffer,
	     size_t *p_write_amount,
	     bool *fsal_stable);

fsal_status_t PTFSAL_close(int p_file_descriptor);

fsal_status_t PTFSAL_terminate(void);

fsal_status_t PTFSAL_lookup(const struct req_op_context *p_context,
			    struct fsal_obj_handle *parent,
			    const char *p_filename,
			    struct attrlist *p_object_attr,
			    ptfsal_handle_t *fh);

fsal_status_t PTFSAL_rename(struct fsal_obj_handle *old_hdl,
			    const char *p_old_name,
			    struct fsal_obj_handle *new_hdl,
			    const char *p_new_name,
			    const struct req_op_context *p_context);

fsal_status_t PTFSAL_truncate(struct fsal_export *export,
			      struct pt_fsal_obj_handle *p_filehandle,
			      const struct req_op_context *p_context,
			      size_t length,
			      struct attrlist *p_object_attributes);


fsal_status_t PTFSAL_unlink(struct fsal_obj_handle *dir_hdl,
			    const char *p_object_name,
			    const struct req_op_context *p_context,
			    struct attrlist *p_parent_attributes);

fsal_status_t PTFSAL_commit(struct pt_fsal_obj_handle *p_file_descriptor,
			    const struct req_op_context *opctx,
			    uint64_t offset, size_t size);

#endif				/* FSAL_INTERNAL_H */
