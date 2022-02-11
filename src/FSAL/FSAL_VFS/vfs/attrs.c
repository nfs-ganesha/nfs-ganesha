// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) CohortFS LLC, 2015
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
 * VFS attribute caching handle object
 */

#include "config.h"

#include "avltree.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/access_check.h"
#include "../vfs_methods.h"
#include "attrs.h"
#include "nfs4_acls.h"

#ifdef ENABLE_VFS_DEBUG_ACL
struct vfs_acl_entry {
	struct gsh_buffdesc	fa_key;		/**< Key for tree */
	struct avltree_node	fa_node;	/**< AVL tree node */
	fsal_acl_data_t		fa_acl;		/**< Actual ACLs */
};

static struct avltree vfs_acl_tree = {0};

/**
 * @brief VFS acl comparator for AVL tree walk
 *
 */
static int vfs_acl_cmpf(const struct avltree_node *lhs,
			const struct avltree_node *rhs)
{
	struct vfs_acl_entry *lk, *rk;

	lk = avltree_container_of(lhs, struct vfs_acl_entry, fa_node);
	rk = avltree_container_of(rhs, struct vfs_acl_entry, fa_node);
	if (lk->fa_key.len != rk->fa_key.len)
		return (lk->fa_key.len < rk->fa_key.len) ? -1 : 1;

	return memcmp(lk->fa_key.addr, rk->fa_key.addr, lk->fa_key.len);
}

static struct vfs_acl_entry *vfs_acl_lookup(struct gsh_buffdesc *key)
{
	struct vfs_acl_entry key_entry;
	struct avltree_node *node;

	memset(&key_entry, 0, sizeof(key_entry));
	key_entry.fa_key = *key;
	node = avltree_lookup(&key_entry.fa_node, &vfs_acl_tree);
	if (!node)
		return NULL;

	return avltree_container_of(node, struct vfs_acl_entry, fa_node);
}

static struct vfs_acl_entry *vfs_acl_locate(struct fsal_obj_handle *obj)
{
	struct vfs_acl_entry *fa_entry;
	struct avltree_node *node;
	struct gsh_buffdesc key;

	obj->obj_ops->handle_to_key(obj, &key);

	fa_entry = vfs_acl_lookup(&key);
	if (fa_entry) {
		LogDebug(COMPONENT_FSAL, "found");
		return fa_entry;
	}

	LogDebug(COMPONENT_FSAL, "create");
	fa_entry = gsh_calloc(1, sizeof(struct vfs_acl_entry));

	fa_entry->fa_key = key;
	node = avltree_insert(&fa_entry->fa_node, &vfs_acl_tree);
	if (unlikely(node)) {
		/* Race won */
		gsh_free(fa_entry);
		fa_entry = avltree_container_of(node, struct vfs_acl_entry,
						fa_node);
	} else {
		fa_entry->fa_acl.aces = (fsal_ace_t *) nfs4_ace_alloc(0);
	}

	return fa_entry;
}

void vfs_acl_init(void)
{
	if (vfs_acl_tree.cmp_fn == NULL)
		avltree_init(&vfs_acl_tree, vfs_acl_cmpf, 0);
}

void vfs_acl_release(struct gsh_buffdesc *key)
{
	struct vfs_acl_entry *fa_entry;

	fa_entry = vfs_acl_lookup(key);
	if (!fa_entry)
		return;

	avltree_remove(&fa_entry->fa_node, &vfs_acl_tree);
	gsh_free(fa_entry);
}
#endif /* ENABLE_VFS_DEBUG_ACL */

fsal_status_t vfs_sub_getattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
	fsal_status_t fsal_st = {ERR_FSAL_NO_ERROR, 0};

	if (FSAL_TEST_MASK(request_mask, ATTR4_FS_LOCATIONS) &&
	    vfs_hdl->obj_handle.obj_ops->is_referral(&vfs_hdl->obj_handle,
		attrib, false /*cache_attrs*/)) {

		fsal_st = vfs_get_fs_locations(vfs_hdl, fd, attrib);
		if (FSAL_IS_ERROR(fsal_st)) {
			/* No error should be returned here, any major error
			 * should have been caught before this */
			LogDebug(COMPONENT_FSAL,
				 "Could not get the fs locations for vfs "
				 "handle: %p", vfs_hdl);
		}
	}

#ifdef ENABLE_VFS_DEBUG_ACL
	fsal_acl_status_t status;
	struct vfs_acl_entry *fa;
	fsal_acl_data_t acldata;
	fsal_acl_t *acl;

	LogDebug(COMPONENT_FSAL, "Enter");

	if (attrib->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's not release the
		 * old ACL properly.
		 */
		int acl_status;

		acl_status = nfs4_acl_release_entry(attrib->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);

		attrib->acl = NULL;
	}

	fa = vfs_acl_locate(&vfs_hdl->obj_handle);
	if (!fa->fa_acl.naces) {
		/* No ACLs yet */
		FSAL_UNSET_MASK(attrib->valid_mask, ATTR_ACL);

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG,
		       (fsal_acl_t *)&fa->fa_acl);
	acldata.naces = fa->fa_acl.naces;
	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);
	memcpy(acldata.aces, fa->fa_acl.aces,
	       acldata.naces * sizeof(fsal_ace_t));

	acl = nfs4_acl_new_entry(&acldata, &status);
	if (!acl)
		return fsalstat(ERR_FSAL_FAULT, status);
	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG, acl);
	attrib->acl = acl;
	FSAL_SET_MASK(attrib->valid_mask, ATTR_ACL);
#endif /* ENABLE_VFS_DEBUG_ACL */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_sub_setattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
#ifdef ENABLE_VFS_DEBUG_ACL
	struct vfs_acl_entry *fa;

	if (!FSAL_TEST_MASK(request_mask, ATTR_ACL) || !attrib || !attrib->acl)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	LogDebug(COMPONENT_FSAL, "Enter");
	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG, attrib->acl);
	fa = vfs_acl_locate(&vfs_hdl->obj_handle);
	nfs4_ace_free(fa->fa_acl.aces);
	fa->fa_acl.naces = attrib->acl->naces;
	fa->fa_acl.aces = (fsal_ace_t *) nfs4_ace_alloc(fa->fa_acl.naces);
	memcpy(fa->fa_acl.aces, attrib->acl->aces,
	       fa->fa_acl.naces * sizeof(fsal_ace_t));
	fsal_print_acl(COMPONENT_FSAL, NIV_FULL_DEBUG,
		       (fsal_acl_t *)&fa->fa_acl);
	if (attrib->valid_mask & ATTR_MODE)
		vfs_hdl->mode = attrib->mode;

	FSAL_SET_MASK(attrib->valid_mask, ATTR_ACL);
#endif /* ENABLE_VFS_DEBUG_ACL */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
