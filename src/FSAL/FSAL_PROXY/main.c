/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 */

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "fsal.h"
#include "FSAL/fsal_init.h"
#include "pxy_fsal_methods.h"

#define PROXY_SUPPORTED_ATTRS ((const attrmask_t) (ATTRS_POSIX))

/* filesystem info for PROXY */
struct pxy_fsal_module PROXY = {
	.module = {
		.fs_info = {
			.maxfilesize = INT64_MAX,
			.maxlink = _POSIX_LINK_MAX,
			.maxnamelen = 1024,
			.maxpathlen = 1024,
			.no_trunc = true,
			.chown_restricted = true,
			.case_preserving = true,
			.lock_support = false,
			.named_attr = true,
			.unique_handles = true,
			.acl_support = FSAL_ACLSUPPORT_ALLOW,
			.homogenous = true,
			.supported_attrs = PROXY_SUPPORTED_ATTRS,
			.link_supports_permission_checks = true,
		}
	}
};

/**
 * @brief Validate and commit the proxy params
 *
 * This is also pretty simple.  Just a NOP in both cases.
 *
 * @param link_mem - pointer to the link_mem struct memory.
 * @param self_struct - NULL for init parent, not NULL for attaching
 */

static struct config_item proxy_params[] = {
	CONF_ITEM_BOOL("link_support", true,
				pxy_fsal_module,
				module.fs_info.link_support),
	CONF_ITEM_BOOL("symlink_support", true,
				pxy_fsal_module,
				module.fs_info.symlink_support),
	CONF_ITEM_BOOL("cansettime", true,
				pxy_fsal_module,
				module.fs_info.cansettime),
	CONF_ITEM_UI64("maxread", 512,
		       FSAL_MAXIOSIZE - SEND_RECV_HEADER_SPACE,
		       DEFAULT_MAX_WRITE_READ,
					 pxy_fsal_module,
					 module.fs_info.maxread),
	CONF_ITEM_UI64("maxwrite", 512,
		       FSAL_MAXIOSIZE - SEND_RECV_HEADER_SPACE,
		       DEFAULT_MAX_WRITE_READ,
		       pxy_fsal_module,
		       module.fs_info.maxwrite),
	CONF_ITEM_MODE("umask", 0,
				pxy_fsal_module,
				module.fs_info.umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
				pxy_fsal_module,
				module.fs_info.auth_exportpath_xdev),

	CONFIG_EOL
};

struct config_block proxy_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.proxy",
	.blk_desc.name = "PROXY",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = proxy_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static fsal_status_t pxy_init_config(struct fsal_module *fsal_hdl,
				     config_file_t config_struct,
				     struct config_error_type *err_type)
{
	struct pxy_fsal_module *pxy =
	    container_of(fsal_hdl, struct pxy_fsal_module, module);

	(void) load_config_from_parse(config_struct,
				      &proxy_param,
				      pxy,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	display_fsinfo(&pxy->module);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

MODULE_INIT void pxy_init(void)
{
	if (register_fsal(&PROXY.module, "PROXY", FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS) != 0)
		return;
	PROXY.module.m_ops.init_config = pxy_init_config;
	PROXY.module.m_ops.create_export = pxy_create_export;

	/* Initialize the fsal_obj_handle ops for FSAL PROXY */
	pxy_handle_ops_init(&PROXY.handle_ops);
}

MODULE_FINI void pxy_unload(void)
{
	int retval;

	retval = unregister_fsal(&PROXY.module);

	if (retval != 0) {
		fprintf(stderr, "PROXY module failed to unregister : %d",
			retval);
		return;
	}
}
