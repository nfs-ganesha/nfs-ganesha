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

/* ZFS FSAL module private storage
 */

struct zfs_fsal_module {	
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	fsal_init_info_t fsal_info;
	 /* vfsfs_specific_initinfo_t specific_info;  placeholder */
};

const char myname[] = "ZFS";
/* filesystem info for your filesystem */
static struct fsal_staticfsinfo_t default_zfs_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  1024,                         /* max links for an object of your filesystem */
  MAXNAMLEN,            /* max filename */
  MAXPATHLEN,            /* min filename */
  true,                         /* no_trunc */
  true,                         /* chown restricted */
  false,                        /* case insensitivity */
  true,                         /* case preserving */
  FSAL_EXPTYPE_PERSISTENT,      /* FH expire type */
  true,                         /* hard link support */
  true,                         /* sym link support */
  false,                        /* lock support */
  false,                        /* lock owners */
  false,                        /* async blocking locks */
  true,                         /* named attributes */
  true,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  true,                         /* can change times */
  true,                         /* homogenous */
  ZFS_SUPPORTED_ATTRIBUTES,   /* supported attributes */
  0,                            /* maxread size */
  0,                            /* maxwrite size */
  0,                            /* default umask */
  0,                            /* don't allow cross fileset export path */
  0400,                         /* default access rights for xattrs: root=RW, owner=R */
  0,                            /* default access check support in FSAL */
  0,                            /* default share reservation support in FSAL */
  0                             /* default share reservation support with open owners in FSAL */
};


/* private helper for export object
 */

struct fsal_staticfsinfo_t *zfs_staticinfo(struct fsal_module *hdl)
{
	struct zfs_fsal_module *myself;

	myself = container_of(hdl, struct zfs_fsal_module, fsal);
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
	struct zfs_fsal_module * zfs_me
		= container_of(fsal_hdl, struct zfs_fsal_module, fsal);
	fsal_status_t fsal_status;

	zfs_me->fs_info = default_zfs_info; /* get a copy of the defaults */

        fsal_status = fsal_load_config(fsal_hdl->ops->get_name(fsal_hdl),
                                       config_struct,
                                       &zfs_me->fsal_info,
                                       &zfs_me->fs_info,
                                       NULL);

	if(FSAL_IS_ERROR(fsal_status))
		return fsal_status;
	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */

	display_fsinfo(&zfs_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%"PRIx64,
                     (uint64_t) ZFS_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%"PRIx64,
		     default_zfs_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%"PRIx64,
		 zfs_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal ZFS method linkage to export object
 */

fsal_status_t zfs_create_export(struct fsal_module *fsal_hdl,
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

static struct zfs_fsal_module ZFS;

/* linkage to the exports and handle ops initializers
 */

void zfs_export_ops_init(struct export_ops *ops);
void zfs_handle_ops_init(struct fsal_obj_ops *ops);

MODULE_INIT void zfs_load(void) {
	int retval;
	struct fsal_module *myself = &ZFS.fsal;

	retval = register_fsal(myself, myname,
			       FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION);
	if(retval != 0) {
		fprintf(stderr, "ZFS module failed to register");
		return;
	}

	myself->ops->create_export = zfs_create_export;
	myself->ops->init_config = init_config;
        init_fsal_parameters(&ZFS.fsal_info);
}

MODULE_FINI void zfs_unload(void) {
	int retval;

	retval = unregister_fsal(&ZFS.fsal);
	if(retval != 0) {
		fprintf(stderr, "ZFS module failed to unregister");
		return;
	}
}
