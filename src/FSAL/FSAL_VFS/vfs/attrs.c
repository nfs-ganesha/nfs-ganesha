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

#ifdef ENABLE_VFS_POSIX_ACL
#include "os/acl.h"
#include "../../posix_acls.h"
#endif /* ENABLE_VFS_POSIX_ACL */

void vfs_sub_getattrs_common(struct vfs_fsal_obj_handle *vfs_hdl,
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
				 "Could not get the fs locations for vfs handle: %p",
				 vfs_hdl);
		}
	}
}

void vfs_sub_getattrs_release(struct fsal_attrlist *attrib)
{
	if (attrib->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's release the
		 * old ACL properly.
		 */
		nfs4_acl_release_entry(attrib->acl);

		attrib->acl = NULL;
	}
}

#if defined(ENABLE_VFS_DEBUG_ACL)
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

fsal_status_t vfs_sub_getattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
	fsal_acl_status_t status;
	struct vfs_acl_entry *fa;
	fsal_acl_data_t acldata;
	fsal_acl_t *acl;

	vfs_sub_getattrs_common(vfs_hdl, fd, request_mask, attrib);

	LogDebug(COMPONENT_FSAL, "Enter");

	vfs_sub_getattrs_release(attrib);

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

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_sub_setattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
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

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

#elif defined(ENABLE_VFS_POSIX_ACL)
fsal_status_t vfs_sub_getattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd,
			       attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
	struct fsal_obj_handle *obj_pub = &vfs_hdl->obj_handle;
	bool is_dir = obj_pub->type == DIRECTORY;
	acl_t e_acl = NULL, i_acl = NULL;
	fsal_acl_data_t acldata;
	fsal_ace_t *pace = NULL;
	fsal_acl_status_t aclstatus;
	int e_count = 0, i_count = 0, new_count = 0, new_i_count = 0;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	vfs_sub_getattrs_common(vfs_hdl, fd, request_mask, attrib);

	vfs_sub_getattrs_release(attrib);

	e_acl = acl_get_fd(fd);
	if (e_acl == (acl_t)NULL) {
		status = fsalstat(posix2fsal_error(errno), errno);
		goto out;
	}

	/*
	 * Adapted from FSAL_CEPH/internal.c:ceph_get_acl() and
	 * FSAL_GLUSTER/gluster_internal.c:glusterfs_get_acl()
	 */

	e_count = ace_count(e_acl);

	if (is_dir) {
		i_acl = acl_get_fd_np(fd, ACL_TYPE_DEFAULT);
		if (i_acl == (acl_t)NULL) {
			status = fsalstat(posix2fsal_error(errno), errno);
			goto out;
		}
		i_count = ace_count(i_acl);
	}

	acldata.naces = 2 * (e_count + i_count);
	LogDebug(COMPONENT_FSAL,
			"No of aces present in fsal_acl_t = %d", acldata.naces);
	if (acldata.naces == 0) {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		goto out;
	}

	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);
	pace = acldata.aces;

	if (e_count > 0) {
		new_count = posix_acl_2_fsal_acl(e_acl, is_dir, false, &pace);
	} else {
		LogDebug(COMPONENT_FSAL,
			"effective acl is not set for this object");
	}

	if (i_count > 0) {
		new_i_count = posix_acl_2_fsal_acl(i_acl, true, true, &pace);
		new_count += new_i_count;
	} else {
		LogDebug(COMPONENT_FSAL,
			"Inherit acl is not set for this directory");
	}

	/* Reallocating acldata into the required size */
	acldata.aces = (fsal_ace_t *) gsh_realloc(acldata.aces,
					new_count*sizeof(fsal_ace_t));
	acldata.naces = new_count;

	attrib->acl = nfs4_acl_new_entry(&acldata, &aclstatus);
	if (attrib->acl == NULL) {
		LogCrit(COMPONENT_FSAL, "failed to create a new acl entry");
		status = fsalstat(posix2fsal_error(EFAULT), EFAULT);
		goto out;
	}

	status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	FSAL_SET_MASK(attrib->valid_mask, ATTR_ACL);

out:
	if (e_acl)
		acl_free((void *)e_acl);

	if (i_acl)
		acl_free((void *)i_acl);

	return status;
}

fsal_status_t vfs_sub_setattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd,
			       attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
	struct fsal_obj_handle *obj_pub = &vfs_hdl->obj_handle;
	bool is_dir = obj_pub->type == DIRECTORY;
	acl_t acl = NULL;
	int ret;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	if (!FSAL_TEST_MASK(request_mask, ATTR_ACL) || !attrib)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	/*
	 * Adapted from FSAL_CEPH/internal.c:ceph_set_acl() and
	 * FSAL_GLUSTER/gluster_internal.c:glusterfs_set_acl()
	 */

	/*
	 * This should not happen.  In this case FSAL_GLUSTER does not
	 * warn and just returns OK, as above.  However, FSAL_CEPH
	 * warns and returns an error.  Adopt a sane middle-ground:
	 * warn only.
	 */
	if (!attrib->acl) {
		LogWarn(COMPONENT_FSAL, "acl is empty");
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		goto out;
	}

	acl = fsal_acl_2_posix_acl(attrib->acl, ACL_TYPE_ACCESS);
	if (acl == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "failed to set access type posix acl");
		status = fsalstat(ERR_FSAL_FAULT, 0);
		goto out;
	}
	ret = acl_set_fd(fd, acl);
	if (ret != 0) {
		status = fsalstat(errno, 0);
		LogMajor(COMPONENT_FSAL, "failed to set access type posix acl");
		goto out;
	}
	acl_free(acl);
	acl = (acl_t)NULL;

	if (!is_dir)
		goto out;

	acl = fsal_acl_2_posix_acl(attrib->acl, ACL_TYPE_DEFAULT);
	if (acl == NULL) {
		LogDebug(COMPONENT_FSAL,
			 "inherited acl is not defined for directory");
		goto out;
	}
	ret = acl_set_fd_np(fd, acl, ACL_TYPE_DEFAULT);
	if (ret != 0) {
		status = fsalstat(errno, 0);
		LogMajor(COMPONENT_FSAL,
			 "failed to set default type posix acl");
		goto out;
	}

	status = fsalstat(ERR_FSAL_NO_ERROR, 0);

out:
	if (acl)
		acl_free((void *)acl);

	return status;
}

#else /* NOT(ENABLE_VFS_DEBUG_ACL OR ENABLE_VFS_POSIX_ACL) */
fsal_status_t vfs_sub_getattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
	vfs_sub_getattrs_common(vfs_hdl, fd, request_mask, attrib);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_sub_setattrs(struct vfs_fsal_obj_handle *vfs_hdl,
			       int fd, attrmask_t request_mask,
			       struct fsal_attrlist *attrib)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
#endif /* NOT(ENABLE_VFS_DEBUG_ACL OR ENABLE_VFS_POSIX_ACL) */
