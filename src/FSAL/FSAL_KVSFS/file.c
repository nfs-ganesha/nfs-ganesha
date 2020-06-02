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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

/* file.c
 * File I/O methods for KVSFS module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "kvsfs_methods.h"
#include <stdbool.h>

/** kvsfs_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t kvsfs_open(struct fsal_obj_handle *obj_hdl,
			fsal_openflags_t openflags)
{
	struct kvsfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
	kvsns_cred_t cred;

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.openflags == FSAL_O_CLOSED);

	rc = kvsns_open(&cred, &myself->handle->kvsfs_handle, O_RDWR,
			0777, &myself->u.file.fd);

	if (rc) {
		fsal_error = posix2fsal_error(-rc);
		return fsalstat(fsal_error, -rc);
	}

	/* >> fill output struct << */
	myself->u.file.openflags = openflags;

	/* save the stat */
	rc = kvsns_getattr(&cred, &myself->handle->kvsfs_handle,
			   &myself->u.file.saved_stat);

	if (rc) {
		fsal_error = posix2fsal_error(-rc);
		return fsalstat(fsal_error, -rc);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* kvsfs_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t kvsfs_status(struct fsal_obj_handle *obj_hdl)
{
	struct kvsfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle,
			      obj_handle);
	return myself->u.file.openflags;
}

/* kvsfs_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t kvsfs_read(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size, void *buffer, size_t *read_amount,
			bool *end_of_file)
{
	struct kvsfs_fsal_obj_handle *myself;
	int retval = 0;
	kvsns_cred_t cred;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.openflags != FSAL_O_CLOSED);

	retval = kvsns_read(&cred, &myself->u.file.fd,
			    buffer, buffer_size, offset);


	/* With FSAL_ZFS, "end of file" is always returned via a last call,
	 * once every data is read. The result is a last,
	 * empty call which set end_of_file to true */
	if (retval < 0) {
		fsal_error = posix2fsal_error(-retval);
		return fsalstat(fsal_error, -retval);
	} else if (retval == 0) {
		*end_of_file = true;
		*read_amount = 0;
	} else {
		*end_of_file = false;
		*read_amount = retval;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* kvsfs_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t kvsfs_write(struct fsal_obj_handle *obj_hdl,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable)
{
	struct kvsfs_fsal_obj_handle *myself;
	kvsns_cred_t cred;
	int retval = 0;

	cred.uid = op_ctx->creds->caller_uid;
	cred.gid = op_ctx->creds->caller_gid;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.openflags != FSAL_O_CLOSED);

	retval = kvsns_write(&cred, &myself->u.file.fd,
			     buffer, buffer_size, offset);

	if (retval < 0)
		return fsalstat(posix2fsal_error(-retval), -retval);
	*write_amount = retval;
	*fsal_stable = false;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* kvsfs_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t kvsfs_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			  off_t offset, size_t len)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* kvsfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t kvsfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct kvsfs_fsal_obj_handle *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	assert(obj_hdl->type == REGULAR_FILE);
	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	if (myself->u.file.openflags != FSAL_O_CLOSED) {
		retval = kvsns_close(&myself->u.file.fd);
		if (retval < 0)
			fsal_error = posix2fsal_error(-retval);

		myself->u.file.openflags = FSAL_O_CLOSED;
	}

	return fsalstat(fsal_error, -retval);
}

/* kvsfs_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t kvsfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			       lru_actions_t requests)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t kvsfs_lock_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
