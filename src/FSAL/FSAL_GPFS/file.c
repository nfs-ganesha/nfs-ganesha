// SPDX-License-Identifier: LGPL-3.0-or-later
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

/* _FILE_OFFSET_BITS macro causes F_GETLK/SETLK/SETLKW to be defined to
 * F_GETLK64/SETLK64/SETLKW64. Currently GPFS kernel module doesn't work
 * with these 64 bit macro values through ganesha interface. Undefine it
 * here to use plain F_GETLK/SETLK/SETLKW values.
 */
#undef _FILE_OFFSET_BITS

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "FSAL/fsal_localfs.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <unistd.h>
#include <fcntl.h>
#include "gpfs_methods.h"

#define STATE2FD(s) (&container_of(s, struct gpfs_state_fd, state)->gpfs_fd)
extern uint64_t get_handle2inode(struct gpfs_file_handle *gfh);

/**
 * @brief GPFS Function to open or reopen a fsal_fd.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   New mode for open
 * @param[out] fsal_fd     File descriptor that is to be used
 *
 * @return FSAL status.
 */

fsal_status_t gpfs_reopen_func(struct fsal_obj_handle *obj_hdl,
			       fsal_openflags_t openflags,
			       struct fsal_fd *fsal_fd)
{
	fsal_status_t status, status2;
	struct gpfs_fd *my_fd;
	int posix_flags = 0;
	int fd;

	my_fd = container_of(fsal_fd, struct gpfs_fd, fsal_fd);

	fsal2posix_openflags(openflags, &posix_flags);

	LogFullDebug(COMPONENT_FSAL,
		     "my_fd->fd = %d openflags = %x, posix_flags = %x",
		     my_fd->fd, openflags, posix_flags);

	status = GPFSFSAL_open(obj_hdl, posix_flags, &fd);

	if (FSAL_IS_ERROR(status))
		return status;

	if (my_fd->fd != -1) {
		/* File was previously open, close old fd */
		status2 = fsal_internal_close(my_fd->fd, NULL, 0);

		if (FSAL_IS_ERROR(status2)) {
			LogFullDebug(COMPONENT_FSAL,
				     "close failed with %s",
				     fsal_err_txt(status));

			/** @todo - what to do about error here... */
		}
	}

	/* Save the file descriptor, make sure we only save the
	 * open modes that actually represent the open file.
	 */
	LogFullDebug(COMPONENT_FSAL,
		     "fd = %d, new openflags = %x",
		     fd, openflags);
	if (fd == 0)
		LogCrit(COMPONENT_FSAL,
			"fd = %d, new openflags = %x",
			fd, openflags);

	my_fd->fd = fd;
	my_fd->fsal_fd.openflags = FSAL_O_NFS_FLAGS(openflags);

	return status;
}

/**
 * @brief GPFS Function to close a fsal_fd.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  fsal_fd     File handle to close
 *
 * @return FSAL status.
 */

fsal_status_t gpfs_close_func(struct fsal_obj_handle *obj_hdl,
			      struct fsal_fd *fsal_fd)
{
	fsal_status_t status;
	struct gpfs_fd *my_fd;

	my_fd = container_of(fsal_fd, struct gpfs_fd, fsal_fd);

	status = fsal_internal_close(my_fd->fd, NULL, 0);
	my_fd->fd = -1;
	my_fd->fsal_fd.openflags = FSAL_O_CLOSED;

	return status;
}

/**
 * @brief Merge a duplicate handle with an original handle
 *
 * This function is used if an upper layer detects that a duplicate
 * object handle has been created. It allows the FSAL to merge anything
 * from the duplicate back into the original.
 *
 * The caller must release the object (the caller may have to close
 * files if the merge is unsuccessful).
 *
 * @param[in]  orig_hdl  Original handle
 * @param[in]  dupe_hdl Handle to merge into original
 *
 * @return FSAL status.
 *
 */

fsal_status_t gpfs_merge(struct fsal_obj_handle *orig_hdl,
			 struct fsal_obj_handle *dupe_hdl)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	if (orig_hdl->type == REGULAR_FILE &&
	    dupe_hdl->type == REGULAR_FILE) {
		/* We need to merge the share reservations on this file.
		 * This could result in ERR_FSAL_SHARE_DENIED.
		*/
		struct gpfs_fsal_obj_handle *orig, *dupe;

		orig = container_of(orig_hdl,
				    struct gpfs_fsal_obj_handle,
				    obj_handle);
		dupe = container_of(dupe_hdl,
				    struct gpfs_fsal_obj_handle,
				    obj_handle);

		/* This can block over an I/O operation. */
		status = merge_share(orig_hdl, &orig->u.file.share,
				     &dupe->u.file.share);
	}

	return status;
}

static fsal_status_t
open_by_handle(struct fsal_obj_handle *obj_hdl, struct state_t *state,
	       fsal_openflags_t openflags,
	       enum fsal_create_mode createmode,
	       fsal_verifier_t verifier,
	       struct fsal_attrlist *attrs_out)
{
	struct fsal_export *export = op_ctx->fsal_export;
	struct gpfs_fsal_obj_handle *gpfs_hdl;
	struct gpfs_filesystem *gpfs_fs = obj_hdl->fs->private_data;
	fsal_status_t status;
	bool truncated = openflags & FSAL_O_TRUNC;
	struct gpfs_fd *my_fd;
	struct fsal_fd *fsal_fd;
	fsal_openflags_t old_openflags;
	bool is_fresh_open = false;

	gpfs_hdl = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	if (state != NULL) {
		my_fd = &container_of(state,
				      struct gpfs_state_fd,
				      state)->gpfs_fd;
	} else {
		/* We need to use the global fd to continue. */
		my_fd = &gpfs_hdl->u.file.fd;
	}

	fsal_fd = &my_fd->fsal_fd;

	/* Indicate we want to do fd work (can't fail since not reclaiming) */
	fsal_start_fd_work_no_reclaim(fsal_fd);

	old_openflags = my_fd->fsal_fd.openflags;

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is a
		 * stateless create such as NFS v3 CREATE and we're just going
		 * to ignore share reservation stuff).
		 */

		/* Now that we have the mutex, and no I/O is in progress so we
		 * have exclusive access to the share's fsal_fd, we can look at
		 * its openflags. We also need to work the share reservation so
		 * take the obj_lock. NOTE: This is the ONLY sequence where both
		 * a work_mutex and the obj_lock are taken, so there is no
		 * opportunity for ABBA deadlock.
		 *
		 * Note that we do hold the obj_lock over an open and a close
		 * which is longer than normal, but the previous iteration of
		 * the code held the obj lock (read granted) over whole I/O
		 * operations... We don't block over I/O because we've assured
		 * that no I/O is in progress or can start before proceeding
		 * past the above while loop.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		/* Now check the new share. */
		status = check_share_conflict(&gpfs_hdl->u.file.share,
					      openflags, false);

		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_FSAL,
				 "check_share_conflict returned %s",
				 fsal_err_txt(status));
			goto exit;
		}
	}

	/* Check for a genuine no-op open. That means we aren't trying to
	 * create, the file is already open in the same mode with the same
	 * deny flags, and we aren't trying to truncate. In this case we want
	 * to avoid bouncing the fd. In the case of JUST changing the deny mode
	 * or an replayed exclusive create, we might bounce the fd when we could
	 * have avoided that, but those scenarios are much less common.
	 */
	if (FSAL_O_NFS_FLAGS(openflags) == FSAL_O_NFS_FLAGS(old_openflags) &&
	    truncated == false && createmode == FSAL_NO_CREATE) {
		LogFullDebug(COMPONENT_FSAL,
			     "no-op reopen2 my_fd->fd = %d openflags = %x",
			     my_fd->fd, openflags);
		goto exit;
	}

	/* Tracking if we were going to reopen a fd that was
	 * closed by another thread before we got here. */
	is_fresh_open = ((old_openflags == FSAL_O_CLOSED) && (my_fd->fd < 0));

	/* No share conflict, re-open the share fd */
	status = gpfs_reopen_func(obj_hdl, openflags, fsal_fd);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "gpfs_reopen_func returned %s",
			 fsal_err_txt(status));
		goto exit;
	}

	/* Inserts to fd_lru only if open succeeds */
	if (is_fresh_open) {
		/* This is actually an open, need to increment
		 * appropriate counter and insert into LRU.
		 */
		insert_fd_lru(fsal_fd);
	} else {
		/* Bump up the FD in fd_lru as it was already in fd lru. */
		bump_fd_lru(fsal_fd);
	}

	if (createmode >= FSAL_EXCLUSIVE || (truncated && attrs_out)) {
		/* NOTE: won't come in here when called from gpfs_reopen2...
		 *       truncated might be set, but attrs_out will be NULL.
		 */

		/* Refresh the attributes */
		status = GPFSFSAL_getattrs(export, gpfs_fs,
					   gpfs_hdl->handle, attrs_out);

		if (!FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL, "New size = %"PRIx64,
				     attrs_out->filesize);

			/* Now check verifier for exclusive */
			if (createmode >= FSAL_EXCLUSIVE &&
			    !check_verifier_attrlist(attrs_out, verifier, false)
			   ) {
				/* Verifier didn't match, return EEXIST */
				status = fsalstat(posix2fsal_error(EEXIST),
						  EEXIST);
			}
		}
	} else if (attrs_out && attrs_out->request_mask & ATTR_RDATTR_ERR) {
		attrs_out->valid_mask = ATTR_RDATTR_ERR;
	}

	if (FSAL_IS_ERROR(status)) {
		if (is_fresh_open) {
			/* Now that we have decided to close this FD,
			 * let's clean it off from fd_lru and
			 * ensure counters are decremented.
			 */
			remove_fd_lru(fsal_fd);
		}
		/* Close fd */
		(void) gpfs_close_func(obj_hdl, fsal_fd);
	}

exit:

	if (state != NULL) {
		if (!FSAL_IS_ERROR(status)) {
			/* Success, establish the new share. */
			update_share_counters(&gpfs_hdl->u.file.share,
					      old_openflags,
					      openflags);
		}

		/* Release obj_lock. */
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

	/* Indicate we are done with fd work and signal any waiters. */
	fsal_complete_fd_work(fsal_fd);

	return status;
}

static fsal_status_t
open_by_name(struct fsal_obj_handle *obj_hdl, struct state_t *state,
	     const char *name, fsal_openflags_t openflags,
	     fsal_verifier_t verifier, struct fsal_attrlist *attrs_out)
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

	status = open_by_handle(temp, state, openflags, FSAL_NO_CREATE,
				verifier, attrs_out);

	if (FSAL_IS_ERROR(status)) {
		/* Release the object we found by lookup. */
		temp->obj_ops->release(temp);
		LogFullDebug(COMPONENT_FSAL, "open returned %s",
			     fsal_err_txt(status));
	}

	return status;
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
 * @param[in]     obj_hdl               File to open or parent directory
 * @param[in,out] state                 state_t to use for this operation
 * @param[in]     openflags             Mode for open
 * @param[in]     createmode            Mode for create
 * @param[in]     name                  Name for file if being created or opened
 * @param[in]     attr_set              Attributes to set on created file
 * @param[in]     verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj               Newly created object
 * @param[in,out] attrs_out             Newly created object attributes
 * @param[in,out] caller_perm_check     The caller must do a permission check
 * @param[in,out] parent_pre_attrs_out  Optional attributes for parent dir
 *                                      before the operation. Should be atomic.
 * @param[in,out] parent_post_attrs_out Optional attributes for parent dir
 *                                      after the operation. Should be atomic.
 *
 * @return FSAL status.
 */
fsal_status_t
gpfs_open2(struct fsal_obj_handle *obj_hdl, struct state_t *state,
	   fsal_openflags_t openflags, enum fsal_create_mode createmode,
	   const char *name, struct fsal_attrlist *attr_set,
	   fsal_verifier_t verifier, struct fsal_obj_handle **new_obj,
	   struct fsal_attrlist *attrs_out, bool *caller_perm_check,
	   struct fsal_attrlist *parent_pre_attrs_out,
	   struct fsal_attrlist *parent_post_attrs_out)
{
	struct gpfs_fsal_obj_handle *hdl = NULL;
	struct fsal_export *export = op_ctx->fsal_export;
	struct gpfs_file_handle fh;
	int posix_flags = 0;
	bool created = false;
	fsal_status_t status;
	mode_t unix_mode;

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG, "attrs ", attr_set, false);

	fsal2posix_openflags(openflags, &posix_flags);

	if (createmode >= FSAL_EXCLUSIVE)
		/* Now fixup attrs for verifier if exclusive create */
		set_common_verifier(attr_set, verifier, false);

	if (name == NULL) {
		status = open_by_handle(obj_hdl, state, openflags, createmode,
					verifier, attrs_out);

		*caller_perm_check = FSAL_IS_SUCCESS(status);
		return status;
	}

	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 * indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

	/* Non creation case, libgpfs doesn't have open by name so we
	 * have to do a lookup and then handle as an open by handle.
	 */
	if (createmode == FSAL_NO_CREATE) {
		/** @todo FSF - this is very suspicious that we don't return
		 *              a new fsal_obj_handle for the newly opened
		 *              file...
		 */
		status = open_by_name(obj_hdl, state, name, openflags,
				      verifier, attrs_out);

		*caller_perm_check = FSAL_IS_SUCCESS(status);
		return status;
	}

	/** @todo: to proceed past here, we need a struct fsal_attrlist in order
	 *         to create the fsal_obj_handle, so if it actually is NULL (it
	 *         will actually never be since mdcache will always ask for
	 *         attributes) we really should create a temporary
	 *         fsal_attrlist...
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

	status = GPFSFSAL_create2(obj_hdl, name, unix_mode, &fh,
				  posix_flags, attrs_out);

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
		status = GPFSFSAL_create2(obj_hdl, name, unix_mode, &fh,
					  posix_flags, attrs_out);
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
	hdl = alloc_handle(&fh, obj_hdl->fs, attrs_out, NULL, export);
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

	/* We created a file with the caller's credentials active, so as such
	 * permission check was done. So we don't need the caller to do
	 * permission check again (for that we have already set
	 * *caller_perm_check=false).
	 */
	return open_by_handle(&hdl->obj_handle, state, openflags, createmode,
			      verifier, attrs_out);

 fileerr:
	if (hdl != NULL) {
		/* Release the handle we just allocated. */
		(*new_obj)->obj_ops->release(*new_obj);
		*new_obj = NULL;
	}

	if (created) {
		fsal_status_t status2;

		/* Remove the file we just created */
		status2 = GPFSFSAL_unlink(obj_hdl, name);
		if (FSAL_IS_ERROR(status2)) {
			LogEvent(COMPONENT_FSAL,
				 "GPFSFSAL_unlink failed, error: %s",
				 msg_fsal_err(status2.major));
		}
	}
	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			get_handle2inode(gfh), msg_fsal_err(status.major));
	}
	return status;
}

/**
 *  @brief GPFS read plus
 *
 *  @param obj_hdl FSAL object handle / or fd
 *  @param offset The offset to read from
 *  @param buffer_size Size of buffer
 *  @param buffer void reference to buffer
 *  @param read_amount size_t reference to amount of data read
 *  @param end_of_file boolean indiocating the end of file
 *  @param info I/O information
 *  @return FSAL status
 */
fsal_status_t
gpfs_read_plus_fd(int my_fd, uint64_t offset,
		  size_t buffer_size, void *buffer, size_t *read_amount,
		  bool *end_of_file, struct io_info *info, int expfd)
{
	const fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct read_arg rarg = {0};
	ssize_t nb_read;
	int errsv;

	if (!buffer || !read_amount || !end_of_file || !info)
		return fsalstat(ERR_FSAL_FAULT, 0);

	assert(my_fd >= 3);

	rarg.mountdirfd = expfd;
	rarg.fd = my_fd;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = buffer_size;
	rarg.options = IO_SKIP_HOLE;
	rarg.cli_ip = NULL;
	if (op_ctx && op_ctx->client)
		rarg.cli_ip = op_ctx->client->hostaddr_str;

	nb_read = gpfs_ganesha(OPENHANDLE_READ_BY_FD, &rarg);
	errsv = errno;

	if (nb_read < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		if (errsv != ENODATA)
			return fsalstat(posix2fsal_error(errsv), errsv);

		/* errsv == ENODATA */
#if 0
		/** @todo FSF: figure out how to fix this... */
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
#endif
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
	    (nb_read == 0 || nb_read < buffer_size))
		*end_of_file = true;
	else
		*end_of_file = false;

	return status;
}

/**
 * @brief Return open status of a state.
 *
 * This function returns open flags representing the current open
 * status for a state. The st_lock must be held.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       File state to interrogate
 *
 * @retval Flags representing current open status
 */

fsal_openflags_t gpfs_status2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state)
{
	struct gpfs_fd *my_fd = &((struct gpfs_state_fd *)state)->gpfs_fd;

	return my_fd->fsal_fd.openflags;
}

/**
 * @brief Re-open a file that may be already opened
 *
 * This function supports changing the access mode of a share reservation and
 * thus should only be called with a share state. The st_lock must be held.
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

fsal_status_t
gpfs_reopen2(struct fsal_obj_handle *obj_hdl, struct state_t *state,
	     fsal_openflags_t openflags)
{
	return open_by_handle(obj_hdl, state, openflags, FSAL_NO_CREATE,
			      NULL, NULL);
}

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations.  This is
 * an (optionally) asynchronous call.  When the I/O is complete, the done
 * callback is called with the results.
 *
 * @note This does not handle iovecs larger than 1
 *
 * @param[in]     obj_hdl	File on which to operate
 * @param[in]     bypass	If state doesn't indicate a share reservation,
 *				bypass any deny read
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 *
 * @return Nothing; results are in callback
 */
void
gpfs_read2(struct fsal_obj_handle *obj_hdl, bool bypass, fsal_async_cb done_cb,
	   struct fsal_io_arg *read_arg, void *caller_arg)
{
	fsal_status_t status, status2;
	struct gpfs_fd *my_fd;
	struct gpfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;
	struct gpfs_fsal_obj_handle *myself;
	int i;
	uint64_t offset = read_arg->offset;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		done_cb(obj_hdl, fsalstat(posix2fsal_error(EXDEV), EXDEV),
			read_arg, caller_arg);
		return;
	}

	/* Indicate a desire to start io and get a usable file descritor */
	status = fsal_start_io(&out_fd, obj_hdl, &myself->u.file.fd.fsal_fd,
			       &temp_fd.fsal_fd, read_arg->state, FSAL_O_READ,
			       false, NULL, bypass, &myself->u.file.share);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			     "fsal_start_io failed returning %s",
			     fsal_err_txt(status));
		goto exit;
	}

	my_fd = container_of(out_fd, struct gpfs_fd, fsal_fd);

	read_arg->io_amount = 0;

	for (i = 0; i < read_arg->iov_count; i++) {
		size_t io_amount;

		if (read_arg->info)
			status = gpfs_read_plus_fd(my_fd->fd, offset,
						   read_arg->iov[i].iov_len,
						   read_arg->iov[i].iov_base,
						   &io_amount,
						   &read_arg->end_of_file,
						   read_arg->info,
						   export_fd);
		else
			status = GPFSFSAL_read(my_fd->fd, offset,
					       read_arg->iov[i].iov_len,
					       read_arg->iov[i].iov_base,
					       &io_amount,
					       &read_arg->end_of_file,
					       export_fd);

		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_FSAL,
				 "Inode involved: %"PRIu64", error: %s",
				 get_handle2inode(myself->handle),
				 fsal_err_txt(status));
			break;
		}

		offset += read_arg->iov[i].iov_len;
		read_arg->io_amount += io_amount;
	}

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	if (read_arg->state == NULL) {
		/* We did I/O without a state so we need to release the temp
		 * share reservation acquired.
		 */

		/* Release the share reservation now by updating the counters.
		 */
		update_share_counters_locked(obj_hdl, &myself->u.file.share,
					     FSAL_O_READ, FSAL_O_CLOSED);
	}

 exit:

	done_cb(obj_hdl, status, read_arg, caller_arg);
}

/**
 * @brief Write data to a file
 *
 * This function writes data to a file. The FSAL must be able to
 * perform the write whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. Even
 * with bypass == true, it will enforce a mandatory (NFSv4) deny_write if
 * an appropriate state is not passed).
 *
 * The FSAL is expected to enforce sync if necessary.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any non-mandatory deny write
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] write_arg	Info about write, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 */

void
gpfs_write2(struct fsal_obj_handle *obj_hdl,
	    bool bypass,
	    fsal_async_cb done_cb,
	    struct fsal_io_arg *write_arg,
	    void *caller_arg)
{
	fsal_status_t status, status2;
	struct gpfs_fd *my_fd;
	struct gpfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;
	struct gpfs_fsal_obj_handle *myself;
	int i;
	uint64_t offset = write_arg->offset;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		done_cb(obj_hdl, fsalstat(posix2fsal_error(EXDEV), EXDEV),
			write_arg, caller_arg);
		return;
	}

	/* Indicate a desire to start io and get a usable file descritor */
	status = fsal_start_io(&out_fd, obj_hdl, &myself->u.file.fd.fsal_fd,
			       &temp_fd.fsal_fd, write_arg->state, FSAL_O_WRITE,
			       false, NULL, bypass, &myself->u.file.share);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			     "fsal_start_io failed returning %s",
			     fsal_err_txt(status));
		goto exit;
	}

	my_fd = container_of(out_fd, struct gpfs_fd, fsal_fd);

	write_arg->io_amount = 0;

	for (i = 0; i < write_arg->iov_count; i++) {
		size_t io_amount;

		status = GPFSFSAL_write(my_fd->fd, offset,
					write_arg->iov[i].iov_len,
					write_arg->iov[i].iov_base,
					&io_amount,
					&write_arg->fsal_stable,
					export_fd);

		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_FSAL,
				 "Inode involved: %"PRIu64", error: %s",
				 get_handle2inode(myself->handle),
				 fsal_err_txt(status));
			break;
		}

		offset += write_arg->iov[i].iov_len;
		write_arg->io_amount += io_amount;
	}

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	if (write_arg->state == NULL) {
		/* We did I/O without a state so we need to release the temp
		 * share reservation acquired.
		 */

		/* Release the share reservation now by updating the counters.
		 */
		update_share_counters_locked(obj_hdl, &myself->u.file.share,
					     FSAL_O_WRITE, FSAL_O_CLOSED);
	}

 exit:

	done_cb(obj_hdl, status, write_arg, caller_arg);
}

fsal_status_t
gpfs_fallocate(struct fsal_obj_handle *obj_hdl, state_t *state,
	       uint64_t offset, uint64_t length, bool allocate)
{
	fsal_status_t status, status2;
	struct gpfs_fd *my_fd;
	struct gpfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogMajor(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	/* Indicate a desire to start io and get a usable file descritor */
	status = fsal_start_io(&out_fd, obj_hdl, &myself->u.file.fd.fsal_fd,
			       &temp_fd.fsal_fd, state, FSAL_O_WRITE,
			       false, NULL, false, &myself->u.file.share);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			     "fsal_start_io failed returning %s",
			     fsal_err_txt(status));
		goto exit;
	}

	my_fd = container_of(out_fd, struct gpfs_fd, fsal_fd);

	status = GPFSFSAL_alloc(my_fd->fd, offset, length, allocate);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			 get_handle2inode(myself->handle),
			 fsal_err_txt(status));
	}

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	if (state == NULL) {
		/* We did I/O without a state so we need to release the temp
		 * share reservation acquired.
		 */

		/* Release the share reservation now by updating the counters.
		 */
		update_share_counters_locked(obj_hdl, &myself->u.file.share,
					     FSAL_O_WRITE, FSAL_O_CLOSED);
	}

 exit:

	return status;
}

static fsal_status_t
gpfs_commit_fd(int my_fd, struct gpfs_fsal_obj_handle *myself, off_t offset,
	       size_t len)
{
	struct fsync_arg arg = {0};
	verifier4 writeverf = {0};
	int retval;

	assert(my_fd >= 3);

	arg.mountdirfd = my_fd;
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

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
 * @param[in] obj_hdl          File on which to operate
 * @param[in] state            state_t to use for this operation
 * @param[in] offset           Start of range to commit
 * @param[in] len              Length of range to commit
 *
 * @return FSAL status.
 */

fsal_status_t
gpfs_commit2(struct fsal_obj_handle *obj_hdl, off_t offset, size_t len)
{
	fsal_status_t status, status2;
	struct gpfs_fd *my_fd;
	struct gpfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	/* Make sure file is open in appropriate mode.
	 * Do not check share reservation.
	 */
	status = fsal_start_global_io(&out_fd, obj_hdl,
				      &myself->u.file.fd.fsal_fd,
				      &temp_fd.fsal_fd,
				      FSAL_O_ANY, false,
				      NULL);

	if (FSAL_IS_ERROR(status))
		return status;

	my_fd = container_of(out_fd, struct gpfs_fd, fsal_fd);

	fsal_set_credentials(&op_ctx->creds);

	status = gpfs_commit_fd(my_fd->fd, myself, offset, len);

	fsal_restore_ganesha_credentials();

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			 get_handle2inode(myself->handle),
			 fsal_err_txt(status));
	}

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	/* We did not do share reservation stuff... */

	return status;
}

/**
 * @brief Perform a lock operation
 *
 * This function performs a lock operation (lock, unlock, test) on a
 * file. This method assumes the FSAL is able to support lock owners,
 * though it need not support asynchronous blocking locks. Passing the
 * lock state allows the FSAL to associate information with a specific
 * lock owner for each file (which may include use of a "file descriptor".
 *
 * For FSAL_VFS etc. we ignore owner, implicitly we have a lock_fd per
 * lock owner (i.e. per state).
 *
 * @param[in]  obj_hdl		File on which to operate
 * @param[in]  state		state_t to use for this operation
 * @param[in]  owner		Lock owner
 * @param[in]  lock_op		Operation to perform
 * @param[in]  req_lock		Lock to take/release/test
 * @param[out] conflicting_lock	Conflicting lock
 *
 * @return FSAL status.
 */
fsal_status_t
gpfs_lock_op2(struct fsal_obj_handle *obj_hdl, struct state_t *state,
	      void *owner, fsal_lock_op_t lock_op, fsal_lock_param_t *req_lock,
	      fsal_lock_param_t *conflicting_lock)
{
	struct fsal_export *export = op_ctx->fsal_export;
	struct glock glock_args;
	struct set_get_lock_arg gpfs_sg_arg;
	fsal_openflags_t openflags;
	fsal_status_t status, status2;
	struct gpfs_fd *my_fd;
	struct gpfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	bool bypass = false;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d sle_type:%d type:%d start:%llu length:%llu owner:%p",
		     lock_op, req_lock->lock_sle_type, req_lock->lock_type,
		     (unsigned long long)req_lock->lock_start,
		     (unsigned long long)req_lock->lock_length, owner);

	if (obj_hdl == NULL) {
		LogCrit(COMPONENT_FSAL, "obj_hdl arg is NULL.");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	if (owner == NULL) {
		LogCrit(COMPONENT_FSAL, "owner arg is NULL.");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	if (conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT) {
		LogDebug(COMPONENT_FSAL,
			 "Conflicting_lock argument can't be NULL with lock_op = LOCKT");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	if (lock_op != FSAL_OP_LOCKT && state == NULL) {
		LogCrit(COMPONENT_FSAL, "Non TEST operation with NULL state");
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	/* flock.l_len being signed long integer, larger lock ranges may
	 * get mapped to negative values. As per 'man 3 fcntl', posix
	 * locks can accept negative l_len values which may lead to
	 * unlocking an unintended range. Better bail out to prevent that.
	 */
	if (req_lock->lock_length > LONG_MAX) {
		LogCrit(COMPONENT_FSAL,
			"Requested lock length is out of range- MAX(%lu), req_lock_length(%"
			PRIu64")",
			LONG_MAX, req_lock->lock_length);
		return fsalstat(ERR_FSAL_BAD_RANGE, 0);
	}

	switch (req_lock->lock_type) {
	case FSAL_LOCK_R:
		glock_args.flock.l_type = F_RDLCK;
		openflags = FSAL_O_READ;
		break;
	case FSAL_LOCK_W:
		glock_args.flock.l_type = F_WRLCK;
		openflags = FSAL_O_WRITE;
		break;
	default:
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	switch (lock_op) {
	case FSAL_OP_LOCKT:
		/* We may end up using global fd, don't fail on a deny mode */
		bypass = true;
		glock_args.cmd = F_GETLK;
		openflags = FSAL_O_ANY;
		break;
	case FSAL_OP_UNLOCK:
		glock_args.flock.l_type = F_UNLCK;
		glock_args.cmd = F_SETLK;
		openflags = FSAL_O_ANY;
		break;
	case FSAL_OP_LOCK:
		glock_args.cmd = F_SETLK;
		break;
	case FSAL_OP_LOCKB:
		glock_args.cmd = F_SETLKW;
		break;
	case FSAL_OP_CANCEL:
		glock_args.cmd = GPFS_F_CANCELLK;
		openflags = FSAL_O_ANY;
		break;
	default:
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Lock operation requested was not TEST, GET, or SET.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	/* Indicate a desire to start io and get a usable file descritor */
	status = fsal_start_io(&out_fd, obj_hdl, &myself->u.file.fd.fsal_fd,
			       &temp_fd.fsal_fd, state, openflags,
			       true, NULL, bypass, &myself->u.file.share);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_FSAL,
			"fsal_start_io failed returning %s",
			fsal_err_txt(status));
		goto exit;
	}

	my_fd = container_of(out_fd, struct gpfs_fd, fsal_fd);

	glock_args.lfd = my_fd->fd;
	glock_args.flock.l_len = req_lock->lock_length;
	glock_args.flock.l_start = req_lock->lock_start;
	glock_args.flock.l_whence = SEEK_SET;
	glock_args.lock_owner = owner;

	gpfs_sg_arg.lock = &glock_args;
	gpfs_sg_arg.reclaim = req_lock->lock_reclaim;
	gpfs_sg_arg.mountdirfd = export_fd;
	gpfs_sg_arg.cli_ip = NULL;
	if (op_ctx && op_ctx->client)
		gpfs_sg_arg.cli_ip = op_ctx->client->hostaddr_str;

	status = GPFSFSAL_lock_op(export, lock_op, req_lock, conflicting_lock,
				  &gpfs_sg_arg);


	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			 get_handle2inode(myself->handle),
			 fsal_err_txt(status));
	}

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	if (state == NULL) {
		/* We did I/O without a state so we need to release the temp
		 * share reservation acquired.
		 */

		/* Release the share reservation now by updating the counters.
		 */
		update_share_counters_locked(obj_hdl, &myself->u.file.share,
					     openflags, FSAL_O_CLOSED);
	}

 exit:

	return status;
}

/**
 * @brief Seek to data or hole
 *
 * This function seek to data or hole in a file.
 *
 * @param[in]     obj_hdl   File on which to operate
 * @param[in]     state     state_t to use for this operation
 * @param[in,out] info      Information about the data
 *
 * @return FSAL status.
 */

fsal_status_t gpfs_seek2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state,
			 struct io_info *info)
{
	off_t offset = info->io_content.hole.di_offset;
	fsal_status_t status, status2;
	struct fsal_attrlist attrs;
	struct gpfs_fd *my_fd;
	struct gpfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct gpfs_io_info io_info = {0};
	struct fseek_arg arg = {0};
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

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

	/** @todo: WARNING there's a size check that isn't atomic, Fixing this
	 *         would probably mean taking the object lock, but that would
	 *         not be entirely safe since we no longer use the object lock
	 *         to proect I/O on the object...
	 */

	/* Indicate a desire to start io and get a usable file descritor */
	status = fsal_start_io(&out_fd, obj_hdl, &myself->u.file.fd.fsal_fd,
			       &temp_fd.fsal_fd, state, FSAL_O_ANY,
			       false, NULL, true, NULL);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			     "fsal_start_io failed returning %s",
			     fsal_err_txt(status));
		goto exit;
	}

	my_fd = container_of(out_fd, struct gpfs_fd, fsal_fd);

	arg.mountdirfd = export_fd;
	arg.openfd = my_fd->fd;
	arg.info = &io_info;

	fsal_prepare_attrs(&attrs,
			   (op_ctx->fsal_export->exp_ops.fs_supported_attrs(
							op_ctx->fsal_export)
				& ~(ATTR_ACL | ATTR4_FS_LOCATIONS)));

	status = GPFSFSAL_getattrs(op_ctx->fsal_export,
				   obj_hdl->fs->private_data,
				   myself->handle,
				   &attrs);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			     "GPFSFSAL_getattrs failed returning %s",
			     fsal_err_txt(status));
		goto out;
	}

	/* RFC7862 15.11.3,
	 * If the sa_offset is beyond the end of the file,
	 * then SEEK MUST return NFS4ERR_NXIO. */
	if (offset >= attrs.filesize) {
		status = posix2fsal_status(ENXIO);
		LogFullDebug(COMPONENT_FSAL,
			     "offset >= file size, returning %s",
			     fsal_err_txt(status));
		goto out;
	}

	if (gpfs_ganesha(OPENHANDLE_SEEK_BY_FD, &arg) == -1) {
		if (errno == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		status = posix2fsal_status(errno);
		goto out;
	}

	info->io_eof = io_info.io_eof;
	info->io_content.hole.di_offset = io_info.io_offset;
	info->io_content.hole.di_length = io_info.io_len;

 out:

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	/* We did FSAL_O_ANY so no share reservation was acquired */

 exit:

	return status;
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

	assert(myself->u.file.fd.fd >= 3 &&
	       myself->u.file.fd.fsal_fd.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd.fd;
	arg.openfd = myself->u.file.fd.fd;
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
 *  @brief Close the file if it is still open.
 *
 *  @param obj_hdl FSAL object handle
 *  @return FSAL status
 */
fsal_status_t gpfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	assert(obj_hdl->type == REGULAR_FILE);

	return close_fsal_fd(obj_hdl, &myself->u.file.fd.fsal_fd, false);
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

fsal_status_t
gpfs_close2(struct fsal_obj_handle *obj_hdl, struct state_t *state)
{
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status;
	struct gpfs_fd *my_fd = &container_of(state, struct gpfs_state_fd,
					      state)->gpfs_fd;

	LogFullDebug(COMPONENT_FSAL, "state %p", state);

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	if (state->state_type == STATE_TYPE_SHARE ||
	    state->state_type == STATE_TYPE_NLM_SHARE ||
	    state->state_type == STATE_TYPE_9P_FID) {
		/* This is a share state, we must update the share counters */
		update_share_counters_locked(obj_hdl, &myself->u.file.share,
					     my_fd->fsal_fd.openflags,
					     FSAL_O_CLOSED);
	}

	status = close_fsal_fd(obj_hdl, &my_fd->fsal_fd, false);

	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			 get_handle2inode(gfh), msg_fsal_err(status.major));
	}

	return status;
}

