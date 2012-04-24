/* FSAL API
 * object oriented fsal api.
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
	struct fsal_ops *ops;
};

/* fsal module methods */

struct fsal_export;

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
		  const char *name);
int unregister_fsal(struct fsal_module *fsal_hdl);

/* find and take a reference on a fsal
 * part of export setup.  Call the 'put' to release
 * your reference before unloading.
 */

struct fsal_module *lookup_fsal(const char *name);

/* export object
 * Created by fsal and referenced by the export list
 */

struct fsal_obj_handle;
struct exportlist__; /* we just need a pointer, not all of nfs_exports.h */

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
					struct fsal_handle_desc *fh_desc);
	fsal_status_t (*create_handle)(struct fsal_export *exp_hdl,
				       struct fsal_handle_desc *hdl_desc,
				       struct fsal_obj_handle **handle);

	/* statistics and configuration access */
	fsal_status_t (*get_fs_dynamic_info)(struct fsal_export *exp_hdl,
					     fsal_dynamicfsinfo_t *infop);
	fsal_boolean_t (*fs_supports)(struct fsal_export *exp_hdl,
				      fsal_fsinfo_options_t option);
	fsal_size_t (*fs_maxfilesize)(struct fsal_export *exp_hdl);
	fsal_size_t (*fs_maxread)(struct fsal_export *exp_hdl);
	fsal_size_t (*fs_maxwrite)(struct fsal_export *exp_hdl);
	fsal_count_t (*fs_maxlink)(struct fsal_export *exp_hdl);
	fsal_mdsize_t (*fs_maxnamelen)(struct fsal_export *exp_hdl);
	fsal_mdsize_t (*fs_maxpathlen)(struct fsal_export *exp_hdl);
	fsal_fhexptype_t (*fs_fh_expire_type)(struct fsal_export *exp_hdl);
	fsal_time_t (*fs_lease_time)(struct fsal_export *exp_hdl);
	fsal_aclsupp_t (*fs_acl_support)(struct fsal_export *exp_hdl);
	fsal_attrib_mask_t (*fs_supported_attrs)(struct fsal_export *exp_hdl);
	fsal_accessmode_t (*fs_umask)(struct fsal_export *exp_hdl);
	fsal_accessmode_t (*fs_xattr_access_rights)(struct fsal_export *exp_hdl);
/* FIXME: these _USE_FOO conditionals in api headers are evil.  Remove asap */
#ifdef _USE_FSALMDS
	fattr4_fs_layout_types (*fs_layout_types)(struct fsal_staticfsinfo_t *info);
	fsal_size_t (*layout_blksize)(struct fsal_staticinfo_t *info);
#endif
	/* quotas are managed at the file system (export) level */
	fsal_status_t (*check_quota)(struct fsal_export *exp_hdl,
				   fsal_path_t * pfsal_path,
				   int quota_type,
				   struct user_cred *creds);
	fsal_status_t (*get_quota)(struct fsal_export *exp_hdl,
				   fsal_path_t * pfsal_path,
				   int quota_type,
				   struct user_cred *creds,
				   fsal_quota_t * pquota);
	fsal_status_t (*set_quota)(struct fsal_export *exp_hdl,
				   fsal_path_t * pfsal_path,
				   int quota_type,
				   struct user_cred *creds,
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
	fsal_nodetype_t type;
	struct fsal_export *export;	/* export who created me */
	fsal_attrib_list_t attributes;  /* used to be in cache_entry */
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
				 fsal_boolean_t *eof);
	fsal_status_t (*create)(struct fsal_obj_handle *dir_hdl,
				fsal_name_t *name,
				fsal_attrib_list_t *attrib,
				struct fsal_obj_handle **new_obj);
	fsal_status_t (*mkdir)(struct fsal_obj_handle *dir_hdl,
			       fsal_name_t *name,
			       fsal_attrib_list_t *attrib,
			       struct fsal_obj_handle **new_obj);
	fsal_status_t (*mknode)(struct fsal_obj_handle *dir_hdl,
				fsal_name_t *name,
				fsal_nodetype_t nodetype,  /* IN */
				fsal_dev_t *dev,  /* IN */
				fsal_attrib_list_t *attrib,
				struct fsal_obj_handle **new_obj);
	fsal_status_t (*symlink)(struct fsal_obj_handle *dir_hdl,
				 fsal_name_t *name,
				 fsal_path_t *link_path,
				 fsal_attrib_list_t *attrib,
				 struct fsal_obj_handle **new_obj);

	/* file object operations */
	fsal_status_t (*readlink)(struct fsal_obj_handle *obj_hdl,
				  char *link_content,
				  uint32_t max_len);
	fsal_status_t (*test_access)(struct fsal_obj_handle *obj_hdl,
				     struct user_cred *creds,
				     fsal_accessflags_t access_type);
	fsal_status_t (*getattrs)(struct fsal_obj_handle *obj_hdl,
				  fsal_attrib_list_t *obj_attr);
	fsal_status_t (*setattrs)(struct fsal_obj_handle *obj_hdl,
				  fsal_attrib_list_t *attrib_set);
	fsal_status_t (*link)(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      fsal_name_t *name);
	fsal_status_t (*rename)(struct fsal_obj_handle *olddir_hdl,
				fsal_name_t *old_name,
				struct fsal_obj_handle *newdir_hdl,
				fsal_name_t *new_name);
	fsal_status_t (*unlink)(struct fsal_obj_handle *obj_hdl,
				fsal_name_t *name);
	fsal_status_t (*truncate)(struct fsal_obj_handle *obj_hdl,
				  fsal_size_t length);

	/* I/O management */
	fsal_status_t (*open)(struct fsal_obj_handle *obj_hdl,
			      fsal_openflags_t openflags);
	fsal_status_t (*read)(struct fsal_obj_handle *obj_hdl,
			      fsal_seek_t * seek_descriptor,
			      size_t buffer_size,
			      caddr_t buffer,
			      ssize_t *read_amount,
			      fsal_boolean_t * end_of_file); /* needed? */
	fsal_status_t (*write)(struct fsal_obj_handle *obj_hdl,
			       fsal_seek_t * seek_descriptor,
			       size_t buffer_size,
			       caddr_t buffer,
			       ssize_t *write_amount);
	fsal_status_t (*commit)(struct fsal_obj_handle *obj_hdl, /* sync */
				off_t offset,
				size_t len);
	fsal_status_t (*lock_op)(struct fsal_obj_handle *obj_hdl,
				 void * p_owner,
				 fsal_lock_op_t lock_op,
				 fsal_lock_param_t   request_lock,
				 fsal_lock_param_t * conflicting_lock);
	fsal_status_t (*close)(struct fsal_obj_handle *obj_hdl);
	fsal_status_t (*rcp)(struct fsal_obj_handle *obj_hdl,
			     const char *local_path,
			     fsal_rcpflag_t transfer_opt);

	/* extended attributes management */
	fsal_status_t (*getextattrs)(struct fsal_obj_handle *obj_hdl,
				     fsal_extattrib_list_t * object_attributes);
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
					  fsal_attrib_list_t * p_attrs);
	fsal_status_t (*remove_extattr_by_id)(struct fsal_obj_handle *obj_hdl,
					      unsigned int xattr_id);
	fsal_status_t (*remove_extattr_by_name)(struct fsal_obj_handle *obj_hdl,
						const char *xattr_name);

	/* handle operations */
	fsal_boolean_t (*handle_is)(struct fsal_obj_handle *obj_hdl,
				    fsal_nodetype_t type);
	fsal_status_t (*lru_cleanup)(struct fsal_obj_handle *obj_hdl,
				     lru_actions_t requests);
	fsal_boolean_t (*compare)(struct fsal_obj_handle *obj1_hdl,
				  struct fsal_obj_handle *obj2_hdl);
	fsal_status_t (*handle_digest)(struct fsal_obj_handle *obj_hdl,
				       fsal_digesttype_t output_type,
				       struct fsal_handle_desc *fh_desc);
	void (*handle_to_key)(struct fsal_obj_handle *obj_hdl,
				       struct fsal_handle_desc *fh_desc);
};
	
