/*
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
 */

/**
 * \file    cache_inode_rdwr.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.20 $
 * \brief   Removes an entry of any type.
 *
 * cache_inode_rdwr.c : performs an IO on a REGULAR_FILE.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal.h"

#include "LRU_List.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_core.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Reads/Writes through the cache layer
 *
 * This function performs I/O, either using the Ganesha in-memory or
 * disk cache or through the FSAL directly.  The caller MUST NOT hold
 * either the content or attribute locks when calling this function.
 *
 * @param[in]     entry        File to be read or written
 * @param[in]     io_direction Whether this is a read or a write
 * @param[in]     offset       Absolute file position for I/O
 * @param[in]     io_size      Amount of data to be read or written
 * @param[out]    bytes_moved  The length of data successfuly read or written
 * @param[in,out] buffer       Where in memory to read or write data
 * @param[out]    eof          Whether a READ encountered the end of file.  May
 *                             be NULL for writes.
 * @param[in]     context      FSAL credentials
 * @param[in]     stable       The stability of the write to perform
 * @param[out]    status       Status of operation
 *
 * @return CACHE_INODE_SUCCESS or various errors
 */

cache_inode_status_t
cache_inode_rdwr(cache_entry_t *entry,
                 cache_inode_io_direction_t io_direction,
                 uint64_t offset,
                 size_t io_size,
                 size_t *bytes_moved,
                 void *buffer,
                 bool_t *eof,
                 fsal_op_context_t *context,
                 cache_inode_stability_t stable,
                 cache_inode_status_t *status)
{
     /* Error return from FSAL calls */
     fsal_status_t fsal_status = {0, 0};
     /* Required open mode to successfully read or write */
     fsal_openflags_t openflags = FSAL_O_CLOSED;
     fsal_openflags_t loflags;
     /* TRUE if we have taken the content lock on 'entry' */
     bool_t content_locked = FALSE;
     /* TRUE if we have taken the attribute lock on 'entry' */
     bool_t attributes_locked = FALSE;
     /* TRUE if we opened a previously closed FD */
     bool_t opened = FALSE;
     /* We need this until Jim Lieb redoes the FSAL interface.  But
        there's no reason to make users of cache_inode deal with it. */
     fsal_seek_t seek_descriptor = {
          .whence = FSAL_SEEK_SET,
          .offset = offset
     };

     /* Set flags for a read or write, as appropriate */
     if (io_direction == CACHE_INODE_READ) {
          openflags = FSAL_O_RDONLY;
     } else {
          openflags = FSAL_O_WRONLY;
          if (stable == CACHE_INODE_SAFE_WRITE_TO_FS)
             openflags |= FSAL_O_SYNC;
     }

     /* IO is done only on REGULAR_FILEs */
     if (entry->type != REGULAR_FILE) {
          *status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if (stable == CACHE_INODE_UNSAFE_WRITE_TO_GANESHA_BUFFER) {
          /* Write to memory */
          pthread_rwlock_wrlock(&entry->attr_lock);
          attributes_locked = TRUE;
          pthread_rwlock_wrlock(&entry->content_lock);
          content_locked = TRUE;

          /* Is the unstable_data buffer allocated? */
          if ((entry->object.file.unstable_data.buffer == NULL) &&
              (io_size <= CACHE_INODE_UNSTABLE_BUFFERSIZE)) {
               if ((entry->object.file.unstable_data.buffer =
                    gsh_malloc(CACHE_INODE_UNSTABLE_BUFFERSIZE)) == NULL) {
                    *status = CACHE_INODE_MALLOC_ERROR;
                    LogEvent(COMPONENT_CACHE_INODE,
                             "cache_inode_rdwr: malloc failed");
                    goto out;
               }

               entry->object.file.unstable_data.offset = offset;
               entry->object.file.unstable_data.length = io_size;

               memcpy(entry->object.file.unstable_data.buffer,
                      buffer, io_size);

               cache_inode_set_time_current(&entry->attributes.mtime);
               *bytes_moved = io_size;
          } else {
               if ((entry->object.file.unstable_data.offset < offset) &&
                   (io_size + offset < CACHE_INODE_UNSTABLE_BUFFERSIZE)) {
                    entry->object.file.unstable_data.length =
                         io_size + offset;
                    memcpy(entry->object.file.unstable_data.buffer +
                           offset, buffer, io_size);

                    cache_inode_set_time_current(&entry->attributes.mtime);
                    *bytes_moved = io_size;
               } else {
                    /* Go back to stable writes */
                    stable = CACHE_INODE_SAFE_WRITE_TO_FS;
               }
          }
          if (content_locked) {
               pthread_rwlock_unlock(&entry->content_lock);
               content_locked = FALSE;
          }
          if (attributes_locked) {
               pthread_rwlock_unlock(&entry->attr_lock);
               attributes_locked = FALSE;
          }
     }

     if (stable == CACHE_INODE_SAFE_WRITE_TO_FS ||
         stable == CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER) {
          /* Write through the FSAL.  We need a write lock only
             if we need to open or close a file descriptor. */
          pthread_rwlock_rdlock(&entry->content_lock);
          content_locked = TRUE;
          loflags = entry->object.file.open_fd.openflags;
          while ((!cache_inode_fd(entry)) ||
                 (loflags && loflags != FSAL_O_RDWR && loflags != openflags)) {
               pthread_rwlock_unlock(&entry->content_lock);
               pthread_rwlock_wrlock(&entry->content_lock);
               loflags = entry->object.file.open_fd.openflags;
               if ((!cache_inode_fd(entry)) ||
                   (loflags && loflags != FSAL_O_RDWR &&
                    loflags != openflags)) {
                    if (cache_inode_open(entry,
                                         openflags,
                                         context,
                                         CACHE_INODE_FLAG_CONTENT_HAVE |
                                         CACHE_INODE_FLAG_CONTENT_HOLD,
                                         status) != CACHE_INODE_SUCCESS) {
                         goto out;
                    }
                    opened = TRUE;
               }
               pthread_rwlock_unlock(&entry->content_lock);
               pthread_rwlock_rdlock(&entry->content_lock);
          }

          /* Call FSAL_read or FSAL_write */
          if (io_direction == CACHE_INODE_READ) {
               fsal_status
                    = FSAL_read(&(entry->object.file.open_fd.fd),
                                &seek_descriptor,
                                io_size,
                                buffer,
                                bytes_moved,
                                eof);
          } else {
               fsal_status
                    = FSAL_write(&(entry->object.file.open_fd.fd),
                                 context,
                                 &seek_descriptor,
                                 io_size,
                                 buffer,
                                 bytes_moved);

               /* Alright, the unstable write is complete. Now if it was
                  supposed to be a stable write we can sync to the hard
                  drive. */

               if (!FSAL_IS_ERROR(fsal_status) &&
                   stable == CACHE_INODE_SAFE_WRITE_TO_FS &&
                   !(entry->object.file.open_fd.openflags & FSAL_O_SYNC)) {
                    fsal_status
                         = FSAL_commit(&(entry->object.file.open_fd.fd),
                                  offset, io_size);
               }
          }

          LogFullDebug(COMPONENT_FSAL,
                       "cache_inode_rdwr: FSAL IO operation returned "
                       "%d, asked_size=%zu, effective_size=%zu",
                       fsal_status.major, io_size, *bytes_moved);

          if (FSAL_IS_ERROR(fsal_status)) {
               if (fsal_status.major == ERR_FSAL_DELAY) {
                    LogEvent(COMPONENT_CACHE_INODE,
                             "cache_inode_rdwr: FSAL_write "
                             " returned EBUSY");
               } else {
                    LogDebug(COMPONENT_CACHE_INODE,
                             "cache_inode_rdwr: fsal_status.major = %d",
                             fsal_status.major);
               }

               *bytes_moved = 0;
               *status = cache_inode_error_convert(fsal_status);

               if (fsal_status.major == ERR_FSAL_STALE) {
                    LogEvent(COMPONENT_CACHE_INODE,
                             "FSAL returned STALE, entry killed");
                    cache_inode_kill_entry(entry);
                    goto out;
               }

               if ((fsal_status.major != ERR_FSAL_NOT_OPENED)
                   && (entry->object.file.open_fd.openflags
                       != FSAL_O_CLOSED)) {
                    cache_inode_status_t cstatus;
                    LogFullDebug(COMPONENT_CACHE_INODE,
                                 "cache_inode_rdwr: CLOSING entry %p",
                                 entry);
                    pthread_rwlock_unlock(&entry->content_lock);
                    pthread_rwlock_wrlock(&entry->content_lock);
                    cache_inode_close(entry,
                                      (CACHE_INODE_FLAG_REALLYCLOSE |
                                       CACHE_INODE_FLAG_CONTENT_HAVE |
                                       CACHE_INODE_FLAG_CONTENT_HOLD),
                                       &cstatus);

                    if (cstatus != CACHE_INODE_SUCCESS) {
                        LogCrit(COMPONENT_CACHE_INODE_LRU,
                                "Error closing file in cache_inode_rdwr: %d.",
                                cstatus); 
                    }
               }

               goto out;
          }

          LogFullDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_rdwr: inode/direct: io_size=%zu, "
                       "bytes_moved=%zu, offset=%"PRIu64,
                       io_size, *bytes_moved, offset);

          if (opened) {
               if (cache_inode_close(entry,
                                     CACHE_INODE_FLAG_CONTENT_HAVE |
                                     CACHE_INODE_FLAG_CONTENT_HOLD,
                                     status) != CACHE_INODE_SUCCESS) {
                    LogEvent(COMPONENT_CACHE_INODE,
                             "cache_inode_rdwr: cache_inode_close = %d",
                             *status);
                    goto out;
               }
          }

          if (content_locked) {
               pthread_rwlock_unlock(&entry->content_lock);
               content_locked = FALSE;
          }
     }

     pthread_rwlock_wrlock(&entry->attr_lock);
     attributes_locked = TRUE;
     if (io_direction == CACHE_INODE_WRITE) {
          if ((*status = cache_inode_refresh_attrs(entry,
                                                   context))
              != CACHE_INODE_SUCCESS) {
               goto out;
          }
     } else {
          cache_inode_set_time_current(&entry->attributes.atime);
     }
     pthread_rwlock_unlock(&entry->attr_lock);
     attributes_locked = FALSE;

     *status = CACHE_INODE_SUCCESS;

out:

     if (content_locked) {
          pthread_rwlock_unlock(&entry->content_lock);
          content_locked = FALSE;
     }

     if (attributes_locked) {
          pthread_rwlock_unlock(&entry->attr_lock);
          attributes_locked = FALSE;
     }

     return *status;
} /* cache_inode_rdwr */
