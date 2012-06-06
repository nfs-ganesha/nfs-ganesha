/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "FSAL/fsal_init.h"

static struct fsal_module PROXY;
static struct fsal_ops pxy_ops;

/* defined the set of attributes supported with POSIX */
#define SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     |  FSAL_ATTR_FILEID  | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_RAWDEV    | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_CHGTIME  )

/* filesystem info for VFS */
fsal_staticfsinfo_t proxy_info = {
	.maxfilesize = 0xFFFFFFFFFFFFFFFFLL,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = FSAL_MAX_NAME_LEN,
	.maxpathlen = FSAL_MAX_PATH_LEN,
	.no_trunc = TRUE,
	.chown_restricted = TRUE,
	.case_preserving = TRUE,
	.fh_expire_type = FSAL_EXPTYPE_PERSISTENT,
	.link_support = TRUE,
	.symlink_support = TRUE,
	.lock_support = TRUE,
	.named_attr = TRUE,
	.unique_handles = TRUE,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.cansettime = TRUE,
	.homogenous = TRUE,
	.supported_attrs = SUPPORTED_ATTRIBUTES,
	.xattr_access_rights = 0400,
	.dirs_have_sticky_bit = TRUE
};

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct)
{
        ReturnCode(ERR_FSAL_FAULT, 0);
}

static void dump_config(struct fsal_module *fsal_hdl, int log_fd)
{
}


fsal_status_t pxy_create_export(struct fsal_module *fsal_hdl,
				const char *export_path,
				const char *fs_options,
				struct exportlist__ *exp_entry,
				struct fsal_module *next_fsal,
				struct fsal_export **export);

MODULE_INIT void 
pxy_init(void)
{
	pxy_ops.init_config = init_config;
	pxy_ops.dump_config = dump_config;
	pxy_ops.create_export = pxy_create_export;

        PROXY.ops = &pxy_ops;

	register_fsal(&PROXY, "PROXY");
}

MODULE_FINI void 
pxy_unload(void)
{
	unregister_fsal(&PROXY);
}
