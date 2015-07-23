/*
 * Copyright CEA/DAM/DIF  (2008)
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
 * ---------------------------------------
 */

/**
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file    cache_inode_open_close.c
 * @brief   Opening and closing files
 *
 * This file manages the opening and closing files and the file
 * descriptor cache in conjunction with the lru_thread in
 * cache_inode_lru.c
 */

#include "config.h"
#include "fsal.h"
#include "abstract_atomic.h"
#include "log.h"
#include "hashtable.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"

#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>
#include <assert.h>

/**
 * @brief Returns true if open in any form
 *
 * This function returns true if the object handle has an open/active
 * file descriptor or its equivalent stored,
 * tests if the cached file is open.
 *
 * @param[in] entry Entry for the file on which to operate
 *
 * @return true/false
 */

bool
is_open(cache_entry_t *entry)
{
	if ((entry == NULL) || (entry->obj_handle == NULL)
	    || (entry->type != REGULAR_FILE))
		return false;
	return (entry->obj_handle->obj_ops.status(entry->obj_handle) !=
		FSAL_O_CLOSED);
}

/**
 * @brief Check if a file is available to write
 *
 * This function checks whether the given file is currently open in a
 * mode supporting write operations.
 *
 * @param[in] entry Entry for the file to check
 *
 * @return true if the file is open for writes
 */

bool
is_open_for_write(cache_entry_t *entry)
{
	fsal_openflags_t openflags;

	if ((entry == NULL) || (entry->type != REGULAR_FILE))
		return false;
	openflags = entry->obj_handle->obj_ops.status(entry->obj_handle);
	return openflags & FSAL_O_WRITE;
}

/**
 * @brief Check if a file is available to read
 *
 * This function checks whether the given file is currently open in a
 * mode supporting read operations.
 *
 * @param[in] entry Entry for the file to check
 *
 * @return true if the file is opened for reads
 */

bool is_open_for_read(cache_entry_t *entry)
{
	fsal_openflags_t openflags;

	if ((entry == NULL) || (entry->type != REGULAR_FILE))
		return false;
	openflags = entry->obj_handle->obj_ops.status(entry->obj_handle);
	return openflags & FSAL_O_READ;
}

/**
 *
 * @brief Opens a file descriptor
 *
 * This function opens a file descriptor on a given cache entry.
 *
 * @param[in]  entry     Cache entry representing the file to open
 * @param[in]  openflags The type of access for which to open
 * @param[in]  flags     Flags indicating lock status
 *
 * @return CACHE_INODE_SUCCESS if successful, errors otherwise
 */

cache_inode_status_t
cache_inode_open(cache_entry_t *entry,
		 fsal_openflags_t openflags,
		 uint32_t flags)
{
	/* Error return from FSAL */
	fsal_status_t fsal_status = { 0, 0 };
	fsal_openflags_t current_flags;
	struct fsal_obj_handle *obj_hdl;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	struct fsal_export *fsal_export;
	bool closed;

	assert(entry->obj_handle != NULL);

	if (entry->type != REGULAR_FILE) {
		status = CACHE_INODE_BAD_TYPE;
		goto out;
	}

	if (!cache_inode_lru_fds_available()) {
		/* This seems the best idea, let the client try again later
		   after the reap. */
		status = CACHE_INODE_DELAY;
		goto out;
	}

	if (!(flags & CACHE_INODE_FLAG_CONTENT_HAVE))
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);

	obj_hdl = entry->obj_handle;
	current_flags = obj_hdl->obj_ops.status(obj_hdl);

	/* Filter out overloaded FSAL_O_RECLAIM */
	openflags &= ~FSAL_O_RECLAIM;

	/* Open file need to be closed, unless it is already open as
	 * read/write */
	if ((current_flags != FSAL_O_RDWR) && (current_flags != FSAL_O_CLOSED)
	    && (current_flags != openflags)) {
		/* If the FSAL has reopen method, we just use it instead
		 * of closing and opening the file again. This avoids
		 * losing any lock state due to closing the file!
		 */
		fsal_export = op_ctx->fsal_export;
		if (fsal_export->exp_ops.fs_supports(fsal_export,
						  fso_reopen_method)) {
			fsal_status = obj_hdl->obj_ops.reopen(obj_hdl,
							   openflags);
			closed = false;
		} else {
			fsal_status = obj_hdl->obj_ops.close(obj_hdl);
			closed = true;
		}
		if (FSAL_IS_ERROR(fsal_status)
		    && (fsal_status.major != ERR_FSAL_NOT_OPENED)) {
			status = cache_inode_error_convert(fsal_status);
			if (fsal_status.major == ERR_FSAL_STALE) {
				LogEvent(COMPONENT_CACHE_INODE,
					 "FSAL returned STALE on close.");
				cache_inode_kill_entry(entry);
			}

			LogDebug(COMPONENT_CACHE_INODE,
				 "cache_inode_open: returning %d(%s) from FSAL_close",
				 status,
				 cache_inode_err_str(status));

			goto unlock;
		}

		if (!FSAL_IS_ERROR(fsal_status) && closed)
			atomic_dec_size_t(&open_fd_count);

		/* Force re-openning */
		current_flags = obj_hdl->obj_ops.status(obj_hdl);
	}

	if (current_flags == FSAL_O_CLOSED) {
		fsal_status = obj_hdl->obj_ops.open(obj_hdl, openflags);
		if (FSAL_IS_ERROR(fsal_status)) {
			status = cache_inode_error_convert(fsal_status);
			LogDebug(COMPONENT_CACHE_INODE,
				 "cache_inode_open: returning %d(%s) from FSAL_open",
				 status,
				 cache_inode_err_str(status));
			if (fsal_status.major == ERR_FSAL_STALE) {
				LogEvent(COMPONENT_CACHE_INODE,
					 "FSAL returned STALE on open.");
				cache_inode_kill_entry(entry);
			}
			goto unlock;
		}

		/* This is temporary code, until Jim Lieb makes FSALs cache
		   their own file descriptors.  Under that regime, the LRU
		   thread will interrogate FSALs for their FD use. */
		atomic_inc_size_t(&open_fd_count);


		LogDebug(COMPONENT_CACHE_INODE,
			 "cache_inode_open: pentry %p: openflags = %d, open_fd_count = %zd",
			 entry, openflags,
			 atomic_fetch_size_t(&open_fd_count));
	}

	status = CACHE_INODE_SUCCESS;

unlock:
	if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD))
		PTHREAD_RWLOCK_unlock(&entry->content_lock);

out:
	return status;

}

static cache_inode_status_t check_open_permission(cache_entry_t *entry,
						  fsal_openflags_t openflags,
						  char **reason)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	fsal_accessflags_t access_mask = 0;

	if (openflags & FSAL_O_READ)
		access_mask |= FSAL_READ_ACCESS;

	if (openflags & FSAL_O_WRITE)
		access_mask |= FSAL_WRITE_ACCESS;

	status = cache_inode_access(entry, access_mask);

	if (status == CACHE_INODE_SUCCESS) {
		*reason = "";
		return status;
	}

	/* If non-permission error, return it. */
	if (status != CACHE_INODE_FSAL_EACCESS) {
		*reason = "cache_inode_access failed - ";
		return status;
	}

	/* If WRITE access is requested, return permission
	 * error
	 */
	if (openflags & FSAL_O_WRITE) {
		*reason = "cache_inode_access failed with WRITE_ACCESS - ";
		return status;
	}

	/* If just a permission error and file was opened read
	 * only, try execute permission.
	 */
	status = cache_inode_access(entry, FSAL_EXECUTE_ACCESS);

	if (status == CACHE_INODE_SUCCESS)
		*reason = "";
	else
		*reason = "cache_inode_access failed with EXECUTE_ACCESS - ";

	return status;
}

/**
 * @brief Verify an exclusive create replay when the file is already open.
 *
 * This may not be necessary in real life, however, pynfs definitely has a
 * test case that walks this path.
 *
 * @param[in]     entry      Cache entry for the opened file
 * @param[in]     verifier   Verifier to use with exclusive create
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t cache_inode_verify2(cache_entry_t *entry,
					 fsal_verifier_t verifier)
{
	cache_inode_status_t status;
	struct fsal_obj_handle *obj_handle = entry->obj_handle;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	status = cache_inode_refresh_attrs(entry);

	if (status == CACHE_INODE_ESTALE) {
		/* Entry doesn't exist, go create. */
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		return status;
	}

	if (!obj_handle->obj_ops.check_verifier(obj_handle,
						verifier)) {
		/* Verifier check failed. */
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		return CACHE_INODE_ENTRY_EXISTS;
	}

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	return CACHE_INODE_SUCCESS;
}

/**
 * @brief Opens a file by name or by handle.
 *
 * This function accomplishes both a LOOKUP if necessary and an open.
 *
 * Returns with an LRU reference held on the entry.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation).
 *
 * @param[in]     in_entry   Parent directory or entry
 * @param[in/out] state      state_t to operate on
 * @param[in]     openflags  Details of how to open the file
 * @param[in]     name       If name is not NULL, entry is the parent directory
 * @param[in]     attr       Attributes to set on the file
 * @param[in]     verifier   Verifier to use with exclusive create
 * @param[out]    entry      Cache entry for the opened file
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t cache_inode_open2(cache_entry_t *in_entry,
				       struct state_t *state,
				       fsal_openflags_t openflags,
				       enum fsal_create_mode createmode,
				       const char *name,
				       struct attrlist *attr,
				       fsal_verifier_t verifier,
				       cache_entry_t **entry)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_obj_handle *object_handle = NULL;
	struct fsal_obj_handle *obj_handle;
	bool parent_locked = false;
	bool attr_update = false;
	bool caller_perm_check = false;
	uint32_t cig_flags;
	char *reason;

	*entry = NULL;

	if (createmode >= FSAL_EXCLUSIVE && verifier == NULL) {
		status = CACHE_INODE_INVALID_ARGUMENT;
		goto out;
	}

	if (name == NULL) {
		if (createmode != FSAL_NO_CREATE) {
			status = CACHE_INODE_INVALID_ARGUMENT;
			goto out;
		}
		/* Get a reference to the entry. */
		status = cache_inode_lru_ref(in_entry, LRU_FLAG_NONE);
		if (status == CACHE_INODE_SUCCESS) {
			*entry = in_entry;
			goto got_entry;
		} else {
			/* return error */
			goto out;
		}
	}

	if (in_entry->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		goto out;
	}

	if (strcmp(name, ".") == 0 ||
	    strcmp(name, "..") == 0) {
		/* Can't open "." or ".."... */
		status = CACHE_INODE_IS_A_DIRECTORY;
		goto out;
	}

	/* Check directory permission for LOOKUP */
	status = cache_inode_access(in_entry, FSAL_EXECUTE_ACCESS);

	if (status != CACHE_INODE_SUCCESS)
		goto out;

	PTHREAD_RWLOCK_rdlock(&in_entry->content_lock);
	parent_locked = true;

	if (createmode >= FSAL_GUARDED) {
		/* If we are doing guarded or exclusive create and dirent
		 * exists, we will want to continue with finding instantiating
		 * the entry because if that dirent is valid, then we have to
		 * return an error to create.
		 */
		cig_flags = CIG_KEYED_FLAG_NONE;
	} else {
		/* Otherwise, we will deal with instantiating the entry
		 * during the open process to minimize FSAL calls.
		 */
		cig_flags = CIG_KEYED_FLAG_CACHED_ONLY;
	}

	status = cache_inode_find_by_name(in_entry, name, cig_flags, entry);

	if (status == CACHE_INODE_SUCCESS) {
		if (*entry == NULL) {
			/* Negative dirent condition */
			if (createmode == FSAL_NO_CREATE) {
				/* On non-create, we're done. */
				status = CACHE_INODE_NOT_FOUND;
				goto out;
			}
			/* On create, go ahead an create it */
			goto open_by_name;
		}

		if (createmode == FSAL_GUARDED) {
			/* Guarded create, with entry found... */
			status = CACHE_INODE_ENTRY_EXISTS;
			goto out;
		}

		if (createmode >= FSAL_EXCLUSIVE) {
			/* Exclusive create with entry found, check verifier */
			obj_handle = (*entry)->obj_handle;

			PTHREAD_RWLOCK_wrlock(&((*entry)->attr_lock));

			status = cache_inode_refresh_attrs(*entry);

			if (status == CACHE_INODE_ESTALE) {
				/* Entry doesn't exist, go create. */
				PTHREAD_RWLOCK_unlock(&((*entry)->attr_lock));
				goto open_by_name;
			}

			if (!obj_handle->obj_ops.check_verifier(obj_handle,
								verifier)) {
				/* Verifier check failed. */
				PTHREAD_RWLOCK_unlock(&((*entry)->attr_lock));
				status = CACHE_INODE_ENTRY_EXISTS;
				goto out;
			}

			PTHREAD_RWLOCK_unlock(&((*entry)->attr_lock));

			/* Else fall through to got_entry to allow FSAL to
			 * check the verifier for completeness (and complete
			 * the open if necessary)/
			 */
		}
		/* Go open the entry as requested */
		goto got_entry;
	} else if (status != CACHE_INODE_NOT_FOUND) {
		/* An error occurred */
		goto out;
	}

 open_by_name:

	obj_handle = in_entry->obj_handle;

	fsal_status = obj_handle->obj_ops.open2(obj_handle,
						state,
						openflags,
						createmode,
						name,
						attr,
						verifier,
						&object_handle,
						&caller_perm_check);

	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		if (status == CACHE_INODE_ESTALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE from a lookup.");
			cache_inode_kill_entry(in_entry);
		}
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "FSAL %d %s returned %s",
			     (int) op_ctx->export->export_id,
			     op_ctx->export->fullpath,
			     cache_inode_err_str(status));
		goto out;
	}

	LogFullDebug(COMPONENT_CACHE_INODE, "Creating entry for %s", name);

	/* Allocation of a new entry in the cache */
	status = cache_inode_new_entry_ex(object_handle, CACHE_INODE_FLAG_NONE,
					  entry, state);

	if (unlikely(*entry == NULL)) {
		/* Note that cache_inode_new_entry COULD have failed because
		 * there was a race between two open by name with conflicting
		 * share reservations. In that case, when cache_inode_new_entry
		 * called FSAL merge to merge the two object_handles that
		 * resulted, the second one will have failed with
		 * ERR_FSAL_SHARE_DENIED.
		 *
		 * Everyting will work correctly as expected.
		 *
		 * cache_inode_new_entry_ex will have closed the file.
		 */
		LogDebug(COMPONENT_CACHE_INODE,
			 "Closing file cache_inode_new_entry_ex failed - %s",
			 cache_inode_err_str(status));
		goto out;
	}

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Created entry %p FSAL %s for %s",
		     *entry, (*entry)->obj_handle->fsal->name, name);

	/* Add this entry to the parent directory */
	status = cache_inode_add_cached_dirent(in_entry, name, *entry, NULL);

	if (status == CACHE_INODE_ENTRY_EXISTS)
		status = CACHE_INODE_SUCCESS;
	else if (status != CACHE_INODE_SUCCESS) {
		reason = "cache_inode_add_cached_dirent failed - ";
		if (state == NULL)
			goto out;
		goto close_file;
	}

	if (!caller_perm_check)
		goto out;

	/* Do a permission check on the just opened file. */
	status = check_open_permission(*entry, openflags, &reason);

	if (status == CACHE_INODE_SUCCESS)
		goto out;

	reason = "check_open_permission failed - ";

 close_file:

	/* A failure occurred once we had the file open, we must ask the
	 * FSAL to close the file.
	 */
	LogDebug(COMPONENT_CACHE_INODE,
		 "Closing file %s%s",
		 reason, cache_inode_err_str(status));

	fsal_status = obj_handle->obj_ops.close2((*entry)->obj_handle, state);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* Just log but don't return this error (we want to
		 * preserve the error that got us here).
		 */
		LogDebug(COMPONENT_CACHE_INODE,
			 "FSAL close2 failed with %s",
			 cache_inode_err_str(
				cache_inode_error_convert(fsal_status)));
	}

	goto out;

 got_entry:

	/* Check type if not a create. */
	if ((*entry)->type == DIRECTORY) {
		status = CACHE_INODE_IS_A_DIRECTORY;
		goto out;
	} else if ((*entry)->type != REGULAR_FILE) {
		status = CACHE_INODE_BAD_TYPE;
		goto out;
	}

	obj_handle = (*entry)->obj_handle;

	/* Do a permission check on the file before opening. */
	status = check_open_permission(*entry, openflags, &reason);

	if (status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Not opening file file %s%s",
			 reason, cache_inode_err_str(status));
		goto out;
	}

	if (createmode != FSAL_EXCLUSIVE ||
	    (createmode == FSAL_UNCHECKED &&
	     attr != NULL &&
	     FSAL_TEST_MASK(attr->mask, ATTR_SIZE) &&
	     attr->filesize == 0)) {
		/* FSAL is going to refresh attributes. */
		PTHREAD_RWLOCK_wrlock(&((*entry)->attr_lock));
		LogDebug(COMPONENT_CACHE_INODE,
			 "UNCHECKED truncate");
		attr_update = true;
	}

	/* Open THIS entry, so name must be NULL. The attr are passed in case
	 * this is a create with size = 0. We pass the verifier because this
	 * might be an exclusive recreate replay and we want the FSAL to
	 * check the verifier.
	 */
	fsal_status = obj_handle->obj_ops.open2(obj_handle,
						state,
						openflags,
						createmode,
						NULL,
						attr,
						verifier,
						&object_handle,
						&caller_perm_check);

	status = cache_inode_error_convert(fsal_status);

	if (attr_update) {
		/* We tried to refresh attributes. */
		if (status == CACHE_INODE_SUCCESS ||
		    status == CACHE_INODE_ENTRY_EXISTS) {
			/* FSAL has refreshed attributes. */
			cache_inode_fixup_md(*entry);
		}
		PTHREAD_RWLOCK_unlock(&((*entry)->attr_lock));
	}

	if (status == CACHE_INODE_ESTALE) {
		/* The entry is stale, so kill it. */
		cache_inode_kill_entry(*entry);

		if (createmode != FSAL_NO_CREATE) {
			/* We thought the file existed and we would retry
			 * create, but it doesn't actually exist, so go
			 * back and create it.
			 */
			goto open_by_name;
		}

		/* We thought the file existed and tried to open, but failed,
		 * the entry was stale, but we should return ENOENT to caller
		 * if this was originally a case of open by name.
		 */
		if (name != NULL)
			status = CACHE_INODE_NOT_FOUND;
	}

 out:

	if (parent_locked)
		PTHREAD_RWLOCK_unlock(&in_entry->content_lock);

	if (status != CACHE_INODE_SUCCESS) {
		if (*entry != NULL)
			cache_inode_put(*entry);
		*entry = NULL;
	}

	return status;
}

/**
 * @brief Re-Opens a file by handle.
 *
 * This MAY be used to open a file the first time if there is no need for
 * open by name or create semantics.
 *
 * @param[in]     entry            Entry to operate on
 * @param[in/out] state            state_t to operate on
 * @param[in]     openflags        Details of how to open the file
 * @param[in]     check_permission Indicate if permission should be checked
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t cache_inode_reopen2(cache_entry_t *entry,
					 struct state_t *state,
					 fsal_openflags_t openflags,
					 bool check_permission)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	fsal_status_t fsal_status = { 0, 0 };
	char *reason = "FSAL reopen failed - ";

	if (check_permission) {
		/* Do a permission check on the file before re-opening. */
		status = check_open_permission(entry, openflags, &reason);

		if (status != CACHE_INODE_SUCCESS)
			goto out;
	}

	/* Re-open the entry in the FSAL.
	 */
	fsal_status = entry->obj_handle->obj_ops.reopen2(entry->obj_handle,
							 state,
							 openflags);

	status = cache_inode_error_convert(fsal_status);

 out:

	if (status == CACHE_INODE_ESTALE) {
		/* The entry is stale, so kill it. */
		cache_inode_kill_entry(entry);
	}

	if (status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "Not re-opening file file %s%s",
			 reason, cache_inode_err_str(status));
	}

	return status;
}

/**
 * @brief Close a file
 *
 * This function calls down to the FSAL to close the file.
 *
 * @param[in]  entry  Cache entry to close
 * @param[in]  flags  Flags for lock management
 *
 * @return CACHE_INODE_SUCCESS or errors on failure
 */

cache_inode_status_t
cache_inode_close(cache_entry_t *entry, uint32_t flags)
{
	/* Error return from the FSAL */
	fsal_status_t fsal_status;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	if (entry->type != REGULAR_FILE) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Entry %p File not a REGULAR_FILE", entry);
		status = CACHE_INODE_BAD_TYPE;
		goto out;
	}

	if (!(flags
	      & (CACHE_INODE_FLAG_CONTENT_HAVE | CACHE_INODE_FLAG_CLEANUP)))
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);

	/* If nothing is opened, do nothing */
	if (!is_open(entry)) {
		LogFullDebug(COMPONENT_CACHE_INODE, "Entry %p File not open",
			     entry);
		status = CACHE_INODE_SUCCESS;
		goto unlock;
	}

	/* If file is pinned, do not close it.  This should
	   be refined.  (A non return_on_close layout should not prevent
	   the file from closing.) */
	if (((flags & CACHE_INODE_FLAG_NOT_PINNED) == 0)
	    && cache_inode_is_pinned(entry)) {
		LogFullDebug(COMPONENT_CACHE_INODE, "Entry %p is pinned",
			     entry);
		status = CACHE_INODE_SUCCESS;
		goto unlock;
	}


	if (!cache_inode_lru_caching_fds()
	    || (flags & CACHE_INODE_FLAG_REALLYCLOSE)
	    || (entry->obj_handle->attrs->numlinks == 0)) {
		LogFullDebug(COMPONENT_CACHE_INODE, "Closing entry %p", entry);
		fsal_status = entry->obj_handle->
				obj_ops.close(entry->obj_handle);
		if (FSAL_IS_ERROR(fsal_status)
		    && (fsal_status.major != ERR_FSAL_NOT_OPENED)) {
			status = cache_inode_error_convert(fsal_status);
			if (fsal_status.major == ERR_FSAL_STALE &&
			    !(flags & CACHE_INODE_DONT_KILL)) {
				LogEvent(COMPONENT_CACHE_INODE,
					 "FSAL returned STALE on close.");
				cache_inode_kill_entry(entry);
			}
			LogCrit(COMPONENT_CACHE_INODE,
				"FSAL_close failed, returning %d(%s) for entry %p",
				status, cache_inode_err_str(status), entry);
			goto unlock;
		}
		if (!FSAL_IS_ERROR(fsal_status))
			atomic_dec_size_t(&open_fd_count);
	}

	status = CACHE_INODE_SUCCESS;

unlock:
	if (!(flags
	      & (CACHE_INODE_FLAG_CONTENT_HOLD | CACHE_INODE_FLAG_CLEANUP)))
		PTHREAD_RWLOCK_unlock(&entry->content_lock);

out:
	return status;
}

/**
 * @brief adjust open flags of a file
 *
 * This function adjusts the current mode of open flags by doing reopen.
 * If all open states that need write access are gone, this function
 * will do reopen and remove the write open flag by calling FSAL's
 * reopen method if present.
 *
 * @param[in]  entry  Cache entry to adjust open flags
 *
 * entry->state_lock should be held while calling this.
 *
 * NOTE: This currently adjusts only the write mode open flag!
 */
void cache_inode_adjust_openflags(cache_entry_t *entry)
{
	struct fsal_export *fsal_export = op_ctx->fsal_export;
	struct fsal_obj_handle *obj_hdl;
	fsal_openflags_t openflags;
	fsal_status_t fsal_status;

	if (entry->type != REGULAR_FILE) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Entry %p File not a REGULAR_FILE", entry);
		return;
	}

	/*
	 * If the file needs to be in write mode, we shouldn't downgrage.
	 * If the fsal doesn't support reopen method, we can't downgrade.
	 *
	 * Also, if we have an outstanding write delegation, we shouldn't
	 * downgrade!
	 */
	if (entry->object.file.share_state.share_access_write > 0 ||
	    entry->object.file.write_delegated ||
	    !fsal_export->exp_ops.fs_supports(fsal_export, fso_reopen_method))
		return;

	obj_hdl = entry->obj_handle;
	PTHREAD_RWLOCK_wrlock(&entry->content_lock);
	openflags = obj_hdl->obj_ops.status(obj_hdl);
	if (!(openflags & FSAL_O_WRITE)) /* nothing to downgrade */
		goto unlock;

	/*
	 * Open or a reopen requires either a WRITE or a READ mode flag.
	 * If the file is not already opened with READ mode, then we
	 * can't downgrade, but the file will eventually be closed later
	 * as no other NFSv4 open (or NFSv4 anonymous read) can race
	 * as cache inode entry's state lock is held until we close the
	 * file.
	 */
	if (!(openflags & FSAL_O_READ))
		goto unlock;

	openflags &= ~FSAL_O_WRITE;
	fsal_status = obj_hdl->obj_ops.reopen(obj_hdl, openflags);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogWarn(COMPONENT_CACHE_INODE,
			"fsal reopen method returned: %d(%d)",
			fsal_status.major, fsal_status.minor);
	}

unlock:
	PTHREAD_RWLOCK_unlock(&entry->content_lock);
}

/** @} */
