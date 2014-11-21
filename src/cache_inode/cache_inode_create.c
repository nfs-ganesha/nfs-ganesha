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
 * @file    cache_inode_create.c
 * @brief   Creation of a file through the cache layer
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
 * @brief Creates an object in a directory
 *
 * This function creates an entry in the cache and underlying
 * filesystem.  If an entry is returned, its refcount charged to the
 * call path is +1.  An entry is returned for both CACHE_INODE_SUCCESS
 * and CACHE_INODE_ENTRY_EXISTS.
 *
 * @param[in]  parent     Parent directory
 * @param[in]  name       Name of the object to create
 * @param[in]  type       Type of the object to create
 * @param[in]  mode       Mode to be used at file creation
 * @param[in]  create_arg Additional argument for object creation
 * @param[out] entry      Cache entry for the created file
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t
cache_inode_create(cache_entry_t *parent,
		   const char *name,
		   object_file_type_t type, uint32_t mode,
		   cache_inode_create_arg_t *create_arg,
		   cache_entry_t **entry)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_obj_handle *object_handle;
	struct attrlist object_attributes;
	struct fsal_obj_handle *dir_handle;
	cache_inode_create_arg_t zero_create_arg;
	bool needdec = false;

	memset(&zero_create_arg, 0, sizeof(zero_create_arg));
	memset(&object_attributes, 0, sizeof(object_attributes));

	if (create_arg == NULL)
		create_arg = &zero_create_arg;

	if ((type != REGULAR_FILE) && (type != DIRECTORY)
	    && (type != SYMBOLIC_LINK) && (type != SOCKET_FILE)
	    && (type != FIFO_FILE) && (type != CHARACTER_FILE)
	    && (type != BLOCK_FILE)) {
		status = CACHE_INODE_BAD_TYPE;

		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because of bad type");
		*entry = NULL;
		goto out;
	}

	/* Permission checking will be done by the FSAL operation. */

	/* Try to create it first */

	dir_handle = parent->obj_handle;
	/* we pass in attributes to the create.  We will get them back below */
	FSAL_SET_MASK(object_attributes.mask,
		      ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
	object_attributes.owner = op_ctx->creds->caller_uid;
	object_attributes.group = op_ctx->creds->caller_gid; /* be more
							       * selective? */
	object_attributes.mode = mode;

	/* increase the refcount to ensure forced lookup in FSAL */
	atomic_inc_uint32_t(&parent->icreate_refcnt);
	needdec = true;

	switch (type) {
	case REGULAR_FILE:
		fsal_status =
		    dir_handle->ops->create(dir_handle, name,
					    &object_attributes, &object_handle);
		break;

	case DIRECTORY:
		fsal_status =
		    dir_handle->ops->mkdir(dir_handle, name,
					   &object_attributes, &object_handle);
		break;

	case SYMBOLIC_LINK:
		fsal_status =
		    dir_handle->ops->symlink(dir_handle, name,
					     create_arg->link_content,
					     &object_attributes,
					     &object_handle);
		break;

	case SOCKET_FILE:
	case FIFO_FILE:
		fsal_status = dir_handle->ops->mknode(dir_handle,
						      name, type,
						      NULL, /* dev_t !needed */
						      &object_attributes,
						      &object_handle);
		break;

	case BLOCK_FILE:
	case CHARACTER_FILE:
		fsal_status =
		    dir_handle->ops->mknode(dir_handle, name, type,
					    &create_arg->dev_spec,
					    &object_attributes, &object_handle);
		break;

	case NO_FILE_TYPE:
	case EXTENDED_ATTR:
		/* we should never go there */
		status = CACHE_INODE_INCONSISTENT_ENTRY;
		*entry = NULL;
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because inconsistent entry");
		goto out;
		break;
	}

	/* Refresh the parent's attributes */
	cache_inode_refresh_attrs_locked(parent);

	/* Check for the result */
	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE on create type %d", type);
			cache_inode_kill_entry(parent);
		} else if (fsal_status.major == ERR_FSAL_EXIST) {
			/* Already exists. Check if type if correct */
			status =
			    cache_inode_lookup(parent, name, entry);
			if (*entry != NULL) {
				status = CACHE_INODE_ENTRY_EXISTS;
				LogFullDebug(COMPONENT_CACHE_INODE,
					     "create failed because it already "
					     "exists");
				if ((*entry)->type != type) {
					/* Incompatible types, returns NULL */
					cache_inode_put(*entry);
					*entry = NULL;
				}
				goto out;
			}

			if (status == CACHE_INODE_NOT_FOUND) {
				/* Too bad, FSAL insist the file exists when
				 * we try to create it, but lookup couldn't
				 * find it, retry. */
				status = CACHE_INODE_INCONSISTENT_ENTRY;
				goto out;
			}
		}

		status = cache_inode_error_convert(fsal_status);
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because FSAL failed");
		*entry = NULL;
		goto out;
	}
	status =
	    cache_inode_new_entry(object_handle, CACHE_INODE_FLAG_CREATE,
				  entry);
	if (*entry == NULL) {
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because insert new entry failed");
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);
	/* Add this entry to the directory (also takes an internal ref) */
	status = cache_inode_add_cached_dirent(parent, name, *entry, NULL);
	PTHREAD_RWLOCK_unlock(&parent->content_lock);
	if (status != CACHE_INODE_SUCCESS) {
		cache_inode_put(*entry);
		*entry = NULL;
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "create failed because add dirent failed");
		goto out;
	}

	if (type == DIRECTORY) {
		/* Insert Parent's key */
		cache_inode_key_dup(&(*entry)->object.dir.parent,
				    &parent->fh_hk.key);
	}

 out:
	if (needdec == true) {
		/* decrease refcnt to allow negative cache lookup */
		atomic_dec_uint32_t(&parent->icreate_refcnt);
	}
	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Returning entry=%p status=%s for %s FSAL=%s", *entry,
		     cache_inode_err_str(status), name,
		     parent->obj_handle->fsal->name);

	return status;
}

/**
 * @brief Set the create verifier
 *
 * This function sets the mtime/atime attributes according to the create
 * verifier
 *
 * @param[in] sattr   attrlist to be managed.
 * @param[in] verf_hi High long of verifier
 * @param[in] verf_lo Low long of verifier
 *
 */
void
cache_inode_create_set_verifier(struct attrlist *sattr, uint32_t verf_hi,
				     uint32_t verf_lo)
{
	sattr->atime.tv_sec = verf_hi;
	sattr->atime.tv_nsec = 0;
	FSAL_SET_MASK(sattr->mask, ATTR_ATIME);
	sattr->mtime.tv_sec = verf_lo;
	sattr->mtime.tv_nsec = 0;
	FSAL_SET_MASK(sattr->mask, ATTR_MTIME);
}

/**
 * @brief Return true if create verifier matches
 *
 * This function returns true if the create verifier matches
 *
 * @param[in] entry   Entry to be managed.
 * @param[in] verf_hi High long of verifier
 * @param[in] verf_lo Low long of verifier
 *
 * @return Errors from cache_inode_lock_trust_attributes.
 *
 */
bool
cache_inode_create_verify(cache_entry_t *entry,
			  uint32_t verf_hi, uint32_t verf_lo)
{
	/* True if the verifier matches */
	bool verified = false;

	/* Lock (and refresh if necessary) the attributes, copy them
	   out, and unlock. */

	if (cache_inode_lock_trust_attrs(entry, false)
	    == CACHE_INODE_SUCCESS) {
		if (FSAL_TEST_MASK
		    (entry->obj_handle->attributes.mask, ATTR_ATIME)
		    && FSAL_TEST_MASK(entry->obj_handle->attributes.mask,
				      ATTR_MTIME)
		    && entry->obj_handle->attributes.atime.tv_sec == verf_hi
		    && entry->obj_handle->attributes.mtime.tv_sec == verf_lo) {
			verified = true;
		}
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	}

	return verified;
}

/** @} */
