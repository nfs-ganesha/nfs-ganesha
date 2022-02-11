// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "FSAL/fsal_localfs.h"
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
	return vfs_open_by_handle(hdl->obj_handle.fs, hdl->handle,
				  openflags, fsal_error);
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
 * @brief Free the VFS OBJ handle
 *
 * @param[in] hdl_ref	VFS OBJ handle reference
 */
void free_vfs_fsal_obj_handle(struct vfs_fsal_obj_handle **hdl_ref)
{
	struct vfs_fsal_obj_handle *myself = *hdl_ref;
	object_file_type_t type = myself->obj_handle.type;

	if (type == SYMBOLIC_LINK) {
		gsh_free(myself->u.symlink.link_content);
	} else if (type == REGULAR_FILE) {
		struct gsh_buffdesc key;

		handle_to_key(&myself->obj_handle, &key);
		vfs_state_release(&key);
	} else if (vfs_unopenable_type(type)) {
		gsh_free(myself->u.unopenable.name);
		gsh_free(myself->u.unopenable.dir);
	}

	LogDebug(COMPONENT_FSAL,
		 "Releasing obj_hdl=%p, myself=%p",
		 &myself->obj_handle, myself);

	gsh_free(myself);
	*hdl_ref = NULL;
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
		     "Creating object %p for file %s of type %s on filesystem %p %s",
		     hdl, path, object_file_type_to_str(hdl->obj_handle.type),
		     fs, fs->path);

	if (hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd.fd = -1;	/* no open on this yet */
		hdl->u.file.fd.openflags = FSAL_O_CLOSED;
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
	free_vfs_fsal_obj_handle(&hdl);
	return NULL;
}

static fsal_status_t populate_fs_locations(struct vfs_fsal_obj_handle *hdl,
					   struct fsal_attrlist *attrs_out)
{
	fsal_status_t status;
	uint64 hash;
	attrmask_t old_request_mask = attrs_out->request_mask;

	attrs_out->request_mask = ATTR4_FS_LOCATIONS;
	status = hdl->sub_ops->getattrs(hdl, -1 /* no fd here */,
			attrs_out->request_mask,
			attrs_out);
	if (FSAL_IS_ERROR(status)) {
		goto out;
	}

	if (FSAL_TEST_MASK(attrs_out->valid_mask, ATTR4_FS_LOCATIONS)) {
		/* on a referral the filesystem id has to change
		 * it get's calculated via a hash from the referral
		 * and stored in the fsid object of the fsal_obj_handle
		 */

		fsal_fs_locations_t *fsloc = attrs_out->fs_locations;
		int loclen = fsloc->server[0].utf8string_len +
			strlen(fsloc->rootpath) + 2;

		char *location = gsh_calloc(1, loclen);

		(void) snprintf(location, loclen, "%.*s:%s",
				fsloc->server[0].utf8string_len,
				fsloc->server[0].utf8string_val,
				fsloc->rootpath);
		hash = CityHash64(location, loclen);
		hdl->obj_handle.fsid.major = hash;
		hdl->obj_handle.fsid.minor = hash;
		LogDebug(COMPONENT_NFS_V4,
				"fsid.major = %"PRIu64", fsid.minor = %"PRIu64,
				hdl->obj_handle.fsid.major,
				hdl->obj_handle.fsid.minor);
		gsh_free(location);
	}

out:
	attrs_out->request_mask |= old_request_mask;
	return status;
}

static fsal_status_t check_filesystem(struct vfs_fsal_obj_handle *parent_hdl,
				      int dirfd, const char *path,
				      struct stat *stat,
				      struct fsal_filesystem **filesystem,
				      bool *xfsal)
{
	int retval;
	fsal_dev_t dev;
	struct fsal_filesystem *fs = NULL;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

again:

	retval = fstatat(dirfd, path, stat, AT_SYMLINK_NOFOLLOW);

	if (retval < 0) {
		retval = errno;
		LogDebug(COMPONENT_FSAL, "Failed to open stat %s: %s", path,
			 msg_fsal_err(posix2fsal_error(retval)));

		if (errno == EAGAIN) {
			/* autofs can cause EAGAIN if the file system is not
			 * yet mounted.
			 */
			/** @todo FSF: should we retry forever
			 *             is there any other reason for EAGAIN?
			 */
			goto again;
		}

		status = posix2fsal_status(retval);
		goto out;
	}

	dev = posix2fsal_devt(stat->st_dev);

	fs = parent_hdl->obj_handle.fs;

	LogFilesystem("Parent", "", fs);

	if ((dev.minor == parent_hdl->dev.minor) &&
	    (dev.major == parent_hdl->dev.major)) {
		/* Filesystem is ok */
		goto out;
	}

	/* XDEV */
	fs = lookup_dev(&dev);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Lookup of %s crosses filesystem boundary to unknown file system dev=%"
			PRIu64".%"PRIu64" - reloading filesystems to find it",
			path, dev.major, dev.minor);

		retval = populate_posix_file_systems(path);

		if (retval != 0) {
			LogCrit(COMPONENT_FSAL,
				"populate_posix_file_systems returned %s (%d)",
				strerror(retval), retval);
			status = posix2fsal_status(EXDEV);
			goto out;
		}

		fs = lookup_dev(&dev);

		if (fs == NULL) {
			LogFullDebug(COMPONENT_FSAL,
				     "Filesystem still was not claimed");
			status = posix2fsal_status(EXDEV);
			goto out;
		} else {
			/* We have now found the file system, since it was
			 * just added, fs->fsal will be NULL so the next if
			 * block below will be executed to claim the file system
			 * and carry on.
			 */
			LogInfo(COMPONENT_FSAL,
				"Filesystem %s will be added to export %d:%s",
				fs->path, op_ctx->ctx_export->export_id,
				op_ctx_export_path(op_ctx));
		}
	}

	LogFilesystem("XDEV", "", fs);

	if (fs->fsal == NULL) {
		/* The filesystem wasn't claimed, it must have been added after
		 * we created this export. Go ahead and try to get it claimed.
		 */
		LogInfo(COMPONENT_FSAL,
			"Lookup of %s crosses filesystem boundary to unclaimed file system %s - attempt to claim it",
			path, fs->path);

		retval = claim_posix_filesystems(CTX_FULLPATH(op_ctx),
						 parent_hdl->obj_handle.fsal,
						 op_ctx->fsal_export,
						 vfs_claim_filesystem,
						 vfs_unclaim_filesystem,
						 &op_ctx->fsal_export->root_fs,
						 stat);

		if (retval != 0) {
			LogFullDebug(COMPONENT_FSAL,
				     "claim_posix_filesystems failed");
			status = posix2fsal_status(EXDEV);
			goto out;
		}
	}

	if (fs->fsal != parent_hdl->obj_handle.fsal) {
		*xfsal = true;
		LogDebug(COMPONENT_FSAL,
			 "Lookup of %s crosses filesystem boundary to file system %s into FSAL %s",
			 path, fs->path,
			 fs->fsal != NULL
				? fs->fsal->name
				: "(none)");
		goto out;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "Lookup of %s crosses filesystem boundary to file system %s",
			 path, fs->path);
		goto out;
	}

out:

	*filesystem = fs;
	return status;
}

static fsal_status_t lookup_with_fd(struct vfs_fsal_obj_handle *parent_hdl,
				    int dirfd, const char *path,
				    struct fsal_obj_handle **handle,
				    struct fsal_attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *hdl;
	int retval;
	struct stat stat;
	vfs_file_handle_t *fh = NULL;
	struct fsal_filesystem *fs;
	bool xfsal = false;
	fsal_status_t status;

	vfs_alloc_handle(fh);

	status = check_filesystem(parent_hdl, dirfd, path, &stat, &fs, &xfsal);

	if (FSAL_IS_ERROR(status))
		return status;

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

		/* Get correct fsid from the fsal_filesystem, it may not be
		 * the device major/minor from stat.
		 */
		attrs_out->fsid = hdl->obj_handle.fs->fsid;
	}

	hdl->obj_handle.fsid = hdl->obj_handle.fs->fsid;

	/* if it is a directory and the sticky bit is set
	 * let's look for referral information
	 */

	if (attrs_out != NULL &&
	    hdl->obj_handle.obj_ops->is_referral(&hdl->obj_handle, attrs_out,
		false) &&
	    hdl->obj_handle.fs->private_data != NULL &&
	    hdl->sub_ops->getattrs) {

		status = populate_fs_locations(hdl, attrs_out);
		if (FSAL_IS_ERROR(status)) {
			LogEvent(COMPONENT_FSAL, "Could not get the referral "
				 "locations the path: %d, %s", dirfd, path);
			free_vfs_fsal_obj_handle(&hdl);
			return status;
		}
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
			    struct fsal_attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *parent_hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int dirfd;
	fsal_status_t status;

	*handle = NULL;		/* poison it first */
	parent_hdl =
	    container_of(parent, struct vfs_fsal_obj_handle, obj_handle);
	if (!fsal_obj_handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p", parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	LogFilesystem("About to check FSALs", "", parent->fs);

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
			     const char *name, struct fsal_attrlist *attrib,
			     struct fsal_obj_handle **handle,
			     struct fsal_attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd;
	struct stat stat;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	struct fsal_attrlist attrs;
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
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

	if (!vfs_set_credentials(&op_ctx->creds, dir_hdl->fsal)) {
		retval = EPERM;
		status = posix2fsal_status(retval);
		goto direrr;
	}

	retval = mkdirat(dir_fd, name, unix_mode);
	if (retval < 0) {
		retval = errno;
		vfs_restore_ganesha_credentials(dir_hdl->fsal);
		LogFullDebug(COMPONENT_FSAL,
			     "mkdirat returned %s",
			     strerror(retval));
		status = posix2fsal_status(retval);
		goto direrr;
	}
	vfs_restore_ganesha_credentials(dir_hdl->fsal);
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

			/* Get correct fsid from the fsal_filesystem, it may
			 * not be the device major/minor from stat.
			 */
			attrs_out->fsid = hdl->obj_handle.fs->fsid;
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
			      struct fsal_attrlist *attrib,
			      struct fsal_obj_handle **handle,
			      struct fsal_attrlist *attrs_out)
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
	struct fsal_attrlist attrs;
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
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

	if (!vfs_set_credentials(&op_ctx->creds, dir_hdl->fsal)) {
		retval = EPERM;
		status = posix2fsal_status(retval);
		goto direrr;
	}

	retval = mknodat(dir_fd, name, unix_mode, unix_dev);

	if (retval < 0) {
		retval = errno;
		vfs_restore_ganesha_credentials(dir_hdl->fsal);
		status = posix2fsal_status(retval);
		goto direrr;
	}

	vfs_restore_ganesha_credentials(dir_hdl->fsal);

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

			/* Get correct fsid from the fsal_filesystem, it may
			 * not be the device major/minor from stat.
			 */
			attrs_out->fsid = hdl->obj_handle.fs->fsid;
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
				 struct fsal_attrlist *attrib,
				 struct fsal_obj_handle **handle,
				 struct fsal_attrlist *attrs_out)
{
	struct vfs_fsal_obj_handle *myself, *hdl;
	int dir_fd = -1;
	struct stat stat;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int flags = O_PATH | O_NOACCESS;
#ifdef ENABLE_VFS_DEBUG_ACL
	struct fsal_attrlist attrs;
	fsal_accessflags_t access_type;
#endif /* ENABLE_VFS_DEBUG_ACL */
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	LogDebug(COMPONENT_FSAL, "create %s", name);

	*handle = NULL;		/* poison it first */
	if (!fsal_obj_handle_is(dir_hdl, DIRECTORY)) {
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
	if (!vfs_set_credentials(&op_ctx->creds, dir_hdl->fsal)) {
		retval = EPERM;
		status = posix2fsal_status(retval);
		goto direrr;
	}

	retval = symlinkat(link_path, dir_fd, name);

	if (retval < 0) {
		retval = errno;
		vfs_restore_ganesha_credentials(dir_hdl->fsal);
		status = posix2fsal_status(retval);
		goto direrr;
	}

	vfs_restore_ganesha_credentials(dir_hdl->fsal);

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

			/* Get correct fsid from the fsal_filesystem, it may
			 * not be the device major/minor from stat.
			 */
			attrs_out->fsid = hdl->obj_handle.fs->fsid;
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

/*
 * Use the smallest buffer possible on FreeBSD
 * to narrow the race due to the d_off workaround.
 */
#ifndef HAS_DOFF
#define BUF_SIZE sizeof(struct dirent)
#else
#define BUF_SIZE 1024
#endif
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
#ifndef HAS_DOFF
	int nreadent;
	char entbuf[sizeof(struct dirent)];
	off_t rewindloc = 0;
	off_t entloc = 0;
#endif

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
#ifndef HAS_DOFF
		/*
		 * Very inefficient workaround to retrieve directory offsets.
		 * We rewind dirfd's to its previous offset in order read the
		 * directory entry by entry and fetch every offset.
		 */
		rewindloc = lseek(dirfd, seekloc, SEEK_SET);
		if (rewindloc < 0) {
			retval = errno;
			status = posix2fsal_status(retval);
			goto done;
		}
#endif
		for (bpos = 0; bpos < nread;) {
			struct fsal_obj_handle *hdl;
			struct fsal_attrlist attrs;
			enum fsal_dir_result cb_rc;

			if (!to_vfs_dirent(buf, bpos, dentryp, baseloc))
				goto skip;

#ifndef HAS_DOFF
			/* Re-read the entry and fetch its offset. */
			nreadent = vfs_readents(dirfd, entbuf,
						dentryp->vd_reclen, &entloc);
			if (nreadent < 0) {
				retval = errno;
				status = posix2fsal_status(retval);
				goto done;
			}
			entloc = lseek(dirfd, 0, SEEK_CUR);
			if (entloc < 0) {
				retval = errno;
				status = posix2fsal_status(retval);
				goto done;
			}
			dentryp->vd_offset = entloc;
#endif

			if (strcmp(dentryp->vd_name, ".") == 0
			    || strcmp(dentryp->vd_name, "..") == 0)
				goto skip;	/* must skip '.' and '..' */

			fsal_prepare_attrs(&attrs, attrmask);

			status = lookup_with_fd(myself, dirfd, dentryp->vd_name,
					&hdl, &attrs);

			if (FSAL_IS_ERROR(status)) {
				goto done;
			}

			/* callback to MDCACHE */
			cb_rc = cb(dentryp->vd_name, hdl, &attrs, dir_state,
				(fsal_cookie_t) dentryp->vd_offset);

			fsal_release_attrs(&attrs);

			/* Read ahead not supported by this FSAL. */
			if (cb_rc >= DIR_READAHEAD)
				goto done;

 skip:
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
	if (!vfs_set_credentials(&op_ctx->creds, obj_hdl->fsal)) {
		retval = EPERM;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	retval = renameat(oldfd, old_name, newfd, new_name);
	if (retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		vfs_restore_ganesha_credentials(obj_hdl->fsal);
		LogDebug(COMPONENT_FSAL, "renameat returned %d (%s)",
			 retval, strerror(retval));
	} else {
		if (vfs_unopenable_type(obj->obj_handle.type)) {
			/* A block, char, or socket has been renamed. Fixup
			 * our information in the handle so we can still stat
			 * it. Go ahead and discard the old name (we will abort
			 * if gsh_strdup fails to copy the new name).
			 */
			gsh_free(obj->u.unopenable.name);

			memcpy(obj->u.unopenable.dir, newdir->handle,
			       sizeof(vfs_file_handle_t));
			obj->u.unopenable.name = gsh_strdup(new_name);
		}

		vfs_restore_ganesha_credentials(obj_hdl->fsal);
	}

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
	int open_flags;

	fsal2posix_openflags(flags, &open_flags);

	switch (obj_hdl->type) {
	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
		cfd.fd = vfs_open_by_handle(myself->obj_handle.fs,
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

	if (!vfs_set_credentials(&op_ctx->creds, dir_hdl->fsal)) {
		retval = EPERM;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	retval = unlinkat(fd, name, (S_ISDIR(stat.st_mode)) ? AT_REMOVEDIR : 0);
	if (retval < 0) {
		retval = errno;
		if (retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
	}
	vfs_restore_ganesha_credentials(dir_hdl->fsal);

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
 * @brief release object handle
 *
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
	free_vfs_fsal_obj_handle(&myself);
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
	ops->close = vfs_close;
#ifdef FALLOC_FL_PUNCH_HOLE
	ops->fallocate = vfs_fallocate;
#endif
	ops->handle_to_wire = handle_to_wire;
	ops->handle_to_key = handle_to_key;
	ops->open2 = vfs_open2;
	ops->reopen2 = vfs_reopen2;
	ops->read2 = vfs_read2;
	ops->write2 = vfs_write2;
#ifdef __USE_GNU
	ops->seek2 = vfs_seek2;
#endif
	ops->commit2 = vfs_commit2;
#ifdef F_OFD_GETLK
	ops->lock_op2 = vfs_lock_op2;
#endif
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

	ops->is_referral = fsal_common_is_referral;
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t vfs_lookup_path(struct fsal_export *exp_hdl,
			      const char *path, struct fsal_obj_handle **handle,
			      struct fsal_attrlist *attrs_out)
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
		LogDebug(COMPONENT_FSAL,
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

		/* Get correct fsid from the fsal_filesystem, it may
		* not be the device major/minor from stat.
		*/
		attrs_out->fsid = hdl->obj_handle.fs->fsid;
	}

	/* if it is a directory and the sticky bit is set
	 * let's look for referral information
	 */

	if (attrs_out != NULL &&
	    hdl->obj_handle.obj_ops->is_referral(&hdl->obj_handle, attrs_out,
		false) &&
	    hdl->obj_handle.fs->private_data != NULL &&
	    hdl->sub_ops->getattrs) {

		fsal_status_t status;

		status = populate_fs_locations(hdl, attrs_out);
		if (FSAL_IS_ERROR(status)) {
			LogEvent(COMPONENT_FSAL, "Could not get the referral "
				 "locations for the exported path: %s", path);
			free_vfs_fsal_obj_handle(&hdl);
			return status;
		}
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
	bool fslocked = false;

	*fs = NULL;

	if (!vfs_valid_handle(hdl_desc))
		return fsalstat(ERR_FSAL_BADHANDLE, 0);

	memcpy(fh->handle_data, hdl_desc->addr, hdl_desc->len);
	fh->handle_len = hdl_desc->len;

	*dummy = vfs_is_dummy_handle(fh);

	retval = vfs_extract_fsid(fh, &fsid_type, &fsid);


	if (retval == 0) {
		/* Since the root_fs is the most likely one, and it can't
		 * change, we can check without locking.
		 */
		if (fsal_fs_compare_fsid(fsid_type,
					 &fsid,
					 exp_hdl->root_fs->fsid_type,
					 &exp_hdl->root_fs->fsid) == 0) {
			/* This is the root_fs of the export, all good. */
			*fs = exp_hdl->root_fs;
		} else {
			/* Must lookup fs and check that it is exported by
			 * the export.
			 */
			PTHREAD_RWLOCK_rdlock(&fs_lock);
			fslocked = true;

			*fs = lookup_fsid_locked(&fsid, fsid_type);
		}

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

		/* If we had to lookup fs, then we must check that it is
		 * exported by the export.
		 */
		if (fslocked && !(*dummy) &&
		    !is_filesystem_exported(*fs, exp_hdl)) {
			/* We've got a handle with a spoofed fsid/export_id */
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
	if (fslocked)
		PTHREAD_RWLOCK_unlock(&fs_lock);
	return fsalstat(fsal_error, retval);
}

/* create_handle
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

fsal_status_t vfs_create_handle(struct fsal_export *exp_hdl,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle,
				struct fsal_attrlist *attrs_out)
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
		fd = vfs_open_by_handle(fs, fh, flags, &fsal_error);

		if (fd < 0) {
			#ifdef __FreeBSD__
			if (fd == -EMLINK) {
				fd = -1;
				fsal_error = ERR_FSAL_NO_ERROR;
			} else
			#endif
			{
				retval = -fd;
				goto errout;
			}
		}

		#ifdef __FreeBSD__
		if (fd < 0)
			retval = fhstat(v_to_fhandle(fh->handle_data),
					&obj_stat);
		else
		#endif
		{
			retval = vfs_stat_by_handle(fd, &obj_stat);
		}
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

		/* Get correct fsid from the fsal_filesystem, it may
		* not be the device major/minor from stat.
		*/
		attrs_out->fsid = hdl->obj_handle.fs->fsid;
	}

	*handle = &hdl->obj_handle;

 errout:
	return fsalstat(fsal_error, retval);
}
