// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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

/* handle.c
 */

#include "config.h"

#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "pseudofs_methods.h"
#include "city.h"
#include "nfs_file_handle.h"
#include "display.h"
#include "common_utils.h"

/* Atomic uint64_t that is used to generate inode numbers in the Pseudo FS */
uint64_t inode_number;

#define V4_FH_OPAQUE_SIZE (NFS4_FHSIZE - sizeof(struct file_handle_v4))

/* helpers
 */

static inline int
pseudofs_n_cmpf(const struct avltree_node *lhs,
		const struct avltree_node *rhs)
{
	struct pseudo_fsal_obj_handle *lk, *rk;

	lk = avltree_container_of(lhs, struct pseudo_fsal_obj_handle, avl_n);
	rk = avltree_container_of(rhs, struct pseudo_fsal_obj_handle, avl_n);

	return strcmp(lk->name, rk->name);
}

static inline int
pseudofs_i_cmpf(const struct avltree_node *lhs,
		const struct avltree_node *rhs)
{
	struct pseudo_fsal_obj_handle *lk, *rk;

	lk = avltree_container_of(lhs, struct pseudo_fsal_obj_handle, avl_i);
	rk = avltree_container_of(rhs, struct pseudo_fsal_obj_handle, avl_i);

	if (lk->index < rk->index)
		return -1;

	if (lk->index == rk->index)
		return 0;

	return 1;
}

static inline struct avltree_node *
avltree_inline_name_lookup(const struct avltree_node *key,
			   const struct avltree *tree)
{
	return avltree_inline_lookup(key, tree, pseudofs_n_cmpf);
}

/**
 * @brief Construct the fs opaque part of a pseudofs nfsv4 handle
 *
 * Given the components of a pseudofs nfsv4 handle, the nfsv4 handle is
 * created by concatenating the components. This is the fs opaque piece
 * of struct file_handle_v4 and what is sent over the wire.
 *
 * @param[in] pathbuf Full patch of the pseudofs node
 * @param[in] hashkey a 64 bit hash of the pseudopath parameter
 *
 * @return The nfsv4 pseudofs file handle as a char *
 */
static void package_pseudo_handle(char *buff,
				  struct display_buffer *pathbuf)
{
	ushort len = display_buffer_len(pathbuf);
	int opaque_bytes_used = 0, pathlen = 0;
	uint64_t hashkey = CityHash64(pathbuf->b_start,
				      display_buffer_len(pathbuf));

	memcpy(buff, &hashkey, sizeof(hashkey));
	opaque_bytes_used += sizeof(hashkey);

	/* include length of the path in the handle.
	 * MAXPATHLEN=4096 ... max path length can be contained in a short int.
	 */
	memcpy(buff + opaque_bytes_used, &len, sizeof(len));
	opaque_bytes_used += sizeof(len);

	/* Either the nfsv4 fh opaque size or the length of the pseudopath.
	 * Ideally we can include entire pseudofs pathname for guaranteed
	 * uniqueness of pseudofs handles.
	 */
	pathlen = MIN(V4_FH_OPAQUE_SIZE - opaque_bytes_used, len);
	memcpy(buff + opaque_bytes_used, pathbuf->b_start, pathlen);
	opaque_bytes_used += pathlen;

	/* If there is more space in the opaque handle due to a short pseudofs
	 * path ... zero it.
	 */
	if (opaque_bytes_used < V4_FH_OPAQUE_SIZE) {
		memset(buff + opaque_bytes_used, 0,
		       V4_FH_OPAQUE_SIZE - opaque_bytes_used);
	}
}

/**
 * @brief Concatenate a number of pseudofs tokens into a string
 *
 * When reading pseudofs paths from export entries, we divide the
 * path into tokens. This function will recombine a specific number
 * of those tokens into a string.
 *
 * @param[in/out] pathbuf Must be not NULL. Tokens are copied to here.
 * @param[in] node for which a full pseudopath needs to be formed.
 * @param[in] maxlen maximum number of chars to copy to pathbuf
 *
 * @return void
 */
static int create_fullpath(struct display_buffer *pathbuf,
			   struct pseudo_fsal_obj_handle *this_node)
{
	int b_left;

	if (this_node->parent != NULL)
		b_left = create_fullpath(pathbuf, this_node->parent);
	else
		b_left = display_start(pathbuf);

	/* Add slash for all but root node */
	if (b_left > 0 && this_node->parent != NULL)
		b_left = display_cat(pathbuf, "/");

	/* Append the node's name.
	 * Note that a Pseudo FS root's name is it's full path.
	 */
	if (b_left > 0)
		b_left = display_cat(pathbuf, this_node->name);

	return b_left;
}

/* alloc_handle
 * allocate and fill in a handle
 */

static struct pseudo_fsal_obj_handle
*alloc_directory_handle(struct pseudo_fsal_obj_handle *parent,
			const char *name,
			struct fsal_export *exp_hdl,
			struct fsal_attrlist *attrs)
{
	struct pseudo_fsal_obj_handle *hdl;
	char path[MAXPATHLEN] = "\0";
	struct display_buffer pathbuf = {sizeof(path), path, path};
	int rc;

	hdl = gsh_calloc(1, sizeof(struct pseudo_fsal_obj_handle) +
			    V4_FH_OPAQUE_SIZE);

	/* Establish tree details for this directory */
	hdl->name = gsh_strdup(name);
	hdl->parent = parent;

	if (hdl->name == NULL) {
		LogDebug(COMPONENT_FSAL,
			 "Could not name");
		goto spcerr;
	}

	/* Create the handle */
	hdl->handle = (char *) &hdl[1];

	/* Create the full path */
	rc = create_fullpath(&pathbuf, hdl);

	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "Could not create handle");
		goto spcerr;
	}

	package_pseudo_handle(hdl->handle, &pathbuf);

	hdl->obj_handle.type = DIRECTORY;

	/* Fills the output struct */
	hdl->attributes.type = DIRECTORY;

	hdl->attributes.filesize = 0;

	/* fsid will be supplied later */
	hdl->obj_handle.fsid.major = 0;
	hdl->obj_handle.fsid.minor = 0;
	hdl->attributes.fsid.major = 0;
	hdl->attributes.fsid.minor = 0;

	hdl->obj_handle.fileid = atomic_postinc_uint64_t(&inode_number);
	hdl->attributes.fileid = hdl->obj_handle.fileid;

	hdl->attributes.mode = attrs->mode & (~S_IFMT & 0xFFFF) &
		~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	hdl->attributes.numlinks = 2;
	hdl->numlinks = 2;

	if ((attrs->valid_mask & ATTR_OWNER) != 0)
		hdl->attributes.owner = attrs->owner;
	else
		hdl->attributes.owner = op_ctx->creds.caller_uid;

	if ((attrs->valid_mask & ATTR_GROUP) != 0)
		hdl->attributes.group = attrs->group;
	else
		hdl->attributes.group = op_ctx->creds.caller_gid;

	/* Use full timer resolution */
	now(&hdl->attributes.ctime);

	if ((attrs->valid_mask & ATTR_ATIME) != 0)
		hdl->attributes.atime = attrs->atime;
	else
		hdl->attributes.atime = hdl->attributes.ctime;

	if ((attrs->valid_mask & ATTR_MTIME) != 0)
		hdl->attributes.mtime = attrs->mtime;
	else
		hdl->attributes.mtime = hdl->attributes.ctime;

	hdl->attributes.change = timespec_to_nsecs(&hdl->attributes.ctime);

	hdl->attributes.spaceused = 0;
	hdl->attributes.rawdev.major = 0;
	hdl->attributes.rawdev.minor = 0;

	/* Set the mask at the end. */
	hdl->attributes.valid_mask = PSEUDO_SUPPORTED_ATTRS;
	hdl->attributes.supported = PSEUDO_SUPPORTED_ATTRS;

	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl, DIRECTORY);
	hdl->obj_handle.obj_ops = &PSEUDOFS.handle_ops;

	avltree_init(&hdl->avl_name, pseudofs_n_cmpf, 0 /* flags */);
	avltree_init(&hdl->avl_index, pseudofs_i_cmpf, 0 /* flags */);
	hdl->next_i = 2;
	if (parent != NULL) {
		/* Attach myself to my parent */
		PTHREAD_RWLOCK_wrlock(&parent->obj_handle.obj_lock);
		avltree_insert(&hdl->avl_n, &parent->avl_name);
		hdl->index = (parent->next_i)++;
		avltree_insert(&hdl->avl_i, &parent->avl_index);
		hdl->inavl = true;
		PTHREAD_RWLOCK_unlock(&parent->obj_handle.obj_lock);
	}
	return hdl;

 spcerr:

	if (hdl->name != NULL)
		gsh_free(hdl->name);

	gsh_free(hdl);		/* elvis has left the building */
	return NULL;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path,
			    struct fsal_obj_handle **handle,
			    struct fsal_attrlist *attrs_out)
{
	struct pseudo_fsal_obj_handle *myself, *hdl = NULL;
	struct pseudo_fsal_obj_handle key[1];
	struct avltree_node *node;
	fsal_errors_t error = ERR_FSAL_NOENT;

	myself = container_of(parent,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);

	/* Check if this context already holds the lock on
	 * this directory.
	 */
	if (op_ctx->fsal_private != parent)
		PTHREAD_RWLOCK_rdlock(&parent->obj_lock);
	else
		LogFullDebug(COMPONENT_FSAL,
			     "Skipping lock for %s",
			     myself->name);

	if (strcmp(path, "..") == 0) {
		/* lookup parent - lookupp */
		if (myself->parent != NULL) {
			hdl = myself->parent;
			*handle = &hdl->obj_handle;
			error = ERR_FSAL_NO_ERROR;
			LogFullDebug(COMPONENT_FSAL,
				     "Found %s/%s hdl=%p",
				     myself->name, path, hdl);
		}

		goto out;
	}

	key->name = (char *) path;
	node = avltree_inline_name_lookup(&key->avl_n, &myself->avl_name);
	if (node) {
		hdl = avltree_container_of(node, struct pseudo_fsal_obj_handle,
					   avl_n);
		*handle = &hdl->obj_handle;
		error = ERR_FSAL_NO_ERROR;
			LogFullDebug(COMPONENT_FSAL,
				     "Found %s/%s hdl=%p",
				     myself->name, path, hdl);
	}

out:

	if (error == ERR_FSAL_NOENT && is_export_update_in_progress()) {
		/* An export update may be the cause of the failure. Tell the
		 * client to retry.
		 */
		LogDebug(COMPONENT_EXPORT,
			 "PseudoFS LOOKUP of %s may have failed due to export update",
			 path);

		error = ERR_FSAL_DELAY;
	}

	if (op_ctx->fsal_private != parent)
		PTHREAD_RWLOCK_unlock(&parent->obj_lock);

	if (error == ERR_FSAL_NO_ERROR && attrs_out != NULL) {
		/* This is unlocked, however, for the most part, attributes
		 * are read-only. Come back later and do some lock protection.
		 */
		fsal_copy_attrs(attrs_out, &hdl->attributes, false);
	}

	return fsalstat(error, 0);
}

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * While FSAL_PSEUDO is a support_ex FSAL, it doesn't actually support
 * setting attributes, so only the mode attribute is relevant. Any other
 * attributes set on creation will be ignored. The owner and group will be
 * set from the active credentials.
 *
 * @param[in]     dir_hdl   Directory in which to create the directory
 * @param[in]     name      Name of directory to create
 * @param[in]     attrs_in  Attributes to set on newly created object
 * @param[out]    handle    Newly created object
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */
static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name,
			     struct fsal_attrlist *attrs_in,
			     struct fsal_obj_handle **handle,
			     struct fsal_attrlist *attrs_out)
{
	struct pseudo_fsal_obj_handle *myself, *hdl;
	uint32_t numlinks;

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */

	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	myself = container_of(dir_hdl,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_directory_handle(myself,
				     name,
				     op_ctx->fsal_export,
				     attrs_in);

	numlinks = atomic_inc_uint32_t(&myself->numlinks);

	LogFullDebug(COMPONENT_FSAL,
		     "%s numlinks %"PRIu32,
		     myself->name, numlinks);

	*handle = &hdl->obj_handle;

	if (attrs_out != NULL)
		fsal_copy_attrs(attrs_out, &hdl->attributes, false);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * read_dirents
 * read the directory and call through the callback function for
 * each entry.
 * @param dir_hdl [IN] the directory to read
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker true == end of dir
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence,
				  void *dir_state,
				  fsal_readdir_cb cb,
				  attrmask_t attrmask,
				  bool *eof)
{
	struct pseudo_fsal_obj_handle *myself, *hdl;
	struct avltree_node *node;
	fsal_cookie_t seekloc;
	struct fsal_attrlist attrs;
	enum fsal_dir_result cb_rc;

	if (whence != NULL)
		seekloc = *whence;
	else
		seekloc = 2;    /* start from index 2, if no cookie */

	*eof = true;

	myself = container_of(dir_hdl,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);

	LogDebug(COMPONENT_FSAL,
		 "hdl=%p, name=%s",
		 myself, myself->name);

	PTHREAD_RWLOCK_rdlock(&dir_hdl->obj_lock);

	/* Use fsal_private to signal to lookup that we hold
	 * the lock.
	 */
	op_ctx->fsal_private = dir_hdl;

	for (node = avltree_first(&myself->avl_index);
	     node != NULL;
	     node = avltree_next(node)) {
		hdl = avltree_container_of(node,
					   struct pseudo_fsal_obj_handle,
					   avl_i);
		/* skip entries before seekloc */
		if (hdl->index < seekloc)
			continue;

		fsal_prepare_attrs(&attrs, attrmask);
		fsal_copy_attrs(&attrs, &hdl->attributes, false);

		cb_rc = cb(hdl->name, &hdl->obj_handle, &attrs,
			   dir_state, hdl->index + 1);

		fsal_release_attrs(&attrs);

		/* Read ahead not supported by this FSAL. */
		if (cb_rc >= DIR_READAHEAD) {
			*eof = false;
			break;
		}
	}

	op_ctx->fsal_private = NULL;

	PTHREAD_RWLOCK_unlock(&dir_hdl->obj_lock);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			      struct fsal_attrlist *outattrs)
{
	struct pseudo_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);

	if (myself->parent != NULL && !myself->inavl) {
		/* Removed entry - stale */
		LogDebug(COMPONENT_FSAL,
			 "Requesting attributes for removed entry %p, name=%s",
			 myself, myself->name);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	/* We need to update the numlinks under attr lock. */
	myself->attributes.numlinks = atomic_fetch_uint32_t(&myself->numlinks);
	*outattrs = myself->attributes;

	LogFullDebug(COMPONENT_FSAL,
		     "hdl=%p, name=%s numlinks %"PRIu32,
		     myself,
		     myself->name,
		     myself->attributes.numlinks);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *obj_hdl,
				 const char *name)
{
	struct pseudo_fsal_obj_handle *myself, *hdl;
	fsal_errors_t error = ERR_FSAL_NOENT;
	uint32_t numlinks;

	myself = container_of(dir_hdl,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);
	hdl = container_of(obj_hdl,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);

	PTHREAD_RWLOCK_wrlock(&dir_hdl->obj_lock);

	/* Check if directory is empty */
	numlinks = atomic_fetch_uint32_t(&hdl->numlinks);
	if (numlinks != 2) {
		LogFullDebug(COMPONENT_FSAL,
			     "%s numlinks %"PRIu32,
			     hdl->name, numlinks);
		error = ERR_FSAL_NOTEMPTY;
		goto unlock;
	}

	/* We need to update the numlinks. */
	numlinks = atomic_dec_uint32_t(&myself->numlinks);
	LogFullDebug(COMPONENT_FSAL,
		     "%s numlinks %"PRIu32,
		     myself->name, numlinks);

	/* Remove from directory's name and index avls */
	avltree_remove(&hdl->avl_n, &myself->avl_name);
	avltree_remove(&hdl->avl_i, &myself->avl_index);
	hdl->inavl = false;

	error = ERR_FSAL_NO_ERROR;

	now(&myself->attributes.mtime);
	myself->attributes.ctime = myself->attributes.mtime;
	myself->attributes.change = timespec_to_nsecs(
					&myself->attributes.mtime);

unlock:
	PTHREAD_RWLOCK_unlock(&dir_hdl->obj_lock);

	return fsalstat(error, 0);
}

/* handle_to_wire
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t handle_to_wire(const struct fsal_obj_handle *obj_hdl,
				    fsal_digesttype_t output_type,
				    struct gsh_buffdesc *fh_desc)
{
	const struct pseudo_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      const struct pseudo_fsal_obj_handle,
			      obj_handle);

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < V4_FH_OPAQUE_SIZE) {
			LogMajor(COMPONENT_FSAL,
				 "Space too small for handle.  need %lu, have %zu",
				 ((unsigned long) V4_FH_OPAQUE_SIZE),
				 fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		}

		memcpy(fh_desc->addr, myself->handle, V4_FH_OPAQUE_SIZE);
		fh_desc->len = V4_FH_OPAQUE_SIZE;
		break;

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * handle_to_key
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	struct pseudo_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);

	fh_desc->addr = myself->handle;
	fh_desc->len = V4_FH_OPAQUE_SIZE;
}

/**
 * @brief release object handle
 *
 * release our export first so they know we are gone
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	struct pseudo_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      struct pseudo_fsal_obj_handle,
			      obj_handle);

	if (myself->parent == NULL || myself->inavl) {
		/* Entry is still live */
		LogDebug(COMPONENT_FSAL,
			 "Releasing live hdl=%p, name=%s, don't deconstruct it",
			 myself, myself->name);
		return;
	}

	fsal_obj_handle_fini(obj_hdl);

	LogDebug(COMPONENT_FSAL,
		 "Releasing obj_hdl=%p, myself=%p, name=%s",
		 obj_hdl, myself, myself->name);

	if (myself->name != NULL)
		gsh_free(myself->name);

	gsh_free(myself);
}

void pseudofs_handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->release = release;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->mkdir = makedir;
	ops->getattrs = getattrs;
	ops->unlink = file_unlink;
	ops->handle_to_wire = handle_to_wire;
	ops->handle_to_key = handle_to_key;
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t pseudofs_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out)
{
	struct pseudofs_fsal_export *myself;
	struct fsal_attrlist attrs;

	myself = container_of(exp_hdl, struct pseudofs_fsal_export, export);

	if (strcmp(path, myself->export_path) != 0) {
		/* Lookup of a path other than the export's root. */
		LogCrit(COMPONENT_FSAL,
			"Attempt to lookup non-root path %s",
			path);
		return fsalstat(ERR_FSAL_NOENT, ENOENT);
	}

	attrs.valid_mask = ATTR_MODE;
	attrs.mode = 0755;

	if (myself->root_handle == NULL) {
		myself->root_handle =
			alloc_directory_handle(NULL,
					       myself->export_path,
					       exp_hdl,
					       &attrs);
	}

	*handle = &myself->root_handle->obj_handle;

	if (attrs_out != NULL)
		fsal_copy_attrs(attrs_out, &myself->root_handle->attributes,
				false);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in mdcache etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket, nor reliably on block or
 * character special devices.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t pseudofs_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle,
				   struct fsal_attrlist *attrs_out)
{
	struct glist_head *glist;
	struct fsal_obj_handle *hdl;
	struct pseudo_fsal_obj_handle *my_hdl;

	*handle = NULL;

	if (hdl_desc->len != V4_FH_OPAQUE_SIZE) {
		LogCrit(COMPONENT_FSAL,
			"Invalid handle size %zu expected %lu",
			hdl_desc->len,
			((unsigned long) V4_FH_OPAQUE_SIZE));

		return fsalstat(ERR_FSAL_BADHANDLE, 0);
	}

	PTHREAD_RWLOCK_rdlock(&exp_hdl->fsal->lock);

	glist_for_each(glist, &exp_hdl->fsal->handles) {
		hdl = glist_entry(glist, struct fsal_obj_handle, handles);

		my_hdl = container_of(hdl,
				      struct pseudo_fsal_obj_handle,
				      obj_handle);

		if (memcmp(my_hdl->handle,
			   hdl_desc->addr,
			   V4_FH_OPAQUE_SIZE) == 0) {
			LogDebug(COMPONENT_FSAL,
				 "Found hdl=%p name=%s",
				 my_hdl, my_hdl->name);

			*handle = hdl;

			PTHREAD_RWLOCK_unlock(&exp_hdl->fsal->lock);

			if (attrs_out != NULL) {
				fsal_copy_attrs(attrs_out, &my_hdl->attributes,
						false);
			}

			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		}
	}

	if (is_export_update_in_progress()) {
		/* An export update may be the cause of the failure. Tell the
		 * client to retry.
		 */
		PTHREAD_RWLOCK_unlock(&exp_hdl->fsal->lock);

		LogDebug(COMPONENT_EXPORT,
			 "PseudoFS create handle may have failed due to export update");

		return fsalstat(ERR_FSAL_DELAY, 0);
	}

	LogDebug(COMPONENT_FSAL,
		"Could not find handle");

	PTHREAD_RWLOCK_unlock(&exp_hdl->fsal->lock);

	return fsalstat(ERR_FSAL_STALE, ESTALE);
}
