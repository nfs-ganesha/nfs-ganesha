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
 * @file    cache_inode_commit.c
 * @brief   Commits operations on regular files
 */
#include "config.h"

#include "fsal.h"

#include "log.h"
#include "hashtable.h"
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
 * @param[in] entry        File whose data should be committed
 * @param[in] offset       Start of region to commit
 * @param[in] count        Number of bytes to commit
 *
 * @return CACHE_INODE_SUCCESS or various errors
 */

cache_inode_status_t
cache_inode_commit(cache_entry_t *entry, uint64_t offset, size_t count)
{
	/* Error return from FSAL operations */
	fsal_status_t fsal_status = { 0, 0 };
	/* True if the content_lock is held */
	bool content_locked = false;
	/* True if we opened our own file descriptor */
	bool opened = false;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cache_inode_status_t cstatus = CACHE_INODE_SUCCESS;

	if ((uint64_t) count > ~(uint64_t) offset)
		return CACHE_INODE_INVALID_ARGUMENT;

	PTHREAD_RWLOCK_rdlock(&entry->content_lock);
	content_locked = true;

	while (!is_open_for_write(entry)) {
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		if (!is_open_for_write(entry)) {
			status =
			    cache_inode_open(entry, FSAL_O_WRITE,
					     CACHE_INODE_FLAG_CONTENT_HAVE |
					     CACHE_INODE_FLAG_CONTENT_HOLD);
			if (status != CACHE_INODE_SUCCESS)
				goto out;
			opened = true;
		}
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_rdlock(&entry->content_lock);
	}

	fsal_status = entry->obj_handle->ops->commit(entry->obj_handle,
						     offset, count);

	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);

		LogMajor(COMPONENT_CACHE_INODE,
			 "fsal_commit() failed: fsal_status.major = %d, cache inode status = %s",
			 fsal_status.major, cache_inode_err_str(status));

		if (fsal_status.major == ERR_FSAL_STALE) {
			cache_inode_kill_entry(entry);
			goto out;
		}

		/* Close the FD if we opened it. No need to catch an
		   additional error form a close? */
		if (opened) {
			PTHREAD_RWLOCK_unlock(&entry->content_lock);
			PTHREAD_RWLOCK_wrlock(&entry->content_lock);
			cstatus =
			    cache_inode_close(entry,
					      CACHE_INODE_FLAG_CONTENT_HAVE |
					      CACHE_INODE_FLAG_CONTENT_HOLD);

			PTHREAD_RWLOCK_unlock(&entry->content_lock);
			opened = false;
			content_locked = false;
		}
		goto out;
	}

	/* Close the FD if we opened it. */
	if (opened) {
		PTHREAD_RWLOCK_unlock(&entry->content_lock);
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		cstatus =
		    cache_inode_close(entry,
				      CACHE_INODE_FLAG_CONTENT_HAVE |
				      CACHE_INODE_FLAG_CONTENT_HOLD);
		if (cstatus != CACHE_INODE_SUCCESS) {
			LogMajor(COMPONENT_CACHE_INODE,
				 "cache_inode_close = %s",
				 cache_inode_err_str(cstatus));
		}
	}

	PTHREAD_RWLOCK_unlock(&entry->content_lock);
	content_locked = false;

	/* In other case cache_inode_rdwr call FSAL_Commit */
	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	cstatus = cache_inode_refresh_attrs(entry);
	if (cstatus != CACHE_INODE_SUCCESS) {
		LogMajor(COMPONENT_CACHE_INODE,
			 "cache_inode_refresh_attrs = %s",
			 cache_inode_err_str(cstatus));
	}
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

 out:

	if (content_locked)
		PTHREAD_RWLOCK_unlock(&entry->content_lock);

	return status;
}

/** @} */
