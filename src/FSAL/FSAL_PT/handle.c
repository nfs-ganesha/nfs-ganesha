/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) IBM Inc., 2013
 *
 * Contributors: Jim Lieb jlieb@panasas.com
 *               Allison Henderson        achender@linux.vnet.ibm.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* handle.c
 * PT object (file|dir) handle object
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "ganesha_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "pt_methods.h"
#include "pt_ganesha.h"

/* PT is effectively a single filesystem, describe it and assign all
 * PT handles to it.
 */
struct fsal_filesystem pt_filesystem = {
	.children = GLIST_HEAD_INIT(pt_filesystem.children),
	.exported = true,
	/** @todo fill in the following
	.dev = xxxxx,
	.fsid_tyoe = xxxx,
	.fsid = xxxx,
	*/
	.path = "/PT",
	.type = "PT",
};

/* helpers
 */

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */

static struct pt_fsal_obj_handle *alloc_handle(ptfsal_handle_t *fh,
					       struct attrlist *attributes,
					       const char *link_content,
					       ptfsal_handle_t *dir_fh,
					       const char *unopenable_name,
					       struct fsal_export *exp_hdl)
{
	struct pt_fsal_obj_handle *hdl;

	hdl = gsh_malloc(sizeof(struct pt_fsal_obj_handle)
			 + sizeof(ptfsal_handle_t));
	if (hdl == NULL)
		return NULL;
	memset(hdl, 0,
	       (sizeof(struct pt_fsal_obj_handle) + sizeof(ptfsal_handle_t)));
	hdl->handle = (ptfsal_handle_t *) &hdl[1];
	memcpy(hdl->handle, fh, sizeof(ptfsal_handle_t));
	hdl->obj_handle.type = attributes->type;
	hdl->obj_handle.fs = &pt_filesystem;
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
	} else if (pt_unopenable_type(hdl->obj_handle.type)
		   && dir_fh != NULL && unopenable_name != NULL) {
		hdl->u.unopenable.dir = gsh_malloc(sizeof(ptfsal_handle_t));
		if (hdl->u.unopenable.dir == NULL)
			goto spcerr;
		memcpy(hdl->u.unopenable.dir, dir_fh, sizeof(ptfsal_handle_t));
		hdl->u.unopenable.name =
		    gsh_malloc(strlen(unopenable_name) + 1);
		if (hdl->u.unopenable.name == NULL)
			goto spcerr;
		strcpy(hdl->u.unopenable.name, unopenable_name);
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
	} else if (pt_unopenable_type(hdl->obj_handle.type)) {
		if (hdl->u.unopenable.name != NULL)
			gsh_free(hdl->u.unopenable.name);
		if (hdl->u.unopenable.dir != NULL)
			gsh_free(hdl->u.unopenable.dir);
	}
	gsh_free(hdl);		/* elvis has left the building */
	return NULL;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t pt_lookup(struct fsal_obj_handle *parent,
			       const char *path,
			       struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct pt_fsal_obj_handle *hdl;
	struct attrlist attrib;
	ptfsal_handle_t *fh = alloca(sizeof(ptfsal_handle_t));

	*handle = NULL;		/* poison it first */
	if (!path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	memset(fh, 0, sizeof(ptfsal_handle_t));
	fh->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
	if (!parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	attrib.mask = parent->attributes.mask;
	status = PTFSAL_lookup(op_ctx, parent, path, &attrib, fh);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &attrib, NULL, NULL, NULL,
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
	struct pt_fsal_obj_handle *hdl;
	fsal_status_t status;

	ptfsal_handle_t *fh = alloca(sizeof(ptfsal_handle_t));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(ptfsal_handle_t));
	fh->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;

	attrib->mask =
	    op_ctx->fsal_export->ops->fs_supported_attrs(op_ctx->fsal_export);
	status = PTFSAL_create(dir_hdl, name, op_ctx,
			       attrib->mode, fh, attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, attrib, NULL, NULL, NULL, op_ctx->fsal_export);
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
	struct pt_fsal_obj_handle *hdl;
	fsal_status_t status;
	ptfsal_handle_t *fh = alloca(sizeof(ptfsal_handle_t));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(ptfsal_handle_t));
	fh->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;

	attrib->mask =
	    op_ctx->fsal_export->ops->fs_supported_attrs(op_ctx->fsal_export);
	status = PTFSAL_mkdir(dir_hdl, name, op_ctx, attrib->mode, fh, attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, attrib, NULL, NULL, NULL, op_ctx->fsal_export);
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
			      const char *name,
			      object_file_type_t nodetype,	/* IN */
			      fsal_dev_t *dev,	/* IN */
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct pt_fsal_obj_handle *hdl;
	ptfsal_handle_t *dir_fh = NULL;
	ptfsal_handle_t *fh = alloca(sizeof(ptfsal_handle_t));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(ptfsal_handle_t));
	fh->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;

	attrib->mask =
	    op_ctx->fsal_export->ops->fs_supported_attrs(op_ctx->fsal_export);
	status =
	    PTFSAL_mknode(dir_hdl, name, op_ctx, attrib->mode,
			  nodetype, dev, fh,
			  attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, attrib, NULL, dir_fh, NULL,
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
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* not defined in linux headers so we do it here
 */

struct linux_dirent {
	unsigned long d_ino;	/* Inode number */
	unsigned long d_off;	/* Offset to next linux_dirent */
	unsigned short d_reclen;	/* Length of this linux_dirent */
	char d_name[];		/* Filename (null-terminated) */
	/* length is actually (d_reclen - 2 -
	 * offsetof(struct linux_dirent, d_name)
	 */
	/*
	   char           pad;       // Zero padding byte
	   char           d_type;    // File type (only since Linux 2.6.4;
	   // offset is (d_reclen - 1))
	 */
};

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
static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  uint64_t *whence, void *dir_state,
				  fsal_readdir_cb cb, bool *eof)
{
	struct pt_fsal_obj_handle *myself;
	int dirfd;
	off_t seekloc = 0;
	ptfsal_cookie_t *entry_cookie;
	char fsi_parent_dir_path[PATH_MAX];
	char fsi_dname[PATH_MAX];
	char fsi_name[PATH_MAX];
	int readdir_rc, rc;
	int readdir_record = 0;
	fsi_stat_struct buffstat;
	static fsal_status_t status;

	if (whence != NULL)
		memcpy(&seekloc, &whence, sizeof(off_t));
	entry_cookie = alloca(sizeof(fsal_cookie_t) + sizeof(off_t));
	myself = container_of(dir_hdl, struct pt_fsal_obj_handle, obj_handle);
	status =
	    fsal_internal_handle2fd_at(op_ctx, myself, &dirfd,
				       (O_RDONLY | O_DIRECTORY));

	if (dirfd < 0)
		return status;

	if (seekloc != 0) {
		FSI_TRACE(FSI_DEBUG,
			  "FSI - seekdir called - NOT SUPPORTED RIGHT NOW!!!\n");
		return fsalstat(posix2fsal_error(ENOTSUP), ENOTSUP);
	}

	/************************/
	/* browse the directory */
	/************************/

	ptfsal_handle_to_name(myself->handle, op_ctx, op_ctx->fsal_export,
			      fsi_parent_dir_path);

	fsi_parent_dir_path[sizeof(fsi_parent_dir_path) - 1] = '\0';
	FSI_TRACE(FSI_DEBUG, "Parent dir path --- %s\n", fsi_parent_dir_path);

	*eof = 0;
	while (*eof == 0) {
		/***********************/
		/* read the next entry */
		/***********************/
		readdir_rc = ptfsal_readdir(op_ctx, op_ctx->fsal_export,
					    dirfd, &buffstat, fsi_dname);
		memset(fsi_name, 0, sizeof(fsi_name));
		fsi_get_whole_path(fsi_parent_dir_path, fsi_dname, fsi_name);
		FSI_TRACE(FSI_DEBUG, "fsi_dname %s, whole path %s\n", fsi_dname,
			  fsi_name);

		/* convert FSI return code to rc */
		rc = 1;
		if (readdir_rc != 0)
			rc = 0;

		/* End of directory */
		if (rc == 0) {
			*eof = 1;
			break;
		} else {
			*eof = 0;
		}

		/***********************************/
		/* Get information about the entry */
		/***********************************/

		FSI_TRACE(FSI_DEBUG, "fsi_dname: %s\n", fsi_dname);

		 /* skip . and .. */
		if (!strcmp(fsi_dname, ".") || !strcmp(fsi_dname, "..")) {
			FSI_TRACE(FSI_DEBUG, "skipping . or ..\n");
			continue;
		}

		entry_cookie->data.cookie = readdir_record;

		FSI_TRACE(FSI_DEBUG, "readdir [%s] rec %d\n", fsi_dname,
			  readdir_record);

		readdir_record++;

		/* callback to cache inode */
		if (!cb(fsi_dname,
			dir_state,
			entry_cookie->data.cookie)) {
				FSI_TRACE(FSI_DEBUG, "callback failed\n");
				break;
		}

	}

	FSI_TRACE(FSI_DEBUG, "End readdir==============================\n");
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	fsal_status_t status;

	status =
	    PTFSAL_rename(olddir_hdl, old_name, newdir_hdl, new_name, op_ctx);
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
	struct pt_fsal_obj_handle *myself;
	fsal_status_t status;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	obj_hdl->attributes.mask =
	    op_ctx->fsal_export->ops->fs_supported_attrs(op_ctx->fsal_export);
	status =
	    PTFSAL_getattrs(op_ctx->fsal_export, op_ctx, myself->handle,
			    &obj_hdl->attributes);
	if (FSAL_IS_ERROR(status)) {
		FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
		FSAL_SET_MASK(obj_hdl->attributes.mask, ATTR_RDATTR_ERR);
	}
	return status;
}

/*
 * NOTE: this is done under protection of the
 * attributes rwlock in the cache entry.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	fsal_status_t status;

	status = PTFSAL_setattrs(obj_hdl, op_ctx, attrs, NULL);

	return status;
}

/* compare
 * compare two handles.
 * return true for equal, false for anything else
 */
bool compare(struct fsal_obj_handle *obj_hdl,
	     struct fsal_obj_handle *other_hdl)
{
	struct pt_fsal_obj_handle *myself, *other;

	if (obj_hdl == other_hdl)
		return true;
	if (!other_hdl)
		return false;
	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);
	other = container_of(other_hdl, struct pt_fsal_obj_handle, obj_handle);
	if ((obj_hdl->type != other_hdl->type)
	    || (myself->handle->data.handle.handle_type !=
		other->handle->data.handle.handle_type)
	    || (myself->handle->data.handle.handle_size !=
		other->handle->data.handle.handle_size))
		return false;
	return memcmp(myself->handle->data.handle.f_handle,
		      other->handle->data.handle.f_handle,
		      myself->handle->data.handle.handle_size) ? false : true;
}

/* file_truncate
 * truncate a file to the size specified.
 * size should really be off_t...
 */
/*
static fsal_status_t file_truncate(struct fsal_obj_handle *obj_hdl,
				   const struct req_op_context *opctx,
				   uint64_t length)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t status;
	struct pt_fsal_obj_handle *myself;
	int retval = 0;

	if(obj_hdl->type != REGULAR_FILE) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	status = PTFSAL_truncate(opctx->fsal_export, myself,
				 opctx, length, NULL);
	return (status);

errout:
	return fsalstat(fsal_error, retval);
}
*/

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	fsal_status_t status;

	status = PTFSAL_unlink(dir_hdl, name, op_ctx, NULL);

	return status;
}

/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
			    fsal_digesttype_t output_type,
			    struct gsh_buffdesc *fh_desc)
{
	struct pt_fsal_obj_handle *myself;
	ptfsal_handle_t *fh;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);
	fh = myself->handle;

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
		fh_size = pt_sizeof_handle(fh);
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
	struct pt_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = pt_sizeof_handle(myself->handle);
}

/*
 * release
 * release our export first so they know we are gone
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	struct pt_fsal_obj_handle *myself;
	object_file_type_t type = obj_hdl->type;

	if (type == REGULAR_FILE) {
		fsal_status_t st = pt_close(obj_hdl);
		if (FSAL_IS_ERROR(st))
			LogCrit(COMPONENT_FSAL,
				"Could not close");
	}
	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	fsal_obj_handle_uninit(obj_hdl);

	if (type == SYMBOLIC_LINK) {
		if (myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	} else if (pt_unopenable_type(type)) {
		if (myself->u.unopenable.name != NULL)
			gsh_free(myself->u.unopenable.name);
		if (myself->u.unopenable.dir != NULL)
			gsh_free(myself->u.unopenable.dir);
	}
	gsh_free(myself);
}

void pt_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = pt_lookup;
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
	ops->open = pt_open;
	ops->status = pt_status;
	ops->read = pt_read;
	ops->write = pt_write;
	ops->commit = pt_commit;
	ops->close = pt_close;
	ops->lru_cleanup = pt_lru_cleanup;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t pt_lookup_path(struct fsal_export *exp_hdl,
			     const char *path, struct fsal_obj_handle **handle)
{

	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t fsal_status;
	int retval = 0;
	int dir_fd;
	struct stat stat;
	fsi_stat_struct p_stat;
	struct pt_fsal_obj_handle *hdl;
	char *basepart;
	char *link_content = NULL;
	ssize_t retlink;
	struct attrlist attributes;
	ptfsal_handle_t *fh = alloca(sizeof(ptfsal_handle_t));

	memset(fh, 0, sizeof(ptfsal_handle_t));
	fh->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
	if (path == NULL || path[0] != '/' || strlen(path) > PATH_MAX
	    || strlen(path) < 2) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	basepart = rindex(path, '/');
	if (basepart[1] == '\0') {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	fsal_status = fsal_internal_get_handle(op_ctx, exp_hdl, path, fh);
	if (FSAL_IS_ERROR(fsal_status))
		return fsal_status;

	if (basepart == path) {
		dir_fd = ptfsal_opendir(op_ctx, exp_hdl, "/", NULL, 0);
	} else {
		char *dirpart = alloca(basepart - path + 1);

		memcpy(dirpart, path, basepart - path);
		dirpart[basepart - path] = '\0';
		dir_fd = ptfsal_opendir(op_ctx, exp_hdl, dirpart, NULL, 0);
	}
	if (dir_fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = ptfsal_stat_by_name(op_ctx, exp_hdl, basepart, &p_stat);
	fsi_stat2stat(&p_stat, &stat);
	if (retval < 0)
		goto fileerr;

	if (!S_ISDIR(stat.st_mode)) /* this had better be a DIR! */
		goto fileerr;

	basepart++;
	fsal_status =
	    fsal_internal_get_handle_at(op_ctx, exp_hdl, dir_fd, basepart, fh);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	/* what about the file? Do no symlink chasing here. */
	retval = ptfsal_stat_by_name(op_ctx, exp_hdl, basepart, &p_stat);
	fsi_stat2stat(&p_stat, &stat);
	if (retval < 0)
		goto fileerr;

	attributes.mask = exp_hdl->ops->fs_supported_attrs(exp_hdl);
	fsal_status = pt_posix2fsal_attributes(&stat, &attributes);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	if (S_ISLNK(stat.st_mode)) {
		link_content = gsh_malloc(PATH_MAX);
		retlink = readlinkat(dir_fd, basepart, link_content, PATH_MAX);
		if (retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if (retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			goto linkerr;
		}
		link_content[retlink] = '\0';
	}
	ptfsal_closedir_fd(op_ctx, exp_hdl, dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &attributes, NULL, NULL, NULL, exp_hdl);
	if (link_content != NULL)
		gsh_free(link_content);
	if (hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL;	/* poison it */
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	retval = errno;
 linkerr:
	if (link_content != NULL)
		gsh_free(link_content);
	ptfsal_closedir_fd(op_ctx, exp_hdl, dir_fd);
	fsal_error = posix2fsal_error(retval);

 errout:
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

fsal_status_t pt_create_handle(struct fsal_export *exp_hdl,
			       struct gsh_buffdesc *hdl_desc,
			       struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct pt_fsal_obj_handle *hdl;
	ptfsal_handle_t *fh;
	struct attrlist attrib;
	char *link_content = NULL;

	*handle = NULL;		/* poison it first */
	if ((hdl_desc->len != (sizeof(ptfsal_handle_t))))
		return fsalstat(ERR_FSAL_FAULT, 0);


	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len); /* struct aligned copy */

	attrib.mask = exp_hdl->ops->fs_supported_attrs(exp_hdl);
	status = PTFSAL_getattrs(exp_hdl, op_ctx, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	hdl = alloc_handle(fh, &attrib, link_content, NULL, NULL, exp_hdl);
	if (hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;

 errout:
	return fsalstat(fsal_error, retval);
}
