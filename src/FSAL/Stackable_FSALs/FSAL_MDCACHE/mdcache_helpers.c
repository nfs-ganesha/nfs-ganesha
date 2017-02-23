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
		if (attrs_out->request_mask & ATTR_RDATTR_ERR) {
			/* Indicate the failure of requesting attributes by
			 * marking the ATTR_RDATTR_ERR in the mask.
			 */
			attrs_out->valid_mask = ATTR_RDATTR_ERR;
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

void
mdc_get_parent(struct mdcache_fsal_export *export, mdcache_entry_t *entry)
{
	struct fsal_obj_handle *sub_handle;
	struct gsh_buffdesc fh_desc;
	fsal_status_t status;

	if (entry->obj_handle.type != DIRECTORY) {
		/* Parent pointer only for directories */
		return;
	}

	if (entry->fsobj.fsdir.parent.kv.len != 0) {
		/* Already has a parent pointer */
		return;
	}

	subcall_raw(export,
		status = entry->sub_handle->obj_ops.lookup(
			    entry->sub_handle, "..", &sub_handle, NULL)
	       );

	if (FSAL_IS_ERROR(status)) {
		/* Top of filesystem */
		return;
	}

	subcall_raw(export,
		    sub_handle->obj_ops.handle_to_key(sub_handle, &fh_desc);
		   );

	cih_hash_key(&entry->fsobj.fsdir.parent,
		     export->export.sub_export->fsal, &fh_desc, CIH_HASH_NONE);

	/* Release parent handle */
	subcall_raw(export,
		    sub_handle->obj_ops.release(sub_handle)
		   );
}

/**
 * @brief Cleans all the dirents belonging to a directory chunk.
 *
 * @note The content lock MUST be held for write
 *
 * @param[in,out] chunk  The chunk being cleaned.
 *
 */

void mdcache_clean_dirent_chunk(struct dir_chunk *chunk)
{
	struct glist_head *glist, *glistn;
	struct mdcache_fsal_obj_handle *parent = chunk->parent;

	glist_for_each_safe(glist, glistn, &chunk->dirents) {
		mdcache_dir_entry_t *dirent;

		dirent = glist_entry(glist, mdcache_dir_entry_t, chunk_list);

		unchunk_dirent(dirent);

		if (dirent->flags & DIR_ENTRY_FLAG_DELETED) {
			/* Remove from deleted names tree */
			avltree_remove(&dirent->node_hk,
				       &parent->fsobj.fsdir.avl.c);
		} else {
			/* Remove from active names tree */
			avltree_remove(&dirent->node_hk,
				       &parent->fsobj.fsdir.avl.t);
		}

		if (dirent->ckey.kv.len)
			mdcache_key_delete(&dirent->ckey);
		gsh_free(dirent);

		/* Don't count this dirent anymore. */
		parent->fsobj.fsdir.nbactive--;
	}

	/* Remove chunk from directory and free it */
	glist_del(&chunk->chunks);
	gsh_free(chunk);
}

/**
 * @brief Cleans all the dirent chunks belonging to a directory.
 *
 * @note The content lock MUST be held for write
 *
 * @param[in,out] emtry  The directory being cleaned.
 *
 */

void mdcache_clean_dirent_chunks(mdcache_entry_t *entry)
{
	struct glist_head *glist, *glistn;

	glist_for_each_safe(glist, glistn, &entry->fsobj.fsdir.chunks) {
		mdcache_clean_dirent_chunk(glist_entry(glist,
						       struct dir_chunk,
						       chunks));
	}
}

/**
 * @brief Invalidates and releases all cached entries for a directory
 *
 * Invalidates all the entries for a cached directory.
 *
 * @note The content lock MUST be held for write
 *
 * @param[in,out] entry  The directory to be managed
 *
 */

void mdcache_dirent_invalidate_all(mdcache_entry_t *entry)
{
	LogFullDebug(COMPONENT_CACHE_INODE, "Invalidating directory for %p",
		     entry);

	/* Clean the chunks first, that will clean most of the active
	 * entries also.
	 */
	mdcache_clean_dirent_chunks(entry);

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

		/* init chunk list */
		glist_init(&nentry->fsobj.fsdir.chunks);
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

	/* Use the attrs_in request_mask because it will know if ACL was
	 * requested or not (anyone calling mdcache_new_entry will have
	 * requested all supported attributes including ACL).
	 */
	nentry->attrs.request_mask = attrs_in->request_mask;
	fsal_copy_attrs(&nentry->attrs, attrs_in, true);

	if (nentry->attrs.expire_time_attr == 0) {
		nentry->attrs.expire_time_attr =
		    atomic_fetch_uint32_t(
			    &op_ctx->ctx_export->expire_time_attr);
	}

	/* Validate the attributes we just set. */
	mdc_fixup_md(nentry, &nentry->attrs);

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

	if (isFullDebug(COMPONENT_CACHE_INODE)) {
		char str[LOG_BUFF_LEN];
		struct display_buffer dspbuf = {sizeof(str), str, str };

		(void) display_mdcache_key(&dspbuf, &nentry->fh_hk.key);

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "New entry %p added with fh_hk.key %s",
			     nentry, str);
	} else {
		LogDebug(COMPONENT_CACHE_INODE, "New entry %p added", nentry);
	}
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
		mdcache_put(nentry);
		mdcache_kill_entry(nentry);
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
		fsal_status_t cstatus = sub_handle->obj_ops.close2(sub_handle,
								   state);

		LogDebug(COMPONENT_CACHE_INODE,
			 "Close of state during error processing returned %s",
			 fsal_err_txt(cstatus));
	}

	/* must free sub_handle if no new entry was created to reference it. */
	sub_handle->obj_ops.release(sub_handle);

	return status;
}

int display_mdcache_key(struct display_buffer *dspbuf, mdcache_key_t *key)
{
	int b_left = display_printf(dspbuf, "hk=%"PRIx64" fsal=%p key=",
				    key->hk, key->fsal);

	if (b_left <= 0)
		return b_left;

	return display_opaque_bytes(dspbuf, key->kv.addr, key->kv.len);
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

	if (isFullDebug(COMPONENT_CACHE_INODE)) {
		char str[LOG_BUFF_LEN];
		struct display_buffer dspbuf = { sizeof(str), str, str };

		(void) display_mdcache_key(&dspbuf, key);

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Looking for %s", str);
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
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Found entry %p, but could not ref error %s",
				     entry, fsal_err_txt(status));

			*entry = NULL;
			return status;
		}

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Found entry %p",
			     entry);

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

	/* Ask for all supported attributes except ACL (we defer fetching ACL
	 * until asked for it (including a permission check).
	 */
	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.
				   fs_supported_attrs(op_ctx->fsal_export)
				   & ~ATTR_ACL);

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
 * @note Currently this function is only used when caching entire directories.
 *
 * @param[in]     mdc_parent  Parent entry
 * @param[in]     name        Name of new entry
 * @param[in]     sub_handle  Handle from sub-FSAL for new entry
 * @param[in]     attrs_in    Attributes for new entry
 *
 * @return FSAL status (ERR_FSAL_OVERFLOW if dircache full)
 */

fsal_status_t mdc_add_cache(mdcache_entry_t *mdc_parent,
			    const char *name,
			    struct fsal_obj_handle *sub_handle,
			    struct attrlist *attrs_in)
{
	struct mdcache_fsal_export *export = mdc_cur_export();
	fsal_status_t status;
	mdcache_entry_t *new_entry = NULL;
	bool invalidate = false;

	if (avltree_size(&mdc_parent->fsobj.fsdir.avl.t) >
	    mdcache_param.dir.avl_max) {
		LogFullDebug(COMPONENT_CACHE_INODE, "Parent %p at max",
			     mdc_parent);
		return fsalstat(ERR_FSAL_OVERFLOW, 0);
	}

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
	status = mdcache_dirent_add(mdc_parent, name, new_entry, &invalidate);

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

	/* If parent isn't caching, return stale */
	if (mdc_parent->mde_flags & MDCACHE_BYPASS_DIRCACHE)
		return fsalstat(ERR_FSAL_STALE, 0);

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
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
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

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Lookup parent (..) of %p", mdc_parent);
		/* ".." doesn't end up in the cache */
		status =  mdcache_locate_keyed(&mdc_parent->fsobj.fsdir.parent,
					       export, new_entry, attrs_out);
		goto out;
	}

	if (mdc_parent->mde_flags & MDCACHE_BYPASS_DIRCACHE) {
		/* Parent isn't caching dirents; call directly */
		goto uncached;
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

uncached:
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
	bool invalidate = false;

	/* Ask for all supported attributes except ACL (we defer fetching ACL
	 * until asked for it (including a permission check).
	 */
	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.
				   fs_supported_attrs(op_ctx->fsal_export)
				   & ~ATTR_ACL);

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

	/* We are only called to fill cache, we should not need to invalidate
	 * parents attributes (or dirents if chunked).
	 *
	 * NOTE: This does mean that a pure lookup of a file that had been added
	 *       external to this Ganesha instance could cause us to not dump
	 *       the dirent cache, however, that should still result in an
	 *       attribute change which should dump the cache.
	 */
	status = mdcache_alloc_and_check_handle(export, sub_handle, &new_obj,
						false, &attrs, attrs_out,
						"lookup ", mdc_parent, name,
						&invalidate, NULL);

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
 * @param[in,out] parent      Cache entry of the directory being updated
 * @param[in]     name        The name to add to the entry
 * @param[in]     entry       The cache entry associated with name
 * @param[in,out] invalidate  Invalidate the parent directory contents if
 *                            adding to a chunk fails, if adding to a chunk
 *                            succeeds, invalidate will be reset to false
 *                            and the caller MUST refresh the attributes
 *                            without invalidating the dirent cache.
 *
 * @return FSAL status
 */

fsal_status_t
mdcache_dirent_add(mdcache_entry_t *parent, const char *name,
		   mdcache_entry_t *entry, bool *invalidate)
{
	mdcache_dir_entry_t *new_dir_entry, *allocated_dir_entry;
	size_t namesize = strlen(name) + 1;
	int code = 0;

	LogFullDebug(COMPONENT_CACHE_INODE, "Add dir entry %s", name);

	/* Sanity check */
	if (parent->obj_handle.type != DIRECTORY)
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	/* Don't cache if parent is not being cached */
	if (parent->mde_flags & MDCACHE_BYPASS_DIRCACHE)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	/* in cache avl, we always insert on pentry_parent */
	new_dir_entry = gsh_calloc(1, sizeof(mdcache_dir_entry_t) + namesize);
	new_dir_entry->flags = DIR_ENTRY_FLAG_NONE;
	allocated_dir_entry = new_dir_entry;

	memcpy(&new_dir_entry->name, name, namesize);
	mdcache_key_dup(&new_dir_entry->ckey, &entry->fh_hk.key);

	/* add to avl */
	code = mdcache_avl_qp_insert(parent, &new_dir_entry);
	if (code < 0) {
		/* Technically only a -2 is a name collision, however, we will
		 * treat a hash collision (which per current code we should
		 * never actually see) the same.
		 */
		LogDebug(COMPONENT_CACHE_INODE,
			 "Returning EEXIST for %s code %d", name, code);
		return fsalstat(ERR_FSAL_EXIST, 0);
	}

	/* we're going to succeed */
	if (new_dir_entry == allocated_dir_entry) {
		/* We only want to count this entry if we did indeed add a new
		 * one.
		 */
		parent->fsobj.fsdir.nbactive++;

		if (mdcache_param.dir.avl_chunk > 0) {
			/* If chunking, try and add this entry to a chunk. */
			bool chunked = add_dirent_to_chunk(parent,
							   new_dir_entry);

			if (!chunked && *invalidate) {
				/* If chunking and invalidating parent, and
				 * chunking this entry failed, invalidate
				 * parent.
				 */
				mdcache_dirent_invalidate_all(parent);
			} else if (chunked && *invalidate) {
				/* We succeeded in adding to chunk, don't
				 * invalidate the parent directory.
				 */
				*invalidate = false;
			}
		}
	}

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

	/* Don't remove if parent is not being cached */
	if (parent->mde_flags & MDCACHE_BYPASS_DIRCACHE)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

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

	/* Don't rename if parent is not being cached */
	if (parent->mde_flags & MDCACHE_BYPASS_DIRCACHE)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	/* Don't rename if chunking. */
	if (mdcache_param.dir.avl_chunk > 0) {
		/* Dump the dirent cache for this directory. */
		mdcache_dirent_invalidate_all(parent);
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	status = mdcache_dirent_find(parent, oldname, &dirent);
	if (FSAL_IS_ERROR(status))
		return status;
	if (!dirent)
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
		} else {
			LogDebug(COMPONENT_CACHE_INODE,
				 "Returning EEXIST for %s", newname);
			return fsalstat(ERR_FSAL_EXIST, 0);
		}
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
	fsal_readdir_cb cb;
	void *dir_state;
};

/**
 * @brief Handle a readdir callback on uncache dir
 *
 * Cache a sindle object, passing it up the stack to the caller.  This is for
 * handling readdir on a directory that is not being cached, for example because
 * is is too big.  Dirents are not created by this callback, just objects.
 *
 * @param[in]     name       Name of the directory entry
 * @param[in]     sub_handle Object for entry
 * @param[in]     attrs      Attributes requested for the object
 * @param[in,out] dir_state  Callback state
 * @param[in]     cookie     Directory cookie
 *
 * @returns fsal_dir_result
 */

static enum fsal_dir_result
mdc_readdir_uncached_cb(const char *name, struct fsal_obj_handle *sub_handle,
			struct attrlist *attrs, void *dir_state,
			fsal_cookie_t cookie, fsal_cookie_t *ret_cookie)
{
	struct mdcache_populate_cb_state *state = dir_state;
	fsal_status_t status = { 0, 0 };
	mdcache_entry_t *directory = container_of(&state->dir->obj_handle,
						  mdcache_entry_t, obj_handle);
	mdcache_entry_t *new_entry = NULL;
	enum fsal_dir_result rv;

	/* This is in the middle of a subcall. Do a supercall */
	supercall_raw(state->export,
		status = mdcache_new_entry(state->export, sub_handle, attrs,
					   NULL, false, &new_entry, NULL)
	);

	if (FSAL_IS_ERROR(status)) {
		*state->status = status;
		if (status.major == ERR_FSAL_XDEV) {
			LogInfo(COMPONENT_NFS_READDIR,
				"Ignoring XDEV entry %s", name);
			*state->status = fsalstat(ERR_FSAL_NO_ERROR, 0);
			return DIR_CONTINUE;
		}
		LogInfo(COMPONENT_CACHE_INODE,
			"Lookup failed on %s in dir %p with %s",
			name, directory, fsal_err_txt(*state->status));
		return DIR_TERMINATE;
	}

	/* Call up the stack.  Do a supercall */
	supercall_raw(state->export,
		      rv = state->cb(name, &new_entry->obj_handle, attrs,
				     state->dir_state, cookie, ret_cookie)
	);

	return rv;
}

/**
 * Perform an uncached readdir
 *
 * Large directories do not have their dirents cached.  This performs readdir on
 * such directories, by passing the sub-FSAL's results back up through the
 * stack.
 *
 * @note The object passed into the callback is ref'd and must be unref'd by the
 * callback.
 *
 * @param[in] directory the directory to read
 * @param[in] whence where to start (next)
 * @param[in] dir_state pass thru of state to callback
 * @param[in] cb callback function
 * @param[in] attrmask Which attributes to fill
 * @param[out] eod_met eod marker true == end of dir
 *
 * @return FSAL status
 */
fsal_status_t
mdcache_readdir_uncached(mdcache_entry_t *directory, fsal_cookie_t *whence,
			 void *dir_state, fsal_readdir_cb cb,
			 attrmask_t attrmask, bool *eod_met)
{
	fsal_status_t status = {0, 0};
	fsal_status_t readdir_status = {0, 0};
	struct mdcache_populate_cb_state state;

	state.export = mdc_cur_export();
	state.dir = directory;
	state.status = &status;
	state.cb = cb;
	state.dir_state = dir_state;

	subcall(
		readdir_status = directory->sub_handle->obj_ops.readdir(
			directory->sub_handle, whence, &state,
			mdc_readdir_uncached_cb, attrmask, eod_met)
	       );

	if (FSAL_IS_ERROR(readdir_status))
		return readdir_status;

	return status;
}

/**
 * @brief Add a dirent from create, lookup, or rename to a chunk if possible.
 *
 * If addition is not possible because the entry does not belong to an
 * active dirent chunk, nothing happens. The dirent may still be inserted
 * into the by name lookup.
 *
 * @param[in] parent_dir     The directory this dir entry is part of
 * @param[in] new_dir_entry  The dirent to add.
 *
 * @returns true if successefull
 *
 */
bool add_dirent_to_chunk(mdcache_entry_t *parent_dir,
			 mdcache_dir_entry_t *new_dir_entry)
{
	mdcache_dir_entry_t *left;
	mdcache_dir_entry_t *right;
	struct avltree_node *node, *parent, *unbalanced, *other;
	int is_left, code;
	fsal_cookie_t ck, nck;

	subcall(
		ck = parent_dir->sub_handle->obj_ops.compute_readdir_cookie(
				parent_dir->sub_handle, new_dir_entry->name)
	       );

	if (ck == 0) {
		/* FSAL does not support computing readdir cookie, so we can't
		 * add this entry to a chunk.
		 */
		LogFullDebug(COMPONENT_CACHE_INODE,
			      "Could not add %s to chunk for directory %p, compute_readdir_cookie failed",
			      new_dir_entry->name, parent_dir);
		return false;
	}

	new_dir_entry->ck = ck;

	node = avltree_do_lookup(&new_dir_entry->node_sorted,
				 &parent_dir->fsobj.fsdir.avl.sorted,
				 &parent, &unbalanced, &is_left,
				 avl_dirent_sorted_cmpf);

	if (isFullDebug(COMPONENT_CACHE_INODE)) {
		if (node) {
			right = avltree_container_of(node, mdcache_dir_entry_t,
						     node_sorted);
		}

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "avltree_do_lookup returned node=%p (name=%s, ck=%"
			     PRIx64") parent=%p unbalanced=%p is_left=%s",
			     node,
			     node ? right->name : "",
			     node ? right->ck : 0,
			     parent, unbalanced, is_left ? "true" : "false");
	}

	if (node) {
		right = avltree_container_of(node, mdcache_dir_entry_t,
					     node_sorted);

		if (ck == FIRST_COOKIE && right->ck == FIRST_COOKIE) {
			/* Special case of inserting a new first entry.
			 * We should only have to do this for FSALs that
			 * sort dirents by cookie value that support
			 * compute_readdir_cookie and are unable to actually
			 * compute the cookie for the very first directory
			 * entry.
			 */
			subcall(
				nck = parent_dir->sub_handle
					->obj_ops.compute_readdir_cookie(
						parent_dir->sub_handle,
						right->name)
			       );

			if (nck == 0) {
				/* Coops, could not compute new cookie... */
				LogCrit(COMPONENT_CACHE_INODE,
					"Could not compute new cookie for %s in directory %p",
					right->name, parent_dir);
				return false;
			}

			/* Just change up the old first entries cookie, which
			 * will leave room to insert the new entry with cookie
			 * of FIRST_COOKIE.
			 */
			right->ck = nck;
		} else {
			/* This should not happen... */
			LogCrit(COMPONENT_CACHE_INODE,
				"Could not add %s to chunk for directory %p, node %s found withck=%"
				PRIx64,
				new_dir_entry->name, parent_dir,
				right->name, right->ck);
			return false;
		}
	}

	if (parent == NULL) {
		/* The tree must be empty, there are no chunks to add this
		 * entry to.
		 */
		LogFullDebug(COMPONENT_CACHE_INODE,
			      "Could not add %s to chunk for directory %p, tree was empty",
			      new_dir_entry->name, parent_dir);
		return false;
	}

	if (is_left) {
		/* Parent will be to the right of the key. */
		right = avltree_container_of(parent, mdcache_dir_entry_t,
					     node_sorted);
		other = avltree_prev(parent);
		if (other) {
			left = avltree_container_of(other, mdcache_dir_entry_t,
						    node_sorted);
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "%s is between %s and parent %s",
				     new_dir_entry->name,
				     left->name, right->name);
		} else {
			left = NULL;

			if (parent_dir->fsobj.fsdir.first_ck == right->ck) {
				/* The right node is the first entry in the
				 * directory. Add this key to the beginning of
				 * the first chunk and fixup the chunk.
				 */
				LogFullDebug(COMPONENT_CACHE_INODE,
					     "Adding %s as new first entry",
					     new_dir_entry->name);
			} else {
				/* The right entry is not the first entry in
				 * the directory, so the key is a dirent
				 * somewhere before the first chunked dirent.
				 * we can't insert this key into a chunk.
				 */
				LogFullDebug(COMPONENT_CACHE_INODE,
					      "Could not add %s to chunk for directory %p, somewhere before first chunk",
					      new_dir_entry->name, parent_dir);
				return false;
			}
		}
	} else {
		/* Parent will be to the left of the key. */
		left = avltree_container_of(parent, mdcache_dir_entry_t,
					    node_sorted);
		other = avltree_next(parent);
		if (other) {
			right = avltree_container_of(other, mdcache_dir_entry_t,
						     node_sorted);
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "%s is between parent %s and %s",
				     new_dir_entry->name,
				     left->name, right->name);
		} else {
			right = NULL;

			if (left->eod) {
				/* The right node is the last entry in the
				 * directory. Add this key to the end of the
				 * last chunk and fixup the chunk.
				 */
				LogFullDebug(COMPONENT_CACHE_INODE,
					     "Adding %s as new last entry",
					     new_dir_entry->name);
			} else {
				/* The left entry is not the last entry in
				 * the directory, so the key is a dirent
				 * somewhere after the last chunked dirent.
				 * we can't insert this key into a chunk.
				 */
				LogFullDebug(COMPONENT_CACHE_INODE,
					      "Could not add %s to chunk for directory %p, somewhere after last chunk",
					      new_dir_entry->name, parent_dir);
				return false;
			}
		}
	}

	/* Note in the following, every dirent that is in the sorted tree MUST
	 * be in a chunk, so we don't check for chunk != NULL.
	 */
	if (left != NULL && right != NULL &&
	    left->chunk != right->chunk &&
	    left->chunk != right->chunk->prev_chunk) {
		/* left and right are in different non-adjacent chunks.
		 */
		return false;
	}

	/* Set up to add to chunk and by cookie AVL tree. */
	if (right == NULL) {
		/* Will go at end of left chunk. */
		new_dir_entry->chunk = left->chunk;
	} else {
		/* Will go at begin of right chunk. */
		new_dir_entry->chunk = right->chunk;
	}

	code = mdcache_avl_insert_ck(parent_dir, new_dir_entry);

	if (code < 0) {
		/* We failed to insert into FSAL cookie AVL tree, will fail.
		 * Nothing to clean up since we haven't done anything
		 * unreversible.
		 */
		return false;
	}

	/* Get the node into the actual tree... */
	avltree_do_insert(&new_dir_entry->node_sorted,
			  &parent_dir->fsobj.fsdir.avl.sorted,
			  parent, unbalanced, is_left);

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Inserted %s into sorted tree left=%p right=%p",
		     new_dir_entry->name, new_dir_entry->node_sorted.left,
		     new_dir_entry->node_sorted.right);

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Adding %s to chunk %p between %s and %s for directory %p",
		     new_dir_entry->name,
		     right ? right->chunk : left->chunk,
		     left ? left->name : "BEGIN",
		     right ? right->name : "END",
		     parent_dir);

	/* And now add it to the chunk */
	if (right == NULL) {
		/* Insert node at END of the chunk represented by left. */
		glist_add_tail(&left->chunk->dirents,
			       &new_dir_entry->chunk_list);

		/* Make the new entry the eod entry. */
		new_dir_entry->eod = true;
		left->eod = false;
	} else {
		/* Insert to left of right, which if left and right are
		 * different chunks, inserts into the right hand chunk.
		 *
		 * NOTE: This looks weird, normally we pass the list head to
		 *       glist_add_tail, but glist_add_tail really just
		 *       inserts the entry before the first parameter, recall
		 *       that the list head is just a member of the list...
		 *
		 * If left == NULL, then the "list node" to the left of
		 * right is the actual list head, and this all works out...
		 */
		glist_add_tail(&right->chunk_list, &new_dir_entry->chunk_list);

		if (left != NULL) {
			/* Fixup left chunk's next cookie */
			left->chunk->next_ck = new_dir_entry->ck;
		} else {
			/* New first entry in directory */
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Setting directory first_ck=%"PRIx64,
				     new_dir_entry->ck);
			parent_dir->fsobj.fsdir.first_ck = new_dir_entry->ck;
		}

		/** @todo FSF - split chunk if desired... */
	}

	/* And now increment the number of entries in the chunk. */
	new_dir_entry->chunk->num_entries++;

	new_dir_entry->flags |= DIR_ENTRY_SORTED;

	return true;
}

/**
 * @brief Handle adding an element to a dirent chunk
 *
 * Cache a sindle object, and add it to the directory chunk in progress.
 *
 * @param[in]     name       Name of the directory entry
 * @param[in]     sub_handle Object for entry
 * @param[in]     attrs      Attributes requested for the object
 * @param[in,out] dir_state  Callback state
 * @param[in]     cookie     Directory cookie
 *
 * @returns fsal_dir_result
 */

static enum fsal_dir_result
mdc_readdir_chunk_object(const char *name, struct fsal_obj_handle *sub_handle,
			 struct attrlist *attrs_in, void *dir_state,
			 fsal_cookie_t cookie, fsal_cookie_t *ret_cookie)
{
	struct mdcache_populate_cb_state *state = dir_state;
	struct dir_chunk *chunk = state->dir_state;
	mdcache_entry_t *mdc_parent = container_of(&state->dir->obj_handle,
						   mdcache_entry_t, obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	mdcache_entry_t *new_entry = NULL;
	mdcache_dir_entry_t *new_dir_entry = NULL, *allocated_dir_entry = NULL;
	size_t namesize = strlen(name) + 1;
	int code = 0;
	fsal_status_t status;
	enum fsal_dir_result result = DIR_CONTINUE;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Creating cache entry for %s cookie=0x%"PRIx64
		     " sub_handle=0x%p",
		     name, cookie, sub_handle);

	status = mdcache_new_entry(export, sub_handle, attrs_in, NULL,
				   false, &new_entry, NULL);

	if (FSAL_IS_ERROR(status)) {
		*state->status = status;
		LogInfo(COMPONENT_CACHE_INODE,
			"mdcache_new_entry failed on %s in dir %p with %s",
			name, mdc_parent, fsal_err_txt(status));
		return DIR_TERMINATE;
	}

	/* Entry was found in the FSAL, add this entry to the parent directory
	 */

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Add mdcache entry %p for %s for FSAL %s",
		     new_entry, name, new_entry->sub_handle->fsal->name);

	/* in cache avl, we always insert on mdc_parent */
	new_dir_entry = gsh_calloc(1, sizeof(mdcache_dir_entry_t) + namesize);
	new_dir_entry->flags = DIR_ENTRY_FLAG_NONE;
	new_dir_entry->chunk = chunk;
	new_dir_entry->ck = cookie;
	allocated_dir_entry = new_dir_entry;

	/** @todo FSF - we could eventually try and support duplicated FSAL
	 *              cookies assuming they come sequentially (which they
	 *              would from EXT4 as far as I can tell from the EXT4
	 *              code). We could never start a chunk with a duplicate
	 *              so we would have to put all of them into the same
	 *              chunk, posssibly making the chunk larger than normal.
	 */

	memcpy(&new_dir_entry->name, name, namesize);
	mdcache_key_dup(&new_dir_entry->ckey, &new_entry->fh_hk.key);

	/* add to avl */
	code = mdcache_avl_qp_insert(mdc_parent, &new_dir_entry);

	if (code < 0) {
		/* We can get here with the following possibilities:
		 *
		 * - FSAL cookie collision, nothing we can do about this, but
		 *   also really should never happen.
		 * - Name collision, something is broken and the FSAL has
		 *   given us multiple directory entries with the same name
		 *   but for different objects. Again, not much we can do.
		 * - Degenerate name hash collision, we have tried many many
		 *   times to find a workable hash for the name and failed.
		 *   Due to the number of retries we should never get here.
		 *
		 * In any case, we will just ignore this entry.
		 */
		/* Technically only a -2 is a name collision, however, we will
		 * treat a hash collision (which per current code we should
		 * never actually see) the same.
		 */
		LogCrit(COMPONENT_CACHE_INODE,
			"Collision while adding dirent for %s", name);
		mdcache_put(new_entry);
		return DIR_CONTINUE;
	}

	/* Note that if this dirent was already in the lookup by name AVL
	 * tree (mdc_parent->fsobj.fsdir.avl.t), then mdcache_avl_qp_insert
	 * freed the dirent we allocated above, and returned the one that was
	 * in tree. It will have set chunk, ck, and nk.
	 *
	 * The existing dirent might or might not be part of a chunk already.
	 */

	if (new_dir_entry == allocated_dir_entry) {
		/* We didn't have a swaparoo, so go ahead count it against this
		 * directorie's max number of active dirents, otherwise we don't
		 * want to double count entries that were in the dirent cache
		 * already due to being put there by create or lookup.
		 */
		mdc_parent->fsobj.fsdir.nbactive++;
	} else {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Swapped using %p instead of %p, new_dir_entry->chunk=%p",
			     new_dir_entry, allocated_dir_entry,
			     new_dir_entry->chunk);
	}

	assert(new_dir_entry->chunk);

	if (op_ctx->fsal_export->exp_ops.fs_supports(
			op_ctx->fsal_export, fso_compute_readdir_cookie)) {
		struct avltree_node *node;

		node = avltree_inline_insert(
					&new_dir_entry->node_sorted,
					&mdc_parent->fsobj.fsdir.avl.sorted,
					avl_dirent_sorted_cmpf);

		if (node != NULL) {
			if (node == &new_dir_entry->node_sorted) {
				LogDebug(COMPONENT_CACHE_INODE,
					 "New entry %s was already in sorted tree",
					 name);
			} else if (isDebug(COMPONENT_CACHE_INODE)) {
				mdcache_dir_entry_t *other;

				other = avltree_container_of(
					node, mdcache_dir_entry_t, node_sorted);
				LogDebug(COMPONENT_CACHE_INODE,
					 "New entry %s collided with entry %s already in sorted tree",
					 name, other->name);
			}
		} else {
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Inserted %s into sorted tree left=%p right=%p",
				     name, new_dir_entry->node_sorted.left,
				     new_dir_entry->node_sorted.right);

			new_dir_entry->flags |= DIR_ENTRY_SORTED;
		}
	}

	/* Add this dirent to the chunk if not already added. */
	if (glist_null(&new_dir_entry->chunk_list)) {
		/* If this dirent is not already on a chunk_list, then we add
		 * it. It could be the allocated_dir_entry or it could be an
		 * old dirent that was not part of a chunk, but it is NOT the
		 * same dirent that was already part of some other chunk.
		 */
		glist_add_tail(&chunk->dirents, &new_dir_entry->chunk_list);

		if (chunk->num_entries == 0 && chunk->prev_chunk != NULL) {
			/* Link the first dirent in a new chunk to the previous
			 * chunk so linkage across chunks works.
			 */
			chunk->prev_chunk->next_ck = cookie;
		}
		chunk->num_entries++;
	}

	if (chunk->num_entries == mdcache_param.dir.avl_chunk ||
	    new_dir_entry->chunk != chunk) {
		/* Chunk is full or we have the situation where we have collided
		 * with a previously used chunk (and thus we have a partial
		 * chunk). Since dirent is pointing to the existing dirent and
		 * the one we allocated above has been freed we don't need to do
		 * any cleanup.
		 */
		if (ret_cookie != NULL) {
			/* Caller cares about marking cookies. */
			new_dir_entry->flags |= DIR_ENTRY_COOKIE_MARKED;
			result = DIR_TERMINATE_MARK;
		} else {
			/* Just indicate this directory is terminated. */
			result = DIR_TERMINATE;
		}
	}

	if (new_entry->obj_handle.type == DIRECTORY) {
		/* Insert Parent's key */
		mdc_dir_add_parent(new_entry, mdc_parent);
	}

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "About to put entry %p refcnt=%"PRIi32,
		     new_entry,
		     atomic_fetch_int32_t(&new_entry->lru.refcnt));

	mdcache_put(new_entry);

	return result;
}

/**
 * @brief Handle a readdir callback for a chunked directory.
 *
 * This is a supercall wrapper around the function above that actually does
 * the work.
 *
 * @param[in]     name       Name of the directory entry
 * @param[in]     sub_handle Object for entry
 * @param[in]     attrs      Attributes requested for the object
 * @param[in,out] dir_state  Callback state
 * @param[in]     cookie     Directory cookie
 *
 * @returns fsal_dir_result
 */

static enum fsal_dir_result
mdc_readdir_chunked_cb(const char *name, struct fsal_obj_handle *sub_handle,
		       struct attrlist *attrs, void *dir_state,
		       fsal_cookie_t cookie, fsal_cookie_t *ret_cookie)
{
	struct mdcache_populate_cb_state *state = dir_state;
	enum fsal_dir_result result;

	/* This is in the middle of a subcall. Do a supercall */
	supercall_raw(state->export,
		result = mdc_readdir_chunk_object(name, sub_handle, attrs,
						  dir_state, cookie,
						  ret_cookie)
	);

	return result;
}

/**
 * @brief Read the next chunk of a directory
 *
 * @param[in] directory   The directory to read
 * @param[in] whence      Where to start (next)
 * @param[in,out] dirent  The first dirent of the chunk
 *
 * @return FSAL status
 */

fsal_status_t mdcache_populate_dir_chunk(mdcache_entry_t *directory,
					 fsal_cookie_t whence,
					 mdcache_dir_entry_t **dirent,
					 struct dir_chunk *prev_chunk)
{
	fsal_status_t status = {0, 0};
	fsal_status_t readdir_status = {0, 0};
	struct mdcache_populate_cb_state state;
	struct dir_chunk *chunk = gsh_calloc(1, sizeof(struct dir_chunk));
	attrmask_t attrmask;
	bool eod = false;

	attrmask = op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) | ATTR_RDATTR_ERR;

	state.export = mdc_cur_export();
	state.dir = directory;
	state.status = &status;
	state.cb = NULL;  /* We don't use the call back during chunking. */
	state.dir_state = chunk; /* Pass the chunk to the callback */

	glist_init(&chunk->dirents);
	chunk->parent = directory;
	chunk->prev_chunk = prev_chunk;

	LogFullDebug(COMPONENT_NFS_READDIR, "Calling FSAL readdir");

	subcall(
		readdir_status = directory->sub_handle->obj_ops.readdir(
			directory->sub_handle, &whence, &state,
			mdc_readdir_chunked_cb, attrmask, &eod)
	       );

	if (FSAL_IS_ERROR(readdir_status)) {
		LogDebug(COMPONENT_NFS_READDIR, "FSAL readdir status=%s",
			 fsal_err_txt(readdir_status));
		*dirent = NULL;
		gsh_free(chunk);
		return readdir_status;
	}

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_NFS_READDIR, "status=%s",
			 fsal_err_txt(status));
		*dirent = NULL;
		gsh_free(chunk);
		return status;
	}

	if (chunk->num_entries == 0) {
		/* Chunk is empty - should only happen for an empty directory
		 * but could happen if the FSAL failed to indicate end of
		 * directory.
		 */
		LogFullDebug(COMPONENT_NFS_READDIR, "Empty chunk");
		*dirent = NULL;
		gsh_free(chunk);
	} else {
		/* Retain this chunk and return it's first entry.
		 * Also make sure cookie for last entry is correct if
		 * end of directory has been indicated.
		 */
		*dirent = glist_first_entry(&chunk->dirents,
					    mdcache_dir_entry_t,
					    chunk_list);

		if (eod) {
			/* If end of directory, mark last dirent as eod. */
			mdcache_dir_entry_t *last;

			last = glist_last_entry(&chunk->dirents,
						mdcache_dir_entry_t,
						chunk_list);
			last->eod = true;
		}

		LogFullDebug(COMPONENT_NFS_READDIR,
			     "Chunk first entry %s%s",
			     *dirent != NULL ? (*dirent)->name : "<NONE>",
			     eod ? " EOD" : "");

		/* Now add this chunk to the list of chunks for the directory.
		 */
		glist_add_tail(&directory->fsobj.fsdir.chunks,
			       &chunk->chunks);
	}

	return status;
}

/**
 * @brief Read the contents of a directory
 *
 * If necessary, populate dirent cache chunks from the underlying FSAL. Then,
  * walk the dirent cache chunks calling the callback.
 *
 * @note The object passed into the callback is ref'd and must be unref'd by the
 * callback.
 *
 * @param[in] directory  The directory to read
 * @param[in] whence     Where to start (next)
 * @param[in] dir_state  Pass thru of state to callback
 * @param[in] cb         Callback function
 * @param[in] attrmask   Which attributes to fill
 * @param[out] eod_met   eod marker true == end of dir
 *
 * @return FSAL status
 */

fsal_status_t mdcache_readdir_chunked(mdcache_entry_t *directory,
				      fsal_cookie_t whence,
				      void *dir_state,
				      fsal_readdir_cb cb,
				      attrmask_t attrmask,
				      bool *eod_met)
{
	mdcache_dir_entry_t *dirent = NULL;
	bool has_write, set_first_ck;
	fsal_cookie_t next_ck = whence, look_ck = whence;
	struct dir_chunk *chunk = NULL;

	/* Dirent's are being chunked; check to see if it needs updating */
	if ((directory->mde_flags & MDCACHE_TRUST_CONTENT) == 0) {
		/* Clean out the existing entries in the directory. */
		PTHREAD_RWLOCK_wrlock(&directory->content_lock);
		mdcache_dirent_invalidate_all(directory);
		has_write = true;
	} else {
		PTHREAD_RWLOCK_rdlock(&directory->content_lock);
		has_write = false;
	}

	if (look_ck == 0) {
		/* If starting from beginning, use the first_ck from the
		 * directory instead, this is only non-zero if the first
		 * chunk of the directory is still present.
		 */
		look_ck = directory->fsobj.fsdir.first_ck;
	}

	/* We need to know if we need to set first_ck. */
	set_first_ck = whence == 0 && look_ck == 0;

again:
	/* Get here on first pass, retry if we don't hold the write lock,
	 * and repeated passes if we need to fetch another chunk.
	 */

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "Readdir chunked next_ck=0x%"PRIx64" look_ck=%"PRIx64,
		     next_ck, look_ck);

	if (look_ck == 0 ||
	    !mdcache_avl_lookup_ck(directory, look_ck, &dirent)) {
		fsal_status_t status;
		/* This starting position isn't in our cache...
		 * Go populate the cache and process from there.
		 */
		if (!has_write) {
			/* Upgrade to write lock and retry just in case
			 * another thread managed to populate this cookie
			 * in the meantime.
			 */
			PTHREAD_RWLOCK_unlock(&directory->content_lock);
			PTHREAD_RWLOCK_wrlock(&directory->content_lock);
			has_write = true;
			goto again;
		}

		LogFullDebug(COMPONENT_NFS_READDIR,
			     "Readdir chunked about to populate next_ck=0x%"
			     PRIx64, next_ck);

		/* No, we need to populate a chunk using this cookie.
		 *
		 * NOTE: empty directory can result in dirent being NULL, and
		 *       we will ALWAYS re-read an empty directory every time.
		 */
		status = mdcache_populate_dir_chunk(directory, next_ck,
						    &dirent, chunk);

		if (FSAL_IS_ERROR(status)) {
			PTHREAD_RWLOCK_unlock(&directory->content_lock);

			LogFullDebug(COMPONENT_NFS_READDIR,
				     "mdcache_populate_dir_chunk failed status=%s",
				     fsal_err_txt(status));

			if (status.major == ERR_FSAL_STALE)
				mdcache_kill_entry(directory);

			return status;
		}

		if (dirent == NULL) {
			/* We must have reached the end of the directory, or the
			 * directory was empty. In any case, there is no next
			 * chunk or dirent.
			 */
			*eod_met = true;
			PTHREAD_RWLOCK_unlock(&directory->content_lock);
			return status;
		}

		chunk = dirent->chunk;
		if (set_first_ck) {
			/* We just populated the first dirent in the directory,
			 * save it's cookie as first_ck.
			 */
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Setting directory first_ck=%"PRIx64,
				     dirent->ck);
			directory->fsobj.fsdir.first_ck = dirent->ck;
			set_first_ck = false;
		}
	} else {
		/* We found the dirent... If next_ck is NOT whence, we SHOULD
		 * have found the first dirent in the chunk, if not, then
		 * something went wrong at some point. That chunk is valid,
		 */
		chunk = dirent->chunk;
	}

	/* dirent WILL be non-NULL, emember the chunk we are in. */
	chunk = dirent->chunk;

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "About to read directory=%p cookie=%" PRIx64,
		     directory, next_ck);

	/* Now satisfy the request from the cached readdir--stop when either
	 * the requested sequence or dirent sequence is exhausted */

	for (;
	     dirent != NULL;
	     dirent = glist_next_entry(&chunk->dirents,
				       mdcache_dir_entry_t,
				       chunk_list,
				       &dirent->chunk_list)) {
		fsal_status_t status;
		enum fsal_dir_result cb_result;
		mdcache_entry_t *entry = NULL;
		struct attrlist attrs;

		if (dirent->ck == whence) {
			/* When called with whence, the caller always wants the
			 * next entry, skip this entry.
			 */
			continue;
		}

		if (dirent->flags & DIR_ENTRY_FLAG_DELETED) {
			/* Skip deleted entries */
			continue;
		}

		/* Get actual entry using the dirent ckey */
		status = mdcache_find_keyed(&dirent->ckey, &entry);

		if (FSAL_IS_ERROR(status)) {
			/* Failed using ckey, do full lookup. */
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "Lookup by key for %s failed, lookup by name now",
				     dirent->name);

			status = mdc_lookup_uncached(directory, dirent->name,
						     &entry, NULL);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&directory->content_lock);

				LogFullDebug(COMPONENT_NFS_READDIR,
					     "lookup by name failed status=%s",
					     fsal_err_txt(status));

				if (status.major == ERR_FSAL_STALE)
					mdcache_kill_entry(directory);

				return status;
			}
		}

		next_ck = dirent->ck;

		/* Ensure the attribute cache is valid.  The simplest way to do
		 * this is to call getattrs().  We need a copy anyway, to ensure
		 * thread safety.
		 */
		fsal_prepare_attrs(&attrs, attrmask);

		status = entry->obj_handle.obj_ops.getattrs(&entry->obj_handle,
							    &attrs);
		if (FSAL_IS_ERROR(status)) {
			PTHREAD_RWLOCK_unlock(&directory->content_lock);

			LogFullDebug(COMPONENT_NFS_READDIR,
				     "getattrs failed status=%s",

				     fsal_err_txt(status));
			return status;
		}

		cb_result = cb(dirent->name, &entry->obj_handle, &entry->attrs,
			       dir_state, next_ck, NULL);

		fsal_release_attrs(&attrs);

		if (cb_result >= DIR_TERMINATE || dirent->eod) {
			/* Caller is done, or we have reached the end of
			 * the directory, no need to get another dirent.
			 */
			PTHREAD_RWLOCK_unlock(&directory->content_lock);

			/* If cb_result is DIR_TERMINATE, the callback did
			 * not consume this entry, so we can not have reached
			 * end of directory (for DIR_TERMINATE_MARK, we expect
			 * the callback DID consume the last entry).
			 */
			*eod_met = cb_result != DIR_TERMINATE && dirent->eod;

			LogDebug(COMPONENT_NFS_READDIR,
				 "dirent = %p %s, cb_result = %s, eod = %s",
				 dirent, dirent->name,
				 fsal_dir_result_str(cb_result),
				 *eod_met ? "true" : "false");

			return status;
		}


	} while (dirent != NULL);

	if (chunk->next_ck != 0) {
		/* If the chunk has a known chunk following it, use the first
		 * cookie in that chunk for AVL tree lookup, which will succeed
		 * rather than having to do a readdir to find the next entry.
		 *
		 * If the chunk is no longer present, the lookup will fail, in
		 * which case next_ck is the right cookie to use as the whence
		 * for the next readdir.
		 */
		look_ck = chunk->next_ck;
	} else {
		/* The next chunk is not resident, skip right to populating
		 * the next chunk. next_ck is the right cookie to use as the
		 * whence for the next readdir.
		 */
		look_ck = 0;
	}

	/* Due to the conditions we return from inside the loop, we know that if
	 * we reach the end of the chunk we must fetch another chunk to satisfy
	 * the directory read. The next_ck is the cookie for the next dirent to
	 * find, which should be the first dirent of the next chunk.
	 */

	/* NOTE: An FSAL that does not return 0 or LAST_COOKIE
	 *       as the cookie for the last directory entry will
	 *       result in our attempting to find one more
	 *       chunk, which will not succeed and then the eod
	 *       condition detected above before the while loop
	 *       will kick in.
	 */

	/* NOTE: We also keep the write lock if we already had
	 *       it. Most likely we will need to populate the
	 *       next chunk also. It's probably not worth
	 *       dropping the write lock and taking the read
	 *       lock just in case the next chunk actually
	 *       happens to be populated.
	 */
	goto again;
}

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
 * @returns fsal_dir_result
 */

static enum fsal_dir_result
mdc_populate_dirent(const char *name, struct fsal_obj_handle *sub_handle,
		    struct attrlist *attrs, void *dir_state,
		    fsal_cookie_t cookie, fsal_cookie_t *ret_cookie)
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
			return DIR_CONTINUE;
		}
		if ((*state->status).major == ERR_FSAL_OVERFLOW) {
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Lookup failed on %s in dir %p with %s",
				     name, directory,
				     fsal_err_txt(*state->status));
		} else {
			LogInfo(COMPONENT_CACHE_INODE,
				"Lookup failed on %s in dir %p with %s",
				name, directory, fsal_err_txt(*state->status));
		}
		return DIR_TERMINATE;
	}

	return DIR_CONTINUE;
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
	state.cb = NULL; /* cached dirs don't use callback */
	state.dir_state = NULL; /* cached dirs don't use dir_state */

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

	if (status.major == ERR_FSAL_OVERFLOW)
		return status;

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
