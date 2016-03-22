/** @file file.c
 *  @brief GPFS FSAL module file I/O functions
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "gpfs_methods.h"

static fsal_status_t
gpfs_open2(struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags,
	   bool reopen)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fsal_status_t status;
	int fd = -1;

	if (reopen) {
		assert(myself->u.file.fd >= 0 &&
		       myself->u.file.openflags != FSAL_O_CLOSED);
		fd = myself->u.file.fd;
	} else {
		assert(myself->u.file.fd == -1 &&
		       myself->u.file.openflags == FSAL_O_CLOSED);
	}

	status = GPFSFSAL_open(obj_hdl, op_ctx, openflags, &fd, NULL, reopen);
	if (FSAL_IS_ERROR(status) == false) {
		myself->u.file.fd = fd;
		myself->u.file.openflags = openflags;
	}

	return status;
}

/**
 * @brief called with appropriate locks taken at the cache inode level
 *
 * @param obj_hdl FSAL object handle
 * @param openflags FSAL open flags
 * @return FSAL status
 */
fsal_status_t
gpfs_open(struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags)
{
	return gpfs_open2(obj_hdl, openflags, false);
}

/**
 *  @brief called with appropriate locks taken at the cache inode level
 *
 *  @param obj_hdl FSAL object handle
 *  @param openflags FSAL open flags
 *  @return FSAL status
 *
 *  The file may have been already opened, so open the file again with
 *  given open flags without losing any locks associated with the file.
 */
fsal_status_t
gpfs_reopen(struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags)
{
	return gpfs_open2(obj_hdl, openflags, true);
}

/**
 * @brief get GPFS status
 *
 * @param obj_hdl FSAL object handle
 * @return FSAL status
 *
 * Let the caller peek into the file's open/close state.
 */
fsal_openflags_t gpfs_status(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	return myself->u.file.openflags;
}

/**
 *  @brief GPFS read command
 *
 *  @param obj_hdl FSAL object handle
 *  @param offset Offset
 *  @param buffer_size Size of buffer
 *  @param buffer void reference to buffer
 *  @param read_amount size_t reference to amount of data read
 *  @param end_of_file boolean indiocating the end of file
 *  @return FSAL status
 *
 *  concurrency (locks) is managed in cache_inode_*
 */
fsal_status_t
gpfs_read(struct fsal_obj_handle *obj_hdl, uint64_t offset, size_t buffer_size,
	  void *buffer, size_t *read_amount, bool *end_of_file)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fsal_status_t status;

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	status = GPFSFSAL_read(myself->u.file.fd, offset, buffer_size, buffer,
			       read_amount, end_of_file);

	if (FSAL_IS_ERROR(status))
		return status;

	if ((*end_of_file == false) &&
	    ((offset + *read_amount) >= myself->attributes.filesize))
		*end_of_file = true;

	return status;
}

/**
 *  @brief GPFS read plus
 *
 *  @param obj_hdl FSAL object handle
 *  @param offset Offset
 *  @param buffer_size Size of buffer
 *  @param buffer void reference to buffer
 *  @param read_amount size_t reference to amount of data read
 *  @param end_of_file boolean indiocating the end of file
 *  @param info I/O information
 *  @return FSAL status
 */
fsal_status_t
gpfs_read_plus(struct fsal_obj_handle *obj_hdl, uint64_t offset,
	       size_t buffer_size, void *buffer, size_t *read_amount,
	       bool *end_of_file, struct io_info *info)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	const fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct read_arg rarg = {0};
	ssize_t nb_read;
	int errsv;

	if (!buffer || !read_amount || !end_of_file || !info)
		return fsalstat(ERR_FSAL_FAULT, 0);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	rarg.mountdirfd = myself->u.file.fd;
	rarg.fd = myself->u.file.fd;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = buffer_size;
	rarg.options = IO_SKIP_HOLE;

	nb_read = gpfs_ganesha(OPENHANDLE_READ_BY_FD, &rarg);
	errsv = errno;

	if (nb_read < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		if (errsv != ENODATA)
			return fsalstat(posix2fsal_error(errsv), errsv);

		/* errsv == ENODATA */
		if ((buffer_size + offset) > myself->attributes.filesize) {
			if (offset >= myself->attributes.filesize)
				*read_amount = 0;
			else
				*read_amount =
					myself->attributes.filesize - offset;
			info->io_content.hole.di_length = *read_amount;
		} else {
			*read_amount = buffer_size;
			info->io_content.hole.di_length = buffer_size;
		}
		info->io_content.what = NFS4_CONTENT_HOLE;
		info->io_content.hole.di_offset = offset;
	} else {
		info->io_content.what = NFS4_CONTENT_DATA;
		info->io_content.data.d_offset = offset + nb_read;
		info->io_content.data.d_data.data_len = nb_read;
		info->io_content.data.d_data.data_val = buffer;
		*read_amount = nb_read;
	}

	if (nb_read != -1 &&
	    (nb_read == 0 || nb_read < buffer_size ||
		 (offset + nb_read) >= myself->attributes.filesize))
		*end_of_file = true;
	else
		*end_of_file = false;

	return status;
}

/**
 *  @brief GPFS write command
 *
 *  @param obj_hdl FSAL object handle
 *  @param offset Offset
 *  @param buffer_size Size of buffer
 *  @param buffer void reference to buffer
 *  @param write_amount reference to amount written
 *  @param fsal_stable FSAL stable
 *  @return FSAL status
 *
 *  concurrency (locks) is managed in cache_inode_*
 */
fsal_status_t
gpfs_write(struct fsal_obj_handle *obj_hdl, uint64_t offset, size_t buffer_size,
	   void *buffer, size_t *write_amount, bool *fsal_stable)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	return GPFSFSAL_write(myself->u.file.fd, offset, buffer_size, buffer,
			      write_amount, fsal_stable, op_ctx);
}

/**
 *  @brief GPFS deallocate command
 *
 *  @param obj_hdl FSAL object handle
 *  @param offset Offset
 *  @param length Length
 *  @return FSAL status
 *
 *  concurrency (locks) is managed in cache_inode_*
 */
static fsal_status_t
gpfs_deallocate(struct fsal_obj_handle *obj_hdl, uint64_t offset,
		uint64_t length)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	return GPFSFSAL_alloc(myself->u.file.fd, offset, length, false);
}

/**
 *  @brief GPFS allocate command
 *
 *  @param obj_hdl FSAL object handle
 *  @param offset Offset
 *  @param length Length
 *  @return FSAL status
 *
 *  concurrency (locks) is managed in cache_inode_*
 */
static fsal_status_t
gpfs_allocate(struct fsal_obj_handle *obj_hdl, uint64_t offset, uint64_t length)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	return GPFSFSAL_alloc(myself->u.file.fd, offset, length, true);
}

/**
 *  @brief GPFS write plus command
 *
 *  @param obj_hdl FSAL object handle
 *  @param offset Offset
 *  @param buffer_size Size of buffer
 *  @param buffer void reference to buffer
 *  @param write_amount reference to amount written
 *  @param fsal_stable FSAL stable
 *  @param io_info I/O information
 *  @return FSAL status
 *
 *  default case not supported
 */
fsal_status_t
gpfs_write_plus(struct fsal_obj_handle *obj_hdl, uint64_t offset,
		size_t buffer_size, void *buffer, size_t *write_amount,
		bool *fsal_stable, struct io_info *info)
{
	switch (info->io_content.what) {
	case NFS4_CONTENT_DATA:
		return gpfs_write(obj_hdl, offset, buffer_size, buffer,
				  write_amount, fsal_stable);
	case NFS4_CONTENT_DEALLOCATE:
		return gpfs_deallocate(obj_hdl, offset, buffer_size);
	case NFS4_CONTENT_ALLOCATE:
		return gpfs_allocate(obj_hdl, offset, buffer_size);
	default:
		return fsalstat(ERR_FSAL_UNION_NOTSUPP, 0);
	}
}

/**
 *  @brief GPFS seek command
 *
 *  @param obj_hdl FSAL object handle
 *  @param io_info I/O information
 *  @return FSAL status
 *
 *  default case not supported
 */
fsal_status_t gpfs_seek(struct fsal_obj_handle *obj_hdl, struct io_info *info)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	struct gpfs_io_info io_info = {0};
	struct fseek_arg arg = {0};

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd;
	arg.openfd = myself->u.file.fd;
	arg.info = &io_info;

	io_info.io_offset = info->io_content.hole.di_offset;
	switch (info->io_content.what) {
	case NFS4_CONTENT_DATA:
		io_info.io_what = SEEK_DATA;
		break;
	case NFS4_CONTENT_HOLE:
		io_info.io_what = SEEK_HOLE;
		break;
	default:
		return fsalstat(ERR_FSAL_UNION_NOTSUPP, 0);
	}

	if (gpfs_ganesha(OPENHANDLE_SEEK_BY_FD, &arg) == -1) {
		if (errno == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errno), 0);
	}

	info->io_eof = io_info.io_eof;
	info->io_content.hole.di_offset = io_info.io_offset;
	info->io_content.hole.di_length = io_info.io_len;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief GPFS IO advise
 *
 *  @param obj_hdl FSAL object handle
 *  @param io_hints I/O information
 *  @return FSAL status
 *
 */
fsal_status_t
gpfs_io_advise(struct fsal_obj_handle *obj_hdl, struct io_hints *hints)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	struct fadvise_arg arg = {0};

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd;
	arg.openfd = myself->u.file.fd;
	arg.offset = hints->offset;
	arg.length = hints->count;
	arg.hints = &hints->hints;

	if (gpfs_ganesha(OPENHANDLE_FADVISE_BY_FD, &arg) == -1) {
		if (errno == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		hints->hints = 0;
		return fsalstat(posix2fsal_error(errno), 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief GPFS commit command
 *
 *  @param obj_hdl FSAL object handle
 *  @param offset Offset
 *  @param buffer_size Size of buffer
 *  @return FSAL status
 *
 *  @brief Commit a file range to storage.
 *
 *  for right now, fsync will have to do.
 */
fsal_status_t
gpfs_commit(struct fsal_obj_handle *obj_hdl, off_t offset, size_t len)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	struct fsync_arg arg = {0};
	verifier4 writeverf = {0};
	int retval;

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd;
	arg.handle = myself->handle;
	arg.offset = offset;
	arg.length = len;
	arg.verifier4 = (int32_t *) &writeverf;

	if (gpfs_ganesha(OPENHANDLE_FSYNC, &arg) == -1) {
		retval = errno;
		if (retval == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(retval), retval);
	}

	set_gpfs_verifier(&writeverf);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief GPFS lcok operation
 *
 *  @param obj_hdl FSAL object handle
 *  @param owner reference to void
 *  @param lock_op FSAL lock
 *  @param request_lock Request lock
 *  @param conflicting_lock Conflicting lock
 *  @return FSAL status
 *
 *  @brief lock a region of the file
 *
 *  throw an error if the fd is not open.  The old fsal didn't check this.
 */
fsal_status_t
gpfs_lock_op(struct fsal_obj_handle *obj_hdl, void *owner,
	     fsal_lock_op_t lock_op, fsal_lock_param_t *request_lock,
	     fsal_lock_param_t *conflicting_lock)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	if (myself->u.file.fd < 0 ||
	    myself->u.file.openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "Attempting to lock with no file descriptor open, fd %d",
			 myself->u.file.fd);
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	if (conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT) {
		LogDebug(COMPONENT_FSAL,
			 "conflicting_lock argument can't be NULL with lock_op  = LOCKT");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d claim:%d start:%" PRIu64
		     " length:%lu ", lock_op, request_lock->lock_type,
		     request_lock->lock_reclaim, request_lock->lock_start,
		     request_lock->lock_length);

	return GPFSFSAL_lock_op(op_ctx->fsal_export, obj_hdl, owner, lock_op,
				*request_lock, conflicting_lock);
}

/**
 *  @brief Close the file if it is still open.
 *
 *  @param obj_hdl FSAL object handle
 *  @return FSAL status
 *
 *  Yes, we ignor lock status.  Closing a file in POSIX
 *  releases all locks but that is state and cache inode's problem.
 */
fsal_status_t gpfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	assert(obj_hdl->type == REGULAR_FILE);

	if (myself->u.file.fd >= 0 &&
	    myself->u.file.openflags != FSAL_O_CLOSED) {
		status = fsal_internal_close(myself->u.file.fd, NULL, 0);
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}

	return status;
}

