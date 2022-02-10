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

static fsal_status_t
gpfs_open_func(struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags,
		struct fsal_fd *fd)
{
	fsal_status_t status;
	struct gpfs_fd *my_fd = (struct gpfs_fd *)fd;
	int posix_flags = 0;

	fsal2posix_openflags(openflags, &posix_flags);

	status = GPFSFSAL_open(obj_hdl, posix_flags, &my_fd->fd);
	if (FSAL_IS_ERROR(status))
		return status;

	my_fd->openflags = FSAL_O_NFS_FLAGS(openflags);
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
	my_fd->openflags = FSAL_O_CLOSED;

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
	       fsal_openflags_t openflags, int posix_flags,
	       fsal_verifier_t verifier, struct fsal_attrlist *attrs_out,
	       enum fsal_create_mode createmode, bool *cpm_check)
{
	struct fsal_export *export = op_ctx->fsal_export;
	struct gpfs_fsal_obj_handle *gpfs_hdl;
	struct gpfs_filesystem *gpfs_fs = obj_hdl->fs->private_data;
	fsal_status_t status;
	const bool truncated = (posix_flags & O_TRUNC) != 0;
	struct gpfs_fd *my_fd;
	int fd = -1;

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	gpfs_hdl = container_of(obj_hdl, struct gpfs_fsal_obj_handle,
				obj_handle);

	if (state != NULL) {
		my_fd = &container_of(state,
				      struct gpfs_state_fd,
				      state)->gpfs_fd;

	       /* Prepare to take the share reservation, but only if we
		* are called with a valid state (if state is NULL the
		* caller is a stateless create such as NFS v3 CREATE).
		*/

		/* Check share reservation conflicts. */
		status = check_share_conflict(&gpfs_hdl->u.file.share,
					      openflags, false);

		if (FSAL_IS_ERROR(status))
			goto out;

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&gpfs_hdl->u.file.share, FSAL_O_CLOSED,
				      openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	} else {
		/* We need to use the global fd to continue. */
		my_fd = &gpfs_hdl->u.file.fd;
	}

	status = GPFSFSAL_open(obj_hdl, posix_flags, &fd);

	if (FSAL_IS_ERROR(status)) {
		if (state == NULL)
			goto out;
		else
			goto undo_share;
	}

	/* Close any old open file descriptor and update with the new
	 * one. There shouldn't be any old open for state based call.
	 */
	if (my_fd->openflags != FSAL_O_CLOSED) {
		assert(my_fd->fd >= 3);
		/* assert(state == NULL); */
		(void)fsal_internal_close(my_fd->fd, NULL, 0);
	}

	my_fd->fd = fd;
	my_fd->openflags = FSAL_O_NFS_FLAGS(openflags);

	if (attrs_out && (createmode >= FSAL_EXCLUSIVE || truncated)) {
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

	(void) fsal_internal_close(my_fd->fd, state->state_owner, 0);
	my_fd->fd = -1;
	my_fd->openflags = FSAL_O_CLOSED;

 undo_share:
	/* On error we need to release our share reservation
	 * and undo the update of the share counters.
	 * This can block over an I/O operation.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	update_share_counters(&gpfs_hdl->u.file.share, openflags,
			      FSAL_O_CLOSED);
 out:
	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

static fsal_status_t
open_by_name(struct fsal_obj_handle *obj_hdl, struct state_t *state,
	     const char *name, fsal_openflags_t openflags, int posix_flags,
	     fsal_verifier_t verifier, struct fsal_attrlist *attrs_out,
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

	status = open_by_handle(temp, state, openflags, posix_flags,
				verifier, attrs_out, FSAL_NO_CREATE, cpm_check);

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
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attr_set              Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] attrs_out         Newly created object attributes
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */
fsal_status_t
gpfs_open2(struct fsal_obj_handle *obj_hdl, struct state_t *state,
	   fsal_openflags_t openflags, enum fsal_create_mode createmode,
	   const char *name, struct fsal_attrlist *attr_set,
	   fsal_verifier_t verifier, struct fsal_obj_handle **new_obj,
	   struct fsal_attrlist *attrs_out, bool *caller_perm_check)
{
	struct gpfs_fsal_obj_handle *hdl = NULL;
	struct fsal_export *export = op_ctx->fsal_export;
	struct gpfs_file_handle fh;
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
		return open_by_handle(obj_hdl, state, openflags, posix_flags,
				      verifier, attrs_out, createmode,
				      caller_perm_check);

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
	if (createmode == FSAL_NO_CREATE)
		return open_by_name(obj_hdl, state, name, openflags,
				    posix_flags, verifier, attrs_out,
				    caller_perm_check);

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

	/* Restore posix_flags as it was modified for create above */
	fsal2posix_openflags(openflags, &posix_flags);
	/* We created a file with the caller's credentials active, so as such
	 * permission check was done. So we don't need the caller to do
	 * permission check again (for that we have already set
	 * *caller_perm_check=false). Passing ignore_perm_check to
	 * open_by_handle() as we don't want to modify the value at
	 * caller_perm_check.
	 */
	return open_by_handle(&hdl->obj_handle, state, openflags, posix_flags,
			verifier, attrs_out, createmode,
			&ignore_perm_check);

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
	struct gpfs_fd *my_share_fd = &container_of(state, struct gpfs_state_fd,
						    state)->gpfs_fd;
	fsal_status_t status;
	int posix_flags = 0;
	int my_fd = -1;
	struct fsal_share *share = &container_of(obj_hdl,
						 struct gpfs_fsal_obj_handle,
						 obj_handle)->u.file.share;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

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

	status = GPFSFSAL_open(obj_hdl, posix_flags, &my_fd);

	if (!FSAL_IS_ERROR(status)) {
		/* Close the existing file descriptor and copy the new
		 * one over. Make sure no one is using the fd that we are
		 * about to close!
		 */
		PTHREAD_RWLOCK_wrlock(&my_share_fd->fdlock);

		fsal_internal_close(my_share_fd->fd, NULL, 0);

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

	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			get_handle2inode(gfh), msg_fsal_err(status.major));
	}
	return status;
}

fsal_status_t
find_fd(int *fd, struct fsal_obj_handle *obj_hdl, bool bypass,
	struct state_t *state, fsal_openflags_t openflags, bool *has_lock,
	bool *closefd, bool open_for_locks)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fd temp_fd = {
			FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, -1 };
	struct gpfs_fd *out_fd = &temp_fd;
	int posix_flags;
	bool reusing_open_state_fd = false;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	fsal2posix_openflags(openflags, &posix_flags);

	LogFullDebug(COMPONENT_FSAL, "openflags 0x%X posix_flags 0x%X",
			openflags, posix_flags);

	switch (obj_hdl->type) {
	case REGULAR_FILE:
		status = fsal_find_fd((struct fsal_fd **)&out_fd, obj_hdl,
				      (struct fsal_fd *)&myself->u.file.fd,
				      &myself->u.file.share,
				      bypass, state, openflags,
				      gpfs_open_func, gpfs_close_func,
				      has_lock, closefd, open_for_locks,
				      &reusing_open_state_fd);

		if (FSAL_IS_SUCCESS(status)) {
			*fd = out_fd->fd;
			assert(*fd >= 3);
		}
		return status;

	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
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
	int my_fd = -1;
	fsal_status_t status;
	bool has_lock = false;
	bool closefd = false;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;
	struct gpfs_fd *gpfs_fd = NULL;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		done_cb(obj_hdl, fsalstat(posix2fsal_error(EXDEV), EXDEV),
			read_arg, caller_arg);
		return;
	}

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (read_arg->state) {
		gpfs_fd = STATE2FD(read_arg->state);
		PTHREAD_RWLOCK_rdlock(&gpfs_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, bypass, read_arg->state, FSAL_O_READ,
			 &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		if (gpfs_fd)
			PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);
		done_cb(obj_hdl, status, read_arg, caller_arg);
		return;
	}

	assert(read_arg->iov_count == 1);

	if (read_arg->info)
		status = gpfs_read_plus_fd(my_fd, read_arg->offset,
					   read_arg->iov[0].iov_len,
					   read_arg->iov[0].iov_base,
					   &read_arg->io_amount,
					   &read_arg->end_of_file,
					   read_arg->info,
					   export_fd);
	else
		status = GPFSFSAL_read(my_fd, read_arg->offset,
				       read_arg->iov[0].iov_len,
				       read_arg->iov[0].iov_base,
				       &read_arg->io_amount,
				       &read_arg->end_of_file, export_fd);

	if (gpfs_fd)
		PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);

	if (closefd) {
		fsal_status_t status2;

		status2 = fsal_internal_close(my_fd, NULL, 0);
		if (FSAL_IS_ERROR(status2)) {
			LogEvent(COMPONENT_FSAL,
				 "fsal close failed, fd:%d, error: %s",
				 my_fd, msg_fsal_err(status2.major));
		}
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			get_handle2inode(gfh), msg_fsal_err(status.major));
	}

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
	fsal_status_t status;
	int my_fd = -1;
	bool has_lock = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;
	struct gpfs_fd *gpfs_fd = NULL;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		done_cb(obj_hdl, fsalstat(posix2fsal_error(EXDEV), EXDEV),
			write_arg, caller_arg);
		return;
	}

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (write_arg->state) {
		gpfs_fd = STATE2FD(write_arg->state);
		PTHREAD_RWLOCK_rdlock(&gpfs_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, bypass, write_arg->state, openflags,
			 &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		if (gpfs_fd)
			PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);
		done_cb(obj_hdl, status, write_arg, caller_arg);
		return;
	}

	status = GPFSFSAL_write(my_fd, write_arg->offset,
				write_arg->iov[0].iov_len,
				write_arg->iov[0].iov_base,
				&write_arg->io_amount,
				&write_arg->fsal_stable,
				export_fd);

	if (gpfs_fd)
		PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);

	if (closefd) {
		fsal_status_t status2;

		status2 = fsal_internal_close(my_fd, NULL, 0);
		if (FSAL_IS_ERROR(status2)) {
			LogEvent(COMPONENT_FSAL,
				 "fsal close failed, fd:%d, error: %s",
				 my_fd, msg_fsal_err(status2.major));
		}
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	done_cb(obj_hdl, status, write_arg, caller_arg);
}

fsal_status_t
gpfs_fallocate(struct fsal_obj_handle *obj_hdl, state_t *state,
	       uint64_t offset, uint64_t length, bool allocate)
{
	fsal_status_t status;
	int my_fd = -1;
	bool has_lock = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;
	struct gpfs_fd *gpfs_fd = NULL;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogMajor(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (state) {
		gpfs_fd = STATE2FD(state);
		PTHREAD_RWLOCK_rdlock(&gpfs_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, false, state, openflags,
			 &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		if (gpfs_fd)
			PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);
		return status;
	}

	status = GPFSFSAL_alloc(my_fd, offset, length, allocate);

	if (gpfs_fd)
		PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);

	if (closefd) {
		fsal_status_t status2;

		status2 = fsal_internal_close(my_fd, NULL, 0);
		if (FSAL_IS_ERROR(status2)) {
			LogEvent(COMPONENT_FSAL,
				 "fsal close failed, fd:%d, error: %s",
				 my_fd, msg_fsal_err(status2.major));
		}
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			get_handle2inode(gfh), msg_fsal_err(status.major));
	}
	return status;
}

static fsal_status_t
gpfs_commit_fd(int my_fd, struct fsal_obj_handle *obj_hdl, off_t offset,
	       size_t len)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
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
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_fd temp_fd = {
			FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, -1 };
	struct gpfs_fd *out_fd = &temp_fd;
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

		fsal_set_credentials(&op_ctx->creds);

		status = gpfs_commit_fd(out_fd->fd, obj_hdl, offset, len);

		fsal_restore_ganesha_credentials();
	}
	if (closefd)
		fsal_internal_close(out_fd->fd, NULL, 0);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			get_handle2inode(gfh), msg_fsal_err(status.major));
	}
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
	fsal_status_t status;
	bool has_lock = false;
	bool closefd = false;
	bool bypass = false;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;
	struct gpfs_fd *gpfs_fd = NULL;

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

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (state) {
		gpfs_fd = STATE2FD(state);
		PTHREAD_RWLOCK_rdlock(&gpfs_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = find_fd(&glock_args.lfd, obj_hdl, bypass, state,
			 openflags, &has_lock, &closefd, true);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		if (gpfs_fd)
			PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);
		return status;
	}

	glock_args.flock.l_len = req_lock->lock_length;
	glock_args.flock.l_start = req_lock->lock_start;
	glock_args.flock.l_whence = SEEK_SET;
	glock_args.lock_owner = owner;

	gpfs_sg_arg.lock = &glock_args;
	gpfs_sg_arg.reclaim = req_lock->lock_reclaim;
	gpfs_sg_arg.mountdirfd = export_fd;
	if (op_ctx && op_ctx->client)
		gpfs_sg_arg.cli_ip = op_ctx->client->hostaddr_str;

	status = GPFSFSAL_lock_op(export, lock_op, req_lock, conflicting_lock,
				  &gpfs_sg_arg);

	if (gpfs_fd)
		PTHREAD_RWLOCK_unlock(&gpfs_fd->fdlock);

	if (closefd) {
		fsal_status_t status2;

		status2 = fsal_internal_close(glock_args.lfd, NULL, 0);
		if (FSAL_IS_ERROR(status2)) {
			LogEvent(COMPONENT_FSAL,
				 "fsal close failed, fd:%d, error: %s",
				 glock_args.lfd, msg_fsal_err(status2.major));
		}
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			get_handle2inode(gfh), msg_fsal_err(status.major));
	}
	return status;
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
fsal_status_t gpfs_seek(struct fsal_obj_handle *obj_hdl,
			 struct io_info *info)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	struct gpfs_io_info io_info = {0};
	struct fseek_arg arg = {0};

	assert(myself->u.file.fd.fd >= 3 &&
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

	assert(myself->u.file.fd.fd >= 3 &&
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
 *  @brief Close the file if it is still open.
 *
 *  @param obj_hdl FSAL object handle
 *  @return FSAL status
 */
fsal_status_t gpfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself =
		container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	assert(obj_hdl->type == REGULAR_FILE);

	/* Take write lock on object to protect file descriptor.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	if (myself->u.file.fd.fd >= 0 &&
	    myself->u.file.fd.openflags != FSAL_O_CLOSED) {
		status = fsal_internal_close(myself->u.file.fd.fd, NULL, 0);
		myself->u.file.fd.fd = -1;
		myself->u.file.fd.openflags = FSAL_O_CLOSED;
	} else {
		status = fsalstat(ERR_FSAL_NOT_OPENED, 0);
	}

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

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

fsal_status_t
gpfs_close2(struct fsal_obj_handle *obj_hdl, struct state_t *state)
{
	struct gpfs_fsal_obj_handle *myself;
	state_owner_t *state_owner = NULL;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct gpfs_fd *my_fd = &container_of(state, struct gpfs_state_fd,
					      state)->gpfs_fd;

	LogFullDebug(COMPONENT_FSAL, "state %p", state);

	myself  = container_of(obj_hdl,
			       struct gpfs_fsal_obj_handle,
			       obj_handle);

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
	if (my_fd->fd >= 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "state %p fd %d", state, my_fd->fd);
		state_owner = state->state_owner;

		/* Acquire state's fdlock to make sure no other thread
		 * is operating on the fd while we close it.
		 */
		PTHREAD_RWLOCK_wrlock(&my_fd->fdlock);
		status = fsal_internal_close(my_fd->fd, state_owner, 0);

		my_fd->fd = -1;
		my_fd->openflags = FSAL_O_CLOSED;
		PTHREAD_RWLOCK_unlock(&my_fd->fdlock);
	}
	if (FSAL_IS_ERROR(status)) {
		struct gpfs_file_handle *gfh = container_of(obj_hdl,
			struct gpfs_fsal_obj_handle, obj_handle)->handle;
		LogDebug(COMPONENT_FSAL, "Inode involved: %"PRIu64", error: %s",
			 get_handle2inode(gfh), msg_fsal_err(status.major));
	}
	return status;
}

