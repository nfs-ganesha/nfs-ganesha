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
#include <sys/types.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/fsal_init.h"

/* GPFS FSAL module private storage
 */

struct gpfs_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	/* gpfsfs_specific_initinfo_t specific_info;  placeholder */
};

const char myname[] = "GPFS";

/* filesystem info for GPFS */
static struct fsal_staticfsinfo_t default_gpfs_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = false,
	.case_insensitive = false,
	.case_preserving = true,
	.link_support = true,
	.symlink_support = true,
	.lock_support = true,
	.lock_support_owner = true,
	.lock_support_async_block = true,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW | FSAL_ACLSUPPORT_DENY,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = GPFS_SUPPORTED_ATTRIBUTES,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.umask = 0,
	.auth_exportpath_xdev = true,
	.xattr_access_rights = 0,
	.accesscheck_support = true,
	.share_support = true,
	.share_support_owner = false,
	.delegations = FSAL_OPTION_FILE_READ_DELEG, /* not working with pNFS */
	.pnfs_mds = true,
	.pnfs_ds = true,
	.fsal_trace = true,
	.reopen_method = true,
	.fsal_grace = false,
};

static struct config_item gpfs_params[] = {
	CONF_ITEM_BOOL("link_support", true,
		       fsal_staticfsinfo_t, link_support),
	CONF_ITEM_BOOL("symlink_support", true,
		       fsal_staticfsinfo_t, symlink_support),
	CONF_ITEM_BOOL("cansettime", true,
		       fsal_staticfsinfo_t, cansettime),
	CONF_ITEM_MODE("umask", 0, 0777, 0,
		       fsal_staticfsinfo_t, umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
		       fsal_staticfsinfo_t, auth_exportpath_xdev),
	CONF_ITEM_MODE("xattr_access_rights", 0, 0777, 0400,
		       fsal_staticfsinfo_t, xattr_access_rights),
	/* At the moment GPFS doesn't support WRITE delegations */
	CONF_ITEM_ENUM_BITS("Delegations",
			    FSAL_OPTION_FILE_READ_DELEG,
			    FSAL_OPTION_FILE_DELEGATIONS,
			    deleg_types, fsal_staticfsinfo_t, delegations),
	CONF_ITEM_BOOL("PNFS_MDS", true,
		       fsal_staticfsinfo_t, pnfs_mds),
	CONF_ITEM_BOOL("PNFS_DS", true,
		       fsal_staticfsinfo_t, pnfs_ds),
	CONF_ITEM_BOOL("fsal_trace", true,
		       fsal_staticfsinfo_t, fsal_trace),
	CONF_ITEM_BOOL("fsal_grace", false,
		       fsal_staticfsinfo_t, fsal_grace),
	CONFIG_EOL
};

struct config_block gpfs_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.gpfs",
	.blk_desc.name = "GPFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = gpfs_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *gpfs_staticinfo(struct fsal_module *hdl)
{
	struct gpfs_fsal_module *gpfs_me =
		container_of(hdl, struct gpfs_fsal_module, fsal);
	return &gpfs_me->fs_info;
}

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static int log_to_gpfs(log_header_t headers, void *private,
		       log_levels_t level,
		       struct display_buffer *buffer, char *compstr,
		       char *message)
{
	struct trace_arg targ;
	int retval = 0;

	if (level > 0) {
		targ.level = level;
		targ.len = strlen(compstr);
		targ.str = compstr;
		retval = gpfs_ganesha(OPENHANDLE_TRACE_ME, &targ);
	}
	return retval;
}

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct gpfs_fsal_module *gpfs_me =
	    container_of(fsal_hdl, struct gpfs_fsal_module, fsal);
	int rc;

	gpfs_me->fs_info = default_gpfs_info; /* get a copy of the defaults */

	(void) load_config_from_parse(config_struct,
				      &gpfs_param,
				      &gpfs_me->fs_info,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(&gpfs_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) GPFS_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_gpfs_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 gpfs_me->fs_info.supported_attrs);

	rc = create_log_facility("GPFS", log_to_gpfs,
				 NIV_FULL_DEBUG, LH_COMPONENT, NULL);
	if (rc != 0)
		LogCrit(COMPONENT_FSAL,
			"Could not create GPFS logger (%s)",
			strerror(-rc));
	if (gpfs_me->fs_info.fsal_trace)
		rc = enable_log_facility("GPFS");
	else
		rc = disable_log_facility("GPFS");
	if (rc != 0)
		LogCrit(COMPONENT_FSAL,
			"Could not enable GPFS logger (%s)",
			strerror(-rc));

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal GPFS method linkage to export object
 */

fsal_status_t gpfs_create_export(struct fsal_module *fsal_hdl,
				 void *parse_node,
				 struct config_error_type *err_type,
				 const struct fsal_up_vector *up_ops);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct gpfs_fsal_module GPFS;

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void gpfs_init(void)
{
	int retval;
	struct fsal_module *myself = &GPFS.fsal;

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_GPFS);
	if (retval != 0) {
		fprintf(stderr, "GPFS module failed to register");
		return;
	}

	/* Set up module operations */
	myself->m_ops.fsal_pnfs_ds_ops = pnfs_ds_ops_init;
	myself->m_ops.create_export = gpfs_create_export;
	myself->m_ops.init_config = init_config;
	myself->m_ops.getdeviceinfo = getdeviceinfo;
	myself->m_ops.fs_da_addr_size = fs_da_addr_size;
}

MODULE_FINI void gpfs_unload(void)
{
	int retval;

	release_log_facility("GPFS");

	retval = unregister_fsal(&GPFS.fsal);
	if (retval != 0) {
		fprintf(stderr, "GPFS module failed to unregister");
		return;
	}
}
