/* NULLFS methods for handles
 */

struct nullfs_fsal_obj_handle;

struct nullfs_file_handle {
	int nothing;
};

struct next_ops {
	struct export_ops exp_ops;	/*< Vector of operations */
	struct fsal_obj_ops obj_ops;	/*< Shared handle methods vector */
	struct fsal_dsh_ops dsh_ops;	/*< Shared handle methods vector */
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
};

extern struct next_ops next_ops;
extern struct fsal_up_vector fsal_up_top;
void nullfs_handle_ops_init(struct fsal_obj_ops *ops);

/*
 * NULLFS internal export
 */
struct nullfs_fsal_export {
	struct fsal_export export;
	struct fsal_export *sub_export;
};

fsal_status_t nullfs_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle);

fsal_status_t nullfs_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle);

/*
 * NULLFS internal object handle
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

struct nullfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct nullfs_file_handle *handle;
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
			struct nullfs_file_handle *dir;
			char *name;
		} unopenable;
	} u;
};

int nullfs_fsal_open(struct nullfs_fsal_obj_handle *, int, fsal_errors_t *);
int nullfs_fsal_readlink(struct nullfs_fsal_obj_handle *, fsal_errors_t *);

static inline bool nullfs_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

	/* I/O management */
fsal_status_t nullfs_open(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags);
fsal_openflags_t nullfs_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t nullfs_read(struct fsal_obj_handle *obj_hdl,
			  uint64_t offset,
			  size_t buffer_size, void *buffer,
			  size_t *read_amount, bool *end_of_file);
fsal_status_t nullfs_write(struct fsal_obj_handle *obj_hdl,
			   uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable);
fsal_status_t nullfs_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len);
fsal_status_t nullfs_lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock);
fsal_status_t nullfs_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			      fsal_share_param_t request_share);
fsal_status_t nullfs_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t nullfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
				 lru_actions_t requests);

/* extended attributes management */
fsal_status_t nullfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int cookie,
				    fsal_xattrent_t *xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list);
fsal_status_t nullfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   unsigned int *pxattr_id);
fsal_status_t nullfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const char *xattr_name,
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size);
fsal_status_t nullfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t nullfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      caddr_t buffer_addr, size_t buffer_size,
				      int create);
fsal_status_t nullfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size);
fsal_status_t nullfs_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				      unsigned int xattr_id,
				      struct attrlist *p_attrs);
fsal_status_t nullfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id);
fsal_status_t nullfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name);
