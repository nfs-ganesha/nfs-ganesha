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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* FSAL API
 * object oriented fsal api.
 */

/** VERSIONING RULES
 *
 * One intent in this API is to be able to support fsals that are built
 * out-of-tree and possibly out of synch with the core of Ganesha.  This
 * is managed by version numbers in this file that are validated at load
 * time for the fsal.  There are major and minor version numbers which are
 * monotonically increasing numbers ( V1 < V2 means V2 is newer).
 *
 * API guarantee:
 *
 * * If major version numbers differ, the fsal will not be loaded because
 *   the api has changed enough to make it unsafe.
 *
 * * If the major versions are equal, the minor version determines loadability.
 *
 *   - A fsal that is older than the Ganesha core can safely load and run.
 *
 *   - A fsal that is newer than the Ganesha core is not safe and will not
 *     be loaded.
 */

/* Major Version
 * Increment this whenever any part of the existing API is changed, e.g.
 * the argument list changed or a method is removed.
 */

#define FSAL_MAJOR_VERSION 1

/* Minor Version
 * Increment this whenever a new method is appended to the ops vector.  The
 * remainder of the API is unchanged.
 *
 * If the major version is incremented, reset the minor to 0 (zero).
 *
 * If new members are appended to struct req_op_context (following its own
 * rules), increment the minor version
 */

#define FSAL_MINOR_VERSION 0

/* forward references for object methods
 */

struct fsal_export;
struct export_ops;
struct fsal_obj_handle;
struct fsal_obj_ops;
struct exportlist__; /* we just need a pointer, not all of nfs_exports.h
		      * full def in include/nfs_exports.h
		      */

/* fsal manager
 */

/* fsal object definition
 * base of fsal instance definition
 */

struct fsal_module {
	struct glist_head fsals;	/* list of loaded fsals */
	pthread_mutex_t lock;
	volatile int refs;
	struct glist_head exports;	/* list of exports from this fsal */
	char *name;			/* name set from .so and/or config */
	char *path;			/* path to .so file */
	void *dl_handle;		/* NULL if statically linked */
	struct fsal_ops *ops;		/* fsal module methods vector */
	struct export_ops *exp_ops;	/* shared export object methods vector */
	struct fsal_obj_ops *obj_ops;   /* shared handle methods vector */
};

/* fsal module methods */

struct fsal_ops {
	/* base methods implemented in fsal_manager.c */
	int (*unload)(struct fsal_module *fsal_hdl);
	const char *(*get_name)(struct fsal_module *fsal_hdl);
	const char *(*get_lib_name)(struct fsal_module *fsal_hdl);
	int (*put)(struct fsal_module *fsal_hdl);
	/* subclass/instance methods in each fsal */
	fsal_status_t (*init_config)(struct fsal_module *fsal_hdl,
				     config_file_t config_struct);
	void (*dump_config)(struct fsal_module *fsal_hdl,
			    int log_fd);
	fsal_status_t (*create_export)(struct fsal_module *fsal_hdl,
				       const char *export_path,
				       const char *fs_options,
				       struct exportlist__ *exp_entry,
				       struct fsal_module *next_fsal,
				       /* upcall vector */
				       struct fsal_export **export);
};

/* global fsal manager functions
 * used by nfs_main to initialize fsal modules */

int start_fsals(config_file_t config);
int load_fsal(const char *path,
	      const char *name,
	      struct fsal_module **fsal_hdl);
int init_fsals(config_file_t config);

/* Called only within MODULE_INIT and MODULE_FINI functions
 * of a fsal module
 */

int register_fsal(struct fsal_module *fsal_hdl,
		  const char *name,
		  uint32_t major_version,
		  uint32_t minor_version);
int unregister_fsal(struct fsal_module *fsal_hdl);

/* find and take a reference on a fsal
 * part of export setup.  Call the 'put' to release
 * your reference before unloading.
 */

struct fsal_module *lookup_fsal(const char *name);

/* export object
 * Created by fsal and referenced by the export list
 */

struct fsal_export {
	struct fsal_module *fsal;
	pthread_mutex_t lock;
	volatile int refs;
	struct glist_head handles;	/* list of obj handles still active */
	struct glist_head exports;
	struct exportlist__ *exp_entry; /* NYI points back to exp list */
	struct export_ops *ops;
};

struct export_ops {
	/* export management */
	void (*get)(struct fsal_export *exp_hdl);
	int (*put)(struct fsal_export *exp_hdl);
	fsal_status_t (*release)(struct fsal_export *exp_hdl);

	/* create an object handle within this export */
	fsal_status_t (*lookup_path)(struct fsal_export *exp_hdl,
				     const char *path,
				     struct fsal_obj_handle **handle);
	fsal_status_t (*lookup_junction)(struct fsal_export *exp_hdl,
				struct fsal_obj_handle *junction,
				struct fsal_obj_handle **handle);
	fsal_status_t (*extract_handle)(struct fsal_export *exp_hdl,
                                        fsal_digesttype_t in_type,
                                        struct netbuf *fh_desc);
	fsal_status_t (*create_handle)(struct fsal_export *exp_hdl,
				       struct gsh_buffdesc *hdl_desc,
				       struct fsal_obj_handle **handle);

	/* statistics and configuration access */
	fsal_status_t (*get_fs_dynamic_info)(struct fsal_export *exp_hdl,
					     fsal_dynamicfsinfo_t *infop);
	bool_t (*fs_supports)(struct fsal_export *exp_hdl,
				      fsal_fsinfo_options_t option);
	uint64_t (*fs_maxfilesize)(struct fsal_export *exp_hdl);
	uint32_t (*fs_maxread)(struct fsal_export *exp_hdl);
	uint32_t (*fs_maxwrite)(struct fsal_export *exp_hdl);
	uint32_t (*fs_maxlink)(struct fsal_export *exp_hdl);
	uint32_t (*fs_maxnamelen)(struct fsal_export *exp_hdl);
	uint32_t (*fs_maxpathlen)(struct fsal_export *exp_hdl);
	fsal_fhexptype_t (*fs_fh_expire_type)(struct fsal_export *exp_hdl);
	gsh_time_t (*fs_lease_time)(struct fsal_export *exp_hdl);
	fsal_aclsupp_t (*fs_acl_support)(struct fsal_export *exp_hdl);
	attrmask_t (*fs_supported_attrs)(struct fsal_export *exp_hdl);
	uint32_t (*fs_umask)(struct fsal_export *exp_hdl);
	uint32_t (*fs_xattr_access_rights)(struct fsal_export *exp_hdl);
	/* quotas are managed at the file system (export) level */
	fsal_status_t (*check_quota)(struct fsal_export *exp_hdl,
				   const char * filepath,
				   int quota_type,
				   struct req_op_context *req_ctx);
	fsal_status_t (*get_quota)(struct fsal_export *exp_hdl,
				   const char * filepath,
				   int quota_type,
				   struct req_op_context *req_ctx,
				   fsal_quota_t * pquota);
	fsal_status_t (*set_quota)(struct fsal_export *exp_hdl,
				   const char * filepath,
				   int quota_type,
				   struct req_op_context *req_ctx,
				   fsal_quota_t * pquota,
				   fsal_quota_t * presquota);
};

/* filesystem object
 * used for files of all types including directories, anything
 * that has a usable handle.
 */

struct fsal_obj_handle {
	pthread_mutex_t lock;
	struct glist_head handles;
	int refs;
	object_file_type_t type;
	struct fsal_export *export;	/* export who created me */
	struct attrlist attributes;  /* used to be in cache_entry */
	struct fsal_obj_ops *ops;
};

/* this cookie gets allocated at cache_inode_dir_entry create time
 * to the size specified by size.  It is at the end of the dir entry
 * so the cookie[] allocation can expand as needed.
 * However, given how GetFromPool works, these have to be fixed size
 * as a result, we go for a V4 handle size for things like proxy until
 * we can fix this. It is a crazy waste of space.  Make this go away with
 * a fixing of GetFromPool.  For now, make it big enough to hold a SHA1...
 * Also note that the readdir code doesn't have to check this (both are hard
 * coded) so long as they obey the proper setting of size.
 */

#define FSAL_READDIR_COOKIE_MAX 40
struct fsal_cookie {
	int size;
	unsigned char cookie[FSAL_READDIR_COOKIE_MAX];
};

struct fsal_obj_ops {
	/* object handle reference management */
	void (*get)(struct fsal_obj_handle *obj_hdl);
	int (*put)(struct fsal_obj_handle *obj_hdl);
	fsal_status_t (*release)(struct fsal_obj_handle *obj_hdl);

	/*directory operations */
	fsal_status_t (*lookup)(struct fsal_obj_handle *dir_hdl,
				const char *path,
				struct fsal_obj_handle **handle);
	fsal_status_t (*readdir)(struct fsal_obj_handle *dir_hdl,
				 uint32_t entry_cnt,
				 struct fsal_cookie *whence,
				 void *dir_state,
				 fsal_status_t (*cb)(
					 const char *name,
					 unsigned int dtype,
					 struct fsal_obj_handle *dir_hdl,
					 void *dir_state,
					 struct fsal_cookie *cookie),
				 bool_t *eof);
	fsal_status_t (*create)(struct fsal_obj_handle *dir_hdl,
				const char *name,
				struct attrlist *attrib,
				struct fsal_obj_handle **new_obj);
	fsal_status_t (*mkdir)(struct fsal_obj_handle *dir_hdl,
			       const char *name,
			       struct attrlist *attrib,
			       struct fsal_obj_handle **new_obj);
	fsal_status_t (*mknode)(struct fsal_obj_handle *dir_hdl,
				const char *name,
				object_file_type_t nodetype,
				fsal_dev_t *dev,
				struct attrlist *attrib,
				struct fsal_obj_handle **new_obj);
	fsal_status_t (*symlink)(struct fsal_obj_handle *dir_hdl,
				 const char *name,
				 const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **new_obj);
	fsal_status_t (*readlink)(struct fsal_obj_handle *obj_hdl,
				  char *link_content,
				  size_t *link_len,
				  bool_t refresh);

	/* file object operations */
	fsal_status_t (*test_access)(struct fsal_obj_handle *obj_hdl,
				     struct req_op_context *req_ctx,
				     fsal_accessflags_t access_type);
	fsal_status_t (*getattrs)(struct fsal_obj_handle *obj_hdl,
				  struct attrlist *obj_attr);
	fsal_status_t (*setattrs)(struct fsal_obj_handle *obj_hdl,
				  struct attrlist *attrib_set);
	fsal_status_t (*link)(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name);
	fsal_status_t (*rename)(struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name);
	fsal_status_t (*unlink)(struct fsal_obj_handle *obj_hdl,
				const char *name);
	fsal_status_t (*truncate)(struct fsal_obj_handle *obj_hdl,
				  uint64_t length);

	/* I/O management */
	fsal_status_t (*open)(struct fsal_obj_handle *obj_hdl,
			      fsal_openflags_t openflags);
	fsal_openflags_t (*status)(struct fsal_obj_handle *obj_hdl);
	fsal_status_t (*read)(struct fsal_obj_handle *obj_hdl,
			      uint64_t offset,
			      size_t buffer_size,
			      void *buffer,
			      size_t *read_amount,
			      bool_t *end_of_file); /* needed? */
	fsal_status_t (*write)(struct fsal_obj_handle *obj_hdl,
                               uint64_t offset,
			       size_t buffer_size,
			       void *buffer,
                               size_t *write_amount);
	fsal_status_t (*commit)(struct fsal_obj_handle *obj_hdl, /* sync */
				off_t offset,
				size_t len);
	fsal_status_t (*lock_op)(struct fsal_obj_handle *obj_hdl,
				 void * p_owner,
				 fsal_lock_op_t lock_op,
				 fsal_lock_param_t *request_lock,
				 fsal_lock_param_t *conflicting_lock);
	fsal_status_t (*share_op)(struct fsal_obj_handle *obj_hdl,
				  void *p_owner,         /* IN (opaque to FSAL) */
				  fsal_share_param_t  request_share);
	fsal_status_t (*close)(struct fsal_obj_handle *obj_hdl);

	/* extended attributes management */
	fsal_status_t (*list_ext_attrs)(struct fsal_obj_handle *obj_hdl,
					unsigned int cookie,
					fsal_xattrent_t * xattrs_tab,
					unsigned int xattrs_tabsize,
					unsigned int *p_nb_returned,
					int *end_of_list);
	fsal_status_t (*getextattr_id_by_name)(struct fsal_obj_handle *obj_hdl,
					       const char *xattr_name,
					       unsigned int *pxattr_id);
	fsal_status_t (*getextattr_value_by_name)(struct fsal_obj_handle *obj_hdl,
						  const char *xattr_name,
						  caddr_t buffer_addr,
						  size_t buffer_size,
						  size_t * p_output_size);
	fsal_status_t (*getextattr_value_by_id)(struct fsal_obj_handle *obj_hdl,
						unsigned int xattr_id,
						caddr_t buffer_addr,
						size_t buffer_size,
						size_t *p_output_size);
	fsal_status_t (*setextattr_value)(struct fsal_obj_handle *obj_hdl,
					  const char *xattr_name,
					  caddr_t buffer_addr,
					  size_t buffer_size,
					  int create);
	fsal_status_t (*setextattr_value_by_id)(struct fsal_obj_handle *obj_hdl,
						unsigned int xattr_id,
						caddr_t buffer_addr,
						size_t buffer_size);
	fsal_status_t (*getextattr_attrs)(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id,
					  struct attrlist *p_attrs);
	fsal_status_t (*remove_extattr_by_id)(struct fsal_obj_handle *obj_hdl,
					      unsigned int xattr_id);
	fsal_status_t (*remove_extattr_by_name)(struct fsal_obj_handle *obj_hdl,
						const char *xattr_name);

	/* handle operations */
	bool_t (*handle_is)(struct fsal_obj_handle *obj_hdl,
                            object_file_type_t type);
	fsal_status_t (*lru_cleanup)(struct fsal_obj_handle *obj_hdl,
				     lru_actions_t requests);
	bool_t (*compare)(struct fsal_obj_handle *obj1_hdl,
                          struct fsal_obj_handle *obj2_hdl);
	fsal_status_t (*handle_digest)(struct fsal_obj_handle *obj_hdl,
				       uint32_t output_type,
				       struct gsh_buffdesc *fh_desc);
	void (*handle_to_key)(struct fsal_obj_handle *obj_hdl,
                              struct gsh_buffdesc *fh_desc);
};
	
