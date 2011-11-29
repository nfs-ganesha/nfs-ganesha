/*
 * fsal common utility functions
 */

/* fsal_module to fsal_export helpers
 */

int fsal_attach_export(struct fsal_module *fsal_hdl,
		  struct glist_head *obj_link);
void fsal_detach_export(struct fsal_module *fsal_hdl,
		   struct glist_head *obj_link);

/* fsal_export to fsal_obj_handle helpers
 */

int fsal_attach_handle(struct fsal_export *exp_hdl,
		       struct glist_head *obj_link);
void fsal_detach_handle(struct fsal_export *exp_hdl,
			struct glist_head *obj_link);

/* fsal_export common methods
 */

void fsal_export_get(struct fsal_export *exp_hdl);
int fsal_export_put(struct fsal_export *exp_hdl);

/* fsal_obj_handle common methods
 */

void fsal_handle_get(struct fsal_obj_handle *obj_hdl);
int fsal_handle_put(struct fsal_obj_handle *obj_hdl);
