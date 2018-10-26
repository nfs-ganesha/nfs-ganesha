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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* file.c
 * File I/O methods for VFS module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include "vfs_methods.h"
#include "os/subr.h"
#include "sal_data.h"

fsal_status_t vfs_open_my_fd(struct vfs_fsal_obj_handle *myself,
			     fsal_openflags_t openflags,
			     int posix_flags,
			     struct vfs_fd *my_fd)
{
	int fd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	LogFullDebug(COMPONENT_FSAL,
		     "my_fd->fd = %d openflags = %x, posix_flags = %x",
		     my_fd->fd, openflags, posix_flags);

	assert(my_fd->fd == -1
	       && my_fd->openflags == FSAL_O_CLOSED && openflags != 0);

	LogFullDebug(COMPONENT_FSAL,
		     "openflags = %x, posix_flags = %x",
		     openflags, posix_flags);

	fd = vfs_fsal_open(myself, posix_flags, &fsal_error);

	if (fd < 0) {
		retval = -fd;
	} else {
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
		my_fd->openflags = openflags;
	}

	return fsalstat(fsal_error, retval);
}

fsal_status_t vfs_close_my_fd(struct vfs_fd *my_fd)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if (my_fd->fd >= 0 && my_fd->openflags != FSAL_O_CLOSED) {
		LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", my_fd->fd);
		retval = close(my_fd->fd);
		if (retval < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
		}
		my_fd->fd = -1;
		my_fd->openflags = FSAL_O_CLOSED;
	}

	return fsalstat(fsal_error, retval);
}

/**
 * @brief Function to open an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   Mode for open
 * @param[out] fd          File descriptor that is to be used
 *
 * @return FSAL status.
 */

static fsal_status_t vfs_open_func(struct fsal_obj_handle *obj_hdl,
				   fsal_openflags_t openflags,
				   struct fsal_fd *fd)
{
	struct vfs_fsal_obj_handle *myself;
	int posix_flags = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	fsal2posix_openflags(openflags, &posix_flags);

	return vfs_open_my_fd(myself, openflags, posix_flags,
			      (struct vfs_fd *)fd);
}

/**
 * @brief Function to close an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  fd          File handle to close
 *
 * @return FSAL status.
 */

static fsal_status_t vfs_close_func(struct fsal_obj_handle *obj_hdl,
				    struct fsal_fd *fd)
{
	return vfs_close_my_fd((struct vfs_fd *)fd);
}

/* vfs_close
 * Close the file if it is still open.
 */

fsal_status_t vfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status;

	assert(obj_hdl->type == REGULAR_FILE);
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	if (myself->u.file.fd.openflags == FSAL_O_CLOSED)
		return fsalstat(ERR_FSAL_NOT_OPENED, 0);

	/* Take write lock on object to protect file descriptor.
	 * This can block over an I/O operation.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	status = vfs_close_my_fd(&myself->u.file.fd);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

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

struct state_t *vfs_alloc_state(struct fsal_export *exp_hdl,
				enum state_type state_type,
				struct state_t *related_state)
{
	struct state_t *state;
	struct vfs_fd *my_fd;

	state = init_state(gsh_calloc(1, sizeof(struct vfs_state_fd)),
			   exp_hdl, state_type, related_state);

	my_fd = &container_of(state, struct vfs_state_fd, state)->vfs_fd;

	my_fd->fd = -1;
	my_fd->openflags = FSAL_O_CLOSED;
	PTHREAD_RWLOCK_init(&my_fd->fdlock, NULL);

	return state;
}

/**
 * @brief free a vfs_state_fd structure
 *
 * @param[in] exp_hdl  Export state_t will be associated with
 * @param[in] state    Related state if appropriate
 *
 */
void vfs_free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	struct vfs_state_fd *state_fd = container_of(state, struct vfs_state_fd,
						     state);
	struct vfs_fd *my_fd = &state_fd->vfs_fd;

	PTHREAD_RWLOCK_destroy(&my_fd->fdlock);

	gsh_free(state_fd);
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

fsal_status_t vfs_merge(struct fsal_obj_handle *orig_hdl,
			struct fsal_obj_handle *dupe_hdl)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	if (orig_hdl->type == REGULAR_FILE &&
	    dupe_hdl->type == REGULAR_FILE) {
		/* We need to merge the share reservations on this file.
		 * This could result in ERR_FSAL_SHARE_DENIED.
		 */
		struct vfs_fsal_obj_handle *orig, *dupe;

		orig = container_of(orig_hdl,
				    struct vfs_fsal_obj_handle,
				    obj_handle);
		dupe = container_of(dupe_hdl,
				    struct vfs_fsal_obj_handle,
				    obj_handle);

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&orig_hdl->obj_lock);

		status = merge_share(&orig->u.file.share, &dupe->u.file.share);

		PTHREAD_RWLOCK_unlock(&orig_hdl->obj_lock);
	}

	return status;
}

static fsal_status_t fetch_attrs(struct vfs_fsal_obj_handle *myself,
				 int my_fd, struct attrlist *attrs)
{
	struct stat stat;
	int retval = 0;
	fsal_status_t status = {0, 0};
	const char *func = "unknown";
#ifdef __FreeBSD__
	struct fhandle *handle;
#endif

	/* Now stat the file as appropriate */
	switch (myself->obj_handle.type) {
	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
		retval = fstatat(my_fd, myself->u.unopenable.name, &stat,
				 AT_SYMLINK_NOFOLLOW);
		func = "fstatat";
		break;

	case REGULAR_FILE:
		retval = fstat(my_fd, &stat);
		func = "fstat";
		break;

	case SYMBOLIC_LINK:
#ifdef __FreeBSD__
		handle = v_to_fhandle(myself->handle->handle_data);
		retval = fhstat(handle, &stat);
		func = "fhstat";
		break;
#endif
	case FIFO_FILE:
	case DIRECTORY:
		retval = vfs_stat_by_handle(my_fd, &stat);
		func = "vfs_stat_by_handle";
		break;

	case NO_FILE_TYPE:
	case EXTENDED_ATTR:
		/* Caught during open with EINVAL */
		break;
	}

	if (retval < 0) {
		if (errno == ENOENT)
			retval = ESTALE;
		else
			retval = errno;

		LogDebug(COMPONENT_FSAL, "%s failed with %s", func,
			 strerror(retval));

		if (attrs->request_mask & ATTR_RDATTR_ERR) {
			/* Caller asked for error to be visible. */
			attrs->valid_mask = ATTR_RDATTR_ERR;
		}

		return fsalstat(posix2fsal_error(retval), retval);
	}

	posix2fsal_attributes_all(&stat, attrs);
	attrs->fsid = myself->obj_handle.fs->fsid;

	if (myself->sub_ops && myself->sub_ops->getattrs) {
		status =
		   myself->sub_ops->getattrs(myself, my_fd, attrs->request_mask,
					     attrs);

		if (FSAL_IS_ERROR(status) &&
		    (attrs->request_mask & ATTR_RDATTR_ERR) != 0) {
			/* Caller asked for error to be visible. */
			attrs->valid_mask = ATTR_RDATTR_ERR;
		}
	}

	return status;
}

static fsal_status_t vfs_open2_by_handle(struct fsal_obj_handle *obj_hdl,
					 struct state_t *state,
					 fsal_openflags_t openflags,
					 enum fsal_create_mode createmode,
					 struct attrlist *attrib_set,
					 fsal_verifier_t verifier,
					 struct attrlist *attrs_out,
					 bool *caller_perm_check)
{
	fsal_status_t status = {0, 0};
	struct vfs_fd *my_fd = NULL;
	int posix_flags = 0;
	bool truncated;
	struct vfs_fsal_obj_handle *myself = container_of(obj_hdl, struct
					  vfs_fsal_obj_handle, obj_handle);

	if (state != NULL)
		my_fd = &container_of(state, struct vfs_state_fd,
				      state)->vfs_fd;

	fsal2posix_openflags(openflags, &posix_flags);
	truncated = (posix_flags & O_TRUNC) != 0;

	LogFullDebug(COMPONENT_FSAL,
		     truncated ? "Truncate" : "No truncate");

	/* This is an open by handle */
	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is a
		 * stateless create such as NFS v3 CREATE).
		 */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		/* Check share reservation conflicts. */
		status = check_share_conflict(&myself->u.file.share,
					      openflags,
					      false);

		if (FSAL_IS_ERROR(status)) {
			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
			return status;
		}

		/* Take the share reservation now by updating the
		 * counters.
		 */
		update_share_counters(&myself->u.file.share,
				      FSAL_O_CLOSED,
				      openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	} else {
		/* We need to use the global fd to continue, and take
		 * the lock to protect it.
		 */
		my_fd = &myself->u.file.fd;
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);
	}

	/* Close if there was any proir fd */
	if (my_fd->openflags != FSAL_O_CLOSED) {
		vfs_close_my_fd(my_fd);
	}
	status = vfs_open_my_fd(myself, openflags, posix_flags, my_fd);

	if (FSAL_IS_ERROR(status)) {
		if (state == NULL) {
			/* Release the lock taken above, and return
			 * since there is nothing to undo.
			 */
			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
			return status;
		} else {
			/* Error - need to release the share */
			goto undo_share;
		}
	}

	/* Check HSM status */
	status = check_hsm_by_fd(my_fd->fd);
	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_DELAY) {
			LogInfo(COMPONENT_FSAL,
				"HSM restore at open for fd=%d", my_fd->fd);
		}

		 /*close fd*/
		(void) vfs_close_my_fd(my_fd);
		if (state == NULL) {
			/* Release the lock taken above, and return
			 * since there is nothing to undo.
			 */
			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
			return status;
		} else {
			/* Error - need to release the share */
			goto undo_share;
		}
	}

	if (createmode >= FSAL_EXCLUSIVE || truncated) {
		/* Refresh the attributes */
		struct attrlist attrs;
		attrmask_t attrs_mask = ATTR_ATIME | ATTR_MTIME;

		if (attrs_out)
			attrs_mask |= attrs_out->request_mask;

		fsal_prepare_attrs(&attrs, attrs_mask);

		status = fetch_attrs(myself, my_fd->fd, &attrs);
		if (FSAL_IS_SUCCESS(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "New size = %" PRIx64,
				     attrs.filesize);

			if (createmode >= FSAL_EXCLUSIVE &&
			    createmode != FSAL_EXCLUSIVE_9P &&
			    !check_verifier_attrlist(&attrs, verifier)) {
				/* Verifier didn't match, return EEXIST */
				status = fsalstat(posix2fsal_error(EEXIST),
						  EEXIST);
			} else if (attrs_out) {
				fsal_copy_attrs(attrs_out, &attrs, true);
			}
		}

		fsal_release_attrs(&attrs);
	} else if (attrs_out && attrs_out->request_mask & ATTR_RDATTR_ERR) {
		attrs_out->valid_mask = ATTR_RDATTR_ERR;
	}

	if (state == NULL) {
		/* If no state, release the lock taken above and return
		 * status. If success, we haven't done any permission
		 * check so ask the caller to do so.
		 */
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
		*caller_perm_check = !FSAL_IS_ERROR(status);
		return status;
	}

	if (!FSAL_IS_ERROR(status)) {
		/* Return success. We haven't done any permission
		 * check so ask the caller to do so.
		 */
		*caller_perm_check = true;
		return status;
	}

	(void) vfs_close_my_fd(my_fd);

undo_share:

	/* Can only get here with state not NULL and an error */

	/* On error we need to release our share reservation
	 * and undo the update of the share counters.
	 * This can block over an I/O operation.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	update_share_counters(&myself->u.file.share,
			      openflags,
			      FSAL_O_CLOSED);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

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
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * The mask should be set in attrs_out indicating which attributes are
 * desired. Note that since this implies a new object is created, if
 * the attributes are not fetched, the fsal_obj_handle itself would not
 * be able to be created and the whole request will fail.
 *
 * The attributes will not be returned if this is an open by object as
 * opposed to an open by name.
 *
 * @note If the file was created, @a new_obj has been ref'd
 *
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrs_in              Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] attrs_out         Optional attributes for newly created object
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */

fsal_status_t vfs_open2(struct fsal_obj_handle *obj_hdl,
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
	int fd, dir_fd;
	int retval = 0;
	mode_t unix_mode = 0000;
	fsal_status_t status = {0, 0};
	struct vfs_fd *my_fd = NULL;
	struct vfs_fsal_obj_handle *myself, *hdl = NULL;
	struct stat stat;
	vfs_file_handle_t *fh = NULL;
	bool created = false;

	if (state != NULL)
		my_fd = &container_of(state, struct vfs_state_fd,
				      state)->vfs_fd;


	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
		    "attrib_set ", attrib_set, false);

	fsal2posix_openflags(openflags, &posix_flags);

	if (createmode >= FSAL_EXCLUSIVE) {
		/* Now fixup attrs for verifier if exclusive create */
		set_common_verifier(attrib_set, verifier);
	}

	if (name == NULL) {
		return vfs_open2_by_handle(obj_hdl, state, openflags,
					   createmode, attrib_set, verifier,
					   attrs_out, caller_perm_check);
	}

	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 * indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

#ifdef ENABLE_VFS_DEBUG_ACL
	if (createmode != FSAL_NO_CREATE) {
		/* Need to ammend attributes for inherited ACL, these will be
		 * set later. We also need to test for permission to create
		 * since there might be an ACL.
		 */
		struct attrlist attrs;
		fsal_accessflags_t access_type;

		access_type = FSAL_MODE_MASK_SET(FSAL_W_OK) |
			FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);
		status = obj_hdl->obj_ops->test_access(obj_hdl, access_type,
						      NULL, NULL, false);

		if (FSAL_IS_ERROR(status))
			return status;

		fsal_prepare_attrs(&attrs, ATTR_ACL);

		status = obj_hdl->obj_ops->getattrs(obj_hdl, &attrs);

		if (FSAL_IS_ERROR(status))
			return status;

		status.major = fsal_inherit_acls(attrib_set, attrs.acl,
						 FSAL_ACE_FLAG_FILE_INHERIT);

		/* Done with the attrs */
		fsal_release_attrs(&attrs);

		if (FSAL_IS_ERROR(status))
			return status;
	}
#endif /* ENABLE_VFS_DEBUG_ACL */

	if (createmode != FSAL_NO_CREATE) {
		/* Now add in O_CREAT and O_EXCL. */
		posix_flags |= O_CREAT;

		/* And if we are at least FSAL_GUARDED, do an O_EXCL create. */
		if (createmode >= FSAL_GUARDED)
			posix_flags |= O_EXCL;

		/* Fetch the mode attribute to use in the openat system call. */
		unix_mode = fsal2unix_mode(attrib_set->mode) &
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

		/* Don't set the mode if we later set the attributes */
		FSAL_UNSET_MASK(attrib_set->valid_mask, ATTR_MODE);
	}

	if (createmode == FSAL_UNCHECKED && (attrib_set->valid_mask != 0)) {
		/* If we have FSAL_UNCHECKED and want to set more attributes
		 * than the mode, we attempt an O_EXCL create first, if that
		 * succeeds, then we will be allowed to set the additional
		 * attributes, otherwise, we don't know we created the file
		 * and this can NOT set the attributes.
		 */
		posix_flags |= O_EXCL;
	}

	dir_fd = vfs_fsal_open(myself, O_PATH | O_NOACCESS, &status.major);

	if (dir_fd < 0)
		return fsalstat(status.major, -dir_fd);

	/** @todo: not sure what this accomplishes... */
	retval = vfs_stat_by_handle(dir_fd, &stat);

	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto direrr;
	}

	/* Become the user because we are creating an object in this dir.
	 */
	if (createmode != FSAL_NO_CREATE)
		if (!vfs_set_credentials(op_ctx->creds, obj_hdl->fsal)) {
			status = posix2fsal_status(EPERM);
			goto direrr;
		}

	if ((posix_flags & O_CREAT) != 0)
		fd = openat(dir_fd, name, posix_flags, unix_mode);
	else
		fd = openat(dir_fd, name, posix_flags);

	if (fd == -1 && errno == EEXIST && createmode == FSAL_UNCHECKED) {
		/* We tried to create O_EXCL to set attributes and failed.
		 * Remove O_EXCL and retry. We still try O_CREAT again just in
		 * case file disappears out from under us.
		 *
		 * Note that because we have dropped O_EXCL, later on we will
		 * not assume we created the file, and thus will not set
		 * additional attributes. We don't need to separately track
		 * the condition of not wanting to set attributes.
		 */
		posix_flags &= ~O_EXCL;
		fd = openat(dir_fd, name, posix_flags, unix_mode);

		/* Preserve errno */
		retval = errno;

		/* If we were creating, restore credentials now. */
		if (createmode != FSAL_NO_CREATE)
			vfs_restore_ganesha_credentials(obj_hdl->fsal);

		LogFullDebug(COMPONENT_FSAL,
			     "File %s exists, retried UNCHECKED create with out O_EXCL, returned %d (%s)",
			     name, retval, strerror(retval));
	} else {
		/* Preserve errno */
		retval = errno;

		/* If we were creating, restore credentials now. */
		if (createmode != FSAL_NO_CREATE)
			vfs_restore_ganesha_credentials(obj_hdl->fsal);
	}

	if (fd < 0) {
		status = posix2fsal_status(retval);
		goto direrr;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Opened fd=%d for file %s", fd, name);

	/* Check HSM status */
	status = check_hsm_by_fd(fd);
	if (FSAL_IS_ERROR(status)) {
		if (status.major == ERR_FSAL_DELAY) {
			LogInfo(COMPONENT_FSAL,
				"HSM restore at open for fd=%d for file %s",
				fd, name);
			status = posix2fsal_status(EAGAIN);
		}

		goto fileerr;
	}

	/* Remember if we were responsible for creating the file.
	 * Note that in an UNCHECKED retry we MIGHT have re-created the
	 * file and won't remember that. Oh well, so in that rare case we
	 * leak a partially created file if we have a subsequent error in here.
	 */
	created = (posix_flags & O_EXCL) != 0;

	/** @todo FSF: If we are running with ENABLE_VFS_DEBUG_ACL or a
	 *             VFS sub-FSAL that supports ACLs but doesn't permission
	 *             check using those ACLs during openat, then there may be
	 *             permission differences here...
	 *
	 *             There are three cases at issue:
	 *             1. If the ACL is more permissive for the caller than
	 *                the mode, and the ACLs are not evaluated by openat
	 *                then a create might fail when the ACL would allow it.
	 *                There's nothing to be done there. Ganesha doesn't
	 *                evaluate directory permissions for create.
	 *             2. An UNCHECKED create where the file already exists
	 *                and the ACL is more permissive then the mode could
	 *                fail. This COULD have been permission checked by
	 *                Ganesha...
	 *             3. An UNCHECKED create where the file already exists
	 *                and the ACL is less permissive then the mode could
	 *                succeed. This COULD have been permission checked by
	 *                Ganesha...
	 *
	 *             These cases are only relevant for create, since if
	 *             create is not in effect, we don't do openat using
	 *             the caller's credentials and instead force Ganesha to
	 *             perform the permission check.
	 */

	/* Do a permission check if we were not attempting to create. If we
	 * were attempting any sort of create, then the openat call was made
	 * with the caller's credentials active and as such was permission
	 * checked.
	 */
	*caller_perm_check = createmode == FSAL_NO_CREATE;

	vfs_alloc_handle(fh);

	retval = vfs_name_to_handle(dir_fd, obj_hdl->fs, name, fh);

	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto fileerr;
	}

	retval = fstat(fd, &stat);

	if (retval < 0) {
		retval = errno;
		status = posix2fsal_status(retval);
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(dir_fd, fh, obj_hdl->fs, &stat, myself->handle, name,
			   op_ctx->fsal_export);

	if (hdl == NULL) {
		status = posix2fsal_status(ENOMEM);
		goto fileerr;
	}

	/* If we didn't have a state above, use the global fd. At this point,
	 * since we just created the global fd, no one else can have a
	 * reference to it, and thus we can mamnipulate unlocked which is
	 * handy since we can then call setattr2 which WILL take the lock
	 * without a double locking deadlock.
	 */
	if (my_fd == NULL) {
		LogFullDebug(COMPONENT_FSAL, "Using global fd");
		my_fd = &hdl->u.file.fd;
	}

	my_fd->fd = fd;
	my_fd->openflags = openflags;

	*new_obj = &hdl->obj_handle;

	if (created && attrib_set->valid_mask != 0) {
		/* Set attributes using our newly opened file descriptor as the
		 * share_fd if there are any left to set (mode and truncate
		 * have already been handled).
		 *
		 * Note that we only set the attributes if we were responsible
		 * for creating the file and we have attributes to set.
		 *
		 * Note if we have ENABLE_VFS_DEBUG_ACL an inherited ACL might
		 * be part of the attributes we are setting here.
		 */
		status = (*new_obj)->obj_ops->setattr2(*new_obj,
						      false,
						      state,
						      attrib_set);

		if (FSAL_IS_ERROR(status))
			goto fileerr;

		if (attrs_out != NULL) {
			status = (*new_obj)->obj_ops->getattrs(*new_obj,
							      attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes. Otherwise continue
				 * with attrs_out indicating ATTR_RDATTR_ERR.
				 */
				goto fileerr;
			}
		}
	} else if (attrs_out != NULL) {
		/* Since we haven't set any attributes other than what was set
		 * on create (if we even created), just use the stat results
		 * we used to create the fsal_obj_handle.
		 */
		posix2fsal_attributes_all(&stat, attrs_out);
		attrs_out->fsid = myself->obj_handle.fs->fsid;
	}

	LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", dir_fd);
	close(dir_fd);

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is
		 * a stateless create such as NFS v3 CREATE).
		 */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&(*new_obj)->obj_lock);

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&hdl->u.file.share,
				      FSAL_O_CLOSED,
				      openflags);

		PTHREAD_RWLOCK_unlock(&(*new_obj)->obj_lock);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:

	/* hdl->u.file.fd will be close in obj_ops->release */
	if (my_fd == &hdl->u.file.fd) {
		LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", fd);
		close(fd);
	}

	if (*new_obj) {
		/* Release the handle we just allocated. */
		(*new_obj)->obj_ops->release(*new_obj);
		*new_obj = NULL;
	}

	/* Delete the file if we actually created it. */
	if (created)
		unlinkat(dir_fd, name, 0);

 direrr:

	LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", dir_fd);
	close(dir_fd);
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

fsal_status_t vfs_reopen2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state,
			  fsal_openflags_t openflags)
{
	struct vfs_fd fd, *my_fd = &fd, *my_share_fd;
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status = {0, 0};
	int posix_flags = 0;
	fsal_openflags_t old_openflags;

	my_share_fd = &container_of(state, struct vfs_state_fd,
				    state)->vfs_fd;

	fsal2posix_openflags(openflags, &posix_flags);

	LogFullDebug(COMPONENT_FSAL,
		     posix_flags & O_TRUNC ? "Truncate" : "No truncate");

	memset(my_fd, 0, sizeof(*my_fd));
	fd.fd = -1;

	myself  = container_of(obj_hdl,
			       struct vfs_fsal_obj_handle,
			       obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	old_openflags = my_share_fd->openflags;

	/* We can conflict with old share, so go ahead and check now. */
	status = check_share_conflict(&myself->u.file.share, openflags, false);

	if (FSAL_IS_ERROR(status)) {
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

		return status;
	}

	/* Set up the new share so we can drop the lock and not have a
	 * conflicting share be asserted, updating the share counters.
	 */
	update_share_counters(&myself->u.file.share, old_openflags, openflags);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	status = vfs_open_my_fd(myself, openflags, posix_flags, my_fd);

	if (!FSAL_IS_ERROR(status)) {
		/* Close the existing file descriptor and copy the new
		 * one over. Make sure no one is using the fd that we are
		 * about to close!
		 */
		PTHREAD_RWLOCK_wrlock(&my_share_fd->fdlock);

		vfs_close_my_fd(my_share_fd);
		my_share_fd->fd = my_fd->fd;
		my_share_fd->openflags = my_fd->openflags;

		PTHREAD_RWLOCK_unlock(&my_share_fd->fdlock);
	} else {
		/* We had a failure on open - we need to revert the share.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		update_share_counters(&myself->u.file.share,
				      openflags,
				      old_openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

	return status;
}

fsal_status_t find_fd(int *fd,
		      struct fsal_obj_handle *obj_hdl,
		      bool bypass,
		      struct state_t *state,
		      fsal_openflags_t openflags,
		      bool *has_lock,
		      bool *closefd,
		      bool open_for_locks)
{
	struct vfs_fsal_obj_handle *myself;
	struct vfs_filesystem *vfs_fs;
	struct vfs_fd temp_fd = {
			FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, -1 };
	struct vfs_fd *out_fd = &temp_fd;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	int rc, posix_flags;
	bool reusing_open_state_fd = false;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	vfs_fs = myself->obj_handle.fs->private_data;

	fsal2posix_openflags(openflags, &posix_flags);

	/* Handle nom-regular files */
	switch (obj_hdl->type) {
	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
		rc = vfs_open_by_handle(vfs_fs,
					myself->u.unopenable.dir,
					O_PATH | O_NOACCESS,
					&status.major);
		if (rc < 0) {
			LogDebug(COMPONENT_FSAL,
				 "Failed with %s openflags 0x%08x",
				 strerror(-rc), O_PATH | O_NOACCESS);
			return fsalstat(posix2fsal_error(-rc), -rc);
		}
		*fd = rc;
		*closefd = true;
		LogFullDebug(COMPONENT_FSAL,
			     "Opened fd=%d for file %p of type %s",
			     rc, myself,
			     object_file_type_to_str(obj_hdl->type));
		return status;

	case REGULAR_FILE:
		status = fsal_find_fd((struct fsal_fd **)&out_fd, obj_hdl,
				      (struct fsal_fd *)&myself->u.file.fd,
				      &myself->u.file.share,
				      bypass, state, openflags,
				      vfs_open_func, vfs_close_func,
				      has_lock, closefd, open_for_locks,
				      &reusing_open_state_fd);

		*fd = out_fd->fd;
		LogFullDebug(COMPONENT_FSAL,
			     "Found fd=%d for file %p of type %s",
			     out_fd->fd, myself,
			     object_file_type_to_str(obj_hdl->type));
		return status;

	case SYMBOLIC_LINK:
		posix_flags |= (O_PATH | O_RDWR | O_NOFOLLOW);
		break;

	case FIFO_FILE:
		posix_flags |= O_NONBLOCK;
		break;

	case DIRECTORY:
		break;

	case NO_FILE_TYPE:
	case EXTENDED_ATTR:
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	/* Open file descriptor for non-regular files. */
	rc = vfs_fsal_open(myself, posix_flags, &status.major);

	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "Failed with %s openflags 0x%08x",
			 strerror(-rc), openflags);
		return fsalstat(posix2fsal_error(-rc), -rc);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Opened fd=%d for file %p of type %s",
		     rc, myself, object_file_type_to_str(obj_hdl->type));

	*fd = rc;
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
 * @param[in]     obj_hdl	File on which to operate
 * @param[in]     bypass	If state doesn't indicate a share reservation,
 *				bypass any deny read
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 *
 * @return Nothing; results are in callback
 */

void vfs_read2(struct fsal_obj_handle *obj_hdl,
	       bool bypass,
	       fsal_async_cb done_cb,
	       struct fsal_io_arg *read_arg,
	       void *caller_arg)
{
	int my_fd = -1;
	ssize_t nb_read;
	fsal_status_t status = {0, 0};
	int retval = 0;
	bool has_lock = false;
	bool closefd = false;
	struct vfs_fd *vfs_fd = NULL;

	if (read_arg->info != NULL) {
		/* Currently we don't support READ_PLUS */
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0), read_arg,
			caller_arg);
		return;
	}

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
		vfs_fd = &container_of(read_arg->state, struct vfs_state_fd,
				       state)->vfs_fd;

		PTHREAD_RWLOCK_rdlock(&vfs_fd->fdlock);
	}

	/* Get a usable file descriptor */
	LogFullDebug(COMPONENT_FSAL, "Calling find_fd, state = %p",
		     read_arg->state);
	status = find_fd(&my_fd, obj_hdl, bypass, read_arg->state, FSAL_O_READ,
			 &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status))
		goto out;

	nb_read = preadv(my_fd, read_arg->iov, read_arg->iov_count,
			 read_arg->offset);

	if (read_arg->offset == -1 || nb_read == -1) {
		retval = errno;
		status = fsalstat(posix2fsal_error(retval), retval);
		goto out;
	}

	read_arg->io_amount = nb_read;

	read_arg->end_of_file = (nb_read == 0);

#if 0
	/** @todo
	 *
	 * Is this all we really need to do to support READ_PLUS? Will anyone
	 * ever get upset that we don't return holes, even for blocks of all
	 * zeroes?
	 *
	 */
	if (info != NULL) {
		info->io_content.what = NFS4_CONTENT_DATA;
		info->io_content.data.d_offset = offset + nb_read;
		info->io_content.data.d_data.data_len = nb_read;
		info->io_content.data.d_data.data_val = buffer;
	}
#endif

 out:

	if (vfs_fd)
		PTHREAD_RWLOCK_unlock(&vfs_fd->fdlock);

	if (closefd) {
		LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", my_fd);
		close(my_fd);
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

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
 *
 * @return FSAL status.
 */

void vfs_write2(struct fsal_obj_handle *obj_hdl,
		bool bypass,
		fsal_async_cb done_cb,
		struct fsal_io_arg *write_arg,
		void *caller_arg)
{
	ssize_t nb_written;
	fsal_status_t status;
	int retval = 0;
	int my_fd = -1;
	bool has_lock = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;
	struct vfs_fd *vfs_fd = NULL;

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
		vfs_fd = &container_of(write_arg->state, struct vfs_state_fd,
				       state)->vfs_fd;

		PTHREAD_RWLOCK_rdlock(&vfs_fd->fdlock);
	}

	/* Get a usable file descriptor */
	LogFullDebug(COMPONENT_FSAL, "Calling find_fd, state = %p",
		     write_arg->state);
	status = find_fd(&my_fd, obj_hdl, bypass, write_arg->state, openflags,
			 &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		goto out;
	}

	if (!vfs_set_credentials(op_ctx->creds, obj_hdl->fsal)) {
		retval = EPERM;
		status = fsalstat(ERR_FSAL_PERM, EPERM);
		goto out;
	}

	nb_written = pwritev(my_fd, write_arg->iov, write_arg->iov_count,
			     write_arg->offset);

	if (nb_written == -1) {
		retval = errno;
		status = fsalstat(posix2fsal_error(retval), retval);
		goto out;
	}

	write_arg->io_amount = nb_written;

	if (write_arg->fsal_stable) {
		retval = fsync(my_fd);
		if (retval == -1) {
			retval = errno;
			status = fsalstat(posix2fsal_error(retval), retval);
			write_arg->fsal_stable = false;
		}
	}

 out:

	vfs_restore_ganesha_credentials(obj_hdl->fsal);

	if (vfs_fd)
		PTHREAD_RWLOCK_unlock(&vfs_fd->fdlock);

	if (closefd) {
		LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", my_fd);
		close(my_fd);
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	done_cb(obj_hdl, status, write_arg, caller_arg);
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

#ifdef __USE_GNU
fsal_status_t vfs_seek2(struct fsal_obj_handle *obj_hdl,
			struct state_t *state,
			struct io_info *info)
{
	struct vfs_fsal_obj_handle *myself;
	off_t ret = 0, offset = info->io_content.hole.di_offset;
	int what = 0;
	bool has_lock = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_ANY;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	int my_fd = -1;
	struct attrlist attrs;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, false, state, openflags,
		&has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status))
		goto out;

	fsal_prepare_attrs(&attrs,
			   (op_ctx->fsal_export->exp_ops.fs_supported_attrs(
							op_ctx->fsal_export)
				& ~(ATTR_ACL | ATTR4_FS_LOCATIONS)));

	status = fetch_attrs(myself, my_fd, &attrs);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_ERROR(status)) {
		goto out;
	}

	/* RFC7862 15.11.3,
	 * If the sa_offset is beyond the end of the file,
	 * then SEEK MUST return NFS4ERR_NXIO. */
	if (offset >= attrs.filesize) {
		status = posix2fsal_status(ENXIO);
		goto out;
	}

	if (info->io_content.what == NFS4_CONTENT_DATA) {
		what = SEEK_DATA;
	} else if (info->io_content.what == NFS4_CONTENT_HOLE) {
		what = SEEK_HOLE;
	} else {
		status = fsalstat(ERR_FSAL_UNION_NOTSUPP, 0);
		goto out;
	}

	ret = lseek(my_fd, offset, what);

	if (ret < 0) {
		if (errno == ENXIO) {
			info->io_eof = TRUE;
		} else {
			status = posix2fsal_status(errno);
		}
		goto out;
	} else {
		info->io_eof = (ret >= attrs.filesize);
		info->io_content.hole.di_offset = ret;
	}

 out:

	if (closefd) {
		LogFullDebug(COMPONENT_FSAL,
			     "Closing Opened fd %d", my_fd);
		close(my_fd);
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}
#endif

/**
 * @brief Reserve/Deallocate space in a region of a file
 *
 * @param[in] obj_hdl File to which bytes should be allocated
 * @param[in] state   open stateid under which to do the allocation
 * @param[in] offset  offset at which to begin the allocation
 * @param[in] length  length of the data to be allocated
 * @param[in] allocate Should space be allocated or deallocated?
 *
 * @return FSAL status.
 */

#ifdef FALLOC_FL_PUNCH_HOLE
fsal_status_t vfs_fallocate(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state, uint64_t offset,
			    uint64_t length, bool allocate)
{
	int ret = 0;
	bool has_lock = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	int my_fd = -1;

	/* Get a usable file descriptor */
	status = find_fd(&my_fd, obj_hdl, false, state, openflags,
		&has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status))
		goto out;

	if (!vfs_set_credentials(op_ctx->creds, obj_hdl->fsal)) {
		status = posix2fsal_status(EPERM);
		goto out;
	}

	ret = fallocate(my_fd,
			allocate
				? 0
				: FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
			offset, length);

	if (ret < 0) {
		ret = errno;
		LogFullDebug(COMPONENT_FSAL,
			     "fallocate returned %s (%d)",
			     strerror(ret), ret);
		status = posix2fsal_status(ret);
	}

	vfs_restore_ganesha_credentials(obj_hdl->fsal);

 out:

	if (closefd) {
		LogFullDebug(COMPONENT_FSAL,
			     "Closing Opened fd %d", my_fd);
		close(my_fd);
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}
#endif

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

fsal_status_t vfs_commit2(struct fsal_obj_handle *obj_hdl,
			  off_t offset,
			  size_t len)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status;
	int retval;
	struct vfs_fd temp_fd = {
			FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, -1 };
	struct vfs_fd *out_fd = &temp_fd;
	bool has_lock = false;
	bool closefd = false;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	/* Make sure file is open in appropriate mode.
	 * Do not check share reservation.
	 */
	status = fsal_reopen_obj(obj_hdl, false, false, FSAL_O_WRITE,
				 (struct fsal_fd *)&myself->u.file.fd,
				 &myself->u.file.share,
				 vfs_open_func, vfs_close_func,
				 (struct fsal_fd **)&out_fd, &has_lock,
				 &closefd);

	if (!FSAL_IS_ERROR(status)) {

		if (!vfs_set_credentials(op_ctx->creds, obj_hdl->fsal)) {
			retval = EPERM;
			status = fsalstat(ERR_FSAL_PERM, EPERM);
			goto out;
		}

		retval = fsync(out_fd->fd);

		if (retval == -1) {
			retval = errno;
			status = fsalstat(posix2fsal_error(retval), retval);
		}

		vfs_restore_ganesha_credentials(obj_hdl->fsal);
	}

out:
	if (closefd) {
		LogFullDebug(COMPONENT_FSAL,
			     "Closing Opened fd %d", out_fd->fd);
		close(out_fd->fd);
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

#ifdef F_OFD_GETLK
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
fsal_status_t vfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   void *owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock)
{
	struct flock lock_args;
	int fcntl_comm;
	fsal_status_t status = {0, 0};
	int retval = 0;
	int my_fd = -1;
	bool has_lock = false;
	bool closefd = false;
	bool bypass = false;
	fsal_openflags_t openflags = FSAL_O_RDWR;
	struct vfs_fd *vfs_fd = NULL;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d start:%" PRIu64 " length:%"
		     PRIu64 " ",
		     lock_op, request_lock->lock_type, request_lock->lock_start,
		     request_lock->lock_length);

	if (lock_op == FSAL_OP_LOCKT) {
		fcntl_comm = F_OFD_GETLK;
		/* We may end up using global fd, don't fail on a deny mode */
		bypass = true;
		openflags = FSAL_O_ANY;
	} else if (lock_op == FSAL_OP_LOCK) {
		fcntl_comm = F_OFD_SETLK;

		if (request_lock->lock_type == FSAL_LOCK_R)
			openflags = FSAL_O_READ;
		else if (request_lock->lock_type == FSAL_LOCK_W)
			openflags = FSAL_O_WRITE;
	} else if (lock_op == FSAL_OP_UNLOCK) {
		fcntl_comm = F_OFD_SETLK;
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
			"The requested lock length is out of range- lock_args.l_len(%"
			PRId64 "), request_lock_length(%" PRIu64 ")",
			lock_args.l_len, request_lock->lock_length);
		return fsalstat(ERR_FSAL_BAD_RANGE, 0);
	}

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (state) {
		vfs_fd = &container_of(state, struct vfs_state_fd,
				       state)->vfs_fd;

		PTHREAD_RWLOCK_rdlock(&vfs_fd->fdlock);
	}

	/* Get a usable file descriptor */
	LogFullDebug(COMPONENT_FSAL, "Calling find_fd, state = %p", state);
	status = find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			 &has_lock, &closefd, true);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_FSAL, "Unable to find fd for lock operation");
		goto err;
	}

	errno = 0;
	retval = fcntl(my_fd, fcntl_comm, &lock_args);

	if (retval /* && lock_op == FSAL_OP_LOCK */) {
		retval = errno;
		status = posix2fsal_status(retval);

		LogDebug(COMPONENT_FSAL,
			 "fcntl returned %d %s",
			 retval, strerror(retval));

		if (conflicting_lock != NULL) {
			/* Get the conflicting lock */
			int rc = fcntl(my_fd, F_GETLK, &lock_args);

			if (rc) {
				retval = errno;	/* we lose the initial error */
				status = posix2fsal_status(retval);
				LogCrit(COMPONENT_FSAL,
					"After failing a lock request, I couldn't even get the details of who owns the lock.");
				goto err;
			}

			if (conflicting_lock != NULL) {
				conflicting_lock->lock_length = lock_args.l_len;
				conflicting_lock->lock_start =
				    lock_args.l_start;
				conflicting_lock->lock_type = lock_args.l_type;
			}
		}

		goto err;
	}

	/* F_UNLCK is returned then the tested operation would be possible. */
	if (conflicting_lock != NULL) {
		if (lock_op == FSAL_OP_LOCKT && lock_args.l_type != F_UNLCK) {
			conflicting_lock->lock_length = lock_args.l_len;
			conflicting_lock->lock_start = lock_args.l_start;
			conflicting_lock->lock_type = lock_args.l_type;
		} else {
			conflicting_lock->lock_length = 0;
			conflicting_lock->lock_start = 0;
			conflicting_lock->lock_type = FSAL_NO_LOCK;
		}
	}

	/* Fall through (status == SUCCESS) */

 err:

	if (vfs_fd)
		PTHREAD_RWLOCK_unlock(&vfs_fd->fdlock);

	if (closefd) {
		LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", my_fd);
		close(my_fd);
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}
#endif

/**
 * @brief Get attributes
 *
 * This function freshens the cached attributes stored on the handle.
 * Since the caller can take the attribute lock and read them off the
 * public filehandle, they are not copied out.
 *
 * @param[in]  obj_hdl  Object to query
 *
 * @return FSAL status.
 */

fsal_status_t vfs_getattr2(struct fsal_obj_handle *obj_hdl,
			   struct attrlist *attrs)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status = {0, 0};
	bool has_lock = false;
	bool closefd = false;
	int my_fd = -1;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s getattr for handle belonging to FSAL %s, ignoring",
			 obj_hdl->fsal->name,
			 obj_hdl->fs->fsal != NULL
				? obj_hdl->fs->fsal->name
				: "(none)");
		goto out;
	}

	#ifdef __FreeBSD__
	if (obj_hdl->type == SYMBOLIC_LINK)
		goto fetch;
	#endif

	/* Get a usable file descriptor (don't need to bypass - FSAL_O_ANY
	 * won't conflict with any share reservation).
	 */
	LogFullDebug(COMPONENT_FSAL, "Calling find_fd, state = NULL");
	status = find_fd(&my_fd, obj_hdl, false, NULL, FSAL_O_ANY,
			 &has_lock, &closefd, false);

	LogFullDebug(COMPONENT_FSAL, "Got fd %d closefd = %s",
		     my_fd, closefd ? "true" : "false");

	if (FSAL_IS_ERROR(status)) {
		if (obj_hdl->type == SYMBOLIC_LINK &&
		    status.major == ERR_FSAL_PERM) {
			/* You cannot open_by_handle (XFS on linux) a symlink
			 * and it throws an EPERM error for it.
			 * open_by_handle_at does not throw that error for
			 * symlinks so we play a game here.  Since there is
			 * not much we can do with symlinks anyway,
			 * say that we did it but don't actually
			 * do anything.  In this case, return the stat we got
			 * at lookup time.  If you *really* want to tweek things
			 * like owners, get a modern linux kernel...
			 */
			status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		}
		goto out;
	}
#ifdef __FreeBSD__
 fetch:
#endif
	status = fetch_attrs(myself, my_fd, attrs);

 out:

	if (closefd) {
		LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", my_fd);
		close(my_fd);
	}

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object.  Which attributes are
 * set is determined by attrib_set->valid_mask. The FSAL must manage bypass
 * or not of share reservations, and a state may be passed.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 * @param[in] attrib_set Attributes to set
 *
 * @return FSAL status.
 */
fsal_status_t vfs_setattr2(struct fsal_obj_handle *obj_hdl,
			   bool bypass,
			   struct state_t *state,
			   struct attrlist *attrib_set)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status = {0, 0};
	int retval = 0;
	fsal_openflags_t openflags = FSAL_O_ANY;
	bool has_lock = false;
	bool closefd = false;
	int my_fd;
	const char *func = "none";
	struct vfs_fd *vfs_fd = NULL;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE))
		attrib_set->mode &=
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name,
			 obj_hdl->fs->fsal != NULL
				? obj_hdl->fs->fsal->name
				: "(none)");
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

#ifdef ENABLE_VFS_DEBUG_ACL
#ifdef ENABLE_RFC_ACL
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE) &&
	    !FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ACL)) {
		/* Set ACL from MODE */
		struct attrlist attrs;

		fsal_prepare_attrs(&attrs, ATTR_ACL);

		status = obj_hdl->obj_ops->getattrs(obj_hdl, &attrs);

		if (FSAL_IS_ERROR(status))
			return status;

		status = fsal_mode_to_acl(attrib_set, attrs.acl);

		/* Done with the attrs */
		fsal_release_attrs(&attrs);
	} else {
		/* If ATTR_ACL is set, mode needs to be adjusted no matter what.
		 * See 7530 s 6.4.1.3 */
		if (!FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE))
			attrib_set->mode = myself->mode;
		status = fsal_acl_to_mode(attrib_set);
	}

	if (FSAL_IS_ERROR(status))
		return status;
#endif /* ENABLE_RFC_ACL */
#endif

	/* This is yet another "you can't get there from here".  If this object
	 * is a socket (AF_UNIX), an fd on the socket s useless _period_.
	 * If it is for a symlink, without O_PATH, you will get an ELOOP error
	 * and (f)chmod doesn't work for a symlink anyway - not that it matters
	 * because access checking is not done on the symlink but the final
	 * target.
	 * AF_UNIX sockets are also ozone material.  If the socket is already
	 * active listeners et al, you can manipulate the mode etc.  If it is
	 * just sitting there as in you made it with a mknod.
	 * (one of those leaky abstractions...)
	 * or the listener forgot to unlink it, it is lame duck.
	 */

	/* Test if size is being set, make sure file is regular and if so,
	 * require a read/write file descriptor.
	 */
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			LogFullDebug(COMPONENT_FSAL,
				     "Setting size on non-regular file");
			return fsalstat(ERR_FSAL_INVAL, EINVAL);
		}
		openflags = FSAL_O_RDWR;
	}

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (state) {
		vfs_fd = &container_of(state, struct vfs_state_fd,
				       state)->vfs_fd;

		PTHREAD_RWLOCK_rdlock(&vfs_fd->fdlock);
	}

	/* Get a usable file descriptor. Share conflict is only possible if
	 * size is being set.
	 */
	LogFullDebug(COMPONENT_FSAL, "Calling find_fd, state = %p", state);
	status = find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			 &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		if (obj_hdl->type == SYMBOLIC_LINK &&
		    (status.major == ERR_FSAL_PERM
#ifdef __FreeBSD__
		     || status.major == ERR_FSAL_MLINK
#endif
			)) {
			/* You cannot open_by_handle (XFS) a symlink and it
			 * throws an EPERM error for it.  open_by_handle_at
			 * does not throw that error for symlinks so we play a
			 * game here.  Since there is not much we can do with
			 * symlinks anyway, say that we did it
			 * but don't actually do anything.
			 * If you *really* want to tweek things
			 * like owners, get a modern linux kernel...
			 */
			status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		}
		LogFullDebug(COMPONENT_FSAL,
			     "find_fd status=%s",
			     fsal_err_txt(status));
		goto out;
	}

	/** TRUNCATE **/
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
		retval = ftruncate(my_fd, attrib_set->filesize);
		if (retval != 0) {
			/** @todo FSF: is this still necessary?
			 *
			 * XXX ESXi volume creation pattern reliably
			 * reached this point in the past, however now that we
			 * only use the already open file descriptor if it is
			 * open read/write, this may no longer fail.
			 * If there is some other error from ftruncate, then
			 * we will needlessly retry, but without more detail
			 * of the original failure, we can't be sure.
			 * Fortunately permission checking is done by
			 * Ganesha before calling here, so we won't get an
			 * EACCES since this call is done as root. We could
			 * get EFBIG, EPERM, or EINVAL.
			 */
			/** @todo FSF: re-open if we really still need this
			 */

			retval = ftruncate(my_fd, attrib_set->filesize);
			if (retval != 0) {
				func = "truncate";
				goto fileerr;
			}
		}
	}

	/** CHMOD **/
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE)) {
		/* The POSIX chmod call doesn't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if (obj_hdl->type != SYMBOLIC_LINK) {
			if (vfs_unopenable_type(obj_hdl->type))
				retval = fchmodat(
					my_fd,
					myself->u.unopenable.name,
					fsal2unix_mode(attrib_set->mode),
					0);
			else
				retval = fchmod(
					my_fd,
					fsal2unix_mode(attrib_set->mode));

			if (retval != 0) {
				func = "chmod";
				goto fileerr;
			}
		}
	}

	/**  CHOWN  **/
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_OWNER | ATTR_GROUP)) {
		uid_t user = FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_OWNER)
		    ? (int)attrib_set->owner : -1;
		gid_t group = FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_GROUP)
		    ? (int)attrib_set->group : -1;

		if (vfs_unopenable_type(obj_hdl->type))
			retval = fchownat(my_fd, myself->u.unopenable.name,
					  user, group, AT_SYMLINK_NOFOLLOW);
		else if (obj_hdl->type == SYMBOLIC_LINK)
			retval = fchownat(my_fd, "", user, group,
					  AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH);
		else
			retval = fchown(my_fd, user, group);

		if (retval) {
			func = "chown";
			goto fileerr;
		}
	}

	/**  UTIME  **/
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTRS_SET_TIME)) {
		struct timespec timebuf[2];

		if (obj_hdl->type == SYMBOLIC_LINK)
			goto out; /* Setting time on symlinks is illegal */
		/* Atime */
		if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME_SERVER)) {
			timebuf[0].tv_sec = 0;
			timebuf[0].tv_nsec = UTIME_NOW;
		} else if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME)) {
			timebuf[0] = attrib_set->atime;
		} else {
			timebuf[0].tv_sec = 0;
			timebuf[0].tv_nsec = UTIME_OMIT;
		}

		/* Mtime */
		if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME_SERVER)) {
			timebuf[1].tv_sec = 0;
			timebuf[1].tv_nsec = UTIME_NOW;
		} else if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME)) {
			timebuf[1] = attrib_set->mtime;
		} else {
			timebuf[1].tv_sec = 0;
			timebuf[1].tv_nsec = UTIME_OMIT;
		}
		if (vfs_unopenable_type(obj_hdl->type))
			retval = vfs_utimesat(my_fd, myself->u.unopenable.name,
					      timebuf, AT_SYMLINK_NOFOLLOW);
		else
			retval = vfs_utimes(my_fd, timebuf);
		if (retval != 0) {
			func = "utimes";
			goto fileerr;
		}
	}

	/** SUBFSAL **/
	if (myself->sub_ops && myself->sub_ops->setattrs) {
		status = myself->sub_ops->setattrs(
					myself,
					my_fd,
					attrib_set->valid_mask, attrib_set);
		if (FSAL_IS_ERROR(status))
			goto out;
	}

	errno = 0;

 fileerr:

	retval = errno;

	if (retval != 0) {
		LogDebug(COMPONENT_FSAL,
			 "%s returned %s",
			 func, strerror(retval));
	}

	status = fsalstat(posix2fsal_error(retval), retval);

 out:

	if (vfs_fd)
		PTHREAD_RWLOCK_unlock(&vfs_fd->fdlock);

	if (closefd) {
		LogFullDebug(COMPONENT_FSAL, "Closing Opened fd %d", my_fd);
		close(my_fd);
	}

	if (has_lock)
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

fsal_status_t vfs_close2(struct fsal_obj_handle *obj_hdl,
			 struct state_t *state)
{
	struct vfs_fsal_obj_handle *myself = NULL;
	fsal_status_t status = {0, 0};
	struct vfs_fd *my_fd = &container_of(state, struct vfs_state_fd,
					     state)->vfs_fd;

	myself = container_of(obj_hdl,
			      struct vfs_fsal_obj_handle,
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

	/* Acquire state's fdlock to make sure no other thread
	 * is operating on the fd while we close it.
	 */
	PTHREAD_RWLOCK_wrlock(&my_fd->fdlock);
	status = vfs_close_my_fd(my_fd);
	PTHREAD_RWLOCK_unlock(&my_fd->fdlock);

	return status;
}
