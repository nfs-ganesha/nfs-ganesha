/* VFS methods for handles
 */

/* private helpers from export
 */

libzfswrap_vfs_t *  tank_get_root_pvfs(struct fsal_export *exp_hdl);

/* method proto linkage to handle.c for export
 */

fsal_status_t tank_lookup_path(struct fsal_export *exp_hdl,
                              const struct req_op_context *opctx,
			      const char *path,
			      struct fsal_obj_handle **handle);

fsal_status_t tank_create_handle(struct fsal_export *exp_hdl,
                                const struct req_op_context *opctx,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle);

/*
 * VFS internal object handle
 * handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 * wrt locks, should this be a lock counter??
 */

struct zfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct zfs_file_handle *handle;
	union {
		struct {
                         creden_t cred;
                         off_t current_offset;
                         libzfswrap_vnode_t *p_vnode;
                         int is_closed;
			 fsal_openflags_t openflags;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
		struct {
			struct file_handle *dir;
			char *name;
		} unopenable;
	} u;
};

static inline bool
vfs_unopenable_type(object_file_type_t type)
{
        if ((type == SOCKET_FILE) ||
            (type == CHARACTER_FILE) ||
            (type == BLOCK_FILE)) {
                return true;
        } else {
                return false;
        }
}


	/* I/O management */
fsal_status_t tank_open(struct fsal_obj_handle *obj_hdl,
		       fsal_openflags_t openflags);
fsal_openflags_t tank_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t tank_read(struct fsal_obj_handle *obj_hdl,
                       const struct req_op_context *opctx,
		       uint64_t offset,
		       size_t buffer_size,
		       void *buffer,
		       size_t *read_amount,
		       bool *end_of_file);
fsal_status_t tank_write(struct fsal_obj_handle *obj_hdl,
                        const struct req_op_context *opctx,
                        uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t * write_amount);
fsal_status_t tank_share_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,         /* IN (opaque to FSAL) */
			   fsal_share_param_t  request_share);
fsal_status_t tank_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t tank_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			      lru_actions_t requests);

/* extended attributes management */
fsal_status_t tank_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				 unsigned int cookie,
				 fsal_xattrent_t * xattrs_tab,
				 unsigned int xattrs_tabsize,
				 unsigned int *p_nb_returned,
				 int *end_of_list);
fsal_status_t tank_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					const char *xattr_name,
					unsigned int *pxattr_id);
fsal_status_t tank_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   caddr_t buffer_addr,
					   size_t buffer_size,
					   size_t * p_output_size);
fsal_status_t tank_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size,
					 size_t *p_output_size);
fsal_status_t tank_setextattr_value(struct fsal_obj_handle *obj_hdl,
				   const char *xattr_name,
				   caddr_t buffer_addr,
				   size_t buffer_size,
				   int create);
fsal_status_t tank_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size);
fsal_status_t tank_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				   unsigned int xattr_id,
				   struct attrlist *p_attrs);
fsal_status_t tank_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
				       unsigned int xattr_id);
fsal_status_t tank_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					 const char *xattr_name);
