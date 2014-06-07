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
 * LUSTRE object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "ganesha_list.h"
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
#include <linux/quota.h>
#define FID_SEQ_ROOT  0x200000007ULL
#endif
#endif

#ifdef USE_FSAL_SHOOK
#include "shook_svr.h"
fsal_status_t lustre_shook_restore(struct fsal_obj_handle *obj_hdl,
				   bool do_truncate,
				   int *trunc_done);
#endif


/* helpers
 */
int lustre_extract_fsid(struct lustre_file_handle *fh,
			enum fsid_type *fsid_type,
			struct fsal_fsid__ *fsid)
{
	struct fsal_dev__ dev;

	dev = posix2fsal_devt(fh->fsdev);

	*fsid_type = FSID_TWO_UINT64;
	fsid->major = dev.major;
	fsid->minor = dev.minor;
	return 0;
}

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */
static struct lustre_fsal_obj_handle *alloc_handle(
				   struct lustre_file_handle *fh,
				   struct fsal_filesystem *fs,
				   struct stat *stat,
				   const char *link_content,
				   struct lustre_file_handle *dir_fh,
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
	hdl->dev = posix2fsal_devt(stat->st_dev);
	hdl->obj_handle.fs = fs;

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

	hdl->obj_handle.attributes.mask =
	    exp_hdl->ops->fs_supported_attrs(exp_hdl);
	st = posix2fsal_attributes(stat, &hdl->obj_handle.attributes);
	if (FSAL_IS_ERROR(st))
		goto spcerr;
	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl,
			     posix2fsal_type(stat->st_mode));
	return hdl;

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
static fsal_status_t lustre_lookup(struct fsal_obj_handle *parent,
				   const char *path,
				   struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc;
	struct stat stat;
	char *link_content = NULL;
	struct lustre_file_handle *dir_hdl = NULL;
	const char *sock_name = NULL;
	ssize_t retlink;
	char fidpath[MAXPATHLEN];
	char link_buff[PATH_MAX+1];
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));
	struct fsal_filesystem *fs;

	*handle = NULL;		/* poison it first */
	fs = parent->fs;

	if (!path)
		return fsalstat(ERR_FSAL_FAULT, 0);

	memset(fh, 0, sizeof(struct lustre_file_handle));
	parent_hdl =
	    container_of(parent, struct lustre_fsal_obj_handle, obj_handle);
	if (!parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	if (parent->fsal != parent->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 parent->fsal->name, parent->fs->fsal->name);
		rc = EXDEV;
		return fsalstat(posix2fsal_error(rc), rc);
	}

	rc = lustre_name_to_handle_at(fs->path,
					  parent_hdl->handle, path, fh, 0);
	if (rc < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto errout;
	}

	lustre_handle_to_path(fs->path, fh,
			      fidpath);

	rc = lstat(fidpath, &stat);
	if (rc < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto errout;
	}
	if (S_ISLNK(stat.st_mode)) {	/* I could lazy eval this... */
		retlink = readlink(fidpath, link_buff, PATH_MAX);
		if (retlink < 0 || retlink == PATH_MAX) {
			rc = errno;
			if (retlink == PATH_MAX)
				rc = ENAMETOOLONG;
			fsal_error = posix2fsal_error(rc);
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
	    alloc_handle(fh, fs, &stat, link_content, dir_hdl, sock_name,
			 op_ctx->fsal_export);
	if (hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL;	/* poison it */
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	return fsalstat(fsal_error, rc);
}

static inline int get_stat_by_handle_at(char *mntpath,
					struct lustre_file_handle *infh,
					const char *name,
					struct lustre_file_handle *fh,
					struct stat *stat)
{
	int rc;
	char lustre_path[MAXPATHLEN];
	char filepath[MAXPATHLEN];

	rc = lustre_handle_to_path(mntpath, infh, lustre_path);
	if (rc < 0) {
		rc = errno;
		goto fileerr;
	}
	snprintf(filepath, MAXPATHLEN, "%s/%s", lustre_path, name);

	/* now that it is owned properly, set accessible mode */

	rc = lustre_name_to_handle_at(mntpath, infh, name, fh, 0);
	if (rc < 0)
		goto fileerr;

	rc = lstat(filepath, stat);
	if (rc < 0) {
		rc = errno;
		goto fileerr;
	}

 fileerr:
	rc = errno;
	return rc;
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t lustre_create(struct fsal_obj_handle *dir_hdl,
				   const char *name, struct attrlist *attrib,
				   struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
	char newpath[MAXPATHLEN];
	char dirpath[MAXPATHLEN];
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
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
	    & ~op_ctx->fsal_export->ops->fs_umask(op_ctx->fsal_export);
	lustre_handle_to_path(dir_hdl->fs->path,
			      myself->handle, dirpath);

	rc = lstat(dirpath, &stat);
	if (rc < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto errout;
	}

	/* create it with no access because we are root when we do this
	 * we use openat because there is no creatat...
	 */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);
	fd = CRED_WRAP(op_ctx->creds, int, open, newpath,
		       O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, unix_mode);
	if (fd < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto errout;
	}
	close(fd);		/* not needed anymore */

	rc =
	    get_stat_by_handle_at(dir_hdl->fs->path,
				  myself->handle, name, fh, &stat);
	if (rc < 0)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs,
			   &stat, NULL, NULL, NULL, op_ctx->fsal_export);
	if (hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:
	fsal_error = posix2fsal_error(rc);
	unlink(newpath);	/* remove the evidence on errors */
 errout:
	return fsalstat(fsal_error, rc);
}

static fsal_status_t lustre_makedir(struct fsal_obj_handle *dir_hdl,
				    const char *name, struct attrlist *attrib,
				    struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
	char dirpath[MAXPATHLEN];
	char newpath[MAXPATHLEN];
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
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
	    & ~op_ctx->fsal_export->ops->fs_umask(op_ctx->fsal_export);
	lustre_handle_to_path(dir_hdl->fs->path,
			      myself->handle, dirpath);
	rc = lstat(dirpath, &stat);
	if (rc < 0) {
		rc = errno;
		goto direrr;
	}

	/* create it with no access because we are root when we do this */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);
	rc = CRED_WRAP(op_ctx->creds, int, mkdir, newpath, unix_mode);
	if (rc < 0) {
		rc = errno;
		goto direrr;
	}
	rc =
	    get_stat_by_handle_at(dir_hdl->fs->path,
				  myself->handle, name, fh, &stat);
	if (rc < 0)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs,
			   &stat, NULL, NULL, NULL, op_ctx->fsal_export);
	if (hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 direrr:
	rc = errno;
	fsal_error = posix2fsal_error(rc);

	return fsalstat(fsal_error, rc);

 fileerr:
	fsal_error = posix2fsal_error(rc);
	rmdir(newpath);		/* remove the evidence on errors */
 errout:
	return fsalstat(fsal_error, rc);
}

static fsal_status_t lustre_makenode(struct fsal_obj_handle *dir_hdl,
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
	int rc = 0;
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
	    & ~op_ctx->fsal_export->ops->fs_umask(op_ctx->fsal_export);
	switch (nodetype) {
	case BLOCK_FILE:
		if (!dev) {
			fsal_error = ERR_FSAL_FAULT;
			goto errout;
		}
		unix_dev =
		    CRED_WRAP(op_ctx->creds, dev_t, makedev, dev->major,
			      dev->minor);
		break;
	case CHARACTER_FILE:
		if (!dev) {
			fsal_error = ERR_FSAL_FAULT;
			goto errout;

		}
		unix_dev =
		    CRED_WRAP(op_ctx->creds, dev_t, makedev, dev->major,
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
	lustre_handle_to_path(dir_hdl->fs->path,
			      myself->handle, dirpath);
	rc = lstat(dirpath, &stat);
	if (rc < 0) {
		rc = errno;
		goto errout;
	}

	/* create it with no access because we are root when we do this */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);
	rc =
	    CRED_WRAP(op_ctx->creds, int, mknod, newpath, unix_mode, unix_dev);
	if (rc < 0) {
		rc = errno;
		goto direrr;
	}
	rc =
	    get_stat_by_handle_at(dir_hdl->fs->path,
				  myself->handle, name, fh, &stat);
	if (rc < 0)
		goto direrr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs,
			   &stat, NULL, dir_fh, sock_name,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto unlinkout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 direrr:
	fsal_error = posix2fsal_error(rc);
 unlinkout:
	unlink(newpath);
 errout:
	return fsalstat(fsal_error, rc);
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t lustre_makesymlink(struct fsal_obj_handle *dir_hdl,
					const char *name, const char *link_path,
					struct attrlist *attrib,
					struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
	char dirpath[MAXPATHLEN];
	char newpath[MAXPATHLEN];
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
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
	lustre_handle_to_path(dir_hdl->fs->path,
			      myself->handle, dirpath);
	rc = lstat(dirpath, &stat);
	if (rc < 0)
		goto direrr;
	if (stat.st_mode & S_ISGID) {
		/*setgid bit on dir propagates dir group owner */
		group = -1;
	}

	/* create it with no access because we are root when we do this */
	snprintf(newpath, MAXPATHLEN, "%s/%s", dirpath, name);

	rc = CRED_WRAP(op_ctx->creds, int, symlink, link_path, newpath);
	if (rc < 0)
		goto direrr;

	/* do this all by hand because we can't use fchmodat on symlinks...
	 */
	rc = lchown(newpath, user, group);
	if (rc < 0)
		goto linkerr;

	rc =
	    lustre_name_to_handle_at(dir_hdl->fs->path,
				     myself->handle, name, fh, 0);
	if (rc < 0)
		goto linkerr;

	/* now get attributes info, being careful
	 * to get the link, not the target */
	rc = lstat(newpath, &stat);
	if (rc < 0)
		goto linkerr;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, dir_hdl->fs,
			   &stat, link_path, NULL, NULL,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		rc = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 linkerr:
	rc = errno;
	unlink(newpath);
	goto errout;

 direrr:
	rc = errno;
 errout:
	if (rc == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(rc);
	return fsalstat(fsal_error, rc);
}

static fsal_status_t lustre_readsymlink(struct fsal_obj_handle *obj_hdl,
					struct gsh_buffdesc *link_content,
					bool refresh)
{
	struct lustre_fsal_obj_handle *myself = NULL;
	char mypath[MAXPATHLEN];
	int rc = 0;
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
		lustre_handle_to_path(obj_hdl->fs->path,
				      myself->handle, mypath);
		retlink = readlink(mypath, link_buff, PATH_MAX);
		 if (retlink < 0 || retlink == PATH_MAX) {
			rc = errno;
			if (retlink == PATH_MAX)
				rc = ENAMETOOLONG;
			fsal_error = posix2fsal_error(rc);
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
	return fsalstat(fsal_error, rc);
}

static fsal_status_t lustre_linkfile(struct fsal_obj_handle *obj_hdl,
				     struct fsal_obj_handle *destdir_hdl,
				     const char *name)
{
	struct lustre_fsal_obj_handle *myself, *destdir;
	char srcpath[MAXPATHLEN];
	char destdirpath[MAXPATHLEN];
	char destnamepath[MAXPATHLEN];
	int rc = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if (!op_ctx->fsal_export->ops->
	    fs_supports(op_ctx->fsal_export, fso_link_support)) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(obj_hdl->fs->path,
			      myself->handle, srcpath);

	destdir =
	    container_of(destdir_hdl, struct lustre_fsal_obj_handle,
			 obj_handle);
	lustre_handle_to_path(obj_hdl->fs->path,
			      destdir->handle, destdirpath);

	snprintf(destnamepath, MAXPATHLEN, "%s/%s", destdirpath, name);

	rc = CRED_WRAP(op_ctx->creds, int, link, srcpath, destnamepath);
	if (rc == -1) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
	}
 out:
	return fsalstat(fsal_error, rc);
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
					 fsal_cookie_t *whence,
					 void *dir_state, fsal_readdir_cb cb,
					 bool *eof)
{
	struct lustre_fsal_obj_handle *myself;
	int dirfd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
	off_t seekloc = 0;
	int bpos, cnt, nread;
	struct linux_dirent *dentry;
	char buf[BUF_SIZE];

	if (whence != NULL)
		seekloc = (off_t) *whence;

	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	dirfd =
	    lustre_open_by_handle(dir_hdl->fs->path,
				  myself->handle, (O_RDONLY | O_DIRECTORY));
	if (dirfd < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto out;
	}
	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if (seekloc < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto done;
	}
	cnt = 0;
	do {
		nread = syscall(SYS_getdents, dirfd, buf, BUF_SIZE);
		if (nread < 0) {
			rc = errno;
			fsal_error = posix2fsal_error(rc);
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
			if (!cb(dentry->d_name,
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
	return fsalstat(fsal_error, rc);
}

static fsal_status_t lustre_renamefile(struct fsal_obj_handle *olddir_hdl,
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
	int rc = 0;

	olddir =
	    container_of(olddir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(olddir_hdl->fs->path,
			      olddir->handle, olddirpath);
	snprintf(oldnamepath, MAXPATHLEN, "%s/%s", olddirpath, old_name);

	newdir =
	    container_of(newdir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(olddir_hdl->fs->path,
			      newdir->handle, newdirpath);
	snprintf(newnamepath, MAXPATHLEN, "%s/%s", newdirpath, new_name);

	rc = CRED_WRAP(op_ctx->creds, int, rename, oldnamepath, newnamepath);
	if (rc < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
	}
	return fsalstat(fsal_error, rc);
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t lustre_getattrs(struct fsal_obj_handle *obj_hdl)
{
	struct lustre_fsal_obj_handle *myself;
	char mypath[MAXPATHLEN];
	int open_flags = O_RDONLY;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t st;
	int rc = 0;

	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	if (obj_hdl->type == SOCKET_FILE) {
		lustre_handle_to_path(obj_hdl->fs->path,
				      myself->u.sock.sock_dir,
				      mypath);
	} else {
		if (obj_hdl->type == SYMBOLIC_LINK)
			open_flags |= O_PATH;
		else if (obj_hdl->type == FIFO_FILE)
			open_flags |= O_NONBLOCK;

		lustre_handle_to_path(obj_hdl->fs->path,
				      myself->handle, mypath);
	}

	rc = lstat(mypath, &stat);
	if (rc < 0)
		goto errout;

	/* convert attributes */
	st = posix2fsal_attributes(&stat, &obj_hdl->attributes);
	if (FSAL_IS_ERROR(st)) {
		FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
		FSAL_SET_MASK(obj_hdl->attributes.mask, ATTR_RDATTR_ERR);
		fsal_error = st.major;
		rc = st.minor;
		goto out;
	}
	goto out;

 errout:
	rc = errno;
	if (rc == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(rc);
 out:
	return fsalstat(fsal_error, rc);
}

/*
 * NOTE: this is done under protection of
 * the attributes rwlock in the cache entry.
 */

static fsal_status_t lustre_setattrs(struct fsal_obj_handle *obj_hdl,
				     struct attrlist *attrs)
{
	struct lustre_fsal_obj_handle *myself;
	char mypath[MAXPATHLEN];
	char mysockpath[MAXPATHLEN];
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
	int trunc_done = 0;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
		attrs->mode &= ~op_ctx->fsal_export->ops->
				fs_umask(op_ctx->fsal_export);
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
		lustre_handle_to_path(obj_hdl->fs->path,
				      myself->u.sock.sock_dir,
				      mypath);
		rc = lstat(mypath, &stat);
	} else {
		lustre_handle_to_path(obj_hdl->fs->path,
				      myself->handle,
				      mypath);
		rc = lstat(mypath, &stat);
	}
	if (rc < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto out;
	}


	/** TRUNCATE **/
	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			fsal_error = ERR_FSAL_INVAL;
			goto out;
		}
#ifdef USE_FSAL_SHOOK
		/* Do Shook Magic */
		fsal_status_t st;
		st = lustre_shook_restore(obj_hdl,
					 (attrs->filesize == 0),
					 &trunc_done);
		if (FSAL_IS_ERROR(st))
			return st;
#endif
		if (!trunc_done) {
			rc = CRED_WRAP(op_ctx->creds, int, truncate, mypath,
					  attrs->filesize);
			if (rc != 0)
				goto fileerr;
		}
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
				rc =
				    chmod(mysockpath,
					  fsal2unix_mode(attrs->mode));
			} else
				rc =
				    chmod(mypath, fsal2unix_mode(attrs->mode));

			if (rc != 0)
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
			rc = lchown(mysockpath, user, group);
		} else
			rc = lchown(mypath, user, group);

		if (rc)
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
			rc = utimes(mysockpath, timebuf);
		} else if (obj_hdl->type == SYMBOLIC_LINK) {
			/* Setting utimes on a SYMLINK is illegal. Do nothing */
			rc = 0;
		} else
			rc = utimes(mypath, ptimebuf);
		if (rc != 0)
			goto fileerr;
	}
	return fsalstat(fsal_error, rc);

 fileerr:
	rc = errno;
	fsal_error = posix2fsal_error(rc);
 out:
	return fsalstat(fsal_error, rc);
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t lustre_file_unlink(struct fsal_obj_handle *dir_hdl,
					const char *name)
{
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	char dirpath[MAXPATHLEN];
	char filepath[MAXPATHLEN];
	struct stat stat;
	int rc = 0;

	myself =
	    container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	lustre_handle_to_path(dir_hdl->fs->path,
			      myself->handle, dirpath);
	snprintf(filepath, MAXPATHLEN, "%s/%s", dirpath, name);
	rc = lstat(filepath, &stat);
	if (rc < 0) {
		rc = errno;
		if (rc == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(rc);
		goto out;
	}
	if (S_ISDIR(stat.st_mode))
		rc = CRED_WRAP(op_ctx->creds, int, rmdir, filepath);
	else
		rc = CRED_WRAP(op_ctx->creds, int, unlink, filepath);
	if (rc < 0) {
		rc = errno;
		if (rc == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(rc);
	}

 out:
	return fsalstat(fsal_error, rc);
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

static void release(struct fsal_obj_handle *obj_hdl)
{
	struct lustre_fsal_obj_handle *myself;
	object_file_type_t type = obj_hdl->type;

	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	if (type == REGULAR_FILE
	    && (myself->u.file.fd >= 0
		|| myself->u.file.openflags != FSAL_O_CLOSED)) {
		fsal_status_t status;
		status = lustre_close(obj_hdl);
		if (FSAL_IS_ERROR(status)) {
			LogCrit(COMPONENT_FSAL,
				"Error in closing fd was %s(%d)",
				strerror(status.minor),
				status.minor);
		}
	}

	fsal_obj_handle_uninit(obj_hdl);

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
				 const char *path,
				 struct fsal_obj_handle **handle)
{
	int dir_fd;
	struct stat stat;
	struct lustre_fsal_obj_handle *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct fsal_filesystem *fs = NULL;
	struct fsal_dev__ dev;
	int rc = 0;
	struct lustre_file_handle *fh =
	    alloca(sizeof(struct lustre_file_handle));

	memset(fh, 0, sizeof(struct lustre_file_handle));

	*handle = NULL;	/* poison it */

	/* Use open_dir_by_path_walk to validate path and stat the final
	 * directory.
	 */
	dir_fd = open_dir_by_path_walk(-1, path, &stat);
	if (dir_fd < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not open directory for path %s",
			path);
		rc = -dir_fd;
		goto errout;
	}
	close(dir_fd);

	dev = posix2fsal_devt(stat.st_dev);
	fs = lookup_dev(&dev);
	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find file system for path %s",
			path);
		rc = ENOENT;
		goto errout;
	}


	if (fs->fsal != exp_hdl->fsal) {
		LogInfo(COMPONENT_FSAL,
		"File system for path %s did not belong to FSAL %s",
		path, exp_hdl->fsal->name);
		rc = EACCES;
		goto errout;
	}

	LogDebug(COMPONENT_FSAL,
		"filesystem %s for path %s",
		fs->path, path);

	/* Get a lustre handle for the requested path */
	rc = lustre_path_to_handle(path, fh);
	if (rc < 0) {
		rc = errno;
		goto errout;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, fs, &stat, NULL, NULL, NULL, exp_hdl);
	if (hdl == NULL) {
		rc = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	fsal_error = posix2fsal_error(rc);
	return fsalstat(fsal_error, rc);
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
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *hdl;
	struct stat stat;
	struct lustre_file_handle *fh;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
	char objpath[MAXPATHLEN];
	char *link_content = NULL;
	ssize_t retlink;
	char link_buff[PATH_MAX+1];
	struct fsal_fsid__ fsid;
	enum fsid_type fsid_type;
	struct fsal_filesystem *fs;

	*handle = NULL;		/* poison it first */
	if (hdl_desc->len > sizeof(struct lustre_file_handle))
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh,
		hdl_desc->addr,
		hdl_desc->len);	/* struct aligned copy */

	rc = lustre_extract_fsid(fh, &fsid_type, &fsid);
	if (rc == 0) {
		fs = lookup_fsid(&fsid, fsid_type);
		if (fs == NULL) {
			LogInfo(COMPONENT_FSAL,
				"Could not map "
				"fsid=0x%016"PRIx64".0x%016"PRIx64
				" to filesytem",
				fsid.major, fsid.minor);
			rc = ESTALE;
			return fsalstat(posix2fsal_error(rc), rc);
		}
		if (fs->fsal != exp_hdl->fsal) {
			LogInfo(COMPONENT_FSAL,
				"fsid=0x%016"PRIx64".0x%016"PRIx64
				" in handle not a %s filesystem",
				fsid.major, fsid.minor,
				exp_hdl->fsal->name);
			rc = ESTALE;
			return fsalstat(posix2fsal_error(rc), rc);
		}

		LogDebug(COMPONENT_FSAL,
			 "Found filesystem %s for handle for FSAL %s",
			 fs->path,
			 fs->fsal != NULL ? fs->fsal->name : "(none)");
	} else {
				LogDebug(COMPONENT_FSAL,
			 "Could not map handle to fsid");
		fsal_error = ERR_FSAL_BADHANDLE;
		return fsalstat(posix2fsal_error(fsal_error), fsal_error);
	}

	lustre_handle_to_path(fs->path, fh, objpath);
	rc = lstat(objpath, &stat);
	if (rc < 0) {
		rc = errno;
		fsal_error = posix2fsal_error(rc);
		goto errout;
	}

	/* think about managing symlinks correctly */
	if (S_ISLNK(stat.st_mode)) {	/* I could lazy eval this... */
		retlink = readlink(objpath, link_buff, PATH_MAX);
		if (retlink < 0 || retlink == PATH_MAX) {
			rc = errno;
			if (retlink == PATH_MAX)
				rc = ENAMETOOLONG;
			fsal_error = posix2fsal_error(rc);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = &link_buff[0];
	}

	hdl = alloc_handle(fh, fs, &stat,
			   link_content, NULL, NULL, exp_hdl);
	if (hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;

 errout:
	return fsalstat(fsal_error, rc);
}
