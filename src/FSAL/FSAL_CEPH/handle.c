/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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

/**
 * @file   FSAL_CEPH/handle.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Mon Jul  9 15:18:47 2012
 *
 * @brief Interface to handle functionality
 *
 * This function implements the interfaces on the struct
 * fsal_obj_handle type.
 */

#include "config.h"
#ifdef LINUX
#include <sys/sysmacros.h> /* for makedev(3) */
#endif
#include <fcntl.h>
#include <cephfs/libcephfs.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"
#include "sal_data.h"

/**
 * @brief Release an object
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in] obj_pub The object to release
 *
 * @return FSAL status codes.
 */

static void release(struct fsal_obj_handle *obj_pub)
{
	/* The private 'full' handle */
	struct handle *obj = container_of(obj_pub, struct handle, handle);

	if (obj != obj->export->root)
		deconstruct_handle(obj);
}

/**
 * @brief Look up an object by name
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in]  dir_pub The directory in which to look up the object.
 * @param[in]  path    The name to look up.
 * @param[out] obj_pub The looked up object.
 *
 * @return FSAL status codes.
 */
static fsal_status_t lookup(struct fsal_obj_handle *dir_pub,
			    const char *path, struct fsal_obj_handle **obj_pub,
			    struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* Stat output */
	struct stat st;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	struct handle *dir = container_of(dir_pub, struct handle, handle);
	struct handle *obj = NULL;
	struct Inode *i = NULL;

	LogFullDebug(COMPONENT_FSAL, "Lookup %s", path);

	rc = ceph_ll_lookup(export->cmount, dir->i, path, &st, &i, 0, 0);

	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&st, i, export, &obj);

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);

		/* Make sure ATTR_RDATTR_ERR is cleared on success. */
		attrs_out->mask &= ~ATTR_RDATTR_ERR;
	}

	*obj_pub = &obj->handle;

	return fsalstat(0, 0);
}

/**
 * @brief Read a directory
 *
 * This function reads the contents of a directory (excluding . and
 * .., which is ironic since the Ceph readdir call synthesizes them
 * out of nothing) and passes dirent information to the supplied
 * callback.
 *
 * @param[in]  dir_pub     The directory to read
 * @param[in]  whence      The cookie indicating resumption, NULL to start
 * @param[in]  dir_state   Opaque, passed to cb
 * @param[in]  cb          Callback that receives directory entries
 * @param[out] eof         True if there are no more entries
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_readdir(struct fsal_obj_handle *dir_pub,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *dir = container_of(dir_pub, struct handle, handle);
	/* The director descriptor */
	struct ceph_dir_result *dir_desc = NULL;
	/* Cookie marking the start of the readdir */
	uint64_t start = 0;
	/* Return status */
	fsal_status_t fsal_status = { ERR_FSAL_NO_ERROR, 0 };

	rc = ceph_ll_opendir(export->cmount, dir->i, &dir_desc, 0, 0);
	if (rc < 0)
		return ceph2fsal_error(rc);

	if (whence != NULL)
		start = *whence;

	ceph_seekdir(export->cmount, dir_desc, start);

	while (!(*eof)) {
		struct stat st;
		struct dirent de;
		int stmask = 0;

		rc = ceph_readdirplus_r(export->cmount, dir_desc, &de, &st,
					&stmask);
		if (rc < 0) {
			fsal_status = ceph2fsal_error(rc);
			goto closedir;
		} else if (rc == 1) {
			struct fsal_obj_handle *obj;
			struct attrlist attrs;
			bool cb_rc;

			/* skip . and .. */
			if ((strcmp(de.d_name, ".") == 0)
			    || (strcmp(de.d_name, "..") == 0)) {
				continue;
			}

			fsal_prepare_attrs(&attrs, attrmask);

			fsal_status = lookup(dir_pub, de.d_name, &obj, &attrs);
			if (FSAL_IS_ERROR(fsal_status)) {
				rc = 0; /* Return fsal_status directly */
				goto closedir;
			}

			cb_rc = cb(de.d_name, obj, &attrs, dir_state, de.d_off);

			fsal_release_attrs(&attrs);

			if (!cb_rc)
				goto closedir;

		} else if (rc == 0) {
			*eof = true;
		} else {
			/* Can't happen */
			abort();
		}
	}

 closedir:

	rc = ceph_ll_releasedir(export->cmount, dir_desc);

	if (rc < 0)
		fsal_status = ceph2fsal_error(rc);

	return fsal_status;
}

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]     dir_hdl Directory in which to create the directory
 * @param[in]     name    Name of directory to create
 * @param[in]     attrib  Attributes to set on newly created object
 * @param[out]    new_obj Newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_mkdir(struct fsal_obj_handle *dir_hdl,
				const char *name, struct attrlist *attrib,
				struct fsal_obj_handle **new_obj,
				struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *dir = container_of(dir_hdl, struct handle, handle);
	/* Stat result */
	struct stat st;
	mode_t unix_mode;
	/* Newly created object */
	struct handle *obj = NULL;
	struct Inode *i = NULL;
	fsal_status_t status;

	unix_mode = fsal2unix_mode(attrib->mode)
		& ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	rc = ceph_ll_mkdir(export->cmount, dir->i, name, unix_mode, &st, &i,
			   op_ctx->creds->caller_uid,
			   op_ctx->creds->caller_gid);

	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&st, i, export, &obj);

	*new_obj = &obj->handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->mask, ATTR_MODE);

	if (attrib->mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*new_obj)->obj_ops.setattr2(*new_obj, false, NULL,
						      attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*new_obj)->obj_ops.release(*new_obj);
			*new_obj = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			posix2fsal_attributes(&st, attrs_out);

			/* Make sure ATTR_RDATTR_ERR is cleared on success. */
			attrs_out->mask &= ~ATTR_RDATTR_ERR;
		}
	}

	FSAL_SET_MASK(attrib->mask, ATTR_MODE);

	return status;
}

/**
 * @brief Create a special file
 *
 * This function creates a new special file.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]     dir_hdl  Directory in which to create the object
 * @param[in]     name     Name of object to create
 * @param[in]     nodetype Type of special file to create
 * @param[in]     dev      Major and minor device numbers for block or
 *                         character special
 * @param[in]     attrib   Attributes to set on newly created object
 * @param[out]    new_obj  Newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */
static fsal_status_t ceph_fsal_mknode(struct fsal_obj_handle *dir_hdl,
				      const char *name,
				      object_file_type_t nodetype,
				      fsal_dev_t *dev,
				      struct attrlist *attrib,
				      struct fsal_obj_handle **new_obj,
				      struct attrlist *attrs_out)
{
#ifdef USE_FSAL_CEPH_MKNOD
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *dir = container_of(dir_hdl, struct handle, handle);
	/* Newly opened file descriptor */
	struct Inode *i = NULL;
	/* Status after create */
	struct stat st;
	mode_t unix_mode;
	dev_t unix_dev = 0;
	/* Newly created object */
	struct handle *obj;
	fsal_status_t status;

	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	switch (nodetype) {
	case BLOCK_FILE:
		unix_mode |= S_IFBLK;
		unix_dev = makedev(dev->major, dev->minor);
		break;
	case CHARACTER_FILE:
		unix_mode |= S_IFCHR;
		unix_dev = makedev(dev->major, dev->minor);
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
		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	rc = ceph_ll_mknod(export->cmount, dir->i, name, unix_mode, unix_dev,
			   &st, &i, op_ctx->creds->caller_uid,
			   op_ctx->creds->caller_gid);
	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&st, i, export, &obj);

	*new_obj = &obj->handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->mask, ATTR_MODE);

	if (attrib->mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*new_obj)->obj_ops.setattr2(*new_obj, false, NULL,
						      attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*new_obj)->obj_ops.release(*new_obj);
			*new_obj = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			posix2fsal_attributes(&st, attrs_out);

			/* Make sure ATTR_RDATTR_ERR is cleared on success. */
			attrs_out->mask &= ~ATTR_RDATTR_ERR;
		}
	}

	FSAL_SET_MASK(attrib->mask, ATTR_MODE);

	return status;
#else
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
#endif
}

/**
 * @brief Create a symbolic link
 *
 * This function creates a new symbolic link.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]     dir_hdl   Directory in which to create the object
 * @param[in]     name      Name of object to create
 * @param[in]     link_path Content of symbolic link
 * @param[in]     attrib    Attributes to set on newly created object
 * @param[out]    new_obj   Newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_symlink(struct fsal_obj_handle *dir_hdl,
				  const char *name, const char *link_path,
				  struct attrlist *attrib,
				  struct fsal_obj_handle **new_obj,
				  struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *dir = container_of(dir_hdl, struct handle, handle);
	/* Stat result */
	struct stat st;
	struct Inode *i = NULL;
	/* Newly created object */
	struct handle *obj = NULL;
	fsal_status_t status;

	rc = ceph_ll_symlink(export->cmount, dir->i, name, link_path, &st, &i,
			     op_ctx->creds->caller_uid,
			     op_ctx->creds->caller_gid);
	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&st, i, export, &obj);

	*new_obj = &obj->handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->mask, ATTR_MODE);

	if (attrib->mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*new_obj)->obj_ops.setattr2(*new_obj, false, NULL,
						      attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*new_obj)->obj_ops.release(*new_obj);
			*new_obj = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			posix2fsal_attributes(&st, attrs_out);

			/* Make sure ATTR_RDATTR_ERR is cleared on success. */
			attrs_out->mask &= ~ATTR_RDATTR_ERR;
		}
	}

	FSAL_SET_MASK(attrib->mask, ATTR_MODE);

	return status;
}

/**
 * @brief Retrieve the content of a symlink
 *
 * This function allocates a buffer, copying the symlink content into
 * it.
 *
 * @param[in]  link_pub    The handle for the link
 * @param[out] content_buf Buffdesc for symbolic link
 * @param[in]  refresh     true if the underlying content should be
 *                         refreshed.
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_readlink(struct fsal_obj_handle *link_pub,
				   struct gsh_buffdesc *content_buf,
				   bool refresh)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *link = container_of(link_pub, struct handle, handle);
	/* Pointer to the Ceph link content */
	char content[PATH_MAX];

	rc = ceph_ll_readlink(export->cmount, link->i, content, PATH_MAX, 0, 0);
	if (rc < 0)
		return ceph2fsal_error(rc);

	/* XXX in Ceph through 1/2016, ceph_ll_readlink returns the
	 * length of the path copied (truncated to 32 bits) in rc,
	 * and it cannot exceed the passed buffer size */
	content_buf->addr = gsh_strldup(content, MIN(rc, (PATH_MAX-1)),
					&content_buf->len);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Freshen and return attributes
 *
 * This function freshens and returns the attributes of the given
 * file.
 *
 * @param[in]  handle_pub Object to interrogate
 *
 * @return FSAL status.
 */

static fsal_status_t getattrs(struct fsal_obj_handle *handle_pub,
			      struct attrlist *attrs)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);
	/* Stat buffer */
	struct stat st;

	rc = ceph_ll_getattr(export->cmount, handle->i, &st, 0, 0);

	if (rc < 0) {
		if (attrs->mask & ATTR_RDATTR_ERR) {
			/* Caller asked for error to be visible. */
			attrs->mask = ATTR_RDATTR_ERR;
		}
		return ceph2fsal_error(rc);
	}

	posix2fsal_attributes(&st, attrs);

	/* Make sure ATTR_RDATTR_ERR is cleared on success. */
	attrs->mask &= ~ATTR_RDATTR_ERR;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a hard link
 *
 * This function creates a link from the supplied file to a new name
 * in a new directory.
 *
 * @param[in] handle_pub  File to link
 * @param[in] destdir_pub Directory in which to create link
 * @param[in] name        Name of link
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_link(struct fsal_obj_handle *handle_pub,
			       struct fsal_obj_handle *destdir_pub,
			       const char *name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);
	/* The private 'full' destination directory handle */
	struct handle *destdir =
	    container_of(destdir_pub, struct handle, handle);
	struct stat st;

	rc = ceph_ll_link(export->cmount, handle->i, destdir->i, name, &st,
			  op_ctx->creds->caller_uid,
			  op_ctx->creds->caller_gid);

	if (rc < 0)
		return ceph2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Rename a file
 *
 * This function renames a file, possibly moving it into another
 * directory.  We assume most checks are done by the caller.
 *
 * @param[in] olddir_pub Source directory
 * @param[in] old_name   Original name
 * @param[in] newdir_pub Destination directory
 * @param[in] new_name   New name
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_rename(struct fsal_obj_handle *obj_hdl,
				 struct fsal_obj_handle *olddir_pub,
				 const char *old_name,
				 struct fsal_obj_handle *newdir_pub,
				 const char *new_name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *olddir = container_of(olddir_pub, struct handle, handle);
	/* The private 'full' destination directory handle */
	struct handle *newdir = container_of(newdir_pub, struct handle, handle);

	rc = ceph_ll_rename(export->cmount, olddir->i, old_name, newdir->i,
			    new_name, op_ctx->creds->caller_uid,
			    op_ctx->creds->caller_gid);

	if (rc < 0)
		return ceph2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Remove a name
 *
 * This function removes a name from the filesystem and possibly
 * deletes the associated file.  Directories must be empty to be
 * removed.
 *
 * @param[in] dir_pub Parent directory
 * @param[in] name    Name to remove
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_unlink(struct fsal_obj_handle *dir_pub,
				      struct fsal_obj_handle *obj_pub,
				      const char *name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *dir = container_of(dir_pub, struct handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		     "Unlink %s, I think it's a %s",
		     name, object_file_type_to_str(obj_pub->type));

	if (obj_pub->type != DIRECTORY) {
		rc = ceph_ll_unlink(export->cmount, dir->i, name,
				    op_ctx->creds->caller_uid,
				    op_ctx->creds->caller_gid);
	} else {
		rc = ceph_ll_rmdir(export->cmount, dir->i, name,
				   op_ctx->creds->caller_uid,
				   op_ctx->creds->caller_gid);
	}

	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "Unlink %s returned %s (%d)",
			 name, strerror(-rc), -rc);
		return ceph2fsal_error(rc);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Open a ceph_fd.
 *
 * @param[in] myself      The ceph internal object handle
 * @param[in] openflags   Mode for open
 * @param[in] posix_flags POSIX open flags for open
 * @param[in] my_fd       The ceph_fd to open
 *
 * @return FSAL status.
 */

fsal_status_t ceph_open_my_fd(struct handle *myself,
			      fsal_openflags_t openflags,
			      int posix_flags,
			      struct ceph_fd *my_fd)
{
	int rc;
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);

	LogFullDebug(COMPONENT_FSAL,
		     "my_fd = %p my_fd->fd = %p openflags = %x, posix_flags = %x",
		     my_fd, my_fd->fd, openflags, posix_flags);

	assert(my_fd->fd == NULL
	       && my_fd->openflags == FSAL_O_CLOSED && openflags != 0);

	LogFullDebug(COMPONENT_FSAL,
		     "openflags = %x, posix_flags = %x",
		     openflags, posix_flags);

	rc = ceph_ll_open(export->cmount, myself->i, posix_flags,
			  &my_fd->fd, 0, 0);

	if (rc < 0) {
		my_fd->fd = NULL;
		LogFullDebug(COMPONENT_FSAL,
			     "open failed with %s",
			     strerror(-rc));
		return ceph2fsal_error(rc);
	}

	/* Save the file descriptor, make sure we only save the
	 * open modes that actually represent the open file.
	 */
	LogFullDebug(COMPONENT_FSAL,
		     "fd = %p, new openflags = %x",
		     my_fd->fd, openflags);

	my_fd->openflags = openflags;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t ceph_close_my_fd(struct handle *handle, struct ceph_fd *my_fd)
{
	int rc = 0;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	if (my_fd->fd != NULL && my_fd->openflags != FSAL_O_CLOSED) {
		rc = ceph_ll_close(handle->export->cmount, my_fd->fd);
		if (rc < 0)
			status = ceph2fsal_error(rc);
		my_fd->fd = NULL;
		my_fd->openflags = FSAL_O_CLOSED;
	}

	return status;
}

/**
 * @brief Function to open an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   Mode for open
 * @param[out] fd          File descriptor that is to be used
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_open_func(struct fsal_obj_handle *obj_hdl,
				    fsal_openflags_t openflags,
				    struct fsal_fd *fd)
{
	int posix_flags = 0;

	fsal2posix_openflags(openflags, &posix_flags);

	return ceph_open_my_fd(container_of(obj_hdl, struct handle, handle),
			       openflags, posix_flags, (struct ceph_fd *)fd);
}

/**
 * @brief Function to close an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  fd          File handle to close
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_close_func(struct fsal_obj_handle *obj_hdl,
				     struct fsal_fd *fd)
{
	return ceph_close_my_fd(container_of(obj_hdl, struct handle, handle),
				(struct ceph_fd *)fd);
}

/**
 * @brief Close a file
 *
 * This function closes a file, freeing resources used for read/write
 * access and releasing capabilities.
 *
 * @param[in] obj_hdl File to close
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_close(struct fsal_obj_handle *obj_hdl)
{
	fsal_status_t status;
	/* The private 'full' object handle */
	struct handle *handle = container_of(obj_hdl, struct handle, handle);

	/* Take write lock on object to protect file descriptor.
	 * This can block over an I/O operation.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

	status = ceph_close_my_fd(handle, &handle->fd);

	PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	return status;
}

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */

struct state_t *ceph_alloc_state(struct fsal_export *exp_hdl,
				 enum state_type state_type,
				 struct state_t *related_state)
{
	return init_state(gsh_calloc(1, sizeof(struct state_t)
					 + sizeof(struct ceph_fd)),
			  exp_hdl, state_type, related_state);
}

/**
 * @brief Merge a duplicate handle with an original handle
 *
 * This function is used if an upper layer detects that a duplicate
 * object handle has been created. It allows the FSAL to merge anything
 * from the duplicate back into the original.
 *
 * The caller must release the object (the caller may have to close
 * files if the merge is unsuccessful).
 *
 * @param[in]  orig_hdl  Original handle
 * @param[in]  dupe_hdl Handle to merge into original
 *
 * @return FSAL status.
 *
 */

fsal_status_t ceph_merge(struct fsal_obj_handle *orig_hdl,
			 struct fsal_obj_handle *dupe_hdl)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	if (orig_hdl->type == REGULAR_FILE &&
	    dupe_hdl->type == REGULAR_FILE) {
		/* We need to merge the share reservations on this file.
		 * This could result in ERR_FSAL_SHARE_DENIED.
		 */
		struct handle *orig, *dupe;

		orig = container_of(orig_hdl, struct handle, handle);
		dupe = container_of(dupe_hdl, struct handle, handle);

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&orig_hdl->lock);

		status = merge_share(&orig->share, &dupe->share);

		PTHREAD_RWLOCK_unlock(&orig_hdl->lock);
	}

	return status;
}

/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * This function opens a file for read or write, possibly creating it.
 * If the caller is passing a state, it must hold the state_lock
 * exclusive.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation), in which case the FSAL must assure protection
 * of any resources. If the file is being created, such protection is
 * simple since no one else will have access to the object yet, however,
 * in the case of an exclusive create, the common resources may still need
 * protection.
 *
 * If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the
 * parent directory.
 *
 * On an exclusive create, the upper layer may know the object handle
 * already, so it MAY call with name == NULL. In this case, the caller
 * expects just to check the verifier.
 *
 * On a call with an existing object handle for an UNCHECKED create,
 * we can set the size to 0.
 *
 * If attributes are not set on create, the FSAL will set some minimal
 * attributes (for example, mode might be set to 0600).
 *
 * If an open by name succeeds and did not result in Ganesha creating a file,
 * the caller will need to do a subsequent permission check to confirm the
 * open. This is because the permission attributes were not available
 * beforehand.
 *
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrib_set            Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */

fsal_status_t ceph_open2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state,
			 fsal_openflags_t openflags,
			 enum fsal_create_mode createmode,
			 const char *name,
			 struct attrlist *attrib_set,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **new_obj,
			 struct attrlist *attrs_out,
			 bool *caller_perm_check)
{
	int posix_flags = 0;
	int retval = 0;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	struct ceph_fd *my_fd = NULL;
	struct handle *myself, *hdl = NULL;
	struct stat stat;
	bool truncated;
	bool created = false;
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	struct Inode *i = NULL;
	Fh *fd;

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
		    "attrs ", attrib_set, false);

	if (state != NULL)
		my_fd = (struct ceph_fd *)(state + 1);

	myself = container_of(obj_hdl, struct handle, handle);

	fsal2posix_openflags(openflags, &posix_flags);

	truncated = (posix_flags & O_TRUNC) != 0;

	if (createmode >= FSAL_EXCLUSIVE) {
		/* Now fixup attrs for verifier if exclusive create */
		set_common_verifier(attrib_set, verifier);
	}

	if (name == NULL) {
		/* This is an open by handle */
		if (state != NULL) {
			/* Prepare to take the share reservation, but only if we
			 * are called with a valid state (if state is NULL the
			 * caller is a stateless create such as NFS v3 CREATE).
			 */

			/* This can block over an I/O operation. */
			PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

			/* Check share reservation conflicts. */
			status = check_share_conflict(&myself->share,
						      openflags, false);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
				return status;
			}

			/* Take the share reservation now by updating the
			 * counters.
			 */
			update_share_counters(&myself->share, FSAL_O_CLOSED,
					      openflags);

			PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
		} else {
			/* We need to use the global fd to continue, and take
			 * the lock to protect it.
			 */
			my_fd = &hdl->fd;
			PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);
		}

		status = ceph_open_my_fd(myself, openflags, posix_flags, my_fd);

		if (FSAL_IS_ERROR(status)) {
			if (state == NULL) {
				/* Release the lock taken above, and return
				 * since there is nothing to undo.
				 */
				PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
				return status;
			} else {
				/* Error - need to release the share */
				goto undo_share;
			}
		}

		if (createmode >= FSAL_EXCLUSIVE || truncated) {
			/* Refresh the attributes */
			retval = ceph_ll_getattr(export->cmount, myself->i,
						 &stat, 0, 0);

			if (retval == 0) {
				LogFullDebug(COMPONENT_FSAL,
					     "New size = %"PRIx64,
					     stat.st_size);
			} else {
				/* Because we have an inode ref, we never
				 * get EBADF like other FSALs might see.
				 */
				status = ceph2fsal_error(retval);
			}

			/* Now check verifier for exclusive, but not for
			 * FSAL_EXCLUSIVE_9P.
			 */
			if (!FSAL_IS_ERROR(status) &&
			    createmode >= FSAL_EXCLUSIVE &&
			    createmode != FSAL_EXCLUSIVE_9P &&
			    !check_verifier_stat(&stat, verifier)) {
				/* Verifier didn't match, return EEXIST */
				status =
				    fsalstat(posix2fsal_error(EEXIST), EEXIST);
			}
		}

		if (state == NULL) {
			/* If no state, release the lock taken above and return
			 * status.
			 */
			PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
			return status;
		}

		if (!FSAL_IS_ERROR(status)) {
			/* Return success. */
			return status;
		}

		(void) ceph_close_my_fd(myself, my_fd);

 undo_share:

		/* Can only get here with state not NULL and an error */

		/* On error we need to release our share reservation
		 * and undo the update of the share counters.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

		update_share_counters(&myself->share, openflags, FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

		return status;
	}

	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 * indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

	if (createmode == FSAL_NO_CREATE) {
		/* Non creation case, libcephfs doesn't have open by name so we
		 * have to do a lookup and then handle as an open by handle.
		 */
		struct fsal_obj_handle *temp = NULL;

		/* We don't have open by name... */
		status = obj_hdl->obj_ops.lookup(obj_hdl, name, &temp, NULL);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "lookup returned %s",
				     fsal_err_txt(status));
			return status;
		}

		/* Now call ourselves without name and attributes to open. */
		status = obj_hdl->obj_ops.open2(temp, state, openflags,
						FSAL_NO_CREATE, NULL, NULL,
						verifier, new_obj,
						attrs_out,
						caller_perm_check);

		if (FSAL_IS_ERROR(status)) {
			/* Release the object we found by lookup. */
			temp->obj_ops.release(temp);
			LogFullDebug(COMPONENT_FSAL,
				     "open returned %s",
				     fsal_err_txt(status));
		} else {
			/* No permission check was actually done... */
			*caller_perm_check = true;
		}

		return status;
	}

	/* Now add in O_CREAT and O_EXCL.
	 * Even with FSAL_UNGUARDED we try exclusive create first so
	 * we can safely set attributes.
	 */
	if (createmode != FSAL_NO_CREATE) {
		/* Now add in O_CREAT and O_EXCL. */
		posix_flags |= O_CREAT;

		/* And if we are at least FSAL_GUARDED, do an O_EXCL create. */
		if (createmode >= FSAL_GUARDED)
			posix_flags |= O_EXCL;

		/* Fetch the mode attribute to use in the openat system call. */
		unix_mode = fsal2unix_mode(attrib_set->mode) &
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

		/* Don't set the mode if we later set the attributes */
		FSAL_UNSET_MASK(attrib_set->mask, ATTR_MODE);
	}

	if (createmode == FSAL_UNCHECKED && (attrib_set->mask != 0)) {
		/* If we have FSAL_UNCHECKED and want to set more attributes
		 * than the mode, we attempt an O_EXCL create first, if that
		 * succeeds, then we will be allowed to set the additional
		 * attributes, otherwise, we don't know we created the file
		 * and this can NOT set the attributes.
		 */
		posix_flags |= O_EXCL;
	}

	retval = ceph_ll_create(export->cmount,  myself->i, name, unix_mode,
				posix_flags, &stat, &i, &fd,
				op_ctx->creds->caller_uid,
				op_ctx->creds->caller_gid);

	if (retval < 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "Create %s failed with %s",
			     name, strerror(-retval));
	}

	if (retval == -EEXIST && createmode == FSAL_UNCHECKED) {
		/* We tried to create O_EXCL to set attributes and failed.
		 * Remove O_EXCL and retry, also remember not to set attributes.
		 * We still try O_CREAT again just in case file disappears out
		 * from under us.
		 *
		 * Note that because we have dropped O_EXCL, later on we will
		 * not assume we created the file, and thus will not set
		 * additional attributes. We don't need to separately track
		 * the condition of not wanting to set attributes.
		 */
		posix_flags &= ~O_EXCL;
		retval =
		    ceph_ll_create(export->cmount,  myself->i, name, unix_mode,
				   posix_flags, &stat, &i, &my_fd->fd,
				   op_ctx->creds->caller_uid,
				   op_ctx->creds->caller_gid);
		if (retval < 0) {
			LogFullDebug(COMPONENT_FSAL,
				     "Non-exclusive Create %s failed with %s",
				     name, strerror(-retval));
		}
	}

	if (retval < 0) {
		return ceph2fsal_error(retval);
	}

	/* Remember if we were responsible for creating the file.
	 * Note that in an UNCHECKED retry we MIGHT have re-created the
	 * file and won't remember that. Oh well, so in that rare case we
	 * leak a partially created file if we have a subsequent error in here.
	 * Since we were able to do the permission check even if we were not
	 * creating the file, let the caller know the permission check has
	 * already been done. Note it IS possible in the case of a race between
	 * an UNCHECKED open and an external unlink, we did create the file.
	 */
	created = (posix_flags & O_EXCL) != 0;
	*caller_perm_check = false;

	construct_handle(&stat, i, export, &hdl);

	/* If we didn't have a state above, use the global fd. At this point,
	 * since we just created the global fd, no one else can have a
	 * reference to it, and thus we can mamnipulate unlocked which is
	 * handy since we can then call setattr2 which WILL take the lock
	 * without a double locking deadlock.
	 */
	if (my_fd == NULL)
		my_fd = &hdl->fd;

	my_fd->fd = fd;
	my_fd->openflags = openflags;

	*new_obj = &hdl->handle;

	if (created && attrib_set->mask != 0) {
		/* Set attributes using our newly opened file descriptor as the
		 * share_fd if there are any left to set (mode and truncate
		 * have already been handled).
		 *
		 * Note that we only set the attributes if we were responsible
		 * for creating the file and we have attributes to set.
		 */
		status = (*new_obj)->obj_ops.setattr2(*new_obj,
						      false,
						      state,
						      attrib_set);

		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			(*new_obj)->obj_ops.release(*new_obj);
			*new_obj = NULL;
			goto fileerr;
		}

		if (attrs_out != NULL) {
			status = (*new_obj)->obj_ops.getattrs(*new_obj,
							      attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes. Otherwise continue
				 * with attrs_out indicating ATTR_RDATTR_ERR.
				 */
				goto fileerr;
			}
		}
	} else if (attrs_out != NULL) {
		/* Since we haven't set any attributes other than what was set
		 * on create (if we even created), just use the stat results
		 * we used to create the fsal_obj_handle.
		 */
		posix2fsal_attributes(&stat, attrs_out);

		/* Make sure ATTR_RDATTR_ERR is cleared on success. */
		attrs_out->mask &= ~ATTR_RDATTR_ERR;
	}

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is
		 * a stateless create such as NFS v3 CREATE).
		 */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&(*new_obj)->lock);

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&hdl->share, FSAL_O_CLOSED, openflags);

		PTHREAD_RWLOCK_unlock(&(*new_obj)->lock);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:

	/* Close the file we just opened. */
	(void) ceph_close_my_fd(container_of(*new_obj, struct handle, handle),
				my_fd);

	if (created) {
		/* Remove the file we just created */
		ceph_ll_unlink(export->cmount, myself->i, name, 0, 0);
	}

	return status;
}

/**
 * @brief Return open status of a state.
 *
 * This function returns open flags representing the current open
 * status for a state. The state_lock must be held.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       File state to interrogate
 *
 * @retval Flags representing current open status
 */

fsal_openflags_t ceph_status2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state)
{
	struct ceph_fd *my_fd = (struct ceph_fd *)(state + 1);

	return my_fd->openflags;
}

/**
 * @brief Re-open a file that may be already opened
 *
 * This function supports changing the access mode of a share reservation and
 * thus should only be called with a share state. The state_lock must be held.
 *
 * This MAY be used to open a file the first time if there is no need for
 * open by name or create semantics. One example would be 9P lopen.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       state_t to use for this operation
 * @param[in] openflags   Mode for re-open
 *
 * @return FSAL status.
 */

fsal_status_t ceph_reopen2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   fsal_openflags_t openflags)
{
	struct ceph_fd fd, *my_fd = &fd, *my_share_fd;
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	fsal_status_t status = {0, 0};
	int posix_flags = 0;
	fsal_openflags_t old_openflags;

	my_share_fd = (struct ceph_fd *)(state + 1);

	fsal2posix_openflags(openflags, &posix_flags);

	memset(my_fd, 0, sizeof(*my_fd));

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

	old_openflags = my_share_fd->openflags;

	/* We can conflict with old share, so go ahead and check now. */
	status = check_share_conflict(&myself->share, openflags, false);

	if (FSAL_IS_ERROR(status)) {
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

		return status;
	}

	/* Set up the new share so we can drop the lock and not have a
	 * conflicting share be asserted, updating the share counters.
	 */
	update_share_counters(&myself->share, old_openflags, openflags);

	PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	status = ceph_open_my_fd(myself, openflags, posix_flags, my_fd);

	if (!FSAL_IS_ERROR(status)) {
		/* Close the existing file descriptor and copy the new
		 * one over.
		 */
		ceph_close_my_fd(myself, my_share_fd);
		*my_share_fd = fd;
	} else {
		/* We had a failure on open - we need to revert the share.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

		update_share_counters(&myself->share, openflags, old_openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
	}

	return status;
}

/**
 * @brief Find a file descriptor for a read or write operation.
 *
 * We do not need file descriptors for non-regular files, so this never has to
 * handle them.
 */
fsal_status_t ceph_find_fd(Fh **fd,
			   struct fsal_obj_handle *obj_hdl,
			   bool bypass,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   bool *has_lock,
			   bool *need_fsync,
			   bool *closefd,
			   bool open_for_locks)
{
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	struct ceph_fd temp_fd = {0, NULL}, *out_fd = &temp_fd;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	status = fsal_find_fd((struct fsal_fd **)&out_fd, obj_hdl,
			      (struct fsal_fd *)&myself->fd, &myself->share,
			      bypass, state, openflags,
			      ceph_open_func, ceph_close_func,
			      has_lock, need_fsync,
			      closefd, open_for_locks);

	LogFullDebug(COMPONENT_FSAL,
		     "fd = %p", out_fd->fd);
	*fd = out_fd->fd;
	return status;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any deny read
 * @param[in]     state          state_t to use for this operation
 * @param[in]     offset         Position from which to read
 * @param[in]     buffer_size    Amount of data to read
 * @param[out]    buffer         Buffer to which data are to be copied
 * @param[out]    read_amount    Amount of data read
 * @param[out]    end_of_file    true if the end of file has been reached
 * @param[in,out] info           more information about the data
 *
 * @return FSAL status.
 */

fsal_status_t ceph_read2(struct fsal_obj_handle *obj_hdl,
			bool bypass,
			struct state_t *state,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *read_amount,
			bool *end_of_file,
			struct io_info *info)
{
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	Fh *my_fd = NULL;
	ssize_t nb_read;
	fsal_status_t status;
	bool has_lock = false;
	bool need_fsync = false;
	bool closefd = false;
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);

	if (info != NULL) {
		/* Currently we don't support READ_PLUS */
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, bypass, state, FSAL_O_READ,
			      &has_lock, &need_fsync, &closefd, false);

	if (FSAL_IS_ERROR(status))
		goto out;

	nb_read =
	    ceph_ll_read(export->cmount, my_fd, offset, buffer_size, buffer);

	if (offset == -1 || nb_read < 0) {
		status = ceph2fsal_error(nb_read);
		goto out;
	}

	*read_amount = nb_read;

	*end_of_file = nb_read == 0;

#if 0
	/** @todo
	 *
	 * Is this all we really need to do to support READ_PLUS? Will anyone
	 * ever get upset that we don't return holes, even for blocks of all
	 * zeroes?
	 *
	 */
	if (info != NULL) {
		info->io_content.what = NFS4_CONTENT_DATA;
		info->io_content.data.d_offset = offset + nb_read;
		info->io_content.data.d_data.data_len = nb_read;
		info->io_content.data.d_data.data_val = buffer;
	}
#endif

 out:

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	return status;
}

/**
 * @brief Write data to a file
 *
 * This function writes data to a file. The FSAL must be able to
 * perform the write whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. Even
 * with bypass == true, it will enforce a mandatory (NFSv4) deny_write if
 * an appropriate state is not passed).
 *
 * The FSAL is expected to enforce sync if necessary.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any non-mandatory deny write
 * @param[in]     state          state_t to use for this operation
 * @param[in]     offset         Position at which to write
 * @param[in]     buffer         Data to be written
 * @param[in,out] fsal_stable    In, if on, the fsal is requested to write data
 *                               to stable store. Out, the fsal reports what
 *                               it did.
 * @param[in,out] info           more information about the data
 *
 * @return FSAL status.
 */

fsal_status_t ceph_write2(struct fsal_obj_handle *obj_hdl,
			 bool bypass,
			 struct state_t *state,
			 uint64_t offset,
			 size_t buffer_size,
			 void *buffer,
			 size_t *wrote_amount,
			 bool *fsal_stable,
			 struct io_info *info)
{
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	ssize_t nb_written;
	fsal_status_t status;
	int retval = 0;
	Fh *my_fd = NULL;
	bool has_lock = false;
	bool need_fsync = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);

	if (info != NULL) {
		/* Currently we don't support WRITE_PLUS */
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (*fsal_stable)
		openflags |= FSAL_O_SYNC;

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			      &has_lock, &need_fsync, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		goto out;
	}

	fsal_set_credentials(op_ctx->creds);

	nb_written =
	    ceph_ll_write(export->cmount, my_fd, offset, buffer_size, buffer);

	if (nb_written < 0) {
		status = ceph2fsal_error(nb_written);
		goto out;
	}

	*wrote_amount = nb_written;

	/* attempt stability if we aren't using an O_SYNC fd */
	if (need_fsync) {
		retval = ceph_ll_fsync(export->cmount, my_fd, false);

		if (retval < 0)
			status = ceph2fsal_error(retval);
	}

 out:

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	fsal_restore_ganesha_credentials();
	return status;
}

/**
 * @brief Commit written data
 *
 * This function flushes possibly buffered data to a file. This method
 * differs from commit due to the need to interact with share reservations
 * and the fact that the FSAL manages the state of "file descriptors". The
 * FSAL must be able to perform this operation without being passed a specific
 * state.
 *
 * @param[in] obj_hdl          File on which to operate
 * @param[in] state            state_t to use for this operation
 * @param[in] offset           Start of range to commit
 * @param[in] len              Length of range to commit
 *
 * @return FSAL status.
 */

fsal_status_t ceph_commit2(struct fsal_obj_handle *obj_hdl,
			   off_t offset,
			   size_t len)
{
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	fsal_status_t status;
	int retval;
	struct ceph_fd temp_fd = {0, NULL}, *out_fd = &temp_fd;
	bool has_lock = false;
	bool closefd = false;
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);

	/* Make sure file is open in appropriate mode.
	 * Do not check share reservation.
	 */
	status = fsal_reopen_obj(obj_hdl, false, false, FSAL_O_WRITE,
				 (struct fsal_fd *)&myself->fd, &myself->share,
				 ceph_open_func, ceph_close_func,
				 (struct fsal_fd **)&out_fd, &has_lock,
				 &closefd);

	if (!FSAL_IS_ERROR(status)) {
		retval = ceph_ll_fsync(export->cmount, out_fd->fd, false);

		if (retval < 0)
			status = ceph2fsal_error(retval);
	}

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, out_fd->fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	return status;
}

#ifdef USE_FSAL_CEPH_SETLK
/**
 * @brief Perform a lock operation
 *
 * This function performs a lock operation (lock, unlock, test) on a
 * file. This method assumes the FSAL is able to support lock owners,
 * though it need not support asynchronous blocking locks. Passing the
 * lock state allows the FSAL to associate information with a specific
 * lock owner for each file (which may include use of a "file descriptor".
 *
 * For FSAL_VFS etc. we ignore owner, implicitly we have a lock_fd per
 * lock owner (i.e. per state).
 *
 * @param[in]  obj_hdl          File on which to operate
 * @param[in]  state            state_t to use for this operation
 * @param[in]  owner            Lock owner
 * @param[in]  lock_op          Operation to perform
 * @param[in]  request_lock     Lock to take/release/test
 * @param[out] conflicting_lock Conflicting lock
 *
 * @return FSAL status.
 */
fsal_status_t ceph_lock_op2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state,
			    void *owner,
			    fsal_lock_op_t lock_op,
			    fsal_lock_param_t *request_lock,
			    fsal_lock_param_t *conflicting_lock)
{
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	struct flock lock_args;
	fsal_status_t status = {0, 0};
	int retval = 0;
	Fh *my_fd = NULL;
	bool has_lock = false;
	bool need_fsync = false;
	bool closefd = false;
	bool bypass = false;
	fsal_openflags_t openflags = FSAL_O_RDWR;
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);

	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d start:%" PRIu64 " length:%"
		     PRIu64 " ",
		     lock_op, request_lock->lock_type, request_lock->lock_start,
		     request_lock->lock_length);

	if (lock_op == FSAL_OP_LOCKT) {
		/* We may end up using global fd, don't fail on a deny mode */
		bypass = true;
		openflags = FSAL_O_ANY;
	} else if (lock_op == FSAL_OP_LOCK) {
		if (request_lock->lock_type == FSAL_LOCK_R)
			openflags = FSAL_O_READ;
		else if (request_lock->lock_type == FSAL_LOCK_W)
			openflags = FSAL_O_WRITE;
	} else if (lock_op == FSAL_OP_UNLOCK) {
		openflags = FSAL_O_ANY;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Lock operation requested was not TEST, READ, or WRITE.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (lock_op != FSAL_OP_LOCKT && state == NULL) {
		LogCrit(COMPONENT_FSAL, "Non TEST operation with NULL state");
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	if (request_lock->lock_type == FSAL_LOCK_R) {
		lock_args.l_type = F_RDLCK;
	} else if (request_lock->lock_type == FSAL_LOCK_W) {
		lock_args.l_type = F_WRLCK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (lock_op == FSAL_OP_UNLOCK)
		lock_args.l_type = F_UNLCK;

	lock_args.l_pid = 0;
	lock_args.l_len = request_lock->lock_length;
	lock_args.l_start = request_lock->lock_start;
	lock_args.l_whence = SEEK_SET;

	/* flock.l_len being signed long integer, larger lock ranges may
	 * get mapped to negative values. As per 'man 3 fcntl', posix
	 * locks can accept negative l_len values which may lead to
	 * unlocking an unintended range. Better bail out to prevent that.
	 */
	if (lock_args.l_len < 0) {
		LogCrit(COMPONENT_FSAL,
			"The requested lock length is out of range- lock_args.l_len(%ld), request_lock_length(%"
			PRIu64 ")",
			lock_args.l_len, request_lock->lock_length);
		return fsalstat(ERR_FSAL_BAD_RANGE, 0);
	}

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			      &has_lock, &need_fsync, &closefd, true);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_FSAL, "Unable to find fd for lock operation");
		return status;
	}

	if (lock_op == FSAL_OP_LOCKT) {
		retval = ceph_ll_getlk(export->cmount, my_fd, &lock_args,
				       (uint64_t) owner);
	} else {
		retval = ceph_ll_setlk(export->cmount, my_fd, &lock_args,
				       (uint64_t) owner, false);
	}

	if (retval < 0) {
		LogDebug(COMPONENT_FSAL,
			 "%s returned %d %s",
			 lock_op == FSAL_OP_LOCKT
				? "ceph_ll_getlk" : "ceph_ll_setlk",
			 -retval, strerror(-retval));

		if (conflicting_lock != NULL) {
			/* Get the conflicting lock */
			retval = ceph_ll_getlk(export->cmount, my_fd,
					       &lock_args, (uint64_t) owner);

			if (retval < 0) {
				LogCrit(COMPONENT_FSAL,
					"After failing a lock request, I couldn't even get the details of who owns the lock, error %d %s",
					-retval, strerror(-retval));
				goto err;
			}

			if (conflicting_lock != NULL) {
				conflicting_lock->lock_length = lock_args.l_len;
				conflicting_lock->lock_start =
				    lock_args.l_start;
				conflicting_lock->lock_type = lock_args.l_type;
			}
		}

		goto err;
	}

	/* F_UNLCK is returned then the tested operation would be possible. */
	if (conflicting_lock != NULL) {
		if (lock_op == FSAL_OP_LOCKT && lock_args.l_type != F_UNLCK) {
			conflicting_lock->lock_length = lock_args.l_len;
			conflicting_lock->lock_start = lock_args.l_start;
			conflicting_lock->lock_type = lock_args.l_type;
		} else {
			conflicting_lock->lock_length = 0;
			conflicting_lock->lock_start = 0;
			conflicting_lock->lock_type = FSAL_NO_LOCK;
		}
	}

	/* Fall through (retval == 0) */

 err:

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	return ceph2fsal_error(retval);
}
#endif

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object.  Which attributes are
 * set is determined by attrib_set->mask. The FSAL must manage bypass
 * or not of share reservations, and a state may be passed.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 * @param[in] attrib_set Attributes to set
 *
 * @return FSAL status.
 */
fsal_status_t ceph_setattr2(struct fsal_obj_handle *obj_hdl,
			    bool bypass,
			    struct state_t *state,
			    struct attrlist *attrib_set)
{
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	fsal_status_t status = {0, 0};
	int rc = 0;
	bool has_lock = false;
	bool need_fsync = false;
	bool closefd = false;
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* Stat buffer */
	struct stat st;
	/* Mask of attributes to set */
	uint32_t mask = 0;

	if (attrib_set->mask & ~settable_attributes) {
		LogDebug(COMPONENT_FSAL,
			 "bad mask %"PRIx64" not settable %"PRIx64,
			 attrib_set->mask,
			 attrib_set->mask & ~settable_attributes);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
		    "attrs ", attrib_set, false);

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_MODE))
		attrib_set->mode &=
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	/* Test if size is being set, make sure file is regular and if so,
	 * require a read/write file descriptor.
	 */
	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			LogFullDebug(COMPONENT_FSAL,
				     "Setting size on non-regular file");
			return fsalstat(ERR_FSAL_INVAL, EINVAL);
		}

		/* We don't actually need an open fd, we are just doing the
		 * share reservation checking, thus the NULL parameters.
		 */
		status = fsal_find_fd(NULL, obj_hdl, NULL, &myself->share,
				      bypass, state, FSAL_O_RDWR, NULL, NULL,
				      &has_lock, &need_fsync, &closefd, false);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "fsal_find_fd status=%s",
				     fsal_err_txt(status));
			goto out;
		}
	}

	memset(&st, 0, sizeof(struct stat));

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_SIZE)) {
		rc = ceph_ll_truncate(export->cmount, myself->i,
				      attrib_set->filesize, 0, 0);

		if (rc < 0) {
			status = ceph2fsal_error(rc);
			LogDebug(COMPONENT_FSAL,
				 "truncate returned %s (%d)",
				 strerror(-rc), -rc);
			goto out;
		}
	}

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_MODE)) {
		mask |= CEPH_SETATTR_MODE;
		st.st_mode = fsal2unix_mode(attrib_set->mode);
	}

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_OWNER)) {
		mask |= CEPH_SETATTR_UID;
		st.st_uid = attrib_set->owner;
	}

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_GROUP)) {
		mask |= CEPH_SETATTR_GID;
		st.st_gid = attrib_set->group;
	}

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_ATIME)) {
		mask |= CEPH_SETATTR_ATIME;
		st.st_atim = attrib_set->atime;
	}

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_ATIME_SERVER)) {
		mask |= CEPH_SETATTR_ATIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			LogDebug(COMPONENT_FSAL,
				 "clock_gettime returned %s (%d)",
				 strerror(errno), errno);
			status = fsalstat(posix2fsal_error(errno), errno);
			goto out;
		}
		st.st_atim = timestamp;
	}

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_MTIME)) {
		mask |= CEPH_SETATTR_MTIME;
		st.st_mtim = attrib_set->mtime;
	}
	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_MTIME_SERVER)) {
		mask |= CEPH_SETATTR_MTIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			LogDebug(COMPONENT_FSAL,
				 "clock_gettime returned %s (%d)",
				 strerror(errno), errno);
			status = fsalstat(posix2fsal_error(errno), errno);
			goto out;
		}
		st.st_mtim = timestamp;
	}

	if (FSAL_TEST_MASK(attrib_set->mask, ATTR_CTIME)) {
		mask |= CEPH_SETATTR_CTIME;
		st.st_ctim = attrib_set->ctime;
	}

	rc = ceph_ll_setattr(export->cmount, myself->i, &st, mask, 0, 0);

	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "setattr returned %s (%d)",
			 strerror(-rc), -rc);
		status = ceph2fsal_error(rc);
	} else {
		/* Success */
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

 out:

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	return status;
}

/**
 * @brief Manage closing a file when a state is no longer needed.
 *
 * When the upper layers are ready to dispense with a state, this method is
 * called to allow the FSAL to close any file descriptors or release any other
 * resources associated with the state. A call to free_state should be assumed
 * to follow soon.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 *
 * @return FSAL status.
 */

fsal_status_t ceph_close2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state)
{
	struct handle *myself = container_of(obj_hdl, struct handle, handle);
	struct ceph_fd *my_fd = (struct ceph_fd *)(state + 1);

	if (state->state_type == STATE_TYPE_SHARE ||
	    state->state_type == STATE_TYPE_NLM_SHARE ||
	    state->state_type == STATE_TYPE_9P_FID) {
		/* This is a share state, we must update the share counters */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

		update_share_counters(&myself->share,
				      my_fd->openflags,
				      FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
	}

	return ceph_close_my_fd(myself, my_fd);
}

/**
 * @brief Write wire handle
 *
 * This function writes a 'wire' handle to be sent to clients and
 * received from the.
 *
 * @param[in]     handle_pub  Handle to digest
 * @param[in]     output_type Type of digest requested
 * @param[in,out] fh_desc     Location/size of buffer for
 *                            digest/Length modified to digest length
 *
 * @return FSAL status.
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *handle_pub,
				   uint32_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	/* The private 'full' object handle */
	const struct handle *handle =
	    container_of(handle_pub, const struct handle, handle);

	switch (output_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < sizeof(handle->vi)) {
			LogMajor(COMPONENT_FSAL,
				 "digest_handle: space too small for handle.  Need %zu, have %zu",
				 sizeof(vinodeno_t), fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		} else {
			memcpy(fh_desc->addr, &handle->vi, sizeof(vinodeno_t));
			fh_desc->len = sizeof(handle->vi);
		}
		break;

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Give a hash key for file handle
 *
 * This function locates a unique hash key for a given file.
 *
 * @param[in]  handle_pub The file whose key is to be found
 * @param[out] fh_desc    Address and length of key
 */

static void handle_to_key(struct fsal_obj_handle *handle_pub,
			  struct gsh_buffdesc *fh_desc)
{
	/* The private 'full' object handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);

	fh_desc->addr = &handle->vi;
	fh_desc->len = sizeof(vinodeno_t);
}

/**
 * @brief Override functions in ops vector
 *
 * This function overrides implemented functions in the ops vector
 * with versions for this FSAL.
 *
 * @param[in] ops Handle operations vector
 */

void handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->merge = ceph_merge;
	ops->lookup = lookup;
	ops->mkdir = ceph_fsal_mkdir;
	ops->mknode = ceph_fsal_mknode;
	ops->readdir = ceph_fsal_readdir;
	ops->symlink = ceph_fsal_symlink;
	ops->readlink = ceph_fsal_readlink;
	ops->getattrs = getattrs;
	ops->link = ceph_fsal_link;
	ops->rename = ceph_fsal_rename;
	ops->unlink = ceph_fsal_unlink;
	ops->close = ceph_fsal_close;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
	ops->open2 = ceph_open2;
	ops->status2 = ceph_status2;
	ops->reopen2 = ceph_reopen2;
	ops->read2 = ceph_read2;
	ops->write2 = ceph_write2;
	ops->commit2 = ceph_commit2;
#ifdef USE_FSAL_CEPH_SETLK
	ops->lock_op2 = ceph_lock_op2;
#endif
	ops->setattr2 = ceph_setattr2;
	ops->close2 = ceph_close2;
#ifdef CEPH_PNFS
	handle_ops_pnfs(ops);
#endif				/* CEPH_PNFS */
}
