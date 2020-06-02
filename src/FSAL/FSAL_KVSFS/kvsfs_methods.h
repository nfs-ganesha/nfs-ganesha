/* KVSFS methods for handles
 */

void kvsfs_handle_ops_init(struct fsal_obj_ops *ops);

/* method proto linkage to handle.c for export
 */

fsal_status_t kvsfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle);

fsal_status_t kvsfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle);

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
	struct fsal_staticfsinfo_t fs_info;
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

/*
 * KVSFS internal object handle
 * handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 * wrt locks, should this be a lock counter??
 */

struct kvsfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct attrlist attributes;
	struct kvsfs_file_handle *handle;
	union {
		struct {
			kvsns_ino_t inode;
			fsal_openflags_t openflags;
			struct stat saved_stat;
			kvsns_file_open_t fd;
			kvsns_cred_t cred;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
	} u;
};

	/* I/O management */
fsal_status_t kvsfs_open(struct fsal_obj_handle *obj_hdl,
			fsal_openflags_t openflags);
fsal_status_t kvsfs_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			  off_t offset, size_t len);
fsal_openflags_t kvsfs_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t kvsfs_read(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file);
fsal_status_t kvsfs_write(struct fsal_obj_handle *obj_hdl,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable);
fsal_status_t kvsfs_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			    fsal_share_param_t request_share);
fsal_status_t kvsfs_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t kvsfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			       lru_actions_t requests);

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
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t kvsfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id,
					  caddr_t buffer_addr,
					  size_t buffer_size,
					  size_t *p_output_size);
fsal_status_t kvsfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				    const char *xattr_name, caddr_t buffer_addr,
				    size_t buffer_size, int create);
fsal_status_t kvsfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id,
					  caddr_t buffer_addr,
					  size_t buffer_size);
fsal_status_t kvsfs_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int xattr_id,
				    struct attrlist *p_attrs);
fsal_status_t kvsfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					unsigned int xattr_id);
fsal_status_t kvsfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					  const char *xattr_name);
fsal_status_t kvsfs_lock_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock);

/* external storage */
int external_write(struct fsal_obj_handle *obj_hdl,
		   uint64_t offset,
		   size_t buffer_size,
		   void *buffer,
		   size_t *write_amount,
		   bool *fsal_stable,
		   struct stat *stat);

int external_read(struct fsal_obj_handle *obj_hdl,
		  uint64_t offset,
		  size_t buffer_size,
		  void *buffer,
		  size_t *read_amount,
		  bool *end_of_file);

int external_consolidate_attrs(struct fsal_obj_handle *obj_hdl,
			       struct stat *kvsstat);

int external_unlink(struct fsal_obj_handle *dir_hdl,
		    const char *name);

int external_truncate(struct fsal_obj_handle *obj_hdl,
		      off_t filesize);



