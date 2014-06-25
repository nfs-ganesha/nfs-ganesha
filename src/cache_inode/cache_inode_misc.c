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
 * @file  cache_inode_misc.c
 * @brief Miscellaneous functions, especially new_entry
 */
#include "config.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_hash.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "hashtable.h"
#include "nfs4_acls.h"
#include "sal_functions.h"
#include "nfs_core.h"
#include "export_mgr.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

pool_t *cache_inode_entry_pool;

const char *
cache_inode_err_str(cache_inode_status_t err)
{
	switch (err) {
	case CACHE_INODE_SUCCESS:
		return "CACHE_INODE_SUCCESS";
	case CACHE_INODE_MALLOC_ERROR:
		return "CACHE_INODE_MALLOC_ERROR";
	case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
		return "CACHE_INODE_POOL_MUTEX_INIT_ERROR";
	case CACHE_INODE_GET_NEW_LRU_ENTRY:
		return "CACHE_INODE_GET_NEW_LRU_ENTRY";
	case CACHE_INODE_INIT_ENTRY_FAILED:
		return "CACHE_INODE_INIT_ENTRY_FAILED";
	case CACHE_INODE_FSAL_ERROR:
		return "CACHE_INODE_FSAL_ERROR";
	case CACHE_INODE_LRU_ERROR:
		return "CACHE_INODE_LRU_ERROR";
	case CACHE_INODE_HASH_SET_ERROR:
		return "CACHE_INODE_HASH_SET_ERROR";
	case CACHE_INODE_NOT_A_DIRECTORY:
		return "CACHE_INODE_NOT_A_DIRECTORY";
	case CACHE_INODE_INCONSISTENT_ENTRY:
		return "CACHE_INODE_INCONSISTENT_ENTRY";
	case CACHE_INODE_BAD_TYPE:
		return "CACHE_INODE_BAD_TYPE";
	case CACHE_INODE_ENTRY_EXISTS:
		return "CACHE_INODE_ENTRY_EXISTS";
	case CACHE_INODE_DIR_NOT_EMPTY:
		return "CACHE_INODE_DIR_NOT_EMPTY";
	case CACHE_INODE_NOT_FOUND:
		return "CACHE_INODE_NOT_FOUND";
	case CACHE_INODE_INVALID_ARGUMENT:
		return "CACHE_INODE_INVALID_ARGUMENT";
	case CACHE_INODE_INSERT_ERROR:
		return "CACHE_INODE_INSERT_ERROR";
	case CACHE_INODE_HASH_TABLE_ERROR:
		return "CACHE_INODE_HASH_TABLE_ERROR";
	case CACHE_INODE_FSAL_EACCESS:
		return "CACHE_INODE_FSAL_EACCESS";
	case CACHE_INODE_IS_A_DIRECTORY:
		return "CACHE_INODE_IS_A_DIRECTORY";
	case CACHE_INODE_FSAL_EPERM:
		return "CACHE_INODE_FSAL_EPERM";
	case CACHE_INODE_NO_SPACE_LEFT:
		return "CACHE_INODE_NO_SPACE_LEFT";
	case CACHE_INODE_READ_ONLY_FS:
		return "CACHE_INODE_READ_ONLY_FS";
	case CACHE_INODE_IO_ERROR:
		return "CACHE_INODE_IO_ERROR";
	case CACHE_INODE_FSAL_ESTALE:
		return "CACHE_INODE_FSAL_ESTALE";
	case CACHE_INODE_FSAL_ERR_SEC:
		return "CACHE_INODE_FSAL_ERR_SEC";
	case CACHE_INODE_STATE_CONFLICT:
		return "CACHE_INODE_STATE_CONFLICT";
	case CACHE_INODE_QUOTA_EXCEEDED:
		return "CACHE_INODE_QUOTA_EXCEEDED";
	case CACHE_INODE_DEAD_ENTRY:
		return "CACHE_INODE_DEAD_ENTRY";
	case CACHE_INODE_ASYNC_POST_ERROR:
		return "CACHE_INODE_ASYNC_POST_ERROR";
	case CACHE_INODE_NOT_SUPPORTED:
		return "CACHE_INODE_NOT_SUPPORTED";
	case CACHE_INODE_UNION_NOTSUPP:
		return "CACHE_INODE_UNION_NOTSUPP";
	case CACHE_INODE_STATE_ERROR:
		return "CACHE_INODE_STATE_ERROR";
	case CACHE_INODE_DELAY:
		return "CACHE_INODE_FSAL_DELAY";
	case CACHE_INODE_NAME_TOO_LONG:
		return "CACHE_INODE_NAME_TOO_LONG";
	case CACHE_INODE_BAD_COOKIE:
		return "CACHE_INODE_BAD_COOKIE";
	case CACHE_INODE_FILE_BIG:
		return "CACHE_INODE_FILE_BIG";
	case CACHE_INODE_KILLED:
		return "CACHE_INODE_KILLED";
	case CACHE_INODE_FILE_OPEN:
		return "CACHE_INODE_FILE_OPEN";
	case CACHE_INODE_FSAL_XDEV:
		return "CACHE_INOE_FSAL_XDEV";
	case CACHE_INODE_FSAL_MLINK:
		return "CACHE_INOE_FSAL_MLINK";
	case CACHE_INODE_SERVERFAULT:
		return "CACHE_INODE_SERVERFAULT";
	case CACHE_INODE_TOOSMALL:
		return "CACHE_INODE_TOOSMALL";
	case CACHE_INODE_FSAL_SHARE_DENIED:
		return "CACHE_INODE_FSAL_SHARE_DENIED";
	case CACHE_INODE_BADNAME:
		return "CACHE_INODE_BADNAME";
	case CACHE_INODE_IN_GRACE:
		return "CACHE_INODE_IN_GRACE";
	case CACHE_INODE_CROSS_JUNCTION:
		return "CACHE_INODE_CROSS_JUNCTION";
	case CACHE_INODE_BADHANDLE:
		return "CACHE_INODE_BADHANDLE";
	}
	return "unknown";
}

/**
 *
 * @brief Compares two keys used in cache inode
 *
 * Compare two keys used in cache inode. These keys are basically made from FSAL
 * related information.
 *
 * @param[in] buff1 First key
 * @param[in] buff2 Second key
 * @return 0 if keys are the same,
 *        -1 if first is greater/larger
 *         1 if second is greater/larger
 *
 * @see FSAL_handlecmp
 *
 */
int
cache_inode_compare_key_fsal(struct gsh_buffdesc *buff1,
			     struct gsh_buffdesc *buff2)
{
	/* Test if one of the entries is NULL */
	if (buff1->addr == NULL)
		return (buff2->addr == NULL) ? 0 : 1;
	else {
		if (buff2->addr == NULL)
			return -1; /* left member is the greater one */
		if (buff1->len == buff2->len)
			return memcmp(buff1->addr, buff2->addr, buff1->len);
		else
			return (buff1->len > buff2->len) ? -1 : 1;
	}
	/* This line should never be reached */
}				/* cache_inode_compare_key_fsal */

/**
 *
 * @brief Set the fsal_time in a pentry struct to the current time.
 *
 * Sets the fsal_time in a pentry struct to the current time. This
 * function is using gettimeofday.
 *
 * @param[out] time Pointer to time to be set
 *
 * @return 0 if keys if successfully build, -1 otherwise
 *
 */
int
cache_inode_set_time_current(struct timespec *time)
{
	struct timeval t;

	if (time == NULL)
		return -1;

	if (gettimeofday(&t, NULL) != 0)
		return -1;

	time->tv_sec = t.tv_sec;
	time->tv_nsec = 1000 * t.tv_usec;

	return 0;
}				/* cache_inode_set_time_current */

/**
 * @brief Adds a new entry to the cache
 *
 * This funcion adds a new entry to the cache.  It will allocate
 * entries of any kind.
 *
 * @param[in]  new_obj Object handle to be added to the cache
 * @param[in]  flags   Vary the function's operation
 * @param[out] entry   Newly instantiated cache entry
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */
cache_inode_status_t
cache_inode_new_entry(struct fsal_obj_handle *new_obj,
		      uint32_t flags,
		      cache_entry_t **entry)
{
	cache_inode_status_t status;
	cache_entry_t *oentry, *nentry = NULL;
	struct gsh_buffdesc fh_desc;
	cih_latch_t latch;
	bool has_hashkey = false;
	int rc = 0;
	cache_inode_key_t key;

	*entry = NULL;

	/* Get FSAL-specific key */
	new_obj->ops->handle_to_key(new_obj, &fh_desc);

	(void) cih_hash_key(&key, op_ctx->fsal_export->fsal, &fh_desc,
			    CIH_HASH_KEY_PROTOTYPE);

	/* Check if the entry already exists.  We allow the following race
	 * because cache_inode_lru_get has a slow path, and the latch is a
	 * shared lock. */
	oentry =
	    cih_get_by_key_latched(&key, &latch,
				  CIH_GET_RLOCK | CIH_GET_UNLOCK_ON_MISS,
				  __func__, __LINE__);
	if (oentry) {
		/* Entry is already in the cache, do not add it */
		status = CACHE_INODE_ENTRY_EXISTS;
		LogDebug(COMPONENT_CACHE_INODE,
			 "Trying to add an already existing "
			 "entry 1. Found entry %p type: %d, New type: %d",
			 oentry, oentry->type, new_obj->type);
		cache_inode_lru_ref(oentry, LRU_FLAG_NONE);
		/* Release the subtree hash table lock */
		cih_latch_rele(&latch);
		*entry = oentry;
		(void)atomic_inc_uint64_t(&cache_stp->inode_conf);
		goto out;
	}
	/* !LATCHED */

	/* We did not find the object.  Pull an entry off the LRU. */
	status = cache_inode_lru_get(&nentry);

	if (nentry == NULL) {
		/* Release the subtree hash table lock */
		cih_latch_rele(&latch);
		LogCrit(COMPONENT_CACHE_INODE, "cache_inode_lru_get failed");
		status = CACHE_INODE_MALLOC_ERROR;
		goto out;
	}

	/* Initialize common fields */
	nentry->type = new_obj->type;
	nentry->flags = 0;
	glist_init(&nentry->state_list);
	glist_init(&nentry->export_list);
	glist_init(&nentry->layoutrecall_list);

	/* See if someone raced us. */
	oentry =
	    cih_get_by_key_latched(&key, &latch, CIH_GET_WLOCK, __func__,
				  __LINE__);
	if (oentry) {
		/* Entry is already in the cache, do not add it. */
		status = CACHE_INODE_ENTRY_EXISTS;
		LogDebug(COMPONENT_CACHE_INODE,
			 "lost race to add entry %p type: %d, New type: %d",
			 oentry, oentry->obj_handle->type, new_obj->type);
		/* Ref it */
		cache_inode_lru_ref(oentry, LRU_FLAG_NONE);
		/* Release the subtree hash table lock */
		cih_latch_rele(&latch);
		*entry = oentry;
		(void)atomic_inc_uint64_t(&cache_stp->inode_conf);
		goto out;
	}

	/* We won the race. */

	/* Set cache key */

	has_hashkey = cih_hash_key(&nentry->fh_hk.key,
				   op_ctx->fsal_export->fsal,
				   &fh_desc, CIH_HASH_NONE);

	if (!has_hashkey) {
		cih_latch_rele(&latch);
		LogCrit(COMPONENT_CACHE_INODE,
			"Could not hash new entry");
		status = CACHE_INODE_MALLOC_ERROR;
		goto out;
	}

	switch (nentry->type) {
	case REGULAR_FILE:
		LogDebug(COMPONENT_CACHE_INODE,
			 "Adding a REGULAR_FILE, entry=%p", nentry);

		/* No shares or locks, yet. */
		glist_init(&nentry->object.file.deleg_list);
		glist_init(&nentry->object.file.lock_list);
		glist_init(&nentry->object.file.nlm_share_list);
		memset(&nentry->object.file.share_state, 0,
		       sizeof(cache_inode_share_t));

		/* Init statistics used for intelligently granting delegations*/
		init_deleg_heuristics(nentry);
		break;

	case DIRECTORY:
		LogDebug(COMPONENT_CACHE_INODE, "Adding a DIRECTORY, entry=%p",
			 nentry);

		atomic_set_uint32_t_bits(&nentry->flags,
					 CACHE_INODE_TRUST_CONTENT);

		/* If the directory is newly created, it is empty.  Because
		   we know its content, we consider it read. */
		if (flags & CACHE_INODE_FLAG_CREATE) {
			atomic_set_uint32_t_bits(&nentry->flags,
						 CACHE_INODE_DIR_POPULATED);
		} else {
			atomic_clear_uint32_t_bits(&nentry->flags,
						   CACHE_INODE_DIR_POPULATED);
		}

		nentry->object.dir.avl.collisions = 0;
		nentry->object.dir.nbactive = 0;
		glist_init(&nentry->object.dir.export_roots);
		/* init avl tree */
		cache_inode_avl_init(nentry);
		break;

	case SYMBOLIC_LINK:
	case SOCKET_FILE:
	case FIFO_FILE:
	case BLOCK_FILE:
	case CHARACTER_FILE:
		LogDebug(COMPONENT_CACHE_INODE,
			 "Adding a special file of type %d " "entry=%p",
			 nentry->type, nentry);
		break;

	default:
		/* Should never happen */
		cih_latch_rele(&latch);
		status = CACHE_INODE_INCONSISTENT_ENTRY;
		LogMajor(COMPONENT_CACHE_INODE, "unknown type %u provided",
			 nentry->type);
		goto out;
	}

	nentry->obj_handle = new_obj;

	if (nentry->obj_handle->attributes.expire_time_attr == 0) {
		nentry->obj_handle->attributes.expire_time_attr =
					op_ctx->export->expire_time_attr;
	}

	cache_inode_fixup_md(nentry);

	/* Everything ready and we are reaty to insert into hash table.
	 * change the lru state from LRU_ENTRY_UNINIT to LRU_FLAG_NONE
	 */
	nentry->flags = LRU_FLAG_NONE;

	/* Hash and insert entry */
	rc = cih_set_latched(nentry, &latch,
			     op_ctx->fsal_export->fsal, &fh_desc,
			     CIH_SET_UNLOCK | CIH_SET_HASHED);
	if (unlikely(rc)) {
		LogCrit(COMPONENT_CACHE_INODE,
			"entry could not be added to hash, rc=%d", rc);
		nentry->obj_handle = NULL; /* give it back and poison the
					    * entry */
		status = CACHE_INODE_HASH_SET_ERROR;
		goto out;
	}

	/* Map this new entry and the active export */
	if (!check_mapping(nentry, op_ctx->export)) {
		LogCrit(COMPONENT_CACHE_INODE,
			"Unable to create export mapping on new entry");
		/* Release the LRU reference and return error.
		 * This could leave a dangling cache entry belonging
		 * to no export, however, such an entry definitely has
		 * no open files, unless another cache_inode_get is
		 * successful, so is safe to allow LRU to eventually
		 * clean up this entry.
		 */
		cache_inode_put(nentry);
		return CACHE_INODE_MALLOC_ERROR;
	}

	LogDebug(COMPONENT_CACHE_INODE, "New entry %p added", nentry);
	*entry = nentry;
	(void)atomic_inc_uint64_t(&cache_stp->inode_added);
	return CACHE_INODE_SUCCESS;

 out:

	if (status == CACHE_INODE_ENTRY_EXISTS) {
		if (!check_mapping(*entry, op_ctx->export)) {
			LogCrit(COMPONENT_CACHE_INODE,
				"Unable to create export mapping on existing entry");
			status = CACHE_INODE_MALLOC_ERROR;
		}
	}

	if (nentry != NULL) {
		/* Deconstruct the object */

		/* Destroy the export mapping if any */
		clean_mapping(nentry);

		/* Destroy the locks */
		pthread_rwlock_destroy(&nentry->attr_lock);
		pthread_rwlock_destroy(&nentry->content_lock);
		pthread_rwlock_destroy(&nentry->state_lock);

		if (has_hashkey)
			cache_inode_key_delete(&nentry->fh_hk.key);

		/* Release the new entry we acquired. */
		cache_inode_lru_putback(nentry, LRU_FLAG_NONE);
	}

	/* must free new_obj if no new entry was created to reference it. */
	new_obj->ops->release(new_obj);

	return status;
}				/* cache_inode_new_entry */

struct export_get_first_entry_parms {
	struct gsh_export *export;
	struct entry_export_map *expmap;
};

/**
 * @brief Function to be called from cache_inode_get_protected to get the
 * first cache inode entry associated with an export.
 *
 * Also returns the expmap in the source parms for use by cache_inode_unexport.
 * This is safe due to the assumptions made by cache_inode_unexport.
 *
 * @param entry  [IN/OUT] call by ref pointer to store cache entry
 * @param source [IN/OUT] void pointer to parms structure
 *
 * @return cache inode status code
 @ @retval CACHE_INODE_NOT_FOUND indicates there are associated entries
 */

cache_inode_status_t export_get_first_entry(cache_entry_t **entry, void *source)
{
	struct export_get_first_entry_parms *parms = source;

	*entry = NULL;

	parms->expmap = glist_first_entry(&parms->export->entry_list,
					  struct entry_export_map,
					  entry_per_export);

	if (unlikely(parms->expmap == NULL))
		return CACHE_INODE_NOT_FOUND;

	*entry = parms->expmap->entry;

	return CACHE_INODE_SUCCESS;
}

/**
 * @brief Cleans up cache inode entries on unexport.
 *
 * Assumptions:
 * - export has been made unreachable
 * - export refcount == 0
 * - export root inode and junction have been cleaned up
 * - state associated with the export has been released
 *
 * @param[in] export The export being unexported
 *
 * @return the result of the conversion.
 *
 */

void cache_inode_unexport(struct gsh_export *export)
{
	struct export_get_first_entry_parms parms;
	cache_entry_t *entry;
	int errcnt = 0;
	cache_inode_status_t status;
	struct entry_export_map *expmap;

	parms.export = export;

	while (errcnt < 10) {
		status = cache_inode_get_protected(&entry,
						   &export->lock,
						   export_get_first_entry,
						   &parms);

		/* If we ran out of entries, we are done. */
		if (status == CACHE_INODE_NOT_FOUND)
			break;

		/* For any other failure skip, we might busy wait.
		 * For out of memory errors, we will limit our
		 * retries. CACHE_INODE_FSAL_ESTALE should eventually
		 * result in CACHE_INODE_NOT_FOUND as the mapping for
		 * the stale inode gets cleaned up.
		 */
		if (status != CACHE_INODE_SUCCESS) {
			if (status == CACHE_INODE_MALLOC_ERROR)
				errcnt++;
			continue;
		}

		/*
		 * Now with the appropriate locks, remove this entry from the
		 * export and if appropriate, dispose of it.
		 */

		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
		PTHREAD_RWLOCK_wrlock(&export->lock);

		/* Remove from list of exports for this entry */
		glist_del(&parms.expmap->export_per_entry);

		/* Remove from list of entries for this export */
		glist_del(&parms.expmap->entry_per_export);

		expmap = glist_first_entry(&entry->export_list,
					   struct entry_export_map,
					   export_per_entry);

		if (expmap == NULL) {
			/* Clear out first export pointer */
			atomic_store_voidptr(&entry->first_export, NULL);
			/* We must not hold entry->attr_lock across
			 * try_cleanup_push (LRU lane lock order) */
			PTHREAD_RWLOCK_unlock(&export->lock);
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);

			/* If there are no exports referencing this
			 * entry, attempt to push it to cleanup queue.
			 */
			cache_inode_lru_cleanup_try_push(entry);
		} else {
			/* Make sure first export pointer is still valid */
			atomic_store_voidptr(&entry->first_export,
					     expmap->export);

			PTHREAD_RWLOCK_unlock(&export->lock);
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		}



		gsh_free(parms.expmap);

		/* Done with entry, it may be cleaned up at this point.
		 * If other exports reference this entry then the entry
		 * will still be alive.
		 */
		cache_inode_put(entry);
	}
}

/**
 * @brief Converts an FSAL error to the corresponding cache_inode error
 *
 * This function converts an FSAL error to the corresponding
 * cache_inode error.
 *
 * @param[in] fsal_status FSAL error to be converted
 *
 * @return the result of the conversion.
 *
 */
cache_inode_status_t
cache_inode_error_convert(fsal_status_t fsal_status)
{
	switch (fsal_status.major) {
	case ERR_FSAL_NO_ERROR:
		return CACHE_INODE_SUCCESS;

	case ERR_FSAL_NOENT:
		return CACHE_INODE_NOT_FOUND;

	case ERR_FSAL_EXIST:
		return CACHE_INODE_ENTRY_EXISTS;

	case ERR_FSAL_ACCESS:
		return CACHE_INODE_FSAL_EACCESS;

	case ERR_FSAL_PERM:
		return CACHE_INODE_FSAL_EPERM;

	case ERR_FSAL_NOSPC:
		return CACHE_INODE_NO_SPACE_LEFT;

	case ERR_FSAL_NOTEMPTY:
		return CACHE_INODE_DIR_NOT_EMPTY;

	case ERR_FSAL_ROFS:
		return CACHE_INODE_READ_ONLY_FS;

	case ERR_FSAL_NOTDIR:
		return CACHE_INODE_NOT_A_DIRECTORY;

	case ERR_FSAL_IO:
	case ERR_FSAL_NXIO:
		return CACHE_INODE_IO_ERROR;

	case ERR_FSAL_STALE:
	case ERR_FSAL_FHEXPIRED:
		return CACHE_INODE_FSAL_ESTALE;

	case ERR_FSAL_INVAL:
	case ERR_FSAL_OVERFLOW:
		return CACHE_INODE_INVALID_ARGUMENT;

	case ERR_FSAL_DQUOT:
	case ERR_FSAL_NO_QUOTA:
		return CACHE_INODE_QUOTA_EXCEEDED;

	case ERR_FSAL_SEC:
		return CACHE_INODE_FSAL_ERR_SEC;

	case ERR_FSAL_NOTSUPP:
	case ERR_FSAL_ATTRNOTSUPP:
		return CACHE_INODE_NOT_SUPPORTED;

	case ERR_FSAL_UNION_NOTSUPP:
		return CACHE_INODE_UNION_NOTSUPP;

	case ERR_FSAL_DELAY:
		return CACHE_INODE_DELAY;

	case ERR_FSAL_NAMETOOLONG:
		return CACHE_INODE_NAME_TOO_LONG;

	case ERR_FSAL_NOMEM:
		return CACHE_INODE_MALLOC_ERROR;

	case ERR_FSAL_BADCOOKIE:
		return CACHE_INODE_BAD_COOKIE;

	case ERR_FSAL_FILE_OPEN:
		return CACHE_INODE_FILE_OPEN;

	case ERR_FSAL_NOT_OPENED:
		LogDebug(COMPONENT_CACHE_INODE,
			 "Conversion of ERR_FSAL_NOT_OPENED to "
			 "CACHE_INODE_FSAL_ERROR");
		return CACHE_INODE_FSAL_ERROR;

	case ERR_FSAL_ISDIR:
		return CACHE_INODE_IS_A_DIRECTORY;

	case ERR_FSAL_SYMLINK:
	case ERR_FSAL_BADTYPE:
		return CACHE_INODE_BAD_TYPE;

	case ERR_FSAL_FBIG:
		return CACHE_INODE_FILE_BIG;

	case ERR_FSAL_XDEV:
		return CACHE_INODE_FSAL_XDEV;

	case ERR_FSAL_MLINK:
		return CACHE_INODE_FSAL_MLINK;

	case ERR_FSAL_FAULT:
	case ERR_FSAL_SERVERFAULT:
	case ERR_FSAL_DEADLOCK:
		return CACHE_INODE_SERVERFAULT;

	case ERR_FSAL_TOOSMALL:
		return CACHE_INODE_TOOSMALL;

	case ERR_FSAL_SHARE_DENIED:
		return CACHE_INODE_FSAL_SHARE_DENIED;

	case ERR_FSAL_IN_GRACE:
		return CACHE_INODE_IN_GRACE;

	case ERR_FSAL_BADHANDLE:
		return CACHE_INODE_BADHANDLE;

	case ERR_FSAL_BLOCKED:
	case ERR_FSAL_INTERRUPT:
	case ERR_FSAL_NOT_INIT:
	case ERR_FSAL_ALREADY_INIT:
	case ERR_FSAL_BAD_INIT:
	case ERR_FSAL_TIMEOUT:
		/* These errors should be handled inside Cache Inode (or
		 * should never be seen by Cache Inode) */
		LogDebug(COMPONENT_CACHE_INODE,
			 "Conversion of FSAL error %d,%d to "
			 "CACHE_INODE_FSAL_ERROR",
			 fsal_status.major, fsal_status.minor);
		return CACHE_INODE_FSAL_ERROR;
	}

	/* We should never reach this line, this may produce a warning with
	 * certain compiler */
	LogCrit(COMPONENT_CACHE_INODE,
		"cache_inode_error_convert: default conversion to "
		"CACHE_INODE_FSAL_ERROR for error %d, line %u should never be "
		"reached",
		fsal_status.major, __LINE__);
	return CACHE_INODE_FSAL_ERROR;
}

/**
 *
 * @brief Prints the content of a directory
 *
 * This debugging function prints the contents of a directory.
 *
 * @param[in] entry the input pentry.
 *
 */
void
cache_inode_print_dir(cache_entry_t *entry)
{
	struct avltree_node *dirent_node;
	cache_inode_dir_entry_t *dirent;
	int i = 0;

	if (entry->type != DIRECTORY) {
		LogDebug(COMPONENT_CACHE_INODE,
			 "This entry is not a directory");
		return;
	}

	dirent_node = avltree_first(&entry->object.dir.avl.t);
	do {
		dirent =
		    avltree_container_of(dirent_node, cache_inode_dir_entry_t,
					 node_hk);
		LogFullDebug(COMPONENT_CACHE_INODE, "Name = %s, i=%d",
			     dirent->name, i);
		i++;
	} while ((dirent_node = avltree_next(dirent_node)));

	LogFullDebug(COMPONENT_CACHE_INODE, "------------------");
}				/* cache_inode_print_dir */

/**
 * cache_inode_release_dirents: release cached dirents associated
 * with an entry.
 *
 * releases dirents associated with pentry.  this is simple, but maybe
 * should be abstracted.
 *
 * @param[in] entry Directory to have entries be released
 * @param[in] which Caches to clear (dense, sparse, or both)
 *
 */
void
cache_inode_release_dirents(cache_entry_t *entry,
			    cache_inode_avl_which_t which)
{
	struct avltree_node *dirent_node = NULL;
	struct avltree_node *next_dirent_node = NULL;
	struct avltree *tree = NULL;
	cache_inode_dir_entry_t *dirent = NULL;

	/* Won't see this */
	if (entry->type != DIRECTORY)
		return;

	switch (which) {
	case CACHE_INODE_AVL_NAMES:
		tree = &entry->object.dir.avl.t;
		break;

	case CACHE_INODE_AVL_COOKIES:
		tree = &entry->object.dir.avl.c;
		break;

	case CACHE_INODE_AVL_BOTH:
		cache_inode_release_dirents(entry, CACHE_INODE_AVL_NAMES);
		cache_inode_release_dirents(entry, CACHE_INODE_AVL_COOKIES);
		/* tree == NULL */
		break;

	default:
		/* tree == NULL */
		break;
	}

	if (tree) {
		dirent_node = avltree_first(tree);

		while (dirent_node) {
			next_dirent_node = avltree_next(dirent_node);
			dirent =
			    avltree_container_of(dirent_node,
						 cache_inode_dir_entry_t,
						 node_hk);
			avltree_remove(dirent_node, tree);
			if (dirent->ckey.kv.len)
				cache_inode_key_delete(&dirent->ckey);
			gsh_free(dirent);
			dirent_node = next_dirent_node;
		}

		if (tree == &entry->object.dir.avl.t) {
			entry->object.dir.nbactive = 0;
			atomic_clear_uint32_t_bits(&entry->flags,
						   CACHE_INODE_DIR_POPULATED);
		}
	}
}

/**
 * @brief Lock attributes and check they are trustworthy
 *
 * This function acquires a read or write lock.  If the attributes need to be
 * refreshed, it drops the read lock, acquires a write lock, and, if the
 * attributes still need to be refreshed, refreshes the attributes.
 * On success this function will return with the attributes either
 * read or write locked.  It should only be used when read access is desired
 * for relatively short periods of time.
 *
 * @param[in,out] entry         The entry to lock and check
 * @param[in]     need_wr_lock  Need to take write lock?
 *
 * @return CACHE_INODE_SUCCESS if the attributes are locked and
 *         trustworthy, various cache_inode error codes otherwise.
 */

cache_inode_status_t
cache_inode_lock_trust_attrs(cache_entry_t *entry,
			     bool need_wr_lock)
{
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	time_t oldmtime = 0;

	if (need_wr_lock)
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	else
		PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

	/* Do we need to refresh? */
	if (cache_inode_is_attrs_valid(entry))
		goto out;

	if (!need_wr_lock) {
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

		/* Has someone else done it for us?  */
		if (cache_inode_is_attrs_valid(entry))
			goto out;
	}

	oldmtime = entry->obj_handle->attributes.mtime.tv_sec;

	cache_status = cache_inode_refresh_attrs(entry);
	if (cache_status != CACHE_INODE_SUCCESS)
		goto unlock;

	if ((entry->type == DIRECTORY)
	    && (oldmtime < entry->obj_handle->attributes.mtime.tv_sec)) {
		PTHREAD_RWLOCK_wrlock(&entry->content_lock);

		cache_status = cache_inode_invalidate_all_cached_dirent(entry);

		PTHREAD_RWLOCK_unlock(&entry->content_lock);

		if (cache_status != CACHE_INODE_SUCCESS) {
			LogCrit(COMPONENT_CACHE_INODE,
				"cache_inode_invalidate_all_cached_dirent "
				"returned %d (%s)", cache_status,
				cache_inode_err_str(cache_status));
			goto unlock;
		}
	}

 out:
	return cache_status;

 unlock:
	/* Release the lock on error */
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	return cache_status;
}

/** @} */
