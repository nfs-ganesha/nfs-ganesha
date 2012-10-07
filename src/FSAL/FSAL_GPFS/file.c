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

/* file.c
 * File I/O methods for GPFS module
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "gpfs_methods.h"
#include "fsal_handle_syscalls.h"

/** gpfs_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t gpfs_open(struct fsal_obj_handle *obj_hdl,
		       fsal_openflags_t openflags)
{
	struct gpfs_fsal_obj_handle *myself;
	int fd;
        fsal_status_t status;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd == -1
	       && myself->u.file.openflags == FSAL_O_CLOSED);

        status = GPFSFSAL_open(obj_hdl, (O_RDWR), &fd, NULL);
        if(FSAL_IS_ERROR(status))
          return(status);

	myself->u.file.fd = fd;
	myself->u.file.openflags = openflags;

	return fsalstat(fsal_error, retval);	
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
                       const struct req_op_context *opctx,
		       uint64_t offset,
                       size_t buffer_size,
                       void *buffer,
		       size_t *read_amount,
		       bool *end_of_file)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        fsal_status_t status;
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 && myself->u.file.openflags != FSAL_O_CLOSED);

        status =  GPFSFSAL_read(myself->u.file.fd, offset, buffer_size, buffer,
                                read_amount, end_of_file);
        if(FSAL_IS_ERROR(status))
          return(status);

        *end_of_file = *read_amount == 0 ? true : false;

	return fsalstat(fsal_error, retval);
}

/* gpfs_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t gpfs_write(struct fsal_obj_handle *obj_hdl,
                        const struct req_op_context *opctx,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *write_amount)
{
	struct gpfs_fsal_obj_handle *myself;
	ssize_t nb_written;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

        nb_written = pwrite(myself->u.file.fd,
                            buffer,
                            buffer_size,
                            offset);

	if(offset == -1 || nb_written == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	*write_amount = nb_written;
out:
	return fsalstat(fsal_error, retval);	
}

/* gpfs_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t gpfs_commit(struct fsal_obj_handle *obj_hdl, /* sync */
			 off_t offset,
			 size_t len)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	retval = fsync(myself->u.file.fd);
	if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	return fsalstat(fsal_error, retval);	
}

/* gpfs_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */

fsal_status_t gpfs_lock_op(struct fsal_obj_handle *obj_hdl,
			  void * p_owner,
			  fsal_lock_op_t lock_op,
			  fsal_lock_param_t *request_lock,
			  fsal_lock_param_t *conflicting_lock)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd < 0 || myself->u.file.openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "Attempting to lock with no file descriptor open");
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	if(conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT) {
		LogDebug(COMPONENT_FSAL,
			 "conflicting_lock argument can't"
			 " be NULL with lock_op  = LOCKT");
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d start:%"PRIu64" length:%lu ",
		     lock_op,
		     request_lock->lock_type,
		     request_lock->lock_start,
		     request_lock->lock_length);

        status = GPFSFSAL_lock_op(obj_hdl, p_owner, lock_op, *request_lock,
                                  conflicting_lock);
        return(status);

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
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	assert(obj_hdl->type == REGULAR_FILE);
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd >= 0 &&
	   myself->u.file.openflags != FSAL_O_CLOSED){
		retval = close(myself->u.file.fd);
		if(retval < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
		}
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}
	return fsalstat(fsal_error, retval);	
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
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if(obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0) {
		retval = close(myself->u.file.fd);
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}
	if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	return fsalstat(fsal_error, retval);	
}
