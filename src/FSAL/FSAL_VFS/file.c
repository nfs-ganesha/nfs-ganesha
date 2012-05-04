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

#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"


fsal_status_t vfs_open(struct fsal_obj_handle *obj_hdl,
		       fsal_openflags_t openflags)
{
	struct vfs_fsal_obj_handle *myself;
	int fd, mntfd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	pthread_mutex_lock(&obj_hdl->lock);
	if(myself->fd >= 0 && !myself->lock_status) {
		if(openflags != myself->openflags) { /* make smarter */
			retval = close(myself->fd);
			myself->fd = -1;
			if(retval) {
				retval = errno;
				fsal_error = posix2fsal_error(errno);
				goto out;
			}
		}
	}
	mntfd = vfs_get_root_fd(obj_hdl->export);
	fd = open_by_handle_at(mntfd, myself->handle, (O_RDWR));
	if(fd < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	myself->fd = fd;
	myself->openflags = openflags;
	myself->lock_status = 0; /* no locks on new files */

out:
	pthread_mutex_unlock(&obj_hdl->lock);
	ReturnCode(fsal_error, retval);	
}

/* vfs_read
 * NOTE: there are potential races here because we are sharing fd state
 * with other threads.  This is not only for concurrent access but for
 * interleaved access with FSAK_SEEK_CUR.
 */

fsal_status_t vfs_read(struct fsal_obj_handle *obj_hdl,
		       fsal_seek_t * seek_descriptor,
		       size_t buffer_size,
		       caddr_t buffer,
		       ssize_t *read_amount,
		       fsal_boolean_t * end_of_file)
{
	struct vfs_fsal_obj_handle *myself;
	ssize_t nb_read;
	off_t offset = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
/** @TODO this is racy as is write and commit.  need to get/put the fd across
 *  these calls.  Same will apply to things like getattrs if they share the fd
 *  at some point.  lower priority for now...
 */
	if(myself->fd < 0 ||
	   !(myself->openflags&(FSAL_O_RDONLY|FSAL_O_RDWR))) {
		fsal_status_t open_status;

		open_status = vfs_open(obj_hdl, FSAL_O_RDONLY);
		if(FSAL_IS_ERROR(open_status)) {
			return open_status;
		}
	}
	if(seek_descriptor == NULL) {
		nb_read = read(myself->fd, buffer, buffer_size);
	} else {
		if(seek_descriptor->whence == FSAL_SEEK_SET) {
			nb_read = pread(myself->fd,
					buffer,
					buffer_size,
					seek_descriptor->offset);
		} else {
			int whence = seek_descriptor->whence ? SEEK_CUR : SEEK_END;

			offset = lseek(myself->fd,
				       seek_descriptor->offset,
				       whence);
			if(offset != -1) {
				nb_read = read(myself->fd, buffer, buffer_size);
			}
		}
	}
	if(offset == -1 || nb_read == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	*end_of_file = nb_read == 0 ? TRUE : FALSE;
	*read_amount = nb_read;
out:
	ReturnCode(fsal_error, retval);	
}

/* vfs_write
 * NOTE: same concurrency issues apply here
 * In addition, switching uids here with setfsuid could cause problems
 * if the open is done with one uid and the write is done with another
 * in a later transaction.  In order to deal with that scenario, an open
 * followed by the I/O followed by a close would be required.  This is
 * both a performance issue because we are no longer safely cacheing fds
 * but it can break POSIX locks.
 */

fsal_status_t vfs_write(struct fsal_obj_handle *obj_hdl,
			fsal_seek_t *seek_descriptor,
			size_t buffer_size,
			caddr_t buffer,
			ssize_t *write_amount)
{
	struct vfs_fsal_obj_handle *myself;
	ssize_t nb_written;
	off_t offset = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(myself->fd < 0 ||
	   !(myself->openflags&FSAL_O_RDWR)) {
		fsal_status_t open_status;

		open_status = vfs_open(obj_hdl, FSAL_O_RDWR);
		if(FSAL_IS_ERROR(open_status)) {
			return open_status;
		}
	}
	if(seek_descriptor == NULL) {
		nb_written = write(myself->fd, buffer, buffer_size);
	} else {
		if(seek_descriptor->whence == FSAL_SEEK_SET) {
			nb_written = pwrite(myself->fd,
					buffer,
					buffer_size,
					seek_descriptor->offset);
		} else {
			int whence = seek_descriptor->whence ? SEEK_CUR : SEEK_END;

			offset = lseek(myself->fd,
				       seek_descriptor->offset,
				       whence);
			if(offset != -1) {
				nb_written = write(myself->fd, buffer, buffer_size);
			}
		}
	}
	if(offset == -1 || nb_written == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	*write_amount = (fsal_size_t) nb_written;
out:
	ReturnCode(fsal_error, retval);	
}

/* vfs_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t vfs_commit(struct fsal_obj_handle *obj_hdl, /* sync */
			 off_t offset,
			 size_t len)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(myself->fd >= 0) {
		retval = fsync(myself->fd);
	}
	if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	ReturnCode(fsal_error, retval);	
}

/* vfs_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */

fsal_status_t vfs_lock_op(struct fsal_obj_handle *obj_hdl,
			  void * p_owner,
			  fsal_lock_op_t lock_op,
			  fsal_lock_param_t   request_lock,
			  fsal_lock_param_t * conflicting_lock)
{
	struct vfs_fsal_obj_handle *myself;
	struct flock lock_args;
	int fcntl_comm;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(myself->fd < 0) {
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
		     "Locking: op:%d type:%d start:%llu length:%llu ",
		     lock_op,
		     request_lock.lock_type,
		     request_lock.lock_start,
		     request_lock.lock_length);
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

	if(request_lock.lock_type == FSAL_LOCK_R) {
		lock_args.l_type = F_RDLCK;
	} else if(request_lock.lock_type == FSAL_LOCK_W) {
		lock_args.l_type = F_WRLCK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}

	if(lock_op == FSAL_OP_UNLOCK)
		lock_args.l_type = F_UNLCK;

	lock_args.l_len = request_lock.lock_length;
	lock_args.l_start = request_lock.lock_start;
	lock_args.l_whence = SEEK_SET;

	errno = 0;
	retval = fcntl(myself->fd, fcntl_comm, &lock_args);
	if(retval && lock_op == FSAL_OP_LOCK) {
		retval = errno;
		if(conflicting_lock != NULL) {
			fcntl_comm = F_GETLK;
			retval = fcntl(myself->fd,
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
	myself->lock_status = 1;  /* what is status on failure path? */
out:
	ReturnCode(fsal_error, retval);	
}

/* vfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks.
 * TBD, do we have to clear/verify as clear locks first?
 */

fsal_status_t vfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	pthread_mutex_lock(&obj_hdl->lock);
	if(myself->fd >= 0 && !myself->lock_status) {
		retval = close(myself->fd);
		if(retval < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
		}
		myself->fd = -1;
		myself->lock_status = 0;
		myself->openflags = 0;
	}
	pthread_mutex_unlock(&obj_hdl->lock);
	ReturnCode(fsal_error, retval);	
}

/* vfs_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t vfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			      lru_actions_t requests)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(myself->fd >= 0 && !(myself->lock_status)) {
		retval = close(myself->fd);
		myself->fd = -1;
		myself->lock_status = 0;
		myself->openflags = 0;
	}
	if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	ReturnCode(fsal_error, retval);	
}

/* vfs_rcp
 * copy a VFS file to/from a local filesystem (file cache)
 * this hardly makes sense in this context but it is here as
 * the model for other fsals with slow backends such as HPSS
 * or PROXY.  This should really be in the file content cache
 * as it has access to the local filesystem and would be making
 * handle calls for these bits anyway...
 * better yet, file content cache should be a layered/stacked fsal
 * and this gone.
 * Leave it here for now.
 */

#define RCP_BUFFER_SIZE 10485760
fsal_status_t vfs_rcp(struct fsal_obj_handle *obj_hdl,
		      const char *local_path,
		      fsal_rcpflag_t transfer_opt)
{
/* 	struct vfs_fsal_obj_handle *myself; */
	int fd;
	int local_flags;
	fsal_openflags_t fs_flags;
	caddr_t buffer;
	int local_to_fs = FALSE;
	int eof = FALSE;
	ssize_t local_size, fs_size;
	fsal_status_t st;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

/* 	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle); */
	local_to_fs = !!(transfer_opt & FSAL_RCP_LOCAL_TO_FS);
	if(local_to_fs) {
		local_flags = O_RDONLY;
		fs_flags = FSAL_O_WRONLY | FSAL_O_TRUNC;
	} else {
		local_flags = O_WRONLY | O_TRUNC;
		if(transfer_opt & FSAL_RCP_LOCAL_CREAT)
			local_flags |= O_CREAT;
		if(transfer_opt & FSAL_RCP_LOCAL_EXCL)
			local_flags |= O_EXCL;
		fs_flags = FSAL_O_WRONLY | FSAL_O_TRUNC;
	}
	fd = open(local_path, local_flags, 0600);
	if(fd == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	st = vfs_open(obj_hdl, fs_flags);
	if(FSAL_IS_ERROR(st)) {
		close(fd);
		fsal_error = st.major;
		goto out;
	}
	buffer = malloc(RCP_BUFFER_SIZE);
	if(buffer == NULL) {
		close(fd);
		vfs_close(obj_hdl);
		goto out;
	}
	if(local_to_fs) {
		while( !eof) {
			local_size = read(fd, buffer, RCP_BUFFER_SIZE);
			if(local_size > 0) {
				st = vfs_write(obj_hdl,
					       NULL,
					       RCP_BUFFER_SIZE,
					       buffer,
					       &fs_size);
				if(FSAL_IS_ERROR(st) ||
				   fs_size != RCP_BUFFER_SIZE) {
					fsal_error = st.major;
					retval = st.minor;
					break;
				}
			} else if(local_size < 0) {
				fsal_error = ERR_FSAL_IO;
				retval = errno;
				break;
			} else {
				eof = TRUE;
			}
		}
	} else {  /* fs to local file */
		while( !eof) {
			fs_size = 0;
			st = vfs_read(obj_hdl,
				      NULL,
				      RCP_BUFFER_SIZE,
				      buffer,
				      &fs_size,
				      &eof);
			if(FSAL_IS_ERROR(st)) {
				fsal_error = st.major;
				retval = st.minor;
				break;
			}
			if( !eof) {
				local_size = write(fd, buffer, fs_size);
				if(local_size == -1) {
					st.major = ERR_FSAL_IO;
					st.minor = errno;
					break;        /* exit loop */
				}
			}
		}
	}
	free(buffer);
	close(fd);
	vfs_close(obj_hdl);
out:
	ReturnCode(fsal_error, retval);	
}

