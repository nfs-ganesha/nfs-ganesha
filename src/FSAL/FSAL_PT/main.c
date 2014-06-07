/*
 *--------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_init.c
 * Description: FSAL initialization operations implementation
 * Author:      FSI IPC dev team
 * ----------------------------------------------------------------------------
 */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) IBM Inc., 2013
 *
 * Contributors: Jim Lieb jlieb@panasas.com
 *               Allison Henderson        achender@linux.vnet.ibm.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "ganesha_list.h"
#include "fsal_internal.h"
#include "FSAL/fsal_init.h"
#include "fsal_api.h"

#include <signal.h>
#include "pt_ganesha.h"
#include <dlfcn.h>
#include <syslog.h>

int polling_thread_handle_timeout_sec = CCL_POLLING_THREAD_HANDLE_TIMEOUT_SEC;
pthread_t g_pthread_closehandle_lisetner;
pthread_t g_pthread_polling_closehandler;
CACHE_TABLE_T g_fsi_name_handle_cache_opened_files;

/* FSAL analogs to CCL variables and structures */
char *g_shm_at_fsal;
struct file_handles_struct_t *g_fsi_handles_fsal;
struct dir_handles_struct_t *g_fsi_dir_handles_fsal;
struct acl_handles_struct_t *g_fsi_acl_handles_fsal;

#define COMPONENT_FSAL_PT  5	/* COMPONENT_FSAL */

#define CCL_SO_PATH "/usr/lib64/libfsi_ipc_ccl.so"

int PTFSAL_log(int level, const char *message)
{
	DisplayLogComponentLevel(COMPONENT_FSAL_PT, __FILE__, __LINE__,
				 (char *)__func__, level, "FSALPT %s",
				 (char *)message);
	return 0;
}

int PTFSAL_log_level_check(int level)
{
	return (unlikely
		(component_log_level[COMPONENT_FSAL_PT] >= level));
}

static int ptfsal_closeHandle_listener_thread_init(void);
static int ptfsal_polling_closeHandler_thread_init(void);
void *ptfsal_parallel_close_thread(void *args);
void *ptfsal_closeHandle_listener_thread(void *args);

/* PT FSAL module private storage
 */

struct pt_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	/* ptfs_specific_initinfo_t specific_info;  placeholder */
};

const char myname[] = "PT";

/* filesystem info for PT */
static struct fsal_staticfsinfo_t default_posix_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.lock_support = true,
	.lock_support_owner = true,
	.lock_support_async_block = true,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_DENY,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = PT_SUPPORTED_ATTRIBUTES,
	.accesscheck_support = true,
	.share_support = true,
	.share_support_owner = false,
};

static struct config_item pt_params[] = {
	CONF_ITEM_BOOL("link_support", true,
		       fsal_staticfsinfo_t, link_support),
	CONF_ITEM_BOOL("symlink_support", true,
		       fsal_staticfsinfo_t, symlink_support),
	CONF_ITEM_BOOL("cansettime", true,
		       fsal_staticfsinfo_t, cansettime),
	CONF_ITEM_UI64("maxread", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       fsal_staticfsinfo_t, maxread),
	CONF_ITEM_UI64("maxwrite", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       fsal_staticfsinfo_t, maxwrite),
	CONF_ITEM_MODE("umask", 0, 0777, 0,
		       fsal_staticfsinfo_t, umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
		       fsal_staticfsinfo_t, auth_exportpath_xdev),
	CONF_ITEM_MODE("xattr_access_rights", 0, 0777, 0400,
		       fsal_staticfsinfo_t, xattr_access_rights),
	CONFIG_EOL
};

struct config_block pt_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.pt",
	.blk_desc.name = "PT",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = pt_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *pt_staticinfo(struct fsal_module *hdl)
{
	struct pt_fsal_module *myself;

	myself = container_of(hdl, struct pt_fsal_module, fsal);
	return &myself->fs_info;
}

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct)
{
	struct pt_fsal_module *pt_me =
	    container_of(fsal_hdl, struct pt_fsal_module, fsal);
	struct config_error_type err_type;

	pt_me->fs_info = default_posix_info;	/* copy of the defaults */

	(void) load_config_from_parse(config_struct,
				      &pt_param,
				      &pt_me->fs_info,
				      true,
				      &err_type);
	if (!config_error_is_harmless(&err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(&pt_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) PT_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 pt_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal PT method linkage to export object
 */

fsal_status_t pt_create_export(struct fsal_module *fsal_hdl,
			       void *parse_node,
			       const struct fsal_up_vector *up_ops);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct pt_fsal_module PT;

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void pt_init(void)
{
	int retval;
	struct fsal_module *myself = &PT.fsal;
	CACHE_TABLE_INIT_PARAM cacheTableInitParam;

	/* load CCL module */
	int rc = pt_ganesha_fsal_ccl_init();
	if (rc)
		return;
	/*
	 * Check the CCL version from the header we got with the version
	 * in the CCL library itself before CCL initialization.
	 */
	rc = CCL_CHECK_VERSION(PT_FSI_CCL_VERSION);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"CCL version mismatch have <%s> got <%s>",
			PT_FSI_CCL_VERSION, CCL_GET_VERSION());
		return;
	}

	/* init mutexes */
	pthread_rwlock_init(&g_fsi_cache_handle_rw_lock, NULL);
	pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
	g_fsi_name_handle_cache.m_count = 0;
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);

	/*
	 * fsi_ipc_trace_level allows using the level settings differently than
	 * Ganesha proper.
	 * We map FSI Trace Level to Ganesha debug levels through this array.
	 */
	int ipc_ccl_to_component_trc_level_map[FSI_NUM_TRACE_LEVELS];
	ipc_ccl_to_component_trc_level_map[FSI_NO_LEVEL] = NIV_NULL;
	ipc_ccl_to_component_trc_level_map[FSI_FATAL] = NIV_MAJ;
	ipc_ccl_to_component_trc_level_map[FSI_ERR] = NIV_CRIT;
	ipc_ccl_to_component_trc_level_map[FSI_WARNING] = NIV_WARN;
	ipc_ccl_to_component_trc_level_map[FSI_NOTICE] = NIV_WARN;
	ipc_ccl_to_component_trc_level_map[FSI_STAT] = NIV_EVENT;
	ipc_ccl_to_component_trc_level_map[FSI_INFO] = NIV_DEBUG;
	ipc_ccl_to_component_trc_level_map[FSI_DEBUG] = NIV_DEBUG;

	/* FSI CCL Layer INIT */
	rc = CCL_INIT(MULTITHREADED, PTFSAL_log, PTFSAL_log_level_check,
		      ipc_ccl_to_component_trc_level_map);

	if (rc == -1) {
		LogCrit(COMPONENT_FSAL, "ccl_init returned rc = -1, errno = %d",
			errno);
		return;
	}

	FSI_TRACE(FSI_NOTICE,
		  "About to call " "ptfsal_closeHandle_listener_thread_init");
	if (ptfsal_closeHandle_listener_thread_init() == -1) {
		FSI_TRACE(FSI_ERR,
			  "ptfsal_closeHandle_listener_thread_init "
			  "returned rc = -1");
		return;
	}

	FSI_TRACE(FSI_NOTICE,
		  "About to call " "ptfsal_polling_closeHandler_thread_init");
	if (ptfsal_polling_closeHandler_thread_init() == -1) {
		FSI_TRACE(FSI_ERR,
			  "ptfsal_polling_closeHandler_thread_init "
			  "returned rc = -1");
		return;
	}

	cacheTableInitParam.cacheKeyComprefn =
	    &fsi_cache_handle2name_keyCompare;
	cacheTableInitParam.cacheTableID =
	    CACHE_ID_192_FRONT_END_HANDLE_TO_NAME_CACHE;
	cacheTableInitParam.dataSizeInBytes =
	    sizeof(CACHE_ENTRY_DATA_HANDLE_TO_NAME_T);
	cacheTableInitParam.keyLengthInBytes =
	    sizeof(g_fsi_name_handle_cache.m_entry[0].m_handle);
	cacheTableInitParam.maxNumOfCacheEntries =
	    FSI_CCL_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS;

	rc = fsi_cache_table_init(&g_fsi_name_handle_cache_opened_files,
				  &cacheTableInitParam);

	if (rc != FSI_CCL_IPC_EOK) {
		LogCrit(FSI_ERR, "Failed to initialize cache table ID[%d]",
			cacheTableInitParam.cacheTableID);
		return;
	}

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		fprintf(stderr, "PT module failed to register");
		return;
	}
	myself->ops->create_export = pt_create_export;
	myself->ops->init_config = init_config;
	pt_filesystem.fsal = myself;
}

MODULE_FINI void pt_unload(void)
{
	int retval;

	retval = unregister_fsal(&PT.fsal);
	if (retval != 0) {
		fprintf(stderr, "PT module failed to unregister");
		return;
	}
}

char *check_dl_error(const char *func_name)
{
	char *error = dlerror();

	if (error != NULL) {
		if (!func_name)
			func_name = "UNKNOWN";

		LogCrit(COMPONENT_FSAL,
			"Failed to dynamically load function: %s, error: %s",
			func_name, error);
		return error;
	} else {
		return NULL;
	}
}

char *load_dynamic_function(void *fn_map_ptr, const char *func_name)
{
	/* sanity checks */
	if (!func_name) {
		LogCrit(COMPONENT_FSAL, "NULL func_name");
		return "NULL func_name";
	}

	/* load function pointers */
	*(void **)(fn_map_ptr) = dlsym(g_ccl_lib_handle, func_name);

	/* check for error */
	char *error_string = check_dl_error(func_name);

	return error_string;
}

int pt_ganesha_fsal_ccl_init()
{
	g_ccl_lib_handle = dlopen(CCL_SO_PATH, RTLD_LAZY);
	if (!g_ccl_lib_handle) {
		LogCrit(COMPONENT_FSAL, "Failed to load library: %s",
			CCL_SO_PATH);
		return -1;
	}

	LogCrit(COMPONENT_FSAL, "loaded library: %s", CCL_SO_PATH);
	/* clearn any existing error */
	dlerror();

	/* load all CCL function pointers */
#define DL_LOAD(func_ptr, func_name)                                   \
	((load_return = load_dynamic_function(func_ptr, func_name)) == NULL)

	char *load_return = NULL;

	if (DL_LOAD(&g_ccl_function_map.init_fn, "ccl_init")
		&& DL_LOAD(&g_ccl_function_map.check_handle_index_fn,
			"ccl_check_handle_index")
		&& DL_LOAD(
			&g_ccl_function_map.find_handle_by_name_and_export_fn,
			"ccl_find_handle_by_name_and_export")
		&& DL_LOAD(&g_ccl_function_map.stat_fn, "ccl_stat")
		&& DL_LOAD(&g_ccl_function_map.fstat_fn, "ccl_fstat")
		&& DL_LOAD(&g_ccl_function_map.stat_by_handle_fn,
			"ccl_stat_by_handle")
		&& DL_LOAD(
		  &g_ccl_function_map.rcv_msg_nowait_fn, "rcv_msg_nowait")
		&& DL_LOAD(&g_ccl_function_map.rcv_msg_wait_fn, "rcv_msg_wait")
		&& DL_LOAD(&g_ccl_function_map.rcv_msg_wait_block_fn,
			"rcv_msg_wait_block")
		&& DL_LOAD(&g_ccl_function_map.send_msg_fn, "send_msg")
		&& DL_LOAD(&g_ccl_function_map.chmod_fn, "ccl_chmod")
		&& DL_LOAD(&g_ccl_function_map.chown_fn, "ccl_chown")
		&& DL_LOAD(&g_ccl_function_map.ntimes_fn, "ccl_ntimes")
		&& DL_LOAD(&g_ccl_function_map.mkdir_fn, "ccl_mkdir")
		&& DL_LOAD(&g_ccl_function_map.rmdir_fn, "ccl_rmdir")
		&& DL_LOAD(&g_ccl_function_map.get_real_filename_fn,
			"ccl_get_real_filename")
		&& DL_LOAD(&g_ccl_function_map.disk_free_fn, "ccl_disk_free")
		&& DL_LOAD(&g_ccl_function_map.unlink_fn, "ccl_unlink")
		&& DL_LOAD(&g_ccl_function_map.rename_fn, "ccl_rename")
		&& DL_LOAD(&g_ccl_function_map.opendir_fn, "ccl_opendir")
		&& DL_LOAD(&g_ccl_function_map.closedir_fn, "ccl_closedir")
		&& DL_LOAD(&g_ccl_function_map.readdir_fn, "ccl_readdir")
		&& DL_LOAD(&g_ccl_function_map.seekdir_fn, "ccl_seekdir")
		&& DL_LOAD(&g_ccl_function_map.telldir_fn, "ccl_telldir")
		&& DL_LOAD(&g_ccl_function_map.chdir_fn, "ccl_chdir")
		&& DL_LOAD(&g_ccl_function_map.fsync_fn, "ccl_fsync")
		&& DL_LOAD(&g_ccl_function_map.ftruncate_fn, "ccl_ftruncate")
		&& DL_LOAD(&g_ccl_function_map.pread_fn, "ccl_pread")
		&& DL_LOAD(&g_ccl_function_map.pwrite_fn, "ccl_pwrite")
		&& DL_LOAD(&g_ccl_function_map.open_fn, "ccl_open")
		&& DL_LOAD(&g_ccl_function_map.close_fn, "ccl_close")
		&& DL_LOAD(&g_ccl_function_map.get_any_io_responses_fn,
			"get_any_io_responses")
		&& DL_LOAD(&g_ccl_function_map.ipc_stats_logger_fn,
			"ccl_ipc_stats_logger")
		&& DL_LOAD(&g_ccl_function_map.update_stats_fn, "update_stats")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_get_entry_fn,
			"ccl_sys_acl_get_entry")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_get_tag_type_fn,
			"ccl_sys_acl_get_tag_type")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_get_permset_fn,
			"ccl_sys_acl_get_permset")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_get_qualifier_fn,
			"ccl_sys_acl_get_qualifier")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_get_file_fn,
			"ccl_sys_acl_get_file")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_clear_perms_fn,
			"ccl_sys_acl_clear_perms")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_add_perm_fn,
			"ccl_sys_acl_add_perm")
		&& DL_LOAD(
		   &g_ccl_function_map.sys_acl_init_fn, "ccl_sys_acl_init")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_create_entry_fn,
			"ccl_sys_acl_create_entry")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_set_tag_type_fn,
			"ccl_sys_acl_set_tag_type")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_set_qualifier_fn,
			"ccl_sys_acl_set_qualifier")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_set_permset_fn,
			"ccl_sys_acl_set_permset")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_set_file_fn,
			"ccl_sys_acl_set_file")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_delete_def_file_fn,
			"ccl_sys_acl_delete_def_file")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_get_perm_fn,
			"ccl_sys_acl_get_perm")
		&& DL_LOAD(&g_ccl_function_map.sys_acl_free_acl_fn,
			"ccl_sys_acl_free_acl")
		&& DL_LOAD(&g_ccl_function_map.name_to_handle_fn,
			"ccl_name_to_handle")
		&& DL_LOAD(&g_ccl_function_map.handle_to_name_fn,
			"ccl_handle_to_name")
		&& DL_LOAD(&g_ccl_function_map.dynamic_fsinfo_fn,
			"ccl_dynamic_fsinfo")
		&& DL_LOAD(&g_ccl_function_map.readlink_fn, "ccl_readlink")
		&& DL_LOAD(&g_ccl_function_map.symlink_fn, "ccl_symlink")
		&& DL_LOAD(&g_ccl_function_map.update_handle_nfs_state_fn,
			"ccl_update_handle_nfs_state")
		&& DL_LOAD(&g_ccl_function_map.safe_update_handle_nfs_state_fn,
			"ccl_safe_update_handle_nfs_state")
		&& DL_LOAD(&g_ccl_function_map.fsal_try_stat_by_index_fn,
			"ccl_fsal_try_stat_by_index")
		&& DL_LOAD(&g_ccl_function_map.fsal_try_fastopen_by_index_fn,
			"ccl_fsal_try_fastopen_by_index")
		&& DL_LOAD(&g_ccl_function_map.find_oldest_handle_fn,
			"ccl_find_oldest_handle")
		&& DL_LOAD(&g_ccl_function_map.can_close_handle_fn,
			"ccl_can_close_handle")
		&& DL_LOAD(&g_ccl_function_map.up_mutex_lock_fn,
			"ccl_up_mutex_lock")
		&& DL_LOAD(&g_ccl_function_map.up_mutex_unlock_fn,
			"ccl_up_mutex_unlock")
		&& DL_LOAD(&g_ccl_function_map.log_fn, "ccl_log")
		&& DL_LOAD(&g_fsal_fsi_handles, "g_fsi_handles")
		&& DL_LOAD(&g_ccl_function_map.implicit_close_for_nfs_fn,
			"ccl_implicit_close_for_nfs")
		&& DL_LOAD(&g_ccl_function_map.update_cache_stat_fn,
			"ccl_update_cache_stat")
		&& DL_LOAD(&g_ccl_function_map.get_version_fn,
			"ccl_get_version")
		&& DL_LOAD(&g_ccl_function_map.check_version_fn,
			"ccl_check_version")
		&& DL_LOAD(&g_ccl_function_map.close_listener_fn,
			"ccl_close_listener")
		&& DL_LOAD(&g_ccl_function_map.ccl_lock_io_operation_mutex_fn,
			"ccl_lock_io_operation_mutex")
		&& DL_LOAD(&g_ccl_function_map.ccl_unlock_io_operation_mutex_fn,
			"ccl_unlock_io_operation_mutex")
		&& DL_LOAD(&g_ccl_function_map.ccl_lock_io_handle_mutex_fn,
			"ccl_lock_io_handle_mutex")
		&& DL_LOAD(&g_ccl_function_map.ccl_unlock_io_handle_mutex_fn,
			"ccl_unlock_io_handle_mutex")
		&& DL_LOAD(&g_ccl_function_map.ccl_lock_file_mutex_fn,
			"ccl_lock_file_mutex")
		&& DL_LOAD(&g_ccl_function_map.ccl_unlock_file_mutex_fn,
			"ccl_unlock_file_mutex")
		) {
		LogInfo(COMPONENT_FSAL, "Functions loaded");
	} else {
		LogCrit(COMPONENT_FSAL, "Failed to load function: %s",
			load_return);
		return -1;
	}

#undef DL_LOAD

	/* load and map variables that reside in the CCL shared library */
	void *g_shm_at_obj = dlsym(g_ccl_lib_handle, "g_shm_at");
	if (!g_shm_at_obj) {
		LogCrit(COMPONENT_FSAL, "Failed to load symbol g_shm_at");
		return -1;
	}
	g_shm_at_fsal = (char *)g_shm_at_obj;

	void *g_fsi_handles_obj = dlsym(g_ccl_lib_handle, "g_fsi_handles");
	if (!g_fsi_handles_obj) {
		LogCrit(COMPONENT_FSAL, "Failed to load symbol g_fsi_handles");
		return -1;
	}
	g_fsi_handles_fsal = (struct file_handles_struct_t *)g_fsi_handles_obj;

	void *g_fsi_dir_handles_obj =
	    dlsym(g_ccl_lib_handle, "g_fsi_dir_handles");
	if (!g_fsi_dir_handles_obj) {
		LogCrit(COMPONENT_FSAL,
			"Failed to load symbol g_fsi_dir_handles");
		return -1;
	}
	g_fsi_dir_handles_fsal =
	    (struct dir_handles_struct_t *)g_fsi_dir_handles_obj;

	void *g_fsi_acl_handles_obj =
	    dlsym(g_ccl_lib_handle, "g_fsi_acl_handles");
	if (!g_fsi_acl_handles_obj) {
		LogCrit(COMPONENT_FSAL, "Failed to load g_fsi_acl_handles");
		return -1;
	}
	g_fsi_acl_handles_fsal =
	    (struct acl_handles_struct_t *)g_fsi_handles_obj;

	return 0;
}

static int ptfsal_closeHandle_listener_thread_init(void)
{
	pthread_attr_t attr_thr;
	int rc;

	/* Init the thread in charge of renewing the client id */
	/* Init for thread parameter (mostly for scheduling) */
	pthread_attr_init(&attr_thr);

	rc = pthread_create(&g_pthread_closehandle_lisetner, &attr_thr,
			    ptfsal_closeHandle_listener_thread, (void *)NULL);

	if (rc != 0) {
		FSI_TRACE(FSI_ERR,
			  "Failed to create CloseHandleListener thread rc[%d]",
			  rc);
		return -1;
	}

	FSI_TRACE(FSI_NOTICE,
		  "CloseHandle listener thread created successfully");
	return 0;
}

static int ptfsal_polling_closeHandler_thread_init(void)
{
	pthread_attr_t attr_thr;
	int rc;

	/* Init the thread in charge of renewing the client id */
	/* Init for thread parameter (mostly for scheduling) */
	pthread_attr_init(&attr_thr);

	rc = pthread_create(&g_pthread_polling_closehandler, &attr_thr,
			    ptfsal_polling_closeHandler_thread, (void *)NULL);

	if (rc != 0) {
		FSI_TRACE(FSI_ERR,
			  "Failed to create polling close handler thread rc[%d]",
			  rc);
		return -1;
	}

	FSI_TRACE(FSI_NOTICE, "Polling close handler created successfully");
	return 0;
}

fsal_status_t PTFSAL_terminate()
{
	int index;
	int minor = 0;
	int major = ERR_FSAL_NO_ERROR;
	int rc;
	pthread_attr_t attr_thr;

	typedef struct {
		int isThreadCreated;
		int handleIdx;
		pthread_t threadContext;
	} CLOSE_THREAD_MAP;

	CLOSE_THREAD_MAP parallelCloseThreadMap[FSI_CCL_MAX_STREAMS +
						FSI_CIFS_RESERVED_STREAMS];

	FSI_TRACE(FSI_NOTICE, "Terminating FSAL_PT");

	/* Set terminate flag to seop ptfsal threads. */
	ptfsal_terminate_ptfsal_threads();

	pthread_attr_init(&attr_thr);
	memset(&parallelCloseThreadMap[0], 0x00,
	       sizeof(parallelCloseThreadMap));

	for (index = FSI_CIFS_RESERVED_STREAMS;
	     index < FSI_CCL_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS; index++) {
		parallelCloseThreadMap[index].handleIdx = index;
	}

	for (index = FSI_CIFS_RESERVED_STREAMS;
	     index < g_fsi_handles_fsal->m_count; index++) {
		if (g_fsi_handles_fsal->m_handle[index].m_hndl_in_use != 0) {
			if ((g_fsi_handles_fsal->m_handle[index].m_nfs_state ==
			     NFS_CLOSE)
			    || (g_fsi_handles_fsal->m_handle[index].
				m_nfs_state == NFS_OPEN)) {

				/*
				 * ignore error code, just trying to clean
				 * up while going down and want to continue
				 * trying to close out other open files
				 */
				rc = pthread_create(&parallelCloseThreadMap
						   [index].threadContext,
						   &attr_thr,
						   ptfsal_parallel_close_thread,
						   &parallelCloseThreadMap
						   [index].handleIdx);

				if (rc != 0) {
					FSI_TRACE(FSI_ERR,
						  "Failed to create parallel close"
						  "thread for handle[%d] rc[%d]",
						  index, rc);
				} else {
					FSI_TRACE(FSI_NOTICE,
						  "Created close thread for handle[%d]",
						  index);
					parallelCloseThreadMap[index].
					    isThreadCreated = 1;
				}
			}
		}
	}

	for (index = FSI_CIFS_RESERVED_STREAMS;
	     index < FSI_CCL_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS; index++) {
		if (parallelCloseThreadMap[index].isThreadCreated == 1) {
			pthread_join(parallelCloseThreadMap[index].
				     threadContext, NULL);
		}
	}

	FSI_TRACE(FSI_NOTICE, "All parallel close threads have exited");

	/* Join the Polling Close Handle thread */
	int terminate_rc = pthread_join(g_pthread_polling_closehandler, NULL);
	if (terminate_rc == 0) {
		FSI_TRACE(FSI_NOTICE,
			  "Join Polling close handle thread successfully");
	} else {
		FSI_TRACE(FSI_ERR,
			  "Join Polling close handle thread failed = %d",
			  terminate_rc);
	}

	/* Terminate Close Handle Listener thread if it's not already dead */
	int signal_send_rc =
	    pthread_kill(g_pthread_closehandle_lisetner, SIGTERM);
	if (signal_send_rc == 0) {
		FSI_TRACE(FSI_NOTICE,
			  "Close Handle Listener thread killed successfully");
	} else if (signal_send_rc == ESRCH) {
		FSI_TRACE(FSI_ERR, "Close Handle Listener already terminated");
	} else if (signal_send_rc) {
		FSI_TRACE(FSI_ERR, "Error from pthread_kill = %d",
			  signal_send_rc);
		minor = 3;
		major = posix2fsal_error(signal_send_rc);
	}

	FSI_TRACE(FSI_NOTICE, "End termination of FSAL_PT");

	/* close dynamically loaded module */
	dlclose(g_ccl_lib_handle);
	return fsalstat(major, minor);
}

void *ptfsal_parallel_close_thread(void *args)
{

	int index = *((int *)args);
	char threadName[40];

	snprintf(threadName, sizeof(threadName), "PT PClose %d",
		 index);
	SetNameFunction(threadName);
	FSI_TRACE(FSI_NOTICE, "Closing handle[%d]", index);
	ptfsal_implicit_close_for_nfs(index, CCL_CLOSE_STYLE_FIRE_AND_FORGET);

	pthread_exit(0);

}
