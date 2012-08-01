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
 * File I/O methods for VFS module
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "lustre_methods.h"
#include "FSAL/FSAL_LUSTRE/fsal_handle.h"

/** lustre_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t lustre_open(struct fsal_obj_handle *obj_hdl,
		       fsal_openflags_t openflags)
{
	struct lustre_fsal_obj_handle *myself;
	int fd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd == -1
	       && myself->u.file.openflags == FSAL_O_CLOSED);

	fd = lustre_open_by_handle( myself->handle, (O_RDWR));
	if(fd < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	myself->u.file.fd = fd;
	myself->u.file.openflags = openflags;

out:
	return fsalstat(fsal_error, retval);	
}

/* lustre_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t lustre_status(struct fsal_obj_handle *obj_hdl)
{
	struct lustre_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	return myself->u.file.openflags;
}

/* lustre_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t lustre_read(struct fsal_obj_handle *obj_hdl,
		       uint64_t offset,
                       size_t buffer_size,
                       void *buffer,
		       size_t *read_amount,
		       bool_t *end_of_file)
{
	struct lustre_fsal_obj_handle *myself;
	ssize_t nb_read;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 && myself->u.file.openflags != FSAL_O_CLOSED);

        nb_read = pread(myself->u.file.fd,
                        buffer,
                        buffer_size,
                        offset);

        if(offset == -1 || nb_read == -1) {
                retval = errno;
                fsal_error = posix2fsal_error(retval);
                goto out;
        }
        *end_of_file = nb_read == 0 ? TRUE : FALSE;
        *read_amount = nb_read;
out:
	return fsalstat(fsal_error, retval);	
}

/* lustre_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t lustre_write(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *write_amount)
{
	struct lustre_fsal_obj_handle *myself;
	ssize_t nb_written;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 && myself->u.file.openflags != FSAL_O_CLOSED);

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

/* lustre_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t lustre_commit(struct fsal_obj_handle *obj_hdl, /* sync */
			 off_t offset,
			 size_t len)
{
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 && myself->u.file.openflags != FSAL_O_CLOSED);

	retval = fsync(myself->u.file.fd);
	if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	return fsalstat(fsal_error, retval);	
}

/* lustre_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */

fsal_status_t lustre_lock_op(struct fsal_obj_handle *obj_hdl,
			  void * p_owner,
			  fsal_lock_op_t lock_op,
			  fsal_lock_param_t *request_lock,
			  fsal_lock_param_t *conflicting_lock)
{
	struct lustre_fsal_obj_handle *myself;
	struct flock lock_args;
	int fcntl_comm;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd < 0 || myself->u.file.openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "Attempting to lock with no file descriptor open");
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	if(p_owner != NULL) {
		fsal_error = ERR_FSAL_NOTSUPP;
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
	if(lock_op == FSAL_OP_LOCKT) {
		fcntl_comm = F_GETLK;
	} else if(lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_UNLOCK) {
		fcntl_comm = F_SETLK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Lock operation requested was not TEST, READ, or WRITE.");
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}

	if(request_lock->lock_type == FSAL_LOCK_R) {
		lock_args.l_type = F_RDLCK;
	} else if(request_lock->lock_type == FSAL_LOCK_W) {
		lock_args.l_type = F_WRLCK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}

	if(lock_op == FSAL_OP_UNLOCK)
		lock_args.l_type = F_UNLCK;

	lock_args.l_len = request_lock->lock_length;
	lock_args.l_start = request_lock->lock_start;
	lock_args.l_whence = SEEK_SET;

	errno = 0;
	retval = fcntl(myself->u.file.fd, fcntl_comm, &lock_args);
	if(retval && lock_op == FSAL_OP_LOCK) {
		retval = errno;
		if(conflicting_lock != NULL) {
			fcntl_comm = F_GETLK;
			retval = fcntl(myself->u.file.fd,
				       fcntl_comm,
				       &lock_args);
			if(retval) {
				retval = errno; /* we lose the inital error */
				LogCrit(COMPONENT_FSAL,
					"After failing a lock request, I couldn't even"
					" get the details of who owns the lock.");
				fsal_error = posix2fsal_error(retval);
				goto out;
			}
			if(conflicting_lock != NULL) {
				conflicting_lock->lock_length = lock_args.l_len;
				conflicting_lock->lock_start = lock_args.l_start;
				conflicting_lock->lock_type = lock_args.l_type;
			}
		}
		fsal_error = posix2fsal_error(retval);
		goto out;
	}

	/* F_UNLCK is returned then the tested operation would be possible. */
	if(conflicting_lock != NULL) {
		if(lock_op == FSAL_OP_LOCKT && lock_args.l_type != F_UNLCK) {
			conflicting_lock->lock_length = lock_args.l_len;
			conflicting_lock->lock_start = lock_args.l_start;
			conflicting_lock->lock_type = lock_args.l_type;
		} else {
			conflicting_lock->lock_length = 0;
			conflicting_lock->lock_start = 0;
			conflicting_lock->lock_type = FSAL_NO_LOCK;
		}
	}
out:
	return fsalstat(fsal_error, retval);	
}

/* lustre_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t lustre_close(struct fsal_obj_handle *obj_hdl)
{
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	retval = close(myself->u.file.fd);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	myself->u.file.fd = -1;
	myself->u.file.openflags = FSAL_O_CLOSED;
	return fsalstat(fsal_error, retval);	
}

/* lustre_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t lustre_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			      lru_actions_t requests)
{
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd >= 0) {
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
