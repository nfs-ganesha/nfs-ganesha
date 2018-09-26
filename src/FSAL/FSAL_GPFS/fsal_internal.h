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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 *
 * @file    fsal_internal.h
 * @brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 */
#ifndef FSAL_INTERNAL_H
#define FSAL_INTERNAL_H

#include <sys/stat.h>
#include "fsal.h"
#include "gsh_list.h"
#include "fsal_types.h"
#include "fcntl.h"
#include "include/gpfs_nfs.h"
#include "include/gpfs.h"
#include "fsal_up.h"
#include "gsh_config.h"
#include "FSAL/fsal_commonlib.h"

struct gpfs_filesystem;

/*
 * GPFS internal module
 */
struct gpfs_fsal_module {
	struct fsal_module module;
	struct fsal_obj_ops handle_ops;
	struct fsal_obj_ops handle_ops_with_pnfs;
};

extern struct gpfs_fsal_module GPFS;

void gpfs_handle_ops_init(struct fsal_obj_ops *ops);

bool fsal_error_is_event(fsal_status_t status);
/*
 * Tests whether an error code should be raised as an info debug.
 */
bool fsal_error_is_info(fsal_status_t status);

/**
 * The full, 'private' DS (data server) handle
 */

struct gpfs_ds {
	struct fsal_ds_handle ds;	/*< Public DS handle */
	struct gpfs_file_handle wire;	/*< Wire data */
	struct gpfs_filesystem *gpfs_fs; /*< filesystem handle belongs to */
	bool connected;		/*< True if the handle has been connected */
};

struct gpfs_fd {
	/** The open and share mode etc. */
	fsal_openflags_t openflags;
	/* rw lock to protect the file descriptor */
	pthread_rwlock_t fdlock;
	/** The gpfsfs file descriptor. */
	int fd;
};

struct gpfs_state_fd {
	struct state_t state;
	struct gpfs_fd gpfs_fd;
};

/* defined the set of attributes supported with POSIX */
#define GPFS_SUPPORTED_ATTRIBUTES ((const attrmask_t) (         \
		ATTRS_POSIX | ATTR_ACL | ATTR4_SPACE_RESERVED | \
		ATTR4_FS_LOCATIONS | ATTR4_XATTR))

#define GPFS_MAX_FH_SIZE OPENHANDLE_HANDLE_LEN

/* Define the buffer size for GPFS NFS4 ACL. */
#define GPFS_ACL_BUF_SIZE 0x1000
#define GPFS_ACL_MAX_RETRY 10

/* Define the standard fsid_type for GPFS*/
#define GPFS_FSID_TYPE FSID_MAJOR_64

/* A set of buffers to retrieve multiple attributes at the same time. */
typedef struct fsal_xstat__ {
	int attr_valid;
	struct stat buffstat;
	fsal_fsid_t fsal_fsid;
	char buffacl[GPFS_ACL_BUF_SIZE];
} gpfsfsal_xstat_t;

static inline size_t gpfs_sizeof_handle(const struct gpfs_file_handle *hdl)
{
	return hdl->handle_size;
}

void export_ops_init(struct export_ops *ops);
void handle_ops_init(struct fsal_obj_ops *ops);
void pnfs_ds_ops_init(struct fsal_pnfs_ds_ops *ops);
void export_ops_pnfs(struct export_ops *ops);
void handle_ops_pnfs(struct fsal_obj_ops *ops);

fsal_status_t fsal_internal_close(int fd, void *owner, int cflags);

int fsal_internal_version(void);

fsal_status_t fsal_internal_get_handle_at(int dfd,
				const char *p_fsalname,
				struct gpfs_file_handle *p_handle,
				int expfd);

fsal_status_t gpfsfsal_xstat_2_fsal_attributes(gpfsfsal_xstat_t *gpfs_buf,
		struct attrlist *fsal_attr, gpfs_acl_t *acl_buf, bool use_acl);

fsal_status_t
fsal_internal_handle2fd(int dirfd, struct gpfs_file_handle *phandle,
			int *pfd, int oflags);
/**
 * Gets a file handle from a parent handle and name
 */
fsal_status_t fsal_internal_get_fh(int dirfd,
				   struct gpfs_file_handle *p_dir_handle,
				   const char *p_fsalname,
				   struct gpfs_file_handle *p_handle);
/**
 * Access a link by a file handle.
 */
fsal_status_t fsal_readlink_by_handle(int dirfd,
				      struct gpfs_file_handle *p_handle,
				      char *__buf, size_t maxlen);

/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_fd2handle(int fd,
				struct gpfs_file_handle *p_handle);

fsal_status_t fsal_internal_link_at(int srcfd, int dfd, char *name);

fsal_status_t fsal_internal_link_fh(int dirfd,
				    struct gpfs_file_handle *p_target_handle,
				    struct gpfs_file_handle *p_dir_handle,
				    const char *p_link_name);

fsal_status_t fsal_internal_stat_name(int dirfd,
				      struct gpfs_file_handle *p_dir_handle,
				      const char *p_stat_name,
				      struct stat *buf);

fsal_status_t fsal_internal_unlink(int dirfd,
				   struct gpfs_file_handle *p_dir_handle,
				   const char *p_stat_name, struct stat *buf);

fsal_status_t fsal_internal_create(struct fsal_obj_handle *dir_hdl,
				   const char *p_stat_name, mode_t mode,
				   int posix_flags,
				   struct gpfs_file_handle *p_new_handle,
				   struct stat *buf);

fsal_status_t fsal_internal_mknode(struct fsal_obj_handle *dir_hdl,
				   const char *p_stat_name, mode_t mode,
				   dev_t dev,
				   struct gpfs_file_handle *p_new_handle,
				   struct stat *buf);

fsal_status_t fsal_internal_rename_fh(int dirfd,
				      struct gpfs_file_handle *p_old_handle,
				      struct gpfs_file_handle *p_new_handle,
				      const char *p_old_name,
				      const char *p_new_name);

fsal_status_t fsal_get_xstat_by_handle(int dirfd,
				       struct gpfs_file_handle *p_handle,
				       gpfsfsal_xstat_t *p_buffxstat,
				       gpfs_acl_t *acl_buf,
				       unsigned int acl_buflen,
				       uint32_t *expire_time_attr,
				       bool expire, bool use_acl);

fsal_status_t fsal_set_xstat_by_handle(int dirfd,
				       const struct req_op_context *p_context,
				       struct gpfs_file_handle *p_handle,
				       int attr_valid, int attr_changed,
				       gpfsfsal_xstat_t *p_buffxstat,
				       gpfs_acl_t *acl_buf);

fsal_status_t fsal_trucate_by_handle(int dirfd,
				     const struct req_op_context *p_context,
				     struct gpfs_file_handle *p_handle,
				     u_int64_t size);

/* All the call to FSAL to be wrapped */

fsal_status_t GPFSFSAL_getattrs(struct fsal_export *export,
				struct gpfs_filesystem *gpfs_fs,
				const struct req_op_context *p_context,
				struct gpfs_file_handle *p_filehandle,
				struct attrlist *p_object_attributes);

fsal_status_t GPFSFSAL_fs_loc(struct fsal_export *export,
			      struct gpfs_filesystem *gpfs_fs,
			      const struct req_op_context *p_context,
			      struct gpfs_file_handle *p_filehandle,
			      struct attrlist *attrs);

fsal_status_t GPFSFSAL_statfs(int fd,
			      struct fsal_obj_handle *obj_hdl,
			      struct statfs *buf);

fsal_status_t GPFSFSAL_setattrs(struct fsal_obj_handle *dir_hdl,
				const struct req_op_context *p_context,
				struct attrlist *p_object_attributes);

fsal_status_t GPFSFSAL_create(struct fsal_obj_handle *dir_hdl,
			      const char *p_filename,
			      const struct req_op_context *p_context,
			      uint32_t accessmode,
			      struct gpfs_file_handle *p_object_handle,
			      struct attrlist *p_object_attributes);

fsal_status_t GPFSFSAL_create2(struct fsal_obj_handle *dir_hdl,
			      const char *p_filename,
			      const struct req_op_context *p_context,
			      mode_t unix_mode,
			      struct gpfs_file_handle *p_object_handle,
			      int posix_flags,
			      struct attrlist *p_object_attributes);

fsal_status_t GPFSFSAL_mkdir(struct fsal_obj_handle *dir_hdl,
			     const char *p_dirname,
			     const struct req_op_context *p_context,
			     uint32_t accessmode,
			     struct gpfs_file_handle *p_object_handle,
			     struct attrlist *p_object_attributes);

fsal_status_t GPFSFSAL_link(struct fsal_obj_handle *dir_hdl,
			    struct gpfs_file_handle *p_target_handle,
			    const char *p_link_name,
			    const struct req_op_context *p_context);

fsal_status_t GPFSFSAL_mknode(struct fsal_obj_handle *dir_hdl,
			      const char *p_node_name,
			      const struct req_op_context *p_context,
			      uint32_t accessmode,
			      mode_t nodetype,
			      fsal_dev_t *dev,
			      struct gpfs_file_handle *p_object_handle,
			      struct attrlist *node_attributes);

fsal_status_t GPFSFSAL_open(struct fsal_obj_handle *obj_hdl,
			    const struct req_op_context *p_context,
			    int posix_flags, int *p_file_descriptor);

fsal_status_t GPFSFSAL_read(int fd,
			    uint64_t offset,
			    size_t buffer_size,
			    void *buffer,
			    size_t *p_read_amount,
			    bool *p_end_of_file,
			    int expfs);

fsal_status_t GPFSFSAL_write(int fd,
			     uint64_t offset,
			     size_t buffer_size,
			     void *buffer,
			     size_t *p_write_amount,
			     bool *fsal_stable,
			     const struct req_op_context *p_context,
			     int expfs);

fsal_status_t GPFSFSAL_alloc(int fd,
			     uint64_t offset,
			     uint64_t length,
			     bool options);

fsal_status_t GPFSFSAL_lookup(const struct req_op_context *p_context,
			      struct fsal_obj_handle *parent,
			      const char *p_filename,
			      struct attrlist *p_object_attr,
			      struct gpfs_file_handle *fh,
			      struct fsal_filesystem **new_fs);

fsal_status_t GPFSFSAL_lock_op(struct fsal_export *export,
			       fsal_lock_op_t lock_op,
			       fsal_lock_param_t *req_lock,
			       fsal_lock_param_t *confl_lock,
			       struct set_get_lock_arg *sg_lock_arg);

fsal_status_t GPFSFSAL_rename(struct fsal_obj_handle *old_hdl,
			      const char *p_old_name,
			      struct fsal_obj_handle *new_hdl,
			      const char *p_new_name,
			      const struct req_op_context *p_context);

fsal_status_t GPFSFSAL_readlink(struct fsal_obj_handle *dir_hdl,
				const struct req_op_context *p_context,
				char *p_link_content,
				size_t link_len);

fsal_status_t GPFSFSAL_symlink(struct fsal_obj_handle *dir_hdl,
			       const char *p_linkname,
			       const char *p_linkcontent,
			       const struct req_op_context *p_context,
			       uint32_t accessmode,	/* IN (ignored) */
			       struct gpfs_file_handle *p_link_handle,
			       struct attrlist *p_link_attributes);

fsal_status_t GPFSFSAL_unlink(struct fsal_obj_handle *dir_hdl,
			      const char *p_object_name,
			      const struct req_op_context *p_context);

void *GPFSFSAL_UP_Thread(void *Arg);

size_t fs_da_addr_size(struct fsal_module *fsal_hdl);

nfsstat4 getdeviceinfo(struct fsal_module *fsal_hdl,
		       XDR *da_addr_body, const layouttype4 type,
		       const struct pnfs_deviceid *deviceid);

void fsal_gpfs_extract_stats(struct fsal_module *fsal_hdl, void *iter);

void fsal_gpfs_reset_stats(struct fsal_module *fsal_hdl);

void prepare_for_stats(struct fsal_module *fsal_hdl);

int gpfs_op2index(int op);
#endif
