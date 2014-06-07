/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file    cache_inode_readdir.c
 * @brief   Reads the content of a directory
 *
 * Reads the content of a directory, also includes support functions
 * for cached directories.
 *
 *
 */
#include "config.h"
#include "abstract_atomic.h"
#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_avl.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Invalidates all cached entries for a directory
 *
 * Invalidates all the entries for a cached directory.  The content
 * lock must be held when this function is called.
 *
 * @param[in,out] entry  The directory to be managed
 *
 * @return CACHE_INODE_SUCCESS or errors.
 *
 */
cache_inode_status_t
cache_inode_invalidate_all_cached_dirent(cache_entry_t *entry)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	/* Only DIRECTORY entries are concerned */
	if (entry->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		return status;
	}

	/* Get rid of entries cached in the DIRECTORY */
	cache_inode_release_dirents(entry, CACHE_INODE_AVL_BOTH);

	/* Now we can trust the content */
	atomic_set_uint32_t_bits(&entry->flags, CACHE_INODE_TRUST_CONTENT);

	status = CACHE_INODE_SUCCESS;

	return status;
}

/**
 * @brief Perform an operation on it on a cached entry
 *
 * This function looks up an entry in the drectory cache and performs
 * the indicated operation.  If the directory has not been populated,
 * it will not return not found errors.
 *
 * The caller must hold the content lock on the directory.
 *
 * @param[in] directory The directory to be operated upon
 * @param[in] name      The name of the relevant entry
 * @param[in] newname   The new name for renames
 * @param[in] dirent_op The operation (LOOKUP, REMOVE, or RENAME) to
 *                      perform
 *
 * @retval CACHE_INODE_SUCCESS on success or failure in an unpopulated
 *                             directory.
 * @retval CACHE_INODE_BAD_TYPE if the supplied cache entry is not a
 *                             directory.
 * @retval CACHE_INODE_NOT_FOUND on lookup failure in a populated
 *                             directory.
 * @retval CACHE_INODE_ENTRY_EXISTS on rename collission in a
 *                             populated directory.
 */

#define CIV_FLAGS (CACHE_INODE_INVALIDATE_ATTRS| \
		   CACHE_INODE_INVALIDATE_CONTENT)

cache_inode_status_t
cache_inode_operate_cached_dirent(cache_entry_t *directory,
				  const char *name,
				  const char *newname,
				  cache_inode_dirent_op_t dirent_op)
{
	cache_inode_dir_entry_t *dirent, *dirent2, *dirent3;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	int code = 0;

	assert((dirent_op == CACHE_INODE_DIRENT_OP_LOOKUP)
	       || (dirent_op == CACHE_INODE_DIRENT_OP_REMOVE)
	       || (dirent_op == CACHE_INODE_DIRENT_OP_RENAME));

	/* Sanity check */
	if (directory->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		goto out;
	}

	LogFullDebug(COMPONENT_CACHE_INODE, "%s %p name=%s newname=%s",
		     dirent_op ==
		     CACHE_INODE_DIRENT_OP_REMOVE ? "REMOVE" : "RENAME",
		     directory, name, newname);

	/* If no active entry, do nothing */
	if (directory->object.dir.nbactive == 0) {
		if (!
		    ((directory->flags & CACHE_INODE_TRUST_CONTENT)
		     && (directory->flags & CACHE_INODE_DIR_POPULATED))) {
			/* We cannot serve negative lookups. */
			/* status == CACHE_INODE_SUCCESS; */
		} else {
			status = CACHE_INODE_NOT_FOUND;
		}
		goto out;
	}

	dirent = cache_inode_avl_qp_lookup_s(directory, name, 1);
	if ((!dirent) || (dirent->flags & DIR_ENTRY_FLAG_DELETED)) {
		if (!
		    ((directory->flags & CACHE_INODE_TRUST_CONTENT)
		     && (directory->flags & CACHE_INODE_DIR_POPULATED))
		    || (dirent_op == CACHE_INODE_DIRENT_OP_REMOVE)) {
			/* We cannot serve negative lookups. */
			/* status == CACHE_INODE_SUCCESS; */
		} else {
			status = CACHE_INODE_NOT_FOUND;
		}
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "dirent=%p%s directory flags%s%s", dirent,
			     dirent ? ((dirent->flags & DIR_ENTRY_FLAG_DELETED)
				       ? " DELETED" : "")
			     : "",
			     (directory->flags & CACHE_INODE_TRUST_CONTENT)
			     ? " TRUST" : "",
			     (directory->flags & CACHE_INODE_DIR_POPULATED)
			     ? " POPULATED" : "");
		goto out;
	}

	/* We perform operations anyway even if CACHE_INODE_TRUST_CONTENT
	   is clear.  That way future upcalls can call in to this
	   function to update the content to be correct.  We just don't
	   ever return a not found or exists error. */

	switch (dirent_op) {
	case CACHE_INODE_DIRENT_OP_REMOVE:
		/* mark deleted */
		avl_dirent_set_deleted(directory, dirent);
		directory->object.dir.nbactive--;
		break;

	case CACHE_INODE_DIRENT_OP_RENAME:
		dirent2 = cache_inode_avl_qp_lookup_s(directory, newname, 1);
		if (dirent2) {
			/* rename would cause a collision */
			if (directory->flags & CACHE_INODE_TRUST_CONTENT) {
				/* overwrite, replace entry and expire the
				 * old */
				cache_entry_t *oldentry;
				avl_dirent_set_deleted(directory, dirent);
				cache_inode_key_dup(&dirent2->ckey,
						    &dirent->ckey);
				oldentry =
				    cache_inode_get_keyed(
					    &dirent2->ckey,
					    CIG_KEYED_FLAG_CACHED_ONLY,
					    &status);
				if (oldentry) {
					/* if it is still around, mark it
					 * gone/stale */
					status =
					    cache_inode_invalidate(
						    oldentry,
						    CIV_FLAGS);
					cache_inode_lru_unref(oldentry,
							      LRU_FLAG_NONE);
				}
			} else
				status = CACHE_INODE_ENTRY_EXISTS;
		} else {
			/* Size (including terminating NUL) of the filename */
			size_t newnamesize = strlen(newname) + 1;
			/* try to rename--no longer in-place */
			dirent3 = gsh_malloc(sizeof(cache_inode_dir_entry_t)
					     + newnamesize);
			memcpy(dirent3->name, newname, newnamesize);
			dirent3->flags = DIR_ENTRY_FLAG_NONE;
			cache_inode_key_dup(&dirent3->ckey, &dirent->ckey);
			avl_dirent_set_deleted(directory, dirent);
			code = cache_inode_avl_qp_insert(directory, dirent3);
			if (code < 0) {
				/* collision, tree state unchanged (unlikely) */
				status = CACHE_INODE_ENTRY_EXISTS;
				/* dirent is on persist tree, undelete it */
				avl_dirent_clear_deleted(directory, dirent);
				/* dirent3 was never inserted */
				gsh_free(dirent3);
			}
		}		/* !found */
		break;

	case CACHE_INODE_DIRENT_OP_LOOKUP:
		/* Lookup was performed before switch statement */
		break;
	}

out:
	return status;
}				/* cache_inode_operate_cached_dirent */

/**
 *
 * @brief Adds a directory entry to a cached directory.
 *
 * This function adds a new directory entry to a directory.  Directory
 * entries have only weak references, so they do not prevent recycling
 * or freeing the entry they locate.  This function may be called
 * either once (for handling creation) or iteratively in directory
 * population.
 *
 * @param[in,out] parent    Cache entry of the directory being updated
 * @param[in]     name      The name to add to the entry
 * @param[in]     entry     The cache entry associated with name
 * @param[out]    dir_entry The directory entry newly added (optional)
 *
 * @return CACHE_INODE_SUCCESS or errors on failure.
 */

cache_inode_status_t
cache_inode_add_cached_dirent(cache_entry_t *parent,
			      const char *name,
			      cache_entry_t *entry,
			      cache_inode_dir_entry_t **dir_entry)
{
	cache_inode_dir_entry_t *new_dir_entry = NULL;
	size_t namesize = strlen(name) + 1;
	int code = 0;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	/* Sanity check */
	if (parent->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		return status;
	}

	/* in cache inode avl, we always insert on pentry_parent */
	new_dir_entry = gsh_malloc(sizeof(cache_inode_dir_entry_t) + namesize);
	if (new_dir_entry == NULL) {
		status = CACHE_INODE_MALLOC_ERROR;
		return status;
	}

	new_dir_entry->flags = DIR_ENTRY_FLAG_NONE;

	memcpy(&new_dir_entry->name, name, namesize);
	cache_inode_key_dup(&new_dir_entry->ckey, &entry->fh_hk.key);

	/* add to avl */
	code = cache_inode_avl_qp_insert(parent, new_dir_entry);
	if (code < 0) {
		/* collision, tree not updated--release both pool objects and
		 * return err */
		gsh_free(new_dir_entry->ckey.kv.addr);
		gsh_free(new_dir_entry);
		status = CACHE_INODE_ENTRY_EXISTS;
		return status;
	}

	if (dir_entry)
		*dir_entry = new_dir_entry;

	/* we're going to succeed */
	parent->object.dir.nbactive++;

	return status;
}

/**
 * @brief Removes an entry from a cached directory.
 *
 * This function removes the named entry from a cached directory.  The
 * caller must hold the content lock.
 *
 * @param[in,out] directory The cache entry representing the directory
 * @param[in]     name      The name indicating the entry to remove
 *
 * @retval CACHE_INODE_SUCCESS on success.
 * @retval CACHE_INODE_BAD_TYPE if directory is not a directory.
 * @retval The result of cache_inode_operate_cached_dirent
 *
 */
cache_inode_status_t
cache_inode_remove_cached_dirent(cache_entry_t *directory,
				 const char *name)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	/* Sanity check */
	if (directory->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		return status;
	}

	status =
	    cache_inode_operate_cached_dirent(directory, name, NULL,
					      CACHE_INODE_DIRENT_OP_REMOVE);
	return status;

}

/**
 * @brief State to be passed to FSAL readdir callbacks
 */

struct cache_inode_populate_cb_state {
	cache_entry_t *directory;
	cache_inode_status_t *status;
	uint64_t offset_cookie;
};

/**
 * @brief Populate a single dir entry
 *
 * This callback serves to populate a single dir entry from the
 * readdir.
 *
 * @param[in]     name      Name of the directory entry
 * @param[in,out] dir_state Callback state
 * @param[in]     cookie    Directory cookie
 *
 * @retval true if more entries are requested
 * @retval false if no more should be sent and the last was not processed
 */

static bool
populate_dirent(const char *name, void *dir_state,
		fsal_cookie_t cookie)
{
	struct cache_inode_populate_cb_state *state =
	    (struct cache_inode_populate_cb_state *)dir_state;
	struct fsal_obj_handle *entry_hdl;
	cache_inode_dir_entry_t *new_dir_entry = NULL;
	cache_entry_t *cache_entry = NULL;
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_obj_handle *dir_hdl = state->directory->obj_handle;

	fsal_status = dir_hdl->ops->lookup(dir_hdl, name, &entry_hdl);
	if (FSAL_IS_ERROR(fsal_status)) {
		*state->status = cache_inode_error_convert(fsal_status);
		if (*state->status == CACHE_INODE_FSAL_XDEV) {
			LogInfo(COMPONENT_NFS_READDIR,
				"Ignoring XDEV entry %s",
				name);
			*state->status = CACHE_INODE_SUCCESS;
			return true;
		}
		LogInfo(COMPONENT_CACHE_INODE,
			"Lookup failed on %s in dir %p with %s",
			name, dir_hdl, cache_inode_err_str(*state->status));
		return !cache_param.retry_readdir;
	}

	LogFullDebug(COMPONENT_NFS_READDIR, "Creating entry for %s", name);

	*state->status =
	    cache_inode_new_entry(entry_hdl, CACHE_INODE_FLAG_NONE,
				  &cache_entry);

	if (cache_entry == NULL) {
		*state->status = CACHE_INODE_NOT_FOUND;
		/* we do not free entry_hdl because it is consumed by
		   cache_inode_new_entry */
		LogEvent(COMPONENT_NFS_READDIR,
			 "cache_inode_new_entry failed with %s",
			 cache_inode_err_str(*state->status));
		return false;
	}

	if (cache_entry->type == DIRECTORY) {
		/* Insert Parent's key */
		cache_inode_key_dup(&cache_entry->object.dir.parent,
				    &state->directory->fh_hk.key);
	}

	*state->status =
	    cache_inode_add_cached_dirent(state->directory, name, cache_entry,
					  &new_dir_entry);
	/* return initial ref */
	cache_inode_put(cache_entry);

	if ((*state->status != CACHE_INODE_SUCCESS) &&
	    (*state->status != CACHE_INODE_ENTRY_EXISTS)) {
		LogEvent(COMPONENT_NFS_READDIR,
			 "cache_inode_add_cached_dirent failed with %s",
			 cache_inode_err_str(*state->status));
		return false;
	}

	return true;
}

/**
 *
 * @brief Cache complete directory contents
 *
 * This function reads a complete directory from the FSAL and caches
 * both the names and filess.  The content lock must be held on the
 * directory being read.
 *
 * @param[in] directory  Entry for the parent directory to be read
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

static cache_inode_status_t
cache_inode_readdir_populate(cache_entry_t *directory)
{
	fsal_status_t fsal_status;
	bool eod = false;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	struct cache_inode_populate_cb_state state;

	/* Only DIRECTORY entries are concerned */
	if (directory->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		LogDebug(COMPONENT_NFS_READDIR,
			 "CACHE_INODE_NOT_A_DIRECTORY");
		return status;
	}

	if ((directory->flags & CACHE_INODE_DIR_POPULATED)
	    && (directory->flags & CACHE_INODE_TRUST_CONTENT)) {
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "CACHE_INODE_DIR_POPULATED and CACHE_INODE_TRUST_CONTENT"
			     );
		status = CACHE_INODE_SUCCESS;
		return status;
	}

	/* Invalidate all the dirents */
	status = cache_inode_invalidate_all_cached_dirent(directory);
	if (status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "cache_inode_invalidate_all_cached_dirent status=%s",
			 cache_inode_err_str(status));
		return status;
	}

	state.directory = directory;
	state.status = &status;
	state.offset_cookie = 0;

	fsal_status =
		directory->obj_handle->ops->readdir(directory->obj_handle,
						    NULL,
						    (void *)&state,
						    populate_dirent,
						    &eod);
	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_NFS_READDIR,
				 "FSAL returned STALE from readdir.");
			cache_inode_kill_entry(directory);
		}

		status = cache_inode_error_convert(fsal_status);
		LogDebug(COMPONENT_NFS_READDIR,
			 "FSAL readdir status=%s",
			 cache_inode_err_str(status));
		return status;
	}

	/* we were supposed to read to the end.... */
	if (!eod && cache_param.retry_readdir) {
		LogInfo(COMPONENT_NFS_READDIR,
			"Readdir didn't reach eod on dir %p (status %s)",
			directory->obj_handle, cache_inode_err_str(status));
		status = CACHE_INODE_DELAY;
	} else if (eod) {
		/* End of work */
		atomic_set_uint32_t_bits(&directory->flags,
					 CACHE_INODE_DIR_POPULATED);

		/* override status in case it was set to a
		 * minor error in the callback */
		status = CACHE_INODE_SUCCESS;
	}

	/* If !eod (and fsal_status isn't an error), then the only error path
	 * is through a callback failure and status has been set the
	 * populate_dirent callback */

	return status;
}				/* cache_inode_readdir_populate */

/**
 * @brief Reads a directory
 *
 * This function iterates over the cached directory entries (possibly
 * after populating the cache) and invokes a supplied callback
 * function for each one.
 *
 * The caller must not hold the attribute or content locks on
 * directory.
 *
 * @param[in]  directory The directory to be read
 * @param[in]  cookie    Starting cookie for the readdir operation
 * @param[out] nbfound   Number of entries returned.
 * @param[out] eod_met   Whether the end of directory was met
 * @param[in]  attrmask  Attributes requested, used for permission checking
 *                       really all that matters is ATTR_ACL and any attrs
 *                       at all, specifics never actually matter.
 * @param[in]  cb        The callback function to receive entries
 * @param[in]  opaque    A pointer passed to be passed in
 *                       cache_inode_readdir_cb_parms
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_BAD_TYPE if entry is not related to a directory
 */

cache_inode_status_t
cache_inode_readdir(cache_entry_t *directory,
		    uint64_t cookie, unsigned int *nbfound,
		    bool *eod_met,
		    attrmask_t attrmask,
		    cache_inode_getattr_cb_t cb,
		    void *opaque)
{
	/* The entry being examined */
	cache_inode_dir_entry_t *dirent = NULL;
	/* The node in the tree being traversed */
	struct avltree_node *dirent_node;
	/* The access mask corresponding to permission to list directory
	   entries */
	fsal_accessflags_t access_mask =
	    (FSAL_MODE_MASK_SET(FSAL_R_OK) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR));
	fsal_accessflags_t access_mask_attr =
	    (FSAL_MODE_MASK_SET(FSAL_R_OK) | FSAL_MODE_MASK_SET(FSAL_X_OK) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR) |
	     FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE));
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cache_inode_status_t attr_status;
	struct cache_inode_readdir_cb_parms cb_parms = { opaque, NULL,
							 true, 0, true };
	bool retry_stale = true;

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "Enter....");

	/* readdir can be done only with a directory */
	if (directory->type != DIRECTORY) {
		status = CACHE_INODE_NOT_A_DIRECTORY;
		/* no lock acquired so far, just return status */
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "Not a directory");
		return status;
	}

	/* cache_inode_lock_trust_attrs can return an error, and no lock will
	 * be acquired */
	status = cache_inode_lock_trust_attrs(directory, false);
	if (status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "cache_inode_lock_trust_attrs status=%s",
			 cache_inode_err_str(status));
		return status;
	}

	/* Adjust access mask if ACL is asked for.
	 * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
	 */
	if ((attrmask & ATTR_ACL) != 0) {
		access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
		access_mask_attr |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
	}

	/* Check if user (as specified by the credentials) is authorized to read
	 * the directory or not */
	status = cache_inode_access_no_mutex(directory, access_mask);
	if (status != CACHE_INODE_SUCCESS) {
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "permission check for directory status=%s",
			     cache_inode_err_str(status));
		goto unlock_attrs;
	}

	if (attrmask != 0) {
		/* Check for access permission to get attributes */
		attr_status = cache_inode_access_no_mutex(directory,
							  access_mask_attr);
		if (attr_status != CACHE_INODE_SUCCESS) {
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "permission check for attributes "
				     "status=%s",
				     cache_inode_err_str(attr_status));
		}
	} else
		/* No attributes requested, we don't need permission */
		attr_status = CACHE_INODE_SUCCESS;

	PTHREAD_RWLOCK_rdlock(&directory->content_lock);
	PTHREAD_RWLOCK_unlock(&directory->attr_lock);
	if (!
	    ((directory->flags & CACHE_INODE_TRUST_CONTENT)
	     && (directory->flags & CACHE_INODE_DIR_POPULATED))) {
		PTHREAD_RWLOCK_unlock(&directory->content_lock);
		PTHREAD_RWLOCK_wrlock(&directory->content_lock);
		status = cache_inode_readdir_populate(directory);
		if (status != CACHE_INODE_SUCCESS) {
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "cache_inode_readdir_populate status=%s",
				     cache_inode_err_str(status));
			goto unlock_dir;
		}
	}

	/* deal with initial cookie value:
	 * 1. cookie is invalid (-should- be checked by caller)
	 * 2. cookie is 0 (first cookie) -- ok
	 * 3. cookie is > than highest dirent position (error)
	 * 4. cookie <= highest dirent position but > highest cached cookie
	 *    (currently equivalent to #2, because we pre-populate the cookie
	 *    avl)
	 * 5. cookie is in cached range -- ok */

	if (cookie > 0) {
		/* N.B., cache_inode_avl_qp_insert_s ensures k > 2 */
		if (cookie < 3) {
			status = CACHE_INODE_BAD_COOKIE;
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "Bad cookie");
			goto unlock_dir;
		}

		/* we assert this can now succeed */
		dirent =
		    cache_inode_avl_lookup_k(directory, cookie,
					     CACHE_INODE_FLAG_NEXT_ACTIVE);
		if (!dirent) {
			/* Linux (3.4, etc) has been observed to send readdir
			 * at the offset of the last entry's cookie, and
			 * returns no dirents to userland if that readdir
			 * notfound or badcookie. */
			if (cache_inode_avl_lookup_k
			    (directory, cookie, CACHE_INODE_FLAG_NONE)) {
				/* yup, it was the last entry */
				LogFullDebug(COMPONENT_NFS_READDIR,
					     "EOD because empty result");
				*eod_met = true;
				goto unlock_dir;
			}
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "seek to cookie=%" PRIu64 " fail",
				     cookie);
			status = CACHE_INODE_BAD_COOKIE;
			goto unlock_dir;
		}

		/* dirent is the NEXT entry to return, since we sent
		 * CACHE_INODE_FLAG_NEXT_ACTIVE */
		dirent_node = &dirent->node_hk;

	} else {
		/* initial readdir */
		dirent_node = avltree_first(&directory->object.dir.avl.t);
	}

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "About to readdir in cache_inode_readdir: directory=%p "
		     "cookie=%" PRIu64 " collisions %d", directory, cookie,
		     directory->object.dir.avl.collisions);

	/* Now satisfy the request from the cached readdir--stop when either
	 * the requested sequence or dirent sequence is exhausted */
	*nbfound = 0;
	*eod_met = false;

	for (; cb_parms.in_result && dirent_node;
	     dirent_node = avltree_next(dirent_node)) {

		cache_entry_t *entry = NULL;
		cache_inode_status_t tmp_status = 0;

		dirent =
		    avltree_container_of(dirent_node, cache_inode_dir_entry_t,
					 node_hk);

 estale_retry:
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "Lookup direct %s",
			     dirent->name);

		entry =
		    cache_inode_get_keyed(&dirent->ckey,
					  CIG_KEYED_FLAG_NONE, &tmp_status);
		if (!entry) {
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "Lookup returned %s",
				     cache_inode_err_str(tmp_status));

			if (retry_stale
			    && tmp_status == CACHE_INODE_FSAL_ESTALE) {
				LogDebug(COMPONENT_NFS_READDIR,
					 "cache_inode_get_keyed returned %s "
					 "for %s - retrying entry",
					 cache_inode_err_str(tmp_status),
					 dirent->name);
				retry_stale = false; /* only one retry per
						      * dirent */
				goto estale_retry;
			}

			if (tmp_status == CACHE_INODE_NOT_FOUND
			    || tmp_status == CACHE_INODE_FSAL_ESTALE) {
				/* Directory changed out from under us.
				   Invalidate it, skip the name, and keep
				   going. */
				atomic_clear_uint32_t_bits(
					&directory->flags,
					CACHE_INODE_TRUST_CONTENT);
				LogDebug(COMPONENT_NFS_READDIR,
					 "cache_inode_get_keyed returned %s "
					 "for %s - skipping entry",
					 cache_inode_err_str(tmp_status),
					 dirent->name);
				continue;
			} else {
				/* Something is more seriously wrong,
				   probably an inconsistency. */
				status = tmp_status;
				LogCrit(COMPONENT_NFS_READDIR,
					"cache_inode_get_keyed returned %s "
					"for %s - bailing out",
					cache_inode_err_str(status),
					dirent->name);
				goto unlock_dir;
			}
		}

		LogFullDebug(COMPONENT_NFS_READDIR,
			     "cache_inode_readdir: dirent=%p name=%s "
			     "cookie=%" PRIu64 " (probes %d)", dirent,
			     dirent->name, dirent->hk.k, dirent->hk.p);

		cb_parms.name = dirent->name;
		cb_parms.attr_allowed = attr_status == CACHE_INODE_SUCCESS;
		cb_parms.cookie = dirent->hk.k;

		tmp_status = cache_inode_getattr(entry, &cb_parms, cb);

		if (tmp_status != CACHE_INODE_SUCCESS) {
			cache_inode_lru_unref(entry, LRU_FLAG_NONE);
			if (tmp_status == CACHE_INODE_FSAL_ESTALE) {
				if (retry_stale) {
					LogDebug(COMPONENT_NFS_READDIR,
						 "cache_inode_getattr returned "
						 "%s for %s - retrying entry",
						 cache_inode_err_str
						 (tmp_status), dirent->name);
					retry_stale = false; /* only one retry
							      * per dirent */
					goto estale_retry;
				}

				/* Directory changed out from under us.
				   Invalidate it, skip the name, and keep
				   going. */
				atomic_clear_uint32_t_bits(
					&directory->flags,
					CACHE_INODE_TRUST_CONTENT);

				LogDebug(COMPONENT_NFS_READDIR,
					 "cache_inode_lock_trust_attrs "
					 "returned %s for %s - skipping entry",
					 cache_inode_err_str(tmp_status),
					 dirent->name);
				continue;
			}

			status = tmp_status;

			LogCrit(COMPONENT_NFS_READDIR,
				"cache_inode_lock_trust_attrs returned %s for "
				"%s - bailing out",
				cache_inode_err_str(status), dirent->name);

			goto unlock_dir;
		}

		(*nbfound)++;

		cache_inode_lru_unref(entry, LRU_FLAG_NONE);

		if (!cb_parms.in_result) {
			LogDebug(COMPONENT_NFS_READDIR,
				 "bailing out due to entry not in result");
			break;
		}
	}

	/* We have reached the last node and every node traversed was
	   added to the result */

	LogDebug(COMPONENT_NFS_READDIR,
		 "dirent_node = %p, nbfound = %u, in_result = %s", dirent_node,
		 *nbfound, cb_parms.in_result ? "TRUE" : "FALSE");

	if (!dirent_node && cb_parms.in_result)
		*eod_met = true;
	else
		*eod_met = false;

unlock_dir:
	PTHREAD_RWLOCK_unlock(&directory->content_lock);
	return status;

unlock_attrs:
	PTHREAD_RWLOCK_unlock(&directory->attr_lock);
	return status;
}				/* cache_inode_readdir */

/** @} */
