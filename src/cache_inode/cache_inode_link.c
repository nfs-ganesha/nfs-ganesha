/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   pilippe.deniel@cea.fr
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
 * @file    cache_inode_link.c
 * @brief   Creation of hard links
 */
#include "config.h"
#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief Links a new name to a file
 *
 * This function hard links a new name to an existing file.
 *
 * @param[in]  entry    The file to which to add the new name.  Must
 *                      not be a directory.
 * @param[in]  dest_dir The directory in which to create the new name
 * @param[in]  name     The new name to add to the file
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_BAD_TYPE either source or destination have
 *                              incorrect type
 * @retval CACHE_INODE_ENTRY_EXISTS entry of that name already exists
 *                                  in destination.
 */
cache_inode_status_t
cache_inode_link(cache_entry_t *entry,
		 cache_entry_t *dest_dir,
		 const char *name)
{
	fsal_status_t fsal_status = { 0, 0 };
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cache_inode_status_t status_ref_entry = CACHE_INODE_SUCCESS;
	cache_inode_status_t status_ref_dest_dir = CACHE_INODE_SUCCESS;

	/* The file to be hardlinked can't be a DIRECTORY */
	if (entry->type == DIRECTORY) {
		status = CACHE_INODE_BAD_TYPE;
		goto out;
	}

	/* Is the destination a directory? */
	if (dest_dir->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		goto out;
	}

	/* Rather than performing a lookup first, just try to make the
	   link and return the FSAL's error if it fails. */
	fsal_status =
	    entry->obj_handle->ops->link(entry->obj_handle,
					 dest_dir->obj_handle, name);
	status_ref_entry = cache_inode_refresh_attrs_locked(entry);
	status_ref_dest_dir =
	    cache_inode_refresh_attrs_locked(dest_dir);

	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		goto out;
	}

	status = status_ref_entry;
	if (status != CACHE_INODE_SUCCESS)
		goto out;

	status = status_ref_dest_dir;
	if (status != CACHE_INODE_SUCCESS)
		goto out;

	/* Add the new entry in the destination directory */
	PTHREAD_RWLOCK_wrlock(&dest_dir->content_lock);

	status = cache_inode_add_cached_dirent(dest_dir, name, entry, NULL);

	PTHREAD_RWLOCK_unlock(&dest_dir->content_lock);

 out:
	return status;
}

/** @} */
