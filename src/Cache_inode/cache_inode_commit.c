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
 * \file    cache_inode_commit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.20 $
 * \brief   Commits an IO on a REGULAR_FILE.
 *
 * cache_inode_commit.c : Commits an IO on a REGULAR_FILE.
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
#include "nfs_core.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Commits a write operation to stable storage
 *
 * This function commits writes from unstable to stable storage.
 *
 * @param[in]  entry        File whose data should be committed
 * @param[in]  offset       Start of region to commit
 * @param[in]  count        Number of bytes to commit
 * @param[in]  typeofcommit What type of commit operation this is
 * @param[in]  context      FSAL credentials
 * @param[out] status       Operation status
 *
 * @return CACHE_INODE_SUCCESS or various errors
 */

cache_inode_status_t
cache_inode_commit(cache_entry_t *entry,
                   uint64_t offset,
                   size_t count,
                   cache_inode_stability_t stability,
                   fsal_op_context_t *context,
                   cache_inode_status_t *status)
{
     /* Number of bytes actually written */
     size_t bytes_moved = 0;
     /* Descriptor for unstable data */
     cache_inode_unstable_data_t *udata = NULL;
     /* Error return from FSAL operations*/
     fsal_status_t fsal_status = {0, 0};
     /* True if the content_lock is held */
     bool_t content_locked = FALSE;
     /* True if we opened our own file descriptor */
     bool_t opened = FALSE;

     if ((uint64_t)count > ~(uint64_t)offset)
         return NFS4ERR_INVAL;

     pthread_rwlock_rdlock(&entry->content_lock);
     content_locked = TRUE;

     /* Just in case the variable holds something funny when we're
        called. */
     *status = CACHE_INODE_SUCCESS;

     /* If we aren't using the Ganesha write buffer, then we're using
        the filesystem write buffer so execute a normal fsal_commit()
        call. */
     if (stability == CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER) {
          while (!is_open_for_write(entry)) {
               pthread_rwlock_unlock(&entry->content_lock);
               pthread_rwlock_wrlock(&entry->content_lock);
               if (!is_open_for_write(entry)) {
                    if (cache_inode_open(entry,
                                         FSAL_O_WRONLY,
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

          fsal_status = FSAL_commit(&(entry->object.file.open_fd.fd),
                                    offset,
                                    count);
          if (FSAL_IS_ERROR(fsal_status)) {
               LogMajor(COMPONENT_CACHE_INODE,
                        "fsal_commit() failed: fsal_status.major = %d",
                        fsal_status.major);

               *status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(entry);
                    goto out;
               }
               /* Close the FD if we opened it. No need to catch an
                  additional error form a close? */
               if (opened) {
                    cache_inode_close(entry,
                                      CACHE_INODE_FLAG_CONTENT_HAVE |
                                      CACHE_INODE_FLAG_CONTENT_HOLD,
                                      status);
                    opened = FALSE;
               }
               goto out;
          }
          /* Close the FD if we opened it. */
          if (opened) {
               if (cache_inode_close(entry,
                                     CACHE_INODE_FLAG_CONTENT_HAVE |
                                     CACHE_INODE_FLAG_CONTENT_HOLD,
                                     status) !=
                   CACHE_INODE_SUCCESS) {
                  LogEvent(COMPONENT_CACHE_INODE,
                          "cache_inode_commit: cache_inode_close = %d",
                          *status);
               }
          }
     } else {
          /* Ok, it looks like we're using the Ganesha write
           * buffer. This means we will either be writing to the
           * buffer, or writing a stable write to the file system if
           * the buffer is already full. */
          udata = &entry->object.file.unstable_data;
          if (udata->buffer == NULL) {
               *status = CACHE_INODE_SUCCESS;
               goto out;
          }
          if (count == 0 || count == 0xFFFFFFFFL) {
               /* Count = 0 means "flush all data to permanent storage */
               pthread_rwlock_unlock(&entry->content_lock);
               content_locked = FALSE;
               *status = cache_inode_rdwr(entry,
                                         CACHE_INODE_WRITE,
                                         offset,
                                         udata->length,
                                         &bytes_moved,
                                         udata->buffer,
                                         NULL,
                                         context,
                                         CACHE_INODE_SAFE_WRITE_TO_FS,
                                         status);
               if (status != CACHE_INODE_SUCCESS) {
                    goto out;
               }
               gsh_free(udata->buffer);
               udata->buffer = NULL;
          } else {
               if (offset < udata->offset) {
                    *status = CACHE_INODE_INVALID_ARGUMENT;
                    goto out;
               }

               cache_inode_rdwr(entry,
                                CACHE_INODE_WRITE,
                                offset,
                                count,
                                &bytes_moved,
                                (udata->buffer +
                                 offset - udata->offset),
                                NULL,
                                context,
                                CACHE_INODE_SAFE_WRITE_TO_FS,
                                status);
          }
     }

out:

     if (content_locked) {
          pthread_rwlock_unlock(&entry->content_lock);
     }

     return *status;
}
