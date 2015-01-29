/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohortFS LLC, 2014
 * Author: Daniel Gryniewicz dang@cohortfs.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* export_vfs.c
 * VFS Sub-FSAL export object
 */

#include "config.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "../vfs_methods.h"
#include "../subfsal.h"

/* Export */

static struct config_item_list fsid_types[] = {
	CONFIG_LIST_TOK("None", FSID_NO_TYPE),
	CONFIG_LIST_TOK("One64", FSID_ONE_UINT64),
	CONFIG_LIST_TOK("Major64", FSID_MAJOR_64),
	CONFIG_LIST_TOK("Two64", FSID_TWO_UINT64),
	CONFIG_LIST_TOK("uuid", FSID_TWO_UINT64),
	CONFIG_LIST_TOK("Two32", FSID_TWO_UINT32),
	CONFIG_LIST_TOK("Dev", FSID_DEVICE),
	CONFIG_LIST_TOK("Device", FSID_DEVICE),
	CONFIG_LIST_EOL
};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_ENUM("fsid_type", -1,
		       fsid_types,
		       vfs_fsal_export, fsid_type),
	CONFIG_EOL
};

static struct config_block export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.vfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

struct config_block *vfs_sub_export_param = &export_param_block;

/* Handle syscalls */

void vfs_sub_fini(struct vfs_fsal_export *myself)
{
}

void vfs_sub_init_handle_ops(struct vfs_fsal_export *myself,
			      struct fsal_obj_ops *ops)
{
}

void vfs_sub_init_export_ops(struct vfs_fsal_export *myself,
			      const char *export_path)
{
}

int vfs_sub_init_export(struct vfs_fsal_export *myself)
{
	return 0;
}
