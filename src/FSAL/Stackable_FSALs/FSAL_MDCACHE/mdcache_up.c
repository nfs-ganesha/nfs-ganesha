/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2019 Red Hat, Inc. and/or its affiliates.
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
#include "nfs_core.h"
#include "nfs4_fs_locations.h"

static fsal_status_t
mdc_up_invalidate(const struct fsal_up_vector *vec, struct gsh_buffdesc *handle,
		  uint32_t flags)
{
	mdcache_entry_t *entry;
	fsal_status_t status;
	struct req_op_context op_context;
	mdcache_key_t key;

	/* Get a ref to the vec->up_gsh_export and initialize op_context for the
	 * upcall
	 */
	get_gsh_export_ref(vec->up_gsh_export);
	init_op_context_simple(&op_context, vec->up_gsh_export,
			       vec->up_fsal_export);

	key.fsal = vec->up_fsal_export->sub_export->fsal;
	cih_hash_key(&key, vec->up_fsal_export->sub_export->fsal, handle,
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

	if (flags & FSAL_UP_INVALIDATE_PARENT &&
	    entry->obj_handle.type == DIRECTORY) {
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		/* Clean up parent key */
		mdcache_free_fh(&entry->fsobj.fsdir.parent);
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
	}

	mdcache_put(entry);

out:

	release_op_context();
	return status;
}

/** Release a cache entry if it's otherwise idle.
 *
 * @param[in] vec    Up ops vector
 * @param[in] handle Handle-key that should be vetted and possibly removed
 * @param[in] flags  Unused, for future expansion
 *
 * @return FSAL status. (ERR_FSAL_NO_ERROR indicates that one was released)
 */
static fsal_status_t
mdc_up_try_release(const struct fsal_up_vector *vec,
		   struct gsh_buffdesc *handle, uint32_t flags)
{
	mdcache_entry_t *entry;
	mdcache_key_t key;
	cih_latch_t latch;
	int32_t refcnt;
	fsal_status_t ret;

	/* flags are for future expansion. For now, we don't accept any. */
	if (flags)
		return fsalstat(ERR_FSAL_INVAL, 0);

	/*
	 * Find the entry, and keep the wrlock on the partition. This ensures
	 * that no other caller can find this entry in the hashtable and
	 * race in to take a reference.
	 */
	key.fsal = vec->up_fsal_export->sub_export->fsal;
	cih_hash_key(&key, vec->up_fsal_export->sub_export->fsal, handle,
		     CIH_HASH_KEY_PROTOTYPE);

	entry = cih_get_by_key_latch(&key, &latch,
				     CIH_GET_WLOCK | CIH_GET_UNLOCK_ON_MISS,
				     __func__, __LINE__);
	if (!entry) {
		LogDebug(COMPONENT_CACHE_INODE, "no entry found");
		return fsalstat(ERR_FSAL_STALE, 0);
	}

	/*
	 * We can remove it if the only ref is the sentinel. We can't put
	 * the last ref while holding the latch though, so we must take an
	 * extra reference, remove it and then put the extra ref after
	 * releasing the latch.
	 */
	refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);
	LogDebug(COMPONENT_CACHE_INODE, "entry %p has refcnt of %d", entry,
		 refcnt);
	if (refcnt == 1) {
		mdcache_get(entry);
		cih_remove_latched(entry, &latch, 0);
		ret = fsalstat(ERR_FSAL_NO_ERROR, 0);
	} else {
		ret = fsalstat(ERR_FSAL_STILL_IN_USE, 0);
	}
	cih_hash_release(&latch);
	if (refcnt == 1)
		mdcache_put(entry);
	return ret;
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
	      struct fsal_attrlist *attr, uint32_t flags)
{
	mdcache_entry_t *entry;
	fsal_status_t status;
	/* Have necessary changes been made? */
	bool mutatis_mutandis = false;
	struct req_op_context op_context;
	mdcache_key_t key;
	attrmask_t mask_set = 0;

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
	      fsal_up_update_mtime_inc |
	      fsal_up_update_spaceused_inc | fsal_up_nlink)) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Get a ref to the vec->up_gsh_export and initialize op_context for the
	 * upcall
	 */
	get_gsh_export_ref(vec->up_gsh_export);
	init_op_context_simple(&op_context, vec->up_gsh_export,
			       vec->up_fsal_export);

	key.fsal = vec->up_fsal_export->sub_export->fsal;
	cih_hash_key(&key, vec->up_fsal_export->sub_export->fsal, handle,
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
				mask_set |= ATTR_SIZE;
			}
		} else {
			entry->attrs.filesize = attr->filesize;
			mutatis_mutandis = true;
			mask_set |= ATTR_SIZE;
		}
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_SPACEUSED)) {
		if (flags & fsal_up_update_spaceused_inc) {
			if (attr->spaceused > entry->attrs.spaceused) {
				entry->attrs.spaceused = attr->spaceused;
				mutatis_mutandis = true;
				mask_set |= ATTR_SPACEUSED;
			}
		} else {
			entry->attrs.spaceused = attr->spaceused;
			mutatis_mutandis = true;
			mask_set |= ATTR_SPACEUSED;
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
		mask_set |= ATTR_ACL;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_MODE)) {
		entry->attrs.mode = attr->mode;
		mutatis_mutandis = true;
		mask_set |= ATTR_MODE;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_NUMLINKS)) {
		entry->attrs.numlinks = attr->numlinks;
		mutatis_mutandis = true;
		mask_set |= ATTR_NUMLINKS;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_OWNER)) {
		entry->attrs.owner = attr->owner;
		mutatis_mutandis = true;
		mask_set |= ATTR_OWNER;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_GROUP)) {
		entry->attrs.group = attr->group;
		mutatis_mutandis = true;
		mask_set |= ATTR_GROUP;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME)
	    && ((flags & ~fsal_up_update_atime_inc)
		||
		(gsh_time_cmp(&attr->atime, &entry->attrs.atime) == 1))) {
		entry->attrs.atime = attr->atime;
		mutatis_mutandis = true;
		mask_set |= ATTR_ATIME;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_CREATION)
	    && ((flags & ~fsal_up_update_creation_inc)
		||
		(gsh_time_cmp(&attr->creation, &entry->attrs.creation) == 1))) {
		entry->attrs.creation = attr->creation;
		mutatis_mutandis = true;
		mask_set |= ATTR_CREATION;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_CTIME)
	    && ((flags & ~fsal_up_update_ctime_inc)
		||
		(gsh_time_cmp(&attr->ctime, &entry->attrs.ctime) == 1))) {
		entry->attrs.ctime = attr->ctime;
		mutatis_mutandis = true;
		mask_set |= ATTR_CTIME;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME)
	    && ((flags & ~fsal_up_update_mtime_inc)
		||
		(gsh_time_cmp(&attr->mtime, &entry->attrs.mtime) == 1))) {
		entry->attrs.mtime = attr->mtime;
		mutatis_mutandis = true;
		mask_set |= ATTR_MTIME;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR_CHANGE)) {
		entry->attrs.change = attr->change;
		mutatis_mutandis = true;
		mask_set |= ATTR_CHANGE;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR4_FS_LOCATIONS)) {
		nfs4_fs_locations_release(entry->attrs.fs_locations);

		entry->attrs.fs_locations = attr->fs_locations;
		mutatis_mutandis = true;
		mask_set |= ATTR4_FS_LOCATIONS;
	}

	if (FSAL_TEST_MASK(attr->valid_mask, ATTR4_SEC_LABEL)) {
		gsh_free(entry->attrs.sec_label.slai_data.slai_data_val);
		entry->attrs.sec_label = attr->sec_label;
		attr->sec_label.slai_data.slai_data_len = 0;
		attr->sec_label.slai_data.slai_data_val = NULL;
		mutatis_mutandis = true;
		mask_set |= ATTR4_SEC_LABEL;
	}

	if (mutatis_mutandis) {
		mdc_fixup_md(entry, attr);
		entry->attrs.valid_mask |= mask_set;
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

	release_op_context();
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
	struct req_op_context op_context;

	/* Get a ref to the vec->up_gsh_export and initialize op_context for the
	 * upcall
	 */
	get_gsh_export_ref(vec->up_gsh_export);
	init_op_context_simple(&op_context, vec->up_gsh_export,
			       vec->up_fsal_export);

	rc = myself->super_up_ops.lock_grant(vec, file, owner, lock_param);

	release_op_context();

	return rc;
}

/** Signal lock availability
 *
 * Pass up to upper layer
 *
 * @param[in] vec	   Up ops vector
 * @param[in] file	 The file in question
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
	struct req_op_context op_context;

	/* Initialize op context */
	/* Get a ref to the vec->up_gsh_export and initialize op_context for the
	 * upcall
	 */
	get_gsh_export_ref(vec->up_gsh_export);
	init_op_context_simple(&op_context, vec->up_gsh_export,
			       vec->up_fsal_export);

	rc = myself->super_up_ops.lock_avail(vec, file, owner, lock_param);

	release_op_context();

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
	struct req_op_context op_context;

	/* Get a ref to the vec->up_gsh_export and initialize op_context for the
	 * upcall
	 */
	get_gsh_export_ref(vec->up_gsh_export);
	init_op_context_simple(&op_context, vec->up_gsh_export,
			       vec->up_fsal_export);

	rc = myself->super_up_ops.layoutrecall(vec, handle, layout_type,
					       changed, segment, cookie, spec);

	release_op_context();

	return rc;
}

/** Recall a delegation
 *
 * Pass to upper layer
 *
 * @param[in] vec	Up ops vector
 * @param[in] handle    The handle on which the delegation is held
 */
state_status_t mdc_up_delegrecall(const struct fsal_up_vector *vec,
				  struct gsh_buffdesc *handle)
{
	struct mdcache_fsal_export *myself = mdc_export(vec->up_fsal_export);
	state_status_t rc;
	struct req_op_context op_context;

	/* Get a ref to the vec->up_gsh_export and initialize op_context for the
	 * upcall
	 */
	get_gsh_export_ref(vec->up_gsh_export);
	init_op_context_simple(&op_context, vec->up_gsh_export,
			       vec->up_fsal_export);

	rc = myself->super_up_ops.delegrecall(vec, handle);

	release_op_context();

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
	my_up_ops->try_release = mdc_up_try_release;

	/* These are pass-through calls that set op_ctx */
	my_up_ops->lock_grant = mdc_up_lock_grant;
	my_up_ops->lock_avail = mdc_up_lock_avail;
	my_up_ops->layoutrecall = mdc_up_layoutrecall;
	/* notify_device cannot call into MDCACHE */
	my_up_ops->delegrecall = mdc_up_delegrecall;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** @} */
