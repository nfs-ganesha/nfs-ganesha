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

/* attrs.c
 * PanFS Attribute handling
 */

#include "fsal_types.h"
#include "fsal_convert.h"
#include "FSAL/access_check.h"
#include "nfs4_acls.h"
#include "../vfs_methods.h"
#include "attrs.h"
#include "panfs_um_pnfs.h"

pan_fs_ace_info_t fsal_to_panace_info(fsal_acetype_t type, fsal_aceflag_t flag)
{
	pan_fs_ace_info_t info;

	switch (type) {
	case FSAL_ACE_TYPE_ALLOW:
		info = PAN_FS_ACE_POS;
		break;
	case FSAL_ACE_TYPE_DENY:
		info = PAN_FS_ACE_NEG;
		break;
	case FSAL_ACE_TYPE_AUDIT:
		info = PAN_FS_ACE_AUDIT;
		break;
	case FSAL_ACE_TYPE_ALARM:
		info = PAN_FS_ACE_ALARM;
		break;
	default:
		return (uint32_t)-1;
	}

	if (flag & FSAL_ACE_FLAG_FILE_INHERIT)
		info |= PAN_FS_ACE_OBJECT_INHERIT;
	if (flag & FSAL_ACE_FLAG_DIR_INHERIT)
		info |= PAN_FS_ACE_CONTAINER_INHERIT;
	if (flag & FSAL_ACE_FLAG_NO_PROPAGATE)
		info |= PAN_FS_ACE_NO_PROPAGATE_INHERIT;
	if (flag & FSAL_ACE_FLAG_INHERIT_ONLY)
		info |= PAN_FS_ACE_INHERIT_ONLY;
	if (flag & FSAL_ACE_FLAG_SUCCESSFUL)
		info |= PAN_FS_ACE_SUCCESSFUL_ACC_ACE_FLAG;
	if (flag & FSAL_ACE_FLAG_FAILED)
		info |= PAN_FS_ACE_FAILED_ACC_ACE_FLAG;
	if (flag & FSAL_ACE_FLAG_INHERITED)
		info |= PAN_FS_ACE_INHERITED_ACE;

	return info;
}

pan_acl_permission_t fsal_perm_to_panace_perm(fsal_aceperm_t perm)
{
	pan_acl_permission_t ret = 0;

	if (perm & FSAL_ACE_PERM_LIST_DIR)
		ret |= PAN_ACL_PERM_LIST_DIR;
	if (perm & FSAL_ACE_PERM_READ_DATA)
		ret |= PAN_ACL_PERM_READ;
	if (perm & FSAL_ACE_PERM_WRITE_DATA)
		ret |= PAN_ACL_PERM_WRITE;
	if (perm & FSAL_ACE_PERM_ADD_FILE)
		ret |= PAN_ACL_PERM_CREATE;
	if (perm & FSAL_ACE_PERM_APPEND_DATA)
		ret |= PAN_ACL_PERM_APPEND;
	if (perm & FSAL_ACE_PERM_ADD_SUBDIRECTORY)
		ret |= PAN_ACL_PERM_CREATE_DIR;
	if (perm & FSAL_ACE_PERM_READ_NAMED_ATTR)
		ret |= PAN_ACL_PERM_READ_NAMED_ATTRS;
	if (perm & FSAL_ACE_PERM_WRITE_NAMED_ATTR)
		ret |= PAN_ACL_PERM_WRITE_NAMED_ATTRS;
	if (perm & FSAL_ACE_PERM_EXECUTE)
		ret |= PAN_ACL_PERM_EXECUTE;
	if (perm & FSAL_ACE_PERM_DELETE_CHILD)
		ret |= PAN_ACL_PERM_DELETE_CHILD;
	if (perm & FSAL_ACE_PERM_READ_ATTR)
		ret |= PAN_ACL_PERM_READ_ATTRS;
	if (perm & FSAL_ACE_PERM_WRITE_ATTR)
		ret |= PAN_ACL_PERM_WRITE_ATTRS;
	if (perm & FSAL_ACE_PERM_DELETE)
		ret |= PAN_ACL_PERM_DELETE;
	if (perm & FSAL_ACE_PERM_READ_ACL)
		ret |= PAN_ACL_PERM_READ_ACL;
	if (perm & FSAL_ACE_PERM_WRITE_ACL)
		ret |= PAN_ACL_PERM_CHANGE_ACL;
	if (perm & FSAL_ACE_PERM_WRITE_OWNER)
		ret |= PAN_ACL_PERM_TAKE_OWNER;
	if (perm & FSAL_ACE_PERM_SYNCHRONIZE)
		ret |= PAN_ACL_PERM_SYNCHRONIZE;

	return ret;
}

/* XXX dang - This will lose PANFS specific types (PAN_IDENTITY_PAN_* and
 * PAN_IDENTITY_WIN_*), since those were consolidated into the FSAL types */
void fsal_id_to_panace_id(fsal_ace_t *ace, pan_identity_t *pan_id)
{
	if (IS_FSAL_ACE_SPECIAL_ID(*ace)) {
		if (IS_FSAL_ACE_SPECIAL_EVERYONE(*ace)) {
			pan_id->type = PAN_IDENTITY_PAN_GROUP;
			pan_id->u.pan_gid = PAN_IDENTITY_EVERYONE_GROUP_ID;
		} else {
			pan_id->type = PAN_IDENTITY_UNKNOWN;
			pan_id->u.uid = ace->who.uid;
		}
	} else {
		if (IS_FSAL_ACE_GROUP_ID(*ace)) {
			pan_id->type = PAN_IDENTITY_UNIX_GROUP;
			pan_id->u.gid = ace->who.gid;
		} else {
			pan_id->type = PAN_IDENTITY_UNIX_USER;
			pan_id->u.uid = ace->who.uid;
		}
	}
}

fsal_acetype_t panace_info_to_fsal_type(pan_fs_ace_info_t aceinfo)
{
	uint32_t type = aceinfo & PAN_FS_ACE_TYPE_MASK;

	switch (type) {
	case PAN_FS_ACE_POS:
	case 8:
		return FSAL_ACE_TYPE_ALLOW;
	case PAN_FS_ACE_NEG:
		return FSAL_ACE_TYPE_DENY;
	case PAN_FS_ACE_AUDIT:
		return FSAL_ACE_TYPE_AUDIT;
	case PAN_FS_ACE_ALARM:
		return FSAL_ACE_TYPE_ALARM;
	default:
		return (uint32_t)-1;
	}
}

fsal_aceflag_t panace_info_to_fsal_flag(pan_fs_ace_info_t aceinfo)
{
	uint32_t flag = aceinfo & PAN_FS_ACE_INHERIT_TYPE_MASK;
	fsal_aceflag_t ret = 0;

	if (flag & PAN_FS_ACE_OBJECT_INHERIT)
		ret |= FSAL_ACE_FLAG_FILE_INHERIT;
	if (flag & PAN_FS_ACE_CONTAINER_INHERIT)
		ret |= FSAL_ACE_FLAG_DIR_INHERIT;
	if (flag & PAN_FS_ACE_NO_PROPAGATE_INHERIT)
		ret |= FSAL_ACE_FLAG_NO_PROPAGATE;
	if (flag & PAN_FS_ACE_INHERIT_ONLY)
		ret |= FSAL_ACE_FLAG_INHERIT_ONLY;
	if (flag & PAN_FS_ACE_SUCCESSFUL_ACC_ACE_FLAG)
		ret |= FSAL_ACE_FLAG_SUCCESSFUL;
	if (flag & PAN_FS_ACE_FAILED_ACC_ACE_FLAG)
		ret |= FSAL_ACE_FLAG_FAILED;
	if (flag & PAN_FS_ACE_INHERITED_ACE)
		ret |= FSAL_ACE_FLAG_INHERITED;

	return ret;
}

void panace_id_to_fsal_id(pan_identity_t *pan_id, fsal_ace_t *ace)
{
	switch (pan_id->type) {
	case PAN_IDENTITY_PAN_USER:
	case PAN_IDENTITY_UNIX_USER:
	case PAN_IDENTITY_WIN_USER:
		/* Consolidate users */
		ace->flag &= ~(FSAL_ACE_FLAG_GROUP_ID);
		ace->who.uid = pan_id->u.uid;
		break;
	case PAN_IDENTITY_PAN_GROUP:
		if (pan_id->u.pan_gid == PAN_IDENTITY_EVERYONE_GROUP_ID) {
			ace->iflag |= FSAL_ACE_IFLAG_SPECIAL_ID;
			/* Do not set IDENTIFIER_GROUP flag for EVERYONE@ */
			ace->who.gid = FSAL_ACE_SPECIAL_EVERYONE;
			break;
		}
		/* FALLTHROUGH */
	case PAN_IDENTITY_UNIX_GROUP:
	case PAN_IDENTITY_WIN_GROUP:
		/* Consolidate groups */
		ace->flag |= FSAL_ACE_FLAG_GROUP_ID;
		ace->who.gid = pan_id->u.gid;
		break;
	case PAN_IDENTITY_UNKNOWN:
		ace->iflag |= FSAL_ACE_IFLAG_SPECIAL_ID;
		/* FALLTHROUGH */
	default:
		/* XXX Store unknown type? Won't work for some types, they won't
		 * fit */
		ace->who.uid = pan_id->u.unknown;
		break;
	}
}

fsal_aceperm_t panace_perm_to_fsal_perm(pan_acl_permission_t perms)
{
	uint32_t flag = perms & PAN_ACL_PERM_ALL;
	fsal_aceperm_t ret = 0;

	if (flag & PAN_ACL_PERM_LIST_DIR)
		ret |= FSAL_ACE_PERM_LIST_DIR;
	if (flag & PAN_ACL_PERM_READ)
		ret |= FSAL_ACE_PERM_READ_DATA;
	if (flag & PAN_ACL_PERM_WRITE)
		ret |= FSAL_ACE_PERM_WRITE_DATA;
	if (flag & PAN_ACL_PERM_CREATE)
		ret |= FSAL_ACE_PERM_ADD_FILE;
	if (flag & PAN_ACL_PERM_APPEND)
		ret |= FSAL_ACE_PERM_APPEND_DATA;
	if (flag & PAN_ACL_PERM_CREATE_DIR)
		ret |= FSAL_ACE_PERM_ADD_SUBDIRECTORY;
	if (flag & PAN_ACL_PERM_READ_NAMED_ATTRS)
		ret |= FSAL_ACE_PERM_READ_NAMED_ATTR;
	if (flag & PAN_ACL_PERM_WRITE_NAMED_ATTRS)
		ret |= FSAL_ACE_PERM_WRITE_NAMED_ATTR;
	if (flag & PAN_ACL_PERM_EXECUTE)
		ret |= FSAL_ACE_PERM_EXECUTE;
	if (flag & PAN_ACL_PERM_DELETE_CHILD)
		ret |= FSAL_ACE_PERM_DELETE_CHILD;
	if (flag & PAN_ACL_PERM_READ_ATTRS)
		ret |= FSAL_ACE_PERM_READ_ATTR;
	if (flag & PAN_ACL_PERM_WRITE_ATTRS)
		ret |= FSAL_ACE_PERM_WRITE_ATTR;
	if (flag & PAN_ACL_PERM_DELETE)
		ret |= FSAL_ACE_PERM_DELETE;
	if (flag & PAN_ACL_PERM_READ_ACL)
		ret |= FSAL_ACE_PERM_READ_ACL;
	if (flag & PAN_ACL_PERM_CHANGE_ACL)
		ret |= FSAL_ACE_PERM_WRITE_ACL;
	if (flag & PAN_ACL_PERM_TAKE_OWNER)
		ret |= FSAL_ACE_PERM_WRITE_OWNER;
	if (flag & PAN_ACL_PERM_SYNCHRONIZE)
		ret |= FSAL_ACE_PERM_SYNCHRONIZE;

	return ret;
}

/**
 * @brief Convert FSAL ACLs to PanFS ACLs
 *
 * @param[in] attrib	Source FSAL attribute list
 * @param[in] panacl	Target PanFS ACL set
 * @return FSAL Status
 */
static fsal_status_t fsal_acl_2_panfs_acl(struct attrlist *attrib,
		struct pan_fs_acl_s *panacl)
{
	fsal_errors_t ret = ERR_FSAL_NO_ERROR;
	struct pan_fs_ace_s *panace;
	fsal_ace_t *ace = NULL;
	fsal_acl_t *acl = NULL;

	/* sanity checks */
	if (!attrib || !attrib->acl || !panacl)
		return fsalstat(ERR_FSAL_FAULT, EFAULT);

	if (attrib->acl->naces > panacl->naces) {
		/* Too many ACEs for storing */
		return fsalstat(ERR_FSAL_INVAL, EFAULT);
	}

	/* Create Panfs acl data. */
	panacl->naces = attrib->acl->naces;
	LogDebug(COMPONENT_FSAL, "Converting %u aces:", panacl->naces);

	for (ace = attrib->acl->aces, panace = panacl->aces;
	     ace < attrib->acl->aces + attrib->acl->naces; ace++, panace++) {
		fsal_print_ace(COMPONENT_FSAL, NIV_DEBUG, ace);
		panace->info = fsal_to_panace_info(ace->type, ace->flag);
		if (panace->info == (uint16_t)-1) {
			ret = ERR_FSAL_INVAL;
			goto fail;
		}
		panace->permissions = fsal_perm_to_panace_perm(ace->perm);
		fsal_id_to_panace_id(ace, &panace->identity);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
fail:
	(void)nfs4_acl_release_entry(acl);
	return fsalstat(ret, 0);
}

/**
 * @brief Convert PanFS ACLs to FSAL ACLs
 *
 * @param[in] panacl	Source PanFS ACL set
 * @param[in,out] attrib	Target FSAL attribute list
 * @return FSAL Status
 */
static fsal_status_t panfs_acl_2_fsal_acl(struct pan_fs_acl_s *panacl,
		struct attrlist *attrib)
{
	fsal_acl_status_t status;
	fsal_acl_data_t acldata;
	struct pan_fs_ace_s *panace;
	fsal_ace_t *ace = NULL;
	fsal_acl_t *acl = NULL;

	/* sanity checks */
	if (!attrib || !panacl)
		return fsalstat(ERR_FSAL_FAULT, EFAULT);

	/* Count the number of aces */
	acldata.naces = 0;
	for (panace = panacl->aces; panace < panacl->aces + panacl->naces;
	     panace++) {
		if (panace_info_to_fsal_type(panace->info) == (uint32_t)-1)
			continue;
		acldata.naces++;
	}

	if (acldata.naces == 0) {
		/* No ACEs on this object */
		FSAL_UNSET_MASK(attrib->mask, ATTR_ACL);
		attrib->acl = NULL;
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* Create fsal acl data. */
	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);

	LogDebug(COMPONENT_FSAL, "Converting %u aces:", acldata.naces);
	ace = acldata.aces;
	for (panace = panacl->aces; panace < panacl->aces + panacl->naces;
	     panace++) {
		ace->type = panace_info_to_fsal_type(panace->info);
		if (ace->type == (uint32_t)-1)
			continue;
		ace->flag = panace_info_to_fsal_flag(panace->info);
		ace->perm = panace_perm_to_fsal_perm(panace->permissions);
		panace_id_to_fsal_id(&panace->identity, ace);
		fsal_print_ace(COMPONENT_FSAL, NIV_DEBUG, ace);
		ace++;
	}

	/* Create a new hash table entry for fsal acl. */
	acl = nfs4_acl_new_entry(&acldata, &status);
	if (!acl)
		return fsalstat(ERR_FSAL_FAULT, status);

	/* Add fsal acl to attribute. */
	attrib->acl = acl;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Get file attributes from PanFS
 *
 * Get the attributes and convert them into VFS FSAL attributes
 *
 * @param[in] panfs_hdl	PanFS Object handle to operate on
 * @param[in] fd	Open FD to operate on
 * @param[in,out] attrib	Attributes to get / storage for attributes
 * @return ERR_FSAL_NO_ERROR on success, other error code on failure
 */
fsal_status_t PanFS_getattrs(struct panfs_fsal_obj_handle *panfs_hdl,
		int fd,
		struct attrlist *attrib)
{
	fsal_status_t st;
	struct pan_attrs pattrs;
	struct pan_fs_ace_s paces[PAN_FS_ACL_LEN_MAX];

	pattrs.acls.naces = PAN_FS_ACL_LEN_MAX;
	pattrs.acls.aces = paces;

	st = panfs_um_get_attr(fd, &pattrs);
	if (FSAL_IS_ERROR(st)) {
		FSAL_UNSET_MASK(attrib->mask, ATTR_ACL);
		attrib->acl = NULL;
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
		/*return st;*/
	}

	return panfs_acl_2_fsal_acl(&pattrs.acls, attrib);
}

/**
 * @brief Set file attributes into PanFS
 *
 * Convert attributes into PanFS format and set them
 *
 * @param[in] panfs_hdl	PanFS Object handle to operate on
 * @param[in] fd	Open FD to operate on
 * @param[in,out] attrib	Attributes to get / storage for attributes
 * @return ERR_FSAL_NO_ERROR on success, other error code on failure
 */
fsal_status_t PanFS_setattrs(struct panfs_fsal_obj_handle *panfs_hdl,
		int fd,
		struct attrlist *attrib)
{
	fsal_status_t st;
	struct pan_attrs pattrs;
	struct pan_fs_ace_s paces[PAN_FS_ACL_LEN_MAX];

	pattrs.acls.naces = PAN_FS_ACL_LEN_MAX;
	pattrs.acls.aces = paces;

	st = fsal_acl_2_panfs_acl(attrib, &pattrs.acls);
	if (FSAL_IS_ERROR(st))
		return st;

	st = panfs_um_set_attr(fd, &pattrs);
	if (FSAL_IS_ERROR(st))
		return st;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
