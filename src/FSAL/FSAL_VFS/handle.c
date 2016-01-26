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
 * VFS object (file|dir) handle object
 */

#include "config.h"

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "fsal_handle_syscalls.h"
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"
#include <os/subr.h>
#include "subfsal.h"

/* helpers
 */

int vfs_fsal_open(struct vfs_fsal_obj_handle *hdl,
		  int openflags,
		  fsal_errors_t *fsal_error)
{
	struct vfs_filesystem *vfs_fs = hdl->obj_handle.fs->private;

	return vfs_open_by_handle(vfs_fs, hdl->handle, openflags, fsal_error);
}

/**
 * @brief Create a VFS OBJ handle
 *
 * @param[in] dirfd	FD for dir containing new handle
 * @param[in] fh	VFS FH for new handle
 * @param[in] fs	FileSystem containing new handle
 * @param[in] stat	stat(2) resutls for new handle
 * @param[in] dir_fh	VFS FH for dir containing new handle
 * @param[in] path	Path to new handle
 * @param[in] exp_hdl	Export containing new handle
 * @return VFS OBJ handle on success, NULL on failure
 */
static struct vfs_fsal_obj_handle *alloc_handle(int dirfd,
						vfs_file_handle_t *fh,
						struct fsal_filesystem *fs,
						struct stat *stat,
						vfs_file_handle_t *dir_fh,
						const char *path,
						struct fsal_export *exp_hdl)
{
	struct vfs_fsal_export *myself =
	    container_of(exp_hdl, struct vfs_fsal_export, export);
	struct vfs_fsal_obj_handle *hdl;

	hdl = vfs_sub_alloc_handle();
	if (hdl == NULL)
		return NULL;
	memcpy(hdl->handle, fh, sizeof(vfs_file_handle_t));
	hdl->obj_handle.type = posix2fsal_type(stat->st_mode);
	hdl->dev = posix2fsal_devt(stat->st_dev);
	hdl->up_ops = exp_hdl->up_ops;
	hdl->obj_handle.fs = fs;
	hdl->obj_handle.attrs = &hdl->attributes;

	if (hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd = -1;	/* no open on this yet */
		hdl->u.file.openflags = FSAL_O_CLOSED;
	} else if (hdl->obj_handle.type == SYMBOLIC_LINK) {
		ssize_t retlink;
		size_t len = stat->st_size + 1;
		char *link_content = gsh_malloc(len);

		if (link_content == NULL)
			goto spcerr;

		retlink =
		    vfs_readlink_by_handle(fh, dirfd, path, link_content, len);
		if (retlink < 0 || retlink == len)
			goto spcerr;
		link_content[retlink] = '\0';
		hdl->u.symlink.link_content = link_content;
		hdl->u.symlink.link_size = len;
	} else if (vfs_unopenable_type(hdl->obj_handle.type)) {
		/* AF_UNIX sockets, character special, and block
		   special files  require craziness */
		if (dir_fh == NULL) {
			int retval;

			vfs_alloc_handle(dir_fh);

			retval =
			    vfs_fd_to_handle(dirfd, hdl->obj_handle.fs, fh);

			if (retval < 0)
				goto spcerr;
		}
		hdl->u.unopenable.dir = gsh_malloc(sizeof(vfs_file_handle_t));
		if (hdl->u.unopenable.dir == NULL)
			goto spcerr;
		memcpy(hdl->u.unopenable.dir, dir_fh,
		       sizeof(vfs_file_handle_t));
		hdl->u.unopenable.name = gsh_strdup(path);
		if (hdl->u.unopenable.name == NULL)
			goto spcerr;
	}
	hdl->attributes.mask = exp_hdl->exp_ops.fs_supported_attrs(exp_hdl);
	posix2fsal_attributes(stat, &hdl->attributes);
	hdl->attributes.fsid = fs->fsid;
	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl,
			     posix2fsal_type(stat->st_mode));
	vfs_handle_ops_init(&hdl->obj_handle.obj_ops);
	if (vfs_sub_init_handle(myself, hdl, path) < 0)
		goto spcerr;

	return hdl;

 spcerr:
	if (hdl->obj_handle.type == SYMBOLIC_LINK) {
		if (hdl->u.symlink.link_content != NULL)
			gsh_free(hdl->u.symlink.link_content);
	} else if (vfs_unopenable_type(hdl->obj_handle.type)) {
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

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval, dirfd;
	struct stat stat;
	vfs_file_handle_t *fh = NULL;
	fsal_dev_t dev;
	struct fsal_filesystem *fs;
	bool xfsal = false;

	vfs_alloc_handle(fh);

	*handle = NULL;		/* poison it first */
	parent_hdl =
	    container_of(parent, struct vfs_fsal_obj_handle, obj_handle);
	if (!parent->obj_ops.handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	if (parent->fsal != parent->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 parent->fsal->name,
			 parent->fs->fsal != NULL
				? parent->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		goto hdlerr;
	}

	fs = parent->fs;
	dirfd = vfs_fsal_open(parent_hdl, O_PATH | O_NOACCESS, &fsal_error);

	if (dirfd < 0)
		return fsalstat(fsal_error, -dirfd);

	retval = fstatat(dirfd, path, &stat, AT_SYMLINK_NOFOLLOW);

	if (retval < 0) {
		retval = errno;
		goto direrr;
	}

	dev = posix2fsal_devt(stat.st_dev);

	if ((dev.minor != parent_hdl->dev.minor) ||
	    (dev.major != parent_hdl->dev.major)) {
		/* XDEV */
		fs = lookup_dev(&dev);
		if (fs == NULL) {
			LogDebug(COMPONENT_FSAL,
				 "Lookup of %s crosses filesystem boundary to unknown file system dev=%"
				 PRIu64".%"PRIu64,
				 path, dev.major, dev.minor);
			retval = EXDEV;
			goto direrr;
		}

		if (fs->fsal != parent->fsal) {
			xfsal = true;
			LogDebug(COMPONENT_FSAL,
				 "Lookup of %s crosses filesystem boundary to file system %s into FSAL %s",
				 path, fs->path,
				 fs->fsal != NULL
					? fs->fsal->name
					: "(none)");
		} else {
			LogDebug(COMPONENT_FSAL,
				 "Lookup of %s crosses filesystem boundary to file system %s",
				 path, fs->path);
		}
	}

	if (xfsal || vfs_name_to_handle(dirfd, fs, path, fh) < 0) {
		retval = errno;
		if (((retval == ENOTTY) ||
		     (retval == EOPNOTSUPP) ||
		     (retval == ENOTSUP) ||
		     xfsal) &&
		    (fs != parent->fs)) {
			/* Crossed device into territory not handled by
			 * this FSAL (XFS or VFS). Need to invent a handle.
			 * The made up handle will be JUST the fsid, we
			 * do not expect to see the handle on the wire, and
			 * this handle will not be valid for any form of this
			 * FSAL.
			 */
			LogDebug(COMPONENT_FSAL,
				 "vfs_name_to_handle %s, inventing FSAL %s handle for FSAL %s filesystem %s",
				 xfsal ? "skipped" : "failed",
				 parent->fsal->name,
				 fs->fsal != NULL
					? fs->fsal->name
					: "(none)",
				 path);

			retval = vfs_encode_dummy_handle(fh, fs);

			if (retval < 0) {
				retval = errno;
				goto direrr;
			}

			retval = 0;
		} else {
			/* Some other error */
			goto direrr;
		}
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dirfd, fh, fs, &stat, parent_hdl->handle, path,
			   op_ctx->fsal_export);
	close(dirfd);
	if (hdl == NULL) {
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

static inline int make_file_safe(struct vfs_fsal_obj_handle *dir_hdl,
				 const struct req_op_context *opctx,
				 int dir_fd, const char *name, mode_t unix_mode,
				 uid_t user, gid_t group,
				 struct vfs_fsal_obj_handle **hdl)
{
	int retval;
	struct stat stat;
	vfs_file_handle_t *fh;

	vfs_alloc_handle(fh);

	retval = fchownat(dir_fd, name, user, group, AT_SYMLINK_NOFOLLOW);
	if (retval < 0)
		goto fileerr;

	/* now that it is owned properly, set accessible mode */

	retval = fchmodat(dir_fd, name, unix_mode, 0);
	if (retval < 0)
		goto fileerr;
	retval = vfs_name_to_handle(dir_fd, dir_hdl->obj_handle.fs, name, fh);
	if (retval < 0)
		goto fileerr;
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if (retval < 0)
		goto fileerr;

	/* allocate an obj_handle and fill it up */
	*hdl = alloc_handle(dir_fd, fh, dir_hdl->obj_handle.fs, &stat,
			    dir_hdl->handle, name, opctx->fsal_export);
	if (*hdl == NULL)
		return ENOMEM;
	return 0;

 fileerr:
	retval = errno;
	return retval;
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
			    const char *name, struct attrlist *attrib,
			    struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int fd, dir_fd;
	struct stat stat;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */

	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

#ifdef ENABLE_VFS_DEBUG_ACL
	status.major = fsal_inherit_acls(attrib, dir_hdl->attrs->acl,
				       FSAL_ACE_FLAG_FILE_INHERIT);
	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */

	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (dir_hdl->fsal != dir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 dir_hdl->fsal->name,
			 dir_hdl->fs->fsal != NULL
				? dir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		goto hdlerr;
	}

#ifdef ENABLE_VFS_DEBUG_ACL
	access_type = FSAL_MODE_MASK_SET(FSAL_W_OK) |
		FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);
	status = fsal_test_access(dir_hdl, access_type, NULL, NULL);
	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */

	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	dir_fd = vfs_fsal_open(myself, flags, &status.major);
	if (dir_fd < 0)
		return fsalstat(status.major, -dir_fd);
	/** @todo: not sure what this accomplishes... */
	retval = vfs_stat_by_handle(dir_fd, &stat);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}
	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(op_ctx->creds);
	fd = openat(dir_fd, name, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL,
		    unix_mode);
	if (fd < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval = vfs_name_to_handle(dir_fd, dir_hdl->fs, name, fh);
	if (retval < 0) {
		retval = errno;
		goto fileerr;
	}
	retval = fstat(fd, &stat);
	if (retval < 0) {
		retval = errno;
		goto fileerr;
	}
	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, dir_hdl->fs, &stat, myself->handle, name,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;
	close(dir_fd);
	close(fd);

	status.major = ERR_FSAL_NO_ERROR;
#ifdef ENABLE_VFS_DEBUG_ACL
	status = (*handle)->obj_ops.setattrs(*handle, attrib);
	if (FSAL_IS_ERROR(status)) {
		/* Release the handle we just allocated. */
		(*handle)->obj_ops.release(*handle);
		*handle = NULL;
	}
#endif /* ENABLE_VFS_DEBUG_ACL */
	return status;

 fileerr:
	close(fd);
	unlinkat(dir_fd, name, 0);
 direrr:
	close(dir_fd);
 hdlerr:
	status.major = posix2fsal_error(retval);
	return fsalstat(status.major, retval);
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd;
	struct stat stat;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (dir_hdl->fsal != dir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 dir_hdl->fsal->name,
			 dir_hdl->fs->fsal != NULL
				? dir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		goto hdlerr;
	}

#ifdef ENABLE_VFS_DEBUG_ACL
	access_type = FSAL_MODE_MASK_SET(FSAL_W_OK) |
		FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_SUBDIRECTORY);
	status = fsal_test_access(dir_hdl, access_type, NULL, NULL);
	if (FSAL_IS_ERROR(status))
		return status;

	status.major = fsal_inherit_acls(attrib, dir_hdl->attrs->acl,
					 FSAL_ACE_FLAG_DIR_INHERIT);
	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */


	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	dir_fd = vfs_fsal_open(myself, flags, &status.major);
	if (dir_fd < 0)
		return fsalstat(status.major, -dir_fd);
	retval = vfs_stat_by_handle(dir_fd, &stat);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}
	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(op_ctx->creds);
	retval = mkdirat(dir_fd, name, unix_mode);
	if (retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval =  vfs_name_to_handle(dir_fd, dir_hdl->fs, name, fh);
	if (retval < 0) {
		retval = errno;
		goto fileerr;
	}
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if (retval < 0) {
		retval = errno;
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, dir_hdl->fs, &stat,
			   myself->handle, name,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;

	close(dir_fd);
	status.major = ERR_FSAL_NO_ERROR;
#ifdef ENABLE_VFS_DEBUG_ACL
	status = (*handle)->obj_ops.setattrs(*handle, attrib);
	if (FSAL_IS_ERROR(status)) {
		/* Release the handle we just allocated. */
		(*handle)->obj_ops.release(*handle);
		*handle = NULL;
	}
#endif /* ENABLE_VFS_DEBUG_ACL */
	return status;

 fileerr:
	unlinkat(dir_fd, name, 0);
 direrr:
	close(dir_fd);
 hdlerr:
	status.major = posix2fsal_error(retval);
	return fsalstat(status.major, retval);
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name,
			      object_file_type_t nodetype,	/* IN */
			      fsal_dev_t *dev,	/* IN */
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	mode_t unix_mode, create_mode = 0;
	fsal_status_t status = {0, 0};
	int retval = 0;
	uid_t user;
	gid_t group;
	dev_t unix_dev = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

#ifdef ENABLE_VFS_DEBUG_ACL
	status.major = fsal_inherit_acls(attrib, dir_hdl->attrs->acl,
				       FSAL_ACE_FLAG_FILE_INHERIT);
	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */

	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (dir_hdl->fsal != dir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 dir_hdl->fsal->name,
			 dir_hdl->fs->fsal != NULL
				? dir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		goto hdlerr;
	}

#ifdef ENABLE_VFS_DEBUG_ACL
	access_type = FSAL_MODE_MASK_SET(FSAL_W_OK) |
		FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);
	status = fsal_test_access(dir_hdl, access_type, NULL, NULL);
	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */

	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	switch (nodetype) {
	case BLOCK_FILE:
		create_mode = S_IFBLK;
		unix_dev = makedev(dev->major, dev->minor);
		break;
	case CHARACTER_FILE:
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
		LogMajor(COMPONENT_FSAL, "Invalid node type in FSAL_mknode: %d",
			 nodetype);
		status.major = ERR_FSAL_INVAL;
		goto errout;
	}
	dir_fd = vfs_fsal_open(myself, flags, &status.major);
	if (dir_fd < 0)
		goto errout;
	retval = vfs_stat_by_handle(dir_fd, &stat);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}
	if (stat.st_mode & S_ISGID)
		group = -1;  /*setgid bit on dir propagates dir group owner */

	/* create it with no access because we are root when we do this */
	fsal_set_credentials(op_ctx->creds);
	retval = mknodat(dir_fd, name, create_mode, unix_dev);
	if (retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval = make_file_safe(myself, op_ctx, dir_fd, name,
				unix_mode, user, group, &hdl);
	if (!retval) {
		close(dir_fd);	/* done with parent */
		*handle = &hdl->obj_handle;
		status.major = ERR_FSAL_NO_ERROR;
#ifdef ENABLE_VFS_DEBUG_ACL
		status = (*handle)->obj_ops.setattrs(*handle, attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			(*handle)->obj_ops.release(*handle);
			*handle = NULL;
		}
#endif /* ENABLE_VFS_DEBUG_ACL */
		return status;
	}

	unlinkat(dir_fd, name, 0);

 direrr:
	close(dir_fd);		/* done with parent */

 hdlerr:
	status.major = posix2fsal_error(retval);
 errout:
	return fsalstat(status.major, retval);
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
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it first */
	if (!dir_hdl->obj_ops.handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (dir_hdl->fsal != dir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 dir_hdl->fsal->name,
			 dir_hdl->fs->fsal != NULL
				? dir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		goto hdlerr;
	}

#ifdef ENABLE_VFS_DEBUG_ACL
	access_type = FSAL_MODE_MASK_SET(FSAL_W_OK) |
		FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);
	status = fsal_test_access(dir_hdl, access_type, NULL, NULL);
	if (FSAL_IS_ERROR(status))
		return status;

	status.major = fsal_inherit_acls(attrib, dir_hdl->attrs->acl,
				       FSAL_ACE_FLAG_FILE_INHERIT);
	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */

	dir_fd = vfs_fsal_open(myself, flags, &status.major);
	if (dir_fd < 0)
		return fsalstat(status.major, -dir_fd);
	flags |= O_NOFOLLOW;	/* BSD needs O_NOFOLLOW for
				 * fhopen() of symlinks */
	retval = vfs_stat_by_handle(dir_fd, &stat);
	if (retval < 0) {
		retval = errno;
		goto direrr;
	}
	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(op_ctx->creds);
	retval = symlinkat(link_path, dir_fd, name);
	if (retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval = vfs_name_to_handle(dir_fd, dir_hdl->fs, name, fh);
	if (retval < 0) {
		retval = errno;
		goto linkerr;
	}
	/* now get attributes info,
	 * being careful to get the link, not the target */
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if (retval < 0) {
		retval = errno;
		goto linkerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, dir_hdl->fs, &stat, NULL, name,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		retval = ENOMEM;
		goto linkerr;
	}
	*handle = &hdl->obj_handle;

	close(dir_fd);
	status.major = ERR_FSAL_NO_ERROR;
#ifdef ENABLE_VFS_DEBUG_ACL
	status = (*handle)->obj_ops.setattrs(*handle, attrib);
	if (FSAL_IS_ERROR(status)) {
		/* Release the handle we just allocated. */
		(*handle)->obj_ops.release(*handle);
		*handle = NULL;
	}
#endif /* ENABLE_VFS_DEBUG_ACL */
	return status;

 linkerr:
	unlinkat(dir_fd, name, 0);

 direrr:
	close(dir_fd);
 hdlerr:
	if (retval == ENOENT)
		status.major = ERR_FSAL_STALE;
	else
		status.major = posix2fsal_error(retval);
	return fsalstat(status.major, retval);
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	struct vfs_fsal_obj_handle *myself = NULL;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if (obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_INVAL;
		goto out;
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name,
			 obj_hdl->fs->fsal != NULL
				? obj_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		goto hdlerr;
	}
	if (refresh) {		/* lazy load or LRU'd storage */
		retval = vfs_readlink(myself, &fsal_error);
		if (retval < 0) {
			retval = -retval;
			goto hdlerr;
		}
	}
	if (myself->u.symlink.link_content == NULL) {
		fsal_error = ERR_FSAL_FAULT;	/* probably a better error?? */
		goto out;
	}

	link_content->len = myself->u.symlink.link_size;
	link_content->addr = gsh_malloc(myself->u.symlink.link_size);
	if (link_content->addr == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto out;
	}
	memcpy(link_content->addr, myself->u.symlink.link_content,
	       link_content->len);

 hdlerr:
	fsal_error = posix2fsal_error(retval);
 out:
	return fsalstat(fsal_error, retval);
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	struct vfs_fsal_obj_handle *myself, *destdir;
	int srcfd, destdirfd;
	int retval = 0;
	int flags = O_PATH | O_NOACCESS | O_NOFOLLOW;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	LogFullDebug(COMPONENT_FSAL, "link to %s", name);

	if (!op_ctx->fsal_export->exp_ops.
	    fs_supports(op_ctx->fsal_export, fso_link_support)) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name,
			 obj_hdl->fs->fsal != NULL
				? obj_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}

	/* Take read lock on object to protect file descriptor.
	 * We only take a read lock because we are not changing the state of
	 * the file descriptor.
	 */
	PTHREAD_RWLOCK_rdlock(&obj_hdl->lock);

	if (obj_hdl->type == REGULAR_FILE &&
	    myself->u.file.openflags != FSAL_O_CLOSED) {
		srcfd = myself->u.file.fd;
	} else {
		srcfd = vfs_fsal_open(myself, flags, &fsal_error);
		if (srcfd < 0) {
			retval = -srcfd;
			fsal_error = posix2fsal_error(retval);
			LogDebug(COMPONENT_FSAL,
				 "open myself returned %d", retval);
			goto out_unlock;
		}
	}

	destdir =
	    container_of(destdir_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (destdir_hdl->fsal != destdir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 destdir_hdl->fsal->name,
			 destdir_hdl->fs->fsal != NULL
				? destdir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		goto fileerr;
	}

	destdirfd = vfs_fsal_open(destdir, flags, &fsal_error);

	if (destdirfd < 0) {
		retval = destdirfd;
		fsal_error = posix2fsal_error(retval);
		LogDebug(COMPONENT_FSAL,
			 "open destdir returned %d", retval);
		goto fileerr;
	}

	retval = vfs_link_by_handle(myself->handle, srcfd, destdirfd, name);

	if (retval < 0) {
		retval = errno;
		LogFullDebug(COMPONENT_FSAL,
			     "link returned %d", retval);
		fsal_error = posix2fsal_error(retval);
	}

	close(destdirfd);

 fileerr:
	if (!(obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0))
		close(srcfd);

 out_unlock:

	PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

 out:
	LogFullDebug(COMPONENT_FSAL, "returning %d, %d", fsal_error, retval);
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
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, bool *eof)
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

	if (whence != NULL)
		seekloc = (off_t) *whence;
	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (dir_hdl->fsal != dir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 dir_hdl->fsal->name,
			 dir_hdl->fs->fsal != NULL
				? dir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	dirfd = vfs_fsal_open(myself, O_RDONLY | O_DIRECTORY, &fsal_error);
	if (dirfd < 0) {
		retval = -dirfd;
		goto out;
	}
	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if (seekloc < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto done;
	}

	do {
		baseloc = seekloc;
		nread = vfs_readents(dirfd, buf, BUF_SIZE, &seekloc);
		if (nread < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto done;
		}
		if (nread == 0)
			break;
		for (bpos = 0; bpos < nread;) {
			if (!to_vfs_dirent(buf, bpos, dentryp, baseloc)
			    || strcmp(dentryp->vd_name, ".") == 0
			    || strcmp(dentryp->vd_name, "..") == 0)
				goto skip;	/* must skip '.' and '..' */

			/* callback to cache inode */
			if (!cb(dentryp->vd_name, dir_state,
				(fsal_cookie_t) dentryp->vd_offset)) {
				goto done;
			}
 skip:
			bpos += dentryp->vd_reclen;
		}
	} while (nread > 0);

	*eof = true;
 done:
	close(dirfd);

 out:
	return fsalstat(fsal_error, retval);
}

static fsal_status_t renamefile(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	struct vfs_fsal_obj_handle *olddir, *newdir, *obj;
	int oldfd = -1, newfd = -1;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	olddir =
	    container_of(olddir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (olddir_hdl->fsal != olddir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 olddir_hdl->fsal->name,
			 olddir_hdl->fs->fsal != NULL
				? olddir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	oldfd = vfs_fsal_open(olddir, O_PATH | O_NOACCESS, &fsal_error);
	if (oldfd < 0) {
		retval = -oldfd;
		goto out;
	}
	newdir =
	    container_of(newdir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (newdir_hdl->fsal != newdir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 newdir_hdl->fsal->name,
			 newdir_hdl->fs->fsal != NULL
				? newdir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	obj = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	newfd = vfs_fsal_open(newdir, O_PATH | O_NOACCESS, &fsal_error);
	if (newfd < 0) {
		retval = -newfd;
		goto out;
	}
	/* Become the user because we are creating/removing objects
	 * in these dirs which messes with quotas and perms.
	 */
	fsal_set_credentials(op_ctx->creds);
	retval = renameat(oldfd, old_name, newfd, new_name);
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	} else if (vfs_unopenable_type(obj->obj_handle.type)) {
		/* A block, char, or socket has been renamed. Fixup
		 * our information in the handle so we can still stat it.
		 * Save the name in case we have to sort of undo.
		 */
		char *saved_name = obj->u.unopenable.name;

		memcpy(obj->u.unopenable.dir, newdir->handle,
		       sizeof(vfs_file_handle_t));
		obj->u.unopenable.name = gsh_strdup(new_name);
		if (obj->u.unopenable.name != NULL) {
			/* Discard saved_name */
			gsh_free(saved_name);
		} else {
			/* It's a bad day, we're going to be messed up
			 * no matter what we try and do, maybe leave
			 * things not completely hosed.
			 */
			LogCrit(COMPONENT_FSAL,
				"Failed to allocate memory to rename special inode from %s to %s",
				old_name, new_name);
			obj->u.unopenable.name = saved_name;
		}
	}
	fsal_restore_ganesha_credentials();
 out:
	if (oldfd >= 0)
		close(oldfd);
	if (newfd >= 0)
		close(newfd);
	return fsalstat(fsal_error, retval);
}

/**
 * @brief Open file and get attributes.
 *
 * This function opens a file and returns the file descriptor and fetches
 * the attributes. The obj_hdl->lock MUST be held for this call.
 *
 * @param[in]     exp         The fsal_export the file belongs to
 * @param[in]     myself      File to access
 * @param[in/out] stat        The struct stat to fetch attributes into
 * @param[in]     open_flags  The mode to open the file in
 * @param[out]    fsal_error  Place to return an error
 *
 * @return The file descriptor plus indication if it needs to be closed.
 *
 */
static struct closefd vfs_fsal_open_and_stat(struct fsal_export *exp,
					     struct vfs_fsal_obj_handle *myself,
					     struct stat *stat,
					     fsal_openflags_t flags,
					     fsal_errors_t *fsal_error)
{
	struct fsal_obj_handle *obj_hdl = &myself->obj_handle;
	struct closefd cfd = { .fd = -1, .close_fd = false };
	int retval = 0;
	const char *func = "unknown";
	struct vfs_filesystem *vfs_fs = myself->obj_handle.fs->private;
	int open_flags;

	fsal2posix_openflags(flags, &open_flags);

	switch (obj_hdl->type) {
	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
		cfd.fd = vfs_open_by_handle(vfs_fs,
					    myself->u.unopenable.dir,
					    O_PATH | O_NOACCESS,
					    fsal_error);
		if (cfd.fd < 0) {
			LogDebug(COMPONENT_FSAL,
				 "Failed with %s open_flags 0x%08x",
				 strerror(-cfd.fd), O_PATH | O_NOACCESS);
			return cfd;
		}
		cfd.close_fd = true;
		retval =
		    fstatat(cfd.fd, myself->u.unopenable.name, stat,
			    AT_SYMLINK_NOFOLLOW);

		func = "fstatat";
		break;
	case REGULAR_FILE:
		/* Check if the file descriptor happens to be in a compatible
		 * mode. If not, open a temporary file descriptor.
		 *
		 * Note that FSAL_O_REOPEN will never be set in
		 * myself->u.file.openflags and thus forces a re-open.
		 */
		if (((flags & FSAL_O_ANY) != 0 &&
		     (myself->u.file.openflags & FSAL_O_RDWR) == 0) ||
		    ((myself->u.file.openflags & flags) != flags)) {
			/* no file open at the moment */
			cfd.fd = vfs_fsal_open(myself, open_flags, fsal_error);
			if (cfd.fd < 0) {
				LogDebug(COMPONENT_FSAL,
					 "Failed with %s open_flags 0x%08x",
					 strerror(-cfd.fd), open_flags);
				return cfd;
			}
			cfd.close_fd = true;
		} else {
			cfd.fd = myself->u.file.fd;
		}
		retval = fstat(cfd.fd, stat);
		func = "fstat";
		break;
	case SYMBOLIC_LINK:
		open_flags |= (O_PATH | O_RDWR | O_NOFOLLOW);
		goto vfos_open;
	case FIFO_FILE:
		open_flags |= O_NONBLOCK;
		/* fall through */
	case DIRECTORY:
	default:
 vfos_open:
		cfd.fd = vfs_fsal_open(myself, open_flags, fsal_error);
		if (cfd.fd < 0) {
			LogDebug(COMPONENT_FSAL,
				 "Failed with %s open_flags 0x%08x",
				 strerror(-cfd.fd), open_flags);
			return cfd;
		}
		cfd.close_fd = true;
		retval = vfs_stat_by_handle(cfd.fd, stat);
		func = "vfs_stat_by_handle";
		break;
	}

	if (retval < 0) {
		retval = errno;
		if (cfd.close_fd) {
			int rc;

			rc = close(cfd.fd);

			if (rc < 0) {
				rc = errno;
				LogDebug(COMPONENT_FSAL, "close failed with %s",
					 strerror(rc));
			}
		}
		if (retval == ENOENT)
			retval = ESTALE;
		*fsal_error = posix2fsal_error(retval);
		LogDebug(COMPONENT_FSAL, "%s failed with %s", func,
			 strerror(retval));
		cfd.fd = -retval;
		cfd.close_fd = false;
		return cfd;
	}
	return cfd;
}

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;
	struct closefd cfd = { .fd = -1, .close_fd = false };
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t st;
	int retval = 0;
	attrmask_t request_mask;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s getattr for handle belonging to FSAL %s, ignoring",
			 obj_hdl->fsal->name,
			 obj_hdl->fs->fsal != NULL
				? obj_hdl->fs->fsal->name
				: "(none)");
		goto out;
	}

	/* Take read lock on object to protect file descriptor.
	 * We only take a read lock because we are not changing the state of
	 * the file descriptor. If the file is not already open for read,
	 * then we will get a temporary file descriptor.
	 */
	PTHREAD_RWLOCK_rdlock(&obj_hdl->lock);

	cfd = vfs_fsal_open_and_stat(op_ctx->fsal_export, myself, &stat,
				     FSAL_O_ANY, &fsal_error);
	if (cfd.fd >= 0) {
		request_mask = myself->attributes.mask;
		posix2fsal_attributes(&stat, &myself->attributes);
		myself->attributes.fsid = obj_hdl->fs->fsid;
		if (myself->sub_ops && myself->sub_ops->getattrs) {
			st = myself->sub_ops->getattrs(myself, cfd.fd,
						       request_mask);
			if (FSAL_IS_ERROR(st)) {
				FSAL_CLEAR_MASK(myself->attributes.mask);
				FSAL_SET_MASK(myself->attributes.mask,
						ATTR_RDATTR_ERR);
				fsal_error = st.major;
				retval = st.minor;
			}
		}
		if (cfd.close_fd)
			close(cfd.fd);
	} else {
		LogDebug(COMPONENT_FSAL, "Failed with %s, fsal_error %s",
			 strerror(-cfd.fd),
			 fsal_error ==
			 ERR_FSAL_STALE ? "ERR_FSAL_STALE" : "other");
		if (obj_hdl->type == SYMBOLIC_LINK
		    && cfd.fd == -EPERM) {
			/* You cannot open_by_handle (XFS on linux) a symlink
			 * and it throws an EPERM error for it.
			 * open_by_handle_at does not throw that error for
			 * symlinks so we play a game here.  Since there is
			 * not much we can do with symlinks anyway,
			 * say that we did it but don't actually
			 * do anything.  In this case, return the stat we got
			 * at lookup time.  If you *really* want to tweek things
			 * like owners, get a modern linux kernel...
			 */
			fsal_error = ERR_FSAL_NO_ERROR;
			goto out_unlock;
		}
		retval = -cfd.fd;
	}

 out_unlock:

	PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

 out:
	return fsalstat(fsal_error, retval);
}

/*
 * NOTE: this is done under protection of the attributes rwlock
 * in the cache entry.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	struct vfs_fsal_obj_handle *myself;
	struct closefd cfd = { .fd = -1, .close_fd = false };
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
#ifdef ENABLE_RFC_ACL
	fsal_status_t fsal_status = {0, 0};
#endif /* ENABLE_RFC_ACL */
	int retval = 0;
	fsal_openflags_t open_flags = FSAL_O_ANY;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
		attrs->mode &= ~op_ctx->fsal_export->exp_ops.
			fs_umask(op_ctx->fsal_export);
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name,
			 obj_hdl->fs->fsal != NULL
				? obj_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		return fsalstat(fsal_error, retval);
	}

#ifdef ENABLE_RFC_ACL
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE) &&
	    !FSAL_TEST_MASK(attrs->mask, ATTR_ACL)) {
		/* Set ACL from MODE */
		fsal_status = fsal_mode_to_acl(attrs, myself->attributes.acl);
	} else {
		/* If ATTR_ACL is set, mode needs to be adjusted no matter what.
		 * See 7530 s 6.4.1.3 */
		if (!FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
			attrs->mode = myself->attributes.mode;
		fsal_status = fsal_acl_to_mode(attrs);
	}
	if (FSAL_IS_ERROR(fsal_status)) {
		fsal_error = fsal_status.major;
		retval = fsal_status.minor;
		goto hdlerr;
	}
#endif /* ENABLE_RFC_ACL */


	/* This is yet another "you can't get there from here".  If this object
	 * is a socket (AF_UNIX), an fd on the socket s useless _period_.
	 * If it is for a symlink, without O_PATH, you will get an ELOOP error
	 * and (f)chmod doesn't work for a symlink anyway - not that it matters
	 * because access checking is not done on the symlink but the final
	 * target.
	 * AF_UNIX sockets are also ozone material.  If the socket is already
	 * active listeners et al, you can manipulate the mode etc.  If it is
	 * just sitting there as in you made it with a mknod.
	 * (one of those leaky abstractions...)
	 * or the listener forgot to unlink it, it is lame duck.
	 */

	/* Test if size is being set, make sure file is regular and if so,
	 * require a read/write file descriptor.
	 */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE)
			return fsalstat(ERR_FSAL_INVAL, EINVAL);
		open_flags = FSAL_O_RDWR;
	}

	/* Take read lock on object to protect file descriptor.
	 * We only take a read lock because we are not changing the state of
	 * the file descriptor. If the file is not open for read (or read/write
	 * in the case of setting size) we will use a temporary file descriptor.
	 */
	PTHREAD_RWLOCK_rdlock(&obj_hdl->lock);

	cfd = vfs_fsal_open_and_stat(op_ctx->fsal_export, myself, &stat,
				     open_flags, &fsal_error);

	if (cfd.fd < 0) {
		if (obj_hdl->type == SYMBOLIC_LINK &&
		    cfd.fd == -EPERM) {
			/* You cannot open_by_handle (XFS) a symlink and it
			 * throws an EPERM error for it.  open_by_handle_at
			 * does not throw that error for symlinks so we play a
			 * game here.  Since there is not much we can do with
			 * symlinks anyway, say that we did it
			 * but don't actually do anything.
			 * If you *really* want to tweek things
			 * like owners, get a modern linux kernel...
			 */
			fsal_error = ERR_FSAL_NO_ERROR;
		} else {
			retval = -cfd.fd;
		}
		goto out_unlock;
	}

	/** TRUNCATE **/
	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		retval = ftruncate(cfd.fd, attrs->filesize);
		if (retval != 0) {
			/** @todo FSF: is this still necessary?
			 *
			 * XXX ESXi volume creation pattern reliably
			 * reached this point in the past, however now that we
			 * only use the already open file descriptor if it is
			 * open read/write, this may no longer fail.
			 * If there is some other error from ftruncate, then
			 * we will needlessly retry, but without more detail
			 * of the original failure, we can't be sure.
			 * Fortunately permission checking is done by
			 * Ganesha before calling here, so we won't get an
			 * EACCES since this call is done as root. We could
			 * get EFBIG, EPERM, or EINVAL.
			 */
			if (cfd.close_fd)
				close(cfd.fd);

			cfd = vfs_fsal_open_and_stat(op_ctx->fsal_export,
						     myself, &stat,
						     open_flags | FSAL_O_REOPEN,
						     &fsal_error);

			if (cfd.fd < 0) {
				retval = -cfd.fd;
				goto out_unlock;
			}

			retval = ftruncate(cfd.fd, attrs->filesize);
			if (retval != 0)
				goto fileerr;
		}
	}

	/** CHMOD **/
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		/* The POSIX chmod call doesn't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if (!S_ISLNK(stat.st_mode)) {
			if (vfs_unopenable_type(obj_hdl->type))
				retval = fchmodat(cfd.fd,
						  myself->u.unopenable.name,
						  fsal2unix_mode(attrs->mode),
						  0);
			else
				retval = fchmod(cfd.fd,
						fsal2unix_mode(attrs->mode));

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

		if (vfs_unopenable_type(obj_hdl->type))
			retval = fchownat(cfd.fd, myself->u.unopenable.name,
					  user, group, AT_SYMLINK_NOFOLLOW);
		else if (obj_hdl->type == SYMBOLIC_LINK)
			retval = fchownat(cfd.fd, "", user, group,
					  AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH);
		else
			retval = fchown(cfd.fd, user, group);

		if (retval)
			goto fileerr;
	}

	/**  UTIME  **/
	if (FSAL_TEST_MASK
	    (attrs->mask,
	     ATTR_ATIME | ATTR_MTIME | ATTR_ATIME_SERVER | ATTR_MTIME_SERVER)) {
		struct timespec timebuf[2];

		if (obj_hdl->type == SYMBOLIC_LINK)
			goto out; /* Setting time on symlinks is illegal */
		/* Atime */
		if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
			timebuf[0].tv_sec = 0;
			timebuf[0].tv_nsec = UTIME_NOW;
		} else if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
			timebuf[0] = attrs->atime;
		} else {
			timebuf[0].tv_sec = 0;
			timebuf[0].tv_nsec = UTIME_OMIT;
		}

		/* Mtime */
		if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
			timebuf[1].tv_sec = 0;
			timebuf[1].tv_nsec = UTIME_NOW;
		} else if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
			timebuf[1] = attrs->mtime;
		} else {
			timebuf[1].tv_sec = 0;
			timebuf[1].tv_nsec = UTIME_OMIT;
		}
		if (vfs_unopenable_type(obj_hdl->type))
			retval = vfs_utimesat(cfd.fd, myself->u.unopenable.name,
					      timebuf, AT_SYMLINK_NOFOLLOW);
		else
			retval = vfs_utimes(cfd.fd, timebuf);
		if (retval != 0)
			goto fileerr;
	}

	/** SUBFSAL **/
	if (myself->sub_ops && myself->sub_ops->setattrs) {
		fsal_status_t st;

		st = myself->sub_ops->setattrs(myself, cfd.fd, attrs->mask,
				attrs);
		if (FSAL_IS_ERROR(st)) {
			fsal_error = st.major;
			retval = st.minor;
			goto out;
		}
	}

	goto out;

 fileerr:
	retval = errno;
	fsal_error = posix2fsal_error(retval);
 out:
	if (cfd.close_fd)
		close(cfd.fd);

 out_unlock:

	PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	return fsalstat(fsal_error, retval);
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct stat stat;
	int fd;
	int retval = 0;

	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (dir_hdl->fsal != dir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 dir_hdl->fsal->name,
			 dir_hdl->fs->fsal != NULL
				? dir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	fd = vfs_fsal_open(myself, O_PATH | O_NOACCESS, &fsal_error);
	if (fd < 0) {
		retval = -fd;
		goto out;
	}
	retval = fstatat(fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if (retval < 0) {
		retval = errno;
		LogDebug(COMPONENT_FSAL, "fstatat %s failed %s", name,
			 strerror(retval));
		if (retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	fsal_set_credentials(op_ctx->creds);
	retval = unlinkat(fd, name, (S_ISDIR(stat.st_mode)) ? AT_REMOVEDIR : 0);
	if (retval < 0) {
		retval = errno;
		if (retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
	}
	fsal_restore_ganesha_credentials();

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

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		/* Log, but allow digest */
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s",
			 obj_hdl->fsal->name,
			 obj_hdl->fs->fsal != NULL
				? obj_hdl->fs->fsal->name
				: "(none)");
	}

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < myself->handle->handle_len) {
			LogMajor(COMPONENT_FSAL,
				 "Space too small for handle.  need %u, have %zu",
				 myself->handle->handle_len,
				 fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		}
		memcpy(fh_desc->addr,
		       myself->handle->handle_data,
		       myself->handle->handle_len);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	fh_desc->len = myself->handle->handle_len;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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

	fh_desc->addr = myself->handle->handle_data;
	fh_desc->len = myself->handle->handle_len;
}

/*
 * release
 * release our export first so they know we are gone
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;
	object_file_type_t type = obj_hdl->type;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (type == REGULAR_FILE) {
		fsal_status_t st = vfs_close(obj_hdl);

		if (FSAL_IS_ERROR(st)) {
			LogCrit(COMPONENT_FSAL,
				"Could not close hdl 0x%p, error %s(%d)",
				obj_hdl, strerror(st.minor), st.minor);
		}
	}

	fsal_obj_handle_fini(obj_hdl);

	if (type == SYMBOLIC_LINK) {
		if (myself->u.symlink.link_content != NULL)
			gsh_free(myself->u.symlink.link_content);
	} else if (vfs_unopenable_type(type)) {
		if (myself->u.unopenable.name != NULL)
			gsh_free(myself->u.unopenable.name);
		if (myself->u.unopenable.dir != NULL)
			gsh_free(myself->u.unopenable.dir);
	}

	gsh_free(myself);
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
			      const char *path, struct fsal_obj_handle **handle)
{
	int dir_fd = -1;
	struct stat stat;
	struct vfs_fsal_obj_handle *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct fsal_filesystem *fs;
	struct fsal_dev__ dev;
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	*handle = NULL;	/* poison it */

	dir_fd = open_dir_by_path_walk(-1, path, &stat);

	if (dir_fd < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not open directory for path %s",
			path);
		retval = -dir_fd;
		goto errout;
	}

	dev = posix2fsal_devt(stat.st_dev);
	fs = lookup_dev(&dev);

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

	retval = vfs_fd_to_handle(dir_fd, fs, fh);

	if (retval < 0) {
		retval = errno;
		LogCrit(COMPONENT_FSAL,
			"Could not get handle for path %s, error %s",
			path, strerror(retval));
		goto errout;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(-1, fh, fs, &stat, NULL, "", exp_hdl);

	if (hdl == NULL) {
		retval = ENOMEM;
		LogCrit(COMPONENT_FSAL,
			"Could not allocate handle for path %s",
			path);
		goto errout;
	}

	close(dir_fd);

	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	if (dir_fd >= 0)
		close(dir_fd);
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

fsal_status_t vfs_check_handle(struct fsal_export *exp_hdl,
			       struct gsh_buffdesc *hdl_desc,
			       struct fsal_filesystem **fs,
			       vfs_file_handle_t *fh,
			       bool *dummy)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct fsal_fsid__ fsid;
	enum fsid_type fsid_type;

	*fs = NULL;

	if (!vfs_valid_handle(hdl_desc))
		return fsalstat(ERR_FSAL_BADHANDLE, 0);

	memcpy(fh->handle_data, hdl_desc->addr, hdl_desc->len);
	fh->handle_len = hdl_desc->len;

	*dummy = vfs_is_dummy_handle(fh);

	retval = vfs_extract_fsid(fh, &fsid_type, &fsid);

	if (retval == 0) {
		*fs = lookup_fsid(&fsid, fsid_type);
		if (*fs == NULL) {
			LogInfo(COMPONENT_FSAL,
				"Could not map fsid=0x%016"PRIx64
				".0x%016"PRIx64" to filesytem",
				fsid.major, fsid.minor);
			retval = ESTALE;
			fsal_error = posix2fsal_error(retval);
			goto errout;
		}
		if (((*fs)->fsal != exp_hdl->fsal) && !(*dummy)) {
			LogInfo(COMPONENT_FSAL,
				"fsid=0x%016"PRIx64".0x%016"PRIx64
				" in handle not a %s filesystem",
				fsid.major, fsid.minor,
				exp_hdl->fsal->name);
			retval = ESTALE;
			fsal_error = posix2fsal_error(retval);
			goto errout;
		}

		LogDebug(COMPONENT_FSAL,
			 "Found filesystem %s for handle for FSAL %s",
			 (*fs)->path,
			 (*fs)->fsal != NULL ? (*fs)->fsal->name : "(none)");
	} else {
		LogDebug(COMPONENT_FSAL,
			 "Could not map handle to fsid");
		fsal_error = ERR_FSAL_BADHANDLE;
		goto errout;
	}

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
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle)
{
	fsal_status_t status;
	struct vfs_fsal_obj_handle *hdl;
	struct stat obj_stat;
	vfs_file_handle_t *fh = NULL;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int fd;
	int flags = O_PATH | O_NOACCESS | O_NOFOLLOW;
	struct fsal_filesystem *fs;
	bool dummy;

	vfs_alloc_handle(fh);
	*handle = NULL;		/* poison it first */

	status = vfs_check_handle(exp_hdl, hdl_desc, &fs, fh, &dummy);

	if (FSAL_IS_ERROR(status))
		return status;

	if (dummy) {
		/* We don't need fd here, just stat the fs->path */
		fd = -1;
		retval = stat(fs->path, &obj_stat);
	} else {
		fd = vfs_open_by_handle(fs->private, fh, flags, &fsal_error);

		if (fd < 0) {
			retval = -fd;
			goto errout;
		}

		retval = vfs_stat_by_handle(fd, &obj_stat);
	}

	/* Test the result of stat */
	if (retval != 0) {
		retval = errno;
		LogDebug(COMPONENT_FSAL,
			 "%s failed with %s",
			 dummy ? "stat" : "vfs_stat_by_handle",
			 strerror(retval));
		fsal_error = posix2fsal_error(retval);
		if (fd >= 0)
			close(fd);
		goto errout;
	}

	hdl = alloc_handle(fd, fh, fs, &obj_stat, NULL, "", exp_hdl);

	if (fd >= 0)
		close(fd);

	if (hdl == NULL) {
		LogDebug(COMPONENT_FSAL,
			 "Could not allocate handle");
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;

 errout:
	return fsalstat(fsal_error, retval);
}
