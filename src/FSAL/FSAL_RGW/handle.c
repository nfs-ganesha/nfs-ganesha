/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat Inc., 2015
 * Author: Orit Wasserman <owasserm@redhat.com>
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
 * RGW object (file|dir) handle object
 */

#include <fcntl.h>
#include <cephfs/libcephfs.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"

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
	struct rgw_handle *obj = container_of(obj_pub, struct rgw_handle,
					      handle);

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
			    const char *path, struct fsal_obj_handle **obj_pub)
{
	/* Generic status return */
	int rc = 0;
	/* Stat output */
	struct stat st;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	struct rgw_handle *dir = container_of(dir_pub, struct rgw_handle,
					      handle);
	struct rgw_handle *obj = NULL;
	uint64_t i;

	rc = rgw_lookup(export->cmount, dir, path, &st, &i, 0, 0);

	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(&st, i, export, &obj);

	if (rc < 0) {
		return rgw2fsal_error(rc);
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

static fsal_status_t fsal_readdir(struct fsal_obj_handle *dir_pub,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, bool *eof)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *dir = container_of(dir_pub, struct rgw_handle,
					      handle);
	/* The director descriptor */
	struct rgw_dir_result *dir_desc = NULL;
	/* Return status */
	fsal_status_t fsal_status = { ERR_FSAL_NO_ERROR, 0 };

	rc = rgw_readdir(export->cmount, dir, &dir_desc);
	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsal_status;
}

/**
 * @brief Create a regular file
 *
 * This function creates an empty, regular file.
 *
 * @param[in]  dir_pub Directory in which to create the file
 * @param[in]  name    Name of file to create
 * @param[out] attrib  Attributes of newly created file
 * @param[out] obj_pub Handle for newly created file
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_create(struct fsal_obj_handle *dir_pub,
				 const char *name, struct attrlist *attrib,
				 struct fsal_obj_handle **obj_pub)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *dir = container_of(dir_pub, struct rgw_handle,
					      handle);
	/* Newly opened file descriptor */
	uint64_t i;
	/* Status after create */
	struct stat st;
	mode_t unix_mode;
	/* Newly created object */
	struct rgw_handle *obj;

	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	rc = rgw_create(export->cmount, dir, name, unix_mode, &st,
			&i);
	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(&st, i, export, &obj);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	*obj_pub = &obj->handle;
	*attrib = obj->handle.attributes;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a directory
 *
 * This funcion creates a new directory.
 *
 * @param[in]  dir_pub The parent in which to create
 * @param[in]  name    Name of the directory to create
 * @param[out] attrib  Attributes of the newly created directory
 * @param[out] obj_pub Handle of the newly created directory
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_mkdir(struct fsal_obj_handle *dir_pub,
				const char *name, struct attrlist *attrib,
				struct fsal_obj_handle **obj_pub)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *dir = container_of(dir_pub, struct rgw_handle,
					      handle);
	/* Stat result */
	struct stat st;
	mode_t unix_mode;
	/* Newly created object */
	struct rgw_handle *obj = NULL;
	uint64_t i;

	unix_mode = fsal2unix_mode(attrib->mode)
		& ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
	rc = rgw_mkdir(export->cmount, dir, name, unix_mode, &st, &i);

	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(&st, i, export, &obj);

	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	*obj_pub = &obj->handle;
	*attrib = obj->handle.attributes;

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

static fsal_status_t getattrs(struct fsal_obj_handle *handle_pub)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);
	/* Stat buffer */
	struct stat st;

	rc = rgw_getattr(export->cmount, handle, &st);

	if (rc < 0)
		return rgw2fsal_error(rc);

	rgw2fsal_attributes(&st, &handle->handle.attributes);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set attributes on a file
 *
 * This function sets attributes on a file.
 *
 * @param[in] handle_pub File to modify.
 * @param[in] attrs      Attributes to set.
 *
 * @return FSAL status.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *handle_pub,
			      struct attrlist *attrs)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);
	/* Stat buffer */
	struct stat st;
	/* Mask of attributes to set */
	uint32_t mask = 0;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
		attrs->mode &= ~op_ctx->fsal_export->exp_ops.
			fs_umask(op_ctx->fsal_export);

	memset(&st, 0, sizeof(struct stat));

	if (attrs->mask & ~rgw_settable_attributes)
		return fsalstat(ERR_FSAL_INVAL, 0);

/*
	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		rc = rgw_ll_truncate(export->cmount, handle->i,
				      attrs->filesize, 0, 0);

		if (rc < 0)
			return rgw2fsal_error(rc);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		mask |= RGW_SETATTR_MODE;
		st.st_mode = fsal2unix_mode(attrs->mode);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)) {
		mask |= RGW_SETATTR_UID;
		st.st_uid = attrs->owner;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)) {
		mask |= RGW_SETATTR_UID;
		st.st_gid = attrs->group;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
		mask |= RGW_SETATTR_ATIME;
		st.st_atim = attrs->atime;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
		mask |= RGW_SETATTR_ATIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0)
			return rgw2fsal_error(rc);
		st.st_atim = timestamp;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
		mask |= RGW_SETATTR_MTIME;
		st.st_mtim = attrs->mtime;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
		mask |= RGW_SETATTR_MTIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0)
			return rgw2fsal_error(rc);
		st.st_mtim = timestamp;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_CTIME)) {
		mask |= RGW_SETATTR_CTIME;
		st.st_ctim = attrs->ctime;
	}
*/
	rc = rgw_setattr(export->cmount, handle, &st, mask);

	if (rc < 0)
		return rgw2fsal_error(rc);

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

static fsal_status_t fsal_rename(struct fsal_obj_handle *obj_hdl,
				 struct fsal_obj_handle *olddir_pub,
				 const char *old_name,
				 struct fsal_obj_handle *newdir_pub,
				 const char *new_name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *olddir = container_of(olddir_pub, struct rgw_handle,
						 handle);
	/* The private 'full' destination directory handle */
	struct rgw_handle *newdir = container_of(newdir_pub, struct rgw_handle,
						 handle);

	rc = rgw_rename(export->cmount, olddir, old_name, newdir,
			new_name);

	if (rc < 0)
		return rgw2fsal_error(rc);

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

static fsal_status_t fsal_unlink(struct fsal_obj_handle *dir_pub,
				 const char *name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *dir = container_of(dir_pub, struct rgw_handle,
					      handle);

	rc = rgw_unlink(export->cmount, dir, name);
	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Open a file for read or write
 *
 * This function opens a file for reading or writing.  No lock is
 * taken, because we assume we are protected by the Cache inode
 * content lock.
 *
 * @param[in] handle_pub File to open
 * @param[in] openflags  Mode to open in
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_open(struct fsal_obj_handle *handle_pub,
			       fsal_openflags_t openflags)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);
	/* Posix open flags */
	int posix_flags = 0;

	if (openflags & FSAL_O_RDWR)
		posix_flags = O_RDWR;
	else if (openflags & FSAL_O_READ)
		posix_flags = O_RDONLY;
	else if (openflags & FSAL_O_WRITE)
		posix_flags = O_WRONLY;

	/* We shouldn't need to lock anything, the content lock
	   should keep the file descriptor protected. */

	if (handle->openflags != FSAL_O_CLOSED)
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);

	rc = rgw_open(export, handle, posix_flags);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	handle->openflags = openflags;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Return the open status of a file
 *
 * This function returns the open status (the open mode last used to
 * open the file, in our case) for a given file.
 *
 * @param[in] handle_pub File to interrogate.
 *
 * @return Open mode.
 */

static fsal_openflags_t status(struct fsal_obj_handle *handle_pub)
{
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);

	return handle->openflags;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from an open file.
 *
 * We take no lock, since we assume we are protected by the
 * Cache inode content lock.
 *
 * @param[in]  handle_pub  File to read
 * @param[in]  offset      Point at which to begin read
 * @param[in]  buffer_size Maximum number of bytes to read
 * @param[out] buffer      Buffer to store data read
 * @param[out] read_amount Count of bytes read
 * @param[out] end_of_file true if the end of file is reached
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_read(struct fsal_obj_handle *handle_pub,
			       uint64_t offset, size_t buffer_size,
			       void *buffer, size_t *read_amount,
			       bool *end_of_file)
{
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);
	/* Signed, so we can pick up on errors */
	int64_t nb_read = 0;

	nb_read =
	    rgw_read(export->cmount, handle, offset, buffer_size,
			 buffer);

	if (nb_read < 0)
		return rgw2fsal_error(nb_read);

	if ((uint64_t) nb_read < buffer_size)
		*end_of_file = true;

	*read_amount = nb_read;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Write data to file
 *
 * This function writes data to an open file.
 *
 * We take no lock, since we assume we are protected by the Cache
 * inode content lock.
 *
 * @param[in]  handle_pub   File to write
 * @param[in]  offset       Position at which to write
 * @param[in]  buffer_size  Number of bytes to write
 * @param[in]  buffer       Data to write
 * @param[out] write_amount Number of bytes written
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_write(struct fsal_obj_handle *handle_pub,
				uint64_t offset, size_t buffer_size,
				void *buffer, size_t *write_amount,
				bool *fsal_stable)
{
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);
	/* Signed, so we can pick up on errors */
	int64_t nb_written = 0;

	nb_written =
	    rgw_write(export->cmount, handle, offset, buffer_size, buffer);

	if (nb_written < 0)
		return rgw2fsal_error(nb_written);

	*write_amount = nb_written;
	*fsal_stable = false;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Commit written data
 *
 * This function commits written data to stable storage.  This FSAL
 * commits data from the entire file, rather than within the given
 * range.
 *
 * @param[in] handle_pub File to commit
 * @param[in] offset     Start of range to commit
 * @param[in] len        Size of range to commit
 *
 * @return FSAL status.
 */

static fsal_status_t commit(struct fsal_obj_handle *handle_pub,
			    off_t offset,
			    size_t len)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);

	rc = rgw_fsync(export->cmount, handle);

	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Close a file
 *
 * This function closes a file, freeing resources used for read/write
 * access and releasing capabilities.
 *
 * @param[in] handle_pub File to close
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_close(struct fsal_obj_handle *handle_pub)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);

	rc = rgw_close(handle->export->cmount, handle);

	if (rc < 0)
		return rgw2fsal_error(rc);

	handle->openflags = FSAL_O_CLOSED;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
	const struct rgw_handle *handle =
	    container_of(handle_pub, const struct rgw_handle, handle);

	switch (output_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
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
	struct rgw_handle *handle = container_of(handle_pub, struct rgw_handle,
						 handle);

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
	ops->lookup = lookup;
	ops->create = fsal_create;
	ops->mkdir = fsal_mkdir;
	ops->readdir = fsal_readdir;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->rename = fsal_rename;
	ops->unlink = fsal_unlink;
	ops->open = fsal_open;
	ops->status = status;
	ops->read = fsal_read;
	ops->write = fsal_write;
	ops->commit = commit;
	ops->close = fsal_close;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
}
