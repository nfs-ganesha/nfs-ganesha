/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
#include "fsal_internal.h"
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
static struct kvsfs_fsal_obj_handle *alloc_handle(struct kvsfs_file_handle *fh,
						struct stat *stat,
						const char *link_content,
						struct fsal_export *exp_hdl)
{
	struct kvsfs_fsal_export *myself =
		container_of(exp_hdl, struct kvsfs_fsal_export, export);
	struct kvsfs_fsal_obj_handle *hdl;

	hdl = gsh_malloc(sizeof(struct kvsfs_fsal_obj_handle) +
			 sizeof(struct kvsfs_file_handle));

	memset(hdl, 0,
	       (sizeof(struct kvsfs_fsal_obj_handle) +
		sizeof(struct kvsfs_file_handle)));
	hdl->handle = (struct kvsfs_file_handle *)&hdl[1];
	memcpy(hdl->handle, fh, sizeof(struct kvsfs_file_handle));

	hdl->obj_handle.attrs = &hdl->attributes;
	hdl->obj_handle.type = posix2fsal_type(stat->st_mode);

	if ((hdl->obj_handle.type == SYMBOLIC_LINK) &&
	    (link_content != NULL)) {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = gsh_malloc(len);
		memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
	}

	hdl->attributes.mask = exp_hdl->exp_ops.fs_supported_attrs(exp_hdl);

	posix2fsal_attributes(stat, &hdl->attributes);

	fsal_obj_handle_init(&hdl->obj_handle,
			     exp_hdl,
			     posix2fsal_type(stat->st_mode));
	kvsfs_handle_ops_init(&hdl->obj_handle.obj_ops);
	if (myself->pnfs_mds_enabled)
		handle_ops_pnfs(&hdl->obj_handle.obj_ops);
	return hdl;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t kvsfs_lookup(struct fsal_obj_handle *parent,
				 const char *path,
				 struct fsal_obj_handle **handle)
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
	if (!parent->obj_ops.handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;
	retval = kvsns_lookup(&cred, &parent_hdl->handle->kvsfs_handle,
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

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	return fsalstat(fsal_error, -retval);
}

/* lookup_path
 * should not be used for "/" only is exported */

fsal_status_t kvsfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle)
{
	kvsns_ino_t object;
	int rc = 0;
	struct kvsfs_file_handle fh;
	struct stat stat;
	kvsns_cred_t cred;
	struct kvsfs_fsal_obj_handle *hdl;

	if (strcmp(path, "/"))
		return fsalstat(ERR_FSAL_NOTSUPP, 0);

	rc = kvsns_get_root(&object);
	if (rc != 0)
		return fsalstat(posix2fsal_error(-rc), -rc);

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;


	rc = kvsns_getattr(&cred, &object, &stat);
	if (rc != 0)
		return fsalstat(posix2fsal_error(-rc), -rc);

	fh.kvsfs_handle = object;

	hdl = alloc_handle(&fh, &stat, NULL, exp_hdl);

	*handle = &hdl->obj_handle;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t kvsfs_create(struct fsal_obj_handle *dir_hdl,
				 const char *name, struct attrlist *attrib,
				 struct fsal_obj_handle **handle)
{
	struct kvsfs_fsal_obj_handle *myself, *hdl;
	int retval = 0;
	struct kvsfs_file_handle fh;
	kvsns_cred_t cred;
	kvsns_ino_t object;
	struct stat stat;

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(&fh, 0, sizeof(struct kvsfs_file_handle));
	myself = container_of(dir_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	cred.uid = attrib->owner;
	cred.gid = attrib->group;

	retval = kvsns_creat(&cred, &myself->handle->kvsfs_handle, (char *)name,
			     fsal2unix_mode(attrib->mode), &object);
	if (retval)
		goto fileerr;

	retval = kvsns_getattr(&cred, &object, &stat);
	if (retval)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, NULL, op_ctx->fsal_export);

	/* >> set output handle << */
	hdl->handle->kvsfs_handle = object;
	*handle = &hdl->obj_handle;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	return fsalstat(posix2fsal_error(-retval), -retval);
}

static fsal_status_t kvsfs_mkdir(struct fsal_obj_handle *dir_hdl,
				const char *name, struct attrlist *attrib,
				struct fsal_obj_handle **handle)
{
	struct kvsfs_fsal_obj_handle *myself, *hdl;
	int retval = 0;
	struct kvsfs_file_handle fh;
	kvsns_cred_t cred;
	kvsns_ino_t object;
	struct stat stat;

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(&fh, 0, sizeof(struct kvsfs_file_handle));
	myself = container_of(dir_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

	cred.uid = attrib->owner;
	cred.gid = attrib->group;

	retval = kvsns_mkdir(&cred, &myself->handle->kvsfs_handle, (char *)name,
			     fsal2unix_mode(attrib->mode), &object);
	if (retval)
		goto fileerr;
	retval = kvsns_getattr(&cred, &object, &stat);
	if (retval)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, NULL, op_ctx->fsal_export);

	/* >> set output handle << */
	hdl->handle->kvsfs_handle = object;
	*handle = &hdl->obj_handle;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	return fsalstat(posix2fsal_error(-retval), -retval);
}

static fsal_status_t kvsfs_makenode(struct fsal_obj_handle *dir_hdl,
				   const char *name,
				   object_file_type_t nodetype,	/* IN */
				   fsal_dev_t *dev,	/* IN */
				   struct attrlist *attrib,
				   struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */


static fsal_status_t kvsfs_makesymlink(struct fsal_obj_handle *dir_hdl,
				      const char *name, const char *link_path,
				      struct attrlist *attrib,
				      struct fsal_obj_handle **handle)
{
	struct kvsfs_fsal_obj_handle *myself, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	kvsns_cred_t cred;
	kvsns_ino_t object;
	struct stat stat;

	struct kvsfs_file_handle fh;

	*handle = NULL;		/* poison it first */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(&fh, 0, sizeof(struct kvsfs_file_handle));
	myself = container_of(dir_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);
	cred.uid = attrib->owner;
	cred.gid = attrib->group;

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

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	/* The link length should be cached in the file handle */

	link_content->len =
	    myself->attributes.filesize ? (myself->attributes.filesize +
					   1) : fsal_default_linksize;
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

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

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
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, bool *eof)
{
	struct kvsfs_fsal_obj_handle *myself;
	int retval = 0;
	off_t seekloc = 0;
	kvsns_cred_t cred;
	kvsns_dentry_t dirents[MAX_ENTRIES];
	unsigned int index = 0;
	int size = 0;
	kvsns_dir_t ddir;

	if (whence != NULL)
		seekloc = (off_t) *whence;
	myself =
		container_of(dir_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	retval = kvsns_opendir(&cred, &myself->handle->kvsfs_handle,
			       &ddir);
	if (retval < 0)
		goto out;

	/* Open the directory */
	do {
		size = MAX_ENTRIES;
		retval = kvsns_readdir(&cred, &ddir, seekloc,
				       dirents, &size);
		if (retval)
			goto out;
		for (index = 0; index < MAX_ENTRIES; index++) {
			/* If psz_filename is NULL,
			 * that's the end of the list */
			if (dirents[index].name[0] == '\0') {
				*eof = true;
				goto done;
			}

			/* callback to cache inode */
			if (!cb(dirents[index].name,
				dir_state,
				(fsal_cookie_t) index))
				goto done;
		}

		seekloc += MAX_ENTRIES;
	} while (size != 0);

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

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	retval = kvsns_rename(&cred,
			      &olddir->handle->kvsfs_handle, (char *)old_name,
			      &newdir->handle->kvsfs_handle, (char *)new_name);

	if (retval)
		fsal_error = posix2fsal_error(-retval);
	return fsalstat(fsal_error, -retval);
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t kvsfs_getattrs(struct fsal_obj_handle *obj_hdl)
{
	struct kvsfs_fsal_obj_handle *myself;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	kvsns_cred_t cred;

	myself =
		container_of(obj_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	retval = kvsns_getattr(&cred, &myself->handle->kvsfs_handle, &stat);

	/* An explanation is required here.
	 * This is an exception management.
	 * when a file is opened, then deleted without being closed,
	 * FSAL_VFS can still getattr on it, because it uses fstat
	 * on a cached FD. This is not possible
	 * to do this with KVSFS, because you can't fstat on a vnode.
	 * To handle this, stat are
	 * cached as the file is opened and used here,
	 * to emulate a successful fstat */
	if ((retval == ENOENT)
	    && (myself->u.file.openflags != FSAL_O_CLOSED)
	    && (S_ISREG(myself->u.file.saved_stat.st_mode))) {
		memcpy(&stat, &myself->u.file.saved_stat,
		       sizeof(struct stat));
		retval = 0;	/* remove the error */
		goto ok_file_opened_and_deleted;
	}

	if (retval)
		goto errout;

	/* convert attributes */
 ok_file_opened_and_deleted:
	posix2fsal_attributes(&stat, &myself->attributes);
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

static fsal_status_t kvsfs_setattrs(struct fsal_obj_handle *obj_hdl,
				   struct attrlist *attrs)
{
	struct kvsfs_fsal_obj_handle *myself;
	struct stat stats = { 0 };
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int flags = 0;
	kvsns_cred_t cred;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
		attrs->mode &= ~op_ctx->fsal_export->exp_ops.
				fs_umask(op_ctx->fsal_export);
	myself =
		container_of(obj_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	/* First, check that FSAL attributes */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			fsal_error = ERR_FSAL_INVAL;
			return fsalstat(fsal_error, retval);
		}
		flags |= STAT_SIZE_SET;
		stats.st_size = attrs->filesize;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		flags |= STAT_MODE_SET;
		stats.st_mode = fsal2unix_mode(attrs->mode);
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)) {
		flags |= STAT_UID_SET;
		stats.st_uid = attrs->owner;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)) {
		flags |= STAT_GID_SET;
		stats.st_gid = attrs->group;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
		flags |= STAT_ATIME_SET;
		stats.st_atime = attrs->atime.tv_sec;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
		flags |= STAT_ATIME_SET;
		struct timespec timestamp;

		retval = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (retval != 0)
			goto out;
		stats.st_atim = timestamp;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
		flags |= STAT_MTIME_SET;
		stats.st_mtime = attrs->mtime.tv_sec;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
		flags |= STAT_MTIME_SET;
		struct timespec timestamp;

		retval = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (retval != 0)
			goto out;
		stats.st_mtim = timestamp;
	}
	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

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

/* file_unlink
 * unlink the named file in the directory
 */
static fsal_status_t kvsfs_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	struct kvsfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	kvsns_cred_t cred;
	kvsns_ino_t object;
	struct stat stat;

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	myself =
		container_of(dir_hdl, struct kvsfs_fsal_obj_handle, obj_handle);

	/* check for presence of file and get its type */
	retval = kvsns_lookup(&cred, &myself->handle->kvsfs_handle,
			      (char *)name, &object);

	if (retval == 0) {

		retval = kvsns_getattr(&cred, &object, &stat);
		if (retval) {
			fsal_error = posix2fsal_error(-retval);
			return fsalstat(fsal_error, -retval);
		}

		if ((stat.st_mode & S_IFDIR) == S_IFDIR)
			retval = kvsns_rmdir(&cred,
					     &myself->handle->kvsfs_handle,
					     (char *)name);
		else
			retval = kvsns_unlink(
					&cred,
					&myself->handle->kvsfs_handle,
					(char *)name);
	}

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

static fsal_status_t kvsfs_handle_digest(const struct fsal_obj_handle *obj_hdl,
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

/*
 * release
 * release our export first so they know we are gone
 */
static void release(struct fsal_obj_handle *obj_hdl)
{
	struct kvsfs_fsal_obj_handle *myself;
	object_file_type_t type = obj_hdl->type;

	myself = container_of(obj_hdl, struct kvsfs_fsal_obj_handle,
			      obj_handle);

#if 0
	/** @todo : to be done as file content is supported */

	if (type == REGULAR_FILE &&
	    myself->u.file.openflags != FSAL_O_CLOSED) {
		fsal_status_t st = tank_close(obj_hdl);

		if (FSAL_IS_ERROR(st)) {
			LogCrit(COMPONENT_FSAL,
				"Could not close, error %s(%d)",
				strerror(st.minor), st.minor);
		}
	}
#endif

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
 * returns a ref counted handle to be later used in cache_inode etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t kvsfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle)
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

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

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

	return fsalstat(fsal_error, 0);
}

void kvsfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = kvsfs_lookup;
	ops->readdir = kvsfs_readdir;
	ops->create = kvsfs_create;
	ops->mkdir = kvsfs_mkdir;
	ops->mknode = kvsfs_makenode;
	ops->symlink = kvsfs_makesymlink;
	ops->readlink = kvsfs_readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = kvsfs_getattrs;
	ops->setattrs = kvsfs_setattrs;
	ops->link = kvsfs_linkfile;
	ops->rename = kvsfs_rename;
	ops->unlink = kvsfs_unlink;
	ops->open = kvsfs_open;
	ops->status = kvsfs_status;
	ops->read = kvsfs_read;
	ops->write = kvsfs_write;
	ops->commit = kvsfs_commit;
	ops->lock_op = kvsfs_lock_op;
	ops->close = kvsfs_close;
	ops->lru_cleanup = kvsfs_lru_cleanup;
	ops->handle_digest = kvsfs_handle_digest;
	ops->handle_to_key = kvsfs_handle_to_key;

	/* xattr related functions */
	ops->list_ext_attrs = kvsfs_list_ext_attrs;
	ops->getextattr_id_by_name = kvsfs_getextattr_id_by_name;
	ops->getextattr_value_by_name = kvsfs_getextattr_value_by_name;
	ops->getextattr_value_by_id = kvsfs_getextattr_value_by_id;
	ops->setextattr_value = kvsfs_setextattr_value;
	ops->setextattr_value_by_id = kvsfs_setextattr_value_by_id;
	ops->getextattr_attrs = kvsfs_getextattr_attrs;
	ops->remove_extattr_by_id = kvsfs_remove_extattr_by_id;
	ops->remove_extattr_by_name = kvsfs_remove_extattr_by_name;
}
