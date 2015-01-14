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

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "fsal.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "lustre_methods.h"
#include "FSAL/fsal_init.h"

bool pnfs_enabled;

/* filesystem info for LUSTRE */
static struct fsal_staticfsinfo_t lustre_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.lock_support = true,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.homogenous = true,
	.supported_attrs = LUSTRE_SUPPORTED_ATTRIBUTES,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.pnfs_mds = true,
	.pnfs_ds = true,
};

static void *dataserver_init(void *link_mem, void *self_struct)
{
	struct lustre_pnfs_ds_parameter *child_param
		= (struct lustre_pnfs_ds_parameter *)self_struct;

	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL) {
		struct glist_head *dslist = (struct glist_head *)self_struct;
		struct lustre_pnfs_parameter *pnfs_param;

		pnfs_param = container_of(dslist,
					  struct lustre_pnfs_parameter,
					  ds_list);
		glist_init(&pnfs_param->ds_list);
		return self_struct;
	} else if (self_struct == NULL) {
		child_param = gsh_calloc(1,
				 sizeof(struct lustre_pnfs_ds_parameter));
		if (child_param == NULL)
			return NULL;

		glist_init(&child_param->ds_list);
		return (void *)child_param;
	} else {
		assert(glist_empty(&child_param->ds_list));

		gsh_free(child_param);
		return NULL;
	}
}

static int dataserver_commit(void *node, void *link_mem, void *self_struct,
			     struct config_error_type *err_type)
{
	struct glist_head *ds_head
		= (struct glist_head *)link_mem;
	struct lustre_pnfs_ds_parameter *child_param
		= (struct lustre_pnfs_ds_parameter *)self_struct;

	glist_add_tail(ds_head, &child_param->ds_list);
	return 0;
}

static struct config_item ds_params[] = {
	CONF_MAND_IPV4_ADDR("DS_Addr", "127.0.0.1",
			    lustre_pnfs_ds_parameter, ipaddr),
	CONF_MAND_INET_PORT("DS_Port", 1024, UINT16_MAX, 3260,
		       lustre_pnfs_ds_parameter, ipport), /* use iscsi port */
	CONF_MAND_UI32("DS_Id", 1, UINT32_MAX, 1,
		       lustre_pnfs_ds_parameter, id),
	CONFIG_EOL
};

static int lustre_conf_pnfs_commit(void *node,
				   void *link_mem,
				   void *self_struct,
				   struct config_error_type *err_type)
{
	/* struct lustre_pnfs_param *lpp = self_struct; */

	/* Verifications/parameter checking to be added here */

	return 0;
}

static struct config_item pnfs_params[] = {
	CONF_ITEM_BLOCK("DataServer", ds_params,
			dataserver_init, dataserver_commit,
			lustre_pnfs_parameter, ds_list),
	CONFIG_EOL
};

static struct config_item lustre_params[] = {
	CONF_ITEM_BOOL("link_support", true,
		       lustre_fsal_module, fs_info.link_support),
	CONF_ITEM_BOOL("symlink_support", true,
		       lustre_fsal_module, fs_info.symlink_support),
	CONF_ITEM_BOOL("cansettime", true,
		       lustre_fsal_module, fs_info.cansettime),
	CONF_ITEM_UI64("maxread", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       lustre_fsal_module, fs_info.maxread),
	CONF_ITEM_UI64("maxwrite", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       lustre_fsal_module, fs_info.maxwrite),
	CONF_ITEM_MODE("umask", 0, 0777, 0,
		       lustre_fsal_module, fs_info.umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
		       lustre_fsal_module, fs_info.auth_exportpath_xdev),
	CONF_ITEM_MODE("xattr_access_rights", 0, 0777, 0400,
		       lustre_fsal_module, fs_info.xattr_access_rights),
	CONF_ITEM_BLOCK("PNFS", pnfs_params,
			noop_conf_init, lustre_conf_pnfs_commit,
			lustre_fsal_module, pnfs_param),
	CONFIG_EOL
};

struct config_block lustre_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.lustre",
	.blk_desc.name = "LUSTRE",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = lustre_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *lustre_staticinfo(struct fsal_module *hdl)
{
	struct lustre_fsal_module *myself;

	myself = container_of(hdl, struct lustre_fsal_module, fsal);
	return &myself->fs_info;
}

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t lustre_init_config(struct fsal_module *fsal_hdl,
					config_file_t config_struct,
					struct config_error_type *err_type)
{
	struct lustre_fsal_module *lustre_me =
	    container_of(fsal_hdl, struct lustre_fsal_module, fsal);

	lustre_me->fs_info = lustre_info; /* get a copy of the defaults */
	/* Read FS parameter for this FSAL */
	(void) load_config_from_parse(config_struct,
				      &lustre_param,
				      lustre_me,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(&lustre_me->fs_info);

	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) LUSTRE_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%"PRIx64,
		     lustre_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 lustre_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal LUSTRE method linkage to export object
 */

fsal_status_t lustre_create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct lustre_fsal_module LUSTRE;

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void lustre_init(void)
{
	int retval;
	struct fsal_module *myself = &LUSTRE.fsal;

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_LUSTRE);
	if (retval != 0) {
		fprintf(stderr, "LUSTRE module failed to register");
		return;
	}

	/* Set up module operations */
	myself->m_ops.fsal_pnfs_ds_ops = lustre_pnfs_ds_ops_init;
	myself->m_ops.create_export = lustre_create_export;
	myself->m_ops.init_config = lustre_init_config;
	myself->m_ops.getdeviceinfo = lustre_getdeviceinfo;
	myself->m_ops.fs_da_addr_size = lustre_fs_da_addr_size;
}

MODULE_FINI void lustre_unload(void)
{
	int retval;

	retval = unregister_fsal(&LUSTRE.fsal);
	if (retval != 0) {
		fprintf(stderr, "LUSTRE module failed to unregister");
		return;
	}
}
