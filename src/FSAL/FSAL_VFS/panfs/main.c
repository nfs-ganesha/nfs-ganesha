/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *                Patrice LUCAS     patrice.lucas@cea.fr
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

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "fsal.h"
#include "FSAL/fsal_init.h"
#include "vfs_methods.h"

/* PANFS FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define PANFS_SUPPORTED_ATTRIBUTES ((const attrmask_t) (ATTRS_POSIX | ATTR_ACL))

const char myname[] = "PANFS";

/* my module private storage
 */

static struct vfs_fsal_module PANFS = {
	.module = {
		.fs_info = {
			.maxfilesize = INT64_MAX,
			.maxlink = _POSIX_LINK_MAX,
			.maxnamelen = 1024,
			.maxpathlen = 1024,
			.no_trunc = true,
			.chown_restricted = true,
			.case_insensitive = false,
			.case_preserving = true,
			.lock_support = false,
			.lock_support_async_block = false,
			.named_attr = true,
			.unique_handles = true,
			.acl_support = FSAL_ACLSUPPORT_ALLOW |
						FSAL_ACLSUPPORT_DENY,
			.homogenous = true,
			.supported_attrs = PANFS_SUPPORTED_ATTRIBUTES,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.link_supports_permission_checks = false,
		}
	},
	.only_one_user = false
};

static struct config_item panfs_params[] = {
	CONF_ITEM_BOOL("link_support", true, vfs_fsal_module,
		       module.fs_info.link_support),
	CONF_ITEM_BOOL("symlink_support", true, vfs_fsal_module,
		       module.fs_info.symlink_support),
	CONF_ITEM_BOOL("cansettime", true, vfs_fsal_module,
		       module.fs_info.cansettime),
	CONF_ITEM_UI64("maxread", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       vfs_fsal_module, module.fs_info.maxread),
	CONF_ITEM_UI64("maxwrite", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       vfs_fsal_module, module.fs_info.maxwrite),
	CONF_ITEM_MODE("umask", 0, vfs_fsal_module,
		       module.fs_info.umask),
	CONF_ITEM_BOOL("auth_xdev_export", false, vfs_fsal_module,
		       module.fs_info.auth_exportpath_xdev),
	CONF_ITEM_BOOL("only_one_user", false, vfs_fsal_module,
		       only_one_user),
	CONFIG_EOL
};

struct config_block panfs_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.panfs",
	.blk_desc.name = "PANFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = panfs_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *panfs_fsal_module,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct vfs_fsal_module *panfs_module =
	    container_of(panfs_fsal_module, struct vfs_fsal_module, module);

	LogFullDebug(COMPONENT_FSAL,
		"Supported attributes default = 0x%" PRIx64,
		panfs_module->module.fs_info.supported_attrs);

	(void) load_config_from_parse(config_struct,
				      &panfs_param,
				      panfs_module,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(&panfs_module->module);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     PANFS_SUPPORTED_ATTRIBUTES);

	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 panfs_module->module.fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal PANFS method linkage to export object
 */

fsal_status_t vfs_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void panfs_init(void)
{
	int retval;
	struct fsal_module *myself = &PANFS.module;

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_PANFS);
	if (retval != 0) {
		fprintf(stderr, "PANFS module failed to register");
		return;
	}
	myself->m_ops.create_export = vfs_create_export;
	myself->m_ops.init_config = init_config;
}

MODULE_FINI void panfs_unload(void)
{
	int retval;

	retval = unregister_fsal(&PANFS.module);
	if (retval != 0) {
		fprintf(stderr, "PANFS module failed to unregister");
		return;
	}
}
