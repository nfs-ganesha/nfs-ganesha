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
 * @defgroup Cache_inode Cache Inode
 * @{
 */

/**
 * @file    cache_inode_getattr.c
 * @brief   Gets the attributes for an entry.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Gets the attributes for a cached entry
 *
 * Gets the attributes for a cached entry. The FSAL attributes are
 * kept in a structure when the entry is added to the cache.  This
 * function locks and ensures the coherence of the attributes before
 * calling a user supplied function to process them.
 *
 * @param[in]     entry   Entry to be managed.
 * @param[in]     req_ctx Request context(user creds, client address etc)
 * @param[in,out] opaque  Opaque pointer passed to callback
 * @param[in]     cb      User supplied callback
 *
 * @return Errors from cache_inode_lock_trust_attributes or the user
 *         supplied callback.
 *
 */
cache_inode_status_t
cache_inode_getattr(cache_entry_t *entry,
		    const struct req_op_context *req_ctx,
		    void *opaque,
		    cache_inode_getattr_cb_t cb)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	if (entry == NULL) {
		status = CACHE_INODE_INVALID_ARGUMENT;
                LogDebug(COMPONENT_CACHE_INODE,
                         "cache_inode_getattr: returning "
                         "CACHE_INODE_INVALID_ARGUMENT because of bad arg");
                goto out;
        }

        /* Set the return default to CACHE_INODE_SUCCESS */
	status = CACHE_INODE_SUCCESS;

        /* Lock (and refresh if necessary) the attributes, copy them
           out, and unlock. */

        if ((status
             = cache_inode_lock_trust_attrs(entry, req_ctx))
            != CACHE_INODE_SUCCESS) {
                goto out;
        }

        status = cb(opaque, &entry->obj_handle->attributes);

        PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:

        return status;
}

/**
 * @brief Gets the fileid of a cached entry
 *
 * Gets the filied for a cached entry.
 *
 * @param[in]  entry   Entry to be managed.
 * @param[in]  req_ctx Request context(user creds, client address etc)
 * @param[out] fileid  The file ID.
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
cache_inode_status_t
cache_inode_fileid(cache_entry_t *entry,
                   const struct req_op_context *req_ctx,
                   uint64_t *fileid)
{
        cache_inode_status_t status = 0;
        if (entry == NULL) {
                status = CACHE_INODE_INVALID_ARGUMENT;
                LogDebug(COMPONENT_CACHE_INODE,
                         "cache_inode_getattr: returning "
                         "CACHE_INODE_INVALID_ARGUMENT because of bad arg");
                goto out;
        }

        /* Set the return default to CACHE_INODE_SUCCESS */
        status = CACHE_INODE_SUCCESS;

        /* Lock (and refresh if necessary) the attributes, copy them
           out, and unlock. */

        if ((status = cache_inode_lock_trust_attrs(entry, req_ctx))
            != CACHE_INODE_SUCCESS) {
                goto out;
        }

        *fileid = entry->obj_handle->attributes.fileid;

        PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:

        return status;
}

/**
 * @brief Gets the fsid of a cached entry
 *
 * This function gets the filied for a cached entry.
 *
 * @param[in]  entry   Entry to be managed.
 * @param[in]  req_ctx Request context(user creds, client address etc)
 * @param[out] fsid    The FS ID.
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
cache_inode_status_t
cache_inode_fsid(cache_entry_t *entry,
                 const struct req_op_context *req_ctx,
                 fsal_fsid_t *fsid)
{
        cache_inode_status_t status = 0;
        if (entry == NULL) {
                status = CACHE_INODE_INVALID_ARGUMENT;
                LogDebug(COMPONENT_CACHE_INODE,
                         "cache_inode_getattr: returning "
                         "CACHE_INODE_INVALID_ARGUMENT because of bad arg");
                goto out;
        }

        /* Set the return default to CACHE_INODE_SUCCESS */
        status = CACHE_INODE_SUCCESS;

        /* Lock (and refresh if necessary) the attributes, copy them
           out, and unlock. */

        if ((status = cache_inode_lock_trust_attrs(entry, req_ctx))
            != CACHE_INODE_SUCCESS) {
                goto out;
        }

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
 * @param[in]  req_ctx Request context(user creds, client address etc)
 * @param[out] size    The file ID.
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
cache_inode_status_t
cache_inode_size(cache_entry_t *entry,
                 const struct req_op_context *req_ctx,
                 uint64_t *size)
{
        cache_inode_status_t status = 0;
        if (entry == NULL) {
                status = CACHE_INODE_INVALID_ARGUMENT;
                LogDebug(COMPONENT_CACHE_INODE,
                         "cache_inode_getattr: returning "
                         "CACHE_INODE_INVALID_ARGUMENT because of bad arg");
                goto out;
        }

        /* Set the return default to CACHE_INODE_SUCCESS */
        status = CACHE_INODE_SUCCESS;

        /* Lock (and refresh if necessary) the attributes, copy them
           out, and unlock. */

        if ((status = cache_inode_lock_trust_attrs(entry, req_ctx))
            != CACHE_INODE_SUCCESS) {
                goto out;
        }

        *size = entry->obj_handle->attributes.filesize;

        PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:

        return status;
}

/**
 * @brief Return true if create verifier matches
 *
 * This functionr eturns true if the create verifier matches
 *
 * @param[in] entry   Entry to be managed.
 * @param[in] req_ctx Request context(user creds, client address etc)
 * @param[in] verf_hi High long of verifier
 * @param[in] verf_lo Low long of verifier
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
bool
cache_inode_create_verify(cache_entry_t *entry,
                          const struct req_op_context *req_ctx,
                          uint32_t verf_hi,
                          uint32_t verf_lo)
{
        /* True if the verifier matches */
        bool verified = false;

        /* Lock (and refresh if necessary) the attributes, copy them
           out, and unlock. */

        if (cache_inode_lock_trust_attrs(entry, req_ctx)
                == CACHE_INODE_SUCCESS) {
                if (FSAL_TEST_MASK(entry->obj_handle->attributes.mask,
                                   ATTR_ATIME) &&
                    FSAL_TEST_MASK(entry->obj_handle->attributes.mask,
                                   ATTR_MTIME) &&
                    entry->obj_handle->attributes.atime.seconds != verf_hi &&
                    entry->obj_handle->attributes.mtime.seconds != verf_lo) {
                        verified = true;
                }
        }

        PTHREAD_RWLOCK_unlock(&entry->attr_lock);

        return verified;
}
/** @} */
