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
			    const char *p_fsalname,	/* IN */
			    ptfsal_handle_t *p_handle);

fsal_status_t
fsal_internal_get_handle(const struct req_op_context *p_context,
			 struct fsal_export *export,
			 const char *p_fsalpath,	/* IN */
			 ptfsal_handle_t *p_handle);
/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_fd2handle(int fd,	/* IN */
				      ptfsal_handle_t *p_handle); /* OUT */

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
			  fsal_accessflags_t access_type,	/* IN */
			struct stat *p_buffstat);	/* IN */

fsal_status_t PTFSAL_access(ptfsal_handle_t *p_object_handle,	/* IN */
			    int dirfd,	/* IN */
			    fsal_accessflags_t access_type,	/* IN */
			    struct attrlist *p_object_attributes);/* IN/OUT */

fsal_status_t PTFSAL_getattrs(struct fsal_export *export,	/* IN */
			      const struct req_op_context *p_context,	/* IN */
			      ptfsal_handle_t *p_filehandle,	/* IN */
			      struct attrlist *p_object_attributes);/* IN/OUT */

fsal_status_t PTFSAL_getattrs_descriptor(int *p_file_descriptor,  /* IN */
					 ptfsal_handle_t *p_filehandle,/* IN */
					 int dirfd,	/* IN */
					 struct attrlist *p_object_attributes);
					/* IN/OUT */

fsal_status_t PTFSAL_setattrs(struct fsal_obj_handle *dir_hdl,	/* IN */
			      const struct req_op_context *p_context,/* IN */
			      struct attrlist *p_attrib_set,	/* IN */
			      struct attrlist *p_object_attributes);
			      /* IN/OUT */

fsal_status_t PTFSAL_create(struct fsal_obj_handle *dir_hdl,	/* IN */
			    const char *p_filename,	/* IN */
			    const struct req_op_context *p_context, /* IN */
			    uint32_t accessmode,	/* IN */
			    ptfsal_handle_t *p_object_handle,	/* OUT */
			    struct attrlist *p_object_attributes); /* IN/OUT */
fsal_status_t PTFSAL_mkdir(struct fsal_obj_handle *dir_hdl,	/* IN */
			   const char *p_dirname,	/* IN */
			   const struct req_op_context *p_context, /* IN */
			   uint32_t accessmode,	/* IN */
			   ptfsal_handle_t *p_object_handle,	/* OUT */
			   struct attrlist *p_object_attributes); /* IN/OUT */
fsal_status_t PTFSAL_mknode(struct fsal_obj_handle *dir_hdl,	/* IN */
			    const char *p_node_name,	/* IN */
			    const struct req_op_context *p_context, /* IN */
			    uint32_t accessmode,	/* IN */
			    mode_t nodetype,	/* IN */
			    fsal_dev_t *dev,	/* IN */
			    ptfsal_handle_t *p_object_handle,	/* OUT */
			    struct attrlist *node_attributes);
fsal_status_t PTFSAL_link(struct fsal_obj_handle *destdir_hdl,	/* IN */
			  ptfsal_handle_t *target_handle,	/* IN */
			  const char *p_link_name,	/* IN */
			  const struct req_op_context *p_context, /* IN */
			  struct attrlist *p_attributes);
fsal_status_t PTFSAL_opendir(ptfsal_handle_t *p_dir_handle,	/* IN */
			     int dirfd,	/* IN */
			     int *p_dir_descriptor,	/* OUT */
			     struct attrlist *p_dir_attributes); /* IN/OUT */

fsal_status_t PTFSAL_closedir(int *p_dir_descriptor /* IN */);

fsal_status_t PTFSAL_open_by_name(ptfsal_handle_t *dirhandle,	/* IN */
				  const char *filename,	/* IN */
				  int dirfd,	/* IN */
				  fsal_openflags_t openflags,	/* IN */
				  int *file_descriptor,	/* OUT */
				  struct attrlist *file_attributes);
				  /* IN/OUT */

fsal_status_t PTFSAL_open(struct fsal_obj_handle *obj_hdl,	/* IN */
			  const struct req_op_context *p_context,
			  fsal_openflags_t openflags,	/* IN */
			  int *p_file_descriptor,	/* OUT */
			  struct attrlist *p_file_attributes);	/* IN/OUT */

fsal_status_t PTFSAL_read(struct pt_fsal_obj_handle *p_file_descriptor,/* IN */
			  const struct req_op_context *opctx,
			  uint64_t offset,	/* [IN] */
			  size_t buffer_size,	/* IN */
			  caddr_t buffer,	/* OUT */
			  size_t *p_read_amount,	/* OUT */
			  bool *p_end_of_file);	/* OUT */

fsal_status_t
PTFSAL_write(struct pt_fsal_obj_handle *p_file_descriptor,
	     const struct req_op_context *opctx,
	     uint64_t offset,	/* IN */
	     size_t buffer_size,	/* IN */
	     caddr_t buffer,	/* IN */
	     size_t *p_write_amount,	/* OUT */
	     bool *fsal_stable);	/* IN/OUT */

fsal_status_t PTFSAL_close(int p_file_descriptor);	/* IN */

fsal_status_t PTFSAL_dynamic_fsinfo(ptfsal_handle_t *p_handle,	/* IN */
				    int dirfd,	/* IN */
				    fsal_dynamicfsinfo_t *p_dynamicinfo);
				    /* OUT */

fsal_status_t PTFSAL_test_access(int dirfd,	/* IN */
				 fsal_accessflags_t access_type, /* IN */
				 struct attrlist *p_object_attributes /* IN */
				);

fsal_status_t PTFSAL_terminate();

fsal_status_t PTFSAL_lookup(const struct req_op_context *p_context, /* IN */
			    struct fsal_obj_handle *parent,
			    const char *p_filename,
			    struct attrlist *p_object_attr,
			    ptfsal_handle_t *fh);

fsal_status_t PTFSAL_lookupPath(const char *p_path,	/* IN */
				int dirfd,	/* IN */
				ptfsal_handle_t *object_handle,	/* OUT */
				struct attrlist *p_object_attributes);
				/* IN/OUT */

fsal_status_t PTFSAL_lookupJunction(ptfsal_handle_t *p_handle,	/* IN */
				    int dirfd,	/* IN */
				    ptfsal_handle_t *fsoot_hdl,	/* OUT */
				    struct attrlist *p_fsroot_attributes);
				    /* IN/OUT */

fsal_status_t PTFSAL_lock_op(struct fsal_obj_handle *obj_hdl,	/* IN */
			     void *p_owner,	/* IN */
			     fsal_lock_op_t lock_op,	/* IN */
			     fsal_lock_param_t request_lock,	/* IN */
			     fsal_lock_param_t *conflicting_lock);  /* OUT */

fsal_status_t PTFSAL_share_op(int mntfd,	/* IN */
			      int fd,	/* IN */
			      void *p_owner,	/* IN */
			      fsal_share_param_t request_share); /* IN */

fsal_status_t PTFSAL_rcp(ptfsal_handle_t *filehandle,	/* IN */
			 int dirfd,	/* IN */
			 const char *p_local_path,	/* IN */
			 int transfer_opt /* IN */);

fsal_status_t PTFSAL_rename(struct fsal_obj_handle *old_hdl,	/* IN */
			    const char *p_old_name,	/* IN */
			    struct fsal_obj_handle *new_hdl,	/* IN */
			    const char *p_new_name,	/* IN */
			    const struct req_op_context *p_context); /* IN */

fsal_status_t PTFSAL_readlink(struct fsal_obj_handle *dir_hdl,	/* IN */
			      const struct req_op_context *p_context, /* IN */
			      char *p_link_content,	/* OUT */
			      size_t *link_len,	/* IN/OUT */
			      struct attrlist *p_link_attributes); /* IN/OUT */

fsal_status_t PTFSAL_symlink(struct fsal_obj_handle *dir_hdl,	/* IN */
			     const char *p_linkname,	/* IN */
			     const char *p_linkcontent,	/* IN */
			     const struct req_op_context *p_context, /* IN */
			     uint32_t accessmode,	/* IN (ignored) */
			     ptfsal_handle_t *p_link_handle,	/* OUT */
			     struct attrlist *p_link_attributes); /* IN/OUT */

int PTFSAL_handlecmp(ptfsal_handle_t *handle1, ptfsal_handle_t *handle2,
		     fsal_status_t *status);

unsigned int PTFSAL_Handle_to_HashIndex(ptfsal_handle_t *p_handle,
					unsigned int cookie,
					unsigned int alphabet_len,
					unsigned int index_size);

unsigned int PTFSAL_Handle_to_RBTIndex(ptfsal_handle_t *p_handle,
				       unsigned int cookie);

fsal_status_t PTFSAL_truncate(struct fsal_export *export,	/* IN */
			      struct pt_fsal_obj_handle *p_filehandle, /* IN */
			      const struct req_op_context *p_context, /* IN */
			      size_t length,	/* IN */
			      struct attrlist *p_object_attributes);
			      /* IN/OUT */

fsal_status_t PTFSAL_unlink(struct fsal_obj_handle *dir_hdl,	/* IN */
			    const char *p_object_name,	/* IN */
			    const struct req_op_context *p_context,	/* IN */
			    struct attrlist *p_parent_attributes); /* IN/OUT */

char *PTFSAL_GetFSName();

fsal_status_t PTFSAL_GetXAttrAttrs(ptfsal_handle_t *obj_hdl,	/* IN */
				   int dirfd,	/* IN */
				   unsigned int xattr_id,	/* IN */
				   struct attrlist *p_attrs);

fsal_status_t PTFSAL_ListXAttrs(ptfsal_handle_t *obj_hdl,	/* IN */
				unsigned int cookie,	/* IN */
				int dirfd,	/* IN */
				fsal_xattrent_t *xattrs_tab,	/* IN/OUT */
				unsigned int xattrs_tabsize,	/* IN */
				unsigned int *p_nb_returned,	/* OUT */
				int *end_of_list);	/* OUT */

fsal_status_t PTFSAL_GetXAttrValueById(ptfsal_handle_t *objhdl,	/* IN */
				       unsigned int xattr_id,	/* IN */
				       int dirfd,	/* IN */
				       caddr_t buffer_addr,	/* IN/OUT */
				       size_t buffer_size,	/* IN */
				       size_t *p_output_size);	/* OUT */

fsal_status_t PTFSAL_GetXAttrIdByName(ptfsal_handle_t *objhdl,	/* IN */
				      const char *xattr_name,	/* IN */
				      int dirfd,	/* IN */
				      unsigned int *pxattr_id);	/* OUT */

fsal_status_t PTFSAL_GetXAttrValueByName(ptfsal_handle_t *objhdl,/* IN */
					 const char *xattr_name, /* IN */
					 int dirfd,	/* IN */
					 caddr_t buffer_addr,	/* IN/OUT */
					 size_t buffer_size,	/* IN */
					 size_t *p_output_size);/* OUT */

fsal_status_t PTFSAL_SetXAttrValue(ptfsal_handle_t *obj_hdl,	/* IN */
				   const char *xattr_name,	/* IN */
				   int dirfd,	/* IN */
				   caddr_t buffer_addr,	/* IN */
				   size_t buffer_size,	/* IN */
				   int create);	/* IN */

fsal_status_t PTFSAL_RemoveXAttrByName(ptfsal_handle_t *objhdl,	/* IN */
				       int dirfd,	/* IN */
				       const char *xattr_name);	/* IN */

unsigned int PTFSAL_GetFileno(int pfile);

fsal_status_t PTFSAL_commit(struct pt_fsal_obj_handle *p_file_descriptor,
			    const struct req_op_context *opctx,
			    uint64_t offset, size_t size);

#endif				/* FSAL_INTERNAL_H */
