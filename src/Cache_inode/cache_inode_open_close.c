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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup Cache_inode Cache Inode
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
#include "HashTable.h"
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
     if (entry == NULL || entry->obj_handle == NULL
         || entry->type != REGULAR_FILE) {
          return false ;
     }
     return entry->obj_handle->ops->status(entry->obj_handle) != FSAL_O_CLOSED;
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

     if (entry == NULL ||
         entry->type != REGULAR_FILE) {
          return false;
     }
     openflags = entry->obj_handle->ops->status(entry->obj_handle);
     return (openflags == FSAL_O_RDWR) || (openflags == FSAL_O_WRITE);
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

bool
is_open_for_read(cache_entry_t *entry)
{
     fsal_openflags_t openflags;

     if (entry == NULL
         || entry->type != REGULAR_FILE) {
          return false;
     }
     openflags = entry->obj_handle->ops->status(entry->obj_handle);
     return (openflags == FSAL_O_RDWR) || (openflags == FSAL_O_READ);
}

/**
 *
 * @brief Opens a file descriptor
 *
 * This function opens a file descriptor on a given cache entry.
 *
 * @param[in]  entry     Cache entry representing the file to open
 * @param[in]  openflags The type of access for which to open
 * @param[in]  req_ctx   FSAL operation context
 * @param[in]  flags     Flags indicating lock status
 *
 * @return CACHE_INODE_SUCCESS if successful, errors otherwise
 */

cache_inode_status_t
cache_inode_open(cache_entry_t *entry,
                 fsal_openflags_t openflags,
                 struct req_op_context *req_ctx,
                 uint32_t flags)
{
     /* Error return from FSAL */
     fsal_status_t fsal_status = {0, 0};
     fsal_accessflags_t access_type = FSAL_O_CLOSED;
     fsal_openflags_t current_flags;
     struct fsal_obj_handle *obj_hdl;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;

     if ((entry == NULL) || (req_ctx == NULL)) {
          status = CACHE_INODE_INVALID_ARGUMENT;
          goto out;
     }

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

     if (openflags & FSAL_O_READ)
	     access_type |= FSAL_R_OK;
     if (openflags & FSAL_O_WRITE)
	     access_type |= FSAL_W_OK;

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HAVE)) {
          PTHREAD_RWLOCK_wrlock(&entry->content_lock);
     }

     /* access check but based on fsal_open_flags_t, not fsal_access_flags_t
      * this may be checked above but here is a last stop check.
      * Execute access not considered here.  Could fail execute opens.
      * FIXME: sort out access checks in callers.
      */
     obj_hdl = entry->obj_handle;
     fsal_status = obj_hdl->ops->test_access(obj_hdl,
					     req_ctx, access_type);
     if(FSAL_IS_ERROR(fsal_status)) {
	 status = cache_inode_error_convert(fsal_status);

	 LogDebug(COMPONENT_CACHE_INODE,
		  "returning %d(%s) from access check",
		  status, cache_inode_err_str(status));
	 goto unlock;
     }
     current_flags = obj_hdl->ops->status(obj_hdl);
     /* Open file need to be closed, unless it is already open as read/write */
     if ((current_flags != FSAL_O_RDWR) &&
         (current_flags != FSAL_O_CLOSED) &&
         (current_flags != openflags)) {
          fsal_status = obj_hdl->ops->close(obj_hdl);
          if (FSAL_IS_ERROR(fsal_status) &&
              (fsal_status.major != ERR_FSAL_NOT_OPENED)) {
               status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
		    LogEvent(COMPONENT_CACHE_INODE,
			"FSAL returned STALE on close.");
                    cache_inode_kill_entry(entry);
               }

               LogDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_open: returning %d(%s) from FSAL_close",
                        status, cache_inode_err_str(status));

               goto unlock;
          }

          if (!FSAL_IS_ERROR(fsal_status))
              atomic_dec_size_t(&open_fd_count);

          /* Force re-openning */
	  current_flags = obj_hdl->ops->status(obj_hdl);
     }

     if ((current_flags == FSAL_O_CLOSED)) {
	  fsal_status = obj_hdl->ops->open(obj_hdl, req_ctx, openflags);
          if (FSAL_IS_ERROR(fsal_status)) {
               status = cache_inode_error_convert(fsal_status);
               LogDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_open: returning %d(%s) from FSAL_open",
                        status, cache_inode_err_str(status));
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
                   open_fd_count);
     }

     status = CACHE_INODE_SUCCESS;

unlock:

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
          PTHREAD_RWLOCK_unlock(&entry->content_lock);
     }

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
cache_inode_close(cache_entry_t *entry,
                  uint32_t flags)
{
     /* Error return from the FSAL */
     fsal_status_t fsal_status;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;

     if ((entry == NULL)) {
          status = CACHE_INODE_INVALID_ARGUMENT;
          goto out;
     }

     if (entry->type != REGULAR_FILE) {
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "Entry %p File not a REGULAR_FILE", entry);
          status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HAVE)) {
          PTHREAD_RWLOCK_wrlock(&entry->content_lock);
     }

     /* If nothing is opened, do nothing */
     if ( !is_open(entry)) {
          if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
               PTHREAD_RWLOCK_unlock(&entry->content_lock);
          }
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "Entry %p File not open", entry);
          status = CACHE_INODE_SUCCESS;
          return status;
     }

     /* If file is pinned, do not close it.  This should
        be refined.  (A non return_on_close layout should not prevent
        the file from closing.) */
     if (((flags & CACHE_INODE_FLAG_NOT_PINNED) == 0) &&
         cache_inode_is_pinned(entry)) {
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "Entry %p is pinned", entry);
          status = CACHE_INODE_SUCCESS;
          goto unlock;
     }

     if (!cache_inode_lru_caching_fds() ||
         (flags & CACHE_INODE_FLAG_REALLYCLOSE)) {
          LogFullDebug(COMPONENT_CACHE_INODE,
                   "Closing entry %p", entry);
	  fsal_status = entry->obj_handle->ops->close(entry->obj_handle);
          if (FSAL_IS_ERROR(fsal_status) &&
              (fsal_status.major != ERR_FSAL_NOT_OPENED)) {
               status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
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

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
          PTHREAD_RWLOCK_unlock(&entry->content_lock);
     }

out:

     return status;
}
/** @} */
