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
 * GPFS object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "gpfs_methods.h"

/* helpers
 */

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */

static struct gpfs_fsal_obj_handle *alloc_handle(struct gpfs_file_handle *fh,
						 struct fsal_filesystem *fs,
						 struct attrlist *attributes,
						 const char *link_content,
						 struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_obj_handle *hdl;

	hdl =
	    gsh_malloc(sizeof(struct gpfs_fsal_obj_handle) +
		       sizeof(struct gpfs_file_handle));
	if (hdl == NULL)
		return NULL;
	memset(hdl, 0,
	       (sizeof(struct gpfs_fsal_obj_handle) +
		sizeof(struct gpfs_file_handle)));
	hdl->handle = (struct gpfs_file_handle *)&hdl[1];
	hdl->obj_handle.fs = fs;
	memcpy(hdl->handle, fh, sizeof(struct gpfs_file_handle));
	hdl->obj_handle.type = attributes->type;
	if (hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd = -1;	/* no open on this yet */
		hdl->u.file.openflags = FSAL_O_CLOSED;
	} else if (hdl->obj_handle.type == SYMBOLIC_LINK
		   && link_content != NULL) {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = gsh_malloc(len);
		if (hdl->u.symlink.link_content == NULL)
			goto spcerr;

		memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
	}

	hdl->obj_handle.attributes.mask =
	    exp_hdl->ops->fs_supported_attrs(exp_hdl);
	memcpy(&hdl->obj_handle.attributes, attributes,
	       sizeof(struct attrlist));

	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl, attributes->type);
	return hdl;

 spcerr:
	if (hdl->obj_handle.type == SYMBOLIC_LINK) {
		if (hdl->u.symlink.link_content != NULL)
			gsh_free(hdl->u.symlink.link_content);
	}
	gsh_free(hdl);		/* elvis has left the building */
	return NULL;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct attrlist attrib;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	struct fsal_filesystem *fs;

	*handle = NULL;		/* poison it first */
	fs = parent->fs;
	if (!path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;
	if (!parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	if (parent->fsal != parent->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 parent->fsal->name, parent->fs->fsal->name);
		retval = EXDEV;
		goto hdlerr;
	}
	attrib.mask = parent->attributes.mask;
	status = GPFSFSAL_lookup(op_ctx, parent, path, &attrib, fh, &fs);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, fs, &attrib, NULL,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto hdlerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 hdlerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
			    const char *name, struct attrlist *attrib,
			    struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *hdl;
	fsal_status_t status;

	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;

	attrib->mask = op_ctx->fsal_export->ops->
		fs_supported_attrs(op_ctx->fsal_export);
	status =
	    GPFSFSAL_create(dir_hdl, name, op_ctx, attrib->mode, fh, attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, attrib, NULL,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *hdl;
	fsal_status_t status;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;

	attrib->mask = op_ctx->fsal_export->ops->
		fs_supported_attrs(op_ctx->fsal_export);
	status = GPFSFSAL_mkdir(dir_hdl, name, op_ctx, attrib->mode, fh,
				attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, attrib, NULL,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      fsal_dev_t *dev,
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;

	attrib->mask = op_ctx->fsal_export->ops->
		fs_supported_attrs(op_ctx->fsal_export);
	status =
	    GPFSFSAL_mknode(dir_hdl, name, op_ctx, attrib->mode, nodetype, dev,
			    fh, attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, attrib, NULL,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto nodeerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 nodeerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL;		/* poison it first */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;

	attrib->mask = op_ctx->fsal_export->ops->
		fs_supported_attrs(op_ctx->fsal_export);
	status =
	    GPFSFSAL_symlink(dir_hdl, name, link_path, op_ctx,
			     attrib->mode, fh,
			     attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, attrib, link_path,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	fsal_error = posix2fsal_error(retval);

	return fsalstat(fsal_error, retval);
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *myself = NULL;
	fsal_status_t status;

	if (obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if (refresh) {		/* lazy load or LRU'd storage */
		size_t retlink;
		char link_buff[PATH_MAX + 1];

		retlink = PATH_MAX;

		if (myself->u.symlink.link_content != NULL) {
			gsh_free(myself->u.symlink.link_content);
			myself->u.symlink.link_content = NULL;
			myself->u.symlink.link_size = 0;
		}

		status =
		    GPFSFSAL_readlink(obj_hdl, op_ctx, link_buff, &retlink,
				      NULL);
		if (FSAL_IS_ERROR(status))
			return status;

		myself->u.symlink.link_content = gsh_malloc(retlink + 1);
		if (myself->u.symlink.link_content == NULL) {
			fsal_error = ERR_FSAL_NOMEM;
			goto out;
		}
		memcpy(myself->u.symlink.link_content, link_buff, retlink);
		myself->u.symlink.link_content[retlink] = '\0';
		myself->u.symlink.link_size = retlink + 1;
	}
	if (myself->u.symlink.link_content == NULL) {
		fsal_error = ERR_FSAL_FAULT;	/* probably a better error?? */
		goto out;
	}
	link_content->len = myself->u.symlink.link_size;
	link_content->addr = gsh_malloc(link_content->len);
	if (link_content->addr == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		link_content->len = 0;
		goto out;
	}
	memcpy(link_content->addr, myself->u.symlink.link_content,
	       link_content->len);

 out:

	return fsalstat(fsal_error, retval);
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	status = GPFSFSAL_link(destdir_hdl, myself->handle, name,
			       op_ctx, NULL);

	return status;
}

#define BUF_SIZE 1024
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
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, bool *eof)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *myself;
	int dirfd;
	fsal_status_t status;
	off_t seekloc = 0;
	int bpos, cnt, nread;
	struct dirent64 *dentry;
	char buf[BUF_SIZE];
	struct gpfs_filesystem *gpfs_fs;

	if (whence != NULL)
		seekloc = (off_t) *whence;

	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = dir_hdl->fs->private;

	status = fsal_internal_handle2fd_at(gpfs_fs->root_fd, myself->handle,
					    &dirfd, O_RDONLY | O_DIRECTORY, 0);
	if (dirfd < 0)
		return status;

	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if (seekloc < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto done;
	}
	cnt = 0;
	do {
		nread = syscall(SYS_getdents64, dirfd, buf, BUF_SIZE);
		if (nread < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto done;
		}
		if (nread == 0)
			break;
		for (bpos = 0; bpos < nread;) {
			dentry = (struct dirent64 *)(buf + bpos);
			if (strcmp(dentry->d_name, ".") == 0
			    || strcmp(dentry->d_name, "..") == 0)
				goto skip;	/* must skip '.' and '..' */

			/* callback to cache inode */
			if (!cb(dentry->d_name, dir_state,
				(fsal_cookie_t) dentry->d_off)) {
				goto done;
			}
 skip:
			bpos += dentry->d_reclen;
			cnt++;
		}
	} while (nread > 0);

	*eof = true;
 done:
	close(dirfd);

	return fsalstat(fsal_error, retval);
}

static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	fsal_status_t status;

	status =
	    GPFSFSAL_rename(olddir_hdl, old_name, newdir_hdl, new_name,
			    op_ctx);
	return status;
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
			      obj_handle);

	obj_hdl->attributes.mask = op_ctx->fsal_export->ops->
		fs_supported_attrs(op_ctx->fsal_export);
	status = GPFSFSAL_getattrs(op_ctx->fsal_export, obj_hdl->fs->private,
				   op_ctx, myself->handle,
				   &obj_hdl->attributes);
	if (FSAL_IS_ERROR(status)) {
		FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
		FSAL_SET_MASK(obj_hdl->attributes.mask, ATTR_RDATTR_ERR);
	}
	return status;
}

/*
 * NOTE: this is done under protection of the attributes rwlock in cache entry.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	fsal_status_t status;

	status = GPFSFSAL_setattrs(obj_hdl, op_ctx, attrs);

	return status;
}

/* gpfs_compare
 * compare two handles.
 * return true for equal, false for anything else
 */
bool gpfs_compare(struct fsal_obj_handle *obj_hdl,
		  struct fsal_obj_handle *other_hdl)
{
	struct gpfs_fsal_obj_handle *myself, *other;

	if (obj_hdl == other_hdl)
		return true;
	if (!other_hdl)
		return false;
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	other =
	    container_of(other_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if ((obj_hdl->type != other_hdl->type)
	    || (myself->handle->handle_type != other->handle->handle_type)
	    || (myself->handle->handle_size != other->handle->handle_size))
		return false;
	return memcmp(myself->handle->f_handle, other->handle->f_handle,
		      myself->handle->handle_size) ? false : true;
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	fsal_status_t status;

	status = GPFSFSAL_unlink(dir_hdl, name, op_ctx);

	return status;
}

/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   fsal_digesttype_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	const struct gpfs_fsal_obj_handle *myself;
	struct gpfs_file_handle *fh;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself =
	    container_of(obj_hdl, const struct gpfs_fsal_obj_handle,
			 obj_handle);
	fh = myself->handle;

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = gpfs_sizeof_handle(fh);
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
 * handle_to_key
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = myself->handle->handle_key_size;
}

/*
 * release
 * release our export first so they know we are gone
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself;
	object_file_type_t type = obj_hdl->type;

	if (type == REGULAR_FILE)
		gpfs_close(obj_hdl);

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	fsal_obj_handle_uninit(obj_hdl);

	if (type == SYMBOLIC_LINK) {
		if (myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	}
	gsh_free(myself);
}

/* gpfs_share_op
 */

static fsal_status_t share_op(struct fsal_obj_handle *obj_hdl,
			      void *p_owner,
			      fsal_share_param_t request_share)
{
	fsal_status_t status;
	int fd, mntfd;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	mntfd = fd = myself->u.file.fd;

	status = GPFSFSAL_share_op(mntfd, fd, p_owner, request_share);

	return status;
}

void gpfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->create = create;
	ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->open = gpfs_open;
	ops->reopen = gpfs_reopen;
	ops->status = gpfs_status;
	ops->read = gpfs_read;
	ops->read_plus = gpfs_read_plus;
	ops->write = gpfs_write;
	ops->write_plus = gpfs_write_plus;
	ops->seek = gpfs_seek;
	ops->io_advise = gpfs_io_advise;
	ops->commit = gpfs_commit;
	ops->lock_op = gpfs_lock_op;
	ops->share_op = share_op;
	ops->close = gpfs_close;
	ops->lru_cleanup = gpfs_lru_cleanup;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
	handle_ops_pnfs(ops);
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t gpfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t fsal_status;
	int retval = 0;
	int dir_fd;
	struct fsal_filesystem *fs;
	struct gpfs_fsal_obj_handle *hdl;
	struct attrlist attributes;
	gpfsfsal_xstat_t buffxstat;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	enum fsid_type fsid_type;
	struct fsal_fsid__ fsid;

	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;

	*handle = NULL;	/* poison it */

	dir_fd = open_dir_by_path_walk(-1, path, &buffxstat.buffstat);

	if (dir_fd < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not open directory for path %s",
			path);
		retval = -dir_fd;
		goto errout;
	}

	fsal_status = fsal_internal_fd2handle(dir_fd, fh);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	attributes.mask = exp_hdl->ops->fs_supported_attrs(exp_hdl);
	fsal_status = fsal_get_xstat_by_handle(dir_fd, fh, &buffxstat,
					       NULL, false);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;
	fsal_status = gpfsfsal_xstat_2_fsal_attributes(&buffxstat, &attributes);
	LogFullDebug(COMPONENT_FSAL,
		     "fsid=0x%016"PRIx64".0x%016"PRIx64,
		     attributes.fsid.major,
		     attributes.fsid.minor);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	close(dir_fd);

	gpfs_extract_fsid(fh, &fsid_type, &fsid);

	fs = lookup_fsid(&fsid, fsid_type);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find file system for path %s",
			path);
		retval = ENOENT;
		goto errout;
	}

	if (fs->fsal != exp_hdl->fsal) {
		LogInfo(COMPONENT_FSAL,
			"File system for path %s did not belong to FSAL %s",
			path, exp_hdl->fsal->name);
		retval = EACCES;
		goto errout;
	}

	LogDebug(COMPONENT_FSAL,
		 "filesystem %s for path %s",
		 fs->path, path);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, fs, &attributes, NULL, exp_hdl);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	retval = errno;
	close(dir_fd);

 errout:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in cache_inode etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket, nor reliably on block or
 * character special devices.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t gpfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh;
	struct attrlist attrib;
	char *link_content = NULL;
	ssize_t retlink = PATH_MAX;
	char link_buff[PATH_MAX + 1];
	enum fsid_type fsid_type;
	struct fsal_fsid__ fsid;
	struct fsal_filesystem *fs;
	struct gpfs_filesystem *gpfs_fs;

	*handle = NULL;		/* poison it first */
	if ((hdl_desc->len > (sizeof(struct gpfs_file_handle))))
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len); /* struct aligned copy */

	gpfs_extract_fsid(fh, &fsid_type, &fsid);

	fs = lookup_fsid(&fsid, fsid_type);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find filesystem for "
			"fsid=0x%016"PRIx64".0x%016"PRIx64
			" from handle",
			fsid.major, fsid.minor);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	if (fs->fsal != exp_hdl->fsal) {
		LogInfo(COMPONENT_FSAL,
			"Non GPFS filesystem "
			"fsid=0x%016"PRIx64".0x%016"PRIx64
			" from handle",
			fsid.major, fsid.minor);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	gpfs_fs = fs->private;

	attrib.mask = exp_hdl->ops->fs_supported_attrs(exp_hdl);
	status = GPFSFSAL_getattrs(exp_hdl, gpfs_fs, op_ctx, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	if (attrib.type == SYMBOLIC_LINK) {	/* I could lazy eval this... */

		status = fsal_readlink_by_handle(gpfs_fs->root_fd, fh,
						 link_buff, &retlink);
		if (FSAL_IS_ERROR(status))
			return status;

		if (retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if (retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = link_buff;
	}
	hdl = alloc_handle(fh, fs, &attrib, link_content, exp_hdl);
	if (hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;

 errout:
	return fsalstat(fsal_error, retval);
}
