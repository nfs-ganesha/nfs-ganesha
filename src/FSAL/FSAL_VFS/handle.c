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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ------------- 
 */

/* handle.c
 * VFS object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include "fsal_handle_syscalls.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"
#include <os/subr.h>

/* helpers
 */

int
vfs_fsal_open(struct vfs_fsal_obj_handle *myself,
	      int openflags,
	      fsal_errors_t *fsal_error)
{
        struct vfs_fsal_export *ve = container_of(myself->obj_handle.export,
                                                  struct vfs_fsal_export,
                                                  export);
	return ve->vex_ops.vex_open_by_handle(myself->obj_handle.export,
                                              myself->handle, openflags,
                                              fsal_error);
}

static int
vfs_fsal_fd_to_handle(struct fsal_export *exp,
                        int dirfd,
                        vfs_file_handle_t *fh)
{
        struct vfs_fsal_export *ve = container_of(exp, struct vfs_fsal_export,
                                                  export);
        return ve->vex_ops.vex_fd_to_handle(dirfd, fh);
}

static int
vfs_fsal_name_to_handle(struct fsal_export *exp,
                        int dirfd,
                        const char *path,
                        vfs_file_handle_t *fh)
{
        struct vfs_fsal_export *ve = container_of(exp, struct vfs_fsal_export,
                                                  export);
        return ve->vex_ops.vex_name_to_handle(dirfd, path, fh);
}

/* alloc_handle
 * allocate and fill in a handle
 */

static struct vfs_fsal_obj_handle *alloc_handle(int dirfd,
                                                vfs_file_handle_t *fh,
                                                struct stat *stat,
                                                vfs_file_handle_t *dir_fh,
                                                const char *path,
                                                struct fsal_export *exp_hdl)
{
	struct vfs_fsal_obj_handle *hdl;
	fsal_status_t st;

	hdl = gsh_calloc(1, (sizeof(struct vfs_fsal_obj_handle) +
			     sizeof(vfs_file_handle_t)));
	if(hdl == NULL)
		return NULL;
	hdl->handle = (vfs_file_handle_t *)&hdl[1];
	memcpy(hdl->handle, fh, sizeof(vfs_file_handle_t));
	hdl->obj_handle.type = posix2fsal_type(stat->st_mode);
	if(hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd = -1;  /* no open on this yet */
		hdl->u.file.openflags = FSAL_O_CLOSED;
	} else if(hdl->obj_handle.type == SYMBOLIC_LINK) {
                ssize_t retlink;
		size_t len = stat->st_size + 1;
                char *link_content = gsh_malloc(len);

		if(link_content == NULL)
			goto spcerr;

                retlink = vfs_readlink_by_handle(fh, dirfd, path,
                                link_content, len);
		if(retlink < 0 || retlink == len) {
			goto spcerr;
		}
		link_content[retlink] = '\0';
		hdl->u.symlink.link_content =  link_content;
		hdl->u.symlink.link_size = len;
	} else if(vfs_unopenable_type(hdl->obj_handle.type)) {
                /* AF_UNIX sockets, character special, and block
                   special files  require craziness */
                if(dir_fh == NULL) {
                        int retval;
                        vfs_alloc_handle(dir_fh);
                        retval = vfs_fsal_fd_to_handle(exp_hdl, dirfd, dir_fh);
                        if(retval < 0) {
                                goto spcerr;
                        }
                }
		hdl->u.unopenable.dir = gsh_malloc(sizeof(vfs_file_handle_t));
		if(hdl->u.unopenable.dir == NULL)
			goto spcerr;
		memcpy(hdl->u.unopenable.dir,
		       dir_fh,
                       sizeof(vfs_file_handle_t));
		hdl->u.unopenable.name = gsh_strdup(path);
		if(hdl->u.unopenable.name == NULL)
			goto spcerr;
	}
	hdl->obj_handle.export = exp_hdl;
	hdl->obj_handle.attributes.mask
		= exp_hdl->ops->fs_supported_attrs(exp_hdl);
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
	retval = vfs_fsal_name_to_handle(parent->export, dirfd, path, fh);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	retval = fstatat(dirfd, path, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dirfd, fh, &stat, parent_hdl->handle, path,
			   parent->export);
	close(dirfd);
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
int make_file_safe(struct vfs_fsal_obj_handle *dir_hdl,
                   int dir_fd,
		   const char *name,
		   mode_t unix_mode,
		   uid_t user,
		   gid_t group,
                   struct vfs_fsal_obj_handle **hdl)
{
	int retval;
        struct stat stat;
        vfs_file_handle_t *fh;

        vfs_alloc_handle(fh);
	
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
	retval = vfs_fsal_name_to_handle(dir_hdl->obj_handle.export,
                                         dir_fd, name, fh);
	if(retval < 0) {
		goto fileerr;
	}
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	*hdl = alloc_handle(dir_fd, fh, &stat, dir_hdl->handle,
                            name, dir_hdl->obj_handle.export);
	if(*hdl == NULL) {
	        return ENOMEM;
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
        int flags = O_PATH|O_NOACCESS;
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
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	dir_fd = vfs_fsal_open(myself, flags, &fsal_error);
	if(dir_fd < 0) {
		return fsalstat(fsal_error, -dir_fd);
	}
	retval = vfs_stat_by_handle(dir_fd, myself->handle, &stat, flags);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(opctx->creds);
	fd = openat(dir_fd, name, O_CREAT|O_WRONLY|O_TRUNC|O_EXCL, unix_mode);
	if(fd < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval = vfs_fsal_name_to_handle(myself->obj_handle.export,
                                         dir_fd, name, fh);
	if(retval < 0) {
		retval = errno;
		goto fileerr;
	}
	retval = fstat(fd, &stat);
	if(retval < 0) {
		retval = errno;
		goto fileerr;
	}
	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, &stat, myself->handle,
                            name, myself->obj_handle.export);
	if(hdl == NULL) {
	        retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;
	close(dir_fd);
        close(fd);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	close(fd);
	unlinkat(dir_fd, name, 0);
direrr:
	close(dir_fd);
	fsal_error = posix2fsal_error(retval);
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
        int flags = O_PATH|O_NOACCESS;
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
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	dir_fd = vfs_fsal_open(myself, flags, &fsal_error);
	if(dir_fd < 0) {
		return fsalstat(fsal_error, -dir_fd);	
	}
	retval = vfs_stat_by_handle(dir_fd, myself->handle, &stat, flags);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(opctx->creds);
	retval = mkdirat(dir_fd, name, unix_mode);
	if(retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval = vfs_fsal_name_to_handle(myself->obj_handle.export,
                                         dir_fd, name, fh);
	if(retval < 0) {
		retval = errno;
		goto fileerr;
	}
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		retval = errno;
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, &stat, myself->handle,
                            name, myself->obj_handle.export);
	if(hdl == NULL) {
	        retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;

fileerr:
	unlinkat(dir_fd, name, 0);
direrr:
	close(dir_fd);
	fsal_error = posix2fsal_error(retval);
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
        int flags = O_PATH|O_NOACCESS;
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
	dir_fd = vfs_fsal_open(myself, flags, &fsal_error);
	if(dir_fd < 0) {
		goto errout;
	}
	retval = vfs_stat_by_handle(dir_fd, myself->handle, &stat, flags);
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
	retval = make_file_safe(myself, dir_fd, name, unix_mode, user, group, &hdl);
	if(!retval) {
                close(dir_fd); /* done with parent */
                *handle = &hdl->obj_handle;
                return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }
	
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
        int flags = O_PATH|O_NOACCESS;
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
	dir_fd = vfs_fsal_open(myself, flags, &fsal_error);
	if(dir_fd < 0) {
		return fsalstat(fsal_error, -dir_fd);
	}
        flags |= O_NOFOLLOW; /* BSD needs O_NOFOLLOW for fhopen() of symlinks */
	retval = vfs_stat_by_handle(dir_fd, myself->handle, &stat, flags);
	if(retval < 0) {
		retval = errno;
		goto direrr;
	}
	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(opctx->creds);
	retval = symlinkat(link_path, dir_fd, name);
	if(retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval = vfs_fsal_name_to_handle(dir_hdl->export, dir_fd, name, fh);
	if(retval < 0) {
		retval = errno;
		goto linkerr;
	}
	/* now get attributes info, being careful to get the link, not the target */
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		retval = errno;
		goto linkerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, &stat, NULL, name, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto linkerr;
	}
	fsal_restore_ganesha_credentials();
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

linkerr:
	unlinkat(dir_fd, name, 0);

direrr:
	close(dir_fd);
	if(retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

int
vfs_fsal_readlink(struct vfs_fsal_obj_handle *myself,
                  fsal_errors_t *fsal_error)
{
        int retval = 0;
        int fd;
        ssize_t retlink;
	struct stat st;
        int flags = O_PATH|O_NOACCESS|O_NOFOLLOW;

        if(myself->u.symlink.link_content != NULL) {
                gsh_free(myself->u.symlink.link_content);
                myself->u.symlink.link_content = NULL;
                myself->u.symlink.link_size = 0;
        }
        fd = vfs_fsal_open(myself, flags, fsal_error);
        if(fd < 0)
                return fd;

        retval = vfs_stat_by_handle(fd, myself->handle, &st, flags);
	if (retval < 0) {
		goto error;
	}

	myself->u.symlink.link_size = st.st_size + 1;
	myself->u.symlink.link_content
		= gsh_malloc(myself->u.symlink.link_size);
	if (myself->u.symlink.link_content == NULL) {
		goto error;
	}

        retlink = vfs_readlink_by_handle(myself->handle,
                             fd, "",
                             myself->u.symlink.link_content,
			     myself->u.symlink.link_size);
        if(retlink < 0) {
		goto error;
        }
	myself->u.symlink.link_content[retlink] = '\0';
        close(fd);

        return retval;

error:
        retval = -errno;
	*fsal_error = posix2fsal_error(errno);
	close(fd);
	if (myself->u.symlink.link_content != NULL) {
		gsh_free(myself->u.symlink.link_content);
		myself->u.symlink.link_content = NULL;
		myself->u.symlink.link_size = 0;
	}
	return retval;
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
                                 const struct req_op_context *opctx,
                                 struct gsh_buffdesc *link_content,
                                 bool refresh)
{
	struct vfs_fsal_obj_handle *myself = NULL;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if(obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(refresh) { /* lazy load or LRU'd storage */
                struct vfs_fsal_export *ve =
                        container_of(obj_hdl->export, struct vfs_fsal_export,
                                     export);
                retval = ve->vex_ops.vex_readlink(myself, &fsal_error);
                if(retval < 0) {
                        retval = -retval;
                        goto out;
                }
	}
	if(myself->u.symlink.link_content == NULL) {
		fsal_error = ERR_FSAL_FAULT; /* probably a better error?? */
		goto out;
	}

	link_content->len = myself->u.symlink.link_size;
	link_content->addr = gsh_malloc(myself->u.symlink.link_size);
	if (link_content->addr == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto out;
	}
	memcpy(link_content->addr,
	       myself->u.symlink.link_content,
	       link_content->len);

out:
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
        int flags = O_PATH|O_NOACCESS|O_NOFOLLOW;
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
		srcfd = vfs_fsal_open(myself, flags, &fsal_error);
		if(srcfd < 0) {
			retval = -srcfd;
			goto out;
		}
	}
	destdir = container_of(destdir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	destdirfd = vfs_fsal_open(destdir, flags, &fsal_error);
	if(destdirfd < 0) {
		retval = destdirfd;
		goto fileerr;
	}
	retval = vfs_link_by_handle(myself->handle,
                                    srcfd, "",
                                    destdirfd, name,
                                    AT_EMPTY_PATH, &fsal_error);
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
                                  const struct req_op_context *opctx,
                                  fsal_cookie_t *whence,
                                  void *dir_state,
                                  fsal_readdir_cb cb,
                                  bool *eof)
{
        struct vfs_fsal_obj_handle *myself;
        int dirfd;
        fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        int retval = 0;
	off_t seekloc = 0;
	off_t baseloc = 0;
	unsigned int bpos;
	int nread;
        struct vfs_dirent dentry, *dentryp = &dentry;
        char buf[BUF_SIZE];

        if(whence != NULL) {
                seekloc = (off_t)*whence;
        }
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

	do {
		baseloc = seekloc;
		nread = vfs_readents(dirfd, buf, BUF_SIZE, &seekloc);
		if(nread < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto done;
		}
		if(nread == 0)
			break;
		for(bpos = 0; bpos < nread;) {
			if( !to_vfs_dirent(buf, bpos, dentryp, baseloc) ||
			    strcmp(dentryp->vd_name, ".") == 0 ||
			    strcmp(dentryp->vd_name, "..") == 0)
				goto skip; /* must skip '.' and '..' */

                        /* callback to cache inode */
                        if (!cb(opctx,
                                dentryp->vd_name,
                                dir_state,
                                (fsal_cookie_t)dentryp->vd_offset)) {
                                goto done;
                        }
		skip:
			bpos += dentryp->vd_reclen;
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
	/* Become the user because we are creating/removing objects in these dirs.
	 */
	fsal_set_credentials(opctx->creds);
	retval = renameat(oldfd, old_name, newfd, new_name);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	fsal_restore_ganesha_credentials();
	close(oldfd);
	close(newfd);
out:
	return fsalstat(fsal_error, retval);	
}

static int
vfs_fsal_open_and_stat(struct vfs_fsal_obj_handle *myself,
		       struct stat *stat,
		       int open_flags,
		       fsal_errors_t *fsal_error)
{
        struct fsal_obj_handle *obj_hdl = &myself->obj_handle;
	int fd = -1;
	int retval = 0;
        struct vfs_fsal_export *ve = NULL;
        vfs_file_handle_t *fh = NULL;
        vfs_alloc_handle(fh);

        switch (obj_hdl->type) {
        case SOCKET_FILE:
        case CHARACTER_FILE:
        case BLOCK_FILE:
                ve = container_of(obj_hdl->export, struct vfs_fsal_export, export);
                fd = ve->vex_ops.vex_open_by_handle(obj_hdl->export,
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

                break;
        case REGULAR_FILE:
                if(myself->u.file.openflags == FSAL_O_CLOSED) {
                        /* no file open at the moment */
                        fd = vfs_fsal_open(myself, open_flags, fsal_error);
                        if(fd < 0) {
                                return fd;
                        }
                } else {
                        fd = myself->u.file.fd;
                }
                retval = fstat(fd, stat);
                break;
        case DIRECTORY:
                fd = vfs_fsal_open(myself, open_flags, fsal_error);
                if(fd < 0) {
                        return fd;
                }
                retval = vfs_stat_by_handle(fd, myself->handle, stat, open_flags);
                break;
        case SYMBOLIC_LINK:
                open_flags |= (O_PATH | O_RDWR | O_NOFOLLOW);
                goto vfos_open;
        case FIFO_FILE:
                open_flags |= O_NONBLOCK;
                /* fall through */
        default:
vfos_open:
                fd = vfs_fsal_open(myself, open_flags, fsal_error);
                if(fd < 0) {
                        return fd;
                }
                retval = vfs_stat_by_handle(fd, myself->handle, stat, open_flags);
                break;
        }

        if(retval < 0) {
                retval = errno;
                *fsal_error = posix2fsal_error(retval);
                if(obj_hdl->type != REGULAR_FILE
                        || myself->u.file.openflags == FSAL_O_CLOSED)
                        close(fd);
                return -retval;
        }
	return fd;
}

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
	fd = vfs_fsal_open_and_stat(myself, &stat, O_RDONLY, &fsal_error);
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
		if(obj_hdl->type == SYMBOLIC_LINK && fd == -ERR_FSAL_PERM) {
			/* You cannot open_by_handle (XFS on linux) a symlink and it
			 * throws an EPERM error for it.  open_by_handle_at
			 * does not throw that error for symlinks so we play a
			 * game here.  Since there is not much we can do with
			 * symlinks anyway, say that we did it but don't actually
			 * do anything.  In this case, return the stat we got
			 * at lookup time.  If you *really* want to tweek things
			 * like owners, get a modern linux kernel...
			 */
			fsal_error = ERR_FSAL_NO_ERROR;
			goto out;
		}
		retval = -fd;
	}

out:
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
	int open_flags = O_RDONLY;

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

	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE))
		open_flags = O_RDWR;

	fd = vfs_fsal_open_and_stat(myself, &stat, open_flags, &fsal_error);
	if(fd < 0) {
		if(obj_hdl->type == SYMBOLIC_LINK && fd == -ERR_FSAL_PERM) {
			/* You cannot open_by_handle (XFS) a symlink and it
			 * throws an EPERM error for it.  open_by_handle_at
			 * does not throw that error for symlinks so we play a
			 * game here.  Since there is not much we can do with
			 * symlinks anyway, say that we did it but don't actually
			 * do anything.  If you *really* want to tweek things
			 * like owners, get a modern linux kernel...
			 */
			fsal_error = ERR_FSAL_NO_ERROR;
			goto out;
		}
		return fsalstat(fsal_error, -fd);
	}
	/** TRUNCATE **/
	if(FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		if(obj_hdl->type != REGULAR_FILE) {
			fsal_error = ERR_FSAL_INVAL;
			goto fileerr;
		}
		retval = ftruncate(fd, attrs->filesize);
		if(retval != 0) {
			goto fileerr;
		}
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
		else if(obj_hdl->type == SYMBOLIC_LINK)
                        retval = fchownat(fd, "", user, group,
                                          AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH);
                else
			retval = fchown(fd, user, group);

		if(retval) {
			goto fileerr;
		}
	}
		
	/**  UTIME  **/
	if(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME | ATTR_MTIME | ATTR_ATIME_SERVER | ATTR_MTIME_SERVER)) {
		struct timespec timebuf[2];

                if( obj_hdl->type == SYMBOLIC_LINK )
                        goto out; /* Setting time on a symbolic link is illegal */
		/* Atime */
		if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER))
		{
			timebuf[0].tv_sec = 0;
			timebuf[0].tv_nsec = UTIME_NOW;
		} else if(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME))
		{
			timebuf[0] = attrs->atime;
		} else {
			timebuf[0].tv_sec  = 0;
			timebuf[0].tv_nsec = UTIME_OMIT;
		}

		/* Mtime */
		if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER))
		{
			timebuf[1].tv_sec = 0;
			timebuf[1].tv_nsec = UTIME_NOW;
		} else if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
			timebuf[1] = attrs->mtime;
		} else {
			timebuf[1].tv_sec = 0;
			timebuf[1].tv_nsec = UTIME_OMIT;
		}
		if(vfs_unopenable_type(obj_hdl->type))
                        retval = vfs_utimesat(fd,
					myself->u.unopenable.name,
					timebuf, AT_SYMLINK_NOFOLLOW);
		else
			retval = vfs_utimes(fd, timebuf);
		if(retval != 0) {
			goto fileerr;
		}
	}
	goto out;

fileerr:
        retval = errno;
        fsal_error = posix2fsal_error(retval);
out:
	if(obj_hdl->type != REGULAR_FILE
	      || myself->u.file.openflags == FSAL_O_CLOSED)
		close(fd);
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

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
                                   fsal_digesttype_t output_type,
                                   struct gsh_buffdesc *fh_desc)
{
	const struct vfs_fsal_obj_handle *myself;
	vfs_file_handle_t *fh;
	size_t fh_size;

	/* sanity checks */
        if( !fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself = container_of(obj_hdl, const struct vfs_fsal_obj_handle, obj_handle);
	fh = myself->handle;

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = sizeof(vfs_file_handle_t);
                if(fh_desc->len < fh_size)
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
	fh_desc->len = sizeof(vfs_file_handle_t);
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;
	int retval = 0;
	object_file_type_t type = obj_hdl->type;

	if(type == REGULAR_FILE) {
		fsal_status_t st = vfs_close(obj_hdl);
		if(FSAL_IS_ERROR(st))
			return st;
	}

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if(type == REGULAR_FILE &&
	   (myself->u.file.fd >=0 || myself->u.file.openflags != FSAL_O_CLOSED)) {
                LogCrit(COMPONENT_FSAL,
                        "Tried to release busy handle, "
                        "hdl = 0x%p, fd = %d, openflags = 0x%x",
                        obj_hdl,
                        myself->u.file.fd, myself->u.file.openflags);
                return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	retval = fsal_obj_handle_uninit(obj_hdl);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"Tried to release busy handle, "
			"hdl = 0x%p->refs = %d",
			obj_hdl, obj_hdl->refs);
		return fsalstat(posix2fsal_error(retval), retval);
	}

	if(type == SYMBOLIC_LINK) {
		if(myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	} else if(vfs_unopenable_type(type)) {
		if(myself->u.unopenable.name != NULL)
			gsh_free(myself->u.unopenable.name);
		if(myself->u.unopenable.dir != NULL)
			gsh_free(myself->u.unopenable.dir);
	}
	gsh_free(myself);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
	int retval = 0;
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
	retval = vfs_fsal_name_to_handle(exp_hdl, dir_fd, basepart, fh);
	if(retval < 0) {
		goto fileerr;
	}

	/* what about the file? Do no symlink chasing here. */
	retval = fstatat(dir_fd, basepart, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, &stat, NULL, basepart, exp_hdl);
	close(dir_fd);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL; /* poison it */
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	retval = errno;
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
        struct vfs_fsal_export *ve;
        int flags = O_PATH|O_NOACCESS|O_NOFOLLOW;

        vfs_alloc_handle(fh);
	*handle = NULL; /* poison it first */
	if((hdl_desc->len > sizeof(vfs_file_handle_t) ||
	   (((vfs_file_handle_t *)(hdl_desc->addr))->handle_bytes >  fh->handle_bytes)))
		return fsalstat(ERR_FSAL_FAULT, 0);

	memcpy(fh, hdl_desc->addr, hdl_desc->len);  /* struct aligned copy */
        ve = container_of(exp_hdl, struct vfs_fsal_export, export);

        fd = ve->vex_ops.vex_open_by_handle(exp_hdl, fh, flags, &fsal_error);
	if(fd < 0) {
		retval = -fd;
		goto errout;
	}
	retval = vfs_stat_by_handle(fd, fh, &stat, flags);
	if(retval != 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		close(fd);
		goto errout;
	}

	hdl = alloc_handle(fd, fh, &stat, NULL, "", exp_hdl);
	close(fd);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	
errout:
	return fsalstat(fsal_error, retval);	
}

