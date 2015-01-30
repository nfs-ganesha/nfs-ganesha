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
				 "cache_inode_open: returning %d(%s) from "
				 "FSAL_close", status,
				 cache_inode_err_str(status));

			goto unlock;
		}

		if (!FSAL_IS_ERROR(fsal_status) && closed)
			atomic_dec_size_t(&open_fd_count);

		/* Force re-openning */
		current_flags = obj_hdl->obj_ops.status(obj_hdl);
	}

	if ((current_flags == FSAL_O_CLOSED)) {
		fsal_status = obj_hdl->obj_ops.open(obj_hdl, openflags);
		if (FSAL_IS_ERROR(fsal_status)) {
			status = cache_inode_error_convert(fsal_status);
			LogDebug(COMPONENT_CACHE_INODE,
				 "cache_inode_open: returning %d(%s) from "
				 "FSAL_open", status,
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
			 "cache_inode_open: pentry %p: openflags = %d, "
			 "open_fd_count = %zd", entry, openflags,
			 atomic_fetch_size_t(&open_fd_count));
	}

	status = CACHE_INODE_SUCCESS;

unlock:
	if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD))
		PTHREAD_RWLOCK_unlock(&entry->content_lock);

out:
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
	    || (entry->obj_handle->attributes.numlinks == 0)) {
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
				"FSAL_close failed, returning %d(%s) for entry "
				"%p", status, cache_inode_err_str(status),
				entry);
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
