/*
 * Copyright © 2012-2014, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * contributeur : William Allen Simpson <bill@cohortfs.com>
 *		  Marcus Watts <mdw@cohortfs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @file FSAL_CEPH/main.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @author William Allen Simpson <bill@cohortfs.com>
 * @date Wed Oct 22 13:24:33 2014
 *
 * @brief Implementation of FSAL module founctions for Ceph
 *
 * This file implements the module functions for the Ceph FSAL, for
 * initialization, teardown, configuration, and creation of exports.
 */

#include <stdlib.h>
#include <assert.h>
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "internal.h"
#include "abstract_mem.h"
#include "nfs_exports.h"
#include "export_mgr.h"

/**
 * Ceph global module object.
 */
struct ceph_fsal_module CephFSM;

/**
 * The name of this module.
 */
static const char *module_name = "Ceph";

static fsal_staticfsinfo_t default_ceph_info = {
	/* settable */
#if 0
	.umask = 0,
	.xattr_access_rights = 0,
#endif
	/* fixed */
	.symlink_support = true,
	.link_support = true,
	.cansettime = true,
	.no_trunc = true,
	.chown_restricted = true,
	.case_preserving = true,
	.unique_handles = true,
	.homogenous = true,
};

static struct config_item ceph_items[] = {
	CONF_ITEM_MODE("umask", 0, 0777, 0,
			ceph_fsal_module, fs_info.umask),
	CONF_ITEM_MODE("xattr_access_rights", 0, 0777, 0,
			ceph_fsal_module, fs_info.xattr_access_rights),
	CONFIG_EOL
};

static struct config_block ceph_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.ceph",
	.blk_desc.name = "Ceph",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = ceph_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *module_in,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct ceph_fsal_module *myself =
	    container_of(module_in, struct ceph_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL,
		 "Ceph module setup.");

	myself->fs_info = default_ceph_info;
	(void) load_config_from_parse(config_struct,
				      &ceph_block,
				      myself,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a new export under this FSAL
 *
 * This function creates a new export object for the Ceph FSAL.
 *
 * @todo ACE: We do not handle re-exports of the same cluster in a
 * sane way.  Currently we create multiple handles and cache objects
 * pointing to the same one.  This is not necessarily wrong, but it is
 * inefficient.  It may also not be something we expect to use enough
 * to care about.
 *
 * @param[in]     module_in  The supplied module handle
 * @param[in]     path       The path to export
 * @param[in]     options    Export specific options for the FSAL
 * @param[in,out] list_entry Our entry in the export list
 * @param[in]     next_fsal  Next stacked FSAL
 * @param[out]    pub_export Newly created FSAL export object
 *
 * @return FSAL status.
 */

static fsal_status_t create_export(struct fsal_module *module_in,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops)
{
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* A fake argument list for Ceph */
	const char *argv[] = { "FSAL_CEPH", op_ctx->export->fullpath };
	/* The internal export object */
	struct export *export = gsh_calloc(1, sizeof(struct export));
	/* The 'private' root handle */
	struct handle *handle = NULL;
	/* Root inode */
	struct Inode *i = NULL;
	/* Root vinode */
	vinodeno_t root;
	/* Stat for root */
	struct stat st;
	/* Return code */
	int rc;
	/* Return code from Ceph calls */
	int ceph_status;
	/* True if we have called fsal_export_init */
	bool initialized = false;

	if (export == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate export object for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	if (fsal_export_init(&export->export) != 0) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate export ops vectors for %s.",
			op_ctx->export->fullpath);
		goto error;
	}
	export_ops_init(&export->export.exp_ops);
	export->export.up_ops = up_ops;

	initialized = true;

	/* allocates ceph_mount_info */
	ceph_status = ceph_create(&export->cmount, NULL);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to create Ceph handle for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	ceph_status = ceph_conf_read_file(export->cmount, NULL);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to read Ceph configuration for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	ceph_status = ceph_conf_parse_argv(export->cmount, 2, argv);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to parse Ceph configuration for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	ceph_status = ceph_mount(export->cmount, NULL);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to mount Ceph cluster for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	if (fsal_attach_export(module_in, &export->export.exports) != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to attach export for %s.",
			op_ctx->export->fullpath);
		goto error;
	}

	export->export.fsal = module_in;

	LogDebug(COMPONENT_FSAL,
		 "Ceph module export %s.",
		 op_ctx->export->fullpath);

	root.ino.val = CEPH_INO_ROOT;
#ifdef CEPH_NOSNAP
	root.snapid.val = CEPH_NOSNAP;
#endif /* CEPH_NOSNAP */
	i = ceph_ll_get_inode(export->cmount, root);
	if (!i) {
		status.major = ERR_FSAL_SERVERFAULT;
		goto error;
	}

	rc = ceph_ll_getattr(export->cmount, i, &st, 0, 0);
	if (rc < 0) {
		status = ceph2fsal_error(rc);
		goto error;
	}

	rc = construct_handle(&st, i, export, &handle);
	if (rc < 0) {
		status = ceph2fsal_error(rc);
		goto error;
	}

	export->root = handle;
	op_ctx->fsal_export = &export->export;
	return status;

 error:
	if (i)
		ceph_ll_put(export->cmount, i);

	if (export) {
		if (export->cmount)
			ceph_shutdown(export->cmount);
		gsh_free(export);
	}

	if (initialized)
		initialized = false;

	return status;
}

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle, being called
 * before any configuration or even mounting of a Ceph cluster.  It
 * exists solely to produce a properly constructed FSAL module
 * handle.
 */

MODULE_INIT void init(void)
{
	struct fsal_module *myself = &CephFSM.fsal;

	LogDebug(COMPONENT_FSAL,
		 "Ceph module registering.");

	/* register_fsal seems to expect zeroed memory. */
	memset(myself, 0, sizeof(*myself));

	if (register_fsal(myself, module_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_CEPH) != 0) {
		/* The register_fsal function prints its own log
		   message if it fails */
		LogCrit(COMPONENT_FSAL,
			"Ceph module failed to register.");
	}

	/* Set up module operations */
#ifdef CEPH_PNFS
	myself->m_ops.fsal_pnfs_ds_ops = pnfs_ds_ops_init;
#endif				/* CEPH_PNFS */
	myself->m_ops.create_export = create_export;
	myself->m_ops.init_config = init_config;
}

/**
 * @brief Release FSAL resources
 *
 * This function unregisters the FSAL and frees its module handle.
 * The Ceph FSAL has no other resources to release on the per-FSAL
 * level.
 */

MODULE_FINI void finish(void)
{
	LogDebug(COMPONENT_FSAL,
		 "Ceph module finishing.");

	if (unregister_fsal(&CephFSM.fsal) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload Ceph FSAL.  Dying with extreme "
			"prejudice.");
		abort();
	}
}
