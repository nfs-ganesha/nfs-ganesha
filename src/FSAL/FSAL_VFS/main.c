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

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "FSAL/fsal_init.h"

/* VFS FSAL module private storage
 */

struct vfs_fsal_module {	
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	fsal_init_info_t fsal_info;
	fs_common_initinfo_t common_info;
	 /* vfsfs_specific_initinfo_t specific_info;  placeholder */
};

/* I keep a static pointer to my instance
 * needed for ctor/dtor ops
 */
struct fsal_module *myself;
const char myname[] = "VFS";

/* filesystem info for VFS */
static fsal_staticfsinfo_t default_posix_info = {
	.maxfilesize = 0xFFFFFFFFFFFFFFFFLL, /* (64bits) */
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = FSAL_MAX_NAME_LEN,
	.maxpathlen = FSAL_MAX_PATH_LEN,
	.no_trunc = TRUE,
	.chown_restricted = TRUE,
	.case_insensitive = FALSE,
	.case_preserving = TRUE,
	.fh_expire_type = FSAL_EXPTYPE_PERSISTENT,
	.link_support = TRUE,
	.symlink_support = TRUE,
	.lock_support = TRUE,
	.lock_support_owner = FALSE,
	.lock_support_async_block = FALSE,
	.named_attr = TRUE,
	.unique_handles = TRUE,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.cansettime = TRUE,
	.homogenous = TRUE,
	.supported_attrs = VFS_SUPPORTED_ATTRIBUTES,
	.maxread = 0,
	.maxwrite = 0,
	.umask = 0,
	.auth_exportpath_xdev = FALSE,
	.xattr_access_rights = 0400, /* root=RW, owner=R */
	.dirs_have_sticky_bit = TRUE
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *vfs_staticinfo(struct fsal_module *hdl)
{
	struct vfs_fsal_module *myself;

	myself = container_of(hdl, struct vfs_fsal_module, fsal);
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
	struct vfs_fsal_module *vfs_me
		= container_of(fsal_hdl, struct vfs_fsal_module, fsal);
	fsal_status_t fsal_status;

	fsal_status = load_FSAL_parameters_from_conf(config_struct,
						     &vfs_me->fsal_info);
	if(FSAL_IS_ERROR(fsal_status))
		return fsal_status;
	fsal_status = load_FS_common_parameters_from_conf(config_struct,
							  &vfs_me->common_info);
	if(FSAL_IS_ERROR(fsal_status))
		return fsal_status;
	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */

	/* Analyzing fs_common_info struct */

	if((vfs_me->common_info.behaviors.maxfilesize != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.maxlink != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.maxnamelen != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.maxpathlen != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.no_trunc != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.case_insensitive != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.case_preserving != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.named_attr != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.lease_time != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.supported_attrs != FSAL_INIT_FS_DEFAULT) ||
	   (vfs_me->common_info.behaviors.homogenous != FSAL_INIT_FS_DEFAULT))
		ReturnCode(ERR_FSAL_NOTSUPP, 0);

	vfs_me->fs_info = default_posix_info; /* get a copy of the defaults */

	SET_BOOLEAN_PARAM(vfs_me->fs_info, &vfs_me->common_info, symlink_support);
	SET_BOOLEAN_PARAM(vfs_me->fs_info, &vfs_me->common_info, link_support);
	SET_BOOLEAN_PARAM(vfs_me->fs_info, &vfs_me->common_info, lock_support);
	SET_BOOLEAN_PARAM(vfs_me->fs_info, &vfs_me->common_info, lock_support_owner);
	SET_BOOLEAN_PARAM(vfs_me->fs_info, &vfs_me->common_info, lock_support_async_block);
	SET_BOOLEAN_PARAM(vfs_me->fs_info, &vfs_me->common_info, cansettime);
	SET_INTEGER_PARAM(vfs_me->fs_info, &vfs_me->common_info, maxread);
	SET_INTEGER_PARAM(vfs_me->fs_info, &vfs_me->common_info, maxwrite);
	SET_BITMAP_PARAM(vfs_me->fs_info, &vfs_me->common_info, umask);
	SET_BOOLEAN_PARAM(vfs_me->fs_info, &vfs_me->common_info, auth_exportpath_xdev);
	SET_BITMAP_PARAM(vfs_me->fs_info, &vfs_me->common_info, xattr_access_rights);

	display_fsinfo(&vfs_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%llX.",
		     VFS_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%llX.",
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%llX.",
		 vfs_me->fs_info.supported_attrs);
	ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static void dump_config(struct fsal_module *fsal_hdl, int log_fd)
{
}


/* Internal VFS method linkage to export object
 */

fsal_status_t vfs_create_export(struct fsal_module *fsal_hdl,
				fsal_path_t *export_path,
				char *fs_options,
				struct fsal_export **export);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

MODULE_INIT void vfs_init(void) {
	int retval;
	struct vfs_fsal_module *vfs_me;
	struct fsal_module *myself;

	vfs_me = malloc(sizeof(struct vfs_fsal_module)+sizeof(struct fsal_ops));
	if(vfs_me== NULL) {
		LogCrit(COMPONENT_FSAL,
			 "vfs_init: VFS module cannot allocate space for itself");
		return;
	}
	memset(vfs_me, 0, sizeof(struct vfs_fsal_module)+sizeof(struct fsal_ops));
	myself = &vfs_me->fsal;
	myself->ops = (struct fsal_ops *) &vfs_me[1];
	retval = register_fsal(myself, myname);
	if(retval != 0) {
		free(vfs_me);
		myself = NULL;
		return;
	}
	myself->ops->init_config = init_config;
	myself->ops->dump_config = dump_config;
	myself->ops->create_export = vfs_create_export;
	init_fsal_parameters(&vfs_me->fsal_info, &vfs_me->common_info);
}

MODULE_FINI void vfs_unload(void) {
	struct vfs_fsal_module *vfs_me;
	int retval;

	retval = unregister_fsal(myself);
	if(retval == 0 && myself != NULL) {
		vfs_me = container_of(myself, struct vfs_fsal_module, fsal);
		/* free my resources */
		free(vfs_me);
		myself = NULL;
	}
}
