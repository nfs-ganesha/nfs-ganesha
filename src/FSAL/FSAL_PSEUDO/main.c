/*
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
#include "pseudofs_methods.h"
#include "../fsal_private.h"

const char pseudoname[] = "PSEUDO";

/* my module private storage
 */

struct pseudo_fsal_module PSEUDOFS = {
	.module = {
		.fs_info = {
			.maxfilesize = 512,
			.maxlink = 0,
			.maxnamelen = MAXNAMLEN,
			.maxpathlen = MAXPATHLEN,
			.no_trunc = true,
			.chown_restricted = true,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = false,
			.symlink_support = false,
			.lock_support = false,
			.lock_support_async_block = false,
			.named_attr = false,
			.unique_handles = true,
			.acl_support = 0,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = PSEUDO_SUPPORTED_ATTRS,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.umask = 0,
			.auth_exportpath_xdev = false,
			.link_supports_permission_checks = false,
		}
	}
};

/* private helper for export object
 */

/* Initialize pseudo fs info */
static void init_config(struct fsal_module *pseudo_fsal_module)
{
	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */

	display_fsinfo(pseudo_fsal_module);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 pseudo_fsal_module->fs_info.supported_attrs);
}

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* linkage to the exports and handle ops initializers
 */

int unload_pseudo_fsal(struct fsal_module *fsal_hdl)
{
	int retval;

	retval = unregister_fsal(&PSEUDOFS.module);
	if (retval != 0)
		fprintf(stderr, "PSEUDO module failed to unregister");

	return retval;
}

void pseudo_fsal_init(void)
{
	int retval;
	struct fsal_module *myself = &PSEUDOFS.module;

	retval = register_fsal(myself, pseudoname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		fprintf(stderr, "PSEUDO module failed to register");
		return;
	}
	myself->m_ops.create_export = pseudofs_create_export;
	myself->m_ops.unload = unload_pseudo_fsal;

	/* Initialize the fsal_obj_handle ops for FSAL PSEUDO */
	pseudofs_handle_ops_init(&PSEUDOFS.handle_ops);

	/* initialize our config */
	init_config(myself);
}
