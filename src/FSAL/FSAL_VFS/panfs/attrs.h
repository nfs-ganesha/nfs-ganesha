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

/* attrs.h
 * PanFS Attribute handling API
 */

#ifndef __PANFS_ATTRS_H
#define __PANFS_ATTRS_H

#include <stdint.h>
#include <stdbool.h>
#include "panfs_int.h"

typedef enum {
	PAN_ACL_PERM_LIST_DIR		= 0x00000001,
	PAN_ACL_PERM_READ		= 0x00000001,
	PAN_ACL_PERM_WRITE		= 0x00000004,
	PAN_ACL_PERM_CREATE		= 0x00000010,
	PAN_ACL_PERM_APPEND		= 0x00000008,
	PAN_ACL_PERM_CREATE_DIR		= 0x00001000,
	PAN_ACL_PERM_READ_NAMED_ATTRS	= 0x00002000,
	PAN_ACL_PERM_WRITE_NAMED_ATTRS	= 0x00004000,
	PAN_ACL_PERM_EXECUTE		= 0x00000002,
	PAN_ACL_PERM_DELETE_CHILD	= 0x00008000,
	PAN_ACL_PERM_READ_ATTRS		= 0x00010000,
	PAN_ACL_PERM_WRITE_ATTRS	= 0x00020000,
	PAN_ACL_PERM_DELETE		= 0x00000020,
	PAN_ACL_PERM_READ_ACL		= 0x00040000,
	PAN_ACL_PERM_CHANGE_ACL		= 0x00000080,
	PAN_ACL_PERM_TAKE_OWNER		= 0x00000100,
	PAN_ACL_PERM_SYNCHRONIZE	= 0x00080000,
	PAN_ACL_PERM_ALL		= 0x003ff3ff
} pan_acl_permission_t;

typedef enum {
	/* Type flags (PAN_FS_ACE_TYPE_MASK) */
	PAN_FS_ACE_INVALID			= 0x0000,
	PAN_FS_ACE_POS				= 0x0001,
	PAN_FS_ACE_NEG				= 0x0002,
	PAN_FS_ACE_AUDIT			= 0x0003,
	PAN_FS_ACE_ALARM			= 0x0004,
	PAN_FS_ACE_NUM_TYPES			= 0x0005,
	PAN_FS_ACE_INHERIT_FLAG_NONE		= 0x0000,

	/* Interitance flags (PAN_FS_ACE_INHERIT_TYPE_MASK) */
	PAN_FS_ACE_OBJECT_INHERIT		= 0x0100,
	PAN_FS_ACE_CONTAINER_INHERIT		= 0x0200,
	PAN_FS_ACE_NO_PROPAGATE_INHERIT		= 0x0400,
	PAN_FS_ACE_INHERIT_ONLY			= 0x0800,
	PAN_FS_ACE_SUCCESSFUL_ACC_ACE_FLAG	= 0x1000,
	PAN_FS_ACE_FAILED_ACC_ACE_FLAG		= 0x2000,
	PAN_FS_ACE_IDENTIFIER_GROUP		= 0x4000,
	PAN_FS_ACE_INHERITED_ACE		= 0x8000,

	PAN_FS_ACE_TYPE_MASK			= 0x00ff,
	PAN_FS_ACE_INHERIT_TYPE_MASK		= 0xff00
} pan_fs_ace_info_t;

struct pan_fs_acl_s {
	uint32_t		naces;
	struct pan_fs_ace_s	*aces;
};

struct pan_attrs {
	struct pan_fs_acl_s acls;
};

struct attrlist;
struct panfs_fsal_obj_handle;

fsal_status_t PanFS_getattrs(struct panfs_fsal_obj_handle *panfs_hdl,
		int fd,
		struct attrlist *attrib);
fsal_status_t PanFS_setattrs(struct panfs_fsal_obj_handle *panfs_hdl,
		int fd,
		struct attrlist *attrib);

#endif /* __PANFS_ATTRS_H */
