fsal_status_t
pxy_open(struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags);

fsal_status_t
pxy_read(struct fsal_obj_handle *obj_hdl, fsal_seek_t * seek_descriptor,
	 size_t buffer_size, caddr_t buffer, ssize_t *read_amount,
	 fsal_boolean_t * eof);

fsal_status_t
pxy_write(struct fsal_obj_handle *obj_hdl, fsal_seek_t *seek_descriptor,
	  size_t buffer_size, caddr_t buffer, ssize_t *write_amount);

fsal_status_t
pxy_commit(struct fsal_obj_handle *obj_hdl, off_t offset, size_t len);

fsal_status_t 
pxy_lock_op(struct fsal_obj_handle *obj_hdl, void * p_owner,
	    fsal_lock_op_t lock_op, fsal_lock_param_t request_lock,
	    fsal_lock_param_t * conflicting_lock);

fsal_status_t
pxy_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
	     fsal_share_param_t request_share);

fsal_status_t
pxy_close(struct fsal_obj_handle *obj_hdl);

fsal_status_t
pxy_lru_cleanup(struct fsal_obj_handle *obj_hdl, lru_actions_t requests);

fsal_status_t
pxy_rcp(struct fsal_obj_handle *obj_hdl, const char *local_path,
        fsal_rcpflag_t transfer_opt);


fsal_status_t
pxy_getextattrs(struct fsal_obj_handle *obj_hdl,
		fsal_extattrib_list_t * object_attributes);

fsal_status_t
pxy_list_ext_attrs(struct fsal_obj_handle *obj_hdl, unsigned int cookie,
		   fsal_xattrent_t * xattrs_tab, unsigned int xattrs_tabsize,
		   unsigned int *p_nb_returned, int *end_of_list);

fsal_status_t
pxy_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
			  const char *xattr_name, unsigned int *pxattr_id);

fsal_status_t
pxy_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
			     const char *xattr_name, caddr_t buffer_addr,
			     size_t buffer_size, size_t * len);

fsal_status_t
pxy_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
			   unsigned int xattr_id, caddr_t buf, size_t sz,
			   size_t *len);

fsal_status_t
pxy_setextattr_value(struct fsal_obj_handle *obj_hdl,
		     const char *xattr_name, caddr_t buf, size_t sz,
		     int create);

fsal_status_t
pxy_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
			   unsigned int xattr_id, caddr_t buf, size_t sz);

fsal_status_t
pxy_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
		     unsigned int xattr_id, fsal_attrib_list_t * attrs);

fsal_status_t
pxy_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
			 unsigned int xattr_id);

fsal_status_t
pxy_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
			   const char *xattr_name);

fsal_status_t
pxy_lookup_path(struct fsal_export *exp_hdl, const char *path,
                struct fsal_obj_handle **handle);

fsal_status_t
pxy_create_handle(struct fsal_export *exp_hdl,
                  struct fsal_handle_desc *hdl_desc,
                  struct fsal_obj_handle **handle);

fsal_status_t
pxy_create_export(struct fsal_module *fsal_hdl,
                  const char *export_path,
                  const char *fs_options,
                  struct exportlist__ *exp_entry,
                  struct fsal_module *next_fsal,
                  struct fsal_export **export);
