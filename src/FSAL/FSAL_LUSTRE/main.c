/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* main.c
 * Module core functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "FSAL/fsal_init.h"

/* LUSTRE FSAL module private storage
 */

struct lustre_fsal_module {	
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	fsal_init_info_t fsal_info;
	 /* vfsfs_specific_initinfo_t specific_info;  placeholder */
};

const char myname[] = "LUSTRE";

/* filesystem info for LUSTRE */
static struct fsal_staticfsinfo_t default_posix_info = {
	.maxfilesize = 0xFFFFFFFFFFFFFFFFLL, /* (64bits) */
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.fh_expire_type = FSAL_EXPTYPE_PERSISTENT,
	.link_support = true,
	.symlink_support = true,
	.lock_support = true,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = LUSTRE_SUPPORTED_ATTRIBUTES,
	.maxread = 0,
	.maxwrite = 0,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400, /* root=RW, owner=R */
	.dirs_have_sticky_bit = true
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

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct)
{
	struct lustre_fsal_module *lustre_me
		= container_of(fsal_hdl, struct lustre_fsal_module, fsal);
	fsal_status_t fsal_status;

	lustre_me->fs_info = default_posix_info; /* get a copy of the defaults */

        fsal_status = fsal_load_config(fsal_hdl->ops->get_name(fsal_hdl),
                                       config_struct,
                                       &lustre_me->fsal_info,
                                       &lustre_me->fs_info,
                                       NULL);

	if(FSAL_IS_ERROR(fsal_status))
		return fsal_status;
	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */

	display_fsinfo(&lustre_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%"PRIx64,
                     (uint64_t) LUSTRE_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%"PRIx64,
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%"PRIx64,
		 lustre_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal LUSTRE method linkage to export object
 */

fsal_status_t lustre_create_export(struct fsal_module *fsal_hdl,
                                   const char *export_path,
                                   const char *fs_options,
                                   struct exportlist__ *exp_entry,
                                   struct fsal_module *next_fsal,
                                   const struct fsal_up_vector *up_ops,
                                   struct fsal_export **export);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct lustre_fsal_module LUSTRE;

/* linkage to the exports and handle ops initializers
 */

void lustre_export_ops_init(struct export_ops *ops);
void lustre_handle_ops_init(struct fsal_obj_ops *ops);

MODULE_INIT void lustre_init(void) {
	int retval;
	struct fsal_module *myself = &LUSTRE.fsal;

	retval = register_fsal(myself, myname,
			       FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION);
	if(retval != 0) {
		fprintf(stderr, "LUSTRE module failed to register");
		return;
	}
	myself->ops->create_export = lustre_create_export;
	myself->ops->init_config = init_config;
	lustre_export_ops_init(myself->exp_ops);
	lustre_handle_ops_init(myself->obj_ops);
        init_fsal_parameters(&LUSTRE.fsal_info);
}

MODULE_FINI void lustre_unload(void) {
	int retval;

	retval = unregister_fsal(&LUSTRE.fsal);
	if(retval != 0) {
		fprintf(stderr, "LUSTRE module failed to unregister");
		return;
	}
}
