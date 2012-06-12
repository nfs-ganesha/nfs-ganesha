/* fsal_init.c
 * Module initialization
 */

/* Every FSAL module has an initializer.
 * any function labeled as MODULE_INIT will be called
 * in order after the module is loaded and before dlopen
 * returns.  This is where you register your fsal.
 */

#define MODULE_INIT __attribute__((constructor))

/* Every FSAL module *must* have a destructor to free any resources.
 * the function should assert() that the module can be safely unloaded.
 * However, the core should do the same check prior to attempting an
 * unload. The function must be defined as void foo(void), i.e. no args
 * passed and no returns evaluated.
 */

#define MODULE_FINI __attribute__((destructor))

/* Shared FSAL data structures have two definitions, one that
 * is global and passed around by the core, the other private which
 * included the global definition within it.
 * All these data structures are passed back to the core with the global
 * pointer and dereferenced with container_of within the FSAL itself.
 * The following functions are defined within the FSAL to allocate
 * and free these structures.  This is a bridge to the legacy API.
 */

struct fsal_alloc_ops {
	struct fsal_export *(*alloc_export)();
	void (*free_export)(struct fsal_export *exp_hdl);
	struct fsal_obj_handle *(*alloc_obj_handle)();
	fsal_handle_t *(*obj_to_fsal_handle)(struct fsal_obj_handle *obj_hdl);
	void (*free_obj_handle)(struct fsal_obj_handle *obj_hdl);
	struct fsal_dirobj *(*alloc_dirobj)();
/* dir obj to dir something TBD */
	void (*free_dirobj)(struct fsal_dirobj *dir_hdl);
	struct fsal_fileobj *(*alloc_fileobj)();
	fsal_file_t *(*fileobj_to_file)(struct fsal_fileobj *file_hdl);
	void (*free_fileobj)(struct fsal_fileobj * file_hdl);
};

/* Initialization
 */

void init_fsal_parameters(fsal_init_info_t *init_info);

typedef int (*fsal_extra_arg_parser_f)(const char *key, const char *val,
                                       fsal_init_info_t *info, const char *block);

fsal_status_t fsal_load_config(const char *name,
                               config_file_t,
                               fsal_init_info_t *,
                               fsal_staticfsinfo_t *,
                               fsal_extra_arg_parser_f);
