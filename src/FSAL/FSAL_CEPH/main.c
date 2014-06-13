/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
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
 * @date Thu Jul  5 14:48:33 2012
 *
 * @brief Impelmentation of FSAL module founctions for Ceph
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
 * A local copy of the handle for this module, so it can be disposed
 * of.
 */
static struct fsal_module *module;

/**
 * The name of this module.
 */
static const char *module_name = "Ceph";

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

static fsal_status_t create_export(struct fsal_module *module,
				   void *parse_node,
				   const struct fsal_up_vector *up_ops)
{
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* True if we have called fsal_export_init */
	bool initialized = false;
	/* The internal export object */
	struct export *export = NULL;
	/* A fake argument list for Ceph */
	const char *argv[] = { "FSAL_CEPH", op_ctx->export->fullpath };
	/* Return code from Ceph calls */
	int ceph_status = 0;
	/* Root inode */
	struct Inode *i = NULL;
	/* Root vindoe */
	vinodeno_t root;
	/* Stat for root */
	struct stat st;
	/* Return code */
	int rc = 0;
	/* The 'private' root handle */
	struct handle *handle = NULL;

	export = gsh_calloc(1, sizeof(struct export));
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
	export_ops_init(export->export.ops);
	handle_ops_init(export->export.obj_ops);
#ifdef CEPH_PNFS
	ds_ops_init(export->export.ds_ops);
#endif				/* CEPH_PNFS */
	export->export.up_ops = up_ops;

	initialized = true;

	/* allocates ceph_mount_info */
	ceph_status = ceph_create(&export->cmount, NULL);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to create Ceph handle");
		goto error;
	}

	ceph_status = ceph_conf_read_file(export->cmount, NULL);
	if (ceph_status == 0)
		ceph_status = ceph_conf_parse_argv(export->cmount, 2, argv);

	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to read Ceph configuration");
		goto error;
	}

	ceph_status = ceph_mount(export->cmount, NULL);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to mount Ceph cluster.");
		goto error;
	}

	if (fsal_attach_export(module, &export->export.exports) != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to attach export.");
		goto error;
	}

	export->export.fsal = module;
	export->export.fsal = module;

	root.ino.val = CEPH_INO_ROOT;
	root.snapid.val = CEPH_NOSNAP;
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
 * handle.  Currently, we have no private, per-module data or
 * initialization.
 */

MODULE_INIT void init(void)
{
	/* register_fsal seems to expect zeroed memory. */
	module = gsh_calloc(1, sizeof(struct fsal_module));
	if (module == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate memory for Ceph FSAL module.");
		return;
	}

	if (register_fsal(module, module_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_CEPH) != 0) {
		/* The register_fsal function prints its own log
		   message if it fails */
		gsh_free(module);
		LogCrit(COMPONENT_FSAL, "Ceph module failed to register.");
	}

	/* Set up module operations */
	module->ops->create_export = create_export;
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
	if (unregister_fsal(module) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload FSAL.  Dying with extreme "
			"prejudice.");
		abort();
	}

	gsh_free(module);
	module = NULL;
}
