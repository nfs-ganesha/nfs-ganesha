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
#include "mem_methods.h"
#include "../fsal_private.h"

/* MEM FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define MEM_SUPPORTED_ATTRIBUTES (				\
	ATTR_TYPE     | ATTR_SIZE     |				\
	ATTR_FSID     | ATTR_FILEID   |				\
	ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     |	\
	ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    |	\
	ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | ATTR_CHGTIME)

struct mem_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	/* mem_specific_initinfo_t specific_info;  placeholder */
};

const char memname[] = "MEM";

/* filesystem info for MEM */
static struct fsal_staticfsinfo_t default_mem_info = {
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
	.lock_support = true,
	.lock_support_owner = true,
	.lock_support_async_block = false,
	.named_attr = false,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = 0,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = MEM_SUPPORTED_ATTRIBUTES,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400,	/* root=RW, owner=R */
	.link_supports_permission_checks = false,
};

static struct config_item mem_items[] = {
	CONF_ITEM_NOOP("placeholder"),
	CONFIG_EOL
};

static struct config_block mem_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.mem",
	.blk_desc.name = "MEM",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = mem_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *mem_staticinfo(struct fsal_module *hdl)
{
	struct mem_fsal_module *myself;

	myself = container_of(hdl, struct mem_fsal_module, fsal);
	return &myself->fs_info;
}

/* Initialize mem fs info */
static fsal_status_t mem_init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct mem_fsal_module *mem_me =
	    container_of(fsal_hdl, struct mem_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL,
		 "MEM module setup.");

	/* get a copy of the defaults */
	mem_me->fs_info = default_mem_info;

	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */
	(void) load_config_from_parse(config_struct,
				      &mem_block,
				      mem_me,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	display_fsinfo(&mem_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) MEM_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_mem_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 mem_me->fs_info.supported_attrs);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Indicate support for extended operations.
 *
 * @retval true if extended operations are supported.
 */

bool mem_support_ex(struct fsal_obj_handle *obj)
{
	return true;
}

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct mem_fsal_module MEM;

/* linkage to the exports and handle ops initializers
 */

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle.  It exists solely to
 * produce a properly constructed FSAL module handle.
 */

MODULE_INIT void init(void)
{
	int retval;
	struct fsal_module *myself = &MEM.fsal;

	/* register_fsal seems to expect zeroed memory. */
	memset(myself, 0, sizeof(*myself));

	retval = register_fsal(myself, memname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"MEM module failed to register.");
	}
	myself->m_ops.create_export = mem_create_export;
	myself->m_ops.init_config = mem_init_config;
	myself->m_ops.support_ex = mem_support_ex;
}

MODULE_FINI void finish(void)
{
	int retval;

	LogDebug(COMPONENT_FSAL,
		 "MEM module finishing.");

	retval = unregister_fsal(&MEM.fsal);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload MEM FSAL.  Dying with extreme prejudice.");
		abort();
	}
}
