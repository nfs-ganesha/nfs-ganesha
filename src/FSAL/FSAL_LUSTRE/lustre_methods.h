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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 *
 * \file    lustre_methods.h
 * \version Revision: 1.01
 * \brief   Lustre-specific definitions usually found in fsal_internal.h.
 *
 */

#ifndef _LUSTRE_METHODS_H
#define _LUSTRE_METHODS_H

extern char *exec_name;

/* LUSTRE methods for handles
 */

/*
 *  * Macro to deal with operation made with creds
 *   */
#define CRED_WRAP(__creds, __rc_type, __function, ...) ({  \
			fsal_set_credentials(__creds);                   \
			__rc_type __local_rc = __function(__VA_ARGS__);  \
			fsal_restore_ganesha_credentials();              \
			__local_rc; })
/* private helpers from export
 */

struct fsal_staticfsinfo_t *lustre_staticinfo(struct fsal_module *hdl);

struct lustre_exp_pnfs_parameter {
	unsigned int stripe_unit;
	bool pnfs_enabled;
};

/*
 * LUSTRE internal export
 */
struct lustre_fsal_export {
	struct fsal_export export;
	struct fsal_filesystem *root_fs;
	struct glist_head filesystems;
	bool pnfs_ds_enabled;
	bool pnfs_mds_enabled;
	struct lustre_exp_pnfs_parameter pnfs_param;
};

/*
 * LUSTRE internal filesystem
 */
struct lustre_filesystem {
	char *fsname;
	struct fsal_filesystem *fs;
	struct glist_head exports;
	bool up_thread_started;
	const struct fsal_up_vector *up_ops;
	pthread_t up_thread; /* upcall thread */
};

/* this needs to be refactored to put ipport inside sockaddr_in */
struct lustre_pnfs_ds_parameter {
	struct glist_head ds_list;
	struct sockaddr_in ipaddr;
	unsigned short ipport;
	unsigned int id;
};

struct lustre_pnfs_parameter {
	struct glist_head ds_list;
};

/* LUSTRE FSAL module private storage
 *  */

struct lustre_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	struct lustre_pnfs_parameter pnfs_param;
};

/*
 * Link LUSTRE file systems and exports
 * Supports a many-to-many relationship
 */
struct lustre_filesystem_export_map {
	struct lustre_fsal_export *exp;
	struct lustre_filesystem *fs;
	struct glist_head on_exports;
	struct glist_head on_filesystems;
};

/* Internal LUSTRE method linkage to export object
 */

fsal_status_t lustre_create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops);

void lustre_unexport_filesystems(struct lustre_fsal_export *exp);

/* method proto linkage to handle.c for export
 */

fsal_status_t lustre_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle);

fsal_status_t lustre_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle);

/*
 * LUSTRE internal object handle
 * handle is a pointer because
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

struct lustre_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct attrlist attributes;
	fsal_dev_t dev;
	struct lustre_file_handle *handle;
	union {
		struct {
			int fd;
			fsal_openflags_t openflags;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
		struct {
			struct lustre_file_handle *sock_dir;
			char *sock_name;
		} sock;
	} u;
};

	/* I/O management */
fsal_status_t lustre_open(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags);
fsal_openflags_t lustre_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t lustre_read(struct fsal_obj_handle *obj_hdl,
			  uint64_t offset,
			  size_t buffer_size, void *buffer,
			  size_t *read_amount, bool *end_of_file);
fsal_status_t lustre_write(struct fsal_obj_handle *obj_hdl,
			   uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable);
fsal_status_t lustre_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len);
fsal_status_t lustre_lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock);
fsal_status_t lustre_share_op(struct fsal_obj_handle *obj_hdl,
			      void *p_owner,
			      fsal_share_param_t request_share);
fsal_status_t lustre_close(struct fsal_obj_handle *obj_hdl);

/* extended attributes management */
fsal_status_t lustre_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int cookie,
				    fsal_xattrent_t *xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list);
fsal_status_t lustre_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   unsigned int *pxattr_id);
fsal_status_t lustre_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const char *xattr_name,
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size);
fsal_status_t lustre_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t lustre_setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      caddr_t buffer_addr, size_t buffer_size,
				      int create);
fsal_status_t lustre_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size);
fsal_status_t lustre_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				      unsigned int xattr_id,
				      struct attrlist *p_attrs);
fsal_status_t lustre_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id);
fsal_status_t lustre_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name);

#endif /* _LUSTRE_METHODS_H */
