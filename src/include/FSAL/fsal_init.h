/**
 * @file fsal_init.h
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Module initialization
 */

/**
 * @brief Initializer macro
 *
 * Every FSAL module has an initializer.  any function labeled as
 * MODULE_INIT will be called in order after the module is loaded and
 * before dlopen returns.  This is where you register your fsal.
 *
 * The initializer function should use register_fsal to initialize
 * public data and get the default operation vectors, then override
 * them with module-specific methods.
 */

#define MODULE_INIT __attribute__((constructor))

/**
 * @brief Finalizer macro
 *
 * Every FSAL module *must* have a destructor to free any resources.
 * the function should assert() that the module can be safely unloaded.
 * However, the core should do the same check prior to attempting an
 * unload. The function must be defined as void foo(void), i.e. no args
 * passed and no returns evaluated.
 */

#define MODULE_FINI __attribute__((destructor))
