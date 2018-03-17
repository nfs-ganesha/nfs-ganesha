/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2017 Red Hat, Inc. and/or its affiliates.
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
#include "mdcache_hash.h"
#include "mdcache_int.h"
#include "nfs4_fs_locations.h"

static fsal_status_t
mdc_up_invalidate(const struct fsal_up_vector *vec, struct gsh_buffdesc *handle,
		  uint32_t flags)
{
	mdcache_entry_t *entry;
	fsal_status_t status;
	struct req_op_context *save_ctx, req_ctx = {0};
	mdcache_key_t key;

	req_ctx.ctx_export = vec->up_gsh_export;
	req_ctx.fsal_export = vec->up_fsal_export;
	save_ctx = op_ctx;
	op_ctx = &req_ctx;

	key.fsal = vec->up_fsal_export->sub_export->fsal;
	(void) cih_hash_key(&key, vec->up_fsal_export->sub_export->fsal, handle,
			    CIH_HASH_KEY_PROTOTYPE);

	status = mdcache_find_keyed(&key, &entry);
	if (status.major == ERR_FSAL_NOENT) {
		/* Not cached, so invalidate is a success */
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		goto out;
	} else if (FSAL_IS_ERROR(status)) {
		/* Real error */
		goto out;
	}

	atomic_clear_uint32_t_bits(&entry->mde_flags,
				   flags & FSAL_UP_INVALIDATE_CACHE);

	if (flags & FSAL_UP_INVALIDATE_CLOSE)
		status = fsal_close(&entry->obj_handle);

	mdcache_put(entry);

out:
	op_ctx = save_ctx;
	return status;
}

/**
 * @brief Update cached attributes
 *
 * @param[in] vec    Up ops vector
 * @param[in] handle Export containing object
 * @param[in] attr   New attributes
 * @param[in] flags  Flags to govern update
 *
 * @return FSAL status
 */

static fsal_status_t
mdc_up_update(const struct fsal_up_vector *vec, struct gsh_buffdesc *handle,
	      struct attrlist *attr, uint32_t flags)
{
	mdcache_entry_t *entry;
	fsal_status_t status;
	/* Have necessary changes been made? */
	bool mutatis_mutandis = false;
	struct req_op_context *save_ctx, req_ctx = {0};
	mdcache_key_t key;

	/* These cannot be updated, changing any of them is
	   tantamount to destroying and recreating the file. */
	if (FSAL_TEST_MASK
	    (attr->valid_mask,
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

	req_ctx.ctx_export = vec->up_gsh_export;
	req_ctx.fsal_export = vec->up_fsal_export;
	save_ctx = op_ctx;
	op_ctx = &req_ctx;

	key.fsal = vec->up_fsal_export->sub_export->fsal;
	(void) cih_hash_key(&key, vec->up_fsal_export->sub_export->fsal, handle,
			    CIH_HASH_KEY_PROTOTYPE);

	status = mdcache_find_keyed(&key, &entry);
	if (status.major == ERR_FSAL_NOENT) {
		/* Not cached, so invalidate is a success */
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		goto out;
	} else if (FSAL_IS_ERROR(status)) {
		/* Real error */
		goto out;
	}

	/* Knock things out if the link count falls to 0. */

	if ((flags & fsal_up_nlink) && (attr->numlinks == 0)) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Entry %p Clearing MDCACHE_TRUST_ATTRS, MDCACHE_TRUST_CONTENT, MDCACHE_DIR_POPULATED",
			     entry);
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS |
					   MDCACHE_TRUST_CONTENT |
					   MDCACHE_DIR_POPULATED);

		status = fsal_close(&entry->obj_handle);

		if (FSAL_IS_ERROR(status))
			goto put;
	}

	if (attr->valid_mask == 0) {
		/* Done */
		goto put;
	}

	/* If the attributes are invalid, we can't update a subset.  Just bail,
	 * and update them on demand */
	if (!mdcache_test_attrs_trust(entry, attr->valid_mask)) {
		goto put;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	if (attr->expire_time_attr != 0)
		entry->attrs.expire_time_attr = attr->expire_time_attr;

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_SIZE)) {
		if (flags & fsal_up_update_filesize_inc) {
			if (attr->filesize > entry->attrs.filesize) {
				entry->attrs.filesize = attr->filesize;
				mutatis_mutandis = true;
			}
		} else {
			entry->attrs.filesize = attr->filesize;
			mutatis_mutandis = true;
		}
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_SPACEUSED)) {
		if (flags & fsal_up_update_spaceused_inc) {
			if (attr->spaceused > entry->attrs.spaceused) {
				entry->attrs.spaceused = attr->spaceused;
				mutatis_mutandis = true;
			}
		} else {
			entry->attrs.spaceused = attr->spaceused;
			mutatis_mutandis = true;
		}
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_ACL)) {
		/**
		 * @todo Someone who knows the ACL code, please look
		 * over this.  We assume that the FSAL takes a
		 * reference on the supplied ACL that we can then hold
		 * onto.  This seems the most reasonable approach in
		 * an asynchronous call.
		 */

		nfs4_acl_release_entry(entry->attrs.acl);

		entry->attrs.acl = attr->acl;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_MODE)) {
		entry->attrs.mode = attr->mode;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_NUMLINKS)) {
		entry->attrs.numlinks = attr->numlinks;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_OWNER)) {
		entry->attrs.owner = attr->owner;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_GROUP)) {
		entry->attrs.group = attr->group;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME)
	    && ((flags & ~fsal_up_update_atime_inc)
		||
		(gsh_time_cmp(&attr->atime, &entry->attrs.atime) == 1))) {
		entry->attrs.atime = attr->atime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_CREATION)
	    && ((flags & ~fsal_up_update_creation_inc)
		||
		(gsh_time_cmp(&attr->creation, &entry->attrs.creation) == 1))) {
		entry->attrs.creation = attr->creation;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_CTIME)
	    && ((flags & ~fsal_up_update_ctime_inc)
		||
		(gsh_time_cmp(&attr->ctime, &entry->attrs.ctime) == 1))) {
		entry->attrs.ctime = attr->ctime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME)
	    && ((flags & ~fsal_up_update_mtime_inc)
		||
		(gsh_time_cmp(&attr->mtime, &entry->attrs.mtime) == 1))) {
		entry->attrs.mtime = attr->mtime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_CHGTIME)
	    && ((flags & ~fsal_up_update_chgtime_inc)
		||
		(gsh_time_cmp(&attr->chgtime, &entry->attrs.chgtime) == 1))) {
		entry->attrs.chgtime = attr->chgtime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_CHANGE)) {
		entry->attrs.change = attr->change;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR4_FS_LOCATIONS)) {
		nfs4_fs_locations_release(entry->attrs.fs_locations);

		entry->attrs.fs_locations = attr->fs_locations;
		mutatis_mutandis = true;
	}

	if (mutatis_mutandis) {
		mdc_fixup_md(entry, attr);
		/* If directory can not trust content anymore. */
		if (entry->obj_handle.type == DIRECTORY) {
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Entry %p Clearing MDCACHE_TRUST_CONTENT, MDCACHE_DIR_POPULATED",
				     entry);
			atomic_clear_uint32_t_bits(&entry->mde_flags,
						   MDCACHE_TRUST_CONTENT |
						   MDCACHE_DIR_POPULATED);
		}
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	} else {
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);
		status = fsalstat(ERR_FSAL_INVAL, 0);
	}

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

put:
	mdcache_put(entry);
out:
	op_ctx = save_ctx;
	return status;
}

/**
 * @brief Invalidate a cached entry
 *
 * @note doesn't need op_ctx, handled in mdc_up_invalidate
 *
 * @param[in] vec    Up ops vector
 * @param[in] key    Key to specify object
 * @param[in] flags  FSAL_UP_INVALIDATE*
 *
 * @return FSAL status
 */

static fsal_status_t
mdc_up_invalidate_close(const struct fsal_up_vector *vec,
			struct gsh_buffdesc *key, uint32_t flags)
{
	fsal_status_t status;

	status = up_async_invalidate(general_fridge, vec, key,
				     flags | FSAL_UP_INVALIDATE_CLOSE,
				     NULL, NULL);
	return status;
}

/** Grant a lock to a client
 *
 * Pass up to upper layer
 *
 * @param[in] vec	Up ops vector
 * @param[in] file      The file in question
 * @param[in] owner     The lock owner
 * @param[in] lock_param   A description of the lock
 *
 */
state_status_t mdc_up_lock_grant(const struct fsal_up_vector *vec,
				 struct gsh_buffdesc *file,
				 void *owner,
				 fsal_lock_param_t *lock_param)
{
	struct mdcache_fsal_export *myself = mdc_export(vec->up_fsal_export);
	state_status_t rc;
	struct req_op_context *save_ctx, req_ctx = {0};

	req_ctx.ctx_export = vec->up_gsh_export;
	req_ctx.fsal_export = vec->up_fsal_export;
	save_ctx = op_ctx;
	op_ctx = &req_ctx;

	rc = myself->super_up_ops.lock_grant(vec, file, owner,
					     lock_param);

	op_ctx = save_ctx;

	return rc;
}

/** Signal lock availability
 *
 * Pass up to upper layer
 *
 * @param[in] vec	   Up ops vector
 * @param[in] file         The file in question
 * @param[in] owner        The lock owner
 * @param[in] lock_param   A description of the lock
 *
 */
state_status_t mdc_up_lock_avail(const struct fsal_up_vector *vec,
				 struct gsh_buffdesc *file,
				 void *owner,
				 fsal_lock_param_t *lock_param)
{
	struct mdcache_fsal_export *myself = mdc_export(vec->up_fsal_export);
	state_status_t rc;
	struct req_op_context *save_ctx, req_ctx = {0};

	req_ctx.ctx_export = vec->up_gsh_export;
	req_ctx.fsal_export = vec->up_fsal_export;
	save_ctx = op_ctx;
	op_ctx = &req_ctx;

	rc = myself->super_up_ops.lock_avail(vec, file, owner,
					     lock_param);

	op_ctx = save_ctx;

	return rc;
}

/** Perform a layoutrecall on a single file
 *
 * Pass to upper layer
 *
 * @param[in] vec	   Up ops vector
 * @param[in] handle       Handle on which the layout is held
 * @param[in] layout_type  The type of layout to recall
 * @param[in] changed      Whether the layout has changed and the
 *                         client ought to finish writes through MDS
 * @param[in] segment      Segment to recall
 * @param[in] cookie       A cookie returned with the return that
 *                         completely satisfies a recall
 * @param[in] spec         Lets us be fussy about what clients we send
 *                         to. May beNULL.
 *
 */
state_status_t mdc_up_layoutrecall(const struct fsal_up_vector *vec,
				   struct gsh_buffdesc *handle,
				   layouttype4 layout_type,
				   bool changed,
				   const struct pnfs_segment *segment,
				   void *cookie,
				   struct layoutrecall_spec *spec)
{
	struct mdcache_fsal_export *myself = mdc_export(vec->up_fsal_export);
	state_status_t rc;
	struct req_op_context *save_ctx, req_ctx = {0};

	req_ctx.ctx_export = vec->up_gsh_export;
	req_ctx.fsal_export = vec->up_fsal_export;
	save_ctx = op_ctx;
	op_ctx = &req_ctx;

	rc = myself->super_up_ops.layoutrecall(vec, handle, layout_type,
					       changed, segment, cookie, spec);

	op_ctx = save_ctx;

	return rc;
}

/** Recall a delegation
 *
 * Pass to upper layer
 *
 * @param[in] vec	Up ops vector
 * @param[in] handle Handle on which the delegation is held
 */
state_status_t mdc_up_delegrecall(const struct fsal_up_vector *vec,
				  struct gsh_buffdesc *handle)
{
	struct mdcache_fsal_export *myself = mdc_export(vec->up_fsal_export);
	state_status_t rc;
	struct req_op_context *save_ctx, req_ctx = {0};

	req_ctx.ctx_export = vec->up_gsh_export;
	req_ctx.fsal_export = vec->up_fsal_export;
	save_ctx = op_ctx;
	op_ctx = &req_ctx;

	rc = myself->super_up_ops.delegrecall(vec, handle);

	op_ctx = save_ctx;

	return rc;
}

fsal_status_t
mdcache_export_up_ops_init(struct fsal_up_vector *my_up_ops,
			   const struct fsal_up_vector *super_up_ops)
{
	/* Init with super ops. Struct copy */
	*my_up_ops = *super_up_ops;

	up_ready_init(my_up_ops);

	/* Replace cache-related calls */
	my_up_ops->invalidate = mdc_up_invalidate;
	my_up_ops->update = mdc_up_update;
	my_up_ops->invalidate_close = mdc_up_invalidate_close;

	/* These are pass-through calls that set op_ctx */
	my_up_ops->lock_grant = mdc_up_lock_grant;
	my_up_ops->lock_avail = mdc_up_lock_avail;
	my_up_ops->layoutrecall = mdc_up_layoutrecall;
	/* notify_device cannot call into MDCACHE */
	my_up_ops->delegrecall = mdc_up_delegrecall;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** @} */
