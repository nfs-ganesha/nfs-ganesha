/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2018 Red Hat, Inc. and/or its affiliates.
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

/* handle.c
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "nfs4_acls.h"
#include "nfs_exports.h"
#include "sal_functions.h"
#include <os/subr.h>

#include "mdcache_lru.h"
#include "mdcache_hash.h"
#include "mdcache_avl.h"

/*
 * handle methods
 */

/**
 * Attempts to create a new mdcache handle, or cleanup memory if it fails.
 *
 * This function is a wrapper of mdcache_alloc_handle. It adds error checking
 * and logging. It also cleans objects allocated in the subfsal if it fails.
 *
 * @note the caller must hold the content lock on the parent.
 *
 * This does not cause an ABBA lock conflict with the potential getattrs
 * if we lose a race to create the cache entry since our caller CAN NOT hold
 * any locks on the cache entry created.
 *
 * Invalidate can be changed from true to false if mdcache is able to add
 * the new dirent to a chunk. In this case, the caller MUST refresh the
 * parent's attributes (we can't do it here due to lock ordering) in a way that
 * does not invalidate the dirent cache.
 *
 * @param[in]     export         The mdcache export used by the handle.
 * @param[in,out] sub_handle     The handle used by the subfsal.
 * @param[in]     fs             The filesystem of the new handle.
 * @param[in]     new_handle     Address where the new allocated pointer should
 *                               be written.
 * @param[in]     new_directory  Indicates a new directory has been created.
 * @param[in,out] attrs_out      Optional attributes for newly created object.
 * @param[in]     parent         Parent directory to add dirent to.
 * @param[in]     name           Name of the dirent to add.
 * @param[in,out] invalidate     Invalidate parent attr (and chunk cache)
 * @param[in]     state          Optional state_t representing open file.
 *
 * @note This returns an INITIAL ref'd entry on success
 *
 * @return An error code for the function.
 */
fsal_status_t mdcache_alloc_and_check_handle(
		struct mdcache_fsal_export *export,
		struct fsal_obj_handle *sub_handle,
		struct fsal_obj_handle **new_obj,
		bool new_directory,
		struct attrlist *attrs_in,
		struct attrlist *attrs_out,
		const char *tag,
		mdcache_entry_t *parent,
		const char *name,
		bool *invalidate,
		struct state_t *state)
{
	fsal_status_t status;
	mdcache_entry_t *new_entry;

	status = mdcache_new_entry(export, sub_handle, attrs_in, attrs_out,
				   new_directory, &new_entry, state,
				   MDC_REASON_DEFAULT);

	if (FSAL_IS_ERROR(status)) {
		*new_obj = NULL;
		return status;
	}

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "%sCreated entry %p FSAL %s for %s",
		     tag, new_entry, new_entry->sub_handle->fsal->name,
		     name);

	if (*invalidate) {
		/* This function is called after a create, so go ahead
		 * and invalidate the parent directory attributes.
		 */
		atomic_clear_uint32_t_bits(&parent->mde_flags,
					   MDCACHE_TRUST_ATTRS);
	}

	if (mdcache_param.dir.avl_chunk != 0) {
		/* Add this entry to the directory (also takes an internal ref)
		 */
		status = mdcache_dirent_add(parent, name, new_entry,
					    invalidate);

		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "%s%s failed because add dirent failed",
				 tag, name);

			mdcache_put(new_entry);
			*new_obj = NULL;
			return status;
		}
	}

	if (new_entry->obj_handle.type == DIRECTORY) {
		/* Insert Parent's key */
		PTHREAD_RWLOCK_wrlock(&new_entry->content_lock);
		mdc_dir_add_parent(new_entry, parent);
		PTHREAD_RWLOCK_unlock(&new_entry->content_lock);
	}

	*new_obj = &new_entry->obj_handle;

	if (attrs_out != NULL) {
		LogAttrlist(COMPONENT_CACHE_INODE, NIV_FULL_DEBUG,
			    tag, attrs_out, true);
	}

	return status;
}

/**
 * @brief Lookup a name
 *
 * Lookup a name relative to another object
 *
 * @param[in]     parent    Handle of parent
 * @param[in]     name      Name to look up
 * @param[out]    handle    Handle of found object, on success
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t mdcache_lookup(struct fsal_obj_handle *parent,
				    const char *name,
				    struct fsal_obj_handle **handle,
				    struct attrlist *attrs_out)
{
	mdcache_entry_t *mdc_parent =
		container_of(parent, mdcache_entry_t, obj_handle);
	mdcache_entry_t *entry = NULL;
	fsal_status_t status;

	*handle = NULL;

	status = mdc_lookup(mdc_parent, name, true, &entry, attrs_out);
	if (entry)
		*handle = &entry->obj_handle;

	return status;
}

/**
 * @brief Make a directory
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] name	Name for new directory
 * @param[in] attrib	Attributes for new directory
 * @param[out] handle	Resulting handle on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t mdcache_mkdir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attrib,
			     struct fsal_obj_handle **handle,
			     struct attrlist *attrs_out)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t,
			     obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct fsal_obj_handle *sub_handle;
	fsal_status_t status;
	struct attrlist attrs;
	bool invalidate = true;

	*handle = NULL;

	/* Ask for all supported attributes except ACL (we defer fetching ACL
	 * until asked for it (including a permission check).
	 */
	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) & ~ATTR_ACL);

	subcall_raw(export,
		status = parent->sub_handle->obj_ops->mkdir(
			parent->sub_handle, name, attrib, &sub_handle, &attrs)
	       );

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "mkdir %s failed with %s",
			 name, fsal_err_txt(status));
		if (status.major == ERR_FSAL_STALE) {
			/* If we got ERR_FSAL_STALE, the previous FSAL call
			 * must have failed with a bad parent.
			 */
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE on mkdir");
			mdcache_kill_entry(parent);
		}
		*handle = NULL;
		fsal_release_attrs(&attrs);
		return status;
	}

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);

	status = mdcache_alloc_and_check_handle(export, sub_handle, handle,
						true, &attrs, attrs_out,
						"mkdir ",  parent, name,
						&invalidate, NULL);

	PTHREAD_RWLOCK_unlock(&parent->content_lock);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_SUCCESS(status) && !invalidate) {
		/* Refresh destination directory attributes without
		 * invalidating dirents.
		 */
		status = mdcache_refresh_attrs_no_invalidate(parent);
	}

	return status;
}

/**
 * @brief Make a device node
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] name	Name of new node
 * @param[in] nodetype	Type of new node
 * @param[in] attrib	Attributes for new node
 * @param[out] handle	New object handle on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t mdcache_mknode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t,
			     obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct fsal_obj_handle *sub_handle;
	fsal_status_t status;
	struct attrlist attrs;
	bool invalidate = true;

	*handle = NULL;

	/* Ask for all supported attributes except ACL (we defer fetching ACL
	 * until asked for it (including a permission check).
	 */
	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) & ~ATTR_ACL);

	subcall_raw(export,
		status = parent->sub_handle->obj_ops->mknode(
			parent->sub_handle, name, nodetype, attrib,
			&sub_handle, &attrs)
	       );

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "mknod %s failed with %s",
			 name, fsal_err_txt(status));
		if (status.major == ERR_FSAL_STALE) {
			/* If we got ERR_FSAL_STALE, the previous FSAL call
			 * must have failed with a bad parent.
			 */
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE on mknod");
			mdcache_kill_entry(parent);
		}
		*handle = NULL;
		fsal_release_attrs(&attrs);
		return status;
	}

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);

	status = mdcache_alloc_and_check_handle(export, sub_handle, handle,
						false, &attrs, attrs_out,
						"mknode ",  parent, name,
						&invalidate, NULL);

	PTHREAD_RWLOCK_unlock(&parent->content_lock);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_SUCCESS(status) && !invalidate) {
		/* Refresh destination directory attributes without
		 * invalidating dirents.
		 */
		status = mdcache_refresh_attrs_no_invalidate(parent);
	}

	return status;
}

/**
 * @brief Make a symlink
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] name	Name of new node
 * @param[in] link_path	Contents of symlink
 * @param[in] attrib	Attributes for new simlink
 * @param[out] handle	New object handle on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t mdcache_symlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t,
			     obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct fsal_obj_handle *sub_handle;
	fsal_status_t status;
	struct attrlist attrs;
	bool invalidate = true;

	*handle = NULL;

	/* Ask for all supported attributes except ACL (we defer fetching ACL
	 * until asked for it (including a permission check).
	 */
	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) & ~ATTR_ACL);

	subcall_raw(export,
		status = parent->sub_handle->obj_ops->symlink(
			parent->sub_handle, name, link_path, attrib,
			&sub_handle, &attrs)
	       );

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "symlink %s failed with %s",
			 name, fsal_err_txt(status));
		if (status.major == ERR_FSAL_STALE) {
			/* If we got ERR_FSAL_STALE, the previous FSAL call
			 * must have failed with a bad parent.
			 */
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE on symlink");
			mdcache_kill_entry(parent);
		}
		*handle = NULL;
		fsal_release_attrs(&attrs);
		return status;
	}

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);

	status = mdcache_alloc_and_check_handle(export, sub_handle, handle,
						false, &attrs, attrs_out,
						"symlink ",  parent, name,
						&invalidate, NULL);

	PTHREAD_RWLOCK_unlock(&parent->content_lock);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_SUCCESS(status) && !invalidate) {
		/* Refresh destination directory attributes without
		 * invalidating dirents.
		 */
		status = mdcache_refresh_attrs_no_invalidate(parent);
	}

	return status;
}

/**
 * @brief Read a symlink
 *
 * @param[in] obj_hdl	Handle for symlink
 * @param[out] link_content	Buffer to fill with link contents
 * @param[in] refresh	If true, refresh attributes on symlink
 * @return FSAL status
 */
static fsal_status_t mdcache_readlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	PTHREAD_RWLOCK_rdlock(&entry->content_lock);
	if (!refresh && !test_mde_flags(entry, MDCACHE_TRUST_CONTENT)) {
		/* Our data are stale.  Drop the lock, get a
		   write-lock, load in new data, and copy it out to
		   the caller. */
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		/* Make sure nobody updated the content while we were
		   waiting. */
		refresh = !test_mde_flags(entry, MDCACHE_TRUST_CONTENT);
	}

	subcall(
		status = entry->sub_handle->obj_ops->readlink(
			entry->sub_handle, link_content, refresh)
	       );

	if (refresh && !(FSAL_IS_ERROR(status)))
		atomic_set_uint32_t_bits(&entry->mde_flags,
					 MDCACHE_TRUST_CONTENT);

	PTHREAD_RWLOCK_unlock(&entry->content_lock);

	return status;
}

/**
 * @brief Create a hard link
 *
 * @param[in] obj_hdl	Object to link to.
 * @param[in] destdir_hdl	Destination dirctory into which to link
 * @param[in] name	Name of new link
 * @return FSAL status
 */
static fsal_status_t mdcache_link(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	mdcache_entry_t *dest =
		container_of(destdir_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;
	bool invalidate = true;

	subcall(
		status = entry->sub_handle->obj_ops->link(
			entry->sub_handle, dest->sub_handle, name)
	       );

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "link failed %s",
			     fsal_err_txt(status));
		return status;
	}

	if (mdcache_param.dir.avl_chunk != 0) {
		PTHREAD_RWLOCK_wrlock(&dest->content_lock);

		/* Add this entry to the directory (also takes an internal ref)
		 */
		status = mdcache_dirent_add(dest, name, entry, &invalidate);

		PTHREAD_RWLOCK_unlock(&dest->content_lock);
	}

	/* Invalidate attributes, so refresh will be forced */
	atomic_clear_uint32_t_bits(&entry->mde_flags, MDCACHE_TRUST_ATTRS);

	if (FSAL_IS_SUCCESS(status) && !invalidate) {
		/* Refresh destination directory attributes without
		 * invalidating dirents.
		 */
		status = mdcache_refresh_attrs_no_invalidate(dest);
	}

	return status;
}

/**
 * Read the contents of a dirctory
 *
 * If necessary, populate the dirent cache from the underlying FSAL.  Then, walk
 * the dirent cache calling the callback.
 *
 * @note The object passed into the callback is ref'd and must be unref'd by the
 * callback.
 *
 * @param[in] dir_hdl the directory to read
 * @param[in] whence where to start (next)
 * @param[in] dir_state pass thru of state to callback
 * @param[in] cb callback function
 * @param[in] attrmask Which attributes to fill
 * @param[out] eod_met eod marker true == end of dir
 *
 * @return FSAL status
 */
static fsal_status_t mdcache_readdir(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eod_met)
{
	mdcache_entry_t *directory = container_of(dir_hdl, mdcache_entry_t,
						  obj_handle);
	if (!(directory->obj_handle.type == DIRECTORY))
		return fsalstat(ERR_FSAL_NOTDIR, 0);

	if (mdcache_param.dir.avl_chunk == 0) {
		/* Not caching dirents; pass through directly to FSAL */
		return mdcache_readdir_uncached(directory, whence, dir_state,
						cb, attrmask, eod_met);
	} else {
		/* Dirent chunking is enabled. */
		LogDebugAlt(COMPONENT_NFS_READDIR, COMPONENT_CACHE_INODE,
			    "Calling mdcache_readdir_chunked whence=%"PRIx64,
			    whence ? *whence : (uint64_t) 0);

		return mdcache_readdir_chunked(directory,
					       whence ? *whence : (uint64_t) 0,
					       dir_state, cb, attrmask,
					       eod_met);
	}
}

/**
 * @brief Check access for a given user against a given object
 *
 * Currently, all FSALs use the default method.  We call the default method
 * directly, so that the test uses cached attributes, rather than having the
 * lower level need to query attributes each call.  This works as long as all
 * FSALs call the default method.  This should be revisited if a FSAL wants to
 * override test_access().
 *
 * @note If @a owner_skip is provided, we test against the cached owner.  This
 * is because doing a getattrs() potentially on each read and write (writes
 * invalidate cached attributes) is a huge performance hit.  Eventually, finer
 * grained attribute validity would be a better solution
 *
 * @param[in] obj_hdl     Handle to check
 * @param[in] access_type Access requested
 * @param[out] allowed    Returned access that could be granted
 * @param[out] denied     Returned access that would be granted
 * @param[in] owner_skip  Skip test if op_ctx->creds is owner
 *
 * @return FSAL status.
 */
static fsal_status_t mdcache_test_access(struct fsal_obj_handle *obj_hdl,
					 fsal_accessflags_t access_type,
					 fsal_accessflags_t *allowed,
					 fsal_accessflags_t *denied,
					 bool owner_skip)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	if (owner_skip && entry->attrs.owner == op_ctx->creds->caller_uid)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	return fsal_test_access(obj_hdl, access_type, allowed, denied,
				owner_skip);
}

/**
 * @brief Rename an object
 *
 * Rename the given object from @a old_name in @a olddir_hdl to @a new_name in
 * @a newdir_hdl.  The old and new directories may be the same.
 *
 * @param[in] obj_hdl	Object to rename
 * @param[in] olddir_hdl	Directory containing @a obj_hdl
 * @param[in] old_name	Current name of @a obj_hdl
 * @param[in] newdir_hdl	Directory to move @a obj_hdl to
 * @param[in] new_name	Name to rename @a obj_hdl to
 * @return FSAL status
 */
static fsal_status_t mdcache_rename(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	mdcache_entry_t *mdc_olddir =
		container_of(olddir_hdl, mdcache_entry_t,
			     obj_handle);
	mdcache_entry_t *mdc_newdir =
		container_of(newdir_hdl, mdcache_entry_t,
			     obj_handle);
	mdcache_entry_t *mdc_obj =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	mdcache_entry_t *mdc_lookup_dst = NULL;
	struct fsal_export *sub_export = op_ctx->fsal_export->sub_export;
	bool refresh = false;
	bool rename_change_key;
	fsal_status_t status = {0, 0};

	status = mdc_lookup(mdc_newdir, new_name, true, &mdc_lookup_dst, NULL);
	if (!FSAL_IS_ERROR(status)) {
		if (mdc_obj == mdc_lookup_dst) {
			/* Same source and destination */
			goto out;
		}
		if (obj_is_junction(&mdc_lookup_dst->obj_handle)) {
			/* Cannot rename on top of junction */
			status = fsalstat(ERR_FSAL_XDEV, 0);
			goto out;
		}

		if (state_deleg_conflict(&mdc_lookup_dst->obj_handle, true)) {
			LogDebug(COMPONENT_CACHE_INODE, "Found an existing delegation for %s",
				  new_name);
			status = fsalstat(ERR_FSAL_DELAY, 0);
			goto out;
		}
	}
	/* Now update cached dirents.  Must take locks in the correct order */
	mdcache_src_dest_lock(mdc_olddir, mdc_newdir);

	subcall(
		status = mdc_olddir->sub_handle->obj_ops->rename(
			mdc_obj->sub_handle, mdc_olddir->sub_handle,
			old_name, mdc_newdir->sub_handle, new_name)
	       );

	if (FSAL_IS_ERROR(status))
		goto unlock;

	if (mdc_lookup_dst != NULL) {
		/* Mark target file attributes as invalid */
		atomic_clear_uint32_t_bits(&mdc_lookup_dst->mde_flags,
					   MDCACHE_TRUST_ATTRS);
	}

	/* Mark renamed file attributes as invalid */
	atomic_clear_uint32_t_bits(&mdc_obj->mde_flags,
				   MDCACHE_TRUST_ATTRS);

	/* Mark directory attributes as invalid */
	atomic_clear_uint32_t_bits(&mdc_olddir->mde_flags,
				   MDCACHE_TRUST_ATTRS);

	if (olddir_hdl != newdir_hdl) {
		atomic_clear_uint32_t_bits(&mdc_newdir->mde_flags,
					   MDCACHE_TRUST_ATTRS);
	}

	/* NOTE: Below we mostly don't check if the directory is not
	 *       cached. The cache manipulation functions we call already
	 *       bail out if we aren't cached. However, for rename into a
	 *       new directory, we need to bypass if not cached even if
	 *       chunking is enabled, so we check in that case to make
	 *       chunk management simpler.
	 */

	if (mdc_lookup_dst) {
		/* Remove the entry from parent dir_entries avl */
		mdcache_dirent_remove(mdc_newdir, new_name);

		/* Mark unreachable */
		mdc_unreachable(mdc_lookup_dst);
	}


	subcall(
		rename_change_key = sub_export->exp_ops.fs_supports(
				sub_export, fso_rename_changes_key)
	       );
	if (rename_change_key) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : key changing", mdc_olddir,
			 old_name, mdc_newdir, new_name);

		/* FSAL changes keys on rename.  Just remove the dirent(s) */

		/* Old dirent first */
		mdcache_dirent_remove(mdc_olddir, old_name);

		/** @todo: With chunking and compute cookie, we can actually
		 *         figure out which chunk the new dirent belongs to
		 *         without a lookup, so we could just invalidate that
		 *         chunk and the rest of the directory can remain
		 *         cached.
		 */

		/* Now new directory.  Here, we just need to invalidate dirents,
		 * since we have a known missing dirent */
		mdcache_dirent_invalidate_all(mdc_newdir);

		/* Handle key is changing.  This means the old handle is
		 * useless.  Mark it unreachable, forcing a lookup next time */
		mdc_unreachable(mdc_obj);
	} else if (mdcache_param.dir.avl_chunk != 0) {
		bool invalidate = true;

		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : moving entry", mdc_olddir,
			 old_name, mdc_newdir, new_name);

		/* Remove the old entry */
		mdcache_dirent_remove(mdc_olddir, old_name);

		/* We may have a cache entry for the destination
		 * filename.  If we do, we must delete it : it is stale. */
		mdcache_dirent_remove(mdc_newdir, new_name);

		status = mdcache_dirent_add(mdc_newdir, new_name, mdc_obj,
					    &invalidate);

		if (FSAL_IS_ERROR(status)) {
			/* We're obviously out of date.  Throw out the cached
			   directory */
			LogCrit(COMPONENT_CACHE_INODE, "Add dirent returned %s",
				fsal_err_txt(status));
			/* Protected by mdcache_src_dst_lock() above */
			mdcache_dirent_invalidate_all(mdc_newdir);
		} else if (!invalidate) {
			/* Refresh destination directory attributes without
			 * invalidating dirents.
			 */
			refresh = true;
		}
	}

unlock:

	/* unlock entries */
	mdcache_src_dest_unlock(mdc_olddir, mdc_newdir);

out:
	/* Refresh, if necessary.  Must be done without lock held */
	if (FSAL_IS_SUCCESS(status)) {
		/* If we're moving a directory out, update parent hash.
		 * Since we already dropped the src_desk lock, things
		 * may have changed, so just free the parent fh.
		 */
		if (mdc_olddir != mdc_newdir && obj_hdl->type == DIRECTORY) {
			PTHREAD_RWLOCK_wrlock(&mdc_obj->content_lock);
			mdcache_free_fh(&mdc_obj->fsobj.fsdir.parent);
			PTHREAD_RWLOCK_unlock(&mdc_obj->content_lock);
		}

		if (refresh)
			status =
			       mdcache_refresh_attrs_no_invalidate(mdc_newdir);
	}

	if (mdc_lookup_dst)
		mdcache_put(mdc_lookup_dst);

	return status;
}

/**
 * @brief Refresh the attributes for an mdcache entry.
 *
 *       The caller must also call mdcache_kill_entry after releasing the
 *       attr_lock if ERR_FSAL_STALE is returned.
 *
 * @note The caller must hold the attribute lock for WRITE
 *
 * @param[in] entry		The mdcache entry to refresh attributes for.
 * @param[in] need_acl		Indicates if the ACL needs updating.
 * @param[in] need_fslocations	Indicates if the fslocations are needed.
 * @param[in] invalidate	Invalidate the dirent cache if the entry is a
 *				directory.
 */

fsal_status_t mdcache_refresh_attrs(mdcache_entry_t *entry, bool need_acl,
				    bool need_fslocations, bool invalidate)
{
	struct attrlist attrs;
	fsal_status_t status = {0, 0};
	struct timespec oldmtime;
	bool file_deleg = false;
	cbgetattr_t *cbgetattr;

	/* Use this to detect if we should invalidate a directory. */
	oldmtime = entry->attrs.mtime;

	file_deleg = (entry->obj_handle.state_hdl &&
	  entry->obj_handle.state_hdl->file.fdeleg_stats.fds_curr_delegations);

	/* We always ask for all regular attributes, even if the caller was
	 * only interested in the ACL unless the file is delegated.
	 */
	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) | ATTR_RDATTR_ERR);

	if (!need_acl) {
		/* Don't request the ACL if not necessary. */
		attrs.request_mask &= ~ATTR_ACL;
	}

	if (!need_fslocations) {
		/* Don't request FS LOCATIONS if not required */
		attrs.request_mask &= ~ATTR4_FS_LOCATIONS;
	}

	if (file_deleg && entry->attrs.expire_time_attr) {
		/* If the file is delegated, then we can trust
		 * the attributes already fetched (i.e, which
		 * are in entry->attrs.valid_mask). Hence mask
		 * them out.
		 */
		attrs.request_mask = (attrs.request_mask &
				      ~entry->attrs.valid_mask);

		/* Bail out if ATTR_RDATTR_ERR is the only remaining
		 * attr set
		 */
		if ((attrs.request_mask & ~ATTR_RDATTR_ERR) == 0)
			goto out;
	}

	/* We will want all the requested attributes in the entry */
	entry->attrs.request_mask = attrs.request_mask;

	subcall(
		status = entry->sub_handle->obj_ops->getattrs(
			entry->sub_handle, &attrs)
	       );

	if (FSAL_IS_ERROR(status)) {
		/* Done with the attrs */
		fsal_release_attrs(&attrs);

		return status;
	}

	mdc_update_attr_cache(entry, &attrs);

out:
	/* Done with the attrs (we didn't need to call this since the
	 * fsal_copy_attrs preceding consumed all the references, but we
	 * release them anyway to make it easy to scan the code for correctness.
	 */
	fsal_release_attrs(&attrs);

	/* Always save copy of latest change and filesize
	 * to compare with values returned in cbgetattr response
	 */
	if (file_deleg) {
		cbgetattr = &entry->obj_handle.state_hdl->file.cbgetattr;
		cbgetattr->change = entry->attrs.change;
		cbgetattr->filesize = entry->attrs.filesize;
	}

	LogAttrlist(COMPONENT_CACHE_INODE, NIV_FULL_DEBUG,
		    "attrs ", &entry->attrs, true);

	if (invalidate && entry->obj_handle.type == DIRECTORY &&
	    gsh_time_cmp(&oldmtime, &entry->attrs.mtime) < 0) {

		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		mdcache_dirent_invalidate_all(entry);
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
	}

	return status;
}

/**
 * @brief Get the attributes for an object
 *
 * If the attribute cache is valid, just return them.  Otherwise, resfresh the
 * cache.
 *
 * @param[in]     obj_hdl   Object to get attributes from
 * @param[in,out] attrs_out Attributes fetched
 * @return FSAL status
 */
static fsal_status_t mdcache_getattrs(struct fsal_obj_handle *obj_hdl,
				      struct attrlist *attrs_out)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status = {0, 0};

	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

	if (mdcache_is_attrs_valid(entry, attrs_out->request_mask)) {
		/* Up-to-date */
		goto unlock;
	}

	/* Promote to write lock */
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	if (mdcache_is_attrs_valid(entry, attrs_out->request_mask)) {
		/* Someone beat us to it */
		goto unlock;
	}

	status = mdcache_refresh_attrs(
			entry, (attrs_out->request_mask & ATTR_ACL) != 0,
			(attrs_out->request_mask & ATTR4_FS_LOCATIONS) != 0,
			true);

	if (FSAL_IS_ERROR(status)) {
		/* We failed to fetch any attributes. Pass that fact back to
		 * the caller. We do not change the validity of the current
		 * entry attributes.
		 */
		if (attrs_out->request_mask & ATTR_RDATTR_ERR)
			attrs_out->valid_mask = ATTR_RDATTR_ERR;
		goto unlock_no_attrs;
	}

unlock:

	/* Struct copy */
	fsal_copy_attrs(attrs_out, &entry->attrs, false);

unlock_no_attrs:

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	if (FSAL_IS_ERROR(status) && (status.major == ERR_FSAL_STALE))
		mdcache_kill_entry(entry);

	LogAttrlist(COMPONENT_CACHE_INODE, NIV_FULL_DEBUG,
		    "attrs ", attrs_out, true);

	return status;
}

/**
 * @brief Set attributes on an object (new style)
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to set attributes on
 * @param[in] attrs	Attributes to set
 * @return FSAL status
 */
static fsal_status_t mdcache_setattr2(struct fsal_obj_handle *obj_hdl,
				      bool bypass,
				      struct state_t *state,
				      struct attrlist *attrs)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status, status2;
	uint64_t change;
	bool need_acl = false, kill_entry = false;


	change = entry->attrs.change;

	subcall(
		status = entry->sub_handle->obj_ops->setattr2(
			entry->sub_handle, bypass, state, attrs)
	       );

	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_STALE)
			kill_entry = true;
		goto out;
	}

	/* In case of ACL enabled, any of the below attribute changes
	 * result in change of ACL set as well.
	 */
	if (!op_ctx_export_has_option(EXPORT_OPTION_DISABLE_ACL) &&
	    (FSAL_TEST_MASK(attrs->valid_mask,
			   ATTR_MODE | ATTR_OWNER | ATTR_GROUP | ATTR_ACL))) {
		need_acl = true;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	status2 = mdcache_refresh_attrs(entry, need_acl,
					false /*need_fslocations*/, false);
	if (FSAL_IS_ERROR(status2)) {
		/* Assume that the cache is bogus now */
		atomic_clear_uint32_t_bits(&entry->mde_flags,
				MDCACHE_TRUST_ATTRS | MDCACHE_TRUST_ACL |
				MDCACHE_TRUST_FS_LOCATIONS |
				MDCACHE_TRUST_SEC_LABEL);
		if (status2.major == ERR_FSAL_STALE)
			kill_entry = true;
	} else if (change == entry->attrs.change) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "setattr2 did not change change attribute before %lld after = %lld",
			 (long long int) change,
			 (long long int) entry->attrs.change);
		entry->attrs.change = change + 1;
	}
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
out:
	if (kill_entry)
		mdcache_kill_entry(entry);

	return status;
}

/**
 * @brief Unlink an object
 *
 * Does some junction handling
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] obj_hdl	Object being removed
 * @param[in] name	Name of object to remove
 * @return FSAL status
 */
static fsal_status_t mdcache_unlink(struct fsal_obj_handle *dir_hdl,
				    struct fsal_obj_handle *obj_hdl,
				    const char *name)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t, obj_handle);
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Unlink %p/%s (%p)",
		     parent, name, entry);

	if (obj_is_junction(&entry->obj_handle)) {
		/* Cannot remove a junction */
		return fsalstat(ERR_FSAL_XDEV, 0);
	}

	subcall(
		status = parent->sub_handle->obj_ops->unlink(
			parent->sub_handle, entry->sub_handle, name)
	       );

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "unlink %s returned %s",
			  name, fsal_err_txt(status));
		if (status.major == ERR_FSAL_STALE)
			(void)mdcache_kill_entry(parent);
		else if (status.major == ERR_FSAL_NOTEMPTY &&
			 (obj_hdl->type == DIRECTORY)) {
			PTHREAD_RWLOCK_wrlock(&entry->content_lock);
			mdcache_dirent_invalidate_all(entry);
			PTHREAD_RWLOCK_unlock(&entry->content_lock);
		} else {
			/* Some real error.  Bail. */
			return status;
		}
	} else {
		PTHREAD_RWLOCK_wrlock(&parent->content_lock);
		mdcache_dirent_remove(parent, name);
		PTHREAD_RWLOCK_unlock(&parent->content_lock);

		/* Invalidate attributes of parent and entry */
		atomic_clear_uint32_t_bits(&parent->mde_flags,
					   MDCACHE_TRUST_ATTRS);
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);

		if (entry->obj_handle.type == DIRECTORY) {
			PTHREAD_RWLOCK_wrlock(&entry->content_lock);
			mdcache_free_fh(&entry->fsobj.fsdir.parent);
			PTHREAD_RWLOCK_unlock(&entry->content_lock);
		}

		mdc_unreachable(entry);
	}

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Unlink %s %p/%s (%p)",
		     FSAL_IS_ERROR(status) ? "failed" : "done",
		     parent, name, entry);

	return status;
}

/**
 * @brief Get the wire version of a handle
 *
 * Just pass through to the underlying FSAL
 *
 * @param[in] obj_hdl	Handle to digest
 * @param[in] out_type	Type of digest to get
 * @param[out] fh_desc	Buffer to write digest into
 * @return FSAL status
 */
static fsal_status_t mdcache_handle_to_wire(
				const struct fsal_obj_handle *obj_hdl,
				fsal_digesttype_t out_type,
				struct gsh_buffdesc *fh_desc)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(
		status = entry->sub_handle->obj_ops->handle_to_wire(
			entry->sub_handle, out_type, fh_desc)
	       );

	return status;
}

/**
 * @brief Get the unique key for a handle
 *
 * Just pass through to the underlying FSAL
 *
 * @param[in] obj_hdl	Handle to digest
 * @param[out] fh_desc	Buffer to write key into
 */
static void mdcache_handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	subcall(
		entry->sub_handle->obj_ops->handle_to_key(entry->sub_handle,
							  fh_desc)
	       );
}

/**
 * @brief Compare two handles
 *
 * All FSALs currently use the default, but delegate in case a FSAL wants to
 * override.
 *
 * @param[in]     obj_hdl1    The first handle to compare
 * @param[in]     obj_hdl2    The second handle to compare
 *
 * @return True if match, false otherwise
 */
static bool mdcache_handle_cmp(struct fsal_obj_handle *obj_hdl1,
			       struct fsal_obj_handle *obj_hdl2)
{
	mdcache_entry_t *entry1 =
		container_of(obj_hdl1, mdcache_entry_t, obj_handle);
	mdcache_entry_t *entry2 =
		container_of(obj_hdl2, mdcache_entry_t, obj_handle);
	bool status;

	subcall(
		status = entry1->sub_handle->obj_ops->handle_cmp(
			entry1->sub_handle, entry2->sub_handle)
	       );

	return status;
}

/**
 * @brief Grant a layout segment.
 *
 * Delegate to sub-FSAL
 *
 * @param[in]     obj_hdl  The handle of the file on which the layout is
 *                         requested.
 * @param[in]     req_ctx  Request context
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */
static nfsstat4 mdcache_layoutget(struct fsal_obj_handle *obj_hdl,
				  struct req_op_context *req_ctx,
				  XDR *loc_body,
				  const struct fsal_layoutget_arg *arg,
				  struct fsal_layoutget_res *res)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	nfsstat4 status;

	subcall(
		status = entry->sub_handle->obj_ops->layoutget(
			entry->sub_handle, req_ctx, loc_body, arg, res)
	       );

	return status;
}

/**
 * @brief Potentially return one layout segment
 *
 * Delegate to sub-FSAL
 *
 * @param[in] obj_hdl  The object on which a segment is to be returned
 * @param[in] req_ctx  Request context
 * @param[in] lrf_body In the case of a non-synthetic return, this is
 *                     an XDR stream corresponding to the layout
 *                     type-specific argument to LAYOUTRETURN.  In
 *                     the case of a synthetic or bulk return,
 *                     this is a NULL pointer.
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */
static nfsstat4 mdcache_layoutreturn(struct fsal_obj_handle *obj_hdl,
				     struct req_op_context *req_ctx,
				     XDR *lrf_body,
				     const struct fsal_layoutreturn_arg *arg)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	nfsstat4 status;

	subcall(
		status = entry->sub_handle->obj_ops->layoutreturn(
			entry->sub_handle, req_ctx, lrf_body, arg)
	       );

	return status;
}

/**
 * @brief Commit a segment of a layout
 *
 * Delegate to sub-FSAL
 *
 * @param[in]     obj_hdl  The object on which to commit
 * @param[in]     req_ctx  Request context
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */
static nfsstat4 mdcache_layoutcommit(struct fsal_obj_handle *obj_hdl,
				     struct req_op_context *req_ctx,
				     XDR *lou_body,
				     const struct fsal_layoutcommit_arg *arg,
				     struct fsal_layoutcommit_res *res)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	nfsstat4 status;

	subcall(
		status = entry->sub_handle->obj_ops->layoutcommit(
			entry->sub_handle, req_ctx, lou_body, arg, res)
	       );

	if (status == NFS4_OK)
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);

	return status;
}

/**
 * @brief Get a reference to the handle
 *
 * @param[in] obj_hdl	Handle to ref
 * @return FSAL status
 */
static void mdcache_get_ref(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	mdcache_get(entry);
}

/**
 * @brief Put a reference to the handle
 *
 * @param[in] obj_hdl	Handle to unref
 * @return FSAL status
 */
static void mdcache_put_ref(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	mdcache_put(entry);
}

/**
 * @brief Release an object handle
 *
 * This force cleans-up.
 *
 * @param[in] obj_hdl	Handle to release
 * @return FSAL status
 */
static void mdcache_hdl_release(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	LogDebug(COMPONENT_CACHE_INODE,
		 "Releasing obj_hdl=%p, entry=%p",
		 obj_hdl, entry);

	mdcache_kill_entry(entry);
}

/**
 * @brief Merge a duplicate handle with an original handle
 *
 * Delegate to sub-FSAL.  This should not happen, because of the cache, but
 * handle it anyway.
 *
 * @param[in]  orig_hdl  Original handle
 * @param[in]  dupe_hdl Handle to merge into original
 * @return FSAL status
 */
static fsal_status_t mdcache_merge(struct fsal_obj_handle *orig_hdl,
				   struct fsal_obj_handle *dupe_hdl)
{
	mdcache_entry_t *entry =
		container_of(orig_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(
		status = entry->sub_handle->obj_ops->merge(entry->sub_handle,
							  dupe_hdl)
	       );

	return status;
}


static bool mdcache_is_referral(struct fsal_obj_handle *obj_hdl,
				struct attrlist *attrs,
				bool cache_attrs)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	bool result, locked, write_locked;
	attrmask_t valid_request_mask = 0;

	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);
	locked = true;

	if (mdcache_is_attrs_valid(entry, attrs->request_mask)) {
		/* Up-to-date */
		goto copy_and_unlock;
	}

	/* Promote to write lock */
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	write_locked = true;

	if (!mdcache_is_attrs_valid(entry, attrs->request_mask)) {
		/* attrs are not valid, let the subfsal take care of it */
		goto invoke_subfsal;
	}

copy_and_unlock:

	valid_request_mask = attrs->request_mask;
	fsal_copy_attrs(attrs, &entry->attrs, false);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	locked = false;
	write_locked = false;

invoke_subfsal:

	subcall(
		result = entry->sub_handle->obj_ops->is_referral(
							entry->sub_handle,
							attrs, cache_attrs);
	       );

	/* If the valid request mask before subcall and after subcall are same
	 * then we can skip attr updates. This should ideally be the most common
	 * case. */
	if (!cache_attrs || valid_request_mask == attrs->request_mask ||
	     attrs->valid_mask == 0) {
		goto out;
	}

	/* Need to take a read lock again to check if cached attrs are valid */
	if (!locked) {
		PTHREAD_RWLOCK_rdlock(&entry->attr_lock);
		locked = true;
		assert(!write_locked);
	}

	/* Check if is_referral added any new attrs and update them in the
	 * cache */
	if (!mdcache_is_attrs_valid(entry, attrs->request_mask)) {
		if (!write_locked) {
			/* Promote to write lock to update the cached attrs */
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
		}

		mdc_update_attr_cache(entry, attrs);
	}

	assert(locked);

out:
	if (locked) {
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	}

	return result;
}

void mdcache_handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->get_ref = mdcache_get_ref;
	ops->put_ref = mdcache_put_ref;
	ops->release = mdcache_hdl_release;
	ops->merge = mdcache_merge;
	ops->lookup = mdcache_lookup;
	ops->readdir = mdcache_readdir;
	ops->mkdir = mdcache_mkdir;
	ops->mknode = mdcache_mknode;
	ops->symlink = mdcache_symlink;
	ops->readlink = mdcache_readlink;
	ops->test_access = mdcache_test_access;
	ops->getattrs = mdcache_getattrs;
	ops->link = mdcache_link;
	ops->rename = mdcache_rename;
	ops->unlink = mdcache_unlink;
	ops->io_advise = mdcache_io_advise;
	ops->close = mdcache_close;
	ops->handle_to_wire = mdcache_handle_to_wire;
	ops->handle_to_key = mdcache_handle_to_key;
	ops->handle_cmp = mdcache_handle_cmp;

	/* pNFS */
	ops->layoutget = mdcache_layoutget;
	ops->layoutreturn = mdcache_layoutreturn;
	ops->layoutcommit = mdcache_layoutcommit;

	/* Multi-FD */
	ops->open2 = mdcache_open2;
	ops->check_verifier = mdcache_check_verifier;
	ops->status2 = mdcache_status2;
	ops->reopen2 = mdcache_reopen2;
	ops->read2 = mdcache_read2;
	ops->write2 = mdcache_write2;
	ops->seek2 = mdcache_seek2;
	ops->io_advise2 = mdcache_io_advise2;
	ops->commit2 = mdcache_commit2;
	ops->lock_op2 = mdcache_lock_op2;
	ops->lease_op2 = mdcache_lease_op2;
	ops->setattr2 = mdcache_setattr2;
	ops->close2 = mdcache_close2;
	ops->fallocate = mdcache_fallocate;

	/* xattr related functions */
	ops->list_ext_attrs = mdcache_list_ext_attrs;
	ops->getextattr_id_by_name = mdcache_getextattr_id_by_name;
	ops->getextattr_value_by_name = mdcache_getextattr_value_by_name;
	ops->getextattr_value_by_id = mdcache_getextattr_value_by_id;
	ops->setextattr_value = mdcache_setextattr_value;
	ops->setextattr_value_by_id = mdcache_setextattr_value_by_id;
	ops->remove_extattr_by_id = mdcache_remove_extattr_by_id;
	ops->remove_extattr_by_name = mdcache_remove_extattr_by_name;
	ops->getxattrs = mdcache_getxattrs;
	ops->setxattrs = mdcache_setxattrs;
	ops->removexattrs = mdcache_removexattrs;
	ops->listxattrs = mdcache_listxattrs;

	ops->is_referral = mdcache_is_referral;
}

/*
 * export methods that create object handles
 */
/**
 * @brief Lookup a path from the export
 *
 * Lookup in the sub-FSAL, and wrap with a MDCACHE entry.  This is the
 * equivalent of ...lookup_path() followed by mdcache_new_entry()
 *
 * @param[in]     exp_hdl   FSAL export to look in
 * @param[in]     path      Path to find
 * @param[out]    handle    Resulting object handle
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
fsal_status_t mdcache_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out)
{
	struct fsal_obj_handle *sub_handle = NULL;
	struct mdcache_fsal_export *export =
		container_of(exp_hdl, struct mdcache_fsal_export, mfe_exp);
	struct fsal_export *sub_export = export->mfe_exp.sub_export;
	fsal_status_t status;
	struct attrlist attrs;
	mdcache_entry_t *new_entry;

	*handle = NULL;

	/* Ask for all supported attributes except ACL (we defer fetching ACL
	 * until asked for it (including a permission check).
	 */
	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) & ~ATTR_ACL);

	subcall_raw(export,
		status = sub_export->exp_ops.lookup_path(sub_export, path,
							 &sub_handle, &attrs)
	       );

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "lookup_path %s failed with %s",
			 path, fsal_err_txt(status));
		fsal_release_attrs(&attrs);
		return status;
	}

	status = mdcache_new_entry(export, sub_handle, &attrs, attrs_out,
				   false, &new_entry, NULL, MDC_REASON_DEFAULT);

	fsal_release_attrs(&attrs);

	if (!FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "lookup_path Created entry %p FSAL %s",
			     new_entry, new_entry->sub_handle->fsal->name);
		/* Make sure this entry has a parent pointer */
		PTHREAD_RWLOCK_wrlock(&new_entry->content_lock);
		mdc_get_parent(export, new_entry);
		PTHREAD_RWLOCK_unlock(&new_entry->content_lock);

		*handle = &new_entry->obj_handle;
	}

	if (attrs_out != NULL) {
		LogAttrlist(COMPONENT_CACHE_INODE, NIV_FULL_DEBUG,
			    "lookup_path ", attrs_out, true);
	}

	return status;
}

/**
 * @brief Find or create a cache entry from a host-handle
 *
 * This is the equivalent of mdcache_get().  It returns a ref'd entry that
 * must be put using obj_ops->release().
 *
 * @param[in]     exp_hdl   The export in which to create the handle
 * @param[in]     hdl_desc  Buffer descriptor for the host handle
 * @param[out]    handle    FSAL object handle
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @return FSAL status
 */
fsal_status_t mdcache_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *fh_desc,
				   struct fsal_obj_handle **handle,
				   struct attrlist *attrs_out)
{
	struct mdcache_fsal_export *export =
		container_of(exp_hdl, struct mdcache_fsal_export, mfe_exp);
	mdcache_entry_t *entry;
	fsal_status_t status;

	*handle = NULL;
	status = mdcache_locate_host(fh_desc, export, &entry, attrs_out);
	if (FSAL_IS_ERROR(status))
		return status;

	/* Make sure this entry has a parent pointer */
	PTHREAD_RWLOCK_wrlock(&entry->content_lock);
	mdc_get_parent(export, entry);
	PTHREAD_RWLOCK_unlock(&entry->content_lock);

	if (attrs_out != NULL) {
		LogAttrlist(COMPONENT_CACHE_INODE, NIV_FULL_DEBUG,
			    "create_handle ", attrs_out, true);
	}

	*handle = &entry->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
