/*
 * vim:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017-2019 Red Hat, Inc.
 * Author: Daniel Gryniewicz  dang@redhat.com
 *
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

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include "FSAL/fsal_init.h"
#include "mem_int.h"
#include "fsal_convert.h"
#include "../fsal_private.h"

/* MEM FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define MEM_SUPPORTED_ATTRIBUTES (ATTRS_POSIX)

static const char memname[] = "MEM";

/* my module private storage */
struct mem_fsal_module MEM = {
	.fsal = {
		.fs_info = {
			.maxfilesize = INT64_MAX,
			.maxlink = 0,
			.maxnamelen = MAXNAMLEN,
			.maxpathlen = MAXPATHLEN,
			.no_trunc = true,
			.chown_restricted = true,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = true,
			.symlink_support = true,
			.lock_support = true,
			.lock_support_async_block = false,
			.named_attr = false,
			.unique_handles = true,
			.acl_support = 0,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = MEM_SUPPORTED_ATTRIBUTES,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.umask = 0,
			.auth_exportpath_xdev = false,
			.link_supports_permission_checks = false,
			.readdir_plus = true,
			.expire_time_parent = -1,
		}
	}
};

static struct config_item mem_items[] = {
	CONF_ITEM_UI32("Inode_Size", 0, 0x200000, 0,
		       mem_fsal_module, inode_size),
	CONF_ITEM_UI32("Up_Test_Interval", 0, UINT32_MAX, 0,
		       mem_fsal_module, up_interval),
	CONF_ITEM_UI32("Async_Threads", 0, 100, 0,
		       mem_fsal_module, async_threads),
	CONF_ITEM_BOOL("Whence_is_name", false,
		       mem_fsal_module, whence_is_name),
	CONFIG_EOL
};

static struct config_block mem_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.mem",
	.blk_desc.name = "MEM",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = mem_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

struct fridgethr *mem_async_fridge;

/**
 * Initialize subsystem
 */
static fsal_status_t
mem_async_pkginit(void)
{
	/* Return code from system calls */
	int code = 0;
	struct fridgethr_params frp;

	if (MEM.async_threads == 0) {
		/* Don't run async-threads */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	if (mem_async_fridge) {
		/* Already initialized */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = MEM.async_threads;
	frp.thr_min = 1;
	frp.flavor = fridgethr_flavor_worker;

	/* spawn MEM_ASYNC background thread */
	code = fridgethr_init(&mem_async_fridge, "MEM_ASYNC_fridge", &frp);
	if (code != 0) {
		LogMajor(COMPONENT_FSAL,
			 "Unable to initialize MEM_ASYNC fridge, error code %d.",
			 code);
	}

	LogEvent(COMPONENT_FSAL,
		 "Initialized FSAL_MEM async thread pool with %"
		 PRIu32" threads.",
		 MEM.async_threads);

	return posix2fsal_status(code);
}

/**
 * Shutdown subsystem
 *
 * @return FSAL status
 */
static fsal_status_t
mem_async_pkgshutdown(void)
{
	if (!mem_async_fridge) {
		/* Async wasn't configured */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	int rc = fridgethr_sync_command(mem_async_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_FSAL,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(mem_async_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_FSAL,
			 "Failed shutting down MEM_ASYNC threads: %d", rc);
	}

	fridgethr_destroy(mem_async_fridge);
	mem_async_fridge = NULL;
	return posix2fsal_status(rc);
}

/* private helper for export object
 */

/* Initialize mem fs info */
static fsal_status_t mem_init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct mem_fsal_module *mem_me =
	    container_of(fsal_hdl, struct mem_fsal_module, fsal);
	fsal_status_t status = {0, 0};

	LogDebug(COMPONENT_FSAL, "MEM module setup.");
	LogFullDebug(COMPONENT_FSAL,
				 "Supported attributes default = 0x%" PRIx64,
				 mem_me->fsal.fs_info.supported_attrs);

	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */
	(void) load_config_from_parse(config_struct,
				      &mem_block,
				      mem_me,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* Initialize UP calls */
	status = mem_up_pkginit();
	if (FSAL_IS_ERROR(status)) {
		LogMajor(COMPONENT_FSAL,
			 "Failed to initialize FSAL_MEM UP package %s",
			 fsal_err_txt(status));
		return status;
	}

	/* Initialize ASYNC call back threads */
	status = mem_async_pkginit();
	if (FSAL_IS_ERROR(status)) {
		LogMajor(COMPONENT_FSAL,
			 "Failed to initialize FSAL_MEM ASYNC package %s",
			 fsal_err_txt(status));
		return status;
	}

	/* Set whence_is_name in fsinfo */
	mem_me->fsal.fs_info.whence_is_name = mem_me->whence_is_name;

	display_fsinfo(&mem_me->fsal);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) MEM_SUPPORTED_ATTRIBUTES);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 mem_me->fsal.fs_info.supported_attrs);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* linkage to the exports and handle ops initializers
 */

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle.  It exists solely to
 * produce a properly constructed FSAL module handle.
 */

MODULE_INIT void init(void)
{
	int retval;
	struct fsal_module *myself = &MEM.fsal;

	retval = register_fsal(myself, memname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"MEM module failed to register.");
	}
	myself->m_ops.create_export = mem_create_export;
	myself->m_ops.update_export = mem_update_export;
	myself->m_ops.init_config = mem_init_config;
	glist_init(&MEM.mem_exports);
	MEM.next_inode = 0xc0ffee;

	/* Initialize the fsal_obj_handle ops for FSAL MEM */
	mem_handle_ops_init(&MEM.handle_ops);
}

MODULE_FINI void finish(void)
{
	int retval;

	LogDebug(COMPONENT_FSAL,
		 "MEM module finishing.");

	/* Shutdown UP calls */
	mem_up_pkgshutdown();

	/* Shutdown ASYNC threads */
	mem_async_pkgshutdown();

	retval = unregister_fsal(&MEM.fsal);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload MEM FSAL.  Dying with extreme prejudice.");
		abort();
	}
}
