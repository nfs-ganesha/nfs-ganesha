/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
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
#include "scality_methods.h"
#include "../fsal_private.h"

/* SCALITY FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define SCALITY_SUPPORTED_ATTRIBUTES (				\
	ATTR_TYPE     | ATTR_SIZE     |				\
	ATTR_FSID     | ATTR_FILEID   |				\
	ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     |	\
	ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    |	\
	ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED |	\
	ATTR_CHGTIME)


const char myname[] = "SCALITY";

/* filesystem info for SCALITY */
static struct fsal_staticfsinfo_t default_posix_info = {
	.maxfilesize = UINT64_MAX,
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
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = false,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = 0,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = SCALITY_SUPPORTED_ATTRIBUTES,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400,	/* root=RW, owner=R */
	.link_supports_permission_checks = false,
};


static struct config_item scality_params[] = {
	CONF_MAND_STR("dbd_url", 1, MAXPATHLEN, NULL,
		       scality_fsal_module, dbd_url),
	CONF_MAND_STR("sproxyd_url", 1, MAXPATHLEN, NULL,
		       scality_fsal_module, sproxyd_url),
	CONF_ITEM_STR("redis_host", 1, MAXHOSTNAMELEN, "127.0.0.1",
		      scality_fsal_module, redis_host),
	CONF_ITEM_INET_PORT("redis_port", 1, UINT16_MAX/2, 6379,
			    scality_fsal_module, redis_port),
	CONFIG_EOL
};

struct config_block scality_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.scality",
	.blk_desc.name = "SCALITY",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = scality_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};



/* private helper for export object
 */

struct fsal_staticfsinfo_t *scality_staticinfo(struct fsal_module *hdl)
{
	struct scality_fsal_module *myself;

	myself = container_of(hdl, struct scality_fsal_module, fsal);
	return &myself->fs_info;
}

/* Initialize scality fs info */
static fsal_status_t init_config(struct fsal_module *fsal_hdl,
			config_file_t config_struct,
			struct config_error_type *err_type)
{
	struct scality_fsal_module *scality_me =
	    container_of(fsal_hdl, struct scality_fsal_module, fsal);

	/* get a copy of the defaults */
	scality_me->fs_info = default_posix_info;

	int rc = load_config_from_parse(config_struct,
				      &scality_param,
				      scality_me,
				      true,
				      err_type);
	if ( rc < 0 ) {
		LogCrit(COMPONENT_FSAL, "Load configuration failed");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	display_fsinfo(&scality_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) SCALITY_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 scality_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct scality_fsal_module SCALITY;

/* linkage to the exports and handle ops initializers
 */

MODULE_FINI int
unload_scality_fsal(struct fsal_module *fsal_hdl)
{
	int retval;

	retval = unregister_fsal(&SCALITY.fsal);
	if (retval != 0)
		fprintf(stderr, "SCALITY module failed to unregister");

	return retval;
}

MODULE_INIT void
scality_fsal_init(void)
{
	int retval;
	struct fsal_module *myself = &SCALITY.fsal;

	retval = register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_EXPERIMENTAL);
	if (retval != 0) {
		fprintf(stderr, "SCALITY module failed to register");
		return;
	}
	myself->m_ops.create_export = scality_create_export;
	myself->m_ops.init_config = init_config;
	myself->m_ops.unload = unload_scality_fsal;
	myself->name = gsh_strdup(myname);

}
