/* XFS methods for handles
 */

/* method proto linkage to handle.c for export
 */

fsal_status_t xfs_lookup_path(struct fsal_export *exp_hdl,
                              const struct req_op_context *opctx,
			      const char *path,
			      struct fsal_obj_handle **handle);

fsal_status_t xfs_create_handle(struct fsal_export *exp_hdl,
                                const struct req_op_context *opctx,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle);


fsal_status_t xfs_create_export(struct fsal_module *fsal_hdl,
                                const char *export_path,
                                const char *fs_specific,
                                struct exportlist__ *exp_entry,
                                struct fsal_module *next_fsal,
                                const struct fsal_up_vector *up_ops,
                                struct fsal_export **export);

/*
 * External XFS handle
 *
 * Type information is duplicated in the external handle represetation
 * to allow for avoid doing silly things with special files when
 * converting from on-the-wire format to the internal representation.
 */
struct xfs_fsal_ext_handle {
	uint64_t inode;
	char type;
	u_char len;
	char data[0];
};

/*
 * XFS internal object handle
 */

struct xfs_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	int fd;
	int openflags;
	struct xfs_fsal_ext_handle xfs_hdl;
};

/*
 * XFS internal export
 */
struct xfs_fsal_export {
	struct fsal_export export;
	char *fs_spec;
	char *mntdir;
	dev_t root_dev;
	struct xfs_fsal_ext_handle *root_handle;
};

static inline size_t xfs_sizeof_handle(const struct xfs_fsal_ext_handle *h)
{
	return sizeof(*h) + h->len;
}

static inline bool
xfs_unopenable_type(object_file_type_t type)
{
        return ((type == SOCKET_FILE) ||
		(type == CHARACTER_FILE) ||
		(type == BLOCK_FILE));
}


	/* I/O management */
fsal_status_t xfs_open(struct fsal_obj_handle *obj_hdl,
		       const struct req_op_context *opctx,
		       fsal_openflags_t openflags);
fsal_openflags_t xfs_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t xfs_read(struct fsal_obj_handle *obj_hdl,
                       const struct req_op_context *opctx,
		       uint64_t offset,
		       size_t buffer_size,
		       void *buffer,
		       size_t *read_amount,
		       bool *end_of_file);
fsal_status_t xfs_write(struct fsal_obj_handle *obj_hdl,
                        const struct req_op_context *opctx,
                        uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t * write_amount);
fsal_status_t xfs_commit(struct fsal_obj_handle *obj_hdl, /* sync */
			 off_t offset,
			 size_t len);
fsal_status_t xfs_lock_op(struct fsal_obj_handle *obj_hdl,
			  const struct req_op_context *opctx,
			  void * p_owner,
			  fsal_lock_op_t lock_op,
			  fsal_lock_param_t *request_lock,
			  fsal_lock_param_t *conflicting_lock);
fsal_status_t xfs_share_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,         /* IN (opaque to FSAL) */
			   fsal_share_param_t  request_share);
fsal_status_t xfs_close(struct fsal_obj_handle *obj_hdl);
fsal_status_t xfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			      lru_actions_t requests);
