/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* handle.c
 * XFS object (file|dir) handle object
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_handle_syscalls.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include <xfs/xfs.h>
#include <xfs/handle.h>
#include "nlm_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "xfs_fsal.h"

/* defined by libhandle but no prototype in xfs/handle.h */

int fd_to_handle(int fd, void **hanp, size_t *hlen);

/* The code that follows is intended to fake a XFS handle from
 * the bulkstat data.
 * It may not be portable
 * I keep it for wanting of a better solution */

#define XFS_FSHANDLE_SZ             8
struct xfs_fshandle {
	char fsh_space[XFS_FSHANDLE_SZ];
};

/* private file handle - for use by open_by_fshandle */
#define XFS_FILEHANDLE_SZ           24
#define XFS_FILEHANDLE_SZ_FOLLOWING 14
#define XFS_FILEHANDLE_SZ_PAD       2
struct xfs_filehandle {
	struct xfs_fshandle fh_fshdl; /* handle of fs containing this inode */
	int16_t fh_sz_following;      /* bytes in handle after this member */
	char fh_pad[XFS_FILEHANDLE_SZ_PAD];   /* padding, must be zeroed */
	__uint32_t fh_gen;            /* generation count */
	xfs_ino_t fh_ino;             /* 64 bit ino */
};

static int
xfs_fsal_bulkstat_inode(int fd, xfs_ino_t ino, xfs_bstat_t *bstat)
{
	xfs_fsop_bulkreq_t bulkreq;
	__u64 i = ino;

	bulkreq.lastip = &i;
	bulkreq.icount = 1;
	bulkreq.ubuffer = bstat;
	bulkreq.ocount = NULL;
	return ioctl(fd, XFS_IOC_FSBULKSTAT_SINGLE, &bulkreq);
}

static int
xfs_fsal_inode2handle(const struct fsal_export *export,
                      ino_t ino,
		      struct gsh_buffdesc *handle)
{
	int fd = 0;
	xfs_bstat_t bstat;
	struct xfs_filehandle *hdl;
	const struct xfs_fsal_export *exp
		= container_of(export, struct xfs_fsal_export, export);

	fd = open(exp->mntdir, O_DIRECTORY);
	if(fd < 0)
		return -errno;
  
	if(xfs_fsal_bulkstat_inode(fd, ino, &bstat) < 0) {
		int rv = errno;
		close(fd);
		return -rv;
	}
	close(fd);

	hdl = gsh_calloc(1, sizeof(*hdl) + handle->len);
	if(hdl == NULL)
		return -ENOMEM;

	memcpy(&hdl->fh_fshdl, exp->root_handle->data, sizeof(hdl->fh_fshdl));
	hdl->fh_sz_following = XFS_FILEHANDLE_SZ_FOLLOWING;
	hdl->fh_gen = bstat.bs_gen;
	hdl->fh_ino = bstat.bs_ino;

	handle->addr = hdl;
	handle->len = sizeof(*hdl);
	return 0;
}

static struct xfs_fsal_obj_handle *
alloc_handle(const struct gsh_buffdesc *fh,
             struct stat *stat,
             struct fsal_export *exp_hdl)
{
	struct xfs_fsal_obj_handle *hdl;
	fsal_status_t st;

	assert(fh->len < 255);

	hdl = gsh_calloc(1, sizeof(*hdl) + fh->len);
	if(hdl == NULL) {
		return NULL;
	}
	
	hdl->xfs_hdl.len = fh->len;
	memcpy(hdl->xfs_hdl.data, fh->addr, fh->len);
	hdl->xfs_hdl.inode = stat->st_ino;
	hdl->xfs_hdl.type = hdl->obj_handle.type = 
		posix2fsal_type(stat->st_mode);
	if(hdl->obj_handle.type == REGULAR_FILE) {
		hdl->fd = -1;  /* no open on this yet */
		hdl->openflags = FSAL_O_CLOSED;
	}
	hdl->obj_handle.export = exp_hdl;
	hdl->obj_handle.attributes.mask
		= exp_hdl->ops->fs_supported_attrs(exp_hdl);
	hdl->obj_handle.attributes.supported_attributes
                = hdl->obj_handle.attributes.mask;
	st = posix2fsal_attributes(stat, &hdl->obj_handle.attributes);
	if(!(FSAL_IS_ERROR(st) ||
	     fsal_obj_handle_init(&hdl->obj_handle, 
				  exp_hdl, hdl->obj_handle.type)))
                return hdl;

	hdl->obj_handle.ops = NULL;
	pthread_mutex_unlock(&hdl->obj_handle.lock);
	pthread_mutex_destroy(&hdl->obj_handle.lock);
	gsh_free(hdl);
	return NULL;
}

/* handle methods */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t
xfs_lookup(struct fsal_obj_handle *parent,
	   const struct req_op_context *opctx,
	   const char *path,
	   struct fsal_obj_handle **handle)
{
	struct xfs_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval, dirfd;
	struct stat stat;
	struct gsh_buffdesc fh = {NULL, 0};

	*handle = NULL; /* poison it first */
	if( !path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	parent_hdl = container_of(parent, struct xfs_fsal_obj_handle, obj_handle);
	if( !parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	dirfd = open_by_handle(parent_hdl->xfs_hdl.data,
			       parent_hdl->xfs_hdl.len, O_DIRECTORY);
	if(dirfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		return fsalstat(fsal_error, retval);	
	}
	retval = fstatat(dirfd, path, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}

	if(S_ISDIR(stat.st_mode) ||  S_ISREG(stat.st_mode)) {
		int tmpfd = openat(dirfd, path, O_RDONLY | O_NOFOLLOW, 0600);
		if(tmpfd < 0) {
			retval = errno;
			goto direrr;
		}
  		if(fd_to_handle(tmpfd, &fh.addr, &fh.len) < 0) {
			retval = errno;
			close(tmpfd);
			goto direrr;
		}
		close(tmpfd);
	} else {
		retval = xfs_fsal_inode2handle(parent->export, stat.st_ino, &fh);
		if(retval < 0) {
			retval = -retval;
			goto direrr;
		}
	}
	close(dirfd);

	/* allocate an obj_handle and fill it up. */
	hdl = alloc_handle(&fh, &stat, parent->export);
	free_handle(fh.addr, fh.len);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto hdlerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

direrr:
	close(dirfd);
hdlerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

/* make_file_safe
 * the file/dir got created mode 0, uid root (me)
 * which leaves it inaccessible. Set ownership first
 * followed by mode.
 * could use setfsuid/gid around the mkdir/mknod/openat
 * but that only works on Linux and is more syscalls
 * 5 (set uid/gid, create, unset uid/gid) vs. 3
 * NOTE: this way escapes quotas however we do check quotas
 * first in cache_inode_*
 */

static int
make_file_safe(int fd,
	       mode_t unix_mode,
	       uid_t user,
	       gid_t group,
	       struct gsh_buffdesc *fh,
	       struct stat *stat)
{
	int retval;
	
	retval = fchown(fd, user, group);
	if(retval < 0) {
		goto fileerr;
	}
	/* now that it is owned properly, set accessible mode */
	retval = fchmod(fd, unix_mode);
	if(retval < 0) {
		goto fileerr;
	}
	retval = fd_to_handle(fd, &fh->addr, &fh->len);
	if(retval < 0) {
		goto fileerr;
	}
	retval = fstat(fd, stat);
	if(!retval) {
		return 0;
	}

	free_handle(fh->addr, fh->len);
fileerr:
	retval = errno;
	return retval;
}
/* create
 * create a regular file and set its attributes
 */

static fsal_status_t 
xfs_create(struct fsal_obj_handle *dir_hdl,
           const struct req_op_context *opctx,
           const char *name,
           struct attrlist *attrib,
           struct fsal_obj_handle **handle)
{
	struct xfs_fsal_obj_handle *myself, *hdl;
	int fd, dir_fd;
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	struct gsh_buffdesc fh;

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	dir_fd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
				O_DIRECTORY);
	if(dir_fd < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		return fsalstat(fsal_error, retval);	
	}
	retval = fstat(dir_fd, &stat);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */

	/* create it with no access because we are root when we do this
	 * we use openat because there is no creatat...
	 */
	fd = openat(dir_fd, name, O_CREAT|O_WRONLY|O_TRUNC|O_EXCL, 0000);
	if(fd < 0) {
		retval = errno;
		goto direrr;
	}

	retval = make_file_safe(fd, unix_mode, user, group, &fh, &stat);
	if(retval != 0) {
		goto fileerr;
	}
	close(dir_fd); /* done with parent */

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, dir_hdl->export);
	free_handle(fh.addr, fh.len);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	close(fd);  /* don't need it anymore. */
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	close(fd);
	unlinkat(dir_fd, name, 0);  /* remove the evidence on errors */

direrr:
	fsal_error = posix2fsal_error(retval);
	close(dir_fd);
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t 
xfs_makedir(struct fsal_obj_handle *dir_hdl,
            const struct req_op_context *opctx,
	    const char *name,
	    struct attrlist *attrib,
	    struct fsal_obj_handle **handle)
{
	struct xfs_fsal_obj_handle *myself, *hdl;
	int dir_fd, newfd;
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	struct gsh_buffdesc fh;

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	dir_fd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
				O_DIRECTORY);
	if(dir_fd < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		return fsalstat(fsal_error, retval);	
	}
	retval = fstat(dir_fd, &stat);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */

	/* create it with no access because we are root when we do this */
	retval = mkdirat(dir_fd, name, 0000);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}

	if((newfd = openat(dir_fd, name, O_DIRECTORY, 0)) < 0) {
		retval = errno;
		goto fileerr;
	}

	retval = make_file_safe(newfd, unix_mode, user, group, &fh, &stat);
	if(retval != 0) {
		close(newfd);
		goto fileerr;
	}
	close(dir_fd);
	close(newfd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, dir_hdl->export);
	free_handle(fh.addr, fh.len);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
fileerr:
	unlinkat(dir_fd, name, AT_REMOVEDIR);  /* remove the evidence on errors */

direrr:
	fsal_error = posix2fsal_error(retval);
	close(dir_fd);
	return fsalstat(fsal_error, retval);	
}

static int
make_node_safe(int dir_fd,
	       const struct fsal_export *exp,
	       const char *name,
               mode_t unix_mode,
               uid_t user,
               gid_t group,
               struct gsh_buffdesc *fh,
               struct stat *stat)
{
	int retval;
	
	retval = fchownat(dir_fd, name,
			  user, group, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}

	/* now that it is owned properly, set accessible mode */
	
	retval = fchmodat(dir_fd, name, unix_mode, 0);
	if(retval < 0) {
		goto fileerr;
	}
	retval = fstatat(dir_fd, name, stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}
	retval = xfs_fsal_inode2handle(exp, stat->st_ino, fh);
	if(retval == 0) {
		return 0;
	}

fileerr:
	retval = errno;
	return retval;
}
static fsal_status_t 
xfs_makenode(struct fsal_obj_handle *dir_hdl,
             const struct req_op_context *opctx,
             const char *name,
             object_file_type_t nodetype,  /* IN */
             fsal_dev_t *dev,  /* IN */
             struct attrlist *attrib,
             struct fsal_obj_handle **handle)
{
	struct xfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	mode_t unix_mode, create_mode = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	dev_t unix_dev = 0;
	struct gsh_buffdesc fh;

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);


		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	switch (nodetype) {
        case BLOCK_FILE:
                if( !dev) {
                        fsal_error = ERR_FSAL_FAULT;
                        goto errout;
                }
                create_mode = S_IFBLK;
                unix_dev = makedev(dev->major, dev->minor);
                break;
        case CHARACTER_FILE:
                if( !dev) {
                        fsal_error = ERR_FSAL_FAULT;
                        goto errout;
                }
                create_mode = S_IFCHR;
                unix_dev = makedev(dev->major, dev->minor);
                break;
        case FIFO_FILE:
                create_mode = S_IFIFO;
                break;
        case SOCKET_FILE:
                create_mode = S_IFSOCK;
                break;
        default:
                LogMajor(COMPONENT_FSAL,
                         "Invalid node type in FSAL_mknode: %d",
                         nodetype);
                fsal_error = ERR_FSAL_INVAL;
                goto errout;
        }
	dir_fd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
				O_DIRECTORY);
	if(dir_fd < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = fstat(dir_fd, &stat);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */

	/* create it with no access because we are root when we do this */
	retval = mknodat(dir_fd, name, create_mode, unix_dev);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	retval = make_node_safe(dir_fd, dir_hdl->export, name, unix_mode,
				user, group, &fh, &stat);
	if(retval != 0) {
		goto nodeerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, dir_hdl->export);
	gsh_free(fh.addr);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto nodeerr;
	}
	close(dir_fd); /* done with parent */
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
nodeerr:
	unlinkat(dir_fd, name, 0);
	
direrr:
	fsal_error = posix2fsal_error(retval);
	close(dir_fd); /* done with parent */

errout:
	return fsalstat(fsal_error, retval);	
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t 
xfs_makesymlink(struct fsal_obj_handle *dir_hdl,
                const struct req_op_context *opctx,
                const char *name,
                const char *link_path,
                struct attrlist *attrib,
                struct fsal_obj_handle **handle)
{
	struct xfs_fsal_obj_handle *parent, *hdl;
	int dir_fd = -1;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	struct gsh_buffdesc fh;

	*handle = NULL; /* poison it first */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	user = attrib->owner;
	group = attrib->group;
	parent = container_of(dir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	dir_fd = open_by_handle(parent->xfs_hdl.data, parent->xfs_hdl.len,
				O_DIRECTORY);
	if(dir_fd < 0) {
		retval = errno;
		goto errout;
	}
	retval = fstatat(dir_fd, "", &stat, AT_EMPTY_PATH);
	if(retval < 0) {
		goto direrr;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */
	
	/* create it with no access because we are root when we do this */
	retval = symlinkat(link_path, dir_fd, name);
	if(retval < 0) {
		goto direrr;
	}
	/* do this all by hand because we can't use fchmodat on symlinks...
	 */
	retval = fchownat(dir_fd, name, user, group, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto linkerr;
	}
	/* now get attributes info, being careful to get the link, not 
	 * the target */
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto linkerr;
	}
	retval = xfs_fsal_inode2handle(dir_hdl->export, stat.st_ino, &fh);
	if(retval < 0) {
		goto linkerr;
	}
	close(dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, dir_hdl->export);
	gsh_free(fh.addr);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

linkerr:
	retval = errno;
	unlinkat(dir_fd, name, 0);
	close(dir_fd);
	goto errout;

direrr:
	retval = errno;
	close(dir_fd);
errout:
	if(retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t
xfs_readsymlink(struct fsal_obj_handle *obj_hdl,
                const struct req_op_context *opctx,
                char *link_content,
                size_t *link_len,
                bool refresh)
{
	struct xfs_fsal_obj_handle *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if(obj_hdl->type != SYMBOLIC_LINK) {
		retval = EBADF;
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);

	retval = readlink_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
				    link_content, *link_len);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	link_content[retval] = '\0';
	*link_len = retval;
	retval = 0;
out:
	return fsalstat(fsal_error, retval);	
}

/* FIXME: Consider playing tricks with saving paths to
 * symlinks and such (similar to VFS) to allow them to be used
 * to create 'source' for linkat(2).
 */
static fsal_status_t 
xfs_linkfile(struct fsal_obj_handle *obj_hdl,
             const struct req_op_context *opctx,
	     struct fsal_obj_handle *destdir_hdl,
	     const char *name)
{
	struct xfs_fsal_obj_handle *myself, *destdir;
	int srcfd, destdirfd;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if( !obj_hdl->export->ops->fs_supports(obj_hdl->export,
					       fso_link_support)) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	if(obj_hdl->type != REGULAR_FILE) {
		fsal_error = ERR_FSAL_INVAL;
		retval = EINVAL;
		goto out;
	}
	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);
	srcfd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
			       O_RDONLY);
	if(srcfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	destdir = container_of(destdir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	destdirfd = open_by_handle(destdir->xfs_hdl.data, destdir->xfs_hdl.len,
				   O_DIRECTORY);
	if(destdirfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto fileerr;
	}
	retval = linkat(srcfd, "", destdirfd, name, AT_EMPTY_PATH);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	close(destdirfd);

fileerr:
	close(srcfd);
out:
	return fsalstat(fsal_error, retval);	
}

/* not defined in linux headers so we do it here
 */

struct linux_dirent {
	unsigned long  d_ino;     /* Inode number */
	unsigned long  d_off;     /* Offset to next linux_dirent */
	unsigned short d_reclen;  /* Length of this linux_dirent */
	char           d_name[];  /* Filename (null-terminated) */
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
 * xfs_read_dirents
 * read the directory and call through the callback function for
 * each entry.
 * @param dir_hdl [IN] the directory to read
 * @param entry_cnt [IN] limit of entries. 0 implies no limit
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker true == end of dir
 */

static fsal_status_t
xfs_read_dirents(struct fsal_obj_handle *dir_hdl,
                 const struct req_op_context *opctx,
                 struct fsal_cookie *whence,
                 void *dir_state,
                 fsal_readdir_cb cb,
                 bool *eof)
{
        struct xfs_fsal_obj_handle *myself;
        int dirfd;
        fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        int retval = 0;
        off_t seekloc = 0;
        int bpos, cnt, nread;
        struct linux_dirent *dentry;
        struct fsal_cookie *entry_cookie;
        char buf[BUF_SIZE];

        if(whence != NULL) {
                if(whence->size != sizeof(off_t)) {
                        fsal_error = posix2fsal_error(EINVAL);
                        retval = errno;
                        goto out;
                }
                memcpy(&seekloc, whence->cookie, sizeof(off_t));
        }
	entry_cookie = alloca(sizeof(struct fsal_cookie) + sizeof(off_t));
	myself = container_of(dir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	dirfd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
			       O_DIRECTORY);
	if(dirfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if(seekloc < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto done;
	}
	cnt = 0;
	do {
		nread = syscall(SYS_getdents, dirfd, buf, BUF_SIZE);
		if(nread < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto done;
		}
		if(nread == 0)
			break;
		for(bpos = 0; bpos < nread;) {
			dentry = (struct linux_dirent *)(buf + bpos);
			if(strcmp(dentry->d_name, ".") == 0 ||
			   strcmp(dentry->d_name, "..") == 0)
				goto skip; /* must skip '.' and '..' */
			entry_cookie->size = sizeof(off_t);
			memcpy(&entry_cookie->cookie, &dentry->d_off, sizeof(off_t));

                        /* callback to cache inode */
                        if (!cb(opctx,
                                dentry->d_name,
                                dir_state,
                                entry_cookie)) {
                                goto done;
                        }
		skip:
			bpos += dentry->d_reclen;
			cnt++;
		}
	} while(nread > 0);

	*eof = nread == 0 ? true : false;
done:
	close(dirfd);
	
out:
	return fsalstat(fsal_error, retval);	
}


static fsal_status_t
xfs_renamefile(struct fsal_obj_handle *olddir_hdl,
               const struct req_op_context *opctx,
	       const char *old_name,
	       struct fsal_obj_handle *newdir_hdl,
	       const char *new_name)
{
	struct xfs_fsal_obj_handle *olddir, *newdir;
	int oldfd, newfd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	olddir = container_of(olddir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	oldfd = open_by_handle(olddir->xfs_hdl.data, olddir->xfs_hdl.len,
			       O_DIRECTORY);
	if(oldfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	newdir = container_of(newdir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	newfd = open_by_handle(newdir->xfs_hdl.data, newdir->xfs_hdl.len,
			       O_DIRECTORY);
	if(newfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		close(oldfd);
		goto out;
	}
	retval = renameat(oldfd, old_name, newfd, new_name);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	close(oldfd);
	close(newfd);
out:
	return fsalstat(fsal_error, retval);	
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t 
xfs_getattrs(struct fsal_obj_handle *obj_hdl,
             const struct req_op_context *opctx)
{
	struct xfs_fsal_obj_handle *myself;
	int fd = -1;
	int open_flags = O_RDONLY;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t st;
	int retval = 0;

	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);
	switch(obj_hdl->type) {
	case REGULAR_FILE:
		open_flags = O_RDONLY;
		break;
	case DIRECTORY:
		open_flags = O_DIRECTORY;
		break;
	default:
                fsal_error = posix2fsal_error(EINVAL);
		retval = EINVAL;
		goto errout;
	}

	fd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
		            open_flags);
	if(fd < 0) {
		goto errout;
	}
	retval = fstat(fd, &stat);
	if(retval < 0) {
		goto errout;
	} 
	st = posix2fsal_attributes(&stat, &obj_hdl->attributes);
	if(FSAL_IS_ERROR(st)) {
                FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
		FSAL_SET_MASK(obj_hdl->attributes.mask,
                              ATTR_RDATTR_ERR);
		fsal_error = st.major;  retval = st.minor;
		goto out;
	}
	goto out;

errout:
        retval = errno;
        if(retval == ENOENT)
                fsal_error = ERR_FSAL_STALE;
        else
                fsal_error = posix2fsal_error(retval);
out:
	if(fd >= 0)
		close(fd);
	return fsalstat(fsal_error, retval);	
}

/*
 * NOTE: this is done under protection of the attributes rwlock in the cache entry.
 */

static fsal_status_t 
xfs_setattrs(struct fsal_obj_handle *obj_hdl,
             const struct req_op_context *opctx,
	     struct attrlist *attrs)
{
	struct xfs_fsal_obj_handle *myself;
	int fd = -1;
	int open_flags = O_RDONLY;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	/* apply umask, if mode attribute is to be changed */
	if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		attrs->mode
			&= ~obj_hdl->export->ops->fs_umask(obj_hdl->export);
	}
	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);
	switch(obj_hdl->type) {
	case REGULAR_FILE:
		open_flags = O_RDONLY;
		break;
	case DIRECTORY:
		open_flags = O_DIRECTORY;
		break;
	default:
                fsal_error = posix2fsal_error(EINVAL);
		retval = EINVAL;
		goto errout;
	}

	fd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
		            open_flags);
	if(fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	if (fstat(fd, &stat) < 0)
		goto fileerr;

	/** CHMOD **/
	if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		/* The POSIX chmod call doesn't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if(obj_hdl->type != SYMBOLIC_LINK) {
			retval = fchmod(fd, fsal2unix_mode(attrs->mode));

			if(retval != 0) {
				goto fileerr;
			}
		}
	}
		
	/**  CHOWN  **/
	if(FSAL_TEST_MASK(attrs->mask,
			  ATTR_OWNER | ATTR_GROUP)) {
		uid_t user = FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)
                        ? (int)attrs->owner : -1;
		gid_t group = FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)
                        ? (int)attrs->group : -1;

		retval = fchown(fd, user, group);
		if(retval) {
			goto fileerr;
		}
	}
		
	/**  UTIME  **/
	if(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME | ATTR_MTIME)) {
		struct timeval timebuf[2];

		/* Atime */
		timebuf[0].tv_sec =
			(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME) ?
                         (time_t) attrs->atime.seconds : stat.st_atime);
		timebuf[0].tv_usec = 0;

		/* Mtime */
		timebuf[1].tv_sec =
			(FSAL_TEST_MASK(attrs->mask, ATTR_MTIME) ?
			 (time_t) attrs->mtime.seconds : stat.st_mtime);
		timebuf[1].tv_usec = 0;
		retval = futimes(fd, timebuf);
		if(retval != 0) {
			goto fileerr;
		}
	}
	goto out;

fileerr:
        retval = errno;
        fsal_error = posix2fsal_error(retval);
out:
	close(fd);
errout:
	return fsalstat(fsal_error, retval);
}

/* compare
 * compare two handles.
 * return true for equal, false for anything else
 */
static bool compare(struct fsal_obj_handle *obj_hdl,
                      struct fsal_obj_handle *other_hdl)
{
	struct xfs_fsal_obj_handle *myself, *other;

	if( !other_hdl)
		return false;
	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);
	other = container_of(other_hdl, struct xfs_fsal_obj_handle, obj_handle);
	if((obj_hdl->type != other_hdl->type) ||
	   (myself->xfs_hdl.len != other->xfs_hdl.len))
		return false;
	return memcmp(myself->xfs_hdl.data, other->xfs_hdl.data,
		      myself->xfs_hdl.len) ? false : true;
}

/* file_truncate
 * truncate a file to the size specified.
 * size should really be off_t...
 */

static fsal_status_t 
xfs_file_truncate(struct fsal_obj_handle *obj_hdl,
                  const struct req_op_context *opctx,
		  uint64_t length)
{
	struct xfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int fd;
	int retval = 0;

	if(obj_hdl->type != REGULAR_FILE) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);
	fd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len, O_RDWR);
	if(fd < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = ftruncate(fd, length);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	close(fd);
	
errout:
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t 
xfs_unlink(struct fsal_obj_handle *dir_hdl,
           const struct req_op_context *opctx,
	   const char *name)
{
	struct xfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct stat stat;
	int fd;
	int retval = 0;

	myself = container_of(dir_hdl, struct xfs_fsal_obj_handle, obj_handle);
	fd = open_by_handle(myself->xfs_hdl.data, myself->xfs_hdl.len,
			    O_DIRECTORY);
	if(fd < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto out;
	}
	retval = fstatat(fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = unlinkat(fd, name,
			  (S_ISDIR(stat.st_mode)) ? AT_REMOVEDIR : 0);
	if(retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
	}
	
errout:
	close(fd);
out:
	return fsalstat(fsal_error, retval);	
}


/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t handle_digest(struct fsal_obj_handle *obj_hdl,
                                   fsal_digesttype_t output_type,
                                   struct gsh_buffdesc *fh_desc)
{
	struct xfs_fsal_obj_handle *myself;
	size_t fh_size;

	/* sanity checks */
        if( !fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);

	switch(output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = xfs_sizeof_handle(&myself->xfs_hdl);
                if(fh_desc->len < fh_size)
                        goto errout;
                memcpy(fh_desc->addr, &myself->xfs_hdl, fh_size);
		break;
	case FSAL_DIGEST_FILEID3:
		fh_size = FSAL_DIGEST_SIZE_FILEID3;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, &myself->xfs_hdl.inode, fh_size);
		break;
	case FSAL_DIGEST_FILEID4:
		fh_size = FSAL_DIGEST_SIZE_FILEID4;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, &myself->xfs_hdl.inode, fh_size);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

errout:
	LogMajor(COMPONENT_FSAL,
		 "Space too small for handle.  need %lu, have %lu",
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
	struct xfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);
	fh_desc->addr = &myself->xfs_hdl;
	fh_desc->len = xfs_sizeof_handle(&myself->xfs_hdl);
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct fsal_export *exp = obj_hdl->export;
	struct xfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if(obj_hdl->type == REGULAR_FILE) {
		fsal_status_t st = xfs_close(obj_hdl);
		if(FSAL_IS_ERROR(st))
			return st;
	}
	myself = container_of(obj_hdl, struct xfs_fsal_obj_handle, obj_handle);
	pthread_mutex_lock(&obj_hdl->lock);
	obj_hdl->refs--;  /* subtract the reference when we were created */
	if(obj_hdl->refs != 0) {
		pthread_mutex_unlock(&obj_hdl->lock);
		retval = obj_hdl->refs > 0 ? EBUSY : EINVAL;
		LogCrit(COMPONENT_FSAL,
			"Tried to release busy handle, "
			"hdl = 0x%p->refs = %d",
			obj_hdl, obj_hdl->refs);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	fsal_detach_handle(exp, &obj_hdl->handles);
	pthread_mutex_unlock(&obj_hdl->lock);
	pthread_mutex_destroy(&obj_hdl->lock);
	myself->obj_handle.ops = NULL; /*poison myself */
	myself->obj_handle.export = NULL;
	gsh_free(myself);
	return fsalstat(fsal_error, 0);
}

void xfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = xfs_lookup;
	ops->readdir = xfs_read_dirents;
	ops->create = xfs_create;
	ops->mkdir = xfs_makedir;
	ops->mknode = xfs_makenode;
	ops->symlink = xfs_makesymlink;
	ops->readlink = xfs_readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = xfs_getattrs;
	ops->setattrs = xfs_setattrs;
	ops->link = xfs_linkfile;
	ops->rename = xfs_renamefile;
	ops->unlink = xfs_unlink;
	ops->truncate = xfs_file_truncate;
	ops->open = xfs_open;
	ops->status = xfs_status;
	ops->read = xfs_read;
	ops->write = xfs_write;
	ops->commit = xfs_commit;
	ops->lock_op = xfs_lock_op;
	ops->close = xfs_close;
	ops->lru_cleanup = xfs_lru_cleanup;
	ops->compare = compare;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t xfs_lookup_path(struct fsal_export *exp_hdl,
                              const struct req_op_context *opctx,
			      const char *path,
			      struct fsal_obj_handle **handle)
{
	struct stat stat;
	struct xfs_fsal_obj_handle *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gsh_buffdesc fh;

	if(path == NULL
	   || path[0] != '/'
	   || strlen(path) > PATH_MAX
	   || strlen(path) < 2) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	if((path_to_handle((char *)path, &fh.addr, &fh.len) < 0) ||
	   (lstat(path, &stat) < 0)) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, &stat, exp_hdl);
	free_handle(fh.addr, fh.len);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL; /* poison it */
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

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

fsal_status_t
xfs_create_handle(struct fsal_export *exp_hdl,
                  const struct req_op_context *opctx,
		  struct gsh_buffdesc *hdl_desc,
		  struct fsal_obj_handle **handle)
{
	struct xfs_fsal_obj_handle *hdl;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int fd;
	struct xfs_fsal_ext_handle *xh = hdl_desc->addr;

	*handle = NULL; /* poison it first */
	if((hdl_desc->len < sizeof(*xh)) || 
	   (hdl_desc->len != sizeof(*xh) + xh->len))
		return fsalstat(ERR_FSAL_FAULT, 0);

	if ((xh->type != REGULAR_FILE) && (xh->type != DIRECTORY))
		return fsalstat(ERR_FSAL_STALE, 0);

	fd = open_by_handle(xh->data, xh->len, O_RDONLY);
	if(fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = fstat(fd, &stat);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		close(fd);
		goto errout;
	}
	close(fd);

	/* NB! Do NOT free handle data like you do in every other place
	 * which calls alloc_handle - it didn't come from libhandle */
	hdl = alloc_handle(hdl_desc, &stat, exp_hdl);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	
errout:
	return fsalstat(fsal_error, retval);	
}

