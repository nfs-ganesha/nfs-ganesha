/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2016 Red Hat, Inc. and/or its affiliates.
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

/* MDCACHE FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define MDCACHE_SUPPORTED_ATTRIBUTES (                    \
	ATTR_TYPE     | ATTR_SIZE     |                  \
	ATTR_FSID     | ATTR_FILEID   |                  \
	ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
	ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
	ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
	ATTR_CHGTIME)

struct mdcache_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	/* mdcachefs_specific_initinfo_t specific_info;  placeholder */
};

struct mdcache_stats cache_st;
struct mdcache_stats *cache_stp = &cache_st;

/* my module private storage
 */

static struct mdcache_fsal_module MDCACHE;

/* FSAL name determines name of shared library: libfsal<name>.so */
const char mdcachename[] = "MDCACHE";

/* filesystem info for MDCACHE */
static struct fsal_staticfsinfo_t default_posix_info = {
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
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = MDCACHE_SUPPORTED_ATTRIBUTES,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400,	/* root=RW, owner=R */
	.link_supports_permission_checks = true,
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *mdcache_staticinfo(struct fsal_module *hdl)
{
	struct mdcache_fsal_module *myself;

	myself = container_of(hdl, struct mdcache_fsal_module, fsal);
	return &myself->fs_info;
}

/* Module methods
 */

/* mdcache_fsal_init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t
mdcache_fsal_init_config(struct fsal_module *fsal_hdl,
			 config_file_t config_struct,
			 struct config_error_type *err_type)
{
	struct mdcache_fsal_module *mdcache_me =
	    container_of(fsal_hdl, struct mdcache_fsal_module, fsal);

	/* get a copy of the defaults */
	mdcache_me->fs_info = default_posix_info;

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

	display_fsinfo(&mdcache_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) MDCACHE_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 mdcache_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Enable caching for a FSAL export
 *
 * This is the API to call to enable caching on an export.  The sub-FSAL calls
 * this with with the up_ops that were passed to it, and is wrapped in a
 * FSAL_MDCACHE instance to do caching.  @ref op_ctx should already be
 * initialized by the sub-FSAL.  On success, @a mdc_up_ops will contain
 * the up_ops for the MDCACHE instance, that the sub-FSAL can the specialize.
 *
 * @see mdcache_fsal_create_export
 *
 * @param[in] super_up_ops	FSAL_UP ops for the super-FSAL
 * @param[out] mdc_up_ops	FSAL_UP ops for MDCACHE
 * @return FSAL status
 */
fsal_status_t mdcache_export_init(const struct fsal_up_vector *super_up_ops,
				  const struct fsal_up_vector **mdc_up_ops)
{
	struct mdcache_fsal_export *exp;
	struct fsal_up_vector my_up_ops;
	fsal_status_t status;

	*mdc_up_ops = NULL;
	mdcache_export_up_ops_init(&my_up_ops, super_up_ops);
	status =  mdc_init_export(&MDCACHE.fsal, &my_up_ops, super_up_ops);
	if (FSAL_IS_ERROR(status))
		return status;

	/* Get ref on FSAL MDCACHE for sub FSAL */
	fsal_get(&MDCACHE.fsal);
	exp = mdc_cur_export();
	*mdc_up_ops = &exp->up_ops;

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
	struct fsal_export *sub_export = exp->export.sub_export;

	fsal_put(sub_export->fsal);

	fsal_detach_export(op_ctx->fsal_export->fsal,
			   &op_ctx->fsal_export->exports);
	free_export_ops(op_ctx->fsal_export);

	gsh_free(exp);

	/* Put back sub export */
	op_ctx->fsal_export = sub_export;
	op_ctx->fsal_module = sub_export->fsal;
}

/* Internal MDCACHE method linkage to export object
 */

fsal_status_t mdcache_fsal_create_export(struct fsal_module *fsal_hdl,
					 void *parse_node,
					 struct config_error_type *err_type,
					 const struct fsal_up_vector *up_ops);

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

	retval = unregister_fsal(&MDCACHE.fsal);
	if (retval != 0)
		fprintf(stderr, "MDCACHE module failed to unregister");

	if (FSAL_IS_ERROR(status))
		return status.major;
	return retval;
}

 /**
 * @brief Get the support_ex for the handle
 *
 * Just pass through to the underlying FSAL
 *
 * @param[in] obj_hdl	Handle to digest
 * @param[out] fh_desc	Buffer to write key into
 * @return True if supported, false otherwise
 */
static bool mdcache_support_ex(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	return entry->sub_handle->fsal->m_ops.support_ex(entry->sub_handle);
}

void mdcache_fsal_init(void)
{
	int retval;
	struct fsal_module *myself = &MDCACHE.fsal;

	retval = register_fsal(myself, mdcachename, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		fprintf(stderr, "MDCACHE module failed to register");
		return;
	}
	myself->m_ops.create_export = mdcache_fsal_create_export;
	myself->m_ops.init_config = mdcache_fsal_init_config;
	myself->m_ops.unload = mdcache_fsal_unload;
	myself->m_ops.support_ex = mdcache_support_ex;
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
