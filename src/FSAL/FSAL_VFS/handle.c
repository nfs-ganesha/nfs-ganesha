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

#ifdef LINUX
#include <sys/sysmacros.h> /* for makedev(3) */
#endif
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
#include "city.h"
#include "nfs_core.h"
#include "nfs_proto_tools.h"

/* helpers
 */

int vfs_fsal_open(struct vfs_fsal_obj_handle *hdl,
		  int openflags,
		  fsal_errors_t *fsal_error)
{
	struct vfs_filesystem *vfs_fs = hdl->obj_handle.fs->private_data;

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
struct vfs_fsal_obj_handle *alloc_handle(int dirfd,
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
	struct vfs_fsal_module *my_module = container_of(
			exp_hdl->fsal, struct vfs_fsal_module, module);

	hdl = vfs_sub_alloc_handle();

	memcpy(hdl->handle, fh, sizeof(vfs_file_handle_t));
	hdl->obj_handle.type = posix2fsal_type(stat->st_mode);
	hdl->dev = posix2fsal_devt(stat->st_dev);
	hdl->up_ops = exp_hdl->up_ops;
	hdl->obj_handle.fs = fs;

	LogFullDebug(COMPONENT_FSAL,
		     "Creating object %p for file %s of type %s",
		     hdl, path, object_file_type_to_str(hdl->obj_handle.type));

	if (hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd.fd = -1;	/* no open on this yet */
		hdl->u.file.fd.openflags = FSAL_O_CLOSED;
	} else if (hdl->obj_handle.type == DIRECTORY) {
		hdl->u.directory.path = NULL;
		hdl->u.directory.fs_location = NULL;
	} else if (hdl->obj_handle.type == SYMBOLIC_LINK) {
		ssize_t retlink;
		size_t len = stat->st_size + 1;
		char *link_content = gsh_malloc(len);

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

		memcpy(hdl->u.unopenable.dir, dir_fh,
		       sizeof(vfs_file_handle_t));
		hdl->u.unopenable.name = gsh_strdup(path);
	}
	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl,
			     posix2fsal_type(stat->st_mode));
	hdl->obj_handle.fsid = fs->fsid;
	hdl->obj_handle.fileid = stat->st_ino;
#ifdef VFS_NO_MDCACHE
	hdl->obj_handle.state_hdl = vfs_state_locate(&hdl->obj_handle);
#endif /* VFS_NO_MDCACHE */
	hdl->obj_handle.obj_ops = &my_module->handle_ops;
	if (vfs_sub_init_handle(myself, hdl, path) < 0)
		goto spcerr;

	return hdl;

 spcerr:
	if (hdl->obj_handle.type == SYMBOLIC_LINK) {
		gsh_free(hdl->u.symlink.link_content);
	} else if (vfs_unopenable_type(hdl->obj_handle.type)) {
		gsh_free(hdl->u.unopenable.name);
		gsh_free(hdl->u.unopenable.dir);
	}
	gsh_free(hdl);		/* elvis has left the building */
	return NULL;
}

static fsal_status_t lookup_with_fd(struct vfs_fsal_obj_handle *parent_hdl,
				    int dirfd, const char *path,
				    struct fsal_obj_handle **handle,
				    struct attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *hdl;
	int retval, fd;
	struct stat stat;
	vfs_file_handle_t *fh = NULL;
	fsal_dev_t dev;
	struct fsal_filesystem *fs;
	bool xfsal = false;
	fsal_status_t status;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	vfs_alloc_handle(fh);

	retval = fstatat(dirfd, path, &stat, AT_SYMLINK_NOFOLLOW);

	if (retval < 0) {
		retval = errno;
		LogDebug(COMPONENT_FSAL, "Failed to open stat %s: %s", path,
			 msg_fsal_err(posix2fsal_error(retval)));
		status = posix2fsal_status(retval);
		return status;
	}

	dev = posix2fsal_devt(stat.st_dev);

	fs = parent_hdl->obj_handle.fs;
	if ((dev.minor != parent_hdl->dev.minor) ||
	    (dev.major != parent_hdl->dev.major)) {
		/* XDEV */
		fs = lookup_dev(&dev);
		if (fs == NULL) {
			LogDebug(COMPONENT_FSAL,
				 "Lookup of %s crosses filesystem boundary to unknown file system dev=%"
				 PRIu64".%"PRIu64,
				 path, dev.major, dev.minor);
			status = fsalstat(ERR_FSAL_XDEV, EXDEV);
			return status;
		}

		if (fs->fsal != parent_hdl->obj_handle.fsal) {
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
		    (fs != parent_hdl->obj_handle.fs)) {
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
				 parent_hdl->obj_handle.fsal->name,
				 fs->fsal != NULL
					? fs->fsal->name
					: "(none)",
				 path);

			retval = vfs_encode_dummy_handle(fh, fs);

			if (retval < 0) {
				retval = errno;
				status = posix2fsal_status(retval);
				return status;
			}

			retval = 0;
		} else {
			/* Some other error */
			status = posix2fsal_status(retval);
			return status;
		}
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dirfd, fh, fs, &stat, parent_hdl->handle, path,
			   op_ctx->fsal_export);

	if (hdl == NULL) {
		status = fsalstat(ERR_FSAL_NOMEM, ENOMEM);
		return status;
	}

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&stat, attrs_out);
	}

	/* if it is a directory and the sticky bit is set
	 * let's look for referral information
	 */

	if (hdl->obj_handle.type == DIRECTORY &&
	    attrs_out != NULL &&
	    is_sticky_bit_set(&hdl->obj_handle, attrs_out) &&
	    hdl->obj_handle.fs->private_data != NULL) {

		caddr_t xattr_content;
		size_t attrsize = 0;
		char proclnk[MAXPATHLEN];
		char readlink_buf[MAXPATHLEN];
		char *spath, *fspath;
		ssize_t r;
		uint64 hash;
		fsal_status_t st;

		struct vfs_filesystem *vfs_fs =
			hdl->obj_handle.fs->private_data;

		/* the real path of the referral directory is needed.
		 * it get's stored in u.directory.path
		 */

		fd = vfs_fsal_open(hdl, O_DIRECTORY, &fsal_error);
		if (fd < 0) {
			return fsalstat(fsal_error, -fd);
		}

		sprintf(proclnk, "/proc/self/fd/%d", fd);
		r = readlink(proclnk, readlink_buf, MAXPATHLEN - 1);
		if (r < 0) {
			fsal_error = posix2fsal_error(errno);
			r = errno;
			LogEvent(COMPONENT_FSAL, "failed to readlink");
			close(fd);
			return fsalstat(fsal_error, r);
		}
		readlink_buf[r] = '\0';
		LogDebug(COMPONENT_FSAL, "fd -> path: %d -> %s",
			 fd, readlink_buf);
		if (hdl->u.directory.path != NULL) {
			LogFullDebug(COMPONENT_FSAL,
				     "freeing old directory.path: %s",
				     hdl->u.directory.path);
			gsh_free(hdl->u.directory.path);
		}

		fspath = vfs_fs->fs->path;

		spath = readlink_buf;

		/* If Path and Pseudo path are not equal replace path with
		 * pseudo path.
		 */
		if (strcmp(op_ctx->ctx_export->fullpath,
			op_ctx->ctx_export->pseudopath) != 0) {
			int pseudo_length = strlen(
				op_ctx->ctx_export->pseudopath);
			int fullpath_length = strlen(
				op_ctx->ctx_export->fullpath);
			char *dirpath = spath + fullpath_length;

			memcpy(proclnk, op_ctx->ctx_export->pseudopath,
				pseudo_length);
			memcpy(proclnk + pseudo_length, dirpath,
				r - fullpath_length);
			proclnk[pseudo_length + (r - fullpath_length)] = '\0';
			spath = proclnk;
		} else if (strncmp(path, fspath, strlen(fspath)) == 0) {
			spath += strlen(fspath);
		}
		hdl->u.directory.path = gsh_strdup(spath);

		/* referral configuration is in a xattr "user.fs_location"
		 * on the directory in the form
		 * server:/path/to/referred/directory.
		 * It gets storeded in u.directory.fs_location
		 */

		xattr_content = gsh_calloc(XATTR_BUFFERSIZE, sizeof(char));

		st = vfs_getextattr_value_by_name((struct fsal_obj_handle *)hdl,
						  "user.fs_location",
						  xattr_content,
						  XATTR_BUFFERSIZE,
						  &attrsize);

		if (!FSAL_IS_ERROR(st)) {
			LogDebug(COMPONENT_FSAL, "user.fs_location: %s",
				 xattr_content);
			if (hdl->u.directory.fs_location != NULL) {
				LogFullDebug(COMPONENT_FSAL,
					     "freeing old directory.fs_location: %s",
					     hdl->u.directory.fs_location);
				gsh_free(hdl->u.directory.fs_location);
			}
			hdl->u.directory.fs_location =
				gsh_strdup(xattr_content);

			/* on a referral the filesystem id has to change
			 * it get's calculated via a hash from the referral
			 * and stored in the fsid object of the fsal_obj_handle
			 */

			hash = CityHash64(xattr_content, attrsize);
			hdl->obj_handle.fsid.major = hash;
			hdl->obj_handle.fsid.minor = hash;
			LogDebug(COMPONENT_NFS_V4,
				 "fsid.major = %"PRIu64", fsid.minor = %"PRIu64,
				 hdl->obj_handle.fsid.major,
				 hdl->obj_handle.fsid.minor);
		}
		gsh_free(xattr_content);
		close(fd);

	} else {
		/* reset fsid if the sticky bit is not set,
		 * because a referral was removed
		 */

		hdl->obj_handle.fsid = hdl->obj_handle.fs->fsid;
	}

	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle,
			    struct attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *parent_hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int dirfd;
	fsal_status_t status;

	*handle = NULL;		/* poison it first */
	parent_hdl =
	    container_of(parent, struct vfs_fsal_obj_handle, obj_handle);
	if (!parent->obj_ops->handle_is(parent, DIRECTORY)) {
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
		return fsalstat(ERR_FSAL_XDEV, EXDEV);
	}

	dirfd = vfs_fsal_open(parent_hdl, O_PATH | O_NOACCESS, &fsal_error);

	if (dirfd < 0) {
		LogDebug(COMPONENT_FSAL, "Failed to open parent: %s",
			 msg_fsal_err(fsal_error));
		status = fsalstat(fsal_error, -dirfd);
		return status;
	}

	status = lookup_with_fd(parent_hdl, dirfd, path, handle, attrs_out);


	close(dirfd);
	return status;
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attrib,
			     struct fsal_obj_handle **handle,
			     struct attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd;
	struct stat stat;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	struct attrlist attrs;
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops->handle_is(dir_hdl, DIRECTORY)) {
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
	status = dir_hdl->obj_ops->test_access(dir_hdl, access_type, NULL, NULL,
					      false);
	if (FSAL_IS_ERROR(status))
		return status;

	fsal_prepare_attrs(&attrs, ATTR_ACL);

	status = dir_hdl->obj_ops->getattrs(dir_hdl, &attrs);

	if (FSAL_IS_ERROR(status))
		return status;

	status.major = fsal_inherit_acls(attrib, attrs.acl,
					 FSAL_ACE_FLAG_DIR_INHERIT);

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */


	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	dir_fd = vfs_fsal_open(myself, flags, &status.major);
	if (dir_fd < 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "vfs_fsal_open returned %s",
			     strerror(-dir_fd));
		return fsalstat(status.major, -dir_fd);
	}

	retval = vfs_stat_by_handle(dir_fd, &stat);
	if (retval < 0) {
		retval = errno;
		LogFullDebug(COMPONENT_FSAL,
			     "vfs_stat_by_handle returned %s",
			     strerror(retval));
		status = posix2fsal_status(retval);
		goto direrr;
	}
	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(op_ctx->creds);
	retval = mkdirat(dir_fd, name, unix_mode);
	if (retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		LogFullDebug(COMPONENT_FSAL,
			     "mkdirat returned %s",
			     strerror(retval));
		status = posix2fsal_status(retval);
		goto direrr;
	}
	fsal_restore_ganesha_credentials();
	retval =  vfs_name_to_handle(dir_fd, dir_hdl->fs, name, fh);
	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto fileerr;
	}
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);
	if (retval < 0) {
		retval = errno;
		LogFullDebug(COMPONENT_FSAL,
			     "fstatat returned %s",
			     strerror(retval));
		status = posix2fsal_status(retval);
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, dir_hdl->fs, &stat,
			   myself->handle, name,
			   op_ctx->fsal_export);
	if (hdl == NULL) {
		LogFullDebug(COMPONENT_FSAL,
			     "alloc_handle returned %s",
			     strerror(retval));
		status = fsalstat(ERR_FSAL_NOMEM, ENOMEM);
		goto fileerr;
	}

	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->valid_mask, ATTR_MODE);

	if (attrib->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops->setattr2(*handle, false, NULL,
						     attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*handle)->obj_ops->release(*handle);
			*handle = NULL;
		} else if (attrs_out != NULL) {
			status = (*handle)->obj_ops->getattrs(*handle,
							     attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes.
				 */
				goto fileerr;
			}
		}
	} else {
		status.major = ERR_FSAL_NO_ERROR;
		status.minor = 0;

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			posix2fsal_attributes_all(&stat, attrs_out);
		}
	}

	close(dir_fd);

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
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	int retval = 0;
	dev_t unix_dev = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	struct attrlist attrs;
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */
	if (!dir_hdl->obj_ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);

#ifdef ENABLE_VFS_DEBUG_ACL
	fsal_prepare_attrs(&attrs, ATTR_ACL);

	status = dir_hdl->obj_ops->getattrs(dir_hdl, &attrs);

	if (FSAL_IS_ERROR(status))
		return status;

	status.major = fsal_inherit_acls(attrib, attrs.acl,
					 FSAL_ACE_FLAG_FILE_INHERIT);

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */

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
	status = dir_hdl->obj_ops->test_access(dir_hdl, access_type, NULL, NULL,
					      false);
	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_VFS_DEBUG_ACL */

	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	switch (nodetype) {
	case BLOCK_FILE:
		unix_mode |= S_IFBLK;
		unix_dev = makedev(attrib->rawdev.major, attrib->rawdev.minor);
		break;
	case CHARACTER_FILE:
		unix_mode |= S_IFCHR;
		unix_dev = makedev(attrib->rawdev.major, attrib->rawdev.minor);
		break;
	case FIFO_FILE:
		unix_mode |= S_IFIFO;
		break;
	case SOCKET_FILE:
		unix_mode |= S_IFSOCK;
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
		status = posix2fsal_status(retval);
		goto direrr;
	}

	fsal_set_credentials(op_ctx->creds);

	retval = mknodat(dir_fd, name, unix_mode, unix_dev);

	if (retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		status = posix2fsal_status(retval);
		goto direrr;
	}

	fsal_restore_ganesha_credentials();

	vfs_alloc_handle(fh);

	retval = vfs_name_to_handle(dir_fd, myself->obj_handle.fs, name, fh);

	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto fileerr;
	}

	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);

	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, myself->obj_handle.fs, &stat,
			   myself->handle, name, op_ctx->fsal_export);

	if (hdl == NULL) {
		status = fsalstat(ERR_FSAL_NOMEM, ENOMEM);
		goto fileerr;
	}

	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->valid_mask, ATTR_MODE);

	if (attrib->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops->setattr2(*handle, false, NULL,
						     attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			(*handle)->obj_ops->release(*handle);
			*handle = NULL;
		} else if (attrs_out != NULL) {
			status = (*handle)->obj_ops->getattrs(*handle,
							     attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes.
				 */
				goto fileerr;
			}
		}
	} else {
		status.major = ERR_FSAL_NO_ERROR;
		status.minor = 0;

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			posix2fsal_attributes_all(&stat, attrs_out);
		}
	}

	close(dir_fd);

	return status;

 fileerr:

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
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	struct attrlist attrs;
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it first */
	if (!dir_hdl->obj_ops->handle_is(dir_hdl, DIRECTORY)) {
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
	status = dir_hdl->obj_ops->test_access(dir_hdl, access_type, NULL, NULL,
					      false);
	if (FSAL_IS_ERROR(status))
		return status;

	fsal_prepare_attrs(&attrs, ATTR_ACL);

	status = dir_hdl->obj_ops->getattrs(dir_hdl, &attrs);

	if (FSAL_IS_ERROR(status))
		return status;

	status.major = fsal_inherit_acls(attrib, attrs.acl,
					 FSAL_ACE_FLAG_FILE_INHERIT);

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

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
		status = posix2fsal_status(retval);
		goto direrr;
	}

	/* Become the user because we are creating an object in this dir.
	 */
	fsal_set_credentials(op_ctx->creds);

	retval = symlinkat(link_path, dir_fd, name);

	if (retval < 0) {
		retval = errno;
		fsal_restore_ganesha_credentials();
		status = posix2fsal_status(retval);
		goto direrr;
	}

	fsal_restore_ganesha_credentials();

	retval = vfs_name_to_handle(dir_fd, dir_hdl->fs, name, fh);

	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto linkerr;
	}

	/* now get attributes info,
	 * being careful to get the link, not the target */
	retval = fstatat(dir_fd, name, &stat, AT_SYMLINK_NOFOLLOW);

	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto linkerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, dir_hdl->fs, &stat, NULL, name,
			   op_ctx->fsal_export);

	if (hdl == NULL) {
		status = fsalstat(ERR_FSAL_NOMEM, ENOMEM);
		goto linkerr;
	}

	*handle = &hdl->obj_handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->valid_mask, ATTR_MODE);

	if (attrib->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*handle)->obj_ops->setattr2(*handle, false, NULL,
						     attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			(*handle)->obj_ops->release(*handle);
			*handle = NULL;
		} else if (attrs_out != NULL) {
			status = (*handle)->obj_ops->getattrs(*handle,
							     attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes.
				 */
				goto linkerr;
			}
		}
	} else {
		status.major = ERR_FSAL_NO_ERROR;
		status.minor = 0;

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			posix2fsal_attributes_all(&stat, attrs_out);
		}
	}

	close(dir_fd);

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

	if (!op_ctx->fsal_export->exp_ops.fs_supports(
				op_ctx->fsal_export, fso_link_support)) {
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
	PTHREAD_RWLOCK_rdlock(&obj_hdl->obj_lock);

	if (obj_hdl->type == REGULAR_FILE &&
	    myself->u.file.fd.openflags != FSAL_O_CLOSED) {
		srcfd = myself->u.file.fd.fd;
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
	if (!(obj_hdl->type == REGULAR_FILE && myself->u.file.fd.fd >= 0))
		close(srcfd);

 out_unlock:

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

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
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	struct vfs_fsal_obj_handle *myself;
	int dirfd;
	fsal_status_t status = {0, 0};
	int retval = 0;
	off_t seekloc = 0;
	off_t baseloc = 0;
	unsigned int bpos;
	int nread;
	struct vfs_dirent dentry, *dentryp = &dentry;
	char buf[BUF_SIZE];
	fsal_cookie_t cookie;
	bool skip_first;

	if (whence != NULL)
		seekloc = (off_t) *whence;
	cookie = seekloc;

	/* If we don't start from beginning skip an entry */
	skip_first = cookie != 0;

	if (cookie == 0) {
		/* Return a non-zero cookie for the first file. */
		cookie = FIRST_COOKIE;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "whence=%p seekloc=%"PRIx64" cookie=%"PRIx64"%s",
		     whence, (uint64_t) seekloc, cookie,
		     skip_first ? " skip_first" : "");

	myself = container_of(dir_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (dir_hdl->fsal != dir_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 dir_hdl->fsal->name,
			 dir_hdl->fs->fsal != NULL
				? dir_hdl->fs->fsal->name
				: "(none)");
		retval = EXDEV;
		status = posix2fsal_status(retval);
		goto out;
	}
	dirfd = vfs_fsal_open(myself, O_RDONLY | O_DIRECTORY, &status.major);
	if (dirfd < 0) {
		retval = -dirfd;
		status = posix2fsal_status(retval);
		goto out;
	}
	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if (seekloc < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto done;
	}

	do {
		baseloc = seekloc;
		nread = vfs_readents(dirfd, buf, BUF_SIZE, &seekloc);
		if (nread < 0) {
			retval = errno;
			status = posix2fsal_status(retval);
			goto done;
		}
		if (nread == 0)
			break;
		for (bpos = 0; bpos < nread;) {
			struct fsal_obj_handle *hdl;
			struct attrlist attrs;
			enum fsal_dir_result cb_rc;

			if (!to_vfs_dirent(buf, bpos, dentryp, baseloc))
				goto skip;

			LogFullDebug(COMPONENT_FSAL,
				     "Entry %s last_ck=%"PRIx64
				     " next_ck=%"PRIx64"%s",
				     dentryp->vd_name, cookie,
				     (uint64_t) dentryp->vd_offset,
				     skip_first ? " skip_first" : "");

			if (strcmp(dentryp->vd_name, ".") == 0 ||
			    strcmp(dentryp->vd_name, "..") == 0)
				goto skip;	/* must skip '.' and '..' */

			/* If this is the first dirent found after a restarted
			 * readdir, we actually just fetched the last dirent
			 * from the previous call so we skip it.
			 */
			if (skip_first) {
				skip_first = false;
				goto skip;
			}

			fsal_prepare_attrs(&attrs, attrmask);

			status = lookup_with_fd(myself, dirfd, dentryp->vd_name,
					&hdl, &attrs);

			if (FSAL_IS_ERROR(status)) {
				goto done;
			}

			/* callback to cache inode */
			cb_rc = cb(dentryp->vd_name, hdl, &attrs, dir_state,
				   cookie);

			fsal_release_attrs(&attrs);

			/* Read ahead not supported by this FSAL. */
			if (cb_rc >= DIR_READAHEAD)
				goto done;

 skip:
			/* Save this d_off for the next cookie */
			cookie = (fsal_cookie_t) dentryp->vd_offset;

			bpos += dentryp->vd_reclen;
		}
	} while (nread > 0);

	*eof = true;
 done:
	close(dirfd);

 out:
	return status;
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
		 * Go ahead and discard the old name (we will abort if
		 * gsh_strdup fails to copy the new name).
		 */
		gsh_free(obj->u.unopenable.name);

		memcpy(obj->u.unopenable.dir, newdir->handle,
		       sizeof(vfs_file_handle_t));
		obj->u.unopenable.name = gsh_strdup(new_name);
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
 * the attributes. The obj_hdl->obj_lock MUST be held for this call.
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
struct closefd vfs_fsal_open_and_stat(struct fsal_export *exp,
				      struct vfs_fsal_obj_handle *myself,
				      struct stat *stat,
				      fsal_openflags_t flags,
				      fsal_errors_t *fsal_error)
{
	struct fsal_obj_handle *obj_hdl = &myself->obj_handle;
	struct closefd cfd = { .fd = -1, .close_fd = false };
	int retval = 0;
	const char *func = "unknown";
	struct vfs_filesystem *vfs_fs = myself->obj_handle.fs->private_data;
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
		 * myself->u.file.fd.openflags and thus forces a re-open.
		 */
		if (((flags & FSAL_O_ANY) != 0 &&
		     (myself->u.file.fd.openflags & FSAL_O_RDWR) == 0) ||
		    ((myself->u.file.fd.openflags & flags) != flags)) {
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
			cfd.fd = myself->u.file.fd.fd;
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

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *obj_hdl,
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
		fsal_status_t st;

		/* Take write lock on object to protect file descriptor.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		st = vfs_close_my_fd(&myself->u.file.fd);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

		if (FSAL_IS_ERROR(st)) {
			LogCrit(COMPONENT_FSAL,
				"Could not close hdl 0x%p, error %s(%d)",
				obj_hdl, strerror(st.minor), st.minor);
		}
	}

	fsal_obj_handle_fini(obj_hdl);

	if (type == SYMBOLIC_LINK) {
		gsh_free(myself->u.symlink.link_content);
	} else if (type == REGULAR_FILE) {
		struct gsh_buffdesc key;

		handle_to_key(obj_hdl, &key);
		vfs_state_release(&key);
	} else if (type == DIRECTORY) {
		gsh_free(myself->u.directory.path);
		gsh_free(myself->u.directory.fs_location);
	} else if (vfs_unopenable_type(type)) {
		gsh_free(myself->u.unopenable.name);
		gsh_free(myself->u.unopenable.dir);
	}

	LogDebug(COMPONENT_FSAL,
		 "Releasing obj_hdl=%p, myself=%p",
		 obj_hdl, myself);

	gsh_free(myself);
}

/* vfs_fs_locations
 * returns the saved referral information to NFS protocol layer
 */

static fsal_status_t vfs_fs_locations(struct fsal_obj_handle *obj_hdl,
					struct fs_locations4 *fs_locs)
{
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	struct vfs_filesystem *vfs_fs = myself->obj_handle.fs->private_data;
	struct fs_location4 *loc_val = fs_locs->locations.locations_val;

	LogFullDebug(COMPONENT_FSAL,
		     "vfs_fs = %s root_fd = %d major = %d minor = %d",
		     vfs_fs->fs->path, vfs_fs->root_fd,
		     (int)vfs_fs->fs->fsid.major,
		     (int)vfs_fs->fs->fsid.minor);

	LogDebug(COMPONENT_FSAL,
		 "fs_location = %p:%s",
		 myself->u.directory.fs_location,
		 myself->u.directory.fs_location);
	if (myself->u.directory.fs_location != NULL) {
		char *server;
		char *path_sav, *path_work;

		path_sav = gsh_strdup(myself->u.directory.fs_location);
		path_work = path_sav;
		server = strsep(&path_work, ":");

		LogDebug(COMPONENT_FSAL,
			 "fs_location server %s",
			 server);
		LogDebug(COMPONENT_FSAL,
			 "fs_location path %s",
			 path_work);

		nfs4_pathname4_free(&fs_locs->fs_root);
		nfs4_pathname4_alloc(&fs_locs->fs_root,
				     myself->u.directory.path);
		strncpy(loc_val->server.server_val->utf8string_val,
			server, strlen(server));
		loc_val->server.server_val->utf8string_len = strlen(server);
		nfs4_pathname4_free(&loc_val->rootpath);
		nfs4_pathname4_alloc(&loc_val->rootpath, path_work);

		gsh_free(path_sav);
	} else {
		return fsalstat(ERR_FSAL_NOTSUPP, -1);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void vfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->release = release;
	ops->merge = vfs_merge;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->getattrs = vfs_getattr2;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->fs_locations = vfs_fs_locations;
	ops->close = vfs_close;
	ops->handle_to_wire = handle_to_wire;
	ops->handle_to_key = handle_to_key;
	ops->open2 = vfs_open2;
	ops->reopen2 = vfs_reopen2;
	ops->read2 = vfs_read2;
	ops->write2 = vfs_write2;
	ops->commit2 = vfs_commit2;
	ops->lock_op2 = vfs_lock_op2;
	ops->setattr2 = vfs_setattr2;
	ops->close2 = vfs_close2;

	/* xattr related functions */
	ops->list_ext_attrs = vfs_list_ext_attrs;
	ops->getextattr_id_by_name = vfs_getextattr_id_by_name;
	ops->getextattr_value_by_name = vfs_getextattr_value_by_name;
	ops->getextattr_value_by_id = vfs_getextattr_value_by_id;
	ops->setextattr_value = vfs_setextattr_value;
	ops->setextattr_value_by_id = vfs_setextattr_value_by_id;
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
			      const char *path, struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out)
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

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&stat, attrs_out);
	}

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
				".0x%016"PRIx64" to filesystem",
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
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out)
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
		fd = vfs_open_by_handle(fs->private_data, fh, flags,
					&fsal_error);

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

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&obj_stat, attrs_out);
	}

	*handle = &hdl->obj_handle;

 errout:
	return fsalstat(fsal_error, retval);
}
