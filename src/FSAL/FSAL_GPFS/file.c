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
gpfs_open_again(struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags,
		bool reopen)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fsal_status_t status;
	int posix_flags = 0;
	int fd = -1;

	if (reopen) {
		assert(myself->u.file.fd.fd >= 0 &&
		       myself->u.file.fd.openflags != FSAL_O_CLOSED);
		fd = myself->u.file.fd.fd;
	} else {
		assert(myself->u.file.fd.fd == -1 &&
		       myself->u.file.fd.openflags == FSAL_O_CLOSED);
	}
	fsal2posix_openflags(openflags, &posix_flags);

	status = GPFSFSAL_open(obj_hdl, op_ctx, posix_flags, &fd, reopen);
	if (FSAL_IS_ERROR(status) == false) {
		myself->u.file.fd.fd = fd;
		myself->u.file.fd.openflags = openflags;
	}

	return status;
}

static fsal_status_t
gpfs_open_func(struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags,
		struct fsal_fd *fd)
{
	fsal_status_t status;
	struct gpfs_fd *my_fd = (struct gpfs_fd *)fd;
	int posix_flags = 0;

	my_fd->fd = -1;
	fsal2posix_openflags(openflags, &posix_flags);

	status = GPFSFSAL_open(obj_hdl, op_ctx, posix_flags, &my_fd->fd, false);
	if (FSAL_IS_ERROR(status) == false)
		my_fd->openflags = openflags;

	LogFullDebug(COMPONENT_FSAL, "new fd %d", my_fd->fd);

	return status;
}

static fsal_status_t
gpfs_close_func(struct fsal_obj_handle *obj_hdl, struct fsal_fd *fd)
{
	fsal_status_t status;
	struct gpfs_fd *my_fd = (struct gpfs_fd *)fd;

	status = fsal_internal_close(my_fd->fd, NULL, 0);
	my_fd->fd = -1;

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
	return gpfs_open_again(obj_hdl, openflags, false);
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
	return gpfs_open_again(obj_hdl, openflags, true);
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

	return myself->u.file.fd.openflags;
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
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrib_set            Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] attrs_out         Newly created object attributes
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */
fsal_status_t gpfs_open2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state,
			 fsal_openflags_t openflags,
			 enum fsal_create_mode createmode,
			 const char *name,
			 struct attrlist *attrib_set,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **new_obj,
			 struct attrlist *attrs_out,
			 bool *caller_perm_check)
{
	int posix_flags = 0;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	struct gpfs_fd *my_fd = NULL;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fsal_obj_handle *hdl = NULL;
	struct gpfs_file_handle fh;
	bool truncated;
	bool created = false;
	struct fsal_export *export = op_ctx->fsal_export;
	struct gpfs_filesystem *gpfs_fs = obj_hdl->fs->private_data;
	int *fd = NULL;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	if (name)
		LogFullDebug(COMPONENT_FSAL, "got name %s", name);
	else
		LogFullDebug(COMPONENT_FSAL, "no name");

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
		    "attrs ", attrib_set, false);

	if (state != NULL)
		my_fd = (struct gpfs_fd *)(state + 1);

	fsal2posix_openflags(openflags, &posix_flags);

	truncated = (posix_flags & O_TRUNC) != 0;

	if (createmode >= FSAL_EXCLUSIVE) {
		/* Now fixup attrs for verifier if exclusive create */
		set_common_verifier(attrib_set, verifier);
	}

	if (name == NULL) {
		/* This is an open by handle */
		if (state != NULL) {
		       /* Prepare to take the share reservation, but only if we
			* are called with a valid state (if state is NULL the
			* caller is a stateless create such as NFS v3 CREATE).
			*/

			/* This can block over an I/O operation. */
			PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

			/* Check share reservation conflicts. */
			status = check_share_conflict(&myself->u.file.share,
						      openflags,
						      false);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
				return status;
			}

			/* Take the share reservation now by updating the
			 * counters.
			 */
			update_share_counters(&myself->u.file.share,
					      FSAL_O_CLOSED,
					      openflags);

			PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

			fd = &my_fd->fd;
			my_fd->openflags = openflags;
		} else {
			/* We need to use the global fd to continue, and take
			 * the lock to protect it.
			 */
			fd = &myself->u.file.fd.fd;
			myself->u.file.fd.openflags = openflags;
			PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);
		}
		status = GPFSFSAL_open(obj_hdl, op_ctx, posix_flags, fd, false);
		if (FSAL_IS_ERROR(status)) {
			if (state == NULL) {
				/* Release the lock taken above, and return
				 * since there is nothing to undo.
				 */
				PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
				return status;
			}
			/* Error - need to release the share */
			goto undo_share;
		}
		if (attrs_out && (createmode >= FSAL_EXCLUSIVE || truncated)) {
			/* Refresh the attributes */
			status = GPFSFSAL_getattrs(export, gpfs_fs, op_ctx,
						   myself->handle, attrs_out);
			if (!FSAL_IS_ERROR(status)) {
				LogFullDebug(COMPONENT_FSAL,
					     "New size = %"PRIx64,
					     attrs_out->filesize);
			}
			/* Now check verifier for exclusive, but not for
			 * FSAL_EXCLUSIVE_9P.
			 */
			if (!FSAL_IS_ERROR(status) &&
			    createmode >= FSAL_EXCLUSIVE &&
			    createmode != FSAL_EXCLUSIVE_9P &&
			    !check_verifier_attrlist(attrs_out, verifier)) {
				/* Verifier didn't match, return EEXIST */
				status =
				    fsalstat(posix2fsal_error(EEXIST), EEXIST);
			}
		}
		if (state == NULL) {
			/* If no state, release the lock taken above and return
			 * status.
			 */
			PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
			return status;
		}
		if (!FSAL_IS_ERROR(status)) {
			/* Return success. */
			return status;
		}
		(void) fsal_internal_close(*fd, state->state_owner, 0);

 undo_share:

		/* Can only get here with state not NULL and an error */

		/* On error we need to release our share reservation
		 * and undo the update of the share counters.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

		update_share_counters(&myself->u.file.share,
				      openflags,
				      FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

		return status;
	}
	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 * indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

	if (createmode == FSAL_NO_CREATE) {
		/* Non creation case, libgpfs doesn't have open by name so we
		 * have to do a lookup and then handle as an open by handle.
		 */
		struct fsal_obj_handle *temp = NULL;

		/* We don't have open by name... */
		status = obj_hdl->obj_ops.lookup(obj_hdl, name, &temp, NULL);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "lookup returned %s",
				     fsal_err_txt(status));
			return status;
		}

		/* Now call ourselves without name and attributes to open. */
		status = obj_hdl->obj_ops.open2(temp, state, openflags,
						FSAL_NO_CREATE, NULL, NULL,
						verifier, new_obj,
						attrs_out,
						caller_perm_check);

		if (FSAL_IS_ERROR(status)) {
			/* Release the object we found by lookup. */
			temp->obj_ops.release(temp);
			LogFullDebug(COMPONENT_FSAL,
				     "open returned %s",
				     fsal_err_txt(status));
		} else {
			/* No permission check was actually done... */
			*caller_perm_check = true;
		}
		return status;
	}

	/* Only proceed here if this is a create */

	/* Now add in O_CREAT and O_EXCL. */
	posix_flags |= O_CREAT;

	/* And if we are at least FSAL_GUARDED, do an O_EXCL create. */
	if (createmode >= FSAL_GUARDED)
		posix_flags |= O_EXCL;

	/* Fetch the mode attribute to use in the openat system call. */
	unix_mode = fsal2unix_mode(attrib_set->mode) &
	    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	/* Don't set the mode if we later set the attributes */
	FSAL_UNSET_MASK(attrib_set->mask, ATTR_MODE);

	if (createmode == FSAL_UNCHECKED && (attrib_set->mask != 0)) {
		/* If we have FSAL_UNCHECKED and want to set more attributes
		 * than the mode, we attempt an O_EXCL create first, if that
		 * succeeds, then we will be allowed to set the additional
		 * attributes, otherwise, we don't know we created the file
		 * and this can NOT set the attributes.
		 */
		posix_flags |= O_EXCL;
	}

	status = GPFSFSAL_create2(obj_hdl, name, op_ctx, unix_mode, &fh,
				  posix_flags, attrs_out);

	if (status.major == ERR_FSAL_EXIST && createmode == FSAL_UNCHECKED) {
		/* We tried to create O_EXCL to set attributes and failed.
		 * Remove O_EXCL and retry, also remember not to set attributes.
		 * We still try O_CREAT again just in case file disappears out
		 * from under us.
		 *
		 * Note that because we have dropped O_EXCL, later on we will
		 * not assume we created the file, and thus will not set
		 * additional attributes. We don't need to separately track
		 * the condition of not wanting to set attributes.
		 */
		posix_flags &= ~O_EXCL;
		status = GPFSFSAL_create2(obj_hdl, name, op_ctx, unix_mode, &fh,
					  posix_flags, attrs_out);
		if (FSAL_IS_ERROR(status))
			return status;
	}

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

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(&fh, obj_hdl->fs, attrs_out,
				NULL, op_ctx->fsal_export);
	if (hdl == NULL) {
		status = fsalstat(posix2fsal_error(ENOMEM), ENOMEM);
		goto fileerr;
	}

	*new_obj = &hdl->obj_handle;

	if (created && attrib_set->mask != 0) {
		/* Set attributes using our newly opened file descriptor as the
		 * share_fd if there are any left to set (mode and truncate
		 * have already been handled).
		 *
		 * Note that we only set the attributes if we were responsible
		 * for creating the file.
		 */
		status = (*new_obj)->obj_ops.setattr2(*new_obj,
						      false,
						      state,
						      attrib_set);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			(*new_obj)->obj_ops.release(*new_obj);
			*new_obj = NULL;
			goto fileerr;
		}
		if (attrs_out != NULL) {
			status = (*new_obj)->obj_ops.getattrs(*new_obj,
							      attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes. Otherwise continue
				 * with attrs_out indicating ATTR_RDATTR_ERR.
				 */
				goto fileerr;
			}
		}
	} else if (attrs_out != NULL) {
		/* Make sure ATTR_RDATTR_ERR is cleared on success. */
		attrs_out->mask &= ~ATTR_RDATTR_ERR;
	}
	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is
		 * a stateless create such as NFS v3 CREATE).
		 */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&(*new_obj)->lock);

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&hdl->u.file.share,
				      FSAL_O_CLOSED,
				      openflags);

		PTHREAD_RWLOCK_unlock(&(*new_obj)->lock);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:

	/* Close the file we just opened. */
	if (fd)
		(void) fsal_internal_close(*fd,
					   state?state->state_owner:NULL, 0);

	if (created) {
		/* Remove the file we just created */
		status = GPFSFSAL_unlink(obj_hdl, name, op_ctx);
	}
	return status;
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

	assert(myself->u.file.fd.fd >= 0 &&
	       myself->u.file.fd.openflags != FSAL_O_CLOSED);

	status = GPFSFSAL_read(myself->u.file.fd.fd, offset, buffer_size,
			       buffer, read_amount, end_of_file);

	if (FSAL_IS_ERROR(status))
		return status;

	return status;
}

/**
 *  @brief GPFS read plus
 *
 *  @param obj_hdl FSAL object handle / or fd
 *  @param offset Offset
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
		  bool *end_of_file, struct io_info *info)
{
	const fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct read_arg rarg = {0};
	ssize_t nb_read;
	int errsv;

	if (!buffer || !read_amount || !end_of_file || !info)
		return fsalstat(ERR_FSAL_FAULT, 0);

	assert(my_fd >= 0);

	rarg.mountdirfd = my_fd;
	rarg.fd = my_fd;
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

fsal_status_t
gpfs_read_plus(struct fsal_obj_handle *obj_hdl, uint64_t offset,
	       size_t buffer_size, void *buffer, size_t *read_amount,
	       bool *end_of_file, struct io_info *info)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	int my_fd;

	if (!buffer || !read_amount || !end_of_file || !info)
		return fsalstat(ERR_FSAL_FAULT, 0);

	my_fd = myself->u.file.fd.fd;
	assert(my_fd >= 0 &&
		myself->u.file.fd.openflags != FSAL_O_CLOSED);

	status = gpfs_read_plus_fd(my_fd, offset,  buffer_size,
				   buffer, read_amount,
				   end_of_file, info);
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

fsal_status_t gpfs_reopen2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state,
			  fsal_openflags_t openflags)
{
	struct gpfs_fd fd, *my_fd = &fd, *my_share_fd;
	struct gpfs_fsal_obj_handle *myself;
	fsal_status_t status = {0, 0};
	fsal_openflags_t old_openflags;
	int posix_flags = 0;

	my_share_fd = (struct gpfs_fd *)(state + 1);

	memset(my_fd, 0, sizeof(*my_fd));
	fd.fd = -1;

	myself  = container_of(obj_hdl,
			       struct gpfs_fsal_obj_handle,
			       obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

	old_openflags = my_share_fd->openflags;

	/* We can conflict with old share, so go ahead and check now. */
	status = check_share_conflict(&myself->u.file.share, openflags, false);

	if (FSAL_IS_ERROR(status)) {
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

		return status;
	}
	/* Set up the new share so we can drop the lock and not have a
	 * conflicting share be asserted, updating the share counters.
	 */
	update_share_counters(&myself->u.file.share, old_openflags, openflags);

	PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	fsal2posix_openflags(openflags, &posix_flags);

	status = GPFSFSAL_open(obj_hdl, op_ctx, posix_flags, &fd.fd, false);
	if (!FSAL_IS_ERROR(status)) {
		/* Close the existing file descriptor and copy the new
		 * one over.
		 */
		fsal_internal_close(my_share_fd->fd, NULL, 0);
		my_share_fd->fd = fd.fd;
		my_share_fd->openflags = openflags;
	} else {
		/* We had a failure on open - we need to revert the share.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

		update_share_counters(&myself->u.file.share,
				      openflags,
				      old_openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
	}
	return status;
}

fsal_status_t find_fd(int *fd,
		      struct fsal_obj_handle *obj_hdl,
		      bool bypass,
		      struct state_t *state,
		      fsal_openflags_t openflags,
		      bool *has_lock,
		      bool *need_fsync,
		      bool *closefd,
		      bool open_for_locks)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fd temp_fd = {0, -1}, *out_fd = &temp_fd;
	int posix_flags;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	fsal2posix_openflags(openflags, &posix_flags);

	LogFullDebug(COMPONENT_FSAL, "openflags 0x%X posix_flags 0x%X",
			openflags, posix_flags);

	/* Handle nom-regular files */
	switch (obj_hdl->type) {
	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
		status = gpfs_open_func(obj_hdl, openflags,
					(struct fsal_fd *)out_fd);
		if (FSAL_IS_ERROR(status)) {
			LogDebug(COMPONENT_FSAL,
				 "Failed with openflags 0x%08x",
				  openflags);
			return status;
		}
		*fd = out_fd->fd;
		*closefd = true;
		return status;

	case REGULAR_FILE:
		status = fsal_find_fd((struct fsal_fd **)&out_fd, obj_hdl,
				      (struct fsal_fd *)&myself->u.file.fd,
				      &myself->u.file.share,
				      bypass, state, openflags,
				      gpfs_open_func, gpfs_close_func,
				      has_lock, need_fsync,
				      closefd, open_for_locks);

		*fd = out_fd->fd;
		return status;

	case SYMBOLIC_LINK:
	case FIFO_FILE:
	case DIRECTORY:
		break;

	case NO_FILE_TYPE:
	case EXTENDED_ATTR:
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	/* Open file descriptor for non-regular files. */
	status = gpfs_open_func(obj_hdl, openflags, (struct fsal_fd *)out_fd);
	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "Failed with openflags 0x%08x",
			  openflags);
		return status;
	}
	LogFullDebug(COMPONENT_FSAL,
		     "Opened fd=%d for file of type %s",
		     out_fd->fd, object_file_type_to_str(obj_hdl->type));

	*fd = out_fd->fd;
	*closefd = true;

	return status;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any deny read
 * @param[in]     state          state_t to use for this operation
 * @param[in]     offset         Position from which to read
 * @param[in]     buffer_size    Amount of data to read
 * @param[out]    buffer         Buffer to which data are to be copied
 * @param[out]    read_amount    Amount of data read
 * @param[out]    end_of_file    true if the end of file has been reached
 * @param[in,out] info           more information about the data
 *
 * @return FSAL status.
 */

fsal_status_t gpfs_read2(struct fsal_obj_handle *obj_hdl,
			bool bypass,
			struct state_t *state,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *read_amount,
			bool *end_of_file,
			struct io_info *info)
{
	int my_fd = -1;
	fsal_status_t status;
	bool has_lock = false;
	bool need_fsync = false;
	bool closefd = false;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, bypass, state, FSAL_O_READ,
			 &has_lock, &need_fsync, &closefd, false);

	if (FSAL_IS_ERROR(status))
		goto out;

	if (info)
		status = gpfs_read_plus_fd(my_fd, offset, buffer_size,
					buffer, read_amount, end_of_file, info);
	else
		status = GPFSFSAL_read(my_fd, offset, buffer_size, buffer,
					read_amount, end_of_file);

 out:

	if (closefd)
		status = fsal_internal_close(my_fd, NULL, 0);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

	return status;
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
 * @param[in]     state          state_t to use for this operation
 * @param[in]     offset         Position at which to write
 * @param[in]     buffer         Data to be written
 * @param[in,out] fsal_stable    In, if on, the fsal is requested to write data
 *                               to stable store. Out, the fsal reports what
 *                               it did.
 * @param[in,out] info           more information about the data
 *
 * @return FSAL status.
 */

fsal_status_t gpfs_write2(struct fsal_obj_handle *obj_hdl,
			 bool bypass,
			 struct state_t *state,
			 uint64_t offset,
			 size_t buffer_size,
			 void *buffer,
			 size_t *wrote_amount,
			 bool *fsal_stable,
			 struct io_info *info)
{
	fsal_status_t status;
	int retval = 0;
	int my_fd = -1;
	bool has_lock = false;
	bool need_fsync = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	if (*fsal_stable)
		openflags |= FSAL_O_SYNC;

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			 &has_lock, &need_fsync, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		goto out;
	}
	if (info)
		status = gpfs_write_plus_fd(my_fd, offset,
				buffer_size, buffer, wrote_amount,
				fsal_stable, info);
	else
		status = GPFSFSAL_write(my_fd, offset, buffer_size, buffer,
				wrote_amount, fsal_stable, op_ctx);


	if (FSAL_IS_ERROR(status))
		goto out;

	/* attempt stability if we aren't using an O_SYNC fd */
	if (need_fsync) {
		retval = fsync(my_fd);
		if (retval == -1) {
			retval = errno;
			status = fsalstat(posix2fsal_error(retval), retval);
		}
	}

 out:

	if (closefd)
		fsal_internal_close(my_fd, NULL, 0);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

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
 * @param[in] obj_hdl          File on which to operate
 * @param[in] state            state_t to use for this operation
 * @param[in] offset           Start of range to commit
 * @param[in] len              Length of range to commit
 *
 * @return FSAL status.
 */

fsal_status_t gpfs_commit2(struct fsal_obj_handle *obj_hdl,
			  off_t offset,
			  size_t len)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fd temp_fd = {0, -1}, *out_fd = &temp_fd;
	bool has_lock = false;
	bool closefd = false;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	/* Make sure file is open in appropriate mode.
	 * Do not check share reservation.
	 */
	status = fsal_reopen_obj(obj_hdl, false, false, FSAL_O_WRITE,
				 (struct fsal_fd *)&myself->u.file.fd,
				 &myself->u.file.share,
				 gpfs_open_func, gpfs_close_func,
				 (struct fsal_fd **)&out_fd, &has_lock,
				 &closefd);

	if (!FSAL_IS_ERROR(status)) {

		fsal_set_credentials(op_ctx->creds);

		status = gpfs_commit_fd(out_fd->fd, obj_hdl, offset, len);

		fsal_restore_ganesha_credentials();
	}
	if (closefd)
		fsal_internal_close(out_fd->fd, NULL, 0);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

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
 * @param[in]  obj_hdl          File on which to operate
 * @param[in]  state            state_t to use for this operation
 * @param[in]  owner            Lock owner
 * @param[in]  lock_op          Operation to perform
 * @param[in]  request_lock     Lock to take/release/test
 * @param[out] conflicting_lock Conflicting lock
 *
 * @return FSAL status.
 */
fsal_status_t gpfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   void *owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock)
{
	struct flock lock_args;
	fsal_status_t status = {0, 0};
	int my_fd = -1;
	bool has_lock = false;
	bool need_fsync = false;
	bool closefd = false;
	bool bypass = false;
	fsal_openflags_t openflags = FSAL_O_RDWR;
	struct fsal_export *export = op_ctx->fsal_export;

	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d start:%" PRIu64 " length:%"
		     PRIu64 " ",
		     lock_op, request_lock->lock_type, request_lock->lock_start,
		     request_lock->lock_length);

	if (lock_op == FSAL_OP_LOCKT) {
		/* We may end up using global fd, don't fail on a deny mode */
		bypass = true;
		openflags = FSAL_O_ANY;
	} else if (lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_LOCKB) {
		if (request_lock->lock_type == FSAL_LOCK_R)
			openflags = FSAL_O_READ;
		else if (request_lock->lock_type == FSAL_LOCK_W)
			openflags = FSAL_O_WRITE;
	} else if (lock_op == FSAL_OP_UNLOCK || lock_op == FSAL_OP_CANCEL) {
		openflags = FSAL_O_ANY;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Lock operation requested was not TEST, READ, or WRITE.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (lock_op != FSAL_OP_LOCKT && state == NULL) {
		LogCrit(COMPONENT_FSAL, "Non TEST operation with NULL state");
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	if (request_lock->lock_type == FSAL_LOCK_R) {
		lock_args.l_type = F_RDLCK;
	} else if (request_lock->lock_type == FSAL_LOCK_W) {
		lock_args.l_type = F_WRLCK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (lock_op == FSAL_OP_UNLOCK)
		lock_args.l_type = F_UNLCK;

	lock_args.l_pid = 0;
	lock_args.l_len = request_lock->lock_length;
	lock_args.l_start = request_lock->lock_start;
	lock_args.l_whence = SEEK_SET;

	/* flock.l_len being signed long integer, larger lock ranges may
	 * get mapped to negative values. As per 'man 3 fcntl', posix
	 * locks can accept negative l_len values which may lead to
	 * unlocking an unintended range. Better bail out to prevent that.
	 */
	if (lock_args.l_len < 0) {
		LogCrit(COMPONENT_FSAL,
			"The requested lock length is out of range- lock_args.l_len(%ld), request_lock_length(%"
			PRIu64 ")",
			lock_args.l_len, request_lock->lock_length);
		return fsalstat(ERR_FSAL_BAD_RANGE, 0);
	}

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			      &has_lock, &need_fsync, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_FSAL, "Unable to find fd for lock operation");
		return status;
	}

	status = GPFSFSAL_lock_op2(my_fd, export, obj_hdl, owner, lock_op,
				   request_lock, conflicting_lock);

	if (closefd)
		status = fsal_internal_close(my_fd, NULL, 0);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

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

	assert(myself->u.file.fd.fd >= 0 &&
	       myself->u.file.fd.openflags != FSAL_O_CLOSED);

	return GPFSFSAL_write(myself->u.file.fd.fd, offset, buffer_size, buffer,
			      write_amount, fsal_stable, op_ctx);
}

fsal_status_t
gpfs_write_fd(int my_fd, struct fsal_obj_handle *obj_hdl, uint64_t offset,
	      size_t buffer_size, void *buffer, size_t *write_amount,
	      bool *fsal_stable)
{
	assert(my_fd >= 0);

	return GPFSFSAL_write(my_fd, offset, buffer_size, buffer,
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

	assert(myself->u.file.fd.fd >= 0 &&
	       myself->u.file.fd.openflags != FSAL_O_CLOSED);

	return GPFSFSAL_alloc(myself->u.file.fd.fd, offset, length, false);
}

static fsal_status_t
gpfs_deallocate_fd(int my_fd,
	uint64_t offset, uint64_t length)
{
	assert(my_fd >= 0);

	return GPFSFSAL_alloc(my_fd, offset, length, false);
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

	assert(myself->u.file.fd.fd >= 0 &&
	       myself->u.file.fd.openflags != FSAL_O_CLOSED);

	return GPFSFSAL_alloc(myself->u.file.fd.fd, offset, length, true);
}

static fsal_status_t
gpfs_allocate_fd(int my_fd, uint64_t offset, uint64_t length)
{
	assert(my_fd >= 0);

	return GPFSFSAL_alloc(my_fd, offset, length, true);
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

fsal_status_t
gpfs_write_plus_fd(int my_fd, uint64_t offset,
		   size_t buffer_size, void *buffer, size_t *write_amount,
		   bool *fsal_stable, struct io_info *info)
{
	switch (info->io_content.what) {
	case NFS4_CONTENT_DATA:
		return GPFSFSAL_write(my_fd, offset, buffer_size, buffer,
				     write_amount, fsal_stable, op_ctx);
	case NFS4_CONTENT_DEALLOCATE:
		return gpfs_deallocate_fd(my_fd, offset, buffer_size);
	case NFS4_CONTENT_ALLOCATE:
		return gpfs_allocate_fd(my_fd, offset, buffer_size);
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

	assert(myself->u.file.fd.fd >= 0 &&
	       myself->u.file.fd.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd.fd;
	arg.openfd = myself->u.file.fd.fd;
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

	assert(myself->u.file.fd.fd >= 0 &&
	       myself->u.file.fd.openflags != FSAL_O_CLOSED);

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

	assert(myself->u.file.fd.fd >= 0 &&
	       myself->u.file.fd.openflags != FSAL_O_CLOSED);

	arg.mountdirfd = myself->u.file.fd.fd;
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

fsal_status_t
gpfs_commit_fd(int my_fd, struct fsal_obj_handle *obj_hdl,
		off_t offset, size_t len)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	struct fsync_arg arg = {0};
	verifier4 writeverf = {0};
	int retval;

	assert(my_fd >= 0);

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

	if (myself->u.file.fd.fd < 0 ||
	    myself->u.file.fd.openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "Attempting to lock with no file descriptor open, fd %d",
			 myself->u.file.fd.fd);
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

	if (myself->u.file.fd.fd >= 0 &&
	    myself->u.file.fd.openflags != FSAL_O_CLOSED) {
		status = fsal_internal_close(myself->u.file.fd.fd, NULL, 0);
		myself->u.file.fd.fd = -1;
		myself->u.file.fd.openflags = FSAL_O_CLOSED;
	}

	return status;
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

fsal_status_t gpfs_close2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state)
{
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fd *my_fd = (struct gpfs_fd *)(state + 1);
	state_owner_t *state_owner = NULL;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	LogFullDebug(COMPONENT_FSAL, "state %p", state);

	myself  = container_of(obj_hdl,
			       struct gpfs_fsal_obj_handle,
			       obj_handle);

	if (state->state_type == STATE_TYPE_SHARE ||
	    state->state_type == STATE_TYPE_NLM_SHARE ||
	    state->state_type == STATE_TYPE_9P_FID) {
		/* This is a share state, we must update the share counters */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

		update_share_counters(&myself->u.file.share,
				      my_fd->openflags,
				      FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
	}
	if (my_fd->fd > 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "state %p fd %d", state, my_fd->fd);
		state_owner = state->state_owner;

		return fsal_internal_close(my_fd->fd, state_owner, 0);
	}
	return status;
}


/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */
struct state_t *gpfs_alloc_state(struct fsal_export *exp_hdl,
				enum state_type state_type,
				struct state_t *related_state)
{
	struct state_t *state;
	struct gpfs_fd *my_fd;

	state = init_state(gsh_calloc(1, sizeof(struct state_t)
					+ sizeof(struct gpfs_fd)),
			   exp_hdl, state_type, related_state);

	my_fd = (struct gpfs_fd *)(state + 1);

	my_fd->fd = -1;

	return state;
}
