/*
 * fsal common utility functions
 */

/* fsal_module to fsal_export helpers
 */

int fsal_attach_export(struct fsal_module *fsal_hdl,
		       struct glist_head *obj_link);
void fsal_detach_export(struct fsal_module *fsal_hdl,
			struct glist_head *obj_link);

/* fsal_export common methods
 */

struct exportlist;

int fsal_export_init(struct fsal_export *, struct exportlist *);

void free_export_ops(struct fsal_export *exp_hdl);

/* fsal_obj_handle common methods
 */

int fsal_obj_handle_init(struct fsal_obj_handle *, struct fsal_export *,
			 object_file_type_t);

int fsal_obj_handle_uninit(struct fsal_obj_handle *obj);

/*
 * pNFS DS Helpers
 */

int fsal_attach_ds(struct fsal_export *exp_hdl, struct glist_head *ds_link);
void fsal_detach_ds(struct fsal_export *exp_hdl, struct glist_head *ds_link);
int fsal_ds_handle_init(struct fsal_ds_handle *, struct fsal_ds_ops *,
			struct fsal_export *);
int fsal_ds_handle_uninit(struct fsal_ds_handle *ds);
