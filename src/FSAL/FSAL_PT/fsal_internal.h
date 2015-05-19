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

/* the following variables must not be defined in fsal_internal.c */

fsal_status_t fsal_internal_access(int mntfd,
				   const struct req_op_context *p_context,
				   ptfsal_handle_t *p_handle,
				   fsal_accessflags_t access_type,
				   struct attrlist *p_object_attributes);

/**
 * Gets a fd from a handle
 */
fsal_status_t fsal_internal_handle2fd(const struct req_op_context *p_context,
				      struct pt_fsal_obj_handle *myself,
				      int *pfd, int oflags);

fsal_status_t fsal_internal_handle2fd_at(const struct req_op_context *p_context,
					 struct pt_fsal_obj_handle *myself,
					 int *pfd, int oflags);

/**
 * Access a link by a file handle.
 */
fsal_status_t fsal_readlink_by_handle(const struct req_op_context *p_context,
				      struct fsal_export *export,
				      ptfsal_handle_t *p_handle, char *__buf,
				      size_t maxlen);

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

/**
 *  test the access to a file from its POSIX attributes (struct stat) OR
 * its FSAL attributes (fsal_attrib_list_t).
 *
 */
fsal_status_t fsal_internal_testAccess(const struct req_op_context *p_context,
				       fsal_accessflags_t access_type,
				       struct attrlist *p_object_attributes);

fsal_status_t fsal_stat_by_handle(int dirfd, ptfsal_handle_t *p_handle,
				  struct stat *buf);

fsal_status_t
fsal_check_access_by_mode(const struct req_op_context *p_context,
			  fsal_accessflags_t access_type,
			struct stat *p_buffstat);

fsal_status_t PTFSAL_access(ptfsal_handle_t *p_object_handle,
			    int dirfd,
			    fsal_accessflags_t access_type,
			    struct attrlist *p_object_attributes);

fsal_status_t PTFSAL_getattrs(struct fsal_export *export,
			      const struct req_op_context *p_context,
			      ptfsal_handle_t *p_filehandle,
			      struct attrlist *p_object_attributes);

fsal_status_t PTFSAL_getattrs_descriptor(int *p_file_descriptor,
					 ptfsal_handle_t *p_filehandle,
					 int dirfd,
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
fsal_status_t PTFSAL_opendir(ptfsal_handle_t *p_dir_handle,
			     int dirfd,
			     int *p_dir_descriptor,
			     struct attrlist *p_dir_attributes);

fsal_status_t PTFSAL_closedir(int *p_dir_descriptor);

fsal_status_t PTFSAL_open_by_name(ptfsal_handle_t *dirhandle,
				  const char *filename,
				  int dirfd,
				  fsal_openflags_t openflags,
				  int *file_descriptor,
				  struct attrlist *file_attributes);


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

fsal_status_t PTFSAL_dynamic_fsinfo(ptfsal_handle_t *p_handle,
				    int dirfd,
				    fsal_dynamicfsinfo_t *p_dynamicinfo);


fsal_status_t PTFSAL_test_access(int dirfd,
				 fsal_accessflags_t access_type,
				 struct attrlist *p_object_attributes
				);

fsal_status_t PTFSAL_terminate(void);

fsal_status_t PTFSAL_lookup(const struct req_op_context *p_context,
			    struct fsal_obj_handle *parent,
			    const char *p_filename,
			    struct attrlist *p_object_attr,
			    ptfsal_handle_t *fh);

fsal_status_t PTFSAL_lookupPath(const char *p_path,
				int dirfd,
				ptfsal_handle_t *object_handle,
				struct attrlist *p_object_attributes);


fsal_status_t PTFSAL_lookupJunction(ptfsal_handle_t *p_handle,
				    int dirfd,
				    ptfsal_handle_t *fsoot_hdl,
				    struct attrlist *p_fsroot_attributes);


fsal_status_t PTFSAL_lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t request_lock,
			     fsal_lock_param_t *conflicting_lock);

fsal_status_t PTFSAL_share_op(int mntfd,
			      int fd,
			      void *p_owner,
			      fsal_share_param_t request_share);

fsal_status_t PTFSAL_rcp(ptfsal_handle_t *filehandle,
			 int dirfd,
			 const char *p_local_path,
			 int transfer_opt);

fsal_status_t PTFSAL_rename(struct fsal_obj_handle *old_hdl,
			    const char *p_old_name,
			    struct fsal_obj_handle *new_hdl,
			    const char *p_new_name,
			    const struct req_op_context *p_context);

fsal_status_t PTFSAL_readlink(struct fsal_obj_handle *dir_hdl,
			      const struct req_op_context *p_context,
			      char *p_link_content,
			      size_t *link_len,
			      struct attrlist *p_link_attributes);

fsal_status_t PTFSAL_symlink(struct fsal_obj_handle *dir_hdl,
			     const char *p_linkname,
			     const char *p_linkcontent,
			     const struct req_op_context *p_context,
			     uint32_t accessmode,
			     ptfsal_handle_t *p_link_handle,
			     struct attrlist *p_link_attributes);

int PTFSAL_handlecmp(ptfsal_handle_t *handle1, ptfsal_handle_t *handle2,
		     fsal_status_t *status);

unsigned int PTFSAL_Handle_to_HashIndex(ptfsal_handle_t *p_handle,
					unsigned int cookie,
					unsigned int alphabet_len,
					unsigned int index_size);

unsigned int PTFSAL_Handle_to_RBTIndex(ptfsal_handle_t *p_handle,
				       unsigned int cookie);

fsal_status_t PTFSAL_truncate(struct fsal_export *export,
			      struct pt_fsal_obj_handle *p_filehandle,
			      const struct req_op_context *p_context,
			      size_t length,
			      struct attrlist *p_object_attributes);


fsal_status_t PTFSAL_unlink(struct fsal_obj_handle *dir_hdl,
			    const char *p_object_name,
			    const struct req_op_context *p_context,
			    struct attrlist *p_parent_attributes);

char *PTFSAL_GetFSName();

fsal_status_t PTFSAL_GetXAttrAttrs(ptfsal_handle_t *obj_hdl,
				   int dirfd,
				   unsigned int xattr_id,
				   struct attrlist *p_attrs);

fsal_status_t PTFSAL_ListXAttrs(ptfsal_handle_t *obj_hdl,
				unsigned int cookie,
				int dirfd,
				fsal_xattrent_t *xattrs_tab,
				unsigned int xattrs_tabsize,
				unsigned int *p_nb_returned,
				int *end_of_list);

fsal_status_t PTFSAL_GetXAttrValueById(ptfsal_handle_t *objhdl,
				       unsigned int xattr_id,
				       int dirfd,
				       caddr_t buffer_addr,
				       size_t buffer_size,
				       size_t *p_output_size);

fsal_status_t PTFSAL_GetXAttrIdByName(ptfsal_handle_t *objhdl,
				      const char *xattr_name,
				      int dirfd,
				      unsigned int *pxattr_id);

fsal_status_t PTFSAL_GetXAttrValueByName(ptfsal_handle_t *objhdl,
					 const char *xattr_name,
					 int dirfd,
					 caddr_t buffer_addr,
					 size_t buffer_size,
					 size_t *p_output_size);

fsal_status_t PTFSAL_SetXAttrValue(ptfsal_handle_t *obj_hdl,
				   const char *xattr_name,
				   int dirfd,
				   caddr_t buffer_addr,
				   size_t buffer_size,
				   int create);

fsal_status_t PTFSAL_RemoveXAttrByName(ptfsal_handle_t *objhdl,
				       int dirfd,
				       const char *xattr_name);

unsigned int PTFSAL_GetFileno(int pfile);

fsal_status_t PTFSAL_commit(struct pt_fsal_obj_handle *p_file_descriptor,
			    const struct req_op_context *opctx,
			    uint64_t offset, size_t size);

#endif				/* FSAL_INTERNAL_H */
