/**
 * @file handle.c
 * @brief GPFS object (file|dir) handle object
 *
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

#include "config.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_localfs.h"
#include "gpfs_methods.h"
#include "nfs_proto_tools.h"


/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */

#define ATTR_GPFS_ALLOC_HANDLE (ATTR_TYPE | ATTR_FILEID | ATTR_FSID)

struct gpfs_fsal_obj_handle *alloc_handle(struct gpfs_file_handle *fh,
					 struct fsal_filesystem *fs,
					 struct fsal_attrlist *attributes,
					 const char *link_content,
					 struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_export *myself =
	    container_of(exp_hdl, struct gpfs_fsal_export, export);
	struct gpfs_fsal_obj_handle *hdl =
	    gsh_calloc(1, sizeof(struct gpfs_fsal_obj_handle) +
			  sizeof(struct gpfs_file_handle));

	hdl->handle = (struct gpfs_file_handle *)&hdl[1];
	hdl->obj_handle.fs = fs;
	memcpy(hdl->handle, fh, fh->handle_size);
	hdl->obj_handle.type = attributes->type;
	if (hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd.fd = -1;	/* no open on this yet */
		hdl->u.file.fd.openflags = FSAL_O_CLOSED;
	} else if (hdl->obj_handle.type == SYMBOLIC_LINK
		   && link_content != NULL) {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = gsh_malloc(len);
		memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
	}

	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl, attributes->type);
	hdl->obj_handle.fsid = attributes->fsid;
	hdl->obj_handle.fileid = attributes->fileid;

	if (myself->pnfs_mds_enabled)
		hdl->obj_handle.obj_ops = &GPFS.handle_ops_with_pnfs;
	else
		hdl->obj_handle.obj_ops = &GPFS.handle_ops;

	return hdl;
}

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */
static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle,
			    struct fsal_attrlist *attrs_out)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct fsal_attrlist attrib;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	struct fsal_filesystem *fs;

	*handle = NULL;		/* poison it first */
	fs = parent->fs;
	if (!path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;
	if (!fsal_obj_handle_is(parent, DIRECTORY)) {
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

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status = GPFSFSAL_lookup(parent, path, &attrib, fh, &fs);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, fs, &attrib, NULL, op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}

	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 hdlerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct fsal_attrlist *attr_in,
			     struct fsal_obj_handle **handle,
			     struct fsal_attrlist *attrs_out)
{
	struct gpfs_fsal_obj_handle *hdl;
	fsal_status_t status;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	/* Use a separate fsal_attrlist to getch the actual attributes into */
	struct fsal_attrlist attrib;

	*handle = NULL;		/* poison it */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status =
	    GPFSFSAL_mkdir(dir_hdl, name, attr_in->mode, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, &attrib, NULL, op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}
	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attr_in->valid_mask, ATTR_MODE);

	if (attr_in->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops->setattr2(*handle, false, NULL,
						     attr_in);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*handle)->obj_ops->release(*handle);
			*handle = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
	FSAL_SET_MASK(attr_in->valid_mask, ATTR_MODE);

	return status;
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      struct fsal_attrlist *attr_in,
			      struct fsal_obj_handle **handle,
			      struct fsal_attrlist *attrs_out)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	/* Use a separate fsal_attrlist to getch the actual attributes into */
	struct fsal_attrlist attrib;

	*handle = NULL;		/* poison it */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status =
	    GPFSFSAL_mknode(dir_hdl, name, attr_in->mode, nodetype,
			    &attr_in->rawdev, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, &attrib, NULL, op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}
	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attr_in->valid_mask, ATTR_MODE);

	if (attr_in->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops->setattr2(*handle, false, NULL,
						     attr_in);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*handle)->obj_ops->release(*handle);
			*handle = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
	FSAL_SET_MASK(attr_in->valid_mask, ATTR_MODE);

	return status;
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */
static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct fsal_attrlist *attr_in,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	/* Use a separate fsal_attrlist to getch the actual attributes into */
	struct fsal_attrlist attrib;

	*handle = NULL;		/* poison it first */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status = GPFSFSAL_symlink(dir_hdl, name, link_path,
				  attr_in->mode, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs, &attrib, link_path,
			   op_ctx->fsal_export);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}
	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attr_in->valid_mask, ATTR_MODE);

	if (attr_in->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops->setattr2(*handle, false, NULL,
						     attr_in);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				      fsal_err_txt(status));
			(*handle)->obj_ops->release(*handle);
			*handle = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
	FSAL_SET_MASK(attr_in->valid_mask, ATTR_MODE);

	return status;
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	struct gpfs_fsal_obj_handle *myself = NULL;
	fsal_status_t status;

	if (obj_hdl->type != SYMBOLIC_LINK)
		return fsalstat(ERR_FSAL_FAULT, 0);

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if (refresh) {		/* lazy load or LRU'd storage */
		char link_buff[PATH_MAX];

		if (myself->u.symlink.link_content != NULL) {
			gsh_free(myself->u.symlink.link_content);
			myself->u.symlink.link_content = NULL;
			myself->u.symlink.link_size = 0;
		}

		status = GPFSFSAL_readlink(obj_hdl, link_buff,
					   sizeof(link_buff));

		if (FSAL_IS_ERROR(status))
			return status;

		myself->u.symlink.link_content = gsh_strdup(link_buff);
		myself->u.symlink.link_size = strlen(link_buff) + 1;
	}

	if (myself->u.symlink.link_content == NULL)
		return fsalstat(ERR_FSAL_FAULT, 0);

	link_content->len = myself->u.symlink.link_size;
	link_content->addr = gsh_strdup(myself->u.symlink.link_content);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	status = GPFSFSAL_link(destdir_hdl, myself->handle, name);

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
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *myself;
	int dirfd;
	fsal_status_t status;
	off_t seekloc = 0;
	int bpos, nread;
	struct dirent64 *dentry;
	char buf[BUF_SIZE];
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	if (whence != NULL)
		seekloc = (off_t) *whence;

	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	status = fsal_internal_handle2fd(export_fd, myself->handle,
					 &dirfd, O_RDONLY | O_DIRECTORY);

	if (FSAL_IS_ERROR(status))
		return status;

	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if (seekloc < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto done;
	}
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
			struct fsal_obj_handle *hdl;
			struct fsal_attrlist attrs;
			enum fsal_dir_result cb_rc;

			dentry = (struct dirent64 *)(buf + bpos);
			if (strcmp(dentry->d_name, ".") == 0
			    || strcmp(dentry->d_name, "..") == 0)
				goto skip;	/* must skip '.' and '..' */

			fsal_prepare_attrs(&attrs, attrmask);

			status = lookup(dir_hdl, dentry->d_name, &hdl, &attrs);

			/* The entries we get in getdents64 syscall may
			 * not be there by the time we do lookup, so
			 * handle some errors here.
			 *
			 * Since we do lookup by name, we mostly get
			 * ERR_FSAL_NOENT, but handle other similar
			 * errors!
			 */
			if (FSAL_IS_ERROR(status)) {
				if (status.major == ERR_FSAL_NOENT ||
				    status.major == ERR_FSAL_STALE ||
				    status.major == ERR_FSAL_XDEV) {
					goto skip;
				} else {
					fsal_error = status.major;
					goto done;
				}
			}

			/* callback to MDCACHE */
			cb_rc = cb(dentry->d_name, hdl, &attrs, dir_state,
				   (fsal_cookie_t) dentry->d_off);

			fsal_release_attrs(&attrs);

			/* Read ahead not supported by this FSAL. */
			if (cb_rc >= DIR_READAHEAD)
				goto done;
 skip:
			bpos += dentry->d_reclen;
		}
	} while (nread > 0);

	*eof = true;
 done:
	fsal_internal_close(dirfd, NULL, 0);

	return fsalstat(fsal_error, retval);
}

static fsal_status_t renamefile(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	fsal_status_t status;

	status = GPFSFSAL_rename(olddir_hdl, old_name, newdir_hdl, new_name);
	return status;
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			      struct fsal_attrlist *attrs)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
			      obj_handle);

	status = GPFSFSAL_getattrs(op_ctx->fsal_export,
				   obj_hdl->fs->private_data,
				   myself->handle,
				   attrs);
	if (FSAL_IS_ERROR(status)) {
		goto out;
	}

	if (!FSAL_TEST_MASK(attrs->request_mask, ATTR4_FS_LOCATIONS)) {
		goto out;
	}

	if (obj_hdl->type != DIRECTORY)
		goto out;

	fsal_status_t fs_loc_status = GPFSFSAL_fs_loc(op_ctx->fsal_export,
						      obj_hdl->fs->private_data,
						      myself->handle,
						      attrs);

	if (FSAL_IS_SUCCESS(fs_loc_status)) {
		FSAL_SET_MASK(attrs->valid_mask, ATTR4_FS_LOCATIONS);
	} else {
		LogDebug(COMPONENT_FSAL,
			 "Request for attribute fs_locations failed with error: %s",
			 msg_fsal_err(fs_loc_status.major));
	}

out:
	return status;
}

static fsal_status_t getxattrs(struct fsal_obj_handle *obj_hdl,
				xattrkey4 *xa_name,
				xattrvalue4 *xa_value)
{
	int rc;
	int errsv;
	struct getxattr_arg gxarg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	gxarg.mountdirfd = export_fd;
	gxarg.handle = myself->handle;
	gxarg.name_len = xa_name->utf8string_len;
	gxarg.name = xa_name->utf8string_val;
	gxarg.value_len = xa_value->utf8string_len;
	gxarg.value = xa_value->utf8string_val;

	rc = gpfs_ganesha(OPENHANDLE_GETXATTRS, &gxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"GETXATTRS returned rc %d errsv %d", rc, errsv);

		if (errsv == ERANGE)
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		if (errsv == ENODATA)
			return fsalstat(ERR_FSAL_NOENT, 0);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	/* Make sure utf8string is NUL terminated */
	xa_value->utf8string_val[gxarg.value_len] = '\0';

	LogDebug(COMPONENT_FSAL,
		"GETXATTRS returned value %s len %d rc %d",
		(char *)gxarg.value, gxarg.value_len, rc);

	xa_value->utf8string_len = gxarg.value_len;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t setxattrs(struct fsal_obj_handle *obj_hdl,
				setxattr_option4 option,
				xattrkey4 *xa_name,
				xattrvalue4 *xa_value)
{
	int rc;
	int errsv;
	struct setxattr_arg sxarg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	sxarg.mountdirfd = export_fd;
	sxarg.handle = myself->handle;
	sxarg.name_len = xa_name->utf8string_len;
	sxarg.name = xa_name->utf8string_val;
	sxarg.value_len = xa_value->utf8string_len;
	sxarg.value = xa_value->utf8string_val;
	if (op_ctx && op_ctx->client)
		sxarg.cli_ip = op_ctx->client->hostaddr_str;

	rc = gpfs_ganesha(OPENHANDLE_SETXATTRS, &sxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"SETXATTRS returned rc %d errsv %d",
			rc, errsv);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t removexattrs(struct fsal_obj_handle *obj_hdl,
				xattrkey4 *xa_name)
{
	int rc;
	int errsv;
	struct removexattr_arg rxarg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	rxarg.mountdirfd = export_fd;
	rxarg.handle = myself->handle;
	rxarg.name_len = xa_name->utf8string_len;
	rxarg.name = xa_name->utf8string_val;
	if (op_ctx && op_ctx->client)
		rxarg.cli_ip = op_ctx->client->hostaddr_str;

	rc = gpfs_ganesha(OPENHANDLE_REMOVEXATTRS, &rxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"REMOVEXATTRS returned rc %d errsv %d",
			rc, errsv);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t listxattrs(struct fsal_obj_handle *obj_hdl,
				count4 la_maxcount,
				nfs_cookie4 *la_cookie,
				bool_t *lr_eof,
				xattrlist4 *lr_names)
{
	int rc;
	int errsv;
	char *name, *next, *end, *val, *valstart;
	int entryCount = 0;
	char *buf = NULL;
	struct listxattr_arg lxarg;
	struct gpfs_fsal_obj_handle *myself;
	component4 *entry = lr_names->xl4_entries;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	val = (char *)entry + la_maxcount;
	valstart = val;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);
	#define MAXCOUNT (1024*64)
	buf = gsh_malloc(MAXCOUNT);

	lxarg.mountdirfd = export_fd;
	lxarg.handle = myself->handle;
	lxarg.cookie = 0; /* For now gpfs doesn't support cookie */
	lxarg.verifier = 0; /* @todo: protocol has no verifier now */
	lxarg.eof = false;
	lxarg.name_len = MAXCOUNT;
	lxarg.names = buf;
	if (op_ctx && op_ctx->client)
		lxarg.cli_ip = op_ctx->client->hostaddr_str;

	LogFullDebug(COMPONENT_FSAL,
		"in cookie %llu len %d",
		(unsigned long long)lxarg.cookie, la_maxcount);

	rc = gpfs_ganesha(OPENHANDLE_LISTXATTRS, &lxarg);
	if (rc < 0) {
		errsv = errno;
		LogDebug(COMPONENT_FSAL,
			"LISTXATTRS returned rc %d errsv %d",
			rc, errsv);
		gsh_free(buf);
		if (errsv == ERANGE)
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	if (!lxarg.eof) {
		errsv = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to get xattr.");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	/* Only return names that the caller can read via getxattr */
	name = buf;
	end = buf + rc;
	entry->utf8string_len = 0;
	entry->utf8string_val = NULL;

	while (name < end) {
		next = strchr(name, '\0');
		next += 1;

		LogDebug(COMPONENT_FSAL,
			 "nameP %s at offset %td", name, (next - name));

		if (entryCount >= *la_cookie) {
			if ((((char *)entry - (char *)lr_names->xl4_entries) +
			     sizeof(component4) > la_maxcount) ||
			     ((val - valstart)+(next - name) > la_maxcount)) {
				gsh_free(buf);
				*lr_eof = false;

				lr_names->xl4_count = entryCount - *la_cookie;
				*la_cookie += entryCount;
				LogFullDebug(COMPONENT_FSAL,
				   "out1 cookie %llu off %td eof %d",
				   (unsigned long long)*la_cookie,
				   (next - name), *lr_eof);

				if (lr_names->xl4_count == 0)
					return fsalstat(ERR_FSAL_TOOSMALL, 0);
				return fsalstat(ERR_FSAL_NO_ERROR, 0);
			}
			entry->utf8string_len = next - name;
			entry->utf8string_val = val;
			memcpy(entry->utf8string_val, name,
						entry->utf8string_len);
			entry->utf8string_val[entry->utf8string_len] = '\0';

			LogFullDebug(COMPONENT_FSAL,
				"entry %d val %p at %p len %d at %p name %s",
				entryCount, val, entry, entry->utf8string_len,
				entry->utf8string_val, entry->utf8string_val);

			val += entry->utf8string_len;
			entry += 1;
		}
		/* Advance to next name in original buffer */
		name = next;
		entryCount += 1;
	}
	lr_names->xl4_count = entryCount - *la_cookie;
	*la_cookie = 0;
	*lr_eof = true;

	gsh_free(buf);

	LogFullDebug(COMPONENT_FSAL,
		"out2 cookie %llu eof %d",
		(unsigned long long)*la_cookie, *lr_eof);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * NOTE: this is done under protection of the attributes rwlock in cache entry.
 */
fsal_status_t gpfs_setattr2(struct fsal_obj_handle *obj_hdl,
				   bool bypass,
				   struct state_t *state,
				   struct fsal_attrlist *attrs)
{
	fsal_status_t status;

	status = GPFSFSAL_setattrs(obj_hdl, attrs);

	return status;
}

/* file_unlink
 * unlink the named file in the directory
 */
static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *obj_hdl,
				 const char *name)
{
	fsal_status_t status;

	status = GPFSFSAL_unlink(dir_hdl, name);

	return status;
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
	LogFullDebug(COMPONENT_FSAL,
		"FSAL fh_size %zu type %d", fh_size, output_type);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	LogMajor(COMPONENT_FSAL,
		 "Space too small for handle.  need %zu, have %zu",
		 fh_size, fh_desc->len);

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

/**
 * @brief release object handle
 *
 * release our export first so they know we are gone
 */
static void release(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself;
	const object_file_type_t type = obj_hdl->type;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	LogFullDebug(COMPONENT_FSAL, "type %d", type);
	if (type == REGULAR_FILE) {
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		if (myself->u.file.fd.openflags != FSAL_O_CLOSED) {
			fsal_internal_close(myself->u.file.fd.fd, NULL, 0);
			myself->u.file.fd.fd = -1;
			myself->u.file.fd.openflags = FSAL_O_CLOSED;
		}

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

	fsal_obj_handle_fini(obj_hdl);

	if (type == SYMBOLIC_LINK) {
		gsh_free(myself->u.symlink.link_content);
	}
	gsh_free(myself);
}

/**
 *
 *  @param ops Object operations
 */
void gpfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->release = release;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->getattrs = getattrs;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->seek = gpfs_seek;
	ops->io_advise = gpfs_io_advise;
	ops->close = gpfs_close;
	ops->handle_to_wire = handle_to_wire;
	ops->handle_to_key = handle_to_key;
	handle_ops_pnfs(ops);
	ops->getxattrs = getxattrs;
	ops->setxattrs = setxattrs;
	ops->removexattrs = removexattrs;
	ops->listxattrs = listxattrs;
	ops->open2 = gpfs_open2;
	ops->reopen2 = gpfs_reopen2;
	ops->read2 = gpfs_read2;
	ops->write2 = gpfs_write2;
	ops->commit2 = gpfs_commit2;
	ops->setattr2 = gpfs_setattr2;
	ops->close2 = gpfs_close2;
	ops->lock_op2 = gpfs_lock_op2;
	ops->merge = gpfs_merge;
	ops->is_referral = fsal_common_is_referral;
	ops->fallocate = gpfs_fallocate;
}

/**
 *  @param exp_hdl Handle
 *  @param path The path to lookup
 *  @param handle Reference to handle
 *
 *  modelled on old api except we don't stuff attributes.
 *  @return Status of operation
 */
fsal_status_t gpfs_lookup_path(struct fsal_export *exp_hdl,
			       const char *path,
			       struct fsal_obj_handle **handle,
			       struct fsal_attrlist *attrs_out)
{
	fsal_status_t fsal_status;
	int retval = 0;
	int dir_fd;
	struct fsal_filesystem *fs;
	struct gpfs_fsal_obj_handle *hdl;
	struct fsal_attrlist attributes;
	gpfsfsal_xstat_t buffxstat;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));
	struct fsal_fsid__ fsid;
	struct gpfs_fsal_export *gpfs_export;
	gpfs_acl_t *acl_buf;
	unsigned int acl_buflen;
	bool use_acl;
	int retry;

	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = GPFS_MAX_FH_SIZE;

	*handle = NULL;	/* poison it */

	dir_fd = open_dir_by_path_walk(-1, path, &buffxstat.buffstat);

	fsal_prepare_attrs(&attributes, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attributes.request_mask |= attrs_out->request_mask;

	if (dir_fd < 0) {
		LogDebug(COMPONENT_FSAL,
			 "Could not open directory for path %s", path);
		fsal_status = fsalstat(posix2fsal_error(-dir_fd), retval);
		goto errout;
	}

	fsal_status = fsal_internal_fd2handle(dir_fd, fh);
	if (FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	gpfs_export = container_of(exp_hdl, struct gpfs_fsal_export, export);

	/* Let us make the first request using the acl buffer that is
	 * part of buffxstat itself. If that is not sufficient, we
	 * allocate from heap and retry.
	 */
	use_acl = attributes.request_mask & ATTR_ACL;
	for (retry = 0; retry < GPFS_ACL_MAX_RETRY; retry++) {
		switch (retry) {
		case 0: /* first attempt */
			acl_buf = (gpfs_acl_t *)buffxstat.buffacl;
			acl_buflen = GPFS_ACL_BUF_SIZE;
			break;
		case 1: /* first retry, don't free the old stack buffer */
			acl_buflen = acl_buf->acl_len;
			acl_buf = gsh_malloc(acl_buflen);
			break;
		default: /* second or later retry, free the old heap buffer */
			acl_buflen = acl_buf->acl_len;
			gsh_free(acl_buf);
			acl_buf = gsh_malloc(acl_buflen);
			break;
		}

		fsal_status = fsal_get_xstat_by_handle(dir_fd, fh, &buffxstat,
				acl_buf, acl_buflen, NULL, false, use_acl);
		if (FSAL_IS_ERROR(fsal_status) || !use_acl ||
				acl_buflen >= acl_buf->acl_len)
			break;
	}

	if (retry == GPFS_ACL_MAX_RETRY) { /* make up an error */
		LogCrit(COMPONENT_FSAL, "unable to get ACLs");
		fsal_status = fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	if (FSAL_IS_ERROR(fsal_status))
		goto xstat_err;

	fsal_status = gpfsfsal_xstat_2_fsal_attributes(&buffxstat, &attributes,
						acl_buf, gpfs_export->use_acl);
	LogFullDebug(COMPONENT_FSAL,
		     "fsid=0x%016"PRIx64".0x%016"PRIx64,
		     attributes.fsid.major, attributes.fsid.minor);

	if (FSAL_IS_ERROR(fsal_status))
		goto xstat_err;

	if (acl_buflen != GPFS_ACL_BUF_SIZE) {
		assert(acl_buf != (gpfs_acl_t *)buffxstat.buffacl);
		gsh_free(acl_buf);
	}

	close(dir_fd);

	gpfs_extract_fsid(fh, &fsid);

	fs = lookup_fsid(&fsid, GPFS_FSID_TYPE);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find file system for path %s",
			path);
		fsal_status = fsalstat(posix2fsal_error(ENOENT), ENOENT);
		goto errout;
	}
	if (fs->fsal != exp_hdl->fsal) {
		LogInfo(COMPONENT_FSAL,
			"File system for path %s did not belong to FSAL %s",
			path, exp_hdl->fsal->name);
		fsal_status = fsalstat(posix2fsal_error(EACCES), EACCES);
		goto errout;
	}

	LogDebug(COMPONENT_FSAL,
		 "filesystem %s for path %s",
		 fs->path, path);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, fs, &attributes, NULL, exp_hdl);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attributes, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attributes);
	}

	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

xstat_err:
	/* free if the acl buffer is from the heap */
	if (acl_buflen != GPFS_ACL_BUF_SIZE) {
		assert(acl_buf != (gpfs_acl_t *)buffxstat.buffacl);
		gsh_free(acl_buf);
	}

fileerr:
	close(dir_fd);

errout:
	/* Done with attributes */
	fsal_release_attrs(&attributes);
	return fsal_status;
}

/**
 * @brief create GPFS handle
 *
 * @param exp_hdl export handle
 * @param hdl_desc handle description
 * @param handle object handle
 * @return status
 *
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
fsal_status_t gpfs_create_handle(struct fsal_export *exp_hdl,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle *fh;
	struct fsal_attrlist attrib;
	char link_buff[PATH_MAX];
	struct fsal_fsid__ fsid;
	struct fsal_filesystem *fs;
	struct gpfs_filesystem *gpfs_fs;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	*handle = NULL;		/* poison it first */
	if ((hdl_desc->len > (sizeof(struct gpfs_file_handle))))
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len);

	gpfs_extract_fsid(fh, &fsid);

	fs = lookup_fsid(&fsid, GPFS_FSID_TYPE);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find filesystem for fsid=0x%016"PRIx64
			".0x%016"PRIx64" from handle",
			fsid.major, fsid.minor);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	if (fs->fsal != exp_hdl->fsal) {
		LogInfo(COMPONENT_FSAL,
			"Non GPFS filesystem fsid=0x%016"PRIx64
			".0x%016"PRIx64" from handle",
			fsid.major, fsid.minor);
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	gpfs_fs = fs->private_data;

	fsal_prepare_attrs(&attrib, ATTR_GPFS_ALLOC_HANDLE);

	if (attrs_out != NULL)
		attrib.request_mask |= attrs_out->request_mask;

	status = GPFSFSAL_getattrs(exp_hdl, gpfs_fs, fh, &attrib);
	if (FSAL_IS_ERROR(status))
		return status;

	if (attrib.type == SYMBOLIC_LINK) {	/* I could lazy eval this... */
		status = fsal_readlink_by_handle(export_fd, fh,
						 link_buff, sizeof(link_buff));
		if (FSAL_IS_ERROR(status))
			return status;
	}

	hdl = alloc_handle(fh, fs, &attrib, link_buff, exp_hdl);

	if (attrs_out != NULL) {
		/* Copy the attributes to caller, passing ACL ref. */
		fsal_copy_attrs(attrs_out, &attrib, true);
	} else {
		/* Done with the attrs */
		fsal_release_attrs(&attrib);
	}

	*handle = &hdl->obj_handle;

	return status;
}
