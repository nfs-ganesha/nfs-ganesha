// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
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
 * KVSFS (via KVSNS) object (file|dir) handle object
 */

#include "config.h"

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <mntent.h>
#include "gsh_list.h"
#include "fsal.h"
#include "kvsfs_fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include "kvsfs_methods.h"
#include <stdbool.h>

/* helpers
 */

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */
struct kvsfs_fsal_obj_handle *kvsfs_alloc_handle(struct kvsfs_file_handle *fh,
						 struct fsal_attrlist *attr,
						 const char *link_content,
						 struct fsal_export *exp_hdl)
{
	struct kvsfs_fsal_export *myself =
		container_of(exp_hdl, struct kvsfs_fsal_export, export);
	struct kvsfs_fsal_obj_handle *hdl;
	struct kvsfs_fsal_module *my_module;

	my_module = container_of(exp_hdl->fsal,
				 struct kvsfs_fsal_module,
				 fsal);

	hdl = gsh_malloc(sizeof(struct kvsfs_fsal_obj_handle) +
			 sizeof(struct kvsfs_file_handle));

	memset(hdl, 0,
	       (sizeof(struct kvsfs_fsal_obj_handle) +
		sizeof(struct kvsfs_file_handle)));
	hdl->handle = (struct kvsfs_file_handle *)&hdl[1];
	memcpy(hdl->handle, fh, sizeof(struct kvsfs_file_handle));

	hdl->obj_handle.type = attr->type;
	hdl->obj_handle.fsid = attr->fsid;
	hdl->obj_handle.fileid = attr->fileid;

	if ((hdl->obj_handle.type == SYMBOLIC_LINK) &&
	    (link_content != NULL)) {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = gsh_malloc(len);
		memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
	}
	fsal_obj_handle_init(&hdl->obj_handle,
			     exp_hdl,
			     attr->type);

	hdl->obj_handle.obj_ops = &my_module->handle_ops;
	if (myself->pnfs_mds_enabled)
		handle_ops_pnfs(hdl->obj_handle.obj_ops);
	return hdl;
}

static struct kvsfs_fsal_obj_handle *alloc_handle(struct kvsfs_file_handle *fh,
						 struct stat *stat,
						 const char *link_content,
						 struct fsal_export *exp_hdl)
{
	struct fsal_attrlist attr;

	posix2fsal_attributes_all(stat, &attr);

	return kvsfs_alloc_handle(fh, &attr, link_content, exp_hdl);
}


/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t kvsfs_lookup(struct fsal_obj_handle *parent,
				 const char *path,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out)
{
	struct kvsfs_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct stat stat;
	struct kvsfs_file_handle fh;
	kvsns_cred_t cred;
	kvsns_ino_t object;

	if (!path)
		return fsalstat(ERR_FSAL_FAULT, 0);

	memset(&fh, 0, sizeof(struct kvsfs_file_handle));
	parent_hdl =
	    container_of(parent, struct kvsfs_fsal_obj_handle, obj_handle);

	LogFullDebug(COMPONENT_FSAL, "lookup: %d/%s",
		 (unsigned int)parent_hdl->handle->kvsfs_handle, path);

	if (!fsal_obj_handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	/* Do we lookup for parent or current directory ? */
	if (!strcmp(path, ".")) {
		retval = 0;
		object = parent_hdl->handle->kvsfs_handle;
	} else if (!strcmp(path, ".."))
		retval = kvsns_lookupp(&cred,
				       &parent_hdl->handle->kvsfs_handle,
				       &object);
	else
		retval = kvsns_lookup(&cred,
				      &parent_hdl->handle->kvsfs_handle,
				      (char *)path, &object);

	if (retval) {
		fsal_error = posix2fsal_error(-retval);
		goto errout;
	}

	retval = kvsns_getattr(&cred, &object, &stat);
	if (retval) {
		fsal_error = posix2fsal_error(-retval);
		goto errout;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, NULL, op_ctx->fsal_export);

	*handle = &hdl->obj_handle;

	hdl->handle->kvsfs_handle = object;

	if (attrs_out != NULL)
		posix2fsal_attributes_all(&stat, attrs_out);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	return fsalstat(fsal_error, -retval);
}

/* lookup_path
 * should not be used for "/" only is exported */

fsal_status_t kvsfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle,
			       struct fsal_attrlist *attrs_out)
{
	kvsns_ino_t object;
	int rc = 0;
	struct kvsfs_file_handle fh;
	struct stat stat;
	kvsns_cred_t cred;
	struct kvsfs_fsal_obj_handle *hdl;

	if (strcmp(path, "/")) {
		LogMajor(COMPONENT_FSAL,
			 "KVSFS can only mount /, no subdirectory");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	LogFullDebug(COMPONENT_FSAL, "lookup_path: %s", path);

	rc = kvsns_get_root(&object);
	if (rc != 0)
		return fsalstat(posix2fsal_error(-rc), -rc);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;


	rc = kvsns_getattr(&cred, &object, &stat);
	if (rc != 0)
		return fsalstat(posix2fsal_error(-rc), -rc);

	fh.kvsfs_handle = object;

	hdl = alloc_handle(&fh, &stat, NULL, exp_hdl);

	*handle = &hdl->obj_handle;

	if (attrs_out != NULL)
		posix2fsal_attributes_all(&stat, attrs_out);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_create2(struct fsal_obj_handle *dir_hdl,
			    const char *filename,
			    const struct req_op_context *op_ctx,
			    mode_t unix_mode,
			    struct kvsfs_file_handle *kvsfs_fh,
			    int posix_flags,
			    struct fsal_attrlist *fsal_attr)
{
	struct kvsfs_fsal_obj_handle *myself, *hdl;
	int retval = 0;
	kvsns_cred_t cred;
	kvsns_ino_t object;
	struct stat stat;

	/* note : fsal_attr is optional. */
	if (!dir_hdl || !op_ctx || !kvsfs_fh || !filename)
		return fsalstat(ERR_FSAL_FAULT, 0);

	LogFullDebug(COMPONENT_FSAL, "Creation mode: 0%o", unix_mode);

	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(kvsfs_fh, 0, sizeof(struct kvsfs_file_handle));
	myself = container_of(dir_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	LogFullDebug(COMPONENT_FSAL, "create2: %d/%s mode=0%o",
		 (unsigned int)myself->handle->kvsfs_handle, filename,
		 unix_mode);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_creat(&cred,
			     &myself->handle->kvsfs_handle,
			     (char *)filename,
			     unix_mode,
			     &object);
	if (retval)
		goto fileerr;

	retval = kvsns_getattr(&cred, &object, &stat);
	if (retval)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(kvsfs_fh, &stat, NULL, op_ctx->fsal_export);

	/* >> set output handle << */
	hdl->handle->kvsfs_handle = object;
	kvsfs_fh->kvsfs_handle = object; /* Useful ? */

	if (fsal_attr != NULL)
		posix2fsal_attributes_all(&stat, fsal_attr);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	return fsalstat(posix2fsal_error(-retval), -retval);
}


static fsal_status_t kvsfs_mkdir(struct fsal_obj_handle *dir_hdl,
				const char *name,
				struct fsal_attrlist *attrib,
				struct fsal_obj_handle **handle,
				struct fsal_attrlist *attrs_out)
{
	struct kvsfs_fsal_obj_handle *myself, *hdl;
	int retval = 0;
	struct kvsfs_file_handle fh;
	kvsns_cred_t cred;
	struct stat stat;

	*handle = NULL;		/* poison it */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(&fh, 0, sizeof(struct kvsfs_file_handle));
	myself = container_of(dir_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	LogFullDebug(COMPONENT_FSAL, "mkdir: %d/%s",
		 (unsigned int)myself->handle->kvsfs_handle, name);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_mkdir(&cred, &myself->handle->kvsfs_handle, (char *)name,
			     fsal2unix_mode(attrib->mode), &fh.kvsfs_handle);
	if (retval)
		goto fileerr;
	retval = kvsns_getattr(&cred, &fh.kvsfs_handle, &stat);
	if (retval)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, NULL, op_ctx->fsal_export);

	/* >> set output handle << */
	*handle = &hdl->obj_handle;

	if (attrs_out != NULL)
		posix2fsal_attributes_all(&stat, attrs_out);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	return fsalstat(posix2fsal_error(-retval), -retval);
}

static fsal_status_t kvsfs_makenode(struct fsal_obj_handle *dir_hdl,
				   const char *name,
				   object_file_type_t nodetype,	/* IN */
				   struct fsal_attrlist *attrib,
				   struct fsal_obj_handle **handle,
				   struct fsal_attrlist *attrsout)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/*! \brief Merge a duplicate handle with an original handle
 *  *
 *   * \see fsal_api.h for more information
 *    */
static fsal_status_t kvsfs_merge(struct fsal_obj_handle *orig_hdl,
				 struct fsal_obj_handle *dupe_hdl)
{
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	if (orig_hdl->type == REGULAR_FILE && dupe_hdl->type == REGULAR_FILE) {
		struct kvsfs_fsal_obj_handle *orig;
		struct kvsfs_fsal_obj_handle *dupe;

		orig = container_of(orig_hdl,
				    struct kvsfs_fsal_obj_handle,
				    obj_handle);
		dupe = container_of(dupe_hdl,
				    struct kvsfs_fsal_obj_handle,
				    obj_handle);

		/* This can block over an I/O operation. */
		status = merge_share(orig_hdl, &orig->u.file.share,
				     &dupe->u.file.share);
	}

	return status;
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */


static fsal_status_t kvsfs_makesymlink(struct fsal_obj_handle *dir_hdl,
				      const char *name, const char *link_path,
				      struct fsal_attrlist *attrib,
				      struct fsal_obj_handle **handle,
				      struct fsal_attrlist *attrsout)
{
	struct kvsfs_fsal_obj_handle *myself, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	kvsns_cred_t cred;
	kvsns_ino_t object;
	struct stat stat;

	struct kvsfs_file_handle fh;

	*handle = NULL;		/* poison it first */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(&fh, 0, sizeof(struct kvsfs_file_handle));
	myself = container_of(dir_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);


	LogFullDebug(COMPONENT_FSAL, "makesymlink: %d/%s -> %s",
		 (unsigned int)myself->handle->kvsfs_handle, name,
		 link_path);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_symlink(&cred, &myself->handle->kvsfs_handle,
			       (char *)name, (char *)link_path, &object);
	if (retval)
		goto err;

	retval = kvsns_getattr(&cred, &object, &stat);
	if (retval)
		goto err;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, link_path, op_ctx->fsal_export);

	*handle = &hdl->obj_handle;
	hdl->handle->kvsfs_handle = object;

	if (attrsout != NULL)
		posix2fsal_attributes_all(&stat, attrsout);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 err:
	if (retval == -ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(-retval);
	return fsalstat(fsal_error, -retval);
}

static fsal_status_t kvsfs_readsymlink(struct fsal_obj_handle *obj_hdl,
				      struct gsh_buffdesc *link_content,
				      bool refresh)
{
	struct kvsfs_fsal_obj_handle *myself = NULL;
	int retval = 0;
	int retlink = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	kvsns_cred_t cred;

	if (obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	LogFullDebug(COMPONENT_FSAL, "readsymlink: %d",
		 (unsigned int)myself->handle->kvsfs_handle);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	/* The link length should be cached in the file handle */

	link_content->len = fsal_default_linksize;
	link_content->addr = gsh_malloc(link_content->len);

	retlink = kvsns_readlink(&cred, &myself->handle->kvsfs_handle,
				 link_content->addr, &link_content->len);

	if (retlink) {
		fsal_error = posix2fsal_error(-retlink);
		gsh_free(link_content->addr);
		link_content->addr = NULL;
		link_content->len = 0;
		goto out;
	}

	link_content->len = strlen(link_content->addr) + 1;
 out:
	return fsalstat(fsal_error, -retval);
}

static fsal_status_t kvsfs_linkfile(struct fsal_obj_handle *obj_hdl,
				   struct fsal_obj_handle *destdir_hdl,
				   const char *name)
{
	struct kvsfs_fsal_obj_handle *myself, *destdir;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	kvsns_cred_t cred;

	myself = container_of(obj_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	destdir =
	    container_of(destdir_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	LogFullDebug(COMPONENT_FSAL, "linkfile: %d -> %d/%s",
		 (unsigned int)myself->handle->kvsfs_handle,
		 (unsigned int)destdir->handle->kvsfs_handle, name);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_link(&cred, &myself->handle->kvsfs_handle,
			    &destdir->handle->kvsfs_handle, (char *)name);
	if (retval)
		fsal_error = posix2fsal_error(-retval);

	return fsalstat(fsal_error, -retval);
}

#define MAX_ENTRIES 256

/**
 * read_dirents
 * read the directory and call through the callback function for
 * each entry.
 * @param dir_hdl [IN] the directory to read
 * @param entry_cnt [IN] limit of entries. 0 implies no limit
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker true == end of dir
 */
static fsal_status_t kvsfs_readdir(struct fsal_obj_handle *dir_hdl,
				   fsal_cookie_t *whence,
				   void *dir_state,
				   fsal_readdir_cb cb,
				   attrmask_t attrmask,
				   bool *eof)
{
	struct kvsfs_fsal_obj_handle *myself;
	int retval = 0;
	off_t seekloc = 0;
	kvsns_cred_t cred;
	kvsns_dentry_t dirents[MAX_ENTRIES];
	unsigned int index = 0;
	unsigned int nb_rddir_done = 0;
	int size = 0;
	kvsns_dir_t ddir;
	struct fsal_attrlist attrs;
	fsal_status_t status;
	struct fsal_obj_handle *hdl;
	int cb_rc;
	fsal_cookie_t cookie;

	if (whence != NULL)
		seekloc = (off_t) *whence;

	/* Think about '.' and '..' */
#define DOTS_OFFSET 2
	if (seekloc > 0)
		seekloc = seekloc - DOTS_OFFSET;

	myself =
		container_of(dir_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_opendir(&cred, &myself->handle->kvsfs_handle,
			       &ddir);
	if (retval < 0)
		goto out;

	/* Open the directory */
	nb_rddir_done = 0;
	*eof = false;
	do {
		size = MAX_ENTRIES;

		memset(dirents, 0, sizeof(kvsns_dentry_t) * MAX_ENTRIES);

		retval = kvsns_readdir(&cred, &ddir, seekloc,
				       dirents, &size);
		if (retval)
			goto out;

		if (size < MAX_ENTRIES)
			*eof = true;

		for (index = 0; index < size; index++) {

			fsal_prepare_attrs(&attrs, attrmask);

			status = kvsfs_lookup(dir_hdl,
					      dirents[index].name,
					      &hdl,
					      &attrs);

			if (FSAL_IS_ERROR(status)) {
				kvsns_closedir(&ddir);
				return status;
			}

			/* callback to mdcache */
			cookie = seekloc + index +
				 (nb_rddir_done * MAX_ENTRIES) +
				 DOTS_OFFSET + 1;

			cb_rc = cb(dirents[index].name,
				   hdl,
				   &attrs,
				   dir_state,
				   cookie);

			LogFullDebug(COMPONENT_FSAL,
				 "readdir: %s cookie=%llu cb_rc=%d",
				 dirents[index].name,
				 (unsigned long long)cookie,
				 cb_rc);

			fsal_release_attrs(&attrs);

			if (cb_rc >= DIR_READAHEAD) {
				/* if we did not reach the last entry in
				 * dirents array, then EOF is not reached */
				if (*eof == true && index < size)
					*eof = false;
				goto done;
			}
		}

		seekloc += MAX_ENTRIES;
		nb_rddir_done += 1;
	} while (size != 0 && *eof == false);

 done:
	retval = kvsns_closedir(&ddir);
	if (retval < 0)
		goto out;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
 out:
	return fsalstat(posix2fsal_error(-retval), -retval);
}

static fsal_status_t kvsfs_rename(struct fsal_obj_handle *obj_hdl,
				 struct fsal_obj_handle *olddir_hdl,
				 const char *old_name,
				 struct fsal_obj_handle *newdir_hdl,
				 const char *new_name)
{
	struct kvsfs_fsal_obj_handle *olddir, *newdir;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	kvsns_cred_t cred;

	olddir =
	    container_of(olddir_hdl, struct kvsfs_fsal_obj_handle, obj_handle);
	newdir =
	    container_of(newdir_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_rename(&cred,
			      &olddir->handle->kvsfs_handle, (char *)old_name,
			      &newdir->handle->kvsfs_handle, (char *)new_name);

	if (retval)
		fsal_error = posix2fsal_error(-retval);
	return fsalstat(fsal_error, -retval);
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t kvsfs_getattrs(struct fsal_obj_handle *obj_hdl,
				    struct fsal_attrlist *attrs)
{
	struct kvsfs_fsal_obj_handle *myself;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	kvsns_cred_t cred;

	myself =
		container_of(obj_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_getattr(&cred, &myself->handle->kvsfs_handle, &stat);

	if (retval)
		goto errout;

	/* convert attributes */
	if (attrs != NULL)
		posix2fsal_attributes_all(&stat, attrs);


	goto out;

 errout:
	if (retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(-retval);
 out:
	return fsalstat(fsal_error, -retval);
}

/*
 * NOTE: this is done under protection of the attributes rwlock
 * in the cache entry.
 */

static fsal_status_t kvsfs_setattr2(struct fsal_obj_handle *obj_hdl,
				    bool bypass,
				    struct state_t *state,
				    struct fsal_attrlist *attrs)
{
	struct kvsfs_fsal_obj_handle *myself;
	struct stat stats = { 0 };
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int flags = 0;
	kvsns_cred_t cred;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_MODE))
		attrs->mode &=
		   ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	myself =
		container_of(obj_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	/* First, check that FSAL attributes */
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			fsal_error = ERR_FSAL_INVAL;
			return fsalstat(fsal_error, retval);
		}
		flags |= STAT_SIZE_SET;
		stats.st_size = attrs->filesize;
	}
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_MODE)) {
		flags |= STAT_MODE_SET;
		stats.st_mode = fsal2unix_mode(attrs->mode);
	}
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_OWNER)) {
		flags |= STAT_UID_SET;
		stats.st_uid = attrs->owner;
	}
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_GROUP)) {
		flags |= STAT_GID_SET;
		stats.st_gid = attrs->group;
	}
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_ATIME)) {
		flags |= STAT_ATIME_SET;
		stats.st_atime = attrs->atime.tv_sec;
	}
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_ATIME_SERVER)) {
		flags |= STAT_ATIME_SET;
		struct timespec timestamp;

		retval = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (retval != 0)
			goto out;
		stats.st_atim = timestamp;
	}
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_MTIME)) {
		flags |= STAT_MTIME_SET;
		stats.st_mtime = attrs->mtime.tv_sec;
	}
	if (FSAL_TEST_MASK(attrs->valid_mask, ATTR_MTIME_SERVER)) {
		flags |= STAT_MTIME_SET;
		struct timespec timestamp;

		retval = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (retval != 0)
			goto out;
		stats.st_mtim = timestamp;
	}
	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_setattr(&cred, &myself->handle->kvsfs_handle,
			       &stats, flags);

	if (retval)
		goto out;

 out:
	if (retval == 0)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	/* Exit with an error */
	fsal_error = posix2fsal_error(-retval);
	return fsalstat(fsal_error, -retval);
}


static fsal_status_t kvsfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct kvsfs_fsal_obj_handle *myself;
	fsal_status_t status;
	int retval = 0;

	assert(obj_hdl->type == REGULAR_FILE);
	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	if (myself->u.file.fd.openflags == FSAL_O_CLOSED)
		status = fsalstat(ERR_FSAL_NOT_OPENED, 0);
	else {
		retval = kvsns_close(&myself->u.file.fd.fd);
		status = fsalstat(posix2fsal_error(-retval), -retval);
		memset(&myself->u.file.fd.fd, 0, sizeof(kvsns_file_open_t));
		myself->u.file.fd.openflags = FSAL_O_CLOSED;
	}

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

/* file_unlink
 * unlink the named file in the directory
 */
static fsal_status_t kvsfs_unlink(struct fsal_obj_handle *dir_hdl,
				  struct fsal_obj_handle *obj_hdl,
				  const char *name)
{
	struct kvsfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	kvsns_cred_t cred;

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	myself =
		container_of(dir_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->type == DIRECTORY)
		retval = kvsns_rmdir(&cred,
				     &myself->handle->kvsfs_handle,
				     (char *)name);
	else
		retval = kvsns_unlink(
				&cred,
				&myself->handle->kvsfs_handle,
				(char *)name);

	if (retval)
		fsal_error = posix2fsal_error(-retval);

	return fsalstat(fsal_error, -retval);
}

/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t kvsfs_handle_to_wire(const struct fsal_obj_handle *obj_hdl,
					  fsal_digesttype_t output_type,
					  struct gsh_buffdesc *fh_desc)
{
	const struct kvsfs_fsal_obj_handle *myself;
	struct kvsfs_file_handle *fh;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself =
	    container_of(obj_hdl, const struct kvsfs_fsal_obj_handle,
			 obj_handle);
	fh = myself->handle;

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = kvsfs_sizeof_handle(fh);
		if (fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, fh, fh_size);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	LogMajor(COMPONENT_FSAL,
		 "Space too small for handle.  need %lu, have %lu", fh_size,
		 fh_desc->len);
	return fsalstat(ERR_FSAL_TOOSMALL, 0);
}

/**
 * @brief release object handle
 *
 * release our export first so they know we are gone
 */
static void kvsfs_release(struct fsal_obj_handle *obj_hdl)
{
	struct kvsfs_fsal_obj_handle *myself;
	object_file_type_t type = obj_hdl->type;

	myself = container_of(obj_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	fsal_obj_handle_fini(obj_hdl);

	if (type == SYMBOLIC_LINK) {
		if (myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	}
	gsh_free(myself);
}

/* export methods that create object handles
 */

/**
 * handle_to_key
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 */

static void kvsfs_handle_to_key(struct fsal_obj_handle *obj_hdl,
				struct gsh_buffdesc *fh_desc)
{
	struct kvsfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	fh_desc->addr = myself->handle;
	fh_desc->len = kvsfs_sizeof_handle(myself->handle);
}

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in mdcache etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t kvsfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out)
{
	struct kvsfs_fsal_obj_handle *hdl;
	struct kvsfs_file_handle fh;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	char link_buff[PATH_MAX];
	char *link_content = NULL;
	struct stat stat;
	kvsns_cred_t cred;
	int retval;
	size_t size;

	*handle = NULL;		/* poison it first */
	if (hdl_desc->len > sizeof(struct kvsfs_file_handle))
		return fsalstat(ERR_FSAL_FAULT, 0);

	memcpy(&fh, hdl_desc->addr, hdl_desc->len);  /* struct aligned copy */

	LogFullDebug(COMPONENT_FSAL, "create_handle: %d",
		 (unsigned int)fh.kvsfs_handle);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	retval = kvsns_getattr(&cred, &fh.kvsfs_handle, &stat);
	if (retval)
		return fsalstat(posix2fsal_error(-retval), -retval);

	link_content = NULL;
	if (S_ISLNK(stat.st_mode)) {
		size = PATH_MAX;
		retval = kvsns_readlink(&cred, &fh.kvsfs_handle,
					link_buff, &size);
		if (retval)
			return fsalstat(posix2fsal_error(-retval), -retval);
		link_content = link_buff;
	}
	hdl = alloc_handle(&fh, &stat, link_content, exp_hdl);

	*handle = &hdl->obj_handle;

	if (attrs_out != NULL)
		posix2fsal_attributes_all(&stat, attrs_out);

	return fsalstat(fsal_error, 0);
}

void kvsfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->release = kvsfs_release;
	ops->merge = kvsfs_merge;
	ops->lookup = kvsfs_lookup;
	ops->mkdir = kvsfs_mkdir;
	ops->mknode = kvsfs_makenode;
	ops->readdir = kvsfs_readdir;
	ops->symlink = kvsfs_makesymlink;
	ops->readlink = kvsfs_readsymlink;
	ops->getattrs = kvsfs_getattrs;
	ops->link = kvsfs_linkfile;
	ops->rename = kvsfs_rename;
	ops->unlink = kvsfs_unlink;
	ops->close = kvsfs_close;
	ops->handle_to_wire = kvsfs_handle_to_wire;
	ops->handle_to_key = kvsfs_handle_to_key;

	ops->open2 = kvsfs_open2;
	ops->status2 = kvsfs_status2;
	ops->reopen2 = kvsfs_reopen2;
	ops->read2 = kvsfs_read2;
	ops->write2 = kvsfs_write2;
	ops->commit2 = kvsfs_commit2;
	ops->setattr2 = kvsfs_setattr2;
	ops->close2 = kvsfs_close2;

	// ops->create = kvsfs_create;
	// ops->test_access = fsal_test_access;

	/* xattr related functions */
	ops->list_ext_attrs = kvsfs_list_ext_attrs;
	ops->getextattr_id_by_name = kvsfs_getextattr_id_by_name;
	ops->getextattr_value_by_name = kvsfs_getextattr_value_by_name;
	ops->getextattr_value_by_id = kvsfs_getextattr_value_by_id;
	ops->setextattr_value = kvsfs_setextattr_value;
	ops->setextattr_value_by_id = kvsfs_setextattr_value_by_id;
	ops->remove_extattr_by_id = kvsfs_remove_extattr_by_id;
	ops->remove_extattr_by_name = kvsfs_remove_extattr_by_name;
}
