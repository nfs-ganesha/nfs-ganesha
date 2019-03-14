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
#ifdef USE_LTTNG
#include "gsh_lttng/mdcache.h"
#endif

#define mdc_chunk_first_dirent(c) \
	glist_first_entry(&(c)->dirents, mdcache_dir_entry_t, chunk_list)

static inline bool trust_negative_cache(mdcache_entry_t *parent)
{
	bool trust = op_ctx_export_has_option(
				  EXPORT_OPTION_TRUST_READIR_NEGATIVE_CACHE) &&
		test_mde_flags(parent, MDCACHE_DIR_POPULATED);

	if (trust)
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Entry %p Trust negative cache",
				parent);
	else
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Entry %p Don't Trust negative cache",
				parent);

	return trust;
}

/**
 * @brief Add a detached dirent to the LRU list (in the MRU position).
 *
 * If rhe maximum number of detached dirents would be exceeded, remove the
 * LRU dirent.
 *
 * @note mdc_parent MUST have it's content_lock held for writing
 *
 * @param[in]     parent  Parent entry
 * @param[in]     dirent  Dirent to move to MRU
 */

static inline void add_detached_dirent(mdcache_entry_t *parent,
				       mdcache_dir_entry_t *dirent)
{
#ifdef DEBUG_MDCACHE
	assert(parent->content_lock.__data.__cur_writer);
#endif
	if (parent->fsobj.fsdir.detached_count ==
	    mdcache_param.dir.avl_detached_max) {
		/* Need to age out oldest detached dirent. */
		mdcache_dir_entry_t *removed;

		/* Find the oldest detached dirent and remove it.
		 * We just hold the spin lock for the list operation.
		 * Technically we don't need it since the content lock is held
		 * for write, there can be no conflicting threads. Since we
		 * don't have a racing thread, it's ok that the list is
		 * unprotected by spin lock while we make the AVL call.
		 */
		pthread_spin_lock(&parent->fsobj.fsdir.spin);

		removed = glist_last_entry(&parent->fsobj.fsdir.detached,
					   mdcache_dir_entry_t,
					   chunk_list);

		pthread_spin_unlock(&parent->fsobj.fsdir.spin);

		/* Remove from active names tree */
		mdcache_avl_remove(parent, removed);
	}

	/* Add new entry to MRU (head) of list */
	pthread_spin_lock(&parent->fsobj.fsdir.spin);
	glist_add(&parent->fsobj.fsdir.detached, &dirent->chunk_list);
	parent->fsobj.fsdir.detached_count++;
	pthread_spin_unlock(&parent->fsobj.fsdir.spin);
}

#define mdcache_alloc_handle(export, sub_handle, fs, reason) \
	_mdcache_alloc_handle(export, sub_handle, fs, reason, \
			      __func__, __LINE__)
/**
 * Allocate and initialize a new mdcache handle.
 *
 * This function doesn't free the sub_handle if the allocation fails. It must
 * be done in the calling function.
 *
 * @param[in] export The mdcache export used by the handle.
 * @param[in] sub_handle The handle used by the subfsal.
 * @param[in] fs The filesystem of the new handle.
 * @param[in] reason The reason the entry is being inserted
 *
 * @return The new handle, or NULL if the unexport in progress.
 */
static mdcache_entry_t *_mdcache_alloc_handle(
		struct mdcache_fsal_export *export,
		struct fsal_obj_handle *sub_handle,
		struct fsal_filesystem *fs,
		mdc_reason_t reason,
		const char *func, int line)
{
	mdcache_entry_t *result;
	fsal_status_t status;

	result = mdcache_lru_get(sub_handle);

	if (result == NULL) {
		/* Should never happen, but our caller will handle... */
		return NULL;
	}

	/* Base data */
	result->sub_handle = sub_handle;
	result->obj_handle.type = sub_handle->type;
	result->obj_handle.fsid = sub_handle->fsid;
	result->obj_handle.fileid = sub_handle->fileid;
	result->obj_handle.fs = fs;

	/* default handlers */
	fsal_obj_handle_init(&result->obj_handle, &export->mfe_exp,
			     sub_handle->type);
	/* mdcache handlers */
	result->obj_handle.obj_ops = &MDCACHE.handle_ops;
	/* state */
	if (sub_handle->type == DIRECTORY) {
		result->obj_handle.state_hdl = &result->fsobj.fsdir.dhdl;
		/* init avl tree */
		mdcache_avl_init(result);

		/* init chunk list and detached dirents list */
		glist_init(&result->fsobj.fsdir.chunks);
		glist_init(&result->fsobj.fsdir.detached);
		(void) pthread_spin_init(&result->fsobj.fsdir.spin,
					 PTHREAD_PROCESS_PRIVATE);
	} else {
		result->obj_handle.state_hdl = &result->fsobj.hdl;
	}
	state_hdl_init(result->obj_handle.state_hdl, result->obj_handle.type,
		       &result->obj_handle);

	/* Initialize common fields */
	result->mde_flags = 0;
	glist_init(&result->export_list);
	atomic_store_int32_t(&result->first_export_id, -1);

	/* Map the export before we put this entry into the LRU, but after it's
	 * well enough set up to be able to be unrefed by unexport should there
	 * be a race.
	 */
	status = mdc_check_mapping(result);

	if (unlikely(FSAL_IS_ERROR(status))) {
		/* The current export is in process to be unexported, don't
		 * create new mdcache entries.
		 */
		LogDebug(COMPONENT_CACHE_INODE,
			 "Trying to allocate a new entry %p for export id %"
			 PRIi16" that is in the process of being unexported",
			 result, op_ctx->ctx_export->export_id);
		/* sub_handle will be freed by the caller */
		result->sub_handle = NULL;
		mdcache_put(result);
		/* Handle is not yet in hash / LRU, so just put the sentinal
		 * ref */
		mdcache_put(result);
		return NULL;
	}

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
		export = expmap->exp;

		PTHREAD_RWLOCK_wrlock(&export->mdc_exp_lock);

		mdc_remove_export_map(expmap);

		PTHREAD_RWLOCK_unlock(&export->mdc_exp_lock);
	}

	/* Clear out first_export */
	atomic_store_int32_t(&entry->first_export_id, -1);

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	if (entry->obj_handle.type == DIRECTORY) {
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);

		/* Clean up dirents */
		mdcache_dirent_invalidate_all(entry);
		/* Clean up parent key */
		mdcache_free_fh(&entry->fsobj.fsdir.parent);

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
 * If an unexport is in progress, return ERR_FSAL_STALE to prevent the caller
 * from proceeding.
 *
 * @param[in]  entry     The cache inode
 *
 * @return FSAL Status
 *
 */

fsal_status_t mdc_check_mapping(mdcache_entry_t *entry)
{
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct glist_head *glist;
	struct entry_export_map *expmap;
	bool try_write = false;

	if (atomic_fetch_uint8_t(&export->flags) & MDC_UNEXPORT) {
		/* In the process of unexporting, don't check export mapping.
		 * Return a stale error.
		 */
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	/* Fast path check to see if this export is already mapped */
	if (atomic_fetch_int32_t(&entry->first_export_id) ==
	    (int32_t) op_ctx->ctx_export->export_id)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

again:
	(void)atomic_inc_uint64_t(&cache_stp->inode_mapping);

	glist_for_each(glist, &entry->export_list) {
		expmap = glist_entry(glist, struct entry_export_map,
				     export_per_entry);

		/* Found active export on list */
		if (expmap->exp == export) {
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
	PTHREAD_RWLOCK_wrlock(&export->mdc_exp_lock);

	/* Check for unexport again, this prevents an interlock issue where
	 * we passed above, but now unexport is in progress. This is required
	 * because the various locks are acquired, dropped, and re-acquired
	 * in such a way that unexport may have started after we made the
	 * check at the top.
	 */
	if (atomic_fetch_uint8_t(&export->flags) & MDC_UNEXPORT) {
		/* In the process of unexporting, don't allow creating a new
		 * export mapping. Return a stale error.
		 */
		PTHREAD_RWLOCK_unlock(&export->mdc_exp_lock);
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	expmap = gsh_calloc(1, sizeof(*expmap));

	/* If export_list is empty, store this export as first */
	if (glist_empty(&entry->export_list)) {
		atomic_store_int32_t(&entry->first_export_id,
				     (int32_t) op_ctx->ctx_export->export_id);
	}

	expmap->exp = export;
	expmap->entry = entry;

	glist_add_tail(&entry->export_list, &expmap->export_per_entry);
	glist_add_tail(&export->entry_list, &expmap->entry_per_export);

	PTHREAD_RWLOCK_unlock(&export->mdc_exp_lock);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* entry's content_lock must be held in exclusive mode */
fsal_status_t
mdc_get_parent_handle(struct mdcache_fsal_export *export,
		      mdcache_entry_t *entry,
		      struct fsal_obj_handle *sub_parent)
{
	char buf[NFS4_FHSIZE];
	struct gsh_buffdesc fh_desc = { buf, NFS4_FHSIZE };
	fsal_status_t status;

#ifdef DEBUG_MDCACHE
	assert(entry->content_lock.__data.__cur_writer);
#endif

	/* Get a wire handle that can be used with create_handle() */
	subcall_raw(export,
		    status = sub_parent->obj_ops->handle_to_wire(sub_parent,
					FSAL_DIGEST_NFSV4, &fh_desc)
		   );
	if (FSAL_IS_ERROR(status))
		return status;

	/* And store in the parent host-handle */
	mdcache_copy_fh(&entry->fsobj.fsdir.parent, &fh_desc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* entry's content_lock must be held in exclusive mode */
void
mdc_get_parent(struct mdcache_fsal_export *export, mdcache_entry_t *entry)
{
	struct fsal_obj_handle *sub_handle;
	fsal_status_t status;

#ifdef DEBUG_MDCACHE
	assert(entry->content_lock.__data.__cur_writer);
#endif

	if (entry->obj_handle.type != DIRECTORY) {
		/* Parent pointer only for directories */
		return;
	}

	if (entry->fsobj.fsdir.parent.len != 0) {
		/* Already has a parent pointer */
		return;
	}

	subcall_raw(export,
		status = entry->sub_handle->obj_ops->lookup(
			    entry->sub_handle, "..", &sub_handle, NULL)
	       );

	if (FSAL_IS_ERROR(status)) {
		/* Top of filesystem */
		return;
	}

	mdc_get_parent_handle(export, entry, sub_handle);

	/* Release parent handle */
	subcall_raw(export,
		    sub_handle->obj_ops->release(sub_handle)
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

#ifdef DEBUG_MDCACHE
	assert(parent->content_lock.__data.__cur_writer);
#endif

	glist_for_each_safe(glist, glistn, &chunk->dirents) {
		mdcache_dir_entry_t *dirent;

		dirent = glist_entry(glist, mdcache_dir_entry_t, chunk_list);

		/* Remove from deleted or active names tree */
		mdcache_avl_remove(parent, dirent);
	}

	/* Remove chunk from directory. */
	glist_del(&chunk->chunks);

	/* At this point the following is true about the chunk:
	 *
	 * chunks is {NULL, NULL} do to the glist_del
	 * dirents is {&dirents, &dirents}, i.e. empty as a result of the
	 *                                  glist_for_each_safe above
	 * the other fields are untouched.
	 */

	/* This chunk is about to be freed or reused, clean up a few more
	 * things.
	 */

	chunk->parent = NULL;
	chunk->next_ck = 0;
	chunk->num_entries = 0;
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

#ifdef DEBUG_MDCACHE
	assert(entry->content_lock.__data.__cur_writer);
#endif
	glist_for_each_safe(glist, glistn, &entry->fsobj.fsdir.chunks) {
		mdcache_lru_unref_chunk(glist_entry(glist, struct dir_chunk,
						    chunks), true);
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
	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Invalidating directory for %p, clearing MDCACHE_DIR_POPULATED setting MDCACHE_TRUST_CONTENT and MDCACHE_TRUST_DIR_CHUNKS",
			entry);

	/* Clean the chunks first, that will clean most of the active
	 * entries also.
	 */
	mdcache_clean_dirent_chunks(entry);

	/* Clean the active and deleted trees */
	mdcache_avl_clean_trees(entry);

	atomic_clear_uint32_t_bits(&entry->mde_flags, MDCACHE_DIR_POPULATED);

	atomic_set_uint32_t_bits(&entry->mde_flags, MDCACHE_TRUST_CONTENT |
						    MDCACHE_TRUST_DIR_CHUNKS);
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
		  struct state_t *state,
		  mdc_reason_t reason)
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
		    sub_handle->obj_ops->handle_to_key(sub_handle, &fh_desc)
		   );

	(void) cih_hash_key(&key, export->mfe_exp.sub_export->fsal, &fh_desc,
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
		goto out_no_new_entry_yet;
	} else if (status.major != ERR_FSAL_NOENT) {
		/* Real error , don't need a new sub_handle ref */
		goto out_no_new_entry_yet;
	}

	/* !LATCHED */

	/* We did not find the object.  Pull an entry off the LRU. The entry
	 * will already be mapped.
	 */
	nentry = mdcache_alloc_handle(export, sub_handle, sub_handle->fs,
				      reason);

	if (nentry == NULL) {
		/* We didn't get an entry because of unexport in progress,
		 * go ahead and bail out now.
		 */
		status = fsalstat(ERR_FSAL_STALE, 0);
		goto out_no_new_entry_yet;
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

		goto out_release_new_entry;
	}

	/* We won the race. */

	/* Set cache key */

	has_hashkey = cih_hash_key(&nentry->fh_hk.key,
				   export->mfe_exp.sub_export->fsal,
				   &fh_desc, CIH_HASH_NONE);

	if (!has_hashkey) {
		cih_hash_release(&latch);
		LogCrit(COMPONENT_CACHE_INODE,
			"Could not hash new entry");
		status = fsalstat(ERR_FSAL_NOMEM, 0);
		goto out_release_new_entry;
	}

	switch (nentry->obj_handle.type) {
	case REGULAR_FILE:
		LogDebug(COMPONENT_CACHE_INODE,
			 "Adding a REGULAR_FILE, entry=%p", nentry);

		/* Init statistics used for intelligently granting delegations*/
		init_deleg_heuristics(&nentry->obj_handle);
		break;

	case DIRECTORY:
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			    "Adding a DIRECTORY, entry=%p setting MDCACHE_TRUST_CONTENT %s",
			    nentry, new_directory
					? "setting MDCACHE_DIR_POPULATED"
					: "clearing MDCACHE_DIR_POPULATED");

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
		goto out_release_new_entry;
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
		    op_ctx->export_perms->expire_time_attr;
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
		goto out_release_new_entry;
	}

	if (isFullDebug(COMPONENT_CACHE_INODE)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str };

		(void) display_mdcache_key(&dspbuf, &nentry->fh_hk.key);

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "New entry %p added with fh_hk.key %s",
			     nentry, str);
	} else {
		LogDebug(COMPONENT_CACHE_INODE, "New entry %p added", nentry);
	}
	mdcache_lru_insert(nentry, reason);
	*entry = nentry;
	(void)atomic_inc_uint64_t(&cache_stp->inode_added);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 out_release_new_entry:

	/* We raced or failed, release the new entry we acquired, this will
	 * result in inline deconstruction. This will release the attributes, we
	 * may not have copied yet, in which case mask and acl are 0/NULL.  This
	 * entry is not yet in the hash or LRU, so just put it's sentinal ref.
	 */
	nentry->sub_handle = NULL;
	mdcache_put(nentry);
	mdcache_put(nentry);

 out_no_new_entry_yet:

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

		subcall_raw(export,
			    status =
			    old_sub_handle->obj_ops->merge(old_sub_handle,
							  sub_handle)
			   );

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
		fsal_status_t cstatus;

		subcall_raw(export,
			    cstatus = sub_handle->obj_ops->close2(sub_handle,
								 state)
			   );

		LogDebug(COMPONENT_CACHE_INODE,
			 "Close of state during error processing returned %s",
			 fsal_err_txt(cstatus));
	}

	/* must free sub_handle if no new entry was created to reference it. */
	subcall_raw(export,
		    sub_handle->obj_ops->release(sub_handle)
		   );

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
 * @param[in] reason	The reason for the lookup
 *
 * @note This returns ref'd entry on success, INITIAL if @a reason is not SCAN
 *
 * @return Status
 */
fsal_status_t
mdcache_find_keyed_reason(mdcache_key_t *key, mdcache_entry_t **entry,
			  mdc_reason_t reason)
{
	cih_latch_t latch;

	if (key->kv.addr == NULL) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Attempt to use NULL key");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	if (isFullDebug(COMPONENT_CACHE_INODE)) {
		char str[LOG_BUFF_LEN] = "\0";
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
		status = mdcache_lru_ref(*entry, (reason != MDC_REASON_SCAN) ?
					 LRU_REQ_INITIAL : LRU_FLAG_NONE);
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

		status = mdc_check_mapping(*entry);

		if (unlikely(FSAL_IS_ERROR(status))) {
			/* Export is in the process of being removed, don't
			 * add this entry to the export, and bail out of the
			 * operation sooner than later.
			 */
			mdcache_put(*entry);
			*entry = NULL;
			return status;
		}

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Found entry %p",
			     *entry);

		(void)atomic_inc_uint64_t(&cache_stp->inode_hit);

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	return fsalstat(ERR_FSAL_NOENT, 0);
}

/**
 * @brief Find or create a cache entry by it's host-handle
 *
 * Locate a cache entry by host-handle.  If it is not in the cache, an attempt
 * will be made to create it and insert it in the cache.
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
mdcache_locate_host(struct gsh_buffdesc *fh_desc,
		    struct mdcache_fsal_export *export,
		    mdcache_entry_t **entry,
		    struct attrlist *attrs_out)
{
	struct fsal_export *sub_export = export->mfe_exp.sub_export;
	mdcache_key_t key;
	struct fsal_obj_handle *sub_handle;
	struct attrlist attrs;
	fsal_status_t status;

	/* Copy the fh_desc into key, todo: is there a function for this? */
	/* We want to save fh_desc */
	key.kv.len = fh_desc->len;
	key.kv.addr = alloca(key.kv.len);
	memcpy(key.kv.addr, fh_desc->addr, key.kv.len);
	subcall_raw(export,
		    status = sub_export->exp_ops.host_to_key(sub_export,
							     &key.kv)
	       );

	if (FSAL_IS_ERROR(status))
		return status;

	(void)cih_hash_key(&key, sub_export->fsal, &key.kv,
			    CIH_HASH_KEY_PROTOTYPE);


	status = mdcache_find_keyed(&key, entry);

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
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) & ~ATTR_ACL);

	sub_export = export->mfe_exp.sub_export;

	subcall_raw(export,
		    status = sub_export->exp_ops.create_handle(sub_export,
							       fh_desc,
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
				   false, entry, NULL, MDC_REASON_DEFAULT);

	fsal_release_attrs(&attrs);

	if (!FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create_handle Created entry %p FSAL %s",
			     (*entry), (*entry)->sub_handle->fsal->name);
	}

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

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Look in cache %s, trust content %s",
			name,
			test_mde_flags(mdc_parent, MDCACHE_TRUST_CONTENT)
				? "yes" : "no");

#ifdef DEBUG_MDCACHE
	assert(mdc_parent->content_lock.__data.__readers ||
	       mdc_parent->content_lock.__data.__cur_writer);
#endif
	*entry = NULL;

	/* If aren't caching dirents, return stale */
	if (mdcache_param.dir.avl_chunk == 0)
		return fsalstat(ERR_FSAL_STALE, 0);

	/* If the dirent cache is untrustworthy, don't even ask it */
	if (!test_mde_flags(mdc_parent, MDCACHE_TRUST_CONTENT))
		return fsalstat(ERR_FSAL_STALE, 0);

	dirent = mdcache_avl_lookup(mdc_parent, name);
	if (dirent) {
		if (dirent->chunk != NULL) {
			/* Bump the chunk in the LRU */
			lru_bump_chunk(dirent->chunk);
		} else {
			/* Bump the detached dirent. */
			bump_detached_dirent(mdc_parent, dirent);
		}
		status = mdcache_find_keyed(&dirent->ckey, entry);
		if (!FSAL_IS_ERROR(status))
			return status;
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"mdcache_find_keyed %s failed %s",
				name, fsal_err_txt(status));
	} else {	/* ! dirent */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"mdcache_avl_lookup %s failed trust negative %s",
				name,
				trust_negative_cache(mdc_parent)
					? "yes" : "no");
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

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Lookup %s", name);

	PTHREAD_RWLOCK_rdlock(&mdc_parent->content_lock);

	/* ".." doesn't end up in the cache */
	if (!strcmp(name, "..")) {
		struct mdcache_fsal_export *export = mdc_cur_export();
		struct gsh_buffdesc tmpfh;

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Lookup parent (..) of %p", mdc_parent);

		if (mdc_parent->fsobj.fsdir.parent.len == 0) {
			/* we need write lock */
			PTHREAD_RWLOCK_unlock(&mdc_parent->content_lock);
			PTHREAD_RWLOCK_wrlock(&mdc_parent->content_lock);
			mdc_get_parent(export, mdc_parent);
		}

		/* We need to drop the content lock around the locate, as that
		 * will try to take the attribute lock on the parent to refresh
		 * it's attributes, which can cause an ABBA with lookup/readdir.
		 * Copy the parent filehandle, so we can drop the lock.
		 */
		mdcache_copy_fh(&tmpfh, &mdc_parent->fsobj.fsdir.parent);
		PTHREAD_RWLOCK_unlock(&mdc_parent->content_lock);

		status =  mdcache_locate_host(&tmpfh, export, new_entry,
					      attrs_out);

		mdcache_free_fh(&tmpfh);

		if (status.major == ERR_FSAL_STALE)
			status.major = ERR_FSAL_NOENT;
		return status;
	}

	if (mdcache_param.dir.avl_chunk == 0) {
		/* We aren't caching dirents; call directly.
		 * NOTE: Technically we will call mdc_lookup_uncached not
		 *       holding the content_lock write as required, however
		 *       since we are operating uncached here, ultimately there
		 *       will be no addition to the dirent cache, and thus no
		 *       need to hold the write lock.
		 */
		goto uncached;
	}

	/* We first try avltree_lookup by name.  If that fails, we dispatch to
	 * the FSAL. */
	status = mdc_try_get_cached(mdc_parent, name, new_entry);

	if (status.major == ERR_FSAL_STALE) {
		/* Get a write lock and try again */
		PTHREAD_RWLOCK_unlock(&mdc_parent->content_lock);

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Try again %s", name);

		PTHREAD_RWLOCK_wrlock(&mdc_parent->content_lock);

		status = mdc_try_get_cached(mdc_parent, name, new_entry);
	}
	if (!FSAL_IS_ERROR(status)) {
		/* Success! Now fetch attr if requested, drop content_lock
		 * to avoid ABBA locking situation.
		 */
		PTHREAD_RWLOCK_unlock(&mdc_parent->content_lock);

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
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
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			    "Lookup %s failed %s",
			    name, fsal_err_txt(status));
		goto out;
	}

	/* Need to look up. */
	if (!test_mde_flags(mdc_parent, MDCACHE_TRUST_CONTENT)) {
		/* We have the write lock and the content is
		 * still invalid.  Empty it out and mark it
		 * valid in preparation for caching the result of this lookup.
		 */
		mdcache_dirent_invalidate_all(mdc_parent);
	}

	LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
		    "Cache Miss detected for %s", name);

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
 * @note This returns an INITIAL ref'd entry on success
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
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) & ~ATTR_ACL);

	subcall(
		status = mdc_parent->sub_handle->obj_ops->lookup(
			    mdc_parent->sub_handle, name, &sub_handle, &attrs)
	       );

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
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
			LogDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
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
			LogDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
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
 *
 * @brief Adds a directory entry to a cached directory.
 *
 * This function adds a new directory entry to a directory.  Directory
 * entries have only weak references, so they do not prevent recycling
 * or freeing the entry they locate.  This function may be called
 * either once (for handling creation) or iteratively in directory
 * population.
 *
 * @note Caller MUST hold the content_lock for write and must only call if
 *       dirents are being cached.
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

	if (name[0] == '\0') {
		/* An empty dirent name is invalid */
		LogInfo(COMPONENT_CACHE_INODE,
			"Invalid dirent with empty name");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

#ifdef DEBUG_MDCACHE
	assert(parent->content_lock.__data.__cur_writer);
#endif

	/* in cache avl, we always insert on pentry_parent */
	new_dir_entry = gsh_calloc(1, sizeof(mdcache_dir_entry_t) + namesize);
	new_dir_entry->flags = DIR_ENTRY_FLAG_NONE;
	allocated_dir_entry = new_dir_entry;

	memcpy(&new_dir_entry->name_buffer, name, namesize);
	new_dir_entry->name = new_dir_entry->name_buffer;
	mdcache_key_dup(&new_dir_entry->ckey, &entry->fh_hk.key);

	/* add to avl */
	code = mdcache_avl_insert(parent, &new_dir_entry);
	if (code < 0) {
		/** @todo: maybe we should actually invalidate the dirent cache
		 *         at this point?
		 *
		 * This indicates an odd condition in the tree, just treat
		 * as an EEXIST condition.
		 */
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			    "Returning EEXIST for %s code %d", name, code);
		return fsalstat(ERR_FSAL_EXIST, 0);
	}

	/* we're going to succeed */
	if (new_dir_entry == allocated_dir_entry) {
		/* Place new dirent into a chunk or as detached. */
		place_new_dirent(parent, new_dir_entry);

		/* Since we are chunking, we can preserve the dirent cache for
		 * the purposes of lookups even if we could not add the new
		 * dirent to a chunk, so we don't want to invalidate the parent
		 * directory.
		 */
		*invalidate = false;
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
 */
void mdcache_dirent_remove(mdcache_entry_t *parent, const char *name)
{
#ifdef DEBUG_MDCACHE
	assert(parent->content_lock.__data.__cur_writer);
#endif
	/* Don't remove if we aren't doing dirent caching or the cache is empty
	 */
	if (mdcache_param.dir.avl_chunk != 0 &&
	    avltree_size(&parent->fsobj.fsdir.avl.t) != 0) {
		mdcache_dir_entry_t *dirent;

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Remove dir entry %s", name);

		dirent = mdcache_avl_lookup(parent, name);

		if (dirent != NULL)
			avl_dirent_set_deleted(parent, dirent);
	}
}

/**
 * @brief State to be passed to FSAL readdir callbacks
 */

struct mdcache_populate_cb_state {
	struct mdcache_fsal_export *export;
	mdcache_entry_t *dir;
	fsal_status_t *status;
	fsal_readdir_cb cb;
	void *dir_state; /**< For unchunked */
	/** First chunk of this cycle */
	struct dir_chunk *first_chunk;
	/** Current chunk of this cycle */
	struct dir_chunk *cur_chunk;
	/** Chunk previous to cur_chunk, if known */
	struct dir_chunk *prev_chunk;
	/** dirent to be filled in when whence_is_name */
	mdcache_dir_entry_t **dirent;
	/** Cookie is what we are actually searching for */
	fsal_cookie_t cookie;
	/** Indicates if FSAL expects whence to be a name. */
	bool whence_is_name;
	/** If whence_is_name, indicate if we are looking for caller's cookie.
	 */
	bool whence_search;
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
			fsal_cookie_t cookie)
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
					   NULL, false, &new_entry, NULL,
					   MDC_REASON_SCAN)
	);

	if (FSAL_IS_ERROR(status)) {
		*state->status = status;
		if (status.major == ERR_FSAL_XDEV) {
			LogInfoAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Ignoring XDEV entry %s", name);
			*state->status = fsalstat(ERR_FSAL_NO_ERROR, 0);
			return DIR_CONTINUE;
		}
		LogInfoAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			   "Lookup failed on %s in dir %p with %s",
			   name, directory, fsal_err_txt(*state->status));
		return DIR_TERMINATE;
	}

	/* Call up the stack.  Do a supercall */
	supercall_raw(state->export,
		      rv = state->cb(name, &new_entry->obj_handle,
				     &new_entry->attrs, state->dir_state,
				     cookie));

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
		readdir_status = directory->sub_handle->obj_ops->readdir(
			directory->sub_handle, whence, &state,
			mdc_readdir_uncached_cb, attrmask, eod_met)
	       );

	if (FSAL_IS_ERROR(readdir_status))
		return readdir_status;

	return status;
}

/**
 * @brief Place a new dirent from create, lookup, or rename into a chunk if
 * possible, otherwise place as a detached dirent.
 *
 * If addition is not possible because the entry does not belong to an
 * active dirent chunk, nothing happens. The dirent may still be inserted
 * into the by name lookup as a detached dirent.
 *
 * @note If we can't insert the dirent into a chunk because we can't figure
 * out which chunk it belongs to, we can still trust the chunks, the new dirent
 * is not within their range, and if inserted between two non-adjacent chunks,
 * a subsequent readdir that enumerates that part of the directory will pick up
 * the new dirent since it will have to populate at least one new chunk in the
 * gap.
 *
 * @note parent_dir MUST have it's content_lock held for writing
 *
 * @param[in] parent_dir     The directory this dir entry is part of
 * @param[in] new_dir_entry  The dirent to add.
 *
 */
void place_new_dirent(mdcache_entry_t *parent_dir,
		      mdcache_dir_entry_t *new_dir_entry)
{
	mdcache_dir_entry_t *left;
	mdcache_dir_entry_t *right;
	struct avltree_node *node, *parent, *unbalanced, *other;
	int is_left, code;
	fsal_cookie_t ck, nck;
	struct dir_chunk *chunk;
	bool invalidate_chunks = true;

#ifdef DEBUG_MDCACHE
	assert(parent_dir->content_lock.__data.__cur_writer);
#endif
	subcall(
		ck = parent_dir->sub_handle->obj_ops->compute_readdir_cookie(
				parent_dir->sub_handle, new_dir_entry->name)
	       );

	if (ck == 0) {
		/* FSAL does not support computing readdir cookie, so we can't
		 * add this entry to a chunk, nor can we trust the chunks.
		 */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Could not add %s to chunk for directory %p, compute_readdir_cookie failed",
				new_dir_entry->name, parent_dir);
		goto out;
	}

	new_dir_entry->ck = ck;

	node = avltree_do_lookup(&new_dir_entry->node_sorted,
				 &parent_dir->fsobj.fsdir.avl.sorted,
				 &parent, &unbalanced, &is_left,
				 avl_dirent_sorted_cmpf);

	if (isFullDebug(COMPONENT_CACHE_INODE) ||
	    isFullDebug(COMPONENT_NFS_READDIR)) {
		if (node) {
			right = avltree_container_of(node, mdcache_dir_entry_t,
						     node_sorted);
		}

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
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
					->obj_ops->compute_readdir_cookie(
						parent_dir->sub_handle,
						right->name)
			       );

			if (nck == 0) {
				/* Oops, could not compute new cookie...
				 * We can no longer trust the chunks.
				 */
				LogCrit(COMPONENT_CACHE_INODE,
					"Could not compute new cookie for %s in directory %p",
					right->name, parent_dir);
				goto out;
			}

			/* Just change up the old first entries cookie, which
			 * will leave room to insert the new entry with cookie
			 * of FIRST_COOKIE.
			 */
			right->ck = nck;
		} else {
			/* This should not happen... Let's no longer trust the
			 * chunks.
			 */
			LogCrit(COMPONENT_CACHE_INODE,
				"Could not add %s to chunk for directory %p, node %s found withck=%"
				PRIx64,
				new_dir_entry->name, parent_dir,
				right->name, right->ck);
			goto out;
		}
	}

	if (parent == NULL) {
		/* The tree must be empty, there are no chunks to add this
		 * entry to. There are no chunks to trust...
		 */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Could not add %s to chunk for directory %p, tree was empty",
				new_dir_entry->name, parent_dir);
		goto out;
	}

	if (is_left) {
		/* Parent will be to the right of the key. */
		right = avltree_container_of(parent, mdcache_dir_entry_t,
					     node_sorted);
		other = avltree_prev(parent);
		if (other) {
			left = avltree_container_of(other, mdcache_dir_entry_t,
						    node_sorted);
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
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
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Adding %s as new first entry",
						new_dir_entry->name);
			} else {
				/* The right entry is not the first entry in
				 * the directory, so the key is a dirent
				 * somewhere before the first chunked dirent.
				 * we can't insert this key into a chunk,
				 * however, we can still trust the chunks since
				 * the new entry is part of the directory we
				 * don't have cached, a readdir that wants that
				 * part of the directory will populate a new
				 * chunk.
				 */
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Could not add %s to chunk for directory %p, somewhere before first chunk",
						new_dir_entry->name,
						parent_dir);

				invalidate_chunks = false;
				goto out;
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
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
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
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Adding %s as new last entry",
						new_dir_entry->name);
			} else {
				/* The left entry is not the last entry in
				 * the directory, so the key is a dirent
				 * somewhere after the last chunked dirent.
				 * we can't insert this key into a chunk,
				 * however, we can still trust the chunks since
				 * the new entry is part of the directory we
				 * don't have cached, a readdir that wants that
				 * part of the directory will populate a new
				 * chunk.
				 */
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Could not add %s to chunk for directory %p, somewhere after last chunk",
						new_dir_entry->name,
						parent_dir);

				invalidate_chunks = false;
				goto out;
			}
		}
	}

	/* Note in the following, every dirent that is in the sorted tree MUST
	 * be in a chunk, so we don't check for chunk != NULL.
	 */
	/* Set up to add to chunk and by cookie AVL tree. */
	if (right == NULL) {
		/* Will go at end of left chunk. */
		chunk = new_dir_entry->chunk = left->chunk;
	} else {
		/* Will go at begin of right chunk. */
		chunk = new_dir_entry->chunk = right->chunk;
	}

	code = mdcache_avl_insert_ck(parent_dir, new_dir_entry);

	if (code < 0) {
		/* We failed to insert into FSAL cookie AVL tree, will fail.
		 * Nothing to clean up since we haven't done anything
		 * unreversible, and we no longer trust the chunks.
		 */
		goto out;
	}

	/* Get the node into the actual tree... */
	avltree_do_insert(&new_dir_entry->node_sorted,
			  &parent_dir->fsobj.fsdir.avl.sorted,
			  parent, unbalanced, is_left);

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Inserted %s into sorted tree left=%p right=%p",
			new_dir_entry->name, new_dir_entry->node_sorted.left,
			new_dir_entry->node_sorted.right);

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
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
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Fixup next_ck=%"PRIx64,
					left->chunk->next_ck);
		} else {
			/* New first entry in directory */
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Setting directory first_ck=%"PRIx64,
					new_dir_entry->ck);
			parent_dir->fsobj.fsdir.first_ck = new_dir_entry->ck;
		}
	}

	/* And now increment the number of entries in the chunk. */
	chunk->num_entries++;

	/* And bump the chunk in the LRU */
	lru_bump_chunk(chunk);

	if (chunk->num_entries == mdcache_param.dir.avl_chunk_split) {
		/* Create a new chunk */
		struct dir_chunk *split;
		struct glist_head *glist;
		mdcache_dir_entry_t *here = NULL;
		int i = 0;
		uint32_t split_count = mdcache_param.dir.avl_chunk_split / 2;

		split = mdcache_get_chunk(parent_dir, chunk, 0);
		split->next_ck = chunk->next_ck;

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Split next_ck=%"PRIx64,
				split->next_ck);

		/* Make sure this chunk is in the MRU of L1 */
		lru_bump_chunk(split);

		/* Scan the list to find what will be the first dirent in the
		 * new split chunk.
		 */
		glist_for_each(glist, &chunk->dirents) {
			if (++i > (split_count)) {
				/* Got past the halfway point. */
				here = glist_entry(glist,
						   mdcache_dir_entry_t,
						   chunk_list);
				break;
			}
		}

		assert(here != NULL);

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Splitting chunk %p for directory %p at %s",
				chunk, parent_dir, here->name);

		/* Split chunk->dirents into split->dirents at here */
		glist_split(&chunk->dirents, &split->dirents, glist);
		chunk->num_entries = split_count;
		split->num_entries = split_count;
		chunk->reload_ck = glist_last_entry(&chunk->dirents,
						    mdcache_dir_entry_t,
						    chunk_list)->ck;

		/* Fill in the first chunk's next_ck to be the cookie of the
		 * first dirent in the new split chunk.
		 */
		chunk->next_ck = here->ck;

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Chunk next_ck=%"PRIx64,
				chunk->next_ck);
	}

	new_dir_entry->flags |= DIR_ENTRY_SORTED;
	invalidate_chunks = false;

out:

	if (invalidate_chunks) {
		/* Indicate we not longer trust the chunk cache. */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Entry %p clearing MDCACHE_DIR_POPULATED, MDCACHE_TRUST_DIR_CHUNKS",
				parent_dir);
		atomic_clear_uint32_t_bits(&parent_dir->mde_flags,
					   MDCACHE_DIR_POPULATED |
					   MDCACHE_TRUST_DIR_CHUNKS);
	}

	if (new_dir_entry->chunk == NULL) {
		/* This is a detached directory entry, add it to the LRU list of
		 * detached directory entries. This is the one and only place a
		 * detached dirent can be added.
		 */
		add_detached_dirent(parent_dir, new_dir_entry);
	}
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
			 fsal_cookie_t cookie)
{
	struct mdcache_populate_cb_state *state = dir_state;
	struct dir_chunk *chunk = state->cur_chunk;
	mdcache_entry_t *mdc_parent = container_of(&state->dir->obj_handle,
						   mdcache_entry_t, obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	mdcache_entry_t *new_entry = NULL;
	mdcache_dir_entry_t *new_dir_entry = NULL, *allocated_dir_entry = NULL;
	size_t namesize = strlen(name) + 1;
	int code = 0;
	fsal_status_t status;
	enum fsal_dir_result result = DIR_CONTINUE;

#ifdef DEBUG_MDCACHE
	assert(mdc_parent->content_lock.__data.__cur_writer);
#endif

	if (chunk->num_entries == mdcache_param.dir.avl_chunk) {
		/* We are being called readahead. */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Readdir readahead first entry in new chunk %s",
				name);

		state->prev_chunk = chunk;
		state->prev_chunk->next_ck = cookie;

		/* Chunk is added to the chunks list before being passed in */
		/* Now start a new chunk, passing this chunk as prev_chunk. */
		chunk = mdcache_get_chunk(chunk->parent, chunk, 0);

		state->cur_chunk = chunk;

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Chunk %p Prev chunk %p next_ck=%" PRIx64,
				chunk, state->prev_chunk,
				state->prev_chunk->next_ck);
		/* And start accepting entries into the new chunk. */
	}

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Creating cache entry for %s cookie=0x%"PRIx64
			" sub_handle=0x%p",
			name, cookie, sub_handle);

	status = mdcache_new_entry(export, sub_handle, attrs_in, NULL,
				   false, &new_entry, NULL, MDC_REASON_SCAN);

	if (FSAL_IS_ERROR(status)) {
		*state->status = status;
		LogInfoAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			   "mdcache_new_entry failed on %s in dir %p with %s",
			   name, mdc_parent, fsal_err_txt(status));
		return DIR_TERMINATE;
	}

	/* Entry was found in the FSAL, add this entry to the parent directory
	 */

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
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

	memcpy(&new_dir_entry->name_buffer, name, namesize);
	new_dir_entry->name = new_dir_entry->name_buffer;
	mdcache_key_dup(&new_dir_entry->ckey, &new_entry->fh_hk.key);

	/* add to avl */
	code = mdcache_avl_insert(mdc_parent, &new_dir_entry);

	if (code < 0) {
		/* We can get here with the following possibilities:
		 *
		 * - FSAL cookie collision, nothing we can do about this, but
		 *   also really should never happen.
		 * - Name collision, something is broken and the FSAL has
		 *   given us multiple directory entries with the same name
		 *   but for different objects. Again, not much we can do.
		 *
		 * In any case, we will just ignore this entry.
		 */
		mdcache_put(new_entry);
		/* Check for return code -3 and/or -4.
		 * -3: This indicates the file name is duplicate but FSAL
		 * cookie is different. This may happen in case lots of new
		 * entries got added to the directory while running readdir.
		 * -4: This indicates that it is FSAL cookie duplication /
		 * collision. This could happen due to fast mutating directory.
		 * In both cases already cached contents are stale/invalid.
		 * Need to invalidate the cache and inform client to re-read
		 * the directory.
		 */
		if (code == -3 || code == -4) {
			atomic_clear_uint32_t_bits(&state->dir->mde_flags,
						   MDCACHE_TRUST_CONTENT);
			state->status->major = ERR_FSAL_DELAY;
			state->status->minor = 0;
			return DIR_TERMINATE;
		}
		LogCrit(COMPONENT_CACHE_INODE,
			"Collision while adding dirent for %s", name);
		return DIR_CONTINUE;
	}

	/* Note that if this dirent was already in the lookup by name AVL
	 * tree (mdc_parent->fsobj.fsdir.avl.t), then mdcache_avl_qp_insert
	 * freed the dirent we allocated above, and returned the one that was
	 * in tree. It will have set chunk, ck, and nk.
	 *
	 * The existing dirent might or might not be part of a chunk already.
	 */

	if (new_dir_entry != allocated_dir_entry) {
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Swapped %s using %p instead of %p, new_dir_entry->chunk=%p chunk=%p",
				new_dir_entry->name, new_dir_entry,
				allocated_dir_entry, new_dir_entry->chunk,
				chunk);
	}

	assert(new_dir_entry->chunk);

	if (state->whence_search && new_dir_entry->ck == state->cookie) {
		/* We have found the dirent the caller is looking for. */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Found dirent %s caller is looking for cookie = %"
				PRIx64, name, state->cookie);
		*(state->dirent) = new_dir_entry;
	}

	if (op_ctx->fsal_export->exp_ops.fs_supports(
			op_ctx->fsal_export, fso_compute_readdir_cookie)) {
		struct avltree_node *node;

		node = avltree_inline_insert(
					&new_dir_entry->node_sorted,
					&mdc_parent->fsobj.fsdir.avl.sorted,
					avl_dirent_sorted_cmpf);

		if (node != NULL) {
			if (node == &new_dir_entry->node_sorted) {
				LogDebugAlt(COMPONENT_NFS_READDIR,
					    COMPONENT_CACHE_INODE,
					    "New entry %s was already in sorted tree",
					    name);
			} else if (isDebug(COMPONENT_CACHE_INODE) ||
				   isDebug(COMPONENT_NFS_READDIR)) {
				mdcache_dir_entry_t *other;

				other = avltree_container_of(
					node, mdcache_dir_entry_t, node_sorted);
				LogDebugAlt(COMPONENT_NFS_READDIR,
					    COMPONENT_CACHE_INODE,
					    "New entry %s collided with entry %s already in sorted tree",
					    name, other->name);
			}
		} else {
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
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
		chunk->num_entries++;
	}

	if (new_dir_entry->chunk != chunk) {
		/* We have the situation where we have collided with a
		 * previously used chunk (and thus we have a partial chunk).
		 * Since dirent is pointing to the existing dirent and the one
		 * we allocated above has been freed we don't need to do any
		 * cleanup.
		 *
		 * Don't allow readahead in this case just indicate this
		 * directory is terminated.
		 */
		result = DIR_TERMINATE;

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Collision old chunk %p next_ck=%"PRIx64
				" new chunk %p next_ck=%"PRIx64,
				new_dir_entry->chunk,
				new_dir_entry->chunk->next_ck, chunk,
				chunk->next_ck);
		if (chunk->num_entries == 0) {

			LogFullDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
				    "Nuking empty Chunk %p", chunk);
			/* We read-ahead into an existing chunk, and this chunk
			 * is empty.  Just ditch it now, to avoid any issue. */
			mdcache_lru_unref_chunk(chunk, true);
			if (state->first_chunk == chunk) {
				/* Drop the first_chunk ref */
				mdcache_lru_unref_chunk(state->first_chunk,
						true);
				state->first_chunk = new_dir_entry->chunk;
				/* And take the first_chunk ref */
				mdcache_lru_ref_chunk(state->first_chunk);
			}
			chunk = new_dir_entry->chunk;
			state->cur_chunk = chunk;
			if (new_dir_entry->entry) {
				/* This was ref'd already; drop extra ref */
				mdcache_put(new_dir_entry->entry);
				new_dir_entry->entry = NULL;
			}
			if (state->prev_chunk) {
				state->prev_chunk->next_ck = new_dir_entry->ck;
			}
		} else {
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"keeping non-empty Chunk %p", chunk);
			chunk->next_ck = new_dir_entry->ck;
		}
	} else if (chunk->num_entries == mdcache_param.dir.avl_chunk) {
		/* Chunk is full. Since dirent is pointing to the existing
		 * dirent and the one we allocated above has been freed we don't
		 * need to do any cleanup.
		 *
		 * Allow readahead.
		 *
		 * If there's actually any readahead, chunk->next_ck will get
		 * filled in.
		 */
		result = DIR_READAHEAD;
	}

	if (new_entry->obj_handle.type == DIRECTORY) {
		/* Insert Parent's key */
		PTHREAD_RWLOCK_wrlock(&new_entry->content_lock);
		mdc_dir_add_parent(new_entry, mdc_parent);
		PTHREAD_RWLOCK_unlock(&new_entry->content_lock);
	}

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"About to put entry %p refcnt=%"PRIi32,
			new_entry,
			atomic_fetch_int32_t(&new_entry->lru.refcnt));

	/* Note that this entry is ref'd, so that mdcache_readdir_chunked can
	 * un-ref it.  Pass this ref off to the dir_entry for this purpose. */
	assert(!new_dir_entry->entry);
	new_dir_entry->entry = new_entry;

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
		       fsal_cookie_t cookie)
{
	struct mdcache_populate_cb_state *state = dir_state;
	enum fsal_dir_result result;

	/* This is in the middle of a subcall. Do a supercall */
	supercall_raw(state->export,
		result = mdc_readdir_chunk_object(name, sub_handle, attrs,
						  dir_state, cookie)
	);

	return result;
}

/**
 * @brief Skip directory chunks while re-filling dirent cache in search of
 *        a specific cookie that is not in cache.
 *
 * @note The content lock MUST be held for write
 *
 * @param[in] directory  The directory being read
 * @param[in] next_ck    The next cookie to find the next chunk
 *
 * @returns The chunk found, or NULL.
 */
static struct dir_chunk *mdcache_skip_chunks(mdcache_entry_t *directory,
					     fsal_cookie_t next_ck)
{
	mdcache_dir_entry_t *dirent = NULL;
	struct dir_chunk *chunk = NULL;

	/* We need to skip chunks that are already cached. */
	while (next_ck != 0 &&
	       mdcache_avl_lookup_ck(directory, next_ck, &dirent)) {
		chunk = dirent->chunk;
		mdcache_lru_unref_chunk(chunk, true);
		next_ck = chunk->next_ck;
	}

	/* At this point, we have the last cached chunk before a gap. */
	return chunk;
}

/**
 * @brief Read the next chunk of a directory
 *
 * If called for an FSAL that only supports whence as the dirent name to
 * continue from, and prev_chunk is NULL, we must scan the directory from the
 * beginning. If prev_chunk is not NULL, we can scan the directory starting with
 * the last dirent name in prev_chunk, but we must still scan the directory
 * until we find whence.
 *
 * @note this returns a ref on the chunk containing @a dirent
 *
 * @param[in] directory   The directory to read
 * @param[in] whence      Where to start (next)
 * @param[in,out] dirent  The first dirent of the chunk
 * @param[in] prev_chunk  The previous chunk populated
 * @param[in,out] eod_met The end of directory has been hit.
 *
 * @return FSAL status
 */

fsal_status_t mdcache_populate_dir_chunk(mdcache_entry_t *directory,
					 fsal_cookie_t whence,
					 mdcache_dir_entry_t **dirent,
					 struct dir_chunk *prev_chunk,
					 bool *eod_met)
{
	fsal_status_t status = {0, 0};
	fsal_status_t readdir_status = {0, 0};
	struct mdcache_populate_cb_state state;
	struct dir_chunk *chunk;
	attrmask_t attrmask;
	fsal_cookie_t *whence_ptr = &whence;

	chunk = mdcache_get_chunk(directory, prev_chunk, whence);

	attrmask = op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) | ATTR_RDATTR_ERR;

	/* Take a ref on the first chunk */
	mdcache_lru_ref_chunk(chunk);

	state.export = mdc_cur_export();
	state.dir = directory;
	state.status = &status;
	state.cb = NULL;  /* We don't use the call back during chunking. */
	state.first_chunk = state.cur_chunk = chunk;
	state.prev_chunk = prev_chunk;
	state.cookie = whence;
	state.dirent = dirent;
	state.whence_is_name = op_ctx->fsal_export->exp_ops.fs_supports(
				op_ctx->fsal_export, fso_whence_is_name);
	state.whence_search = state.whence_is_name && whence != 0 &&
							prev_chunk == NULL;

	if (state.whence_is_name) {
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"whence_is_name %s cookie %"
				PRIx64,
				state.whence_search ? "search" : "no search",
				state.cookie);
	}


again:

	/* In whence_is_name case, we may need to do another FSAL readdir
	 * call to continue scanning for the desired cookie, so we will jump
	 * back to here to accomplish that. chunk is newly allocated and
	 * prev_chunk has been updated to point to the last cached chunk.
	 */
	if (state.whence_is_name) {
		if (prev_chunk != NULL) {
			/* Start from end of prev_chunk */
			/* If end of directory, mark last dirent as eod. */
			mdcache_dir_entry_t *last;

			last = glist_last_entry(&prev_chunk->dirents,
						mdcache_dir_entry_t,
						chunk_list);
			whence_ptr = (fsal_cookie_t *)last->name;

			if (state.whence_search) {
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Calling FSAL readdir whence = %s, search %"
						PRIx64,
						last->name, state.cookie);
			} else {
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Calling FSAL readdir whence = %s, no search",
						last->name);
			}
		} else {
			/* Signal start from beginning by passing NULL pointer.
			 */
			whence_ptr = NULL;
			if (state.whence_search) {
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Calling FSAL readdir whence = NULL, search %"
						PRIx64, state.cookie);
			} else {
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Calling FSAL readdir whence = NULL, no search");
			}
		}
	} else {
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Calling FSAL readdir whence = 0x%"PRIx64,
				whence);
	}

#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_readdir_populate,
		   __func__, __LINE__, &directory->obj_handle,
		   directory->sub_handle, whence);
#endif
	subcall(
		readdir_status = directory->sub_handle->obj_ops->readdir(
			directory->sub_handle, whence_ptr, &state,
			mdc_readdir_chunked_cb, attrmask, eod_met)
	       );

	if (FSAL_IS_ERROR(readdir_status)) {
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			    "FSAL readdir status=%s",
			    fsal_err_txt(readdir_status));
		*dirent = NULL;
		mdcache_lru_unref_chunk(chunk, true);
		return readdir_status;
	}

	if (FSAL_IS_ERROR(status)) {
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			    "status=%s",
			    fsal_err_txt(status));
		*dirent = NULL;
		mdcache_lru_unref_chunk(chunk, true);
		return status;
	}

	/* Recover the most recent chunk from cur_chunk, if we had readahead.
	 * it might have changed.
	 */
	chunk = state.cur_chunk;

	if (chunk->num_entries == 0) {
		/* Chunk is empty - should only happen for an empty directory
		 * but could happen if the FSAL failed to indicate end of
		 * directory. This COULD happen on a readahead chunk, but it
		 * would be unusual.
		 */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Empty chunk");

		mdcache_lru_unref_chunk(chunk, true);

		if (chunk == state.first_chunk) {
			/* We really got nothing on this readdir, so don't
			 * return a dirent.
			 */
			*dirent = NULL;
			mdcache_lru_unref_chunk(chunk, true);
			LogDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
				    "status=%s",
				    fsal_err_txt(status));
			return status;
		}

		/* If the empty chunk wasn't first, then prev_chunk is valid */
		chunk = state.prev_chunk;
	}

	if (*eod_met) {
		/* If end of directory, mark last dirent as eod. */
		mdcache_dir_entry_t *last;

		last = glist_last_entry(&chunk->dirents, mdcache_dir_entry_t,
					chunk_list);
		last->eod = true;
	}

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Chunk first entry %s%s",
			*dirent != NULL ? (*dirent)->name : "<NONE>",
			*eod_met ? " EOD" : "");

	if (state.whence_search && *dirent == NULL) {
		if (*eod_met) {
			/* Did not find cookie. */
			status = fsalstat(ERR_FSAL_BADCOOKIE, 0);
			LogDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
				    "Could not find search cookie status=%s",
				    fsal_err_txt(status));
			return status;
		}

		/* We are re-scanning directory, and we have not found our
		 * cookie yet, we either used up the FSAL's readdir (with any
		 * readahead) or we collided with an already cached chunk,
		 * which we know DOES NOT have our cookie (because otherwise we
		 * would have found it on lookup), so we will start from where
		 * we left off.
		 *
		 * chunk points to the last valid chunk of what we just read,
		 * but we also have to check if we must skip chunks that had
		 * already been in cache.
		 *
		 * If chunk->next_ck is 0, then we didn't collide, so there are
		 * no chunks to skip.
		 */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Rescan dir to find cookie needs to continue search for %"
				PRIx64, state.cookie);

		if (chunk->next_ck != 0) {
			/* In the collision case, chunk->next_ck was set,
			 * so now start skipping.
			 */
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Search skipping from cookie %"PRIx64,
					chunk->next_ck);
			chunk = mdcache_skip_chunks(directory, chunk->next_ck);
		}

		/* We need to start a new FSAL readdir call, but we don't just
		 * want to call mdcache_populate_dir_chunk raw, so set up a few
		 * things and jump to again...
		 */
		/* The chunk we just dealt with is now prev_chunk. */
		prev_chunk = chunk;

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"About to allocate a new chunk to continue search, prev chunk = %p",
				prev_chunk);

		/* And we need to allocate a fresh chunk. */
		chunk = mdcache_get_chunk(directory, chunk, 0);

		/* And switch over to new chunk. */
		state.cur_chunk = chunk;
		state.prev_chunk = prev_chunk;

		/* And go start a new FSAL readdir call.  */
		goto again;
	}

	if (*dirent == NULL) {
		/* We haven't set dirent yet, return the first entry of the
		 * first chunk.
		 */
		*dirent = glist_first_entry(&state.first_chunk->dirents,
					    mdcache_dir_entry_t,
					    chunk_list);
	}

	LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
		    "status=%s",
		    fsal_err_txt(status));

	return status;
}

/**
 * @brief Read the contents of a directory
 *
 * If necessary, populate dirent cache chunks from the underlying FSAL. Then,
 * walk the dirent cache chunks calling the callback.
 *
 * Interactions between readdir and entry LRU lifetime is complicated.  We want
 * the LRU to be scan resistant, so that readdir() doesn't empty useful entries
 * from the LRU.  However, the readdir() has to work in such a way that it's
 * entries are still in the cache when they're used.  To achieve this, we do two
 * things:
 *
 * First, we insert objects created during a scan into the MRU of L2, rather
 * than the LRU of L1.  This allows them to be recycled in FIFO order rather
 * than LIFO order.  Observed behavior was that, when we are over the hi-water
 * mark, readdir() of a large directory would empty the L2 by recycling entries.
 * Then, it would start recycling the LRU of L1.  However, the LRU of L1
 * contained entries created during the readdir().  This means that, after the
 * chunk loaded, and it's entries need to be returned to the upper layer, they
 * have been recycled, and need to be re-created via a lookup() and getattr()
 * pair, causing large numbers of round-trips to the cluster.  Inserting into
 * the MRU of L2 keeps the L2 from being emptied, and causes the entries to be
 * recycled FIFO, making it likely that the entries for a chunk are still in the
 * cache when needed.
 *
 * The second important thing to do is to *not* take an INITIAL ref on entries
 * when they are used during the scan.  An INITIAL ref promotes the entry in the
 * LRU, which would put it at LRU of L1, recreating the above situation.  To
 * avoid this, and keep scan resistance, we take a non-initial ref during
 * readdir().
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
	bool first_pass = true;
	bool eod = false;
	bool reload_chunk = false;

#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_readdir,
		   __func__, __LINE__, &directory->obj_handle);
#endif
	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			"Starting chunked READDIR for %p, MDCACHE_TRUST_CONTENT %s, MDCACHE_TRUST_DIR_CHUNKS %s",
			directory,
			test_mde_flags(directory, MDCACHE_TRUST_CONTENT)
				? "true" : "false",
			test_mde_flags(directory, MDCACHE_TRUST_DIR_CHUNKS)
				? "true" : "false");

	/* Dirent's are being chunked; check to see if it needs updating */
	if (!test_mde_flags(directory, MDCACHE_TRUST_CONTENT |
				       MDCACHE_TRUST_DIR_CHUNKS)) {
		/* Clean out the existing entries in the directory. */
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Flushing invalid dirent cache");
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

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
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
			first_pass = true;
			chunk = NULL;
			goto again;
		}

		/* Assure that dirent is NULL */
		dirent = NULL;

		if (look_ck != 0 &&
		    look_ck == directory->fsobj.fsdir.first_ck) {
			/* We failed to find the first dentry in the directory,
			 * and will load this chunk.  Make sure we save
			 * whatever is the new first_ck. */
			set_first_ck = true;
		}

		if (op_ctx->fsal_export->exp_ops.fs_supports(
				op_ctx->fsal_export, fso_whence_is_name)
		    && first_pass && directory->fsobj.fsdir.first_ck != 0) {
			/* If whence must be the directory entry name we wish
			 * to continue from, we need to start at the beginning
			 * of the directory and readdir until we find the
			 * caller's cookie, but we have the beginning of the
			 * directory cached, so skip any chunks cached from
			 * the start.
			 *
			 * Since the chunk we pass to
			 * mdcache_populate_dir_chunk is the previous chunk
			 * that function will use the chunk we resolved to
			 * fetch the dirent name to continue from.
			 *
			 * If we DID NOT HAVE at least the first chunk cached,
			 * mdcache_populate_dir_chunk MUST start from the
			 * beginning, this is signaled by the fact that
			 * prev_chunk will be NULL.
			 *
			 * In any case, whence will be the cookie we are looking
			 * for.
			 */
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Search skipping initial chunks to find cookie");
			chunk = mdcache_skip_chunks(
				directory, directory->fsobj.fsdir.first_ck);
			/* Since first_ck was not 0, we MUST have found at least
			 * one chunk...
			 */
			assert(chunk != NULL);
		}

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Readdir chunked about to populate chunk %p next_ck=0x%"
				PRIx64, chunk, next_ck);

		/* No, we need to populate a chunk using this cookie.
		 *
		 * NOTE: empty directory can result in dirent being NULL, and
		 *       we will ALWAYS re-read an empty directory every time.
		 *       Although we do end up setting MDCACHE_DIR_POPULATED on
		 *       an empty directory, we don't consider that here, and
		 *       will re-read the directory.
		 */
		status = mdcache_populate_dir_chunk(directory, next_ck,
						    &dirent, chunk, &eod);

		if (FSAL_IS_ERROR(status)) {
			PTHREAD_RWLOCK_unlock(&directory->content_lock);

			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
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

			if (whence == 0) {
				/* Since eod is true and whence is 0, we know
				 * the entire directory is populated. This can
				 * indicate that an empty directory may be
				 * considered "populated."
				 */
				atomic_set_uint32_t_bits(&directory->mde_flags,
							 MDCACHE_DIR_POPULATED);
			}

			PTHREAD_RWLOCK_unlock(&directory->content_lock);

			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"readdir completed, eod = %s",
					*eod_met ? "true" : "false");

			return status;
		}

		if ((whence == 0) && eod) {
			/* We started at the beginning of the directory and
			 * populated through to end of directory, thus we can
			 * indicate the directory is fully populated.
			 */
			atomic_set_uint32_t_bits(&directory->mde_flags,
						 MDCACHE_DIR_POPULATED);
		} else {
			/* Since we just populated a chunk and have not
			 * determined that we read the entire directory, make
			 * sure the MDCACHE_DIR_POPULATED is cleared.
			 */
			atomic_clear_uint32_t_bits(&directory->mde_flags,
						   MDCACHE_DIR_POPULATED);
		}

		chunk = dirent->chunk;

		LogFullDebugAlt(COMPONENT_NFS_READDIR,
				COMPONENT_CACHE_INODE,
				"mdcache_populate_dir_chunk finished chunk %p dirent %p %s",
				chunk, dirent, dirent->name);

		if (set_first_ck) {
			/* We just populated the first dirent in the directory,
			 * save it's cookie as first_ck.
			 */
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
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
		LogFullDebugAlt(COMPONENT_NFS_READDIR,
				COMPONENT_CACHE_INODE,
				"found dirent in cached chunk %p dirent %p %s",
				chunk, dirent, dirent->name);
	}

	/* Bump the chunk in the LRU */
	lru_bump_chunk(chunk);

	/* We can drop the ref now, we've bumped.  This cannot be the last ref
	 * drop.  To get here, we had at least 2 refs, and we also hold the
	 * content_lock for at least read.  This means noone holds it for write,
	 * and all final ref drops are done with it held for write. */
	mdcache_lru_unref_chunk(chunk, true);

	LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
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

		if (dirent->flags & DIR_ENTRY_FLAG_DELETED) {
			/* Skip deleted entries */
			continue;
		}

		status.major = ERR_FSAL_NO_ERROR;
		/* We have the content_lock for at least read. */
		if (dirent->entry) {
			/* Take a ref for our use */
			entry = dirent->entry;
			mdcache_get(entry);
		} else {
			/* Not cached, get actual entry using the dirent ckey */
			status = mdcache_find_keyed_reason(&dirent->ckey,
							   &entry,
							   MDC_REASON_SCAN);
		}

		if (FSAL_IS_ERROR(status)) {
			/* Failed using ckey, do full lookup. */
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Lookup by key for %s failed, lookup by name now",
					dirent->name);

			/* mdc_lookup_uncached needs write lock, dropping the
			 * read lock means we can no longer trust the dirent or
			 * the chunk.
			 */
			if (!has_write) {
				/* We will have to re-find this dirent after we
				 * re-acquire the lock.
				 */
				look_ck = dirent->ck;

				PTHREAD_RWLOCK_unlock(&directory->content_lock);
				PTHREAD_RWLOCK_wrlock(&directory->content_lock);
				has_write = true;

				/* Dropping the content_lock may have
				 * invalidated some or all of the dirents and/or
				 * chunks in this directory.  We need to start
				 * over from this point.  look_ck is now correct
				 * if the dirent is still cached, and we haven't
				 * changed next_ck, so it's still correct for
				 * reloading the chunk.
				 */
				first_pass = true;
				chunk = NULL;

				/* Now we need to look for this dirent again.
				 * We haven't updated next_ck for this dirent
				 * yet, so it is the right whence to use for a
				 * repopulation readdir if the chunk is
				 * discarded.
				 */
				goto again;
			} else if (op_ctx->fsal_export->exp_ops.fs_supports(
				   op_ctx->fsal_export, fso_readdir_plus)) {

				/* If the FSAL supports readdir_plus, then a
				 * single round-trip for the chunk is preferable
				 * to lookups for every missing obj.  Nuke the
				 * chunk, and reload it using readdir_plus */
				look_ck = dirent->ck;
				next_ck = chunk->reload_ck;
				reload_chunk = true;
				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"Reloading chunk %p look_ck %"
						PRIx64" next_ck %"PRIx64,
						chunk, look_ck, next_ck);
				/* In order to get here, we passed the has_write
				 * check above, and took the write lock. */
				mdcache_lru_unref_chunk(chunk, true);
				chunk = NULL;
				goto again;
			}

			status = mdc_lookup_uncached(directory, dirent->name,
						     &entry, NULL);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&directory->content_lock);

				LogFullDebugAlt(COMPONENT_NFS_READDIR,
						COMPONENT_CACHE_INODE,
						"lookup by name failed status=%s",
						fsal_err_txt(status));

				if (status.major == ERR_FSAL_STALE)
					mdcache_kill_entry(directory);

				return status;
			}
		}

		if (has_write && dirent->entry) {
			/* If we get here, we have the write lock, have an
			 * entry, and took a ref on it above.  The dirent also
			 * has a ref on the entry.  Drop that ref now.  This can
			 * only be done under the write lock.  If we don't have
			 * the write lock, then this was not the readdir that
			 * took the ref, and another readdir will drop the ref,
			 * or it will be dropped when the dirent is cleaned up.
			 * */
			mdcache_put(dirent->entry);
			dirent->entry = NULL;
		}

		if (reload_chunk && look_ck != 0 && dirent->ck !=
			   look_ck) {
			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"Skipping already used dirent %s (%p)",
					dirent->name, &entry->obj_handle);
			/* This chunk was reloaded, but some dirents were
			 * already consumed.  Deref and continue */
			mdcache_put(entry);
			continue;
		}

		if (dirent->ck == whence) {
			/* When called with whence, the caller always wants the
			 * next entry, skip this entry. */
			mdcache_put(entry);
			continue;
		}

		next_ck = dirent->ck;
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Setting next_ck=%"PRIx64,
				next_ck);

		/* Ensure the attribute cache is valid.  The simplest way to do
		 * this is to call getattrs().  We need a copy anyway, to ensure
		 * thread safety.
		 */
		fsal_prepare_attrs(&attrs, attrmask);

		status = entry->obj_handle.obj_ops->getattrs(&entry->obj_handle,
							    &attrs);
		if (FSAL_IS_ERROR(status)) {
			PTHREAD_RWLOCK_unlock(&directory->content_lock);

			LogFullDebugAlt(COMPONENT_NFS_READDIR,
					COMPONENT_CACHE_INODE,
					"getattrs failed status=%s",
					fsal_err_txt(status));

			mdcache_put(entry);
			return status;
		}

#ifdef USE_LTTNG
		tracepoint(mdcache, mdc_readdir_cb,
			   __func__, __LINE__, dirent->name, &entry->obj_handle,
			   entry->sub_handle, entry->lru.refcnt);
#endif
		cb_result = cb(dirent->name, &entry->obj_handle, &entry->attrs,
			       dir_state, dirent->ck);

		fsal_release_attrs(&attrs);

		/* The ref on entry was put by the callback.  Don't use it
		 * anymore */

		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"dirent = %p %s, cb_result = %s, eod = %s",
				dirent, dirent->name,
				fsal_dir_result_str(cb_result),
				dirent->eod ? "true" : "false");

		if (cb_result >= DIR_TERMINATE || dirent->eod) {
			/* Caller is done, or we have reached the end of
			 * the directory, no need to get another dirent.
			 */

			/* If cb_result is DIR_TERMINATE, the callback did
			 * not consume this entry, so we can not have reached
			 * end of directory.
			 */
			*eod_met = cb_result != DIR_TERMINATE && dirent->eod;

			if (*eod_met && whence == 0) {
				/* Since eod is true and whence is 0, we know
				 * the entire directory is populated.
				 */
				atomic_set_uint32_t_bits(&directory->mde_flags,
							 MDCACHE_DIR_POPULATED);
			}

			LogDebugAlt(COMPONENT_NFS_READDIR,
				    COMPONENT_CACHE_INODE,
				    "readdir completed, eod = %s",
				    *eod_met ? "true" : "false");

			PTHREAD_RWLOCK_unlock(&directory->content_lock);

			return status;
		}

		reload_chunk = false;
	}

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
		LogFullDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
				"Setting look_ck from next_ck=%"PRIx64,
				chunk->next_ck);
	} else {
		/* The next chunk is not resident, or we don't know what the
		 * next_ck is. Skip right to populating the next chunk. next_ck
		 * is the right cookie to use as the whence for the next
		 * readdir.
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
	first_pass = false;
	goto again;
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
#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_kill_entry,
		   function, line, &entry->obj_handle, entry->lru.refcnt,
		   freed);
#endif

	if (!freed) {
		/* queue for cleanup */
		mdcache_lru_cleanup_push(entry);
	}

}

/**
 * @brief Update the cached attributes
 *
 * Update the cached attributes on @a entry with the attributes in @a attrs
 *
 * @note The caller must hold the attribute lock for WRITE
 *
 * @param[in] entry	Entry to update
 * @param[in] attrs	New attributes to cache
 * @return FSAL status
 */
void mdc_update_attr_cache(mdcache_entry_t *entry, struct attrlist *attrs)
{
	if (entry->attrs.acl != NULL) {
		/* We used to have an ACL... */
		if (attrs->acl != NULL) {
			/* We got an ACL from the sub FSAL whether we asked for
			 * it or not, given that we had an ACL before, and we
			 * got a new one, update the ACL, so release the old
			 * one.
			 */
			nfs4_acl_release_entry(entry->attrs.acl);
		} else {
			/* A new ACL wasn't provided, so move the old one
			 * into the new attributes so it will be preserved
			 * by the fsal_copy_attrs.
			 */
			attrs->acl = entry->attrs.acl;
			attrs->valid_mask |= ATTR_ACL;
		}

		/* NOTE: Because we already had an ACL,
		 * entry->attrs.request_mask MUST have the ATTR_ACL bit set.
		 * This will assure that fsal_copy_attrs below will copy the
		 * selected ACL (old or new) into entry->attrs.
		 */

		/* ACL was released or moved to new attributes. */
		entry->attrs.acl = NULL;
	} else if (attrs->acl != NULL) {
		/* We didn't have an ACL before, but we got a new one. We may
		 * not have asked for it, but receive it anyway.
		 */
		entry->attrs.request_mask |= ATTR_ACL;
	}

	// Same as above but for fs_locations
	if (entry->attrs.fs_locations != NULL) {
		if (attrs->fs_locations != NULL) {
			nfs4_fs_locations_release(entry->attrs.fs_locations);
		} else {
			attrs->fs_locations = entry->attrs.fs_locations;
			attrs->valid_mask |= ATTR4_FS_LOCATIONS;
		}

		entry->attrs.fs_locations = NULL;
	} else if (attrs->fs_locations != NULL) {
		entry->attrs.request_mask |= ATTR4_FS_LOCATIONS;
	}

	// Same as above but for sec_label
	if (entry->attrs.sec_label.slai_data.slai_data_val != NULL) {
		char *secdata = entry->attrs.sec_label.slai_data.slai_data_val;

		if (attrs->sec_label.slai_data.slai_data_val != NULL) {
			gsh_free(secdata);
		} else {
			attrs->sec_label.slai_data.slai_data_len =
				entry->attrs.sec_label.slai_data.slai_data_len;
			attrs->sec_label.slai_data.slai_data_val = secdata;
			attrs->valid_mask |= ATTR4_SEC_LABEL;
		}

		entry->attrs.sec_label.slai_data.slai_data_len = 0;
		entry->attrs.sec_label.slai_data.slai_data_val = NULL;
	} else if (attrs->sec_label.slai_data.slai_data_val != NULL) {
		entry->attrs.request_mask |= ATTR4_SEC_LABEL;
	}

	if (attrs->expire_time_attr == 0) {
		/* FSAL did not set this, retain what was in the entry. */
		attrs->expire_time_attr = entry->attrs.expire_time_attr;
	}

	/* Now move the new attributes into the entry. */
	fsal_copy_attrs(&entry->attrs, attrs, true);

	/* Note that we use &entry->attrs here in case attrs.request_mask was
	 * modified by the FSAL. entry->attrs.request_mask reflects the
	 * attributes we requested, and was updated to "request" ACL if the
	 * FSAL provided one for us gratis.
	 */
	mdc_fixup_md(entry, &entry->attrs);
}

/** @} */
