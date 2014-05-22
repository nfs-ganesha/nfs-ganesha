/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @file cache_inode_rdwr.c
 * @brief Performs I/O on regular files
 */

#include "config.h"
#include "fsal.h"

#include "log.h"
#include "hashtable.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "export_mgr.h"

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
 * @param[in]     req_ctx      FSAL credentials
 * @param[in]     sync         Whether the write is synchronous or not
 *
 * @return CACHE_INODE_SUCCESS or various errors
 */

cache_inode_status_t
cache_inode_rdwr_plus(cache_entry_t *entry,
		      cache_inode_io_direction_t io_direction,
		      uint64_t offset, size_t io_size,
		      size_t *bytes_moved, void *buffer,
		      bool *eof,
		      struct req_op_context *req_ctx,
		      bool *sync, struct io_info *info)
{
	/* Error return from FSAL calls */
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_obj_handle *obj_hdl = entry->obj_handle;
	/* Required open mode to successfully read or write */
	fsal_openflags_t openflags = FSAL_O_CLOSED;
	fsal_openflags_t loflags;
	/* True if we have taken the content lock on 'entry' */
	bool content_locked = false;
	/* True if we have taken the attribute lock on 'entry' */
	bool attributes_locked = false;
	/* TRUE if we opened a previously closed FD */
	bool opened = false;

	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	/* Set flags for a read or write, as appropriate */
	if (io_direction == CACHE_INODE_READ ||
	    io_direction == CACHE_INODE_READ_PLUS) {
		openflags = FSAL_O_READ;
	} else {
		struct export_perms *perms;

		/* Pretent that the caller requested sync (stable write)
		 * if the export has COMMIT option. Note that
		 * FSAL_O_SYNC is not always honored, so just setting
		 * FSAL_O_SYNC has no guaranty that this write will be
		 * a stable write.
		 */
		perms = &req_ctx->export->export_perms;
		if (perms->options & EXPORT_OPTION_COMMIT)
			*sync = true;
		openflags = FSAL_O_WRITE;
		if (*sync)
			openflags |= FSAL_O_SYNC;
	}

	assert(obj_hdl != NULL);

	/* IO is done only on REGULAR_FILEs */
	if (entry->type != REGULAR_FILE) {
		status =
		    entry->type ==
		    DIRECTORY ? CACHE_INODE_IS_A_DIRECTORY :
		    CACHE_INODE_BAD_TYPE;
		goto out;
	}

	/* Write through the FSAL.  We need a write lock only if we need
	   to open or close a file descriptor. */
	PTHREAD_RWLOCK_rdlock(&entry->content_lock);
	content_locked = true;
	loflags = obj_hdl->ops->status(obj_hdl);
	while ((!is_open(entry))
	       || (loflags && loflags != FSAL_O_RDWR && loflags != openflags)) {
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		loflags = obj_hdl->ops->status(obj_hdl);
		if ((!is_open(entry))
		    || (loflags && loflags != FSAL_O_RDWR
			&& loflags != openflags)) {
			status =
			    cache_inode_open(entry, openflags, req_ctx,
					     (CACHE_INODE_FLAG_CONTENT_HAVE |
					      CACHE_INODE_FLAG_CONTENT_HOLD));
			if (status != CACHE_INODE_SUCCESS)
				goto out;
			opened = true;
		}
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_rdlock(&entry->content_lock);
		loflags = obj_hdl->ops->status(obj_hdl);
	}

	/* Call FSAL_read or FSAL_write */
	if (io_direction == CACHE_INODE_READ) {
		fsal_status =
		    obj_hdl->ops->read(obj_hdl, req_ctx, offset, io_size,
				       buffer, bytes_moved, eof);
	} else if (io_direction == CACHE_INODE_READ_PLUS) {
		fsal_status =
		    obj_hdl->ops->read_plus(obj_hdl, req_ctx, offset, io_size,
					    buffer, bytes_moved, eof, info);
	} else {
		bool fsal_sync = *sync;
		if (io_direction == CACHE_INODE_WRITE)
			fsal_status =
			  obj_hdl->ops->write(obj_hdl, req_ctx, offset,
					      io_size, buffer, bytes_moved,
					      &fsal_sync);
		else
			fsal_status =
			  obj_hdl->ops->write_plus(obj_hdl, req_ctx, offset,
						   io_size, buffer,
						   bytes_moved, &fsal_sync,
						   info);
		/* Alright, the unstable write is complete. Now if it was
		   supposed to be a stable write we can sync to the hard
		   drive. */

		if (*sync && !(obj_hdl->ops->status(obj_hdl) & FSAL_O_SYNC)
		    && !fsal_sync && !FSAL_IS_ERROR(fsal_status)) {
			fsal_status = obj_hdl->ops->commit(obj_hdl, req_ctx,
							   offset, io_size);
		} else {
			*sync = fsal_sync;
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
		status = cache_inode_error_convert(fsal_status);

		if (fsal_status.major == ERR_FSAL_STALE) {
			cache_inode_kill_entry(entry);
			goto out;
		}

		if ((fsal_status.major != ERR_FSAL_NOT_OPENED)
		    && (obj_hdl->ops->status(obj_hdl) != FSAL_O_CLOSED)) {
			cache_inode_status_t cstatus;

			LogFullDebug(COMPONENT_CACHE_INODE,
				     "cache_inode_rdwr: CLOSING entry %p",
				     entry);
			PTHREAD_RWLOCK_unlock(&entry->content_lock);
			PTHREAD_RWLOCK_wrlock(&entry->content_lock);

			cstatus =
			    cache_inode_close(entry,
					      (CACHE_INODE_FLAG_REALLYCLOSE |
					       CACHE_INODE_FLAG_CONTENT_HAVE |
					       CACHE_INODE_FLAG_CONTENT_HOLD));

			if (cstatus != CACHE_INODE_SUCCESS) {
				LogCrit(COMPONENT_CACHE_INODE,
					"Error closing file in cache_inode_rdwr: %d.",
					cstatus);
			}
		}

		goto out;
	}

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "cache_inode_rdwr: inode/direct: io_size=%zu, "
		     "bytes_moved=%zu, offset=%" PRIu64, io_size, *bytes_moved,
		     offset);

	if (opened) {
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		status =
		    cache_inode_close(entry,
				      CACHE_INODE_FLAG_CONTENT_HAVE |
				      CACHE_INODE_FLAG_CONTENT_HOLD);
		if (status != CACHE_INODE_SUCCESS) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "cache_inode_rdwr: cache_inode_close = %d",
				 status);
			goto out;
		}
	}

	if (content_locked) {
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		content_locked = false;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	attributes_locked = true;
	if (io_direction == CACHE_INODE_WRITE ||
	    io_direction == CACHE_INODE_WRITE_PLUS) {
		status = cache_inode_refresh_attrs(entry, req_ctx);
		if (status != CACHE_INODE_SUCCESS)
			goto out;
	} else
		cache_inode_set_time_current(&obj_hdl->attributes.atime);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	attributes_locked = false;

	status = CACHE_INODE_SUCCESS;

 out:

	if (content_locked) {
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		content_locked = false;
	}

	if (attributes_locked) {
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		attributes_locked = false;
	}

	return status;
}

cache_inode_status_t
cache_inode_rdwr(cache_entry_t *entry,
		 cache_inode_io_direction_t io_direction,
		 uint64_t offset, size_t io_size,
		 size_t *bytes_moved, void *buffer,
		 bool *eof,
		 struct req_op_context *req_ctx,
		 bool *sync)
{
	return cache_inode_rdwr_plus(entry, io_direction, offset, io_size,
				bytes_moved, buffer, eof, req_ctx,
				sync, NULL);
}

/** @} */
