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
 */

/* handle.c
 * VFS object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include <fsal_handle_syscalls.h>
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "nlm_list.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_handle.h"
#include "lustre_methods.h"
#include <stdbool.h>

#ifdef HAVE_INCLUDE_LUSTREAPI_H
#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>
#else
#ifdef HAVE_INCLUDE_LIBLUSTREAPI_H
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>
#define FID_SEQ_ROOT  0x200000007ULL
#endif
#endif


/* helpers
 */

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */

static struct lustre_fsal_obj_handle *alloc_handle(struct lustre_file_handle
						   *fh, struct stat *stat,
						   const char *link_content,
						   struct lustre_file_handle
						   *dir_fh,
						   const char *sock_name,
						   struct fsal_export *exp_hdl)
{
	struct lustre_fsal_obj_handle *hdl;
	fsal_status_t st;

	hdl =
	    gsh_malloc(sizeof(struct lustre_fsal_obj_handle) +
		       sizeof(struct lustre_file_handle));
	if (hdl == NULL)
		return NULL;
	memset(hdl, 0,
	       (sizeof(struct lustre_fsal_obj_handle) +
		sizeof(struct lustre_file_handle)));
	hdl->handle = (struct lustre_file_handle *)&hdl[1];
	memcpy(hdl->handle, fh, sizeof(struct lustre_file_handle));
	hdl->obj_handle.type = posix2fsal_type(stat->st_mode);
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
	} else if (hdl->obj_handle.type == SOCKET_FILE && dir_fh != NULL
		   && sock_name != NULL) {
		hdl->u.sock.sock_dir =
		    gsh_malloc(sizeof(struct lustre_file_handle));
		if (hdl->u.sock.sock_dir == NULL)
			goto spcerr;
		memcpy(hdl->u.sock.sock_dir, dir_fh,
		       sizeof(struct lustre_file_handle));
		hdl->u.sock.sock_name = gsh_malloc(strlen(sock_name) + 1);
		if (hdl->u.sock.sock_name == NULL)
			goto spcerr;
		strcpy(hdl->u.sock.sock_name, sock_name);
	}
	hdl->obj_handle.export = exp_hdl;
	hdl->obj_handle.attributes.mask =
	    exp_hdl->ops->fs_supported_attrs(exp_hdl);
	st = posix2fsal_attributes(stat, &hdl->obj_handle.attributes);
	if (FSAL_IS_ERROR(st))
		goto spcerr;
	if (!fsal_obj_handle_init
	    (&hdl->obj_handle, exp_hdl, posix2fsal_type(stat->st_mode)))
		return hdl;

	hdl->obj_handle.ops = NULL;
	pthread_mutex_unlock(&hdl->obj_handle.lock);
	pthread_mutex_destroy(&hdl->obj_handle.lock);
 spcerr:
	if (hdl->obj_handle.type == SYMBOLIC_LINK) {
		if (hdl->u.symlink.link_content != NULL)
			gsh_free(hdl->u.symlink.link_content);
	} else if (hdl->obj_handle.type == SOCKET_FILE) {
		if (hdl->u.sock.sock_name != NULL)
			gsh_free(hdl->u.sock.sock_name);
		if (hdl->u.sock.sock_dir != NULL)
			gsh_free(hdl->u.sock.sock_dir);
	}
	gsh_free(hdl);		/* elvis has left the building */
	return NULL;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */
static fsal_status_t lustre_lookupp(struct fsal_obj_handle *parent,
				const struct req_op_context *opctx,
				struct fsal_obj_handle **handle)
{
	int retval;
	struct lustre_fsal_obj_handle *parent_hdl, *hdl;
	char path[MAXPATHLEN];
	lustre_fid parentfid;
	char name[MAXNAMLEN];
	char parentpath[MAXPATHLEN];
	struct stat stat;
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	memset(fh, 0, sizeof(struct lustre_file_handle));
	parent_hdl =
		container_of(parent, struct lustre_fsal_obj_handle,
			     obj_handle);

	if (!parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	retval =
		lustre_handle_to_path(lustre_get_root_path(parent->export),
	parent_hdl->handle, path);
	if (retval < 0) {
		retval = errno;
		return fsalstat(posix2fsal_error(retval), retval);
	}

	retval = Lustre_GetNameParent(path,
				      0,
				      &parentfid,
				      name,
				      MAXNAMLEN);

	if (retval < 0)
		return fsalstat(posix2fsal_error(-retval), -retval);

	snprintf(parentpath, MAXPATHLEN, "%s/.lustre/fid/" DFID_NOBRACE,
		 lustre_get_root_path(parent->export),
		PFID(&parentfid));

	retval = lustre_path_to_handle(parentpath, fh);
	if (retval < 0) {
		retval = errno;
		return fsalstat(posix2fsal_error(retval), retval);
	}

	retval = lstat(parentpath, &stat);
	if (retval < 0) {
		retval = errno;
		return fsalstat(posix2fsal_error(retval), retval);
	}

	/* allocate an obj_handle and fill it up */
	hdl =
		alloc_handle(fh, &stat, NULL, NULL, NULL,
			     parent->export);

	if (hdl != NULL)
		*handle = &hdl->obj_handle;
	else {
		*handle = NULL; /* poison it */
		return fsalstat(ERR_FSAL_NOMEM, ENOMEM);
	}

	/* Success !! */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t lustre_lookup(struct fsal_obj_handle *parent,
				   const struct req_op_context *opctx,
				   const char *path,
				   struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct stat stat;
	char *link_content = NULL;
	struct lustre_file_handle *dir_hdl = NULL;
	const char *sock_name = NULL;
	ssize_t retlink;
	char fidpath[MAXPATHLEN];
	char link_buff[PATH_MAX+1];
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	if (!path)
		return fsalstat(ERR_FSAL_FAULT, 0);

	if (!strcmp(path, ".."))
		return lustre_lookupp(parent, opctx, handle);

	memset(fh, 0, sizeof(struct lustre_file_handle));
	parent_hdl =
	    container_of(parent, struct lustre_fsal_obj_handle, obj_handle);
	if (!parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	retval =
	    lustre_name_to_handle_at(lustre_get_root_path(parent->export),
				     parent_hdl->handle, path, fh, 0);
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	lustre_handle_to_path(lustre_get_root_path(parent->export), fh,
			      fidpath);

	retval = lstat(fidpath, &stat);
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	if (S_ISLNK(stat.st_mode)) {	/* I could lazy eval this... */
		retlink = readlink(fidpath, link_buff, PATH_MAX);
		if (retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if (retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = &link_buff[0];
	} else if (S_ISSOCK(stat.st_mode)) {
		dir_hdl = parent_hdl->handle;
		sock_name = path;
	}
	/* allocate an obj_handle and fill it up */
	hdl =
	    alloc_handle(fh, &stat, link_content, dir_hdl, sock_name,
			 parent->export);
	if (hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL;	/* poison it */
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	return fsalstat(fsal_error, retval);
}

static inline int get_stat_by_handle_at(char *mntpath,
					struct lustre_file_handle *infh,
					const char *name,
					struct lustre_file_handle *fh,
					struct stat *stat)
{
	int retval;
	char lustre_path[MAXPATHLEN];
	char filepath[MAXPATHLEN];

	retval = lustre_handle_to_path(mntpath, infh, lustre_path);
	if (retval < 0) {
		retval = errno;
		goto fileerr;
	}
	snprintf(filepath, MAXPATHLEN, "%s/%s", lustre_path, name);

	/* now that it is owned properly, set accessible mode */

	retval = lustre_name_to_handle_at(mntpath, infh, name, fh, 0);
	if (retval < 0)
		goto fileerr;

	retval = lstat(filepath, stat);
	if (retval < 0) {
		retval = errno;
		goto fileerr;
	}

 fileerr:
	retval = errno;
	return retval;
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t lustre_create(struct fsal_obj_handle *dir_hdl,
				   const struct req_op_context *opctx,
				   const char *name, struct attrlist *attrib,
				   struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
	char newpath[MAXPATHLEN];
	char dirpath[MAXPATHLEN];
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int fd;
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle));
	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	lustre_handle_to_path(lustre_get_root_path(dir_hdl->export),
			      myself->handle, dirpath);

	retval = lstat(dirpath, &stat);
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	/* create it with no access because we are root when we do this
	 * we use openat because there is no creatat...
	 */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);
	fd = CRED_WRAP(opctx->creds, int, open, newpath,
		       O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, unix_mode);
	if (fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	close(fd);		/* not needed anymore */

	retval =
	    get_stat_by_handle_at(lustre_get_root_path(dir_hdl->export),
				  myself->handle, name, fh, &stat);
	if (retval < 0)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, NULL, NULL, dir_hdl->export);
	if (hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	fsal_error = posix2fsal_error(retval);
	unlink(newpath);	/* remove the evidence on errors */
 errout:
	return fsalstat(fsal_error, retval);
}

static fsal_status_t lustre_makedir(struct fsal_obj_handle *dir_hdl,
				    const struct req_op_context *opctx,
				    const char *name, struct attrlist *attrib,
				    struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
	char dirpath[MAXPATHLEN];
	char newpath[MAXPATHLEN];
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle));
	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	lustre_handle_to_path(lustre_get_root_path(dir_hdl->export),
			      myself->handle, dirpath);
	retval = lstat(dirpath, &stat);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}

	/* create it with no access because we are root when we do this */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);
	retval = CRED_WRAP(opctx->creds, int, mkdir, newpath, unix_mode);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}
	retval =
	    get_stat_by_handle_at(lustre_get_root_path(dir_hdl->export),
				  myself->handle, name, fh, &stat);
	if (retval < 0)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, NULL, NULL, dir_hdl->export);
	if (hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 direrr:
	retval = errno;
	fsal_error = posix2fsal_error(retval);

	return fsalstat(fsal_error, retval);

 fileerr:
	fsal_error = posix2fsal_error(retval);
	rmdir(newpath);		/* remove the evidence on errors */
 errout:
	return fsalstat(fsal_error, retval);
}

static fsal_status_t lustre_makenode(struct fsal_obj_handle *dir_hdl,
				     const struct req_op_context *opctx,
				     const char *name,
				     object_file_type_t nodetype,	/* IN */
				     fsal_dev_t *dev,	/* IN */
				     struct attrlist *attrib,
				     struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
	char dirpath[MAXPATHLEN];
	char newpath[MAXPATHLEN];
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	dev_t unix_dev = 0;
	struct lustre_file_handle *dir_fh = NULL;
	const char *sock_name = NULL;
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	*handle = NULL;		/* poison it */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle));
	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	switch (nodetype) {
	case BLOCK_FILE:
		if (!dev) {
			fsal_error = ERR_FSAL_FAULT;
			goto errout;
		}
		unix_dev =
		    CRED_WRAP(opctx->creds, dev_t, makedev, dev->major,
			      dev->minor);
		break;
	case CHARACTER_FILE:
		if (!dev) {
			fsal_error = ERR_FSAL_FAULT;
			goto errout;

		}
		unix_dev =
		    CRED_WRAP(opctx->creds, dev_t, makedev, dev->major,
			      dev->minor);
		break;
	case FIFO_FILE:
		break;
	case SOCKET_FILE:
		dir_fh = myself->handle;
		sock_name = name;
		break;
	default:
		LogMajor(COMPONENT_FSAL, "Invalid node type in FSAL_mknode: %d",
			 nodetype);
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	lustre_handle_to_path(lustre_get_root_path(dir_hdl->export),
			      myself->handle, dirpath);
	retval = lstat(dirpath, &stat);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}

	/* create it with no access because we are root when we do this */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);
	retval =
	    CRED_WRAP(opctx->creds, int, mknod, newpath, unix_mode, unix_dev);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}
	retval =
	    get_stat_by_handle_at(lustre_get_root_path(dir_hdl->export),
				  myself->handle, name, fh, &stat);
	if (retval < 0)
		goto direrr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, dir_fh, sock_name, dir_hdl->export);
	if (hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 direrr:
	fsal_error = posix2fsal_error(retval);
 errout:
	unlink(newpath);
	return fsalstat(fsal_error, retval);
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t lustre_makesymlink(struct fsal_obj_handle *dir_hdl,
					const struct req_op_context *opctx,
					const char *name, const char *link_path,
					struct attrlist *attrib,
					struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
	char dirpath[MAXPATHLEN];
	char newpath[MAXPATHLEN];
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	*handle = NULL;		/* poison it first */
	if (!dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle));
	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	lustre_handle_to_path(lustre_get_root_path(dir_hdl->export),
			      myself->handle, dirpath);
	retval = lstat(dirpath, &stat);
	if (retval < 0)
		goto direrr;
	if (stat.st_mode & S_ISGID) {
		/*setgid bit on dir propagates dir group owner */
		group = -1;
	}

	/* create it with no access because we are root when we do this */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);

	retval = CRED_WRAP(opctx->creds, int, symlink, link_path, newpath);
	if (retval < 0)
		goto direrr;

	/* do this all by hand because we can't use fchmodat on symlinks...
	 */
	retval = lchown(newpath, user, group);
	if (retval < 0)
		goto linkerr;

	retval =
	    lustre_name_to_handle_at(lustre_get_root_path(dir_hdl->export),
				     myself->handle, name, fh, 0);
	if (retval < 0)
		goto linkerr;

	/* now get attributes info, being careful
	 * to get the link, not the target */
	retval = lstat(newpath, &stat);
	if (retval < 0)
		goto linkerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, link_path, NULL, NULL, dir_hdl->export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 linkerr:
	retval = errno;
	unlink(newpath);
	goto errout;

 direrr:
	retval = errno;
 errout:
	if (retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t lustre_readsymlink(struct fsal_obj_handle *obj_hdl,
					const struct req_op_context *opctx,
					struct gsh_buffdesc *link_content,
					bool refresh)
{
	struct lustre_fsal_obj_handle *myself = NULL;
	char mypath[MAXPATHLEN];
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if (obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	if (refresh) {		/* lazy load or LRU'd storage */
		ssize_t retlink;
		char link_buff[PATH_MAX+1];

		if (myself->u.symlink.link_content != NULL) {
			gsh_free(myself->u.symlink.link_content);
			myself->u.symlink.link_content = NULL;
			myself->u.symlink.link_size = 0;
		}
		lustre_handle_to_path(lustre_get_root_path(obj_hdl->export),
				      myself->handle, mypath);
		retlink = readlink(mypath, link_buff, PATH_MAX);
		 if (retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if (retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			goto out;
		}

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
		goto out;
	}
	memcpy(link_content->addr, myself->u.symlink.link_content,
	       myself->u.symlink.link_size);

 out:
	return fsalstat(fsal_error, retval);
}

static fsal_status_t lustre_linkfile(struct fsal_obj_handle *obj_hdl,
				     const struct req_op_context *opctx,
				     struct fsal_obj_handle *destdir_hdl,
				     const char *name)
{
	struct lustre_fsal_obj_handle *myself, *destdir;
	char srcpath[MAXPATHLEN];
	char destdirpath[MAXPATHLEN];
	char destnamepath[MAXPATHLEN];
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if (!obj_hdl->export->ops->
	    fs_supports(obj_hdl->export, fso_link_support)) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(lustre_get_root_path(obj_hdl->export),
			      myself->handle, srcpath);

	destdir =
	    container_of(destdir_hdl, struct lustre_fsal_obj_handle,
			 obj_handle);
	lustre_handle_to_path(lustre_get_root_path(obj_hdl->export),
			      destdir->handle, destdirpath);

	snprintf(destnamepath, MAXPATHLEN, "%s/%s", destdirpath, name);

	retval = CRED_WRAP(opctx->creds, int, link, srcpath, destnamepath);
	if (retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
 out:
	return fsalstat(fsal_error, retval);
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

static fsal_status_t lustre_read_dirents(struct fsal_obj_handle *dir_hdl,
					 const struct req_op_context *opctx,
					 fsal_cookie_t *whence,
					 void *dir_state, fsal_readdir_cb cb,
					 bool *eof)
{
	struct lustre_fsal_obj_handle *myself;
	int dirfd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	off_t seekloc = 0;
	int bpos, cnt, nread;
	struct linux_dirent *dentry;
	char buf[BUF_SIZE];

	if (whence != NULL)
		seekloc = (off_t) *whence;

	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	dirfd =
	    lustre_open_by_handle(lustre_get_root_path(dir_hdl->export),
				  myself->handle, (O_RDONLY | O_DIRECTORY));
	if (dirfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if (seekloc < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto done;
	}
	cnt = 0;
	do {
		nread = syscall(SYS_getdents, dirfd, buf, BUF_SIZE);
		if (nread < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto done;
		}
		if (nread == 0)
			break;
		for (bpos = 0; bpos < nread;) {
			dentry = (struct linux_dirent *)(buf + bpos);
			if (strcmp(dentry->d_name, ".") == 0
			    || strcmp(dentry->d_name, "..") == 0)
				goto skip;	/* must skip '.' and '..' */

			/* In Lustre 2.4 and above, .lustre behave weirdly
			 * so we skip this entry to avoid troubles */
			if ((myself->handle->fid.f_seq == FID_SEQ_ROOT) &&
				(strcmp(dentry->d_name, ".lustre") == 0)) {
				goto skip;
			}

			/* callback to cache inode */
			if (!cb(opctx,
				dentry->d_name,
				dir_state,
				(fsal_cookie_t) dentry->d_off))
					goto done;
 skip:
			bpos += dentry->d_reclen;
			cnt++;
		}
	} while (nread > 0);

	*eof = (nread == 0);
 done:
	close(dirfd);

 out:
	return fsalstat(fsal_error, retval);
}

static fsal_status_t lustre_renamefile(struct fsal_obj_handle *olddir_hdl,
				       const struct req_op_context *opctx,
				       const char *old_name,
				       struct fsal_obj_handle *newdir_hdl,
				       const char *new_name)
{
	struct lustre_fsal_obj_handle *olddir, *newdir;
	char olddirpath[MAXPATHLEN];
	char oldnamepath[MAXPATHLEN];
	char newdirpath[MAXPATHLEN];
	char newnamepath[MAXPATHLEN];
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	olddir =
	    container_of(olddir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(lustre_get_root_path(olddir_hdl->export),
			      olddir->handle, olddirpath);
	snprintf(oldnamepath, MAXPATHLEN, "%s/%s", olddirpath, old_name);

	newdir =
	    container_of(newdir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(lustre_get_root_path(newdir_hdl->export),
			      newdir->handle, newdirpath);
	snprintf(newnamepath, MAXPATHLEN, "%s/%s", newdirpath, new_name);

	retval = CRED_WRAP(opctx->creds, int, rename, oldnamepath, newnamepath);
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	return fsalstat(fsal_error, retval);
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t lustre_getattrs(struct fsal_obj_handle *obj_hdl,
				     const struct req_op_context *opctx)
{
	struct lustre_fsal_obj_handle *myself;
	char mypath[MAXPATHLEN];
	int open_flags = O_RDONLY;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t st;
	int retval = 0;

	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	if (obj_hdl->type == SOCKET_FILE) {
		lustre_handle_to_path(lustre_get_root_path(obj_hdl->export),
				      myself->u.sock.sock_dir, mypath);
	} else {
		if (obj_hdl->type == SYMBOLIC_LINK)
			open_flags |= O_PATH;
		else if (obj_hdl->type == FIFO_FILE)
			open_flags |= O_NONBLOCK;

		lustre_handle_to_path(lustre_get_root_path(obj_hdl->export),
				      myself->handle, mypath);
	}

	retval = lstat(mypath, &stat);
	if (retval < 0)
		goto errout;

	/* convert attributes */
	st = posix2fsal_attributes(&stat, &obj_hdl->attributes);
	if (FSAL_IS_ERROR(st)) {
		FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
		FSAL_SET_MASK(obj_hdl->attributes.mask, ATTR_RDATTR_ERR);
		fsal_error = st.major;
		retval = st.minor;
		goto out;
	}
	goto out;

 errout:
	retval = errno;
	if (retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);
 out:
	return fsalstat(fsal_error, retval);
}

/*
 * NOTE: this is done under protection of
 * the attributes rwlock in the cache entry.
 */

static fsal_status_t lustre_setattrs(struct fsal_obj_handle *obj_hdl,
				     const struct req_op_context *opctx,
				     struct attrlist *attrs)
{
	struct lustre_fsal_obj_handle *myself;
	char mypath[MAXPATHLEN];
	char mysockpath[MAXPATHLEN];
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
		attrs->mode &= ~obj_hdl->export->ops->fs_umask(obj_hdl->export);
	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	/* This is yet another "you can't get there from here".
	 * If this object is a socket (AF_UNIX), an fd on the
	 * socket is useless _period_.
	 * If it is for a symlink, without O_PATH, you will
	 * get an ELOOP error and (f)chmod doesn't work for
	 * a symlink anyway - not that it matters
	 * because access checking is not done on the symlink
	 * but the final target.
	 * AF_UNIX sockets are also ozone material.  If the
	 * socket is already active listeners et al, you can
	 * manipulate the mode etc.  If it is just sitting
	 * there as in you made it with a mknod (one of those
	 * leaky abstractions...) or the listener forgot to unlink it,
	 * it is lame duck.
	 */

	if (obj_hdl->type == SOCKET_FILE) {
		lustre_handle_to_path(lustre_get_root_path(obj_hdl->export),
				      myself->u.sock.sock_dir, mypath);
		retval = lstat(mypath, &stat);
	} else {
		lustre_handle_to_path(lustre_get_root_path(obj_hdl->export),
				      myself->handle, mypath);
		retval = lstat(mypath, &stat);
	}
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	/** TRUNCATE **/
	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			fsal_error = ERR_FSAL_INVAL;
			goto out;
		}
		retval =
		    CRED_WRAP(opctx->creds, int, truncate, mypath,
			      attrs->filesize);
		if (retval != 0)
			goto fileerr;
	}
	/** CHMOD **/
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		/* The POSIX chmod call doesn't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if (!S_ISLNK(stat.st_mode)) {
			if (obj_hdl->type == SOCKET_FILE) {
				snprintf(mysockpath, MAXPATHLEN, "%s/%s",
					 mypath, myself->u.sock.sock_name);
				retval =
				    chmod(mysockpath,
					  fsal2unix_mode(attrs->mode));
			} else
				retval =
				    chmod(mypath, fsal2unix_mode(attrs->mode));

			if (retval != 0)
				goto fileerr;
		}
	}

	/**  CHOWN  **/
	if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER | ATTR_GROUP)) {
		uid_t user = FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)
		    ? (int)attrs->owner : -1;
		gid_t group = FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)
		    ? (int)attrs->group : -1;

		if (obj_hdl->type == SOCKET_FILE) {
			snprintf(mysockpath, MAXPATHLEN, "%s/%s", mypath,
				 myself->u.sock.sock_name);
			retval = lchown(mysockpath, user, group);
		} else
			retval = lchown(mypath, user, group);

		if (retval)
			goto fileerr;
	}

	/**  UTIME  **/
	if (FSAL_TEST_MASK
	    (attrs->mask,
	     ATTR_ATIME | ATTR_MTIME | ATTR_ATIME_SERVER | ATTR_MTIME_SERVER)) {
		struct timeval timebuf[2];
		struct timeval *ptimebuf = timebuf;

		/* Atime */
		timebuf[0].tv_sec =
		    (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME) ? (time_t) attrs->
		     atime.tv_sec : stat.st_atime);
		timebuf[0].tv_usec = 0;

		/* Mtime */
		timebuf[1].tv_sec =
		    (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME) ? (time_t) attrs->
		     mtime.tv_sec : stat.st_mtime);
		timebuf[1].tv_usec = 0;
		if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)
		    && FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
			/* If both times are set to server time,
			 * we can shortcut and use the utimes interface
			 * to set both times to current time. */
			ptimebuf = NULL;
		} else {
			if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
				/* Since only one time is set to
				 * server time, we must get time of day
				 * to set it. */
				gettimeofday(&timebuf[0], NULL);
			}
			if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
				/* Since only one time is set to server time,
				 * we must get time of day to set it. */
				gettimeofday(&timebuf[1], NULL);
			}
		}

		if (obj_hdl->type == SOCKET_FILE) {
			snprintf(mysockpath, MAXPATHLEN, "%s/%s", mypath,
				 myself->u.sock.sock_name);
			retval = utimes(mysockpath, timebuf);
		} else if (obj_hdl->type == SYMBOLIC_LINK) {
			/* Setting utimes on a SYMLINK is illegal. Do nothing */
			retval = 0;
		} else
			retval = utimes(mypath, ptimebuf);
		if (retval != 0)
			goto fileerr;
	}
	return fsalstat(fsal_error, retval);

 fileerr:
	retval = errno;
	fsal_error = posix2fsal_error(retval);
 out:
	return fsalstat(fsal_error, retval);
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t lustre_file_unlink(struct fsal_obj_handle *dir_hdl,
					const struct req_op_context *opctx,
					const char *name)
{
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	char dirpath[MAXPATHLEN];
	char filepath[MAXPATHLEN];
	struct stat stat;
	int retval = 0;

	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(lustre_get_root_path(dir_hdl->export),
			      myself->handle, dirpath);
	snprintf(filepath, MAXPATHLEN, "%s/%s", dirpath, name);
	retval = lstat(filepath, &stat);
	if (retval < 0) {
		retval = errno;
		if (retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto out;
	}
	if (S_ISDIR(stat.st_mode))
		retval = CRED_WRAP(opctx->creds, int, rmdir, filepath);
	else
		retval = CRED_WRAP(opctx->creds, int, unlink, filepath);
	if (retval < 0) {
		retval = errno;
		if (retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
	}

 out:
	return fsalstat(fsal_error, retval);
}

/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t lustre_handle_digest(const struct fsal_obj_handle *obj_hdl,
					  fsal_digesttype_t output_type,
					  struct gsh_buffdesc *fh_desc)
{
	const struct lustre_fsal_obj_handle *myself;
	struct lustre_file_handle *fh;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself =
	    container_of(obj_hdl, const struct lustre_fsal_obj_handle,
			 obj_handle);
	fh = myself->handle;

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = lustre_sizeof_handle(fh);
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

static void lustre_handle_to_key(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *fh_desc)
{
	struct lustre_fsal_obj_handle *myself;

	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = lustre_sizeof_handle(myself->handle);
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct lustre_fsal_obj_handle *myself;
	int retval = 0;
	object_file_type_t type = obj_hdl->type;

	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	if (type == REGULAR_FILE
	    && (myself->u.file.fd >= 0
		|| myself->u.file.openflags != FSAL_O_CLOSED)) {
		LogCrit(COMPONENT_FSAL,
			"Tried to release busy handle, "
			"hdl = 0x%p, fd = %d, openflags = 0x%x", obj_hdl,
			myself->u.file.fd, myself->u.file.openflags);
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	retval = fsal_obj_handle_uninit(obj_hdl);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"Tried to release busy handle, "
			"hdl = 0x%p->refs = %d", obj_hdl, obj_hdl->refs);
		return fsalstat(posix2fsal_error(retval), retval);
	}

	if (type == SYMBOLIC_LINK) {
		if (myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	} else if (type == SOCKET_FILE) {
		if (myself->u.sock.sock_name != NULL)
			gsh_free(myself->u.sock.sock_name);
		if (myself->u.sock.sock_dir != NULL)
			gsh_free(myself->u.sock.sock_dir);
	}
	gsh_free(myself);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void lustre_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = lustre_lookup;
	ops->readdir = lustre_read_dirents;
	ops->create = lustre_create;
	ops->mkdir = lustre_makedir;
	ops->mknode = lustre_makenode;
	ops->symlink = lustre_makesymlink;
	ops->readlink = lustre_readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = lustre_getattrs;
	ops->setattrs = lustre_setattrs;
	ops->link = lustre_linkfile;
	ops->rename = lustre_renamefile;
	ops->unlink = lustre_file_unlink;
	ops->open = lustre_open;
	ops->status = lustre_status;
	ops->read = lustre_read;
	ops->write = lustre_write;
	ops->commit = lustre_commit;
	ops->lock_op = lustre_lock_op;
	ops->close = lustre_close;
	ops->lru_cleanup = lustre_lru_cleanup;
	ops->handle_digest = lustre_handle_digest;
	ops->handle_to_key = lustre_handle_to_key;

	/* xattr related functions */
	ops->list_ext_attrs = lustre_list_ext_attrs;
	ops->getextattr_id_by_name = lustre_getextattr_id_by_name;
	ops->getextattr_value_by_name = lustre_getextattr_value_by_name;
	ops->getextattr_value_by_id = lustre_getextattr_value_by_id;
	ops->setextattr_value = lustre_setextattr_value;
	ops->setextattr_value_by_id = lustre_setextattr_value_by_id;
	ops->getextattr_attrs = lustre_getextattr_attrs;
	ops->remove_extattr_by_id = lustre_remove_extattr_by_id;
	ops->remove_extattr_by_name = lustre_remove_extattr_by_name;

}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 * @todo : use of dirfd is no more needed with FSAL_LUSTRE
 */

fsal_status_t lustre_lookup_path(struct fsal_export *exp_hdl,
				 const struct req_op_context *opctx,
				 const char *path,
				 struct fsal_obj_handle **handle)
{
	int dir_fd;
	struct stat stat;
	struct lustre_fsal_obj_handle *hdl;
	char *basepart;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	char *link_content = NULL;
	ssize_t retlink;
	struct lustre_file_handle *dir_fh = NULL;
	char *sock_name = NULL;
	char dirpart[MAXPATHLEN];
	char dirfullpath[MAXPATHLEN];
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	memset(fh, 0, sizeof(struct lustre_file_handle));
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
	if (basepart == path) {
		dir_fd = open("/", O_RDONLY);
	} else {
		memcpy(dirpart, path, basepart - path);
		dirpart[basepart - path] = '\0';
		dir_fd = open(dirpart, O_RDONLY, 0600);
	}
	if (dir_fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = fstat(dir_fd, &stat);
	if (!S_ISDIR(stat.st_mode)) {	/* this had better be a DIR! */
		goto fileerr;
	}
	basepart++;
	snprintf(dirfullpath, MAXPATHLEN, "%s/%s", dirpart, basepart);
	retval = lustre_path_to_handle(path, fh);
	if (retval < 0)
		goto fileerr;

	/* what about the file? Do no symlink chasing here. */
	retval = fstatat(dir_fd, basepart, &stat, AT_SYMLINK_NOFOLLOW);
	if (retval < 0)
		goto fileerr;
	if (S_ISLNK(stat.st_mode)) {
		link_content = gsh_malloc(PATH_MAX+1);
		retlink = readlinkat(dir_fd, basepart, link_content,
				     PATH_MAX);
		if (retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if (retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			goto linkerr;
		}
		link_content[retlink] = '\0';
	} else if (S_ISSOCK(stat.st_mode)) {
		/* AF_UNIX sockets require craziness */
		dir_fh = gsh_malloc(sizeof(struct lustre_file_handle));
		memset(dir_fh, 0, sizeof(struct lustre_file_handle));
		retval = lustre_path_to_handle(path, dir_fh);
		if (retval < 0)
			goto fileerr;
		sock_name = basepart;
	}
	close(dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, link_content, dir_fh, sock_name, exp_hdl);
	if (link_content != NULL)
		gsh_free(link_content);
	if (dir_fh != NULL)
		gsh_free(dir_fh);
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
	if (dir_fh != NULL)
		gsh_free(dir_fh);
	close(dir_fd);
	fsal_error = posix2fsal_error(retval);

 errout:
	return fsalstat(fsal_error, retval);
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

fsal_status_t lustre_create_handle(struct fsal_export *exp_hdl,
				   const struct req_op_context *opctx,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *hdl;
	struct stat stat;
	struct lustre_file_handle *fh;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	char objpath[MAXPATHLEN];
	char *link_content = NULL;
	ssize_t retlink;
	char link_buff[PATH_MAX+1];

	*handle = NULL;		/* poison it first */
	if (hdl_desc->len > sizeof(struct lustre_file_handle))
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh,
		hdl_desc->addr,
		hdl_desc->len);	/* struct aligned copy */
	lustre_handle_to_path(lustre_get_root_path(exp_hdl), fh, objpath);
	retval = lstat(objpath, &stat);
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	if (S_ISLNK(stat.st_mode)) {	/* I could lazy eval this... */
		retlink = readlink(objpath, link_buff, PATH_MAX);
		if (retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if (retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = &link_buff[0];
	}

	hdl = alloc_handle(fh, &stat, link_content, NULL, NULL, exp_hdl);
	if (hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;

 errout:
	return fsalstat(fsal_error, retval);
}
