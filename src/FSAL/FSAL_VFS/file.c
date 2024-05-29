// SPDX-License-Identifier: LGPL-3.0-or-later
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
	       && my_fd->fsal_fd.openflags == FSAL_O_CLOSED && openflags != 0);

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
		my_fd->fsal_fd.openflags = FSAL_O_NFS_FLAGS(openflags);
	}

	return fsalstat(fsal_error, retval);
}

fsal_status_t vfs_close_my_fd(struct vfs_fd *my_fd)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if (my_fd->fd >= 0 && my_fd->fsal_fd.openflags != FSAL_O_CLOSED) {
		LogFullDebug(COMPONENT_FSAL,
			"Closing Opened fd %d for fsal_fd(%p) with type(%d)",
			my_fd->fd, &my_fd->fsal_fd,
			my_fd->fsal_fd.fd_type);
		retval = close(my_fd->fd);
		if (retval < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
		}
		my_fd->fd = -1;
		my_fd->fsal_fd.openflags = FSAL_O_CLOSED;
	} else {
		fsal_error = ERR_FSAL_NOT_OPENED;
	}

	return fsalstat(fsal_error, retval);
}

/**
 * @brief VFS Function to open or reopen a fsal_fd.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   New mode for open
 * @param[out] fsal_fd     File descriptor that is to be used
 *
 * @return FSAL status.
 */

fsal_status_t vfs_reopen_func(struct fsal_obj_handle *obj_hdl,
			      fsal_openflags_t openflags,
			      struct fsal_fd *fsal_fd)
{
	struct vfs_fsal_obj_handle *myself;
	struct vfs_fd *my_fd;
	int fd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int posix_flags = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	my_fd = container_of(fsal_fd, struct vfs_fd, fsal_fd);

	fsal2posix_openflags(openflags, &posix_flags);

	LogFullDebug(COMPONENT_FSAL,
		     "my_fd->fd = %d openflags = %x, posix_flags = %x",
		     my_fd->fd, openflags, posix_flags);

	fd = vfs_fsal_open(myself, posix_flags, &fsal_error);

	if (fd < 0) {
		retval = -fd;
	} else {
		if (my_fd->fd != -1) {
			/* File was previously open, close old fd */
			retval = close(my_fd->fd);

			if (retval < 0) {
				retval = errno;

				LogFullDebug(COMPONENT_FSAL,
					     "close failed with %s",
					     strerror(retval));

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
	}

	return fsalstat(fsal_error, retval);
}

/**
 * @brief VFS Function to close a fsal_fd.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  fd          File handle to close
 *
 * @return FSAL status.
 */

fsal_status_t vfs_close_func(struct fsal_obj_handle *obj_hdl,
			     struct fsal_fd *fd)
{
	return vfs_close_my_fd(container_of(fd, struct vfs_fd, fsal_fd));
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

	status = close_fsal_fd(obj_hdl, &myself->u.file.fd.fsal_fd, false);

	if (FSAL_IS_ERROR(status))
		return status;

	/** @todo don't merge me... hack to avoid legacy open_fd_count */
	return fsalstat(ERR_FSAL_NOT_OPENED, 0);
}

void vfs_free_state(struct state_t *state)
{
	struct vfs_fd *my_fd;

	my_fd = &container_of(state, struct vfs_state_fd, state)->vfs_fd;

	LogFullDebug(COMPONENT_FSAL,
		"Destroying fd %d for fsal_fd(%p) with type(%d)",
		my_fd->fd, &my_fd->fsal_fd,
		my_fd->fsal_fd.fd_type);
	destroy_fsal_fd(&my_fd->fsal_fd);

	gsh_free(state);
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
			   vfs_free_state, state_type, related_state);

	my_fd = &container_of(state, struct vfs_state_fd, state)->vfs_fd;

	init_fsal_fd(&my_fd->fsal_fd, FSAL_FD_STATE, op_ctx->fsal_export);
	my_fd->fd = -1;

	return state;
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
		status = merge_share(orig_hdl, &orig->u.file.share,
				     &dupe->u.file.share);
	}

	return status;
}

static fsal_status_t fetch_attrs(struct vfs_fsal_obj_handle *myself,
				 int my_fd, struct fsal_attrlist *attrs)
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

	/* Get correct fsid from the fsal_filesystem, it may
	* not be the device major/minor from stat.
	*/
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
					 fsal_verifier_t verifier,
					 struct fsal_attrlist *attrs_out)
{
	struct vfs_fd *my_fd = NULL;
	struct fsal_fd *fsal_fd;
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	fsal_openflags_t old_openflags;
	bool truncated = openflags & FSAL_O_TRUNC;
	bool is_fresh_open = false;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (state != NULL)
		my_fd = &container_of(state, struct vfs_state_fd,
				      state)->vfs_fd;
	else
		my_fd = &myself->u.file.fd;

	fsal_fd = &my_fd->fsal_fd;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return fsalstat(posix2fsal_error(EXDEV), EXDEV);
	}

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
		status = check_share_conflict(&myself->u.file.share, openflags,
					      false);

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
	status = vfs_reopen_func(obj_hdl, openflags, fsal_fd);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "vfs_reopen_func returned %s",
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

	/* Check HSM status */
	status = check_hsm_by_fd(my_fd->fd);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "check_hsm_by_fd returned %s",
			 fsal_err_txt(status));

		if (status.major == ERR_FSAL_DELAY) {
			LogInfo(COMPONENT_FSAL,
				"HSM restore at open for fd=%d", my_fd->fd);
		}

		goto out;
	}

	if (createmode >= FSAL_EXCLUSIVE || (truncated && attrs_out)) {
		/* NOTE: won't come in here when called from vfs_reopen2...
		 *       truncated might be set, but attrs_out will be NULL.
		 */

		/* Refresh the attributes */
		struct fsal_attrlist attrs;
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
			    !check_verifier_attrlist(&attrs, verifier,
						     obj_hdl->fs->trunc_verif)
			   ) {
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

out:

	if (FSAL_IS_ERROR(status)) {
		if (is_fresh_open) {
			/* Now that we have decided to close this FD,
			 * let's clean it off from fd_lru and
			 * ensure counters are decremented.
			 */
			remove_fd_lru(fsal_fd);
		}
		/* Close fd */
		(void) vfs_close_my_fd(my_fd);
	}

exit:

	if (state != NULL) {
		if (!FSAL_IS_ERROR(status)) {
			/* Success, establish the new share. */
			update_share_counters(&myself->u.file.share,
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
 * @param[in]     obj_hdl               File to open or parent directory
 * @param[in,out] state                 state_t to use for this operation
 * @param[in]     openflags             Mode for open
 * @param[in]     createmode            Mode for create
 * @param[in]     name                  Name for file if being created or
 *                                      opened
 * @param[in]     attrs_in              Attributes to set on created file
 * @param[in]     verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj               Newly created object
 * @param[in,out] attrs_out             Optional attributes for newly created
 *                                      object
 * @param[in,out] caller_perm_check     The caller must do a permission check
 * @param[in,out] parent_pre_attrs_out  Optional attributes for parent dir
 *                                      before the operation. Should be atomic.
 * @param[in,out] parent_post_attrs_out Optional attributes for parent dir
 *                                      after the operation. Should be atomic.
 *
 * @return FSAL status.
 */

fsal_status_t vfs_open2(struct fsal_obj_handle *obj_hdl,
			struct state_t *state,
			fsal_openflags_t openflags,
			enum fsal_create_mode createmode,
			const char *name,
			struct fsal_attrlist *attrib_set,
			fsal_verifier_t verifier,
			struct fsal_obj_handle **new_obj,
			struct fsal_attrlist *attrs_out,
			bool *caller_perm_check,
			struct fsal_attrlist *parent_pre_attrs_out,
			struct fsal_attrlist *parent_post_attrs_out)
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
		set_common_verifier(attrib_set, verifier,
				    obj_hdl->fs->trunc_verif);
	}

	if (name == NULL) {
		status = vfs_open2_by_handle(obj_hdl, state, openflags,
					     createmode, verifier,
					     attrs_out);

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

#ifdef ENABLE_VFS_DEBUG_ACL
	if (createmode != FSAL_NO_CREATE) {
		/* Need to amend attributes for inherited ACL, these will be
		 * set later. We also need to test for permission to create
		 * since there might be an ACL.
		 */
		struct fsal_attrlist attrs;
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
		if (!vfs_set_credentials(&op_ctx->creds, obj_hdl->fsal)) {
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

	/* Check if the opened file is not a regular file. */
	if (posix2fsal_type(stat.st_mode) == DIRECTORY) {
		/* Trying to open2 a directory */
		status = fsalstat(ERR_FSAL_ISDIR, 0);
		goto fileerr;
	}

	if (posix2fsal_type(stat.st_mode) != REGULAR_FILE) {
		/* Trying to open2 any other non-regular file */
		status = fsalstat(ERR_FSAL_SYMLINK, 0);
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
		my_fd = &hdl->u.file.fd;
		LogFullDebug(COMPONENT_FSAL,
			"Using global fd with fsal_fd(%p) for fd(%d/%d) type(%d)",
			&my_fd->fsal_fd, my_fd->fd, fd,
			my_fd->fsal_fd.fd_type);
		/* Need to LRU track global fd including incrementing
		 * fsal_fd_global_counter.
		 */
		insert_fd_lru(&my_fd->fsal_fd);
	}

	my_fd->fd = fd;
	my_fd->fsal_fd.openflags = FSAL_O_NFS_FLAGS(openflags);

	*new_obj = &hdl->obj_handle;

	if (created && attrib_set->valid_mask != 0) {

retry_attr:

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
		status = (*new_obj)->obj_ops->setattr2(*new_obj, false,
						       state, attrib_set);

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

			LogFullDebug(COMPONENT_FSAL,
				     "Set atime %llx %llx mtime %llx %llx",
				     (long long) attrs_out->atime.tv_sec,
				     (long long) attrs_out->atime.tv_nsec,
				     (long long) attrs_out->mtime.tv_sec,
				     (long long) attrs_out->mtime.tv_nsec);

			if ((createmode >= FSAL_EXCLUSIVE) &&
			    (!(*new_obj)->fs->trunc_verif) &&
			    ((attrs_out->atime.tv_sec !=
						attrib_set->atime.tv_sec) ||
			     (attrs_out->mtime.tv_sec !=
						attrib_set->mtime.tv_sec))) {
				LogInfo(COMPONENT_FSAL,
					"Verifier was not stored correctly for filesystem %s, trying again with truncated verifier",
					(*new_obj)->fs->path);
				(*new_obj)->fs->trunc_verif = true;
				FSAL_UNSET_MASK(attrib_set->valid_mask,
						ATTR_ATIME | ATTR_MTIME);
				set_common_verifier(attrib_set, verifier, true);
				goto retry_attr;
			}
		}
	} else if (attrs_out != NULL) {
		/* Since we haven't set any attributes other than what was set
		 * on create (if we even created), just use the stat results
		 * we used to create the fsal_obj_handle.
		 */
		posix2fsal_attributes_all(&stat, attrs_out);

		/* Get correct fsid from the fsal_filesystem, it may
		* not be the device major/minor from stat.
		*/
		attrs_out->fsid = myself->obj_handle.fs->fsid;
	}

	LogFullDebug(COMPONENT_FSAL, "Closing Opened dir fd %d", dir_fd);
	close(dir_fd);

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is
		 * a stateless create such as NFS v3 CREATE).
		 */

		/* Take the share reservation now by updating the counters. */
		update_share_counters_locked(*new_obj, &hdl->u.file.share,
					     FSAL_O_CLOSED,
					     openflags);
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

	LogFullDebug(COMPONENT_FSAL, "Closing Opened dir fd %d", dir_fd);
	close(dir_fd);
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

fsal_openflags_t vfs_status2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state)
{
	struct vfs_fd *my_fd = &((struct vfs_state_fd *)state)->vfs_fd;

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

fsal_status_t vfs_reopen2(struct fsal_obj_handle *obj_hdl,
			  struct state_t *state,
			  fsal_openflags_t openflags)
{
	return vfs_open2_by_handle(obj_hdl, state, openflags, FSAL_NO_CREATE,
				   NULL, NULL);
}

/**
 * @brief Find a usable fd for getattr and setattr.
 *
 * NOTE: If an fd is returned, fsal_complete_io must be called to close it.
 *       If the global or state fd is used, fsal_start_io will have done its
 *       thing.
 *
 * @param[in,out] out_fd         The fd to be used
 * @param[in]     obj_hdl        The object being worked on
 * @param[in]     tmp_fd         A temp fd the caller passes in to be used if
 *                               a state fd or the global fd is not usable. Will
 *                               be used for all non-regular files.
 * @param[in]     state          The state if any.
 * @param[in]     openflags      How the getattr or setattr needs the fd open
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any deny read
 *
 * @return FSAL status.
 */

fsal_status_t find_fd(struct fsal_fd **out_fd,
		      struct fsal_obj_handle *obj_hdl,
		      struct fsal_fd *tmp_fd,
		      struct state_t *state,
		      fsal_openflags_t openflags,
		      bool bypass)
{
	struct vfs_fsal_obj_handle *myself;
	struct vfs_fd *my_fd;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	int rc, posix_flags;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->type == REGULAR_FILE) {
		status = fsal_start_io(out_fd, obj_hdl,
				       &myself->u.file.fd.fsal_fd, tmp_fd,
				       state, openflags, false, NULL, bypass,
				       &myself->u.file.share);

		return status;
	}

	/* Setup to use tmp_fd for non-regiular files. The way we set it up,
	 * it's OK to call fsal_complete_io() to close the file which means
	 * our caller doesn't have to do anything special with the found fd...
	 */
	my_fd = container_of(tmp_fd, struct vfs_fd, fsal_fd);

	if (openflags == FSAL_O_ANY) {
		/* Since this is a temp file, we will open READ */
		openflags = FSAL_O_READ;
	}

	fsal2posix_openflags(openflags, &posix_flags);

	/* Handle nom-regular files */
	switch (obj_hdl->type) {
	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
		rc = vfs_open_by_handle(myself->obj_handle.fs,
					myself->u.unopenable.dir,
					O_PATH | O_NOACCESS,
					&status.major);

		if (rc < 0) {
			LogDebug(COMPONENT_FSAL,
				 "Failed with %s openflags 0x%08x",
				 strerror(-rc), O_PATH | O_NOACCESS);
			return posix2fsal_status(-rc);
		}

		goto success;

	case REGULAR_FILE:
		/* Already handled above */
		assert(false);
		break;

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
		return posix2fsal_status(EINVAL);
	}

	/* Open file descriptor for non-regular files. */
	rc = vfs_fsal_open(myself, posix_flags, &status.major);

	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "Failed with %s openflags 0x%08x",
			 strerror(-rc), openflags);
		return posix2fsal_status(-rc);
	}

success:

	/* We will want to close the temp_fd. */
	tmp_fd->close_on_complete = true;

	LogFullDebug(COMPONENT_FSAL,
		     "Opened fd=%d for file %p of type %s with open flags 0x%08x",
		     rc, myself, object_file_type_to_str(obj_hdl->type),
		     openflags);

	/* Finish setting up tmp_fd. */
	my_fd->fd = rc;
	my_fd->fsal_fd.openflags = FSAL_O_NFS_FLAGS(openflags);
	*out_fd = tmp_fd;

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
	ssize_t nb_read;
	fsal_status_t status = {0, 0}, status2;
	struct vfs_fd *my_fd;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (read_arg->info != NULL) {
		/* Currently we don't support READ_PLUS */
		status = posix2fsal_status(ENOTSUP);
		goto exit;
	}

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		status = posix2fsal_status(EXDEV);
		goto exit;
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

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

	nb_read = preadv(my_fd->fd, read_arg->iov, read_arg->iov_count,
			 read_arg->offset);

	if (read_arg->offset == -1 || nb_read == -1) {
		status = posix2fsal_status(errno);
		LogFullDebug(COMPONENT_FSAL,
			     "preadv failed returning %s",
			     fsal_err_txt(status));
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
	fsal_status_t status, status2;
	int retval = 0;
	struct vfs_fd *my_fd;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		status = posix2fsal_status(EXDEV);
		goto exit;
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

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

	if (!vfs_set_credentials(&op_ctx->creds, obj_hdl->fsal)) {
		status = posix2fsal_status(EPERM);
		LogFullDebug(COMPONENT_FSAL,
			     "vfs_set_credentials failed returning %s",
			     fsal_err_txt(status));
		goto out2;
	}

	nb_written = pwritev(my_fd->fd, write_arg->iov, write_arg->iov_count,
			     write_arg->offset);

	if (nb_written == -1) {
		status = posix2fsal_status(errno);
		LogFullDebug(COMPONENT_FSAL,
			     "pwritev failed returning %s",
			     fsal_err_txt(status));
		goto out;
	}

	write_arg->io_amount = nb_written;

	if (write_arg->fsal_stable) {
		retval = fsync(my_fd->fd);
		if (retval == -1) {
			status2 = posix2fsal_status(errno);
			write_arg->fsal_stable = false;
			LogFullDebug(COMPONENT_FSAL,
				     "fsync returned %s",
				     fsal_err_txt(status2));
		}
	}

 out:

	vfs_restore_ganesha_credentials(obj_hdl->fsal);

 out2:

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
	fsal_status_t status, status2;
	struct fsal_attrlist attrs;
	struct vfs_fd *my_fd;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

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

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

	fsal_prepare_attrs(&attrs,
			   (op_ctx->fsal_export->exp_ops.fs_supported_attrs(
							op_ctx->fsal_export)
				& ~(ATTR_ACL | ATTR4_FS_LOCATIONS)));

	status = fetch_attrs(myself, my_fd->fd, &attrs);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			     "fetch_attrs failed returning %s",
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

	if (info->io_content.what == NFS4_CONTENT_DATA) {
		what = SEEK_DATA;
	} else if (info->io_content.what == NFS4_CONTENT_HOLE) {
		what = SEEK_HOLE;
	} else {
		status = fsalstat(ERR_FSAL_UNION_NOTSUPP, 0);
		goto out;
	}

	ret = lseek(my_fd->fd, offset, what);

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

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	/* We did FSAL_O_ANY so no share reservation was acquired */

 exit:

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
	fsal_status_t status, status2;
	struct vfs_fd *my_fd;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

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

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

	if (!vfs_set_credentials(&op_ctx->creds, obj_hdl->fsal)) {
		status = posix2fsal_status(EPERM);
		LogFullDebug(COMPONENT_FSAL,
			     "vfs_set_credentials failed returning %s",
			     fsal_err_txt(status));
		goto out;
	}

	ret = fallocate(my_fd->fd,
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
	fsal_status_t status, status2;
	int retval;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct vfs_fd *my_fd;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

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

	if (!vfs_set_credentials(&op_ctx->creds, obj_hdl->fsal)) {
		status = posix2fsal_status(EPERM);
		goto out;
	}

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

	retval = fsync(my_fd->fd);

	if (retval == -1)
		status = posix2fsal_status(errno);

	vfs_restore_ganesha_credentials(obj_hdl->fsal);

out:

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	/* We did not do share reservation stuff... */

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
	fsal_status_t status, status2;
	int retval = 0;
	bool bypass = false;
	fsal_openflags_t openflags = FSAL_O_RDWR;
	struct vfs_fd *my_fd;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		return posix2fsal_status(EXDEV);
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
		return posix2fsal_status(ERANGE);
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

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

	errno = 0;
	retval = fcntl(my_fd->fd, fcntl_comm, &lock_args);

	if (retval) {
		retval = errno;
		status = posix2fsal_status(retval);

		LogDebug(COMPONENT_FSAL,
			 "fcntl returned %d %s",
			 retval, strerror(retval));

		if (conflicting_lock != NULL) {
			/* Get the conflicting lock */
			int rc = fcntl(my_fd->fd, F_GETLK, &lock_args);

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
			   struct fsal_attrlist *attrs)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	fsal_status_t status2;
	struct vfs_fd *my_fd;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd = NULL;

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

	status = find_fd(&out_fd, obj_hdl, &temp_fd.fsal_fd, NULL, FSAL_O_ANY,
			 false);

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
		goto exit;
	}

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

#ifdef __FreeBSD__
 fetch:
#endif
	status = fetch_attrs(myself, my_fd->fd, attrs);

 out:

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

 exit:

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
			   struct fsal_attrlist *attrib_set)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_status_t status = {0, 0}, status2;
	int retval = 0;
	fsal_openflags_t openflags = FSAL_O_ANY;
	const char *func = "none";
	struct vfs_fd *my_fd;
	struct vfs_fd temp_fd = { FSAL_FD_INIT, -1 };
	struct fsal_fd *out_fd;

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
		struct fsal_attrlist attrs;

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
		openflags = FSAL_O_WRITE;
	}

	/* Get a usable file descriptor. Share conflict is only possible if
	 * size is being set.
	 */
	LogFullDebug(COMPONENT_FSAL, "Calling find_fd, state = %p", state);

	status = find_fd(&out_fd, obj_hdl, &temp_fd.fsal_fd, state, openflags,
			 bypass);

	LogFullDebug(COMPONENT_FSAL,
		     "find_fd, state = %p returned status %s using %p (tmp_fd = %p)",
		     state, fsal_err_txt(status), out_fd, &temp_fd);

	my_fd = container_of(out_fd, struct vfs_fd, fsal_fd);

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
		goto exit;
	}

	/** TRUNCATE **/
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
#ifdef LINUX
		/* On Linux at least off_t is signed and offsets < 0 are not
		 * valid for ftruncate. We want to make sure to return EFBIG
		 * not EINVAL.
		 */
		if ((off_t) attrib_set->filesize < 0) {
			errno = EFBIG;
			retval = -1;
			func = "truncate";
			LogDebug(COMPONENT_FSAL,
				 "filesize %"PRIx64" as off_t is < 0",
				 attrib_set->filesize);
			goto fileerr;
		}
#endif

		retval = ftruncate(my_fd->fd, attrib_set->filesize);

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

			retval = ftruncate(my_fd->fd, attrib_set->filesize);
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
					my_fd->fd,
					myself->u.unopenable.name,
					fsal2unix_mode(attrib_set->mode),
					0);
			else
				retval = fchmod(
					my_fd->fd,
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
			retval = fchownat(my_fd->fd, myself->u.unopenable.name,
					  user, group, AT_SYMLINK_NOFOLLOW);
		else if (obj_hdl->type == SYMBOLIC_LINK)
			retval = fchownat(my_fd->fd, "", user, group,
					  AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH);
		else
			retval = fchown(my_fd->fd, user, group);

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

		LogFullDebug(COMPONENT_FSAL,
			     "Setting atime %lx %lx mtime %lx %lx",
			     timebuf[0].tv_sec, timebuf[0].tv_nsec,
			     timebuf[1].tv_sec, timebuf[1].tv_nsec);

		if (vfs_unopenable_type(obj_hdl->type))
			retval = vfs_utimesat(my_fd->fd,
					      myself->u.unopenable.name,
					      timebuf, AT_SYMLINK_NOFOLLOW);
		else
			retval = vfs_utimes(my_fd->fd, timebuf);
		if (retval != 0) {
			func = "utimes";
			goto fileerr;
		}
	}

	/** SUBFSAL **/
	if (myself->sub_ops && myself->sub_ops->setattrs) {
		status = myself->sub_ops->setattrs(
					myself,
					my_fd->fd,
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

	status = posix2fsal_status(retval);

 out:

	status2 = fsal_complete_io(obj_hdl, out_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fsal_complete_io returned %s",
		     fsal_err_txt(status2));

	if (state == NULL && openflags != FSAL_O_ANY) {
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
	struct vfs_fd *my_fd = &container_of(state, struct vfs_state_fd,
					     state)->vfs_fd;

	myself = container_of(obj_hdl,
			      struct vfs_fsal_obj_handle,
			      obj_handle);

	if (state->state_type == STATE_TYPE_SHARE ||
	    state->state_type == STATE_TYPE_NLM_SHARE ||
	    state->state_type == STATE_TYPE_9P_FID) {
		/* This is a share state, we must update the share counters */
		update_share_counters_locked(obj_hdl, &myself->u.file.share,
					     my_fd->fsal_fd.openflags,
					     FSAL_O_CLOSED);
	}

	return close_fsal_fd(obj_hdl, &my_fd->fsal_fd, false);
}
