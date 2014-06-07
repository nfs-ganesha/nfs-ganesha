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
			    const char *path, struct fsal_obj_handle **obj_pub)
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

	rc = ceph_ll_lookup(export->cmount, dir->i, path, &st, &i, 0, 0);

	if (rc < 0)
		return ceph2fsal_error(rc);

	rc = construct_handle(&st, i, export, &obj);

	if (rc < 0) {
		ceph_ll_put(export->cmount, i);
		return ceph2fsal_error(rc);
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
			/* skip . and .. */
			if ((strcmp(de.d_name, ".") == 0)
			    || (strcmp(de.d_name, "..") == 0)) {
				continue;
			}

			if (!cb(de.d_name, dir_state, de.d_off))
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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *dir = container_of(dir_pub, struct handle, handle);
	/* Newly opened file descriptor */
	struct Inode *i = NULL;
	/* Status after create */
	struct stat st;
	/* Newly created object */
	struct handle *obj;

	rc = ceph_ll_create(export->cmount, dir->i, name, 0600, O_CREAT, &st,
			    &i, NULL, op_ctx->creds->caller_uid,
			    op_ctx->creds->caller_gid);
	if (rc < 0)
		return ceph2fsal_error(rc);

	rc = construct_handle(&st, i, export, &obj);

	if (rc < 0) {
		ceph_ll_put(export->cmount, i);
		return ceph2fsal_error(rc);
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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *dir = container_of(dir_pub, struct handle, handle);
	/* Stat result */
	struct stat st;
	/* Newly created object */
	struct handle *obj = NULL;
	struct Inode *i = NULL;

	rc = ceph_ll_mkdir(export->cmount, dir->i, name, 0700, &st, &i,
			   op_ctx->creds->caller_uid,
			   op_ctx->creds->caller_gid);

	if (rc < 0)
		return ceph2fsal_error(rc);

	rc = construct_handle(&st, i, export, &obj);

	if (rc < 0) {
		ceph_ll_put(export->cmount, i);
		return ceph2fsal_error(rc);
	}

	*obj_pub = &obj->handle;
	*attrib = obj->handle.attributes;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @name Crete a symlink
 *
 * This function creates a new symlink with the given content.
 *
 * @param[in]  dir_pub   Parent directory
 * @param[in]  name      Name of the link
 * @param[in]  link_path Path linked to
 * @param[out] attrib    Attributes of the new symlink
 * @param[out] obj_pub   Handle for new symlink
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_symlink(struct fsal_obj_handle *dir_pub,
				  const char *name, const char *link_path,
				  struct attrlist *attrib,
				  struct fsal_obj_handle **obj_pub)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *dir = container_of(dir_pub, struct handle, handle);
	/* Stat result */
	struct stat st;
	struct Inode *i = NULL;
	/* Newly created object */
	struct handle *obj = NULL;

	rc = ceph_ll_symlink(export->cmount, dir->i, name, link_path, &st, &i,
			     op_ctx->creds->caller_uid,
			     op_ctx->creds->caller_gid);
	if (rc < 0)
		return ceph2fsal_error(rc);

	rc = construct_handle(&st, i, export, &obj);
	if (rc < 0) {
		ceph_ll_put(export->cmount, i);
		return ceph2fsal_error(rc);
	}

	*obj_pub = &obj->handle;
	*attrib = obj->handle.attributes;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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

static fsal_status_t fsal_readlink(struct fsal_obj_handle *link_pub,
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

	/* Content points into a static buffer in the Ceph client's
	   cache, so we don't have to free it. */

	rc = ceph_ll_readlink(export->cmount, link->i, content,
			      sizeof(content), 0, 0);

	if (rc < 0)
		return ceph2fsal_error(rc);

	content_buf->len = (strlen(content) + 1);
	content_buf->addr = gsh_malloc(content_buf->len);
	if (content_buf->addr == NULL)
		return fsalstat(ERR_FSAL_NOMEM, 0);
	memcpy(content_buf->addr, content, content_buf->len);
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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);
	/* Stat buffer */
	struct stat st;

	rc = ceph_ll_getattr(export->cmount, handle->i, &st, 0, 0);

	if (rc < 0)
		return ceph2fsal_error(rc);

	ceph2fsal_attributes(&st, &handle->handle.attributes);

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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' directory handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);
	/* Stat buffer */
	struct stat st;
	/* Mask of attributes to set */
	uint32_t mask = 0;

	memset(&st, 0, sizeof(struct stat));

	if (attrs->mask & ~settable_attributes)
		return fsalstat(ERR_FSAL_INVAL, 0);

	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		rc = ceph_ll_truncate(export->cmount, handle->i,
				      attrs->filesize, 0, 0);

		if (rc < 0)
			return ceph2fsal_error(rc);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		mask |= CEPH_SETATTR_MODE;
		st.st_mode = fsal2unix_mode(attrs->mode);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)) {
		mask |= CEPH_SETATTR_UID;
		st.st_uid = attrs->owner;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)) {
		mask |= CEPH_SETATTR_UID;
		st.st_gid = attrs->group;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
		mask |= CEPH_SETATTR_ATIME;
		st.st_atim = attrs->atime;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
		mask |= CEPH_SETATTR_ATIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0)
			return ceph2fsal_error(rc);
		st.st_atim = timestamp;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
		mask |= CEPH_SETATTR_MTIME;
		st.st_mtim = attrs->mtime;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
		mask |= CEPH_SETATTR_MTIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0)
			return ceph2fsal_error(rc);
		st.st_mtim = timestamp;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_CTIME)) {
		mask |= CEPH_SETATTR_CTIME;
		st.st_ctim = attrs->ctime;
	}

	rc = ceph_ll_setattr(export->cmount, handle->i, &st, mask, 0, 0);

	if (rc < 0)
		return ceph2fsal_error(rc);

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

static fsal_status_t fsal_link(struct fsal_obj_handle *handle_pub,
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

static fsal_status_t fsal_rename(struct fsal_obj_handle *olddir_pub,
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

static fsal_status_t fsal_unlink(struct fsal_obj_handle *dir_pub,
				 const char *name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *dir = container_of(dir_pub, struct handle, handle);

	rc = ceph_ll_unlink(export->cmount, dir->i, name,
			    op_ctx->creds->caller_uid,
			    op_ctx->creds->caller_gid);
	if (rc == -EISDIR) {
		rc = ceph_ll_rmdir(export->cmount, dir->i, name,
				   op_ctx->creds->caller_uid,
				   op_ctx->creds->caller_gid);
	}

	if (rc < 0)
		return ceph2fsal_error(rc);

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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);
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

	rc = ceph_ll_open(export->cmount, handle->i, posix_flags, &(handle->fd),
			  0, 0);
	if (rc < 0) {
		handle->fd = NULL;
		return ceph2fsal_error(rc);
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
	struct handle *handle = container_of(handle_pub, struct handle, handle);

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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);
	/* Signed, so we can pick up on errors */
	int64_t nb_read = 0;

	nb_read =
	    ceph_ll_read(export->cmount, handle->fd, offset, buffer_size,
			 buffer);

	if (nb_read < 0)
		return ceph2fsal_error(nb_read);

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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);
	/* Signed, so we can pick up on errors */
	int64_t nb_written = 0;

	nb_written =
	    ceph_ll_write(export->cmount, handle->fd, offset, buffer_size,
			  buffer);

	if (nb_written < 0)
		return ceph2fsal_error(nb_written);

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
	struct export *export =
	    container_of(op_ctx->fsal_export, struct export, export);
	/* The private 'full' object handle */
	struct handle *handle = container_of(handle_pub, struct handle, handle);

	rc = ceph_ll_fsync(export->cmount, handle->fd, false);

	if (rc < 0)
		return ceph2fsal_error(rc);

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
	struct handle *handle = container_of(handle_pub, struct handle, handle);

	rc = ceph_ll_close(handle->export->cmount, handle->fd);

	if (rc < 0)
		return ceph2fsal_error(rc);

	handle->fd = NULL;
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
	const struct handle *handle =
	    container_of(handle_pub, const struct handle, handle);

	switch (output_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < sizeof(handle->vi)) {
			LogMajor(COMPONENT_FSAL,
				 "digest_handle: space too small for "
				 "handle.  Need %zu, have %zu",
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
	ops->lookup = lookup;
	ops->create = fsal_create;
	ops->mkdir = fsal_mkdir;
	ops->readdir = fsal_readdir;
	ops->symlink = fsal_symlink;
	ops->readlink = fsal_readlink;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->link = fsal_link;
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
#ifdef CEPH_PNFS
	handle_ops_pnfs(ops);
#endif				/* CEPH_PNFS */
}
