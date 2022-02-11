// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributor : Philippe DENIEL   philippe.deniel@cea.fr
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

/**
 * \file  file.c
 * \brief File I/O methods for KVSFS module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "kvsfs_fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "kvsfs_methods.h"
#include <stdbool.h>

static fsal_status_t kvsfs_open_by_handle(struct fsal_obj_handle *obj_hdl,
					 struct state_t *state,
					 fsal_openflags_t openflags,
					 int posix_flags,
					 fsal_verifier_t verifier,
					 struct fsal_attrlist *attrs_out,
					 enum fsal_create_mode createmode,
					 bool *cpm_check)
{
	struct kvsfs_fsal_obj_handle *kvsfs_hdl;
	fsal_status_t status;
	const bool truncated = (posix_flags & O_TRUNC) != 0;
	struct kvsfs_fd *my_fd;
	kvsns_file_open_t fd;
	int retval = 0;
	kvsns_cred_t cred;

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	kvsfs_hdl = container_of(obj_hdl,
				 struct kvsfs_fsal_obj_handle,
				 obj_handle);

	if (state != NULL) {
		my_fd = &container_of(state,
				      struct kvsfs_state_fd,
				      state)->kvsfs_fd;

	       /* Prepare to take the share reservation, but only if we
		* are called with a valid state (if state is NULL the
		* caller is a stateless create such as NFS v3 CREATE).
		*/

		/* Check share reservation conflicts. */
		status = check_share_conflict(&kvsfs_hdl->u.file.share,
					      openflags, false);

		if (FSAL_IS_ERROR(status))
			goto out;

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&kvsfs_hdl->u.file.share, FSAL_O_CLOSED,
				      openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	} else {
		/* We need to use the global fd to continue. */
		my_fd = &kvsfs_hdl->u.file.fd;
	}

	retval = kvsns_open(&cred, &kvsfs_hdl->handle->kvsfs_handle,
			    posix_flags, 0777, &fd);
	status = fsalstat(posix2fsal_error(-retval), -retval);

	if (FSAL_IS_ERROR(status)) {
		if (state == NULL)
			goto out;
		else
			goto undo_share;
	}
	/* Close any old open file descriptor and update with the new
	 * one. There shouldn't be any old open for state based call.
	 */
	if (my_fd->openflags != FSAL_O_CLOSED)
		(void)kvsns_close(&my_fd->fd);

	my_fd->fd = fd;
	my_fd->openflags = FSAL_O_NFS_FLAGS(openflags);

	if (attrs_out && (createmode >= FSAL_EXCLUSIVE || truncated)) {
		/* Refresh the attributes */
		struct stat stat;

		cred.uid = op_ctx->creds.caller_uid;
		cred.gid = op_ctx->creds.caller_gid;

		retval = kvsns_getattr(&cred,
				       &kvsfs_hdl->handle->kvsfs_handle,
				       &stat);
		status = fsalstat(posix2fsal_error(-retval), -retval);

		if (!FSAL_IS_ERROR(status)) {
			if (attrs_out != NULL)
				posix2fsal_attributes_all(&stat, attrs_out);
		}
	} else if (attrs_out && attrs_out->request_mask & ATTR_RDATTR_ERR) {
		attrs_out->valid_mask = ATTR_RDATTR_ERR;
	}

	if (state == NULL) {
		/* If no state, return status. If success, we haven't done
		 * any permission check so ask the caller to do so.
		 */
		*cpm_check = !FSAL_IS_ERROR(status);
		goto out;
	}

	if (!FSAL_IS_ERROR(status)) {
		/* Return success. We haven't done any permission
		 * check so ask the caller to do so.
		 */
		*cpm_check = true;
		return status;
	}

	(void)kvsns_close(&my_fd->fd);
	my_fd->openflags = FSAL_O_CLOSED;

 undo_share:
	/* On error we need to release our share reservation
	 * and undo the update of the share counters.
	 * This can block over an I/O operation.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	update_share_counters(&kvsfs_hdl->u.file.share, openflags,
			      FSAL_O_CLOSED);
 out:
	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

static fsal_status_t kvsfs_open_by_name(struct fsal_obj_handle *obj_hdl,
					struct state_t *state,
					const char *name,
					fsal_openflags_t openflags,
					int posix_flags,
					fsal_verifier_t verifier,
					struct fsal_attrlist *attrs_out,
					bool *cpm_check)
{
	struct fsal_obj_handle *temp = NULL;
	fsal_status_t status;

	/* We don't have open by name... */
	status = obj_hdl->obj_ops->lookup(obj_hdl, name, &temp, NULL);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL, "lookup returned %s",
			     fsal_err_txt(status));
		return status;
	}

	if (temp->type != REGULAR_FILE) {
		if (temp->type == DIRECTORY) {
			/* Trying to open2 a directory */
			status = fsalstat(ERR_FSAL_ISDIR, 0);
		} else {
			/* Trying to open2 any other non-regular file */
			status = fsalstat(ERR_FSAL_SYMLINK, 0);
		}

		/* Release the object we found by lookup. */
		temp->obj_ops->release(temp);
		LogFullDebug(COMPONENT_FSAL,
			     "open returned %s",
			     fsal_err_txt(status));
		return status;
	}

	status = kvsfs_open_by_handle(temp,
				      state,
				      openflags,
				      posix_flags,
				      verifier,
				      attrs_out,
				      FSAL_NO_CREATE,
				      cpm_check);

	if (FSAL_IS_ERROR(status)) {
		/* Release the object we found by lookup. */
		temp->obj_ops->release(temp);
		LogFullDebug(COMPONENT_FSAL, "open returned %s",
			     fsal_err_txt(status));
	}

	return status;
}

/** @fn fsal_status_t
 *       kvsfs_open(struct fsal_obj_handle *obj_hdl,
 *		     const struct req_op_context *op_ctx,
 *		     fsal_openflags_t openflags)
 *
 *  @brief Open a regular file for reading/writing its data content.
 *
 * @param obj_hdl Handle of the file to be read/modified.
 * @param op_ctx Authentication context for the operation (user,...).
 * @param openflags Flags that indicates behavior for file opening and access.
 *	This is an inclusive OR of the following values
 *	( such of them are not compatible) :
 *	- FSAL_O_RDONLY: opening file for reading only.
 *	- FSAL_O_RDWR: opening file for reading and writing.
 *	- FSAL_O_WRONLY: opening file for writing only.
 *	- FSAL_O_APPEND: always write at the end of the file.
 *	- FSAL_O_TRUNC: truncate the file to 0 on opening.
 * @param file_desc The file descriptor to be used for FSAL_read/write ops.
 *
 * @return ERR_FSAL_NO_ERROR on success, error otherwise
 */
static fsal_status_t kvsfs_open(struct fsal_obj_handle *obj_hdl,
				const struct req_op_context *op_ctx,
				int posix_flags,
				kvsns_file_open_t *fd)
{
	struct kvsfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
	kvsns_cred_t cred;

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	rc = kvsns_open(&cred, &myself->handle->kvsfs_handle, O_RDWR,
			0777, fd);

	if (rc) {
		fsal_error = posix2fsal_error(-rc);
		return fsalstat(fsal_error, -rc);
	}

	myself->u.file.fd.fd = *fd;

	if (rc) {
		fsal_error = posix2fsal_error(-rc);
		return fsalstat(fsal_error, -rc);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

static fsal_status_t kvsfs_open_func(struct fsal_obj_handle *obj_hdl,
				     fsal_openflags_t openflags,
				     struct fsal_fd *fd)
{
	fsal_status_t status;
	struct kvsfs_fd *my_fd = (struct kvsfs_fd *)fd;
	int posix_flags = 0;
	kvsns_cred_t cred;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct kvsfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle,
			      obj_handle);

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	fsal2posix_openflags(openflags, &posix_flags);

	retval = kvsns_open(&cred,
			    &myself->handle->kvsfs_handle,
			    posix_flags,
			    0777,
			    &my_fd->fd);

	fsal_error = posix2fsal_error(-retval);
	status = fsalstat(fsal_error, -retval);

	if (FSAL_IS_ERROR(status))
		return status;

	my_fd->openflags = FSAL_O_NFS_FLAGS(openflags);

	return status;
}

static fsal_status_t kvsfs_close_func(struct fsal_obj_handle *obj_hdl,
				      struct fsal_fd *fd)
{
	struct kvsfs_fd *my_fd = (struct kvsfs_fd *)fd;
	int retval;

	retval = kvsns_close(&my_fd->fd);

	memset(&my_fd->fd, 0, sizeof(kvsns_file_open_t));
	my_fd->openflags = FSAL_O_CLOSED;

	return fsalstat(posix2fsal_error(-retval), -retval);
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
 * At least the mode attribute must be set if createmode is not FSAL_NO_CREATE.
 * Some FSALs may still have to pass a mode on a create call for exclusive,
 * and even with FSAL_NO_CREATE, and empty set of attributes MUST be passed.
 *
 * If an open by name succeeds and did not result in Ganesha creating a file,
 * the caller will need to do a subsequent permission check to confirm the
 * open. This is because the permission attributes were not available
 * beforehand.
 *
 * @param[in] obj_hdl	       File to open or parent directory
 * @param[in,out] state	     state_t to use for this operation
 * @param[in] openflags	     Mode for open
 * @param[in] createmode	    Mode for create
 * @param[in] name		  Name for file if being created or opened
 * @param[in] attr_set	      Attributes to set on created file
 * @param[in] verifier	      Verifier to use for exclusive create
 * @param[in,out] new_obj	   Newly created object
 * @param[in,out] attrs_out	 Newly created object attributes
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */

fsal_status_t kvsfs_open2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state,
			  fsal_openflags_t openflags,
			  enum fsal_create_mode createmode,
			  const char *name,
			  struct fsal_attrlist *attr_set,
			  fsal_verifier_t verifier,
			  struct fsal_obj_handle **new_obj,
			  struct fsal_attrlist *attrs_out,
			  bool *caller_perm_check)
{
	struct fsal_export *export = op_ctx->fsal_export;
	struct kvsfs_fsal_obj_handle *hdl = NULL;
	struct kvsfs_file_handle fh;
	int posix_flags = 0;
	bool created = false;
	fsal_status_t status;
	mode_t unix_mode;
	bool ignore_perm_check;

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG, "attrs ", attr_set, false);

	fsal2posix_openflags(openflags, &posix_flags);

	if (createmode >= FSAL_EXCLUSIVE)
		/* Now fixup attrs for verifier if exclusive create */
		set_common_verifier(attr_set, verifier, false);

	if (name == NULL)
		return kvsfs_open_by_handle(obj_hdl,
					    state,
					    openflags,
					    posix_flags,
					    verifier, attrs_out, createmode,
					    caller_perm_check);

	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 *indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

	/* Non creation case, libgpfs doesn't have open by name so we
	 * have to do a lookup and then handle as an open by handle.
	*/
	if (createmode == FSAL_NO_CREATE)
		return kvsfs_open_by_name(obj_hdl, state, name,
					  openflags,
					  posix_flags,
					  verifier,
					  attrs_out,
					  caller_perm_check);

	/** @todo: to proceed past here, we need a struct fsal_attrlist in order
	 *         to create the fsal_obj_handle, so if it actually is NULL (it
	 *	   will actually never be since mdcache will always ask for
	 *	   attributes) we really should create a temporary fsal_attrlist
	 *         ...
	 */

	posix_flags |= O_CREAT;

	/* And if we are at least FSAL_GUARDED, do an O_EXCL create. */
	if (createmode >= FSAL_GUARDED)
		posix_flags |= O_EXCL;

       /* Fetch the mode attribute to use in the openat system call. */
	unix_mode = fsal2unix_mode(attr_set->mode) &
			~export->exp_ops.fs_umask(export);

	/* Don't set the mode if we later set the attributes */
	FSAL_UNSET_MASK(attr_set->valid_mask, ATTR_MODE);

	if (createmode == FSAL_UNCHECKED && (attr_set->valid_mask != 0)) {
	/* If we have FSAL_UNCHECKED and want to set more attributes
	 * than the mode, we attempt an O_EXCL create first, if that
	 * succeeds, then we will be allowed to set the additional
	 * attributes, otherwise, we don't know we created the file
	 * and this can NOT set the attributes.
	*/
		posix_flags |= O_EXCL;
	}

	status = kvsfs_create2(obj_hdl,
			       name,
			       op_ctx,
			       unix_mode,
			       &fh,
			       posix_flags,
			       attrs_out);

	if (status.major == ERR_FSAL_EXIST && createmode == FSAL_UNCHECKED &&
	    (posix_flags & O_EXCL) != 0) {
		/* If we tried to create O_EXCL to set attributes and
		 * failed. Remove O_EXCL and retry, also remember not
		 * to set attributes. We still try O_CREAT again just
		 * in case file disappears out from under us.
		 *
		 * Note that because we have dropped O_EXCL, later on we
		 * will not assume we created the file, and thus will
		 * not set additional attributes. We don't need to
		 * separately track the condition of not wanting to set
		 * attributes.
		 */
		posix_flags &= ~O_EXCL;
		status = kvsfs_create2(obj_hdl,
				       name,
				       op_ctx,
				       unix_mode,
				       &fh,
				       posix_flags,
				       attrs_out);
	}

	if (FSAL_IS_ERROR(status))
		return status;

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

	/* Check if the object type is SYMBOLIC_LINK for a state object.
	 * If yes, then give error ERR_FSAL_SYMLINK.
	 */
	if (state != NULL &&
		attrs_out != NULL && attrs_out->type != REGULAR_FILE) {
		LogDebug(COMPONENT_FSAL, "Trying to open a non-regular file");
			if (attrs_out->type == DIRECTORY) {
				/* Trying to open2 a directory */
				status = fsalstat(ERR_FSAL_ISDIR, 0);
			} else {
				/* Trying to open2 any other non-regular file */
				status = fsalstat(ERR_FSAL_SYMLINK, 0);
			}
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = kvsfs_alloc_handle(&fh, attrs_out, NULL, export);
	if (hdl == NULL) {
		status = fsalstat(posix2fsal_error(ENOMEM), ENOMEM);
		goto fileerr;
	}

	*new_obj = &hdl->obj_handle;

	if (created && attr_set->valid_mask != 0) {
		/* Set attributes using our newly opened file descriptor as the
		 * share_fd if there are any left to set (mode and truncate
		 * have already been handled).
		 *
		 * Note that we only set the attributes if we were responsible
		 * for creating the file.
		 */
		status = (*new_obj)->obj_ops->setattr2(*new_obj, false, state,
						      attr_set);
		if (FSAL_IS_ERROR(status))
			goto fileerr;

		if (attrs_out != NULL) {
			status = (*new_obj)->obj_ops->getattrs(*new_obj,
							      attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0)
				/* Get attributes failed and caller expected
				 * to get the attributes. Otherwise continue
				 * with attrs_out indicating ATTR_RDATTR_ERR.
				 */
				goto fileerr;
		}
	}

	/* Restore posix_flags as it was modified for create above */
	fsal2posix_openflags(openflags, &posix_flags);

	/* We created a file with the caller's credentials active, so as such
	  * permission check was done. So we don't need the caller to do
	  * permission check again (for that we have already set
	  * *caller_perm_check=false). Passing ignore_perm_check to
	  * open_by_handle() as we don't want to modify the value at
	  * caller_perm_check.
	  */
	return kvsfs_open_by_handle(&hdl->obj_handle,
				    state,
				    openflags,
				    posix_flags,
				    verifier,
				    attrs_out,
				    createmode,
				    &ignore_perm_check);

 fileerr:
	if (hdl != NULL) {
		/* Release the handle we just allocated. */
		(*new_obj)->obj_ops->release(*new_obj);
		*new_obj = NULL;
	}

	if (created) {
		fsal_status_t status2;
		kvsns_cred_t cred;
		struct kvsfs_fsal_obj_handle *myself;
		int retval = 0;

		myself = container_of(obj_hdl,
				      struct kvsfs_fsal_obj_handle,
				      obj_handle);

		cred.uid = op_ctx->creds.caller_uid;
		cred.gid = op_ctx->creds.caller_gid;

		/* Remove the file we just created */
		retval = kvsns_unlink(&cred,
				      &myself->handle->kvsfs_handle,
				      (char *)name);
		status2 = fsalstat(posix2fsal_error(-retval), -retval);
		if (FSAL_IS_ERROR(status2)) {
			LogEvent(COMPONENT_FSAL,
				 "kvsns_unlink failed, error: %s",
				 msg_fsal_err(status2.major));
		}
	}
	return status;
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
fsal_status_t kvsfs_reopen2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state,
			    fsal_openflags_t openflags)
{
	struct kvsfs_fd *my_share_fd = &container_of(state,
						     struct kvsfs_state_fd,
						     state)->kvsfs_fd;
	fsal_status_t status;
	int posix_flags = 0;
	kvsns_file_open_t my_fd;
	struct fsal_share *share = &container_of(obj_hdl,
						 struct kvsfs_fsal_obj_handle,
						 obj_handle)->u.file.share;

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	/* We can conflict with old share, so go ahead and check now. */
	status = check_share_conflict(share, openflags, false);

	if (FSAL_IS_ERROR(status)) {
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
		return status;
	}

	/* Set up the new share so we can drop the lock and not have a
	* conflicting share be asserted, updating the share counters.
	*/
	update_share_counters(share, my_share_fd->openflags, openflags);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	fsal2posix_openflags(openflags, &posix_flags);

	status = kvsfs_open(obj_hdl, op_ctx, posix_flags, &my_fd);

	if (!FSAL_IS_ERROR(status)) {
		/* Close the existing file descriptor and copy the new
		 * one over. Make sure no one is using the fd that we are
		 * about to close!
		 */
		PTHREAD_RWLOCK_wrlock(&my_share_fd->fdlock);

		(void)kvsns_close(&my_share_fd->fd);

		my_share_fd->fd = my_fd;
		my_share_fd->openflags = FSAL_O_NFS_FLAGS(openflags);

		PTHREAD_RWLOCK_unlock(&my_share_fd->fdlock);
	} else {
		/* We had a failure on open - we need to revert the share.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		update_share_counters(share, openflags, my_share_fd->openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

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
 * @param[in] obj_hdl	  File on which to operate
 * @param[in] state	    state_t to use for this operation
 * @param[in] offset	   Start of range to commit
 * @param[in] len	      Length of range to commit
 *
 * @return FSAL status.
 */

fsal_status_t kvsfs_commit2(struct fsal_obj_handle *obj_hdl,    /* sync */
			    off_t offset,
			    size_t len)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}



/* kvsfs_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t kvsfs_status2(struct fsal_obj_handle *obj_hdl,
			       struct state_t *state)
{
	struct kvsfs_fsal_obj_handle *myself;

	/** @todo better management of state_t */
	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle,
			      obj_handle);
	return myself->u.file.fd.openflags;
}

/* kvsfs_read
 * concurrency (locks) is managed in mdcache_*
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

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd.openflags != FSAL_O_CLOSED);

	retval = kvsns_read(&cred, &myself->u.file.fd.fd,
			    buffer, buffer_size, offset);


	/* With FSAL_KVSFS, "end of file" is always returned via a last call,
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
 * concurrency (locks) is managed in mdcache_*
 */

fsal_status_t kvsfs_write(struct fsal_obj_handle *obj_hdl,
			 uint64_t offset,
			 size_t buffer_size, void *buffer,
			 size_t *write_amount, bool *fsal_stable)
{
	struct kvsfs_fsal_obj_handle *myself;
	kvsns_cred_t cred;
	int retval = 0;

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd.openflags != FSAL_O_CLOSED);

	retval = kvsns_write(&cred, &myself->u.file.fd.fd,
			     buffer, buffer_size, offset);

	if (retval < 0)
		return fsalstat(posix2fsal_error(-retval), -retval);
	*write_amount = retval;
	*fsal_stable = true;

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

fsal_status_t kvsfs_close2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state)
{
	struct kvsfs_fsal_obj_handle *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct kvsfs_fd *my_fd = NULL;

	assert(obj_hdl->type == REGULAR_FILE);
	assert(state != NULL);

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle, obj_handle);

	my_fd = &container_of(state,
			      struct kvsfs_state_fd,
			      state)->kvsfs_fd;

	if (state->state_type == STATE_TYPE_SHARE ||
	    state->state_type == STATE_TYPE_NLM_SHARE ||
	    state->state_type == STATE_TYPE_9P_FID) {
		/* This is a share state, we must update the share counters */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		update_share_counters(&myself->u.file.share,
				      my_fd->openflags,
				      FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

	PTHREAD_RWLOCK_wrlock(&my_fd->fdlock);
	retval = kvsns_close(&my_fd->fd);
	PTHREAD_RWLOCK_unlock(&my_fd->fdlock);

	if (retval < 0)
		fsal_error = posix2fsal_error(-retval);

	my_fd->openflags = FSAL_O_CLOSED;

	return fsalstat(fsal_error, -retval);
}

static fsal_status_t kvsfs_find_fd(struct kvsfs_fd *fd,
				   struct fsal_obj_handle *obj_hdl,
				   bool bypass,
				   struct state_t *state,
				   fsal_openflags_t openflags,
				   bool *has_lock,
				   bool *closefd,
				   bool open_for_locks)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct kvsfs_fsal_obj_handle *myself;
	struct kvsfs_fd temp_fd;
	struct kvsfs_fd *out_fd;
	int posix_flags;
	bool reusing_open_state_fd = false;

	myself = container_of(obj_hdl,
			      struct kvsfs_fsal_obj_handle,
			      obj_handle);

	memset(&temp_fd, 0, sizeof(struct kvsfs_fd));
	(void)pthread_rwlock_destroy(&temp_fd.fdlock);
	temp_fd.openflags = FSAL_O_CLOSED;

	out_fd = &temp_fd;

	fsal2posix_openflags(openflags, &posix_flags);

	LogFullDebug(COMPONENT_FSAL, "openflags 0x%X posix_flags 0x%X",
			openflags, posix_flags);

	if (obj_hdl->type != REGULAR_FILE)
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);

	status = fsal_find_fd((struct fsal_fd **)&out_fd,
			      obj_hdl,
			      (struct fsal_fd *)&myself->u.file.fd,
			      &myself->u.file.share,
			      bypass,
			      state,
			      openflags,
			      kvsfs_open_func,
			      kvsfs_close_func,
			      has_lock,
			      closefd,
			      open_for_locks,
			      &reusing_open_state_fd);

	if (FSAL_IS_SUCCESS(status))
		*fd = *out_fd;

	return status;
}


void kvsfs_read2(struct fsal_obj_handle *obj_hdl,
		 bool bypass,
		 fsal_async_cb done_cb,
		 struct fsal_io_arg *read_arg,
		 void *caller_arg)
{
	struct kvsfs_fd kvsfs_fd;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	bool has_lock = false;
	bool closefd = false;
	ssize_t nb_read;
	uint64_t offset = read_arg->offset;
	kvsns_cred_t cred;
	int i;

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	if (read_arg->info != NULL) {
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0), read_arg,
					caller_arg);
		return;
	}

	status =
		kvsfs_find_fd(&kvsfs_fd,
			      obj_hdl,
			      bypass,
			      read_arg->state,
			      FSAL_O_READ,
			      &has_lock,
			      &closefd,
			      false);

	if (FSAL_IS_ERROR(status)) {
		goto out;
	}

	for (i = 0; i < read_arg->iov_count; i++) {
		nb_read = kvsns_read(&cred,
				     &kvsfs_fd.fd,
				     read_arg->iov[i].iov_base,
				     read_arg->iov[i].iov_len,
				     offset);

		read_arg->io_amount += nb_read;
		offset += nb_read;
	}

	read_arg->end_of_file = (read_arg->io_amount == 0);

out:
	if (closefd)
		kvsns_close(&kvsfs_fd.fd);

	if (has_lock) {
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

	done_cb(obj_hdl, status, read_arg, caller_arg);
}

void kvsfs_write2(struct fsal_obj_handle *obj_hdl,
		  bool bypass,
		  fsal_async_cb done_cb,
		  struct fsal_io_arg *write_arg,
		  void *caller_arg)
{
	struct kvsfs_fd kvsfs_fd;
	fsal_status_t status;
	bool has_lock = false;
	bool closefd = false;
	ssize_t nb_written;
	uint64_t offset = write_arg->offset;
	kvsns_cred_t cred;
	int i;

	cred.uid = op_ctx->creds.caller_uid;
	cred.gid = op_ctx->creds.caller_gid;

	if (write_arg->info)
		return done_cb(obj_hdl,
			       fsalstat(ERR_FSAL_NOTSUPP, 0),
			       write_arg,
			       caller_arg);

	status = kvsfs_find_fd(&kvsfs_fd,
			       obj_hdl,
			       bypass,
			       write_arg->state,
			       FSAL_O_WRITE,
			       &has_lock,
			       &closefd,
			       false);

	if (FSAL_IS_ERROR(status))
		goto out;

	for (i = 0; i < write_arg->iov_count; i++) {
		nb_written = kvsns_write(&cred,
					 &kvsfs_fd.fd,
					 write_arg->iov[i].iov_base,
					 write_arg->iov[i].iov_len,
					 offset);

		if (nb_written < 0)
			goto out;

		write_arg->io_amount += nb_written;
		offset += nb_written;
	}

out:
	if (closefd)
		kvsns_close(&kvsfs_fd.fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	done_cb(obj_hdl, status, write_arg, caller_arg);
}

