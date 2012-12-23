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
 * VFS object (file|dir) handle object
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
#include "nlm_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"

/* helpers
 */

static int
vfs_fsal_open_exp(struct fsal_export *exp,
		  vfs_file_handle_t *fh,
		  int openflags,
		  fsal_errors_t *fsal_error)
{
	int mount_fd = vfs_get_root_fd(exp);
	int fd = vfs_open_by_handle(mount_fd, fh, openflags);
	if(fd < 0) {
		fd = -errno;
		if(fd == -ENOENT)
			*fsal_error = ERR_FSAL_STALE;
		else
			*fsal_error = posix2fsal_error(-fd);
	}
	return fd;
}

int
vfs_fsal_open(struct vfs_fsal_obj_handle *myself,
	      int openflags,
	      fsal_errors_t *fsal_error)
{
	return vfs_fsal_open_exp(myself->obj_handle.export,
				 myself->handle, openflags, fsal_error);
}

/* alloc_handle
 * allocate and fill in a handle
 */

static struct vfs_fsal_obj_handle *alloc_handle(vfs_file_handle_t *fh,
                                                struct stat *stat,
                                                const char *link_content,
                                                vfs_file_handle_t *dir_fh,
                                                const char *unopenable_name,
                                                struct fsal_export *exp_hdl)
{
	struct vfs_fsal_obj_handle *hdl;
	fsal_status_t st;

	hdl = gsh_calloc(1, (sizeof(struct vfs_fsal_obj_handle) +
			     vfs_sizeof_handle(fh)));
	if(hdl == NULL)
		return NULL;
	hdl->handle = (vfs_file_handle_t *)&hdl[1];
	memcpy(hdl->handle, fh, vfs_sizeof_handle(fh));
	hdl->obj_handle.type = posix2fsal_type(stat->st_mode);
	if(hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd = -1;  /* no open on this yet */
		hdl->u.file.openflags = FSAL_O_CLOSED;
	} else if(hdl->obj_handle.type == SYMBOLIC_LINK
	   && link_content != NULL) {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = gsh_malloc(len);
		if(hdl->u.symlink.link_content == NULL) {
			goto spcerr;
		}
		memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
	} else if(vfs_unopenable_type(hdl->obj_handle.type)
		  && dir_fh != NULL
		  && unopenable_name != NULL) {
		hdl->u.unopenable.dir = gsh_malloc(vfs_sizeof_handle(dir_fh));
		if(hdl->u.unopenable.dir == NULL)
			goto spcerr;
		memcpy(hdl->u.unopenable.dir,
		       dir_fh,
                       vfs_sizeof_handle(dir_fh));
		hdl->u.unopenable.name = gsh_malloc(strlen(unopenable_name) + 1);
		if(hdl->u.unopenable.name == NULL)
			goto spcerr;
		strcpy(hdl->u.unopenable.name, unopenable_name);
	}
	hdl->obj_handle.export = exp_hdl;
	hdl->obj_handle.attributes.mask
		= exp_hdl->ops->fs_supported_attrs(exp_hdl);
	hdl->obj_handle.attributes.supported_attributes
                = hdl->obj_handle.attributes.mask;
	st = posix2fsal_attributes(stat, &hdl->obj_handle.attributes);
	if(FSAL_IS_ERROR(st))
		goto spcerr;
	if(!fsal_obj_handle_init(&hdl->obj_handle,
				 exp_hdl,
	                         posix2fsal_type(stat->st_mode)))
                return hdl;

	hdl->obj_handle.ops = NULL;
	pthread_mutex_unlock(&hdl->obj_handle.lock);
	pthread_mutex_destroy(&hdl->obj_handle.lock);
spcerr:
	if(hdl->obj_handle.type == SYMBOLIC_LINK) {
		if(hdl->u.symlink.link_content != NULL)
			gsh_free(hdl->u.symlink.link_content);
        } else if(vfs_unopenable_type(hdl->obj_handle.type)) {
		if(hdl->u.unopenable.name != NULL)
			gsh_free(hdl->u.unopenable.name);
		if(hdl->u.unopenable.dir != NULL)
			gsh_free(hdl->u.unopenable.dir);
	}
	gsh_free(hdl);  /* elvis has left the building */
	return NULL;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
                            const struct req_op_context *opctx,
			    const char *path,
			    struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval, dirfd;
	struct stat stat;
	char *link_content = NULL;
	vfs_file_handle_t *dir_hdl = NULL;
	const char *unopenable_name = NULL;
	ssize_t retlink;
	char link_buff[1024];
	vfs_file_handle_t *fh = NULL;
        vfs_alloc_handle(fh);

	*handle = NULL; /* poison it first */
	if( !path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	parent_hdl = container_of(parent, struct vfs_fsal_obj_handle, obj_handle);
	if( !parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	dirfd = vfs_fsal_open(parent_hdl, O_PATH|O_NOACCESS, &fsal_error);
	if(dirfd < 0) {
		return fsalstat(fsal_error, -dirfd);
	}
	retval = vfs_name_to_handle_at(dirfd, path, fh);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	retval = fstatat(dirfd, path, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	if(S_ISLNK(stat.st_mode)) { /* I could lazy eval this... */
		retlink = readlinkat(dirfd, path, link_buff, 1024);
		if(retlink < 0 || retlink == 1024) {
			retval = errno;
			if(retlink == 1024)
				retval = ENAMETOOLONG;
			goto direrr;
		}
		link_buff[retlink] = '\0';
		link_content = &link_buff[0];
	} else if(S_ISSOCK(stat.st_mode) ||
                  S_ISCHR(stat.st_mode) ||
                  S_ISBLK(stat.st_mode)) {
		dir_hdl = parent_hdl->handle;
		unopenable_name = path;
	}
	close(dirfd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat,
			   link_content,
			   dir_hdl,
			   unopenable_name,
			   parent->export);
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

static inline
int make_file_safe(int dir_fd,
		   const char *name,
		   mode_t unix_mode,
		   uid_t user,
		   gid_t group,
		   vfs_file_handle_t *fh,
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
	retval = vfs_name_to_handle_at(dir_fd, name, fh);
	if(retval < 0) {
		goto fileerr;
	}
	retval = fstatat(dir_fd, name, stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}
	return 0;

fileerr:
	retval = errno;
	return retval;
}
/* create
 * create a regular file and set its attributes
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
                            const struct req_op_context *opctx,
                            const char *name,
                            struct attrlist *attrib,
                            struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int fd, dir_fd;
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
        vfs_file_handle_t *fh = NULL;
        vfs_alloc_handle(fh);

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	dir_fd = vfs_fsal_open(myself, O_PATH|O_NOACCESS, &fsal_error);
	if(dir_fd < 0) 
		return fsalstat(fsal_error, -dir_fd);

	retval = fstatat(dir_fd, "", &stat, AT_EMPTY_PATH);
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

	retval = make_file_safe(dir_fd, name, unix_mode, user, group, fh, &stat);
	if(retval != 0) {
		goto fileerr;
	}
	close(dir_fd); /* done with parent */

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, NULL, NULL, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	close(fd);  /* don't need it anymore. */
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	unlinkat(dir_fd, name, 0);  /* remove the evidence on errors */

direrr:
	fsal_error = posix2fsal_error(retval);
	close(dir_fd);
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
                             const struct req_op_context *opctx,
			     const char *name,
			     struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd;
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
        vfs_file_handle_t *fh = NULL;
        vfs_alloc_handle(fh);

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	dir_fd = vfs_fsal_open(myself, O_PATH|O_NOACCESS, &fsal_error);
	if(dir_fd < 0) {
		return fsalstat(fsal_error, -dir_fd);	
	}
	retval = fstatat(dir_fd, "", &stat, AT_EMPTY_PATH);
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
	retval = make_file_safe(dir_fd, name, unix_mode, user, group, fh, &stat);
	if(retval != 0) {
		retval = errno;
		goto fileerr;
	}
	close(dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, NULL, NULL, dir_hdl->export);
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

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
                              const struct req_op_context *opctx,
                              const char *name,
                              object_file_type_t nodetype,  /* IN */
                              fsal_dev_t *dev,  /* IN */
                              struct attrlist *attrib,
                              struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	mode_t unix_mode, create_mode = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	dev_t unix_dev = 0;
	vfs_file_handle_t *dir_fh = NULL;
	const char *unopenable_name = NULL;
        vfs_file_handle_t *fh = NULL;
        vfs_alloc_handle(fh);

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);


		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
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
        if (vfs_unopenable_type(nodetype)) {
                dir_fh = myself->handle;
                unopenable_name = name;
        }
	dir_fd = vfs_fsal_open(myself, O_PATH|O_NOACCESS, &fsal_error);
	if(dir_fd < 0) {
		goto errout;
	}
	retval = fstatat(dir_fd, "", &stat, AT_EMPTY_PATH);
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
	retval = make_file_safe(dir_fd, name, unix_mode, user, group, fh, &stat);
	if(retval != 0) {
		goto nodeerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, dir_fh, unopenable_name, dir_hdl->export);
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

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
                                 const struct req_op_context *opctx,
                                 const char *name,
                                 const char *link_path,
                                 struct attrlist *attrib,
                                 struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
        vfs_file_handle_t *fh = NULL;
        vfs_alloc_handle(fh);

	*handle = NULL; /* poison it first */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	dir_fd = vfs_fsal_open(myself, O_PATH|O_NOACCESS, &fsal_error);
	if(dir_fd < 0) {
		return fsalstat(fsal_error, -dir_fd);
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

	retval = vfs_name_to_handle_at(dir_fd, name, fh);
	if(retval < 0) {
		goto linkerr;
	}
	/* now get attributes info, being careful to get the link, not the target */
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto linkerr;
	}
	close(dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, link_path, NULL, NULL, dir_hdl->export);
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

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
                                 const struct req_op_context *opctx,
                                 char *link_content,
                                 size_t *link_len,
                                 bool refresh)
{
	struct vfs_fsal_obj_handle *myself = NULL;
	int fd;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if(obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(refresh) { /* lazy load or LRU'd storage */
		ssize_t retlink;
		char link_buff[1024];

		if(myself->u.symlink.link_content != NULL) {
			gsh_free(myself->u.symlink.link_content);
			myself->u.symlink.link_content = NULL;
			myself->u.symlink.link_size = 0;
		}
		fd = vfs_fsal_open(myself, O_PATH|O_NOACCESS, &fsal_error);
		if(fd < 0) {
			retval = -fd;
			goto out;
		}
		retlink = readlinkat(fd, "", link_buff, 1024);
		if(retlink < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			close(fd);
			goto out;
		}
		close(fd);

		myself->u.symlink.link_content = gsh_malloc(retlink + 1);
		if(myself->u.symlink.link_content == NULL) {
			fsal_error = ERR_FSAL_NOMEM;
			goto out;
		}
		memcpy(myself->u.symlink.link_content, link_buff, retlink);
		myself->u.symlink.link_content[retlink] = '\0';
		myself->u.symlink.link_size = retlink + 1;
	}
	if(myself->u.symlink.link_content == NULL
	   || *link_len <= myself->u.symlink.link_size) {
		fsal_error = ERR_FSAL_FAULT; /* probably a better error?? */
		goto out;
	}
	memcpy(link_content,
	       myself->u.symlink.link_content,
	       myself->u.symlink.link_size);

out:
	*link_len = myself->u.symlink.link_size;
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	struct vfs_fsal_obj_handle *myself, *destdir;
	int srcfd, destdirfd;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if( !obj_hdl->export->ops->fs_supports(obj_hdl->export,
					       fso_link_support)) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0) {
		srcfd = myself->u.file.fd;
	} else {
		srcfd = vfs_fsal_open(myself, O_PATH|O_NOACCESS, &fsal_error);
		if(srcfd < 0) {
			retval = -srcfd;
			goto out;
		}
	}
	destdir = container_of(destdir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	destdirfd = vfs_fsal_open(destdir, O_PATH|O_NOACCESS, &fsal_error);
	if(destdirfd < 0) {
		retval = destdirfd;
		goto fileerr;
	}
	retval = linkat(srcfd, "", destdirfd, name, AT_EMPTY_PATH);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	close(destdirfd);

fileerr:
	if( !(obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0))
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
                                  const struct req_op_context *opctx,
                                  struct fsal_cookie *whence,
                                  void *dir_state,
                                  fsal_readdir_cb cb,
                                  bool *eof)
{
        struct vfs_fsal_obj_handle *myself;
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
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	dirfd = vfs_fsal_open(myself, O_RDONLY|O_DIRECTORY, &fsal_error);
	if(dirfd < 0) {
		retval = -dirfd;
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


static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
                                const struct req_op_context *opctx,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	struct vfs_fsal_obj_handle *olddir, *newdir;
	int oldfd, newfd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	olddir = container_of(olddir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	oldfd = vfs_fsal_open(olddir, O_PATH|O_NOACCESS, &fsal_error);
	if(oldfd < 0) {
		retval = -oldfd;
		goto out;
	}
	newdir = container_of(newdir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	newfd = vfs_fsal_open(newdir, O_PATH|O_NOACCESS, &fsal_error);
	if(newfd < 0) {
		retval = -newfd;
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

static int
vfs_fsal_open_and_stat(struct vfs_fsal_obj_handle *myself,
		       struct stat *stat,
		       fsal_errors_t *fsal_error)
{
        struct fsal_obj_handle *obj_hdl = &myself->obj_handle;
	int open_flags = O_RDONLY;
	int fd = -1;
	int retval = 0;

	if(obj_hdl->type == REGULAR_FILE) {
		if(myself->u.file.fd < 0) {
			goto open_file;  /* no file open at the moment */
		}
		fd = myself->u.file.fd;
		retval = fstat(fd, stat);
        } else if(vfs_unopenable_type(obj_hdl->type)) {
		fd = vfs_fsal_open_exp(obj_hdl->export,
				       myself->u.unopenable.dir,
			               O_PATH|O_NOACCESS,
				       fsal_error);
		if(fd < 0) {
			return fd;
		}
		retval = fstatat(fd,
				 myself->u.unopenable.name,
				 stat,
				 AT_SYMLINK_NOFOLLOW);
	} else {
		if(obj_hdl->type == SYMBOLIC_LINK)
			open_flags |= O_PATH;
		else if(obj_hdl->type == FIFO_FILE)
			open_flags |= O_NONBLOCK;
	open_file:
		fd = vfs_fsal_open(myself, open_flags, fsal_error);
		if(fd < 0) {
			return fd;
		}
		retval = fstatat(fd,
				 "",
				 stat,
				 (AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH));
	}

	if(retval < 0) {
		retval = errno;
                *fsal_error = posix2fsal_error(retval);
		if(obj_hdl->type != REGULAR_FILE || myself->u.file.fd < 0)
			close(fd);
		return -retval;
	}

	return fd;
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx)
{
	struct vfs_fsal_obj_handle *myself;
	int fd = -1;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t st;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	fd = vfs_fsal_open_and_stat(myself, &stat, &fsal_error);
	if(fd >= 0) {
	   	if(obj_hdl->type != REGULAR_FILE || myself->u.file.fd < 0)
			close(fd);
		st = posix2fsal_attributes(&stat, &obj_hdl->attributes);
		if(FSAL_IS_ERROR(st)) {
			FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
			FSAL_SET_MASK(obj_hdl->attributes.mask,
				      ATTR_RDATTR_ERR);
			fsal_error = st.major;  retval = st.minor;
		}
	} else {
		retval = -fd;
	}

	return fsalstat(fsal_error, retval);	
}

/*
 * NOTE: this is done under protection of the attributes rwlock in the cache entry.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
			      struct attrlist *attrs)
{
	struct vfs_fsal_obj_handle *myself;
	int fd = -1;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	/* apply umask, if mode attribute is to be changed */
	if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		attrs->mode
			&= ~obj_hdl->export->ops->fs_umask(obj_hdl->export);
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	/* This is yet another "you can't get there from here".  If this object
	 * is a socket (AF_UNIX), an fd on the socket s useless _period_.
	 * If it is for a symlink, without O_PATH, you will get an ELOOP error
	 * and (f)chmod doesn't work for a symlink anyway - not that it matters
	 * because access checking is not done on the symlink but the final target.
	 * AF_UNIX sockets are also ozone material.  If the socket is already active
	 * listeners et al, you can manipulate the mode etc.  If it is just sitting
	 * there as in you made it with a mknod (one of those leaky abstractions...)
	 * or the listener forgot to unlink it, it is lame duck.
	 */

	fd = vfs_fsal_open_and_stat(myself, &stat, &fsal_error);
	if(fd < 0) {
		return fsalstat(fsal_error, -fd);
	}
	/** CHMOD **/
	if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		/* The POSIX chmod call doesn't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if(!S_ISLNK(stat.st_mode)) {
			if(vfs_unopenable_type(obj_hdl->type))
				retval = fchmodat(fd,
						  myself->u.unopenable.name,
						  fsal2unix_mode(attrs->mode), 0);
			else
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

		if(vfs_unopenable_type(obj_hdl->type))
			retval = fchownat(fd,
					  myself->u.unopenable.name,
					  user,
					  group,
					  AT_SYMLINK_NOFOLLOW);
		else
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
		if(vfs_unopenable_type(obj_hdl->type))
			retval = futimesat(fd,
					   myself->u.unopenable.name,
					   timebuf);
		else
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
	if( !(obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0))
		close(fd);
	return fsalstat(fsal_error, retval);
}

/* file_truncate
 * truncate a file to the size specified.
 * size should really be off_t...
 */

static fsal_status_t file_truncate(struct fsal_obj_handle *obj_hdl,
                                   const struct req_op_context *opctx,
				   uint64_t length)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int fd;
	int retval = 0;

	if(obj_hdl->type != REGULAR_FILE) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	fd = vfs_fsal_open(myself, O_RDWR, &fsal_error);
	if(fd < 0) {
		retval = -fd;
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

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
                                 const struct req_op_context *opctx,
				 const char *name)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct stat stat;
	int fd;
	int retval = 0;

	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	fd = vfs_fsal_open(myself, O_PATH|O_NOACCESS, &fsal_error);
	if(fd < 0) {
		retval = -fd;
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
	uint32_t ino32;
	uint64_t ino64;
	struct vfs_fsal_obj_handle *myself;
	vfs_file_handle_t *fh;
	size_t fh_size;

	/* sanity checks */
        if( !fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	fh = myself->handle;

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = vfs_sizeof_handle(fh);
                if(fh_desc->len < fh_size)
                        goto errout;
                memcpy(fh_desc->addr, fh, fh_size);
		break;
	case FSAL_DIGEST_FILEID2:
		fh_size = FSAL_DIGEST_SIZE_FILEID2;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, fh->handle, fh_size);
		break;
	case FSAL_DIGEST_FILEID3:
		fh_size = FSAL_DIGEST_SIZE_FILEID3;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(&ino32, fh->handle, sizeof(ino32));
		ino64 = ino32;
		memcpy(fh_desc->addr, &ino64, fh_size);
		break;
	case FSAL_DIGEST_FILEID4:
		fh_size = FSAL_DIGEST_SIZE_FILEID4;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(&ino32, fh->handle, sizeof(ino32));
		ino64 = ino32;
		memcpy(fh_desc->addr, &ino64, fh_size);
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
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = vfs_sizeof_handle(myself->handle);
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct fsal_export *exp = obj_hdl->export;
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if(obj_hdl->type == REGULAR_FILE) {
		fsal_status_t st = vfs_close(obj_hdl);
		if(FSAL_IS_ERROR(st))
			return st;
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	pthread_mutex_lock(&obj_hdl->lock);
	obj_hdl->refs--;  /* subtract the reference when we were created */
	if(obj_hdl->refs != 0 || (obj_hdl->type == REGULAR_FILE
				  && (myself->u.file.fd >=0
				      || myself->u.file.openflags != FSAL_O_CLOSED))) {
		pthread_mutex_unlock(&obj_hdl->lock);
		retval = obj_hdl->refs > 0 ? EBUSY : EINVAL;
		LogCrit(COMPONENT_FSAL,
			"Tried to release busy handle, "
			"hdl = 0x%p->refs = %d, fd = %d, openflags = 0x%x",
			obj_hdl, obj_hdl->refs,
			myself->u.file.fd, myself->u.file.openflags);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	fsal_detach_handle(exp, &obj_hdl->handles);
	pthread_mutex_unlock(&obj_hdl->lock);
	pthread_mutex_destroy(&obj_hdl->lock);
	myself->obj_handle.ops = NULL; /*poison myself */
	myself->obj_handle.export = NULL;
	if(obj_hdl->type == SYMBOLIC_LINK) {
		if(myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	} else if(vfs_unopenable_type(obj_hdl->type)) {
		if(myself->u.unopenable.name != NULL)
			gsh_free(myself->u.unopenable.name);
		if(myself->u.unopenable.dir != NULL)
			gsh_free(myself->u.unopenable.dir);
	}
	gsh_free(myself);
	return fsalstat(fsal_error, 0);
}

void vfs_handle_ops_init(struct fsal_obj_ops *ops)
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
	ops->truncate = file_truncate;
	ops->open = vfs_open;
	ops->status = vfs_status;
	ops->read = vfs_read;
	ops->write = vfs_write;
	ops->commit = vfs_commit;
	ops->lock_op = vfs_lock_op;
	ops->close = vfs_close;
	ops->lru_cleanup = vfs_lru_cleanup;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
        
        /* xattr related functions */
        ops->list_ext_attrs = vfs_list_ext_attrs;
        ops->getextattr_id_by_name = vfs_getextattr_id_by_name;
        ops->getextattr_value_by_name = vfs_getextattr_value_by_name;
        ops->getextattr_value_by_id = vfs_getextattr_value_by_id;
        ops->setextattr_value = vfs_setextattr_value;
        ops->setextattr_value_by_id = vfs_setextattr_value_by_id;
        ops->getextattr_attrs = vfs_getextattr_attrs;
        ops->remove_extattr_by_id = vfs_remove_extattr_by_id;
        ops->remove_extattr_by_name = vfs_remove_extattr_by_name;

}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t vfs_lookup_path(struct fsal_export *exp_hdl,
                              const struct req_op_context *opctx,
			      const char *path,
			      struct fsal_obj_handle **handle)
{
	int dir_fd;
	struct stat stat;
	struct vfs_fsal_obj_handle *hdl;
	char *basepart;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int mnt_id = 0;
	int retval = 0;
	char *link_content = NULL;
	ssize_t retlink;
	vfs_file_handle_t *dir_fh = NULL;
	char *unopenable_name = NULL;
        vfs_file_handle_t *fh = NULL;
        vfs_alloc_handle(fh);

	if(path == NULL
	   || path[0] != '/'
	   || strlen(path) > PATH_MAX
	   || strlen(path) < 2) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	basepart = rindex(path, '/');
	if(basepart[1] == '\0') {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	if(basepart == path) {
		dir_fd = open("/", O_RDONLY);
	} else {
		char *dirpart = alloca(basepart - path + 1);

		memcpy(dirpart, path, basepart - path);
		dirpart[basepart - path] = '\0';
		dir_fd = open(dirpart, O_RDONLY, 0600);
	}
	if(dir_fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = fstat(dir_fd, &stat);
	if( !S_ISDIR(stat.st_mode)) {  /* this had better be a DIR! */
		goto fileerr;
	}
	basepart++;
	retval = vfs_name_to_handle_at(dir_fd, basepart, fh);
	if(retval < 0) {
		goto fileerr;
	}

	/* what about the file? Do no symlink chasing here. */
	retval = fstatat(dir_fd, basepart, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}
	if(S_ISLNK(stat.st_mode)) {
		link_content = gsh_malloc(PATH_MAX);
		retlink = readlinkat(dir_fd, basepart,
				     link_content, PATH_MAX);
		if(retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if(retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			goto linkerr;
		}
		link_content[retlink] = '\0';
	} else if(S_ISSOCK(stat.st_mode) ||
                  S_ISCHR(stat.st_mode) ||
                  S_ISBLK(stat.st_mode)) {
                /* AF_UNIX sockets, character special, and block
                   special files  require craziness */
                vfs_alloc_handle(dir_fh);
		retval = vfs_fd_to_handle(dir_fd, dir_fh, &mnt_id);
		if(retval < 0) {
			goto fileerr;
		}
		unopenable_name = basepart;
	}
	close(dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, link_content, dir_fh, unopenable_name, exp_hdl);
	if(link_content != NULL)
		gsh_free(link_content);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL; /* poison it */
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	retval = errno;
linkerr:
	if(link_content != NULL)
		gsh_free(link_content);
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
 * we cannot get an fd on an AF_UNIX socket, nor reliably on block or
 * character special devices.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t vfs_create_handle(struct fsal_export *exp_hdl,
                                const struct req_op_context *opctx,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *hdl;
	struct stat stat;
	vfs_file_handle_t  *fh = NULL;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int fd;
	char *link_content = NULL;
	ssize_t retlink;
	char link_buff[PATH_MAX];

        vfs_alloc_handle(fh);
	*handle = NULL; /* poison it first */
	if((hdl_desc->len > (vfs_sizeof_handle(fh)) ||
	   (((vfs_file_handle_t *)(hdl_desc->addr))->handle_bytes >  fh->handle_bytes)))
		return fsalstat(ERR_FSAL_FAULT, 0);

	memcpy(fh, hdl_desc->addr, hdl_desc->len);  /* struct aligned copy */
	fd = vfs_fsal_open_exp(exp_hdl, fh, O_PATH|O_NOACCESS, &fsal_error);
	if(fd < 0) {
		retval = -fd;
		goto errout;
	}
	retval = fstatat(fd, "", &stat, AT_EMPTY_PATH);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		close(fd);
		goto errout;
	}
	if(S_ISLNK(stat.st_mode)) { /* I could lazy eval this... */
		retlink = readlinkat(fd, "", link_buff, PATH_MAX);
		if(retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if(retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			close(fd);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = link_buff;
	}
	close(fd);

	hdl = alloc_handle(fh, &stat, link_content, NULL, NULL, exp_hdl);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	
errout:
	return fsalstat(fsal_error, retval);	
}

