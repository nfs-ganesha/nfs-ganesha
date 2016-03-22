/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2016 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/**
 * @addtogroup FSAL_MDCACHE
 * @{
 */

/**
 * @file  mdcache_helpers.c
 * @brief Miscellaneous helper functions
 */

#include "config.h"
#include "fsal.h"
#include "nfs4_acls.h"
#include "mdcache_int.h"

static fsal_status_t
mdc_up_invalidate(struct fsal_export *export, struct gsh_buffdesc *handle,
		  uint32_t flags)
{
	struct fsal_obj_handle *obj;
	mdcache_entry_t *entry;
	fsal_status_t status;
	uint32_t mdc_flags = 0;

	status = export->exp_ops.create_handle(export, handle, &obj);
	if (FSAL_IS_ERROR(status))
		return status;
	entry = container_of(obj, mdcache_entry_t, obj_handle);

	if (flags & FSAL_UP_INVALIDATE_ATTRS)
		mdc_flags |= MDCACHE_INVALIDATE_ATTRS;
	if (flags & FSAL_UP_INVALIDATE_CONTENT)
		mdc_flags |= MDCACHE_INVALIDATE_CONTENT;
	if (flags & FSAL_UP_INVALIDATE_CLOSE)
		mdc_flags |= MDCACHE_INVALIDATE_CLOSE;

	status = mdcache_invalidate(entry, mdc_flags);

	obj->obj_ops.put_ref(obj);
	return status;
}

/**
 * @brief Update cached attributes
 *
 * @param[in] obj    Key to specify object
 * @param[in] attr   New attributes
 * @param[in] flags  Flags to govern update
 *
 * @return FSAL status
 */

static fsal_status_t
mdc_up_update(struct fsal_export *export, struct gsh_buffdesc *handle,
	      struct attrlist *attr, uint32_t flags)
{
	struct fsal_obj_handle *obj;
	mdcache_entry_t *entry;
	fsal_status_t status;
	/* Have necessary changes been made? */
	bool mutatis_mutandis = false;

	/* These cannot be updated, changing any of them is
	   tantamount to destroying and recreating the file. */
	if (FSAL_TEST_MASK
	    (attr->mask,
	     ATTR_TYPE | ATTR_FSID | ATTR_FILEID | ATTR_RAWDEV | ATTR_RDATTR_ERR
	     | ATTR_GENERATION)) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Filter out garbage flags */

	if (flags &
	    ~(fsal_up_update_filesize_inc | fsal_up_update_atime_inc |
	      fsal_up_update_creation_inc | fsal_up_update_ctime_inc |
	      fsal_up_update_mtime_inc | fsal_up_update_chgtime_inc |
	      fsal_up_update_spaceused_inc | fsal_up_nlink)) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	status = export->exp_ops.create_handle(export, handle, &obj);
	if (FSAL_IS_ERROR(status))
		return status;
	entry = container_of(obj, mdcache_entry_t, obj_handle);

	/* Knock things out if the link count falls to 0. */

	if ((flags & fsal_up_nlink) && (attr->numlinks == 0)) {
		status = mdcache_invalidate(entry, (MDCACHE_INVALIDATE_ATTRS |
						    MDCACHE_INVALIDATE_CONTENT |
						    MDCACHE_INVALIDATE_CLOSE));

		if (FSAL_IS_ERROR(status))
			goto out;
	}

	if (attr->mask == 0) {
		/* Done */
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	if (attr->expire_time_attr != 0)
		obj->attrs->expire_time_attr = attr->expire_time_attr;

	if (FSAL_TEST_MASK(attr->mask, ATTR_SIZE)) {
		if (flags & fsal_up_update_filesize_inc) {
			if (attr->filesize > obj->attrs->filesize) {
				obj->attrs->filesize = attr->filesize;
				mutatis_mutandis = true;
			}
		} else {
			obj->attrs->filesize = attr->filesize;
			mutatis_mutandis = true;
		}
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_SPACEUSED)) {
		if (flags & fsal_up_update_spaceused_inc) {
			if (attr->spaceused > obj->attrs->spaceused) {
				obj->attrs->spaceused = attr->spaceused;
				mutatis_mutandis = true;
			}
		} else {
			obj->attrs->spaceused = attr->spaceused;
			mutatis_mutandis = true;
		}
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_ACL)) {
		/**
		 * @todo Someone who knows the ACL code, please look
		 * over this.  We assume that the FSAL takes a
		 * reference on the supplied ACL that we can then hold
		 * onto.  This seems the most reasonable approach in
		 * an asynchronous call.
		 */

		nfs4_acl_release_entry(obj->attrs->acl);

		obj->attrs->acl = attr->acl;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_MODE)) {
		obj->attrs->mode = attr->mode;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_NUMLINKS)) {
		obj->attrs->numlinks = attr->numlinks;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_OWNER)) {
		obj->attrs->owner = attr->owner;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_GROUP)) {
		obj->attrs->group = attr->group;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_ATIME)
	    && ((flags & ~fsal_up_update_atime_inc)
		||
		(gsh_time_cmp(&attr->atime, &obj->attrs->atime) == 1))) {
		obj->attrs->atime = attr->atime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CREATION)
	    && ((flags & ~fsal_up_update_creation_inc)
		||
		(gsh_time_cmp(&attr->creation, &obj->attrs->creation) == 1))) {
		obj->attrs->creation = attr->creation;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CTIME)
	    && ((flags & ~fsal_up_update_ctime_inc)
		||
		(gsh_time_cmp(&attr->ctime, &obj->attrs->ctime) == 1))) {
		obj->attrs->ctime = attr->ctime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_MTIME)
	    && ((flags & ~fsal_up_update_mtime_inc)
		||
		(gsh_time_cmp(&attr->mtime, &obj->attrs->mtime) == 1))) {
		obj->attrs->mtime = attr->mtime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CHGTIME)
	    && ((flags & ~fsal_up_update_chgtime_inc)
		||
		(gsh_time_cmp(&attr->chgtime, &obj->attrs->chgtime) == 1))) {
		obj->attrs->chgtime = attr->chgtime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CHANGE)) {
		obj->attrs->change = attr->change;
		mutatis_mutandis = true;
	}

	if (mutatis_mutandis) {
		mdc_fixup_md(entry);
		/* If directory can not trust content anymore. */
		if (obj->type == DIRECTORY)
			mdcache_invalidate(entry,
					   (MDCACHE_INVALIDATE_CONTENT |
					    MDCACHE_INVALIDATE_GOT_LOCK));
	} else {
		mdcache_invalidate(entry, (MDCACHE_INVALIDATE_ATTRS |
					   MDCACHE_INVALIDATE_GOT_LOCK));
		status = fsalstat(ERR_FSAL_INVAL, 0);
	}
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

 out:
	obj->obj_ops.put_ref(obj);
	return status;
}

/**
 * @brief Invalidate a cached entry
 *
 * @param[in] key    Key to specify object
 * @param[in] flags  FSAL_UP_INVALIDATE*
 *
 * @return FSAL status
 */

static fsal_status_t
mdc_up_invalidate_close(struct fsal_export *export,
			struct gsh_buffdesc *handle, uint32_t flags)
{
	struct fsal_obj_handle *obj;
	fsal_status_t status;

	status = export->exp_ops.create_handle(export, handle, &obj);
	if (FSAL_IS_ERROR(status))
		return status;

	if (fsal_is_open(obj))
		status = up_async_invalidate(general_fridge, export, handle,
					     flags | FSAL_UP_INVALIDATE_CLOSE,
					     NULL, NULL);
	else
		status = mdc_up_invalidate(export, handle, flags);

	obj->obj_ops.put_ref(obj);
	return status;
}

fsal_status_t
mdcache_export_up_ops_init(struct fsal_up_vector *my_up_ops,
			   const struct fsal_up_vector *super_up_ops)
{
	/* Init with super ops. Struct copy */
	*my_up_ops = *super_up_ops;

	/* Replace cache-related calls */
	my_up_ops->invalidate = mdc_up_invalidate;
	my_up_ops->update = mdc_up_update;
	my_up_ops->invalidate_close = mdc_up_invalidate_close;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** @} */
