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
 * @file    cache_inode_getattr.c
 * @brief   Gets the attributes for an entry.
 */
#include "config.h"
#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "nfs_exports.h"
#include "export_mgr.h"
#include "nfs_core.h"

/**
 * @brief Gets the attributes for a cached entry
 *
 * Gets the attributes for a cached entry. The FSAL attributes are
 * kept in a structure when the entry is added to the cache.  This
 * function locks and ensures the coherence of the attributes before
 * calling a user supplied function to process them.
 *
 * @param[in]     entry   Entry to be managed.
 * @param[in,out] opaque  Opaque pointer passed to callback
 * @param[in]     cb      User supplied callback
 *
 * @return Errors from cache_inode_lock_trust_attributes or the user
 *         supplied callback.
 *
 */
cache_inode_status_t
cache_inode_getattr(cache_entry_t *entry,
		    void *opaque,
		    cache_inode_getattr_cb_t cb)
{
	cache_inode_status_t status;
	struct gsh_export *junction_export;
	cache_entry_t *junction_entry;
	uint64_t mounted_on_fileid;

	/* Lock (and refresh if necessary) the attributes, copy them
	   out, and unlock. */
	status = cache_inode_lock_trust_attrs(entry, false);
	if (status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_CACHE_INODE, "Failed %s",
			 cache_inode_err_str(status));
		return status;
	}

	PTHREAD_RWLOCK_rdlock(&op_ctx->export->lock);

	if (entry == op_ctx->export->exp_root_cache_inode)
		mounted_on_fileid = op_ctx->export->exp_mounted_on_file_id;
	else
		mounted_on_fileid = entry->obj_handle->attributes.fileid;

	PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

	status = cb(opaque,
		    entry,
		    &entry->obj_handle->attributes,
		    mounted_on_fileid);

	if (status == CACHE_INODE_CROSS_JUNCTION) {
		PTHREAD_RWLOCK_rdlock(&op_ctx->export->lock);

		junction_export = entry->object.dir.junction_export;

		if (junction_export != NULL)
			get_gsh_export_ref(entry->object.dir.junction_export);

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
	}

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	if (status == CACHE_INODE_CROSS_JUNCTION) {
		/* Get the root of the export across the junction. */
		if (junction_export != NULL) {
			status = nfs_export_get_root_entry(junction_export,
							   &junction_entry);

			if (status != CACHE_INODE_SUCCESS) {
				LogMajor(COMPONENT_CACHE_INODE,
					 "Failed to get root for %s, id=%d, status = %s",
					 junction_export->fullpath,
					 junction_export->export_id,
					 cache_inode_err_str(status));
				/* Need to signal problem to callback */
				(void) cb(opaque, junction_entry, NULL, 0);
				return status;
			}
		} else {
			LogMajor(COMPONENT_CACHE_INODE,
				 "A junction became stale");
			status = CACHE_INODE_FSAL_ESTALE;
			/* Need to signal problem to callback */
			(void) cb(opaque, junction_entry, NULL, 0);
			return status;
		}

		/* Now call the callback again with that. */
		status =
		    cache_inode_getattr(junction_entry, opaque, cb);

		cache_inode_put(junction_entry);
		put_gsh_export(junction_export);
	}

	return status;
}

/**
 * @brief Gets the fileid of a cached entry
 *
 * Gets the filied for a cached entry.
 *
 * @param[in]  entry   Entry to be managed.
 * @param[out] fileid  The file ID.
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
cache_inode_status_t
cache_inode_fileid(cache_entry_t *entry,
		   uint64_t *fileid)
{
	cache_inode_status_t status;

	PTHREAD_RWLOCK_rdlock(&op_ctx->export->lock);

	if (entry == op_ctx->export->exp_root_cache_inode) {

		*fileid = op_ctx->export->exp_mounted_on_file_id;
		status = CACHE_INODE_SUCCESS;

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
	} else {
		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

		/* Lock (and refresh if necessary) the attributes, copy them
		   out, and unlock. */
		status = cache_inode_lock_trust_attrs(entry, false);

		if (status == CACHE_INODE_SUCCESS) {
			*fileid = entry->obj_handle->attributes.fileid;

			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		}
	}

	return status;
}

/**
 * @brief Gets the fsid of a cached entry
 *
 * This function gets the filied for a cached entry.
 *
 * @param[in]  entry   Entry to be managed.
 * @param[out] fsid    The FS ID.
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
cache_inode_status_t
cache_inode_fsid(cache_entry_t *entry,
		 fsal_fsid_t *fsid)
{
	cache_inode_status_t status = 0;

	/* Set the return default to CACHE_INODE_SUCCESS */
	status = CACHE_INODE_SUCCESS;

	/* Lock (and refresh if necessary) the attributes, copy them
	   out, and unlock. */
	status = cache_inode_lock_trust_attrs(entry, false);
	if (status != CACHE_INODE_SUCCESS)
		goto out;

	*fsid = entry->obj_handle->attributes.fsid;

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

 out:

	return status;
}

/**
 * @brief Gets the file size of a cached entry
 *
 * This function gets the file size for a cached entry.
 *
 * @param[in]  entry   Entry to be managed.
 * @param[out] size    The file ID.
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
cache_inode_status_t
cache_inode_size(cache_entry_t *entry,
		 uint64_t *size)
{
	cache_inode_status_t status = 0;

	/* Set the return default to CACHE_INODE_SUCCESS */
	status = CACHE_INODE_SUCCESS;

	/* Lock (and refresh if necessary) the attributes, copy them
	   out, and unlock. */
	status = cache_inode_lock_trust_attrs(entry, false);
	if (status != CACHE_INODE_SUCCESS)
		goto out;

	*size = entry->obj_handle->attributes.filesize;

	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

 out:
	return status;
}

/** @} */
