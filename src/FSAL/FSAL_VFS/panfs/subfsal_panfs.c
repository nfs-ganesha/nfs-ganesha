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

/* export_panfs.c
 * PanFS Sub-FSAL export object
 */

#include "config.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "panfs.h"
#include "../subfsal.h"
#include "mds.h"

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

struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_BOOL("pnfs", false,
		       panfs_fsal_export, pnfs_enabled),
	CONF_ITEM_TOKEN("fsid_type", FSID_NO_TYPE,
			fsid_types,
			panfs_fsal_export, vfs_export.fsid_type),
	CONFIG_EOL
};


static struct config_block export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.panfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

struct config_block *vfs_sub_export_param = &export_param_block;

/* Handle syscalls */

void vfs_sub_fini(struct vfs_fsal_export *vfs)
{
	struct panfs_fsal_export *myself = EXPORT_PANFS_FROM_VFS(vfs);

	pnfs_panfs_fini(myself->pnfs_data);
}

void vfs_sub_init_export_ops(struct vfs_fsal_export *vfs,
			      const char *export_path)
{
	struct panfs_fsal_export *myself = EXPORT_PANFS_FROM_VFS(vfs);

	if (myself->pnfs_enabled) {
		LogInfo(COMPONENT_FSAL,
			"pnfs_panfs was enabled for [%s]",
			export_path);
		export_ops_pnfs(&vfs->export.exp_ops);
		fsal_ops_pnfs(&vfs->export.fsal->m_ops);
	}
}

int vfs_sub_init_export(struct vfs_fsal_export *vfs)
{
	int retval = 0;
	struct panfs_fsal_export *myself = EXPORT_PANFS_FROM_VFS(vfs);

	if (myself->pnfs_enabled) {
		retval = pnfs_panfs_init(vfs_get_root_fd(&vfs->export),
							 &myself->pnfs_data);
		if (retval) {
			LogCrit(COMPONENT_FSAL,
				"vfs export_ops_pnfs failed => %d [%s]",
				retval, strerror(retval));
		}
	}

	return retval;
}

struct vfs_fsal_obj_handle *vfs_sub_alloc_handle(void)
{
	struct panfs_fsal_obj_handle *hdl;

	hdl = gsh_calloc(1,
			 (sizeof(struct panfs_fsal_obj_handle) +
			  sizeof(vfs_file_handle_t)));

	hdl->vfs_obj_handle.handle = (vfs_file_handle_t *) &hdl[1];
	return &hdl->vfs_obj_handle;
}

int vfs_sub_init_handle(struct vfs_fsal_export *vfs_export,
		struct vfs_fsal_obj_handle *vfs_hdl,
		const char *path)
{
	struct panfs_fsal_export *myself = EXPORT_PANFS_FROM_VFS(vfs_export);
	struct panfs_fsal_obj_handle *hdl = OBJ_PANFS_FROM_VFS(vfs_hdl);

	if (myself->pnfs_enabled)
		handle_ops_pnfs(&vfs_hdl->obj_handle.obj_ops);

	panfs_handle_ops_init(hdl);
	return 0;
}
