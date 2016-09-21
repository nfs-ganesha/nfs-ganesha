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

#include "sal_functions.h"
#include "fsal.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_convert.h"

#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>

#include "nfs_exports.h"

#include "mdcache_lru.h"
#include "mdcache_hash.h"
#include "mdcache_avl.h"

static inline bool trust_negative_cache(mdcache_entry_t *parent)
{
	return op_ctx_export_has_option(
				  EXPORT_OPTION_TRUST_READIR_NEGATIVE_CACHE) &&
		parent->icreate_refcnt == 0 &&
		(parent->mde_flags & MDCACHE_DIR_POPULATED) != 0;
}

/**
 * @brief Fetch optional attributes
 *
 * The mask should be set in attrs_out indicating which attributes are
 * desired. If ATTR_RDATTR_ERR is set, and the attribute fetch fails,
 * the requested handle will still be returned, however the attributes
 * will not be set, otherwise, if the attributes are requested and the
 * getattrs fails, the lookup itself will fail.
 *
 * @param[in]     obj_hdl   Object to get attributes for.
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @return FSAL status.
 */
fsal_status_t get_optional_attrs(struct fsal_obj_handle *obj_hdl,
				 struct attrlist *attrs_out)
{
	fsal_status_t status;

	if (attrs_out == NULL)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	status = obj_hdl->obj_ops.getattrs(obj_hdl, attrs_out);

	if (FSAL_IS_ERROR(status)) {
		if (attrs_out->mask & ATTR_RDATTR_ERR) {
			/* Indicate the failure of requesting attributes by
			 * marking the ATTR_RDATTR_ERR in the mask.
			 */
			attrs_out->mask = ATTR_RDATTR_ERR;
			status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		} /* otherwise let the error stand. */
	}

	return status;
}

/**
 * Allocate and initialize a new mdcache handle.
 *
 * This function doesn't free the sub_handle if the allocation fails. It must
 * be done in the calling function.
 *
 * @param[in] export The mdcache export used by the handle.
 * @param[in] sub_handle The handle used by the subfsal.
 * @param[in] fs The filesystem of the new handle.
 *
 * @return The new handle, or NULL if the allocation failed.
 */
static mdcache_entry_t *mdcache_alloc_handle(
		struct mdcache_fsal_export *export,
		struct fsal_obj_handle *sub_handle,
		struct fsal_filesystem *fs)
{
	mdcache_entry_t *result;

	mdcache_lru_get(&result);
	if (!result)
		return NULL;

	/* Base data */
	result->sub_handle = sub_handle;
	result->obj_handle.type = sub_handle->type;
	result->obj_handle.fsid = sub_handle->fsid;
	result->obj_handle.fileid = sub_handle->fileid;
	result->obj_handle.fs = fs;

	/* default handlers */
	fsal_obj_handle_init(&result->obj_handle, &export->export,
			     sub_handle->type);
	/* mdcache handlers */
	mdcache_handle_ops_init(&result->obj_handle.obj_ops);
	/* state */
	if (sub_handle->type == DIRECTORY)
		result->obj_handle.state_hdl = &result->fsobj.fsdir.dhdl;
	else
		result->obj_handle.state_hdl = &result->fsobj.hdl;
	state_hdl_init(result->obj_handle.state_hdl, result->obj_handle.type,
		       &result->obj_handle);

	/* Initialize common fields */
	result->mde_flags = 0;
	result->icreate_refcnt = 0;
	glist_init(&result->export_list);

	return result;
}

/**
 *
 * @brief Cleans up an entry so it can be reused
 *
 * @param[in]  entry     The cache entry to clean
 */
void mdc_clean_entry(mdcache_entry_t *entry)
{
	struct glist_head *glist;
	struct glist_head *glistn;

	/* Must get attr_lock before mdc_exp_lock */
	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	glist_for_each_safe(glist, glistn, &entry->export_list) {
		struct entry_export_map *expmap;
		struct mdcache_fsal_export *export;

		expmap = glist_entry(glist,
				     struct entry_export_map,
				     export_per_entry);
		export = expmap->export;

		PTHREAD_RWLOCK_wrlock(&export->mdc_exp_lock);

		mdc_remove_export_map(expmap);

		PTHREAD_RWLOCK_unlock(&export->mdc_exp_lock);
	}

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	/* Clear out first_export */
	atomic_store_voidptr(&entry->first_export, NULL);

	if (entry->obj_handle.type == DIRECTORY) {
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);

		/* Clean up dirents */
		(void) mdcache_dirent_invalidate_all(entry);
		/* Clean up parent key */
		mdcache_key_delete(&entry->fsobj.fsdir.parent);

		PTHREAD_RWLOCK_unlock(&entry->content_lock);
	}
	cih_remove_checked(entry);

}

/**
 *
 * Check the active export mapping for this entry and update if necessary.
 *
 * If the entry does not have a mapping for the active export, add one.
 *
 * @param[in]  entry     The cache inode
 * @param[in]  export    The active export
 *
 * @return FSAL Status
 *
 */

static void
mdc_check_mapping(mdcache_entry_t *entry)
{
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct glist_head *glist;
	struct entry_export_map *expmap;
	bool try_write = false;

	/* Fast path check to see if this export is already mapped */
	if (atomic_fetch_voidptr(&entry->first_export) == export)
		return;

	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

again:
	(void)atomic_inc_uint64_t(&cache_stp->inode_mapping);

	glist_for_each(glist, &entry->export_list) {
		expmap = glist_entry(glist, struct entry_export_map,
				     export_per_entry);

		/* Found active export on list */
		if (expmap->export == export) {
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			return;
		}
	}

	if (!try_write) {
		/* Now take write lock and try again in
		 * case another thread has raced with us.
		 */
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
		try_write = true;
		goto again;
	}

	/* We have the write lock and did not find
	 * this export on the list, add it.
	 */
	expmap = gsh_calloc(1, sizeof(*expmap));

	PTHREAD_RWLOCK_wrlock(&export->mdc_exp_lock);

	/* If export_list is empty, store this export as first */
	if (glist_empty(&entry->export_list))
		atomic_store_voidptr(&entry->first_export, export);

	expmap->export = export;
	expmap->entry = entry;

	glist_add_tail(&entry->export_list, &expmap->export_per_entry);
	glist_add_tail(&export->entry_list, &expmap->entry_per_export);

	PTHREAD_RWLOCK_unlock(&export->mdc_exp_lock);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
}

/**
 * @brief Invalidates and releases all cached entries for a directory
 *
 * Invalidates all the entries for a cached directory.  The content
 * lock must be held for write when this function is called.
 *
 * @param[in,out] entry  The directory to be managed
 *
 */

void mdcache_dirent_invalidate_all(mdcache_entry_t *entry)
{
	/* Won't see this */
	if (entry->obj_handle.type != DIRECTORY)
		return;

	/* First the active tree */
	mdcache_avl_clean_tree(&entry->fsobj.fsdir.avl.t);
	entry->fsobj.fsdir.nbactive = 0;
	atomic_clear_uint32_t_bits(&entry->mde_flags, MDCACHE_DIR_POPULATED);

	/* Next the inactive tree */
	mdcache_avl_clean_tree(&entry->fsobj.fsdir.avl.c);

	/* Now we can trust the content */
	atomic_set_uint32_t_bits(&entry->mde_flags, MDCACHE_TRUST_CONTENT);
}

/**
 * @brief Adds a new entry to the cache
 *
 * This function adds a new entry to the cache.  It will allocate
 * entries of any kind.
 *
 * The caller is responsible for releasing attrs_in, however, the references
 * will have been transferred to the new mdcache entry. fsal_copy_attrs leaves
 * the state of the source attributes still safe to call fsal_release_attrs,
 * so all will be well.
 *
 * @param[in]     export         Export for this cache
 * @param[in]     sub_handle     Handle for sub-FSAL
 * @param[in]     attrs_in       Attributes provided for the object
 * @param[in,out] attrs_out      Attributes requested for the object
 * @param[in]     new_directory  Indicate a new directory was created
 * @param[out]    entry          Newly instantiated cache entry
 * @param[in]     state          Optional state_t representing open file.
 *
 * @note This returns an INITIAL ref'd entry on success
 *
 * @return FSAL status
 */
fsal_status_t
mdcache_new_entry(struct mdcache_fsal_export *export,
		  struct fsal_obj_handle *sub_handle,
		  struct attrlist *attrs_in,
		  struct attrlist *attrs_out,
		  bool new_directory,
		  mdcache_entry_t **entry,
		  struct state_t *state)
{
	fsal_status_t status;
	mdcache_entry_t *oentry, *nentry = NULL;
	struct gsh_buffdesc fh_desc;
	cih_latch_t latch;
	bool has_hashkey = false;
	int rc = 0;
	mdcache_key_t key;

	*entry = NULL;

	/* Get FSAL-specific key */
	subcall_raw(export,
		    sub_handle->obj_ops.handle_to_key(sub_handle, &fh_desc);
		   );

	(void) cih_hash_key(&key, export->export.sub_export->fsal, &fh_desc,
			    CIH_HASH_KEY_PROTOTYPE);

	/* Check if the entry already exists.  We allow the following race
	 * because mdcache_lru_get has a slow path, and the latch is a
	 * shared lock. */
	status = mdcache_find_keyed(&key, entry);
	if (!FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Trying to add an already existing entry. Found entry %p type: %d, New type: %d",
			 *entry, (*entry)->obj_handle.type, sub_handle->type);

		/* If it was unreachable before, mark it reachable */
		atomic_clear_uint32_t_bits(&(*entry)->mde_flags,
					 MDCACHE_UNREACHABLE);

		/* Don't need a new sub_handle ref */
		goto out_release;
	} else if (status.major != ERR_FSAL_NOENT) {
		/* Real error , don't need a new sub_handle ref */
		goto out_release;
	}

	/* !LATCHED */

	/* We did not find the object.  Pull an entry off the LRU. */
	nentry = mdcache_alloc_handle(export, sub_handle, sub_handle->fs);
	if (!nentry) {
		LogCrit(COMPONENT_CACHE_INODE, "mdcache_alloc_handle failed");
		status = fsalstat(ERR_FSAL_NOMEM, 0);
		goto out_release;
	}

	/* See if someone raced us. */
	oentry = cih_get_by_key_latch(&key, &latch, CIH_GET_WLOCK, __func__,
					__LINE__);
	if (oentry) {
		/* Entry is already in the cache, do not add it. */
		LogDebug(COMPONENT_CACHE_INODE,
			 "lost race to add entry %p type: %d, New type: %d",
			 oentry, oentry->obj_handle.type, sub_handle->type);
		*entry = oentry;

		/* Ref it */
		status = mdcache_lru_ref(*entry, LRU_REQ_INITIAL);
		if (!FSAL_IS_ERROR(status)) {
			/* We used to return ERR_FSAL_EXIST but all callers
			 * just converted that to ERR_FSAL_NO_ERROR, so
			 * leave the status alone.
			 */
			(void)atomic_inc_uint64_t(&cache_stp->inode_conf);
		}

		/* It it was unreachable before, mark it reachable */
		atomic_clear_uint32_t_bits(&(*entry)->mde_flags,
					 MDCACHE_UNREACHABLE);

		/* Release the subtree hash table lock */
		cih_hash_release(&latch);

		goto out;
	}

	/* We won the race. */

	/* Set cache key */

	has_hashkey = cih_hash_key(&nentry->fh_hk.key,
				   export->export.sub_export->fsal,
				   &fh_desc, CIH_HASH_NONE);

	if (!has_hashkey) {
		cih_hash_release(&latch);
		LogCrit(COMPONENT_CACHE_INODE,
			"Could not hash new entry");
		status = fsalstat(ERR_FSAL_NOMEM, 0);
		goto out;
	}

	switch (nentry->obj_handle.type) {
	case REGULAR_FILE:
		LogDebug(COMPONENT_CACHE_INODE,
			 "Adding a REGULAR_FILE, entry=%p", nentry);

		/* Init statistics used for intelligently granting delegations*/
		init_deleg_heuristics(&nentry->obj_handle);
		break;

	case DIRECTORY:
		LogDebug(COMPONENT_CACHE_INODE, "Adding a DIRECTORY, entry=%p",
			 nentry);

		atomic_set_uint32_t_bits(&nentry->mde_flags,
					 MDCACHE_TRUST_CONTENT);

		/* If the directory is newly created, it is empty.  Because
		   we know its content, we consider it read. */
		if (new_directory) {
			atomic_set_uint32_t_bits(&nentry->mde_flags,
						 MDCACHE_DIR_POPULATED);
		} else {
			atomic_clear_uint32_t_bits(&nentry->mde_flags,
						   MDCACHE_DIR_POPULATED);
		}

		/* init avl tree */
		mdcache_avl_init(nentry);
		break;

	case SYMBOLIC_LINK:
	case SOCKET_FILE:
	case FIFO_FILE:
	case BLOCK_FILE:
	case CHARACTER_FILE:
		LogDebug(COMPONENT_CACHE_INODE,
			 "Adding a special file of type %d entry=%p",
			 nentry->obj_handle.type, nentry);
		break;

	default:
		/* Should never happen */
		cih_hash_release(&latch);
		status = fsalstat(ERR_FSAL_INVAL, 0);
		LogMajor(COMPONENT_CACHE_INODE, "unknown type %u provided",
			 nentry->obj_handle.type);
		goto out;
	}

	/* nentry not reachable yet; no need to lock */

	/* Copy over the attributes and pass off the ACL reference. We also
	 * copy the output attrs at this point to avoid needing the attr_lock.
	 */
	if (attrs_out != NULL)
		fsal_copy_attrs(attrs_out, attrs_in, false);

	fsal_copy_attrs(&nentry->attrs, attrs_in, true);

	if (nentry->attrs.expire_time_attr == 0) {
		nentry->attrs.expire_time_attr =
		    atomic_fetch_uint32_t(
			    &op_ctx->ctx_export->expire_time_attr);
	}

	/* Validate the attributes we just set. */
	mdc_fixup_md(nentry, nentry->attrs.mask);

	/* Hash and insert entry, after this would need attr_lock to
	 * access attributes.
	 */
	rc = cih_set_latched(nentry, &latch,
			     op_ctx->fsal_export->fsal, &fh_desc,
			     CIH_SET_UNLOCK | CIH_SET_HASHED);
	if (unlikely(rc)) {
		LogCrit(COMPONENT_CACHE_INODE,
			"entry could not be added to hash, rc=%d", rc);
		status = fsalstat(ERR_FSAL_NOMEM, 0);
		if (attrs_out != NULL) {
			/* Release the attrs we just copied. */
			fsal_release_attrs(attrs_out);
		}
		goto out;
	}

	/* Map this new entry and the active export */
	mdc_check_mapping(nentry);

	LogDebug(COMPONENT_CACHE_INODE, "New entry %p added", nentry);
	*entry = nentry;
	(void)atomic_inc_uint64_t(&cache_stp->inode_added);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 out:

	if (nentry != NULL) {
		/* We raced or failed, deconstruct the new entry, release
		 * the attributes, we may not have copied yet, in which case
		 * mask and acl are 0/NULL.
		 */
		fsal_release_attrs(&nentry->attrs);

		/* Destroy the export mapping if any */
		mdc_clean_entry(nentry);

		/* Destroy the locks */
		PTHREAD_RWLOCK_destroy(&nentry->attr_lock);
		PTHREAD_RWLOCK_destroy(&nentry->content_lock);

		if (has_hashkey)
			mdcache_key_delete(&nentry->fh_hk.key);

		/* Release the new entry we acquired. */
		mdcache_lru_putback(nentry, LRU_FLAG_NONE);
	}

 out_release:

	/* If attributes were requested, fetch them now if we still have a
	 * success return since we did not actually create a new object and
	 * use the provided attributes (we can't trust that the provided
	 * attributes are newer).
	 *
	 * NOTE: There can not be an ABBA lock ordering issue since our caller
	 *        does not hold a lock on the "new" entry.
	 */
	if (!FSAL_IS_ERROR(status) && attrs_out != NULL) {
		status = get_optional_attrs(&(*entry)->obj_handle,
					    attrs_out);
		if (FSAL_IS_ERROR(status)) {
			/* Oops, failed to get attributes and ATTR_RDATTR_ERR
			 * was not requested, so we are failing and thus must
			 * drop the object reference we got.
			 */
			mdcache_put(*entry);
			*entry = NULL;
		}
	}

	if (!FSAL_IS_ERROR(status)) {
		/* Give the FSAL a chance to merge new_obj into
		 * oentry->obj_handle since we will be using
		 * oentry->obj_handle for all access to the oject.
		 */
		struct fsal_obj_handle *old_sub_handle = (*entry)->sub_handle;

		status =
		    old_sub_handle->obj_ops.merge(old_sub_handle, sub_handle);

		if (FSAL_IS_ERROR(status)) {
			/* Report this error and unref the entry */
			LogDebug(COMPONENT_CACHE_INODE,
				 "Merge of object handles after race returned %s",
				 fsal_err_txt(status));

			mdcache_put(*entry);
			*entry = NULL;
		}
	}

	if (FSAL_IS_ERROR(status) && state != NULL) {
		/* Our caller passed in a state for an open file, since
		 * there is not a valid entry to use, or a merge failed
		 * we must close that file before disposing of new_obj.
		 */
		fsal_status_t status = sub_handle->obj_ops.close2(sub_handle,
								  state);

		LogDebug(COMPONENT_CACHE_INODE,
			 "Close of state during error processing returned %s",
			 fsal_err_txt(status));
	}

	/* must free sub_handle if no new entry was created to reference it. */
	sub_handle->obj_ops.release(sub_handle);

	return status;
}

/**
 * @brief Find a cache entry by it's key
 *
 * Lookup a cache entry by key.  If it is not in the cache, it is not returned.
 *
 * @param[in] key	Cache key to use for lookup
 * @param[out] entry	Entry, if found
 *
 * @note This returns an INITIAL ref'd entry on success
 *
 * @return Status
 */
fsal_status_t
mdcache_find_keyed(mdcache_key_t *key, mdcache_entry_t **entry)
{
	cih_latch_t latch;

	if (key->kv.addr == NULL) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Attempt to use NULL key");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	*entry = cih_get_by_key_latch(key, &latch,
					CIH_GET_RLOCK | CIH_GET_UNLOCK_ON_MISS,
					__func__, __LINE__);
	if (likely(*entry)) {
		fsal_status_t status;
		/* Initial Ref on entry */
		status = mdcache_lru_ref(*entry, LRU_REQ_INITIAL);
		/* Release the subtree hash table lock */
		cih_hash_release(&latch);
		if (FSAL_IS_ERROR(status)) {
			/* Return error instead of entry */
			*entry = NULL;
			return status;
		}

		mdc_check_mapping(*entry);
		(void)atomic_inc_uint64_t(&cache_stp->inode_hit);
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	return fsalstat(ERR_FSAL_NOENT, 0);
}

/**
 * @brief Find or create a cache entry by it's key
 *
 * Locate a cache entry by key.  If it is not in the cache, an attempt will be
 * made to create it and insert it in the cache.
 *
 * @param[in]     key       Cache key to use for lookup
 * @param[in]     export    Export for this cache
 * @param[out]    entry     Entry, if found
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note This returns an INITIAL ref'd entry on success
 *
 * @return Status
 */
fsal_status_t
mdcache_locate_keyed(mdcache_key_t *key,
		     struct mdcache_fsal_export *export,
		     mdcache_entry_t **entry,
		     struct attrlist *attrs_out)
{
	fsal_status_t status;
	struct fsal_obj_handle *sub_handle;
	struct fsal_export *sub_export;
	struct attrlist attrs;

	status = mdcache_find_keyed(key, entry);

	if (!FSAL_IS_ERROR(status)) {
		status = get_optional_attrs(&(*entry)->obj_handle, attrs_out);
		return status;
	} else if (status.major != ERR_FSAL_NOENT) {
		/* Actual error */
		return status;
	}

	/* Cache miss, allocate a new entry */
	fsal_prepare_attrs(&attrs, op_ctx->fsal_export->exp_ops.
				   fs_supported_attrs(op_ctx->fsal_export));

	sub_export = export->export.sub_export;

	subcall_raw(export,
		    status = sub_export->exp_ops.create_handle(sub_export,
							       &key->kv,
							       &sub_handle,
							       &attrs)
	       );

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "create_handle failed with %s",
			 fsal_err_txt(status));
		*entry = NULL;
		fsal_release_attrs(&attrs);
		return status;
	}

	status = mdcache_new_entry(export, sub_handle, &attrs, attrs_out,
				   false, entry, NULL);

	fsal_release_attrs(&attrs);

	if (!FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create_handle Created entry %p FSAL %s",
			     (*entry), (*entry)->sub_handle->fsal->name);
	}

	return status;
}

/**
 * @brief Create a new entry and add it to the parent's cache
 *
 * A new entry for @a sub_handle is created, and it is added to the dirent cache
 * of @a mdc_parent.
 *
 * @note mdc_parent MUST have it's content_lock held for writing
 *
 * @param[in]     mdc_parent  Parent entry
 * @param[in]     name        Name of new entry
 * @param[in]     sub_handle  Handle from sub-FSAL for new entry
 * @param[in]     attrs_in    Attributes for new entry
 *
 * @return FSAL status
 */

fsal_status_t mdc_add_cache(mdcache_entry_t *mdc_parent,
			    const char *name,
			    struct fsal_obj_handle *sub_handle,
			    struct attrlist *attrs_in)
{
	struct mdcache_fsal_export *export = mdc_cur_export();
	fsal_status_t status;
	mdcache_entry_t *new_entry = NULL;

	LogFullDebug(COMPONENT_CACHE_INODE, "Creating entry for %s", name);

	status = mdcache_new_entry(export, sub_handle, attrs_in, NULL,
				   false, &new_entry, NULL);

	if (FSAL_IS_ERROR(status))
		return status;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Created entry %p FSAL %s for %s",
		     new_entry, new_entry->sub_handle->fsal->name, name);

	/* Entry was found in the FSAL, add this entry to the
	   parent directory */
	status = mdcache_dirent_add(mdc_parent, name, new_entry);

	if (status.major == ERR_FSAL_EXIST)
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	if (!FSAL_IS_ERROR(status) && new_entry->obj_handle.type == DIRECTORY) {
		/* Insert Parent's key */
		mdc_dir_add_parent(new_entry, mdc_parent);
	}

	mdcache_put(new_entry);

	return status;
}

/**
 * @brief Try to get a cached child
 *
 * Get the cached entry child of @a mdc_parent If the cached entry cannot be
 * found, for whatever reason, return ERR_FSAL_STALE
 *
 * @note Caller MUST hold the content_lock for read
 *
 * @param[in]     mdc_parent     Parent directory
 * @param[in]     name           Name of child
 * @param[out]    entry	         Child entry, on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
fsal_status_t mdc_try_get_cached(mdcache_entry_t *mdc_parent,
				 const char *name,
				 mdcache_entry_t **entry)
{
	mdcache_dir_entry_t *dirent = NULL;
	fsal_status_t status = {0, 0};

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Look in cache %s, trust content %s",
		     name,
		     mdc_parent->mde_flags & MDCACHE_TRUST_CONTENT
				? "yes" : "no");

	*entry = NULL;

	/* If the dirent cache is untrustworthy, don't even ask it */
	if (!(mdc_parent->mde_flags & MDCACHE_TRUST_CONTENT))
		return fsalstat(ERR_FSAL_STALE, 0);

	dirent = mdcache_avl_qp_lookup_s(mdc_parent, name, 1);
	if (dirent) {
		status = mdcache_find_keyed(&dirent->ckey, entry);
		if (!FSAL_IS_ERROR(status))
			return status;
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "mdcache_find_keyed %s failed %s",
			     name, fsal_err_txt(status));
	} else {	/* ! dirent */
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "mdcache_avl_qp_lookup_s %s failed trust negative %s",
			     name,
			     trust_negative_cache(mdc_parent) ? "yes" : "no");
		if (trust_negative_cache(mdc_parent)) {
			/* If the dirent cache is both fully populated and
			 * valid, it can serve negative lookups. */
			return fsalstat(ERR_FSAL_NOENT, 0);
		}
	}
	return fsalstat(ERR_FSAL_STALE, 0);
}

/**
 * @brief Lookup a name (helper)
 *
 * Lookup a name relative to another object.  If @a uncached is true and a cache
 * miss occurs, then the underlying file is looked up and added to the cache, if
 * it exists.
 *
 * The caller will set the mask in attrs_out to indicate the attributes of
 * interest.
 *
 * Since this method instantiates a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * @param[in]     parent    Handle of container
 * @param[in]     name      Name to look up
 * @param[in]     uncached  If true, do an uncached lookup on cache failure
 * @param[out]    handle    Handle of found object, on success
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
fsal_status_t mdc_lookup(mdcache_entry_t *mdc_parent, const char *name,
			 bool uncached, mdcache_entry_t **new_entry,
			 struct attrlist *attrs_out)
{
	*new_entry = NULL;
	fsal_status_t status;

	LogFullDebug(COMPONENT_CACHE_INODE, "Lookup %s", name);

	PTHREAD_RWLOCK_rdlock(&mdc_parent->content_lock);

	if (!strcmp(name, "..")) {
		struct mdcache_fsal_export *export = mdc_cur_export();

		LogFullDebug(COMPONENT_CACHE_INODE, "Lookup parent (..)");
		/* ".." doesn't end up in the cache */
		status =  mdcache_locate_keyed(&mdc_parent->fsobj.fsdir.parent,
					       export, new_entry, attrs_out);
		goto out;
	}

	/* We first try avltree_lookup by name.  If that fails, we dispatch to
	 * the FSAL. */
	status = mdc_try_get_cached(mdc_parent, name, new_entry);

	if (status.major == ERR_FSAL_STALE) {
		/* Get a write lock and try again */
		PTHREAD_RWLOCK_unlock(&mdc_parent->content_lock);

		LogFullDebug(COMPONENT_CACHE_INODE, "Try again %s", name);

		PTHREAD_RWLOCK_wrlock(&mdc_parent->content_lock);

		status = mdc_try_get_cached(mdc_parent, name, new_entry);
	}
	if (!FSAL_IS_ERROR(status)) {
		/* Success! Now fetch attr if requested, drop content_lock
		 * to avoid ABBA locking situation.
		 */
		PTHREAD_RWLOCK_unlock(&mdc_parent->content_lock);

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Found, possible getattrs %s (%s)",
			     name, attrs_out != NULL ? "yes" : "no");

		status = get_optional_attrs(&(*new_entry)->obj_handle,
					    attrs_out);

		if (FSAL_IS_ERROR(status)) {
			/* Oops, failed to get attributes and ATTR_RDATTR_ERR
			 * was not requested, so we are failing lookup and
			 * thus must drop the object reference we got.
			 */
			mdcache_put(*new_entry);
			*new_entry = NULL;
		}
		return status;
	} else if (!uncached) {
		/* Was only looking in cache, so don't bother looking further */
		goto out;
	} else if (status.major != ERR_FSAL_STALE) {
		/* Actual failure */
		LogDebug(COMPONENT_CACHE_INODE, "Lookup %s failed %s",
			 name, fsal_err_txt(status));
		goto out;
	}

	/* Need to look up. */
	if (!(mdc_parent->mde_flags & MDCACHE_TRUST_CONTENT)) {
		/* We have the write lock and the content is
		 * still invalid.  Empty it out and mark it
		 * valid in preparation for caching the result of this lookup.
		 */
		mdcache_dirent_invalidate_all(mdc_parent);
	}

	LogDebug(COMPONENT_CACHE_INODE, "Cache Miss detected for %s", name);

	status = mdc_lookup_uncached(mdc_parent, name, new_entry, attrs_out);

out:
	PTHREAD_RWLOCK_unlock(&mdc_parent->content_lock);
	if (status.major == ERR_FSAL_STALE)
		status.major = ERR_FSAL_NOENT;
	return status;
}

/**
 * @brief Lookup an uncached entry from the sub-FSAL
 *
 * The entry has already been determined to not be cached, and the parent is
 * already write-locked.  Lookup the child and create a cached entry for it.
 *
 * @note mdc_parent MUST have it's content_lock held for writing
 *
 * @param[in]     mdc_parent	Parent entry
 * @param[in]     name		Name of entry to find
 * @param[out]    new_entry	New entry to return;
 * @param[in,out] attrs_out     Optional attributes for entry
 *
 * @return FSAL status
 */
fsal_status_t mdc_lookup_uncached(mdcache_entry_t *mdc_parent,
				  const char *name,
				  mdcache_entry_t **new_entry,
				  struct attrlist *attrs_out)
{
	struct fsal_obj_handle *sub_handle = NULL, *new_obj = NULL;
	fsal_status_t status;
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct attrlist attrs;

	fsal_prepare_attrs(&attrs, op_ctx->fsal_export->exp_ops.
				   fs_supported_attrs(op_ctx->fsal_export));


	subcall(
		status = mdc_parent->sub_handle->obj_ops.lookup(
			    mdc_parent->sub_handle, name, &sub_handle, &attrs)
	       );

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "lookup %s failed with %s",
			 name, fsal_err_txt(status));
		*new_entry = NULL;
		fsal_release_attrs(&attrs);
		return status;
	}

	status = mdcache_alloc_and_check_handle(export, sub_handle, &new_obj,
						false, &attrs, attrs_out,
						"lookup ", mdc_parent, name,
						true, NULL);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_ERROR(status)) {
		*new_entry = NULL;
	} else {
		*new_entry = container_of(new_obj, mdcache_entry_t, obj_handle);
	}

	return status;
}

/**
 * @brief Lock two directories in order
 *
 * This function gets the locks on both entries. If src and dest are
 * the same, it takes only one lock.  Locks are acquired with lowest
 * cache_entry first to avoid deadlocks.
 *
 * @param[in] src  Source directory to lock
 * @param[in] dest Destination directory to lock
 */

void
mdcache_src_dest_lock(mdcache_entry_t *src, mdcache_entry_t *dest)
{
	int rc;

	/*
	 * A problem found in this order
	 * 1. mdcache_readdir holds A's content_lock, and tries to
	 * grab B's attr_lock.
	 * 2. mdcache_remove holds B's attr_lock, and tries to grab B's
	 * content_lock
	 * 3. mdcache_rename holds B's content_lock, and tries to grab the
	 * A's content_lock (which is held by thread 1).
	 * This change is to avoid this deadlock.
	 */

retry_lock:
	if (src == dest)
		PTHREAD_RWLOCK_wrlock(&src->content_lock);
	else if (src < dest) {
		PTHREAD_RWLOCK_wrlock(&src->content_lock);
		rc = pthread_rwlock_trywrlock(&dest->content_lock);
		if (rc) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "retry dest %p lock, src %p",
				 dest, src);
			PTHREAD_RWLOCK_unlock(&src->content_lock);
			sleep(1);
			goto retry_lock;
		}
	} else {
		PTHREAD_RWLOCK_wrlock(&dest->content_lock);
		rc = pthread_rwlock_trywrlock(&src->content_lock);
		if (rc) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "retry src %p lock, dest %p",
				 src, dest);
			PTHREAD_RWLOCK_unlock(&dest->content_lock);
			sleep(1);
			goto retry_lock;
		}
	}
}

/**
 * @brief Unlock two directories in order
 *
 * This function releases the locks on both entries. If src and dest
 * are the same, it releases the lock and returns.  Locks are released
 * with lowest cache_entry first.
 *
 * @param[in] src  Source directory to lock
 * @param[in] dest Destination directory to lock
 */

void
mdcache_src_dest_unlock(mdcache_entry_t *src, mdcache_entry_t *dest)
{
	if (src == dest)
		PTHREAD_RWLOCK_unlock(&src->content_lock);
	else if (src < dest) {
		PTHREAD_RWLOCK_unlock(&dest->content_lock);
		PTHREAD_RWLOCK_unlock(&src->content_lock);
	} else {
		PTHREAD_RWLOCK_unlock(&src->content_lock);
		PTHREAD_RWLOCK_unlock(&dest->content_lock);
	}
}

/**
 * @brief Find a cached directory entry
 *
 * Look up the entry in the cache.  Success is if found (obviously), or if the
 * cache isn't trusted.  NOENT is only retrned if both not found and trusted.
 *
 * @note Caller MUST hold the content_lock for read
 *
 * @param[in] dir	Directory to search
 * @param[in] name	Name to find
 * @param[in] direntp	Directory entry, if found
 * @return FSAL status
 */
fsal_status_t mdcache_dirent_find(mdcache_entry_t *dir, const char *name,
				  mdcache_dir_entry_t **direntp)
{
	mdcache_dir_entry_t *dirent;

	LogFullDebug(COMPONENT_CACHE_INODE, "Find dir entry %s", name);

	*direntp = NULL;

	/* Sanity check */
	if (dir->obj_handle.type != DIRECTORY)
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	/* If no active entry, do nothing */
	if (dir->fsobj.fsdir.nbactive == 0) {
		if (mdc_dircache_trusted(dir))
			return fsalstat(ERR_FSAL_NOENT, 0);
		else
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	dirent = mdcache_avl_qp_lookup_s(dir, name, 1);
	if (!dirent) {
		if (mdc_dircache_trusted(dir))
			return fsalstat(ERR_FSAL_NOENT, 0);

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	*direntp = dirent;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *
 * @brief Adds a directory entry to a cached directory.
 *
 * This function adds a new directory entry to a directory.  Directory
 * entries have only weak references, so they do not prevent recycling
 * or freeing the entry they locate.  This function may be called
 * either once (for handling creation) or iteratively in directory
 * population.
 *
 * @note Caller MUST hold the content_lock for write
 *
 * @param[in,out] parent    Cache entry of the directory being updated
 * @param[in]     name      The name to add to the entry
 * @param[in]     entry     The cache entry associated with name
 *
 * @return FSAL status
 */

fsal_status_t
mdcache_dirent_add(mdcache_entry_t *parent, const char *name,
		   mdcache_entry_t *entry)
{
	mdcache_dir_entry_t *new_dir_entry = NULL;
	size_t namesize = strlen(name) + 1;
	int code = 0;

	LogFullDebug(COMPONENT_CACHE_INODE, "Add dir entry %s", name);

	/* Sanity check */
	if (parent->obj_handle.type != DIRECTORY)
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	/* in cache avl, we always insert on pentry_parent */
	new_dir_entry = gsh_calloc(1, sizeof(mdcache_dir_entry_t) + namesize);
	new_dir_entry->flags = DIR_ENTRY_FLAG_NONE;

	memcpy(&new_dir_entry->name, name, namesize);
	mdcache_key_dup(&new_dir_entry->ckey, &entry->fh_hk.key);

	/* add to avl */
	code = mdcache_avl_qp_insert(parent, &new_dir_entry);
	if (code < 0) {
		/* Technically only a -2 is a name collision, however, we will
		 * treat a hash collision (which per current code we should
		 * never actually see) the same.
		 */
		return fsalstat(ERR_FSAL_EXIST, 0);
	}

	/* we're going to succeed */
	parent->fsobj.fsdir.nbactive++;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Remove a cached directory entry
 *
 * @note Caller MUST hold the content_lock for write
 *
 * @param[in] parent	Parent directory
 * @param[in] name	Name to remove
 * @return FSAL status
 */
fsal_status_t
mdcache_dirent_remove(mdcache_entry_t *parent, const char *name)
{
	mdcache_dir_entry_t *dirent;
	fsal_status_t status;

	LogFullDebug(COMPONENT_CACHE_INODE, "Remove dir entry %s", name);

	status = mdcache_dirent_find(parent, name, &dirent);
	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_NOENT)
			/* Wasn't there */
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		return status;
	} else if (!dirent)
		return status;

	avl_dirent_set_deleted(parent, dirent);
	parent->fsobj.fsdir.nbactive--;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Rename a cached directory entry
 *
 * @note Caller MUST hold the content_lock for write
 *
 * @param[in] parent	Parent directory
 * @param[in] oldname	Current name of dirent
 * @param[in] newname	New name for dirent
 * @return FSAL status
 */
fsal_status_t
mdcache_dirent_rename(mdcache_entry_t *parent, const char *oldname,
		      const char *newname)

{
	mdcache_dir_entry_t *dirent, *dirent2;
	fsal_status_t status;
	int code = 0;


	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Rename dir entry %s to %s",
		     oldname, newname);

	status = mdcache_dirent_find(parent, oldname, &dirent);
	if (FSAL_IS_ERROR(status))
		return status;

	status = mdcache_dirent_find(parent, newname, &dirent2);
	if (FSAL_IS_ERROR(status) && status.major != ERR_FSAL_NOENT)
		return status;

	if (dirent2) {
		/* rename would cause a collision */
		if (parent->mde_flags & MDCACHE_TRUST_CONTENT) {
			/* overwrite, replace entry and expire the old */
			mdcache_entry_t *oldentry;

			(void)mdcache_find_keyed(&dirent2->ckey, &oldentry);

			/* dirent2 (newname) will now point to renamed entry */
			mdcache_key_delete(&dirent2->ckey);
			mdcache_key_dup(&dirent2->ckey, &dirent->ckey);

			/* Delete dirent for oldname */
			avl_dirent_set_deleted(parent, dirent);

			if (oldentry) {
				/* if it is still around, mark it gone/stale */
				atomic_clear_uint32_t_bits(
							&oldentry->mde_flags,
							MDCACHE_TRUST_ATTRS |
							MDCACHE_TRUST_CONTENT |
							MDCACHE_DIR_POPULATED);
				mdcache_put(oldentry);
			}
			return status;
		} else
			return fsalstat(ERR_FSAL_EXIST, 0);
	}

	/* Size (including terminating NULL) of the filename */
	size_t newnamesize = strlen(newname) + 1;

	/* try to rename--no longer in-place */
	dirent2 = gsh_calloc(1, sizeof(mdcache_dir_entry_t) + newnamesize);
	memcpy(dirent2->name, newname, newnamesize);
	dirent2->flags = DIR_ENTRY_FLAG_NONE;
	mdcache_key_dup(&dirent2->ckey, &dirent->ckey);

	/* Delete the entry for oldname */
	avl_dirent_set_deleted(parent, dirent);

	/* Insert the entry for newname */
	code = mdcache_avl_qp_insert(parent, &dirent2);

	/* We should not be able to have a name collision. */
	assert(code != -2);

	if (code < 0) {
		/* We had a hash collision (impossible for all practical
		 * purposes). Just abandon...
		 */
		/* dirent2 was never inserted */
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief State to be passed to FSAL readdir callbacks
 */

struct mdcache_populate_cb_state {
	struct mdcache_fsal_export *export;
	mdcache_entry_t *dir;
	fsal_status_t *status;
	uint64_t offset_cookie;
};

/**
 * @brief Populate a single dir entry
 *
 * This callback serves to populate a single dir entry from the
 * readdir.
 *
 * NOTE: Attributes are passed up from sub-FSAL, it will call
 * fsal_release_attrs, though if we do an fsal_copy_attrs(dest, src, true), any
 * references will have been transferred to the mdcache entry and the FSAL's
 * fsal_release_attrs will not really have anything to do.
 *
 * @param[in]     name       Name of the directory entry
 * @param[in]     sub_handle Object for entry
 * @param[in]     attrs      Attributes requested for the object
 * @param[in,out] dir_state  Callback state
 * @param[in]     cookie     Directory cookie
 *
 * @retval true if more entries are requested
 * @retval false if no more should be sent and the last was not processed
 */

static bool
mdc_populate_dirent(const char *name, struct fsal_obj_handle *sub_handle,
		    struct attrlist *attrs, void *dir_state,
		    fsal_cookie_t cookie)
{
	struct mdcache_populate_cb_state *state = dir_state;
	fsal_status_t status = { 0, 0 };
	mdcache_entry_t *directory = container_of(&state->dir->obj_handle,
						  mdcache_entry_t, obj_handle);

	/* This is in the middle of a subcall. Do a supercall */
	supercall_raw(state->export,
		status = mdc_add_cache(directory, name, sub_handle, attrs)
	);

	if (FSAL_IS_ERROR(status)) {
		*state->status = status;
		if (status.major == ERR_FSAL_XDEV) {
			LogInfo(COMPONENT_NFS_READDIR,
				"Ignoring XDEV entry %s", name);
			*state->status = fsalstat(ERR_FSAL_NO_ERROR, 0);
			return true;
		}
		LogInfo(COMPONENT_CACHE_INODE,
			"Lookup failed on %s in dir %p with %s",
			name, directory, fsal_err_txt(*state->status));
		return false;
	}

	return true;
}

/**
 *
 * @brief Cache complete directory contents
 *
 * This function reads a complete directory from the FSAL and caches
 * both the names and files.  The content lock must be held on the
 * directory being read.
 *
 * @note dir MUST have it's content_lock held for writing
 *
 * @param[in] dir  Entry for the parent directory to be read
 *
 * @return FSAL status
 */

fsal_status_t
mdcache_dirent_populate(mdcache_entry_t *dir)
{
	fsal_status_t fsal_status;
	fsal_status_t status = {0, 0};
	bool eod = false;
	attrmask_t attrmask;

	struct mdcache_populate_cb_state state;

	/* Only DIRECTORY entries are concerned */
	if (dir->obj_handle.type != DIRECTORY) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "CACHE_INODE_NOT_A_DIRECTORY");
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	if ((dir->mde_flags & MDCACHE_DIR_POPULATED)
	    && (dir->mde_flags & MDCACHE_TRUST_CONTENT)) {
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "MDCACHE_DIR_POPULATED and MDCACHE_TRUST_CONTENT");
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* Invalidate all the dirents */
	mdcache_dirent_invalidate_all(dir);

	state.export = mdc_cur_export();
	state.dir = dir;
	state.status = &status;
	state.offset_cookie = 0;

	attrmask = op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) | ATTR_RDATTR_ERR;

	subcall_raw(state.export,
		fsal_status = dir->sub_handle->obj_ops.readdir(
			dir->sub_handle, NULL, (void *)&state,
			mdc_populate_dirent, attrmask, &eod)
	       );
	if (FSAL_IS_ERROR(fsal_status)) {

		LogDebug(COMPONENT_NFS_READDIR, "FSAL readdir status=%s",
			 fsal_err_txt(fsal_status));
		return fsal_status;
	}

	/* we were supposed to read to the end.... */
	if (!eod && mdcache_param.retry_readdir) {
		LogInfo(COMPONENT_NFS_READDIR,
			"Readdir didn't reach eod on dir %p (status %s)",
			&dir->sub_handle, fsal_err_txt(status));
		return fsalstat(ERR_FSAL_DELAY, 0);
	} else if (eod) {
		/* End of work */
		atomic_set_uint32_t_bits(&dir->mde_flags,
					 MDCACHE_DIR_POPULATED);

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* If !eod (and fsal_status isn't an error), then the only error path
	 * is through a callback failure and status has been set the
	 * mdc_populate_dirent callback */

	return status;
}

/**
 * @brief Forcibly remove an entry from the cache (top half)
 *
 * This function is used to invalidate a cache entry when it
 * has become unusable (for example, when the FSAL declares it to be
 * stale).
 *
 * To simplify interaction with the SAL, this function no longer
 * finalizes the entry, but schedules the entry for out-of-line
 * cleanup, after first making it unreachable.
 *
 * @param[in] entry The entry to be killed
 */

void
_mdcache_kill_entry(mdcache_entry_t *entry,
		    char *file, int line, char *function)
{
	bool freed;

	if (isDebug(COMPONENT_CACHE_INODE)) {
		DisplayLogComponentLevel(COMPONENT_CACHE_INODE,
					 file, line, function, NIV_DEBUG,
					 "Kill %s entry %p obj_handle %p",
					 object_file_type_to_str(
							entry->obj_handle.type),
					 entry, &entry->obj_handle);
	}

	freed = cih_remove_checked(entry); /* !reachable, drop sentinel ref */

	if (!freed) {
		/* queue for cleanup */
		mdcache_lru_cleanup_push(entry);
	}

}

/** @} */
