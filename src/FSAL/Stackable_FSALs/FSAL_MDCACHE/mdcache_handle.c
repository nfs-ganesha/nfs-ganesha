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
#include <os/subr.h>

#include "mdcache_lru.h"
#include "mdcache_hash.h"
#include "mdcache_avl.h"

static fsal_status_t mdcache_getattrs(struct fsal_obj_handle *obj_hdl);

/*
 * Helper functions
 */

static fsal_status_t mdc_add_dirent(mdcache_entry_t *parent, const char *name,
				    mdcache_entry_t *entry)
{
	fsal_status_t status;

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);
	/* Add this entry to the directory (also takes an internal ref) */
	status = mdcache_dirent_add(parent, name, entry, NULL);
	PTHREAD_RWLOCK_unlock(&parent->content_lock);

	return status;
}

/*
 * handle methods
 */

/**
 * Attempts to create a new mdcache handle, or cleanup memory if it fails.
 *
 * This function is a wrapper of mdcache_alloc_handle. It adds error checking
 * and logging. It also cleans objects allocated in the subfsal if it fails.
 *
 * @param[in] export The mdcache export used by the handle.
 * @param[in,out] sub_handle The handle used by the subfsal.
 * @param[in] fs The filesystem of the new handle.
 * @param[in] new_handle Address where the new allocated pointer should be
 * written.
 * @param[in] subfsal_status Result of the allocation of the subfsal handle.
 *
 * @note This returns an INITIAL ref'd entry on success
 *
 * @return An error code for the function.
 */
static fsal_status_t mdcache_alloc_and_check_handle(
		struct mdcache_fsal_export *export,
		struct fsal_obj_handle *sub_handle,
		mdcache_entry_t **new_handle,
		fsal_status_t subfsal_status)
{
	if (FSAL_IS_ERROR(subfsal_status))
		return subfsal_status;

	return mdcache_new_entry(export, sub_handle, MDCACHE_FLAG_NONE,
				 new_handle);
}

/**
 * @brief Lookup a name
 *
 * Lookup a name relative to another object
 *
 * @param[in] parent	Handle of parent
 * @param[in] name	Name to look up
 * @param[out] handle	Handle of found object, on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t mdcache_lookup(struct fsal_obj_handle *parent,
				    const char *name,
				    struct fsal_obj_handle **handle)
{
	mdcache_entry_t *mdc_parent =
		container_of(parent, mdcache_entry_t, obj_handle);
	mdcache_entry_t *entry = NULL;
	fsal_status_t status;

	*handle = NULL;

	status = mdc_lookup(mdc_parent, name, true, &entry);
	if (entry)
		*handle = &entry->obj_handle;

	return status;
}

/**
 * @brief Create a file
 *
 * @param[in] dir_hdl	Handle of parent directory
 * @param[in] name	Name of file to create
 * @param[in] attrib	Attributes to set on new file
 * @param[out] handle	Newly created file
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t mdcache_create(struct fsal_obj_handle *dir_hdl,
			    const char *name, struct attrlist *attrib,
			    struct fsal_obj_handle **handle)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t,
			     obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct fsal_obj_handle *sub_handle;
	mdcache_entry_t *entry;
	fsal_status_t status;

	*handle = NULL;

	subcall_raw(export,
		status = parent->sub_handle->obj_ops.create(
			parent->sub_handle, name, attrib, &sub_handle)
	       );

	if (FSAL_IS_ERROR(status) && status.major == ERR_FSAL_STALE) {
		LogEvent(COMPONENT_CACHE_INODE,
			 "FSAL returned STALE on create");
		mdcache_kill_entry(parent);
	}

	status = mdcache_alloc_and_check_handle(export, sub_handle,
						&entry, status);
	if (FSAL_IS_ERROR(status))
		return status;

	status = mdc_add_dirent(parent, name, entry);
	if (FSAL_IS_ERROR(status)) {
		mdcache_put(entry);
		*handle = NULL;
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because add dirent failed");
		return status;
	}

	*handle = &entry->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
			     struct fsal_obj_handle **handle)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t,
			     obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	mdcache_entry_t *entry;
	struct fsal_obj_handle *sub_handle;
	fsal_status_t status;

	*handle = NULL;

	subcall_raw(export,
		status = parent->sub_handle->obj_ops.mkdir(
			parent->sub_handle, name, attrib, &sub_handle)
	       );

	if (FSAL_IS_ERROR(status) && status.major == ERR_FSAL_STALE) {
		LogEvent(COMPONENT_CACHE_INODE,
			 "FSAL returned STALE on create");
		mdcache_kill_entry(parent);
	}

	status = mdcache_alloc_and_check_handle(export, sub_handle,
						&entry, status);
	if (FSAL_IS_ERROR(status))
		return status;

	status = mdc_add_dirent(parent, name, entry);
	if (FSAL_IS_ERROR(status)) {
		mdcache_put(entry);
		*handle = NULL;
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because add dirent failed");
		return status;
	}

	/* Insert Parent's key */
	mdcache_key_dup(&entry->fsobj.fsdir.parent, &parent->fh_hk.key);

	*handle = &entry->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Make a device node
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] name	Name of new node
 * @param[in] nodetype	Type of new node
 * @param[in] dev	Device information
 * @param[in] attrib	Attributes for new node
 * @param[out] handle	New object handle on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t mdcache_mknode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      fsal_dev_t *dev,	/* IN */
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t,
			     obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct fsal_obj_handle *sub_handle;
	mdcache_entry_t *entry;
	fsal_status_t status;

	*handle = NULL;

	subcall_raw(export,
		status = parent->sub_handle->obj_ops.mknode(
			parent->sub_handle, name, nodetype, dev, attrib,
			&sub_handle)
	       );

	if (FSAL_IS_ERROR(status) && status.major == ERR_FSAL_STALE) {
		LogEvent(COMPONENT_CACHE_INODE,
			 "FSAL returned STALE on create");
		mdcache_kill_entry(parent);
	}

	status = mdcache_alloc_and_check_handle(export, sub_handle,
						&entry, status);
	if (FSAL_IS_ERROR(status))
		return status;

	status = mdc_add_dirent(parent, name, entry);
	if (FSAL_IS_ERROR(status)) {
		mdcache_put(entry);
		*handle = NULL;
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because add dirent failed");
		return status;
	}

	*handle = &entry->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
				 struct fsal_obj_handle **handle)
{
	mdcache_entry_t *parent =
		container_of(dir_hdl, mdcache_entry_t,
			     obj_handle);
	struct mdcache_fsal_export *export = mdc_cur_export();
	struct fsal_obj_handle *sub_handle;
	mdcache_entry_t *entry;
	fsal_status_t status;

	*handle = NULL;

	subcall_raw(export,
		status = parent->sub_handle->obj_ops.symlink(
			parent->sub_handle, name, link_path, attrib,
			&sub_handle)
	       );

	if (FSAL_IS_ERROR(status) && status.major == ERR_FSAL_STALE) {
		LogEvent(COMPONENT_CACHE_INODE,
			 "FSAL returned STALE on create");
		mdcache_kill_entry(parent);
	}

	status = mdcache_alloc_and_check_handle(export, sub_handle,
						&entry, status);
	if (FSAL_IS_ERROR(status))
		return status;

	status = mdc_add_dirent(parent, name, entry);
	if (FSAL_IS_ERROR(status)) {
		mdcache_put(entry);
		*handle = NULL;
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because add dirent failed");
		return status;
	}

	*handle = &entry->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
	if (!refresh && !(entry->mde_flags & MDCACHE_TRUST_CONTENT)) {
		/* Our data are stale.  Drop the lock, get a
		   write-lock, load in new data, and copy it out to
		   the caller. */
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		/* Make sure nobody updated the content while we were
		   waiting. */
		refresh = !(entry->mde_flags & MDCACHE_TRUST_CONTENT);
	}

	subcall(
		status = entry->sub_handle->obj_ops.readlink(
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

	subcall(
		status = entry->sub_handle->obj_ops.link(
			entry->sub_handle, dest->sub_handle, name)
	       );
	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "link failed %s",
			     fsal_err_txt(status));
		return status;
	}

	/* Add the new entry in the destination directory */
	status = mdc_add_dirent(dest, name, entry);

	/* Invalidate attributes, so refresh will be forced */
	status = mdcache_invalidate(entry, MDCACHE_INVALIDATE_ATTRS);
	if (FSAL_IS_ERROR(status))
		return status;

	status = mdcache_invalidate(dest, MDCACHE_INVALIDATE_ATTRS);
	if (FSAL_IS_ERROR(status))
		return status;

	/* Attributes are refreshed by fsal_link */

	return status;
}

/**
 * Read the contents of a dirctory
 *
 * If necessary, populate the dirent cache from the underlying FSAL.  Then, walk
 * the dirent cache calling the callback.
 *
 * @param dir_hdl [IN] the directory to read
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eod_met [OUT] eod marker true == end of dir
 *
 * @return FSAL status
 */

static fsal_status_t mdcache_readdir(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, bool *eod_met)
{
	mdcache_entry_t *directory = container_of(dir_hdl, mdcache_entry_t,
						  obj_handle);
	mdcache_dir_entry_t *dirent = NULL;
	struct avltree_node *dirent_node;
	fsal_status_t status = {0, 0};
	bool cb_result = true;

	if (!mdc_dircache_trusted(directory)) {
		PTHREAD_RWLOCK_wrlock(&directory->content_lock);
		status = mdcache_dirent_populate(directory);
		PTHREAD_RWLOCK_unlock(&directory->content_lock);
		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "mdcache_dirent_populate status=%s",
				     fsal_err_txt(status));
			goto unlock_dir;
		}
	}

	PTHREAD_RWLOCK_rdlock(&directory->content_lock);
	/* Get initial starting position */
	if (*whence > 0) {
		/* Not a full directory walk */
		if (*whence < 3) {
			/* mdcache always uses 1 and 2 for . and .. */
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "Bad cookie");
			status = fsalstat(ERR_FSAL_BADCOOKIE, 0);
			goto unlock_dir;
		}
		dirent = mdcache_avl_lookup_k(directory, *whence,
						  MDCACHE_FLAG_NEXT_ACTIVE);
		if (!dirent) {
			/* May be offset of last entry */
			if (mdcache_avl_lookup_k(directory, *whence,
						     MDCACHE_FLAG_NONE)) {
				/* yup, it was the last entry, not an error */
				LogFullDebug(COMPONENT_NFS_READDIR,
					     "EOD because empty result");
				*eod_met = true;
				status = fsalstat(ERR_FSAL_NOENT, 0);
				goto unlock_dir;
			}
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "seek to cookie=%" PRIu64 " fail",
				     *whence);
			status = fsalstat(ERR_FSAL_BADCOOKIE, 0);
			goto unlock_dir;
		}
		dirent_node = &dirent->node_hk;
	} else {
		/* Start at beginning */
		dirent_node = avltree_first(&directory->fsobj.fsdir.avl.t);
	}

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "About to readdir in mdcache_readdir: directory=%p cookie=%"
		     PRIu64 " collisions %d",
		     directory, *whence, directory->fsobj.fsdir.avl.collisions);

	/* Now satisfy the request from the cached readdir--stop when either
	 * the requested sequence or dirent sequence is exhausted */
	*eod_met = false;

	for (; cb_result && dirent_node;
	     dirent_node = avltree_next(dirent_node)) {
		mdcache_entry_t *entry = NULL;

		dirent = avltree_container_of(dirent_node,
					      mdcache_dir_entry_t,
					      node_hk);

		/* Get actual entry */
		status = mdc_try_get_cached(directory, dirent->name, &entry);
		if (status.major == ERR_FSAL_STALE) {
			status = mdc_lookup_uncached(directory, dirent->name,
						     &entry);
		}
		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "lookup failed status=%s",
				     fsal_err_txt(status));
			goto unlock_dir;
		}

		cb_result = cb(dirent->name, &entry->obj_handle, dir_state,
			       dirent->hk.k);

		mdcache_put(entry);

		if (!cb_result)
			break;
	}

	LogDebug(COMPONENT_NFS_READDIR,
		 "dirent_node = %p, in_result = %s", dirent_node,
		 cb_result ? "TRUE" : "FALSE");

	if (!dirent_node && cb_result)
		*eod_met = true;
	else
		*eod_met = false;


unlock_dir:
	PTHREAD_RWLOCK_unlock(&directory->content_lock);
	return status;
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
	fsal_status_t status;

	status = mdc_try_get_cached(mdc_newdir, new_name, &mdc_lookup_dst);

	if (!FSAL_IS_ERROR(status) && (mdc_obj == mdc_lookup_dst)) {
		/* Same source and destination */
		goto out;
	}

	subcall(
		status = mdc_olddir->sub_handle->obj_ops.rename(
			mdc_obj->sub_handle, mdc_olddir->sub_handle,
			old_name, mdc_newdir->sub_handle, new_name)
	       );

	if (FSAL_IS_ERROR(status))
		goto out;

	/* Refresh attribute caches */
	status = fsal_refresh_attrs(olddir_hdl);
	if (FSAL_IS_ERROR(status))
		goto out;

	if (olddir_hdl != newdir_hdl) {
		status = fsal_refresh_attrs(newdir_hdl);
		if (FSAL_IS_ERROR(status))
			goto out;
	}

	/* Now update cached dirents.  Must take locks in the correct order */
	mdcache_src_dest_lock(mdc_olddir, mdc_newdir);

	if (mdc_lookup_dst) {
		/* Remove the entry from parent dir_entries avl */
		status = mdcache_dirent_remove(mdc_newdir, new_name);

		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "remove entry failed with status %s",
				 fsal_err_txt(status));
			mdcache_dirent_invalidate_all(mdc_newdir);
		}

		/* Mark unreachable */
		mdc_unreachable(mdc_lookup_dst);
	}

	if (mdc_olddir == mdc_newdir) {
		/* if the rename operation is made within the same dir, then we
		 * use an optimization: mdcache_rename_dirent is used
		 * instead of adding/removing dirent. This limits the use of
		 * resource in this case */

		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : source and target directory  the same",
			 mdc_olddir, old_name, mdc_newdir, new_name);

		status = mdcache_dirent_rename(mdc_newdir, old_name, new_name);
		if (FSAL_IS_ERROR(status)) {
			/* We're obviously out of date.  Throw out the cached
			   directory */
			mdcache_dirent_invalidate_all(mdc_newdir);
		}
	} else {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Rename (%p,%s)->(%p,%s) : moving entry", mdc_olddir,
			 old_name, mdc_newdir, new_name);

		/* We may have a cache entry for the destination
		 * filename.  If we do, we must delete it : it is stale. */
		status = mdcache_dirent_remove(mdc_newdir, new_name);

		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "Remove stale dirent returned %s",
				 fsal_err_txt(status));
			mdcache_dirent_invalidate_all(mdc_newdir);
		}

		status = mdcache_dirent_add(mdc_newdir, new_name, mdc_obj,
					    NULL);
		if (FSAL_IS_ERROR(status)) {
			/* We're obviously out of date.  Throw out the cached
			   directory */
			LogCrit(COMPONENT_CACHE_INODE, "Add dirent returned %s",
				fsal_err_txt(status));
			mdcache_dirent_invalidate_all(mdc_newdir);
		}

		/* Remove the old entry */
		status = mdcache_dirent_remove(mdc_olddir, old_name);
		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "Remove old dirent returned %s",
				 fsal_err_txt(status));
			mdcache_dirent_invalidate_all(mdc_olddir);
		}
	}

	/* unlock entries */
	mdcache_src_dest_unlock(mdc_olddir, mdc_newdir);

out:
	if (mdc_lookup_dst)
		mdcache_put(mdc_lookup_dst);

	return status;
}

/**
 * @brief Get the attributes for an object
 *
 * If the attribute cache is valid, just return them.  Otherwise, resfresh the
 * cache.
 *
 * @param[in] obj_hdl	Object to get attributes from
 * @return FSAL status
 */
static fsal_status_t mdcache_getattrs(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status = {0, 0};
	time_t oldmtime = 0;

	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

	if (mdcache_is_attrs_valid(entry))
		/* Up-to-date */
		goto unlock;

	/* Promote to write lock */
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	if (mdcache_is_attrs_valid(entry))
		/* Someone beat us to it */
		goto unlock;

	oldmtime = obj_hdl->attrs->mtime.tv_sec;

	subcall(
		status = entry->sub_handle->obj_ops.getattrs(
			entry->sub_handle)
	       );

	if (FSAL_IS_ERROR(status)) {
		mdcache_kill_entry(entry);
		goto unlock;
	}

	mdc_fixup_md(entry);

	if ((obj_hdl->type == DIRECTORY) &&
	    (oldmtime < obj_hdl->attrs->mtime.tv_sec)) {

		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		status = mdcache_dirent_invalidate_all(entry);
		PTHREAD_RWLOCK_unlock(&entry->content_lock);

		if (FSAL_IS_ERROR(status)) {
			LogCrit(COMPONENT_CACHE_INODE,
				"mdcache_dirent_invalidate_all returned (%s)",
				fsal_err_txt(status));
			goto unlock;
		}
	}

unlock:
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	return status;
}

/**
 * @brief Set attributes on an object
 *
 * @param[in] obj_hdl	Object to set attributes on
 * @param[in] attrs	Attributes to set
 * @return FSAL status
 */
static fsal_status_t mdcache_setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	subcall(
		status = entry->sub_handle->obj_ops.setattrs(
			entry->sub_handle, attrs)
	       );

	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_STALE)
			mdcache_kill_entry(entry);
		goto unlock;
	}

	subcall(
		status = entry->sub_handle->obj_ops.getattrs(
			entry->sub_handle)
	       );

	if (FSAL_IS_ERROR(status)) {
		mdcache_kill_entry(entry);
		goto unlock;
	}

	mdc_fixup_md(entry);

unlock:
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

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
	fsal_status_t status;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	subcall(
		status = entry->sub_handle->obj_ops.setattr2(
			entry->sub_handle, bypass, state, attrs)
	       );

	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_STALE)
			mdcache_kill_entry(entry);
		goto unlock;
	}

	subcall(
		status = entry->sub_handle->obj_ops.getattrs(
			entry->sub_handle)
	       );

	if (FSAL_IS_ERROR(status)) {
		mdcache_kill_entry(entry);
		goto unlock;
	}

	mdc_fixup_md(entry);

unlock:
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

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

	subcall(
		status = parent->sub_handle->obj_ops.unlink(
			parent->sub_handle, entry->sub_handle, name)
	       );

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);
	(void)mdcache_dirent_remove(parent, name);
	PTHREAD_RWLOCK_unlock(&parent->content_lock);


	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_STALE)
			(void)mdcache_kill_entry(parent);
		else if (status.major == ERR_FSAL_NOTEMPTY &&
			 (obj_hdl->type == DIRECTORY))
			(void)mdcache_dirent_invalidate_all(entry);
	} else
		(void)mdcache_invalidate(entry, MDCACHE_INVALIDATE_ATTRS);

	mdc_unreachable(entry);

	return status;
}

/**
 * @brief Get the digest for a handle
 *
 * Just pass through to the underlying FSAL
 *
 * @param[in] obj_hdl	Handle to digest
 * @param[in] out_type	Type of digest to get
 * @param[out] fh_desc	Buffer to write digest into
 * @return FSAL status
 */
static fsal_status_t mdcache_handle_digest(
				const struct fsal_obj_handle *obj_hdl,
				fsal_digesttype_t out_type,
				struct gsh_buffdesc *fh_desc)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(
		status = entry->sub_handle->obj_ops.handle_digest(
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
		entry->sub_handle->obj_ops.handle_to_key(entry->sub_handle,
							  fh_desc)
	       );
}

/**
 * @brief Get a reference to the handle
 *
 * @param[in] obj_hdl	Handle to digest
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
 * @param[in] obj_hdl	Handle to digest
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
 * @param[in] obj_hdl	Handle to digest
 * @return FSAL status
 */
static void mdcache_hdl_release(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	mdcache_kill_entry(entry);
}

void mdcache_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->get_ref = mdcache_get_ref;
	ops->put_ref = mdcache_put_ref;
	ops->release = mdcache_hdl_release;
	ops->lookup = mdcache_lookup;
	ops->readdir = mdcache_readdir;
	ops->create = mdcache_create;
	ops->mkdir = mdcache_mkdir;
	ops->mknode = mdcache_mknode;
	ops->symlink = mdcache_symlink;
	ops->readlink = mdcache_readlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = mdcache_getattrs;
	ops->setattrs = mdcache_setattrs;
	ops->link = mdcache_link;
	ops->rename = mdcache_rename;
	ops->unlink = mdcache_unlink;
	ops->open = mdcache_open;
	ops->reopen = mdcache_reopen;
	ops->status = mdcache_status;
	ops->read = mdcache_read;
	ops->read_plus = mdcache_read_plus;
	ops->write = mdcache_write;
	ops->write_plus = mdcache_write_plus;
	ops->commit = mdcache_commit;
	ops->lock_op = mdcache_lock_op;
	ops->share_op = mdcache_share_op;
	ops->close = mdcache_close;
	ops->handle_digest = mdcache_handle_digest;
	ops->handle_to_key = mdcache_handle_to_key;

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
	ops->setattr2 = mdcache_setattr2;
	ops->close2 = mdcache_close2;

	/* xattr related functions */
	ops->list_ext_attrs = mdcache_list_ext_attrs;
	ops->getextattr_id_by_name = mdcache_getextattr_id_by_name;
	ops->getextattr_value_by_name = mdcache_getextattr_value_by_name;
	ops->getextattr_value_by_id = mdcache_getextattr_value_by_id;
	ops->setextattr_value = mdcache_setextattr_value;
	ops->setextattr_value_by_id = mdcache_setextattr_value_by_id;
	ops->getextattr_attrs = mdcache_getextattr_attrs;
	ops->remove_extattr_by_id = mdcache_remove_extattr_by_id;
	ops->remove_extattr_by_name = mdcache_remove_extattr_by_name;
	ops->getxattrs = mdcache_getxattrs;
	ops->setxattrs = mdcache_setxattrs;
	ops->removexattrs = mdcache_removexattrs;
	ops->listxattrs = mdcache_listxattrs;

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
 * @param[in] exp_hdl	FSAL export to look in
 * @param[in] path	Path to find
 * @param[out] handle	Resulting object handle
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
fsal_status_t mdcache_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle)
{
	struct fsal_obj_handle *sub_handle = NULL;
	struct mdcache_fsal_export *export =
		container_of(exp_hdl, struct mdcache_fsal_export, export);
	mdcache_entry_t *entry;
	fsal_status_t status;

	*handle = NULL;

	subcall_raw(export,
		status = export->sub_export->exp_ops.lookup_path(
			export->sub_export, path, &sub_handle)
	       );

	status = mdcache_alloc_and_check_handle(export, sub_handle,
						&entry, status);
	if (FSAL_IS_ERROR(status))
		return status;

	*handle = &entry->obj_handle;
	return status;
}

/**
 * @brief Find or create a cache entry from a key
 *
 * This is the equivalent of mdcache_get().  It returns a ref'd entry that
 * must be put using obj_ops.release().
 *
 * @param[in] exp_hdl	Export to search
 * @return FSAL status
 */
fsal_status_t mdcache_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle)
{
	struct mdcache_fsal_export *export =
		container_of(exp_hdl, struct mdcache_fsal_export, export);
	mdcache_key_t key;
	mdcache_entry_t *entry;
	fsal_status_t status;

	*handle = NULL;
	key.fsal = export->sub_export->fsal;

	(void) cih_hash_key(&key, export->sub_export->fsal, hdl_desc,
			    CIH_HASH_KEY_PROTOTYPE);

	status = mdcache_locate_keyed(&key, export, &entry);
	if (FSAL_IS_ERROR(status))
		return status;

	*handle = &entry->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
