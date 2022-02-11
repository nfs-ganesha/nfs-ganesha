// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#include "config.h"

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "fsal.h"
#include "fsal_api.h"
#include "fsal_types.h"
#include "fsal_pnfs.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_init.h"
#include "pnfs_utils.h"

#include "kvsfs_fsal_internal.h"
#include "kvsfs_methods.h"

static const char myname[] = "KVSFS";

/* filesystem info for your filesystem */
struct kvsfs_fsal_module KVSFS = {
	.fsal = {
		.fs_info = {
			.maxfilesize = INT64_MAX,
			.maxlink = _POSIX_LINK_MAX,
			.maxnamelen = MAXNAMLEN,
			.maxpathlen = MAXPATHLEN,
			.no_trunc = true,
			.chown_restricted = false,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = true,
			.symlink_support = false,
			.lock_support = false,
			.lock_support_async_block = false,
			.named_attr = true, /* XXX */
			.unique_handles = true,
			.acl_support = 0,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = KVSFS_SUPPORTED_ATTRIBUTES,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.umask = 0,
		}
	}
};

/* Module methods
 */

/* kvsfs_init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t kvsfs_init_config(struct fsal_module *fsal_hdl,
				     config_file_t config_struct,
				     struct config_error_type *err_type)
{
	struct kvsfs_fsal_module *kvsfs_me =
	    container_of(fsal_hdl, struct kvsfs_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL, "KVSFS module setup.");

	display_fsinfo(fsal_hdl);

	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) KVSFS_SUPPORTED_ATTRIBUTES);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 kvsfs_me->fsal.fs_info.supported_attrs);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal KVSFS method linkage to export object
 */

fsal_status_t kvsfs_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

MODULE_INIT void kvsfs_load(void)
{
	int retval;
	struct fsal_module *myself = &KVSFS.fsal;

	retval = register_fsal(myself, myname,
			       FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION,
			       FSAL_ID_KVSFS);
	if (retval != 0) {
		fprintf(stderr, "KVSFS module failed to register\n");
		return;
	}

	myself->m_ops.create_export = kvsfs_create_export;
	myself->m_ops.init_config = kvsfs_init_config;

	myself->m_ops.fsal_pnfs_ds_ops = kvsfs_pnfs_ds_ops_init;
	myself->m_ops.getdeviceinfo = kvsfs_getdeviceinfo;
	myself->m_ops.fs_da_addr_size = kvsfs_fs_da_addr_size;

	kvsfs_handle_ops_init(&KVSFS.handle_ops);
}

MODULE_FINI void kvsfs_unload(void)
{
	int retval;

	retval = unregister_fsal(&KVSFS.fsal);
	if (retval != 0) {
		fprintf(stderr, "KVSFS module failed to unregister");
		return;
	}
}
