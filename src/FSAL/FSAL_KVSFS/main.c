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

#include "fsal_internal.h"
#include "kvsfs_methods.h"

const char myname[] = "KVSFS";

/* filesystem info for your filesystem */
static struct fsal_staticfsinfo_t default_kvsfs_info = {
	.maxfilesize = UINT64_MAX,		/* Max fiule size */
	.maxlink = 1024,	/* max links for an object of your filesystem */
	.maxnamelen = MAXNAMLEN,		/* max filename */
	.maxpathlen = MAXPATHLEN,		/* min filename */
	.no_trunc = true,			/* no_trunc */
	.chown_restricted = false,		/* chown restricted */
	.case_insensitive = false,		/* case insensitivity */
	.case_preserving = true,		/* case preserving */
	.link_support = true,
	.symlink_support = true,
	.lock_support = false,	/* lock support */
	.lock_support_async_block = false,	/* async blocking locks */
	.named_attr = true,			/* named attributes */
	.unique_handles = true,		/* handles are unique and persistent */
	.acl_support = FSAL_ACLSUPPORT_ALLOW,	/* ACL support */
	.cansettime = true,
	.homogenous = true,			/* homogenous */
	.supported_attrs = KVSFS_SUPPORTED_ATTRIBUTES, /* supp attributes */
	.link_supports_permission_checks = true,
	.pnfs_mds = false,
	.pnfs_ds = false,
	.fsal_trace = false,
	.fsal_grace = false,
	.link_supports_permission_checks = true,
};

static struct config_item kvsfs_params[] = {
	CONF_ITEM_BOOL("link_support", true,
		       kvsfs_fsal_module, fs_info.link_support),
	CONF_ITEM_BOOL("symlink_support", true,
		       kvsfs_fsal_module, fs_info.symlink_support),
	CONF_ITEM_BOOL("cansettime", true,
		       kvsfs_fsal_module, fs_info.cansettime),
	CONF_ITEM_UI32("maxread", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       kvsfs_fsal_module, fs_info.maxread),
	CONF_ITEM_UI32("maxwrite", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       kvsfs_fsal_module, fs_info.maxwrite),
	CONF_ITEM_MODE("umask", 0,
		       kvsfs_fsal_module, fs_info.umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
		       kvsfs_fsal_module, fs_info.auth_exportpath_xdev),
	CONF_ITEM_BOOL("fsal_trace", true, fsal_staticfsinfo_t, fsal_trace),
	CONF_ITEM_BOOL("fsal_grace", false, fsal_staticfsinfo_t, fsal_grace),
	CONFIG_EOL
};

struct config_block kvsfs_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.kvsfs",
	.blk_desc.name = "KVSFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = kvsfs_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *kvsfs_staticinfo(struct fsal_module *hdl)
{
	struct kvsfs_fsal_module *myself;

	myself = container_of(hdl, struct kvsfs_fsal_module, fsal);
	return &myself->fs_info;
}

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

	kvsfs_me->fs_info = default_kvsfs_info;	/* copy the consts */
	(void) load_config_from_parse(config_struct,
				      &kvsfs_param,
				      &kvsfs_me,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(&kvsfs_me->fsal);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) KVSFS_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_kvsfs_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 kvsfs_me->fs_info.supported_attrs);
	
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

static struct kvsfs_fsal_module KVSFS;

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
