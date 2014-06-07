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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* file.c
 * File I/O methods for NULL module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "nullfs_methods.h"


/** nullfs_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t nullfs_open(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags)
{
	return next_ops.obj_ops->open(obj_hdl, openflags);
}

/* nullfs_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t nullfs_status(struct fsal_obj_handle *obj_hdl)
{
	return next_ops.obj_ops->status(obj_hdl);
}

/* nullfs_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t nullfs_read(struct fsal_obj_handle *obj_hdl,
			  uint64_t offset,
			  size_t buffer_size, void *buffer,
			  size_t *read_amount,
			  bool *end_of_file)
{
	return next_ops.obj_ops->read(obj_hdl, offset, buffer_size,
				      buffer, read_amount, end_of_file);
}

/* nullfs_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t nullfs_write(struct fsal_obj_handle *obj_hdl,
			   uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable)
{
	return next_ops.obj_ops->write(obj_hdl, offset, buffer_size,
				       buffer, write_amount, fsal_stable);
}

/* nullfs_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t nullfs_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len)
{
	return next_ops.obj_ops->commit(obj_hdl, offset, len);
}

/* nullfs_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */

fsal_status_t nullfs_lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock)
{
	return next_ops.obj_ops->lock_op(obj_hdl, p_owner, lock_op,
					 request_lock, conflicting_lock);
}

/* nullfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t nullfs_close(struct fsal_obj_handle *obj_hdl)
{
	return next_ops.obj_ops->close(obj_hdl);
}

/* nullfs_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t nullfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
				 lru_actions_t requests)
{
	return next_ops.obj_ops->lru_cleanup(obj_hdl, requests);
}
