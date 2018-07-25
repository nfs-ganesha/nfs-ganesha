/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2017 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * @addtogroup FSAL_MDCACHE
 * @{
 */

/**
 * @file  main.c
 * @brief FSAL entry functions
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include "gsh_list.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "mdcache_hash.h"
#include "mdcache_lru.h"

pool_t *mdcache_entry_pool;

struct mdcache_stats cache_st;
struct mdcache_stats *cache_stp = &cache_st;

/* FSAL name determines name of shared library: libfsal<name>.so */
const char mdcachename[] = "MDCACHE";

/* my module private storage
 */
struct mdcache_fsal_module MDCACHE = {
	.module = {
		.fs_info = {
			.maxfilesize = UINT64_MAX,
			.maxlink = _POSIX_LINK_MAX,
			.maxnamelen = 1024,
			.maxpathlen = 1024,
			.no_trunc = true,
			.chown_restricted = true,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = true,
			.symlink_support = true,
			.lock_support = true,
			.lock_support_async_block = false,
			.named_attr = true,
			.unique_handles = true,
			.acl_support = FSAL_ACLSUPPORT_ALLOW,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = ALL_ATTRIBUTES,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.umask = 0,
			.auth_exportpath_xdev = false,
			.link_supports_permission_checks = true,
		}
	}
};

/* private helper for export object
 */

/* Module methods
 */

/* mdcache_fsal_init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t
mdcache_fsal_init_config(struct fsal_module *mdcache_fsal_module,
			 config_file_t config_struct,
			 struct config_error_type *err_type)
{
	/* Configuration setting options:
	 * 1. there are none that are changeable. (this case)
	 *
	 * 2. we set some here.  These must be independent of whatever
	 *    may be set by lower level fsals.
	 *
	 * If there is any filtering or change of parameters in the stack,
	 * this must be done in export data structures, not fsal params because
	 * a stackable could be configured above multiple fsals for multiple
	 * diverse exports.
	 */

	display_fsinfo(mdcache_fsal_module);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 mdcache_fsal_module->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Clean up caching for a FSAL export on error
 *
 * If init has an error after @ref mdcache_export_init is called, this should be
 * called to clean up any MDCACHE state on the export.  This is only intended to
 * be called on startup error.
 *
 */
void mdcache_export_uninit(void)
{
	struct mdcache_fsal_export *exp = mdc_cur_export();
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;

	fsal_put(sub_export->fsal);

	LogFullDebug(COMPONENT_FSAL,
		     "FSAL %s refcount %"PRIu32,
		     sub_export->fsal->name,
		     atomic_fetch_int32_t(&sub_export->fsal->refcount));

	fsal_detach_export(op_ctx->fsal_export->fsal,
			   &op_ctx->fsal_export->exports);
	free_export_ops(op_ctx->fsal_export);

	gsh_free(exp);

	/* Put back sub export */
	op_ctx->fsal_export = sub_export;
	op_ctx->fsal_module = sub_export->fsal;
}

/**
 * @brief Create an export for MDCACHE
 *
 * Create the stacked export for MDCACHE to allow metadata caching on another
 * export.  Unlike other Stackable FSALs, this one is created @b after the FSAL
 * underneath.  It assumes the sub-FSAL's export is already created and
 * available via the @e fsal_export member of @link op_ctx @endlink, the same
 * way that this export is returned.
 *
 * There is currently no config; FSALs that want caching should call @ref
 * mdcache_export_init
 *
 * @param[in] sub_fsal		Sub-FSAL module handle
 * @param[in] parse_node	Config node for export
 * @param[out] err_type		Parse errors
 * @param[in] super_up_ops	Upcall ops for export
 * @return FSAL status
 */
fsal_status_t
mdcache_fsal_create_export(struct fsal_module *sub_fsal, void *parse_node,
			   struct config_error_type *err_type,
			   const struct fsal_up_vector *super_up_ops)
{
	fsal_status_t status = {0, 0};
	struct mdcache_fsal_export *myself;
	int namelen;
	pthread_rwlockattr_t attrs;

	myself = gsh_calloc(1, sizeof(struct mdcache_fsal_export));
	namelen = strlen(sub_fsal->name) + 5;
	myself->name = gsh_calloc(1, namelen);
	snprintf(myself->name, namelen, "%s/MDC", sub_fsal->name);

	fsal_export_init(&myself->mfe_exp);
	mdcache_export_ops_init(&myself->mfe_exp.exp_ops);

	myself->super_up_ops = *super_up_ops; /* Struct copy */
	mdcache_export_up_ops_init(&myself->up_ops, super_up_ops);
	myself->up_ops.up_gsh_export = op_ctx->ctx_export;
	myself->up_ops.up_fsal_export = &myself->mfe_exp;
	myself->mfe_exp.up_ops = &myself->up_ops;
	myself->mfe_exp.fsal = &MDCACHE.module;

	glist_init(&myself->entry_list);
	pthread_rwlockattr_init(&attrs);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(&attrs,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	PTHREAD_RWLOCK_init(&myself->mdc_exp_lock, &attrs);
	pthread_rwlockattr_destroy(&attrs);

	status = sub_fsal->m_ops.create_export(sub_fsal,
						 parse_node,
						 err_type,
						 &myself->up_ops);
	if (FSAL_IS_ERROR(status)) {
		LogMajor(COMPONENT_FSAL,
			 "Failed to call create_export on underlying FSAL %s",
			 sub_fsal->name);
		gsh_free(myself->name);
		gsh_free(myself);
		return status;
	}

	/* Get ref for sub-FSAL */
	fsal_get(myself->mfe_exp.fsal);

	LogFullDebug(COMPONENT_FSAL,
		     "FSAL %s refcount %"PRIu32,
		     myself->mfe_exp.fsal->name,
		     atomic_fetch_int32_t(&myself->mfe_exp.fsal->refcount));

	fsal_export_stack(op_ctx->fsal_export, &myself->mfe_exp);


	/* Set up op_ctx */
	op_ctx->fsal_export = &myself->mfe_exp;
	op_ctx->fsal_module = &MDCACHE.module;

	/* Stacking is setup and ready to take upcalls now */
	up_ready_set(&myself->up_ops);

	return status;
}

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* linkage to the exports and handle ops initializers
 */


static int
mdcache_fsal_unload(struct fsal_module *fsal_hdl)
{
	fsal_status_t status;
	int retval;

	/* Destroy the cache inode AVL tree */
	cih_pkgdestroy();

	status = mdcache_lru_pkgshutdown();
	if (FSAL_IS_ERROR(status))
		fprintf(stderr, "MDCACHE LRU failed to shut down");

	/* Destroy the cache inode entry pool */
	pool_destroy(mdcache_entry_pool);
	mdcache_entry_pool = NULL;

	retval = unregister_fsal(&MDCACHE.module);
	if (retval != 0)
		fprintf(stderr, "MDCACHE module failed to unregister");

	if (FSAL_IS_ERROR(status))
		return status.major;
	return retval;
}

void mdcache_fsal_init(void)
{
	int retval;
	struct fsal_module *myself = &MDCACHE.module;

	retval = register_fsal(myself, mdcachename, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		fprintf(stderr, "MDCACHE module failed to register");
		return;
	}
	/*myself->m_ops.create_export = mdcache_fsal_create_export;*/
	myself->m_ops.init_config = mdcache_fsal_init_config;
	myself->m_ops.unload = mdcache_fsal_unload;

	/* Initialize the fsal_obj_handle ops for FSAL MDCACHE */
	mdcache_handle_ops_init(&MDCACHE.handle_ops);
}

/**
 * @brief Initialize the MDCACHE package.
 *
 * This shuold be called once at startup, after parsing config
 *
 * @param[in] parm	Parameter description
 * @return FSAL status
 */
fsal_status_t mdcache_pkginit(void)
{
	fsal_status_t status;

	if (mdcache_entry_pool)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	mdcache_entry_pool = pool_basic_init("MDCACHE Entry Pool",
					     sizeof(mdcache_entry_t));

	status = mdcache_lru_pkginit();
	if (FSAL_IS_ERROR(status)) {
		pool_destroy(mdcache_entry_pool);
		mdcache_entry_pool = NULL;
		return status;
	}

	cih_pkginit();

	return status;
}

#ifdef USE_DBUS
void mdcache_dbus_show(DBusMessageIter *iter)
{
	struct timespec timestamp;
	DBusMessageIter struct_iter;
	char *type;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	type = "cache_req";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_req);
	type = "cache_hit";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_hit);
	type = "cache_miss";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_miss);
	type = "cache_conf";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_conf);
	type = "cache_added";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_added);
	type = "cache_mapping";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_mapping);

	dbus_message_iter_close_container(iter, &struct_iter);
}
#endif /* USE_DBUS */

/** @} */
