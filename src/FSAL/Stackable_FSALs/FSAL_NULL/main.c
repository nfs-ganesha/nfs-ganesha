/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
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
#include "gsh_list.h"
#include "FSAL/fsal_init.h"
#include "nullfs_methods.h"


/* FSAL name determines name of shared library: libfsal<name>.so */
const char myname[] = "NULL";

/* my module private storage
 */

struct null_fsal_module NULLFS = {
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

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *nullfs_fsal_module,
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

	display_fsinfo(nullfs_fsal_module);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 nullfs_fsal_module->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal NULLFS method linkage to export object
 */

fsal_status_t nullfs_create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops);

fsal_status_t nullfs_update_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   struct config_error_type *err_type,
				   struct fsal_export *original,
				   struct fsal_module *updated_super);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* linkage to the exports and handle ops initializers
 */
MODULE_INIT void nullfs_init(void)
{
	int retval;
	struct fsal_module *myself = &NULLFS.module;

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		fprintf(stderr, "NULLFS module failed to register");
		return;
	}
	myself->m_ops.create_export = nullfs_create_export;
	myself->m_ops.update_export = nullfs_update_export;
	myself->m_ops.init_config = init_config;

	/* Initialize the fsal_obj_handle ops for FSAL NULL */
	nullfs_handle_ops_init(&NULLFS.handle_ops);
}

MODULE_FINI void nullfs_unload(void)
{
	int retval;

	retval = unregister_fsal(&NULLFS.module);
	if (retval != 0) {
		fprintf(stderr, "NULLFS module failed to unregister");
		return;
	}
}
