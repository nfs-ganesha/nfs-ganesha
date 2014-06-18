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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* file.c
 * File I/O methods for GPFS module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "gpfs_methods.h"

/* common code gpfs_open and gpfs_reopen */
fsal_status_t gpfs_open2(struct fsal_obj_handle *obj_hdl,
			 fsal_openflags_t openflags, bool reopen)
{
	struct gpfs_fsal_obj_handle *myself;
	int fd;
	fsal_status_t status;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	if (reopen) {
		assert((myself->u.file.fd >= 0 &&
			myself->u.file.openflags != FSAL_O_CLOSED));
		fd = myself->u.file.fd;
	} else {
		assert(myself->u.file.fd == -1 &&
		       myself->u.file.openflags == FSAL_O_CLOSED);
	}

	status = GPFSFSAL_open(obj_hdl, op_ctx, openflags, &fd, NULL, reopen);
	if (FSAL_IS_ERROR(status))
		return status;

	myself->u.file.fd = fd;
	myself->u.file.openflags = openflags;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** gpfs_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t gpfs_open(struct fsal_obj_handle *obj_hdl,
			fsal_openflags_t openflags)
{
	return gpfs_open2(obj_hdl, openflags, 0);
}

/** gpfs_reopen
 * called with appropriate locks taken at the cache inode level
 *
 * The file may have been already opened, so open the file again with
 * given open flags without losing any locks associated with the file.
 */

fsal_status_t gpfs_reopen(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags)
{
	return gpfs_open2(obj_hdl, openflags, 1);
}

/* gpfs_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t gpfs_status(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	return myself->u.file.openflags;
}

/* gpfs_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t gpfs_read(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0
	       && myself->u.file.openflags != FSAL_O_CLOSED);

	status =
	    GPFSFSAL_read(myself->u.file.fd, offset, buffer_size, buffer,
			  read_amount, end_of_file);
	if (FSAL_IS_ERROR(status))
		return status;

	if ((*end_of_file == false)
	    && ((offset + *read_amount) >= obj_hdl->attributes.filesize))
		*end_of_file = true;

	return fsalstat(fsal_error, retval);
}

fsal_status_t gpfs_read_plus(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file, struct io_info *info)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct read_arg rarg;
	ssize_t nb_read;
	int errsv = 0;

	if (!buffer || !read_amount || !end_of_file || !info)
		return fsalstat(ERR_FSAL_FAULT, 0);

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0
	       && myself->u.file.openflags != FSAL_O_CLOSED);

	rarg.mountdirfd = myself->u.file.fd;
	rarg.fd = myself->u.file.fd;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = buffer_size;
	rarg.options = IO_SKIP_HOLE;

	nb_read = gpfs_ganesha(OPENHANDLE_READ_BY_FD, &rarg);
	errsv = errno;

	if (nb_read == -1 && errsv != ENODATA) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	if (errsv == ENODATA) {
		info->io_content.what = NFS4_CONTENT_HOLE;
		info->io_content.hole.di_offset = offset;     /*offset of hole*/
		info->io_content.hole.di_length = buffer_size;/*length of hole*/
		info->io_content.hole.di_allocated = TRUE;    /*maybe ??? */
		*read_amount = buffer_size;
	} else {
		info->io_content.what = NFS4_CONTENT_DATA;
		info->io_content.data.d_offset = offset + nb_read;
		info->io_content.data.d_allocated = TRUE;   /* ??? */
		info->io_content.data.d_data.data_len = nb_read;
		info->io_content.data.d_data.data_val = buffer;
		*read_amount = nb_read;
	}
	if (nb_read != -1 &&
		(nb_read == 0 || nb_read < buffer_size ||
		((offset + nb_read) >= obj_hdl->attributes.filesize)))
		*end_of_file = TRUE;
	else
		*end_of_file = FALSE;

	return status;
}

/* gpfs_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t gpfs_write(struct fsal_obj_handle *obj_hdl,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0
	       && myself->u.file.openflags != FSAL_O_CLOSED);

	status =
	    GPFSFSAL_write(myself->u.file.fd, offset, buffer_size, buffer,
			   write_amount, fsal_stable, op_ctx);
	return status;
}

/* gpfs_clear
 * concurrency (locks) is managed in cache_inode_*
 */

static
fsal_status_t gpfs_clear(struct fsal_obj_handle *obj_hdl,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable,
			 bool allocated)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0
	       && myself->u.file.openflags != FSAL_O_CLOSED);

	status =
	    GPFSFSAL_clear(myself->u.file.fd, offset, buffer_size, buffer,
			   write_amount, fsal_stable, op_ctx, allocated);
	return status;
}

/* file_write_plus
 * default case not supported
 */

fsal_status_t gpfs_write_plus(struct fsal_obj_handle *obj_hdl,
				uint64_t seek_descriptor, size_t buffer_size,
				void *buffer, size_t *write_amount,
				bool *fsal_stable, struct io_info *info)
{
	if (info->io_content.what == NFS4_CONTENT_DATA) {
		return gpfs_write(obj_hdl, seek_descriptor, buffer_size,
				buffer, write_amount, fsal_stable);
	}
	if (info->io_content.what == NFS4_CONTENT_HOLE) {
		return gpfs_clear(obj_hdl,
				  seek_descriptor, buffer_size,
				  buffer, write_amount, fsal_stable,
				  info->io_content.hole.di_allocated);
	}
	return fsalstat(ERR_FSAL_UNION_NOTSUPP, 0);
}

/* seek
 * default case not supported
 */

fsal_status_t gpfs_seek(struct fsal_obj_handle *obj_hdl,
				struct io_info *info)
{
	struct fseek_arg arg;
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct gpfs_io_info io_info;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0
	       && myself->u.file.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd;
	arg.openfd = myself->u.file.fd;
	arg.info = &io_info;
	io_info.io_offset = info->io_content.hole.di_offset;
	if (info->io_content.what == NFS4_CONTENT_DATA)
		io_info.io_what = SEEK_DATA;
	else if (info->io_content.what == NFS4_CONTENT_HOLE)
		io_info.io_what = SEEK_HOLE;
	else
		return fsalstat(ERR_FSAL_UNION_NOTSUPP, 0);

	retval = gpfs_ganesha(OPENHANDLE_SEEK_BY_FD, &arg);
	if (retval == -1) {
		retval = errno;
		if (retval == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		fsal_error = posix2fsal_error(retval);
	} else {
		info->io_eof = io_info.io_eof;
		info->io_content.hole.di_offset = io_info.io_offset;
		info->io_content.hole.di_allocated = io_info.io_alloc;
		info->io_content.hole.di_length = io_info.io_len;
	}
	return fsalstat(fsal_error, 0);
}

/* io_advise
 */

fsal_status_t gpfs_io_advise(struct fsal_obj_handle *obj_hdl,
				struct io_hints *hints)
{
	struct fadvise_arg arg;
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0
	       && myself->u.file.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd;
	arg.openfd = myself->u.file.fd;
	arg.offset = hints->offset;
	arg.length = hints->count;
	arg.hints = &hints->hints;

	retval = gpfs_ganesha(OPENHANDLE_FADVISE_BY_FD, &arg);
	if (retval == -1) {
		retval = errno;
		if (retval == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		fsal_error = posix2fsal_error(retval);
		hints->hints = 0;
	}
	return fsalstat(fsal_error, 0);
}

/* gpfs_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t gpfs_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			  off_t offset, size_t len)
{
	struct fsync_arg arg;
	verifier4 writeverf;
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0
	       && myself->u.file.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd;
	arg.handle = myself->handle;
	arg.offset = offset;
	arg.length = len;
	arg.verifier4 = (int32_t *) &writeverf;

	retval = gpfs_ganesha(OPENHANDLE_FSYNC, &arg);
	if (retval == -1) {
		retval = errno;
		if (retval == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		fsal_error = posix2fsal_error(retval);
	}
	set_gpfs_verifier(&writeverf);

	return fsalstat(fsal_error, retval);
}

/* gpfs_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */

fsal_status_t gpfs_lock_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if (myself->u.file.fd < 0 ||
			myself->u.file.openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "Attempting to lock with no file descriptor open, fd %d",
			 myself->u.file.fd);
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	if (conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT) {
		LogDebug(COMPONENT_FSAL,
			 "conflicting_lock argument can't"
			 " be NULL with lock_op  = LOCKT");
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	LogFullDebug(COMPONENT_FSAL,
		"Locking: op:%d type:%d claim:%d start:%" PRIu64 " length:%lu ",
		lock_op, request_lock->lock_type,
		request_lock->lock_reclaim,
		request_lock->lock_start,
		request_lock->lock_length);

	status = GPFSFSAL_lock_op(op_ctx->fsal_export, obj_hdl,
				  p_owner, lock_op, *request_lock,
				  conflicting_lock);
	return status;

 out:
	return fsalstat(fsal_error, retval);
}

/* gpfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t gpfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

	assert(obj_hdl->type == REGULAR_FILE);
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if (myself->u.file.fd >= 0 &&
			myself->u.file.openflags != FSAL_O_CLOSED) {
		status = fsal_internal_close(myself->u.file.fd, NULL, 0);
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}
	return status;
}

/* gpfs_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t gpfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			       lru_actions_t requests)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if (obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0) {
		status = fsal_internal_close(myself->u.file.fd, NULL, 0);
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}
	return status;
}
