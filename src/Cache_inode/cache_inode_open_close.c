/**
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
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
 *
 * @file    cache_inode_open_close.c
 * @brief   Manage opening and closing files and caching file
 *          descriptors
 *
 * This file manages the opening and closing files and the file
 * descriptor cache, in conjunction with the lru_thread in
 * cache_inode_lru.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal.h"

#include "abstract_atomic.h"
#include "log.h"
#include "HashData.h"
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
 * @brief Returns a file descriptor, if open
 *
 * This function returns the file descriptor stored in a cache entry,
 * if the cached file is open.
 *
 * @param[in] entry Entry for the file on which to operate
 *
 * @return A pointer to a file descriptor or NULL if the entry is
 *         closed.
 */

fsal_file_t *
cache_inode_fd(cache_entry_t *entry)
{
     if (entry == NULL) {
          return NULL;
     }

     if (entry->type != REGULAR_FILE) {
          return NULL;
     }

     if (entry->object.file.open_fd.openflags != FSAL_O_CLOSED) {
          return &entry->object.file.open_fd.fd;
     }

     return NULL;
}

/**
 * @brief Check if a file is available to write
 *
 * This function checks whether the given file is currently open in a
 * mode supporting write operations.
 *
 * @param[in] entry Entry for the file to check
 *
 * @return TRUE if the file is open for writes
 */

bool_t
is_open_for_write(cache_entry_t *entry)
{
     return
          (entry &&
           (entry->type == REGULAR_FILE) &&
           ((entry->object.file.open_fd.openflags == FSAL_O_RDWR) ||
            (entry->object.file.open_fd.openflags == FSAL_O_WRONLY)));
}

/**
 * @brief Check if a file is available to read
 *
 * This function checks whether the given file is currently open in a
 * mode supporting read operations.
 *
 * @param[in] entry Entry for the file to check
 *
 * @return TRUE if the file is opened for reads
 */

bool_t
is_open_for_read(cache_entry_t *entry)
{
     return
          (entry &&
           (entry->type == REGULAR_FILE) &&
           ((entry->object.file.open_fd.openflags == FSAL_O_RDWR) ||
            (entry->object.file.open_fd.openflags == FSAL_O_RDONLY)));
}

/**
 *
 * @brief Opens a file descriptor
 *
 * This function opens a file descriptor on a given cache entry.
 *
 * @param[in]  entry     Cache entry representing the file to open
 * @param[in]  openflags The type of access for which to open
 * @param[in]  context   FSAL operation context
 * @param[in]  flags     Flags indicating lock status
 * @param[out] status    Operation status
 *
 * @return CACHE_INODE_SUCCESS if successful, errors otherwise
 */

cache_inode_status_t
cache_inode_open(cache_entry_t *entry,
                 fsal_openflags_t openflags,
                 fsal_op_context_t *context,
                 uint32_t flags,
                 cache_inode_status_t *status)
{
     /* Error return from FSAL */
     fsal_status_t fsal_status = {0, 0};

     if ((entry == NULL) || (context == NULL) ||
         (status == NULL)) {
          *status = CACHE_INODE_INVALID_ARGUMENT;
          goto out;
     }

     if (entry->type != REGULAR_FILE) {
          *status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if (!cache_inode_lru_fds_available()) {
          /* This seems the best idea, let the client try again later
             after the reap. */
          *status = CACHE_INODE_DELAY;
          goto out;
     }

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HAVE)) {
          pthread_rwlock_wrlock(&entry->content_lock);
     }

     /* Open file need to be closed, unless it is already open as read/write */
     if ((entry->object.file.open_fd.openflags != FSAL_O_RDWR) &&
         (entry->object.file.open_fd.openflags != 0) &&
         (entry->object.file.open_fd.openflags != openflags)) {
          fsal_status = FSAL_close(&(entry->object.file.open_fd.fd));
          if (FSAL_IS_ERROR(fsal_status) &&
              (fsal_status.major != ERR_FSAL_NOT_OPENED)) {
               *status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    LogEvent(COMPONENT_CACHE_INODE,
                       "FSAL returned STALE on close.");
                    cache_inode_kill_entry(entry);
               }

               LogDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_open: returning %d(%s) from FSAL_close",
                        *status, cache_inode_err_str(*status));

               goto unlock;
          }

          if (cache_inode_gc_policy.use_fd_cache && !FSAL_IS_ERROR(fsal_status))
              atomic_dec_size_t(&open_fd_count);

          /* Force re-openning */
          entry->object.file.open_fd.openflags = FSAL_O_CLOSED;
     }

     if ((entry->object.file.open_fd.openflags == FSAL_O_CLOSED)) {
          fsal_status = FSAL_open(&(entry->handle),
                                  context,
                                  openflags,
                                  &entry->object.file.open_fd.fd,
                                  NULL);
          if (FSAL_IS_ERROR(fsal_status)) {
               *status = cache_inode_error_convert(fsal_status);
               LogDebug(COMPONENT_CACHE_INODE,
                        "cache_inode_open: returning %d(%s) from FSAL_open",
                        *status, cache_inode_err_str(*status));
               if (fsal_status.major == ERR_FSAL_STALE) {
                    LogEvent(COMPONENT_CACHE_INODE,
                       "FSAL returned STALE on open.");
                    cache_inode_kill_entry(entry);
               }
               goto unlock;
          }

          entry->object.file.open_fd.openflags = openflags;
          /* This is temporary code, until Jim Lieb makes FSALs cache
             their own file descriptors.  Under that regime, the LRU
             thread will interrogate FSALs for their FD use. */
          if (cache_inode_gc_policy.use_fd_cache)
              atomic_inc_size_t(&open_fd_count);

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_open: pentry %p: openflags = %d, "
                   "open_fd_count = %zd", entry, openflags,
                   open_fd_count);
     }

     *status = CACHE_INODE_SUCCESS;

unlock:

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
          pthread_rwlock_unlock(&entry->content_lock);
     }

out:

     return *status;

} /* cache_inode_open */

/**
 * @brief Close a file
 *
 * This function calls down to the FSAL to close the file.
 *
 * @param[in]  entry  Cache entry to close
 * @param[in]  flags  Flags for lock management
 * @param[out] status Operation status
 *
 * @return CACHE_INODE_SUCCESS or errors on failure
 */

cache_inode_status_t
cache_inode_close(cache_entry_t *entry,
                  uint32_t flags,
                  cache_inode_status_t *status)
{
     /* Error return from the FSAL */
     fsal_status_t fsal_status;

     if ((entry == NULL) || (status == NULL)) {
          *status = CACHE_INODE_INVALID_ARGUMENT;
          goto out;
     }

     if (entry->type != REGULAR_FILE) {
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "Entry %p File not a REGULAR_FILE", entry);
          *status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HAVE)) {
          pthread_rwlock_wrlock(&entry->content_lock);
     }

     /* If nothing is opened, do nothing */
     if (entry->object.file.open_fd.openflags == FSAL_O_CLOSED) {
          if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
               pthread_rwlock_unlock(&entry->content_lock);
          }
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "Entry %p File not open", entry);
          *status = CACHE_INODE_SUCCESS;
          return *status;
     }

     /* If file is pinned, do not close it.  This should
        be refined.  (A non return_on_close layout should not prevent
        the file from closing.) */
     if (((flags & CACHE_INODE_FLAG_NOT_PINNED) == 0) &&
         cache_inode_is_pinned(entry)) {
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "Entry %p is pinned", entry);
          *status = CACHE_INODE_SUCCESS;
          goto unlock;
     }

     if (!cache_inode_lru_caching_fds() ||
         (flags & CACHE_INODE_FLAG_REALLYCLOSE)) {
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "Closing entry %p", entry);
          fsal_status = FSAL_close(&(entry->object.file.open_fd.fd));

          entry->object.file.open_fd.openflags = FSAL_O_CLOSED;
          if (FSAL_IS_ERROR(fsal_status) &&
              (fsal_status.major != ERR_FSAL_NOT_OPENED)) {
               *status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(entry);
               }
               LogCrit(COMPONENT_CACHE_INODE,
                       "FSAL_close failed, returning %d(%s) for entry %p",
                       *status, cache_inode_err_str(*status), entry);
               goto unlock;
          }
          if (cache_inode_gc_policy.use_fd_cache && !FSAL_IS_ERROR(fsal_status))
              atomic_dec_size_t(&open_fd_count);
     }

     *status = CACHE_INODE_SUCCESS;

unlock:

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
          pthread_rwlock_unlock(&entry->content_lock);
     }

out:

     return *status;
}
