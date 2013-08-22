/*
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
 * @addtogroup fsal_up
 * @{
 */

/**
 * @file fsal_up_top.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Top level FSAL Upcall handlers
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "cache_inode_hash.h"
#include "HashTable.h"
#include "fsal_up.h"
#include "sal_functions.h"
#include "pnfs_utils.h"
#include "nfs_rpc_callback.h"
#include "nfs_proto_tools.h"
#include "delayed_exec.h"

/**
 * @brief Fake root credentials
 */
static struct user_cred synthetic_creds = {
	.caller_uid = 0,
	.caller_gid = 0,
	.caller_glen = 0,
	.caller_garray = NULL
};

/**
 * @brief Synthetic request context for calling into Cache_Inode.
 */
static struct req_op_context synthetic_context = {
	.creds = &synthetic_creds,
	.caller_addr = NULL,
	.clientid = NULL
};

/**
 * @brief Invalidate a cached entry
 *
 * @param[in] export FSAL export
 * @param[in] key    Key to specify object
 * @param[in] flags  Flags to pass to cache_inode_invalidate
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

static cache_inode_status_t invalidate(
	struct fsal_export *export,
	const struct gsh_buffdesc *key,
	uint32_t flags)
{
	cache_entry_t *entry = NULL;
	cache_inode_status_t rc = 0;

	rc = up_get(key, &entry);
	if (rc == 0) {
		rc = cache_inode_invalidate(entry,
					    flags);
		cache_inode_put(entry);
	}

	return rc;
}

/**
 * @brief Update cached attributes
 *
 * @param[in] export FSAL export
 * @param[in] obj    Key to specify object
 * @param[in] attr   New attributes
 * @param[in] flags  Flags to govern update
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

static cache_inode_status_t update(
	struct fsal_export *export,
	const struct gsh_buffdesc *obj,
	struct attrlist *attr,
	uint32_t flags)
{
	cache_entry_t *entry = NULL;
	int rc = 0;
	/* Have necessary changes been made? */
	bool mutatis_mutandis = false;

	/* These cannot be updated, changing any of them is
	   tantamount to destroying and recreating the file. */
	if (FSAL_TEST_MASK(attr->mask,
			   ATTR_TYPE	    | ATTR_FSID       |
			   ATTR_FILEID      | ATTR_RAWDEV     |
			   ATTR_MOUNTFILEID | ATTR_RDATTR_ERR |
			   ATTR_GENERATION)) {
		return CACHE_INODE_INVALID_ARGUMENT;
	}

	/* Filter out garbage flags */

	if (flags & ~(fsal_up_update_filesize_inc |
		      fsal_up_update_atime_inc    |
		      fsal_up_update_creation_inc |
		      fsal_up_update_ctime_inc    |
		      fsal_up_update_mtime_inc    |
		      fsal_up_update_chgtime_inc  |
		      fsal_up_update_spaceused_inc)) {
		return CACHE_INODE_INVALID_ARGUMENT;
	}

	rc = up_get(obj, &entry);
	if (rc != 0) {
		return rc;
	}

	/* Knock things out if the link count falls to 0. */

	if ((flags & fsal_up_nlink) &&
	    (attr->numlinks == 0) ) {
		rc = cache_inode_invalidate(
			entry,
			(CACHE_INODE_INVALIDATE_ATTRS |
			 CACHE_INODE_INVALIDATE_CLOSE));
	}


	if (rc != 0) {
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	if (FSAL_TEST_MASK(attr->mask,
			   ATTR_SIZE) &&
	    ((flags & ~fsal_up_update_filesize_inc) ||
	     (entry->obj_handle->attributes.filesize <=
	      attr->filesize))) {
		entry->obj_handle->attributes.filesize =
			attr->filesize;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask,
			   ATTR_ACL)) {
		/**
		 * @todo Someone who knows the ACL code, please look
		 * over this.  We assume that the FSAL takes a
		 * reference on the supplied ACL that we can then hold
		 * onto.  This seems the most reasonable approach in
		 * an asynchronous call.
		 */

		/* This idiom is evil. */
		fsal_acl_status_t acl_status;

		nfs4_acl_release_entry(
			entry->obj_handle->attributes.acl,
			&acl_status);

		entry->obj_handle->attributes.acl =
			attr->acl;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask,
			   ATTR_MODE)) {
		entry->obj_handle->attributes.mode =
			attr->mode;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_NUMLINKS)) {
		entry->obj_handle->attributes.numlinks =
			attr->numlinks;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_OWNER)) {
		entry->obj_handle->attributes.owner =
			attr->owner;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_GROUP)) {
		entry->obj_handle->attributes.group =
			attr->group;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_ATIME) &&
	    ((flags & ~fsal_up_update_atime_inc) ||
	     (gsh_time_cmp(&attr->atime,
			   &entry->obj_handle->attributes.atime) == 1))) {
		entry->obj_handle->attributes.atime = attr->atime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CREATION) &&
	    ((flags & ~fsal_up_update_creation_inc) ||
	     (gsh_time_cmp(&attr->creation,
			   &entry->obj_handle->attributes.creation) == 1))) {
		entry->obj_handle->attributes.creation = attr->creation;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CTIME) &&
	    ((flags & ~fsal_up_update_ctime_inc) ||
	     (gsh_time_cmp(&attr->ctime,
		     &entry->obj_handle->attributes.ctime) == 1))) {
		entry->obj_handle->attributes.ctime = attr->ctime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask,
			   ATTR_MTIME) &&
	    ((flags & ~fsal_up_update_mtime_inc) ||
	     (gsh_time_cmp(&attr->mtime,
			   &entry->obj_handle->attributes.mtime) == 1))) {
		entry->obj_handle->attributes.mtime = attr->mtime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_SPACEUSED) &&
	    ((flags & ~fsal_up_update_spaceused_inc) ||
	     (entry->obj_handle->attributes.spaceused <=
	      attr->spaceused))) {
		entry->obj_handle->attributes.spaceused =
			attr->spaceused;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask,
			   ATTR_CHGTIME) &&
	    ((flags & ~fsal_up_update_chgtime_inc) ||
	     (gsh_time_cmp(
		     &attr->chgtime,
		     &entry->obj_handle->attributes.chgtime) == 1))) {
		entry->obj_handle->attributes.chgtime = attr->chgtime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CHANGE)) {
		entry->obj_handle->attributes.change = attr->change;
		mutatis_mutandis = true;
	}

	if (mutatis_mutandis) {
		cache_inode_fixup_md(entry);
		/* If directory can not trust content anymore. */
		if (entry->type == DIRECTORY)
			cache_inode_invalidate(entry,
				       CACHE_INODE_INVALIDATE_CONTENT |
				       CACHE_INODE_INVALIDATE_GOT_LOCK);
	} else {
		cache_inode_invalidate(entry,
				       CACHE_INODE_INVALIDATE_ATTRS |
				       CACHE_INODE_INVALIDATE_GOT_LOCK);
		rc = CACHE_INODE_INCONSISTENT_ENTRY;
	}
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:
	cache_inode_put(entry);
	return rc;
}

/**
 * @brief Initiate a lock grant
 *
 * @param[in] export     The export in question
 * @param[in] file       The file in question
 * @param[in] owner      The lock owner
 * @param[in] lock_param description of the lock
 *
 * @return STATE_SUCCESS or errors.
 */

static state_status_t lock_grant(
	struct fsal_export *export,
	const struct gsh_buffdesc *file,
	void *owner,
	fsal_lock_param_t *lock_param)
{
	cache_entry_t *entry = NULL;
	int rc = 0;

	rc = up_get(file, &entry);
	if (rc == 0) {
		grant_blocked_lock_upcall(entry, owner, lock_param);

		if (entry) {
			cache_inode_put(entry);
		}
	} else {
		rc = STATE_NOT_FOUND;
	}

	return rc;
}

/**
 * @brief Signal lock availability
 *
 * @param[in] export     The export in question
 * @param[in] file       The file in question
 * @param[in] owner      The lock owner
 * @param[in] lock_param description of the lock
 *
 * @return STATE_SUCCESS or errors.
 */

static state_status_t lock_avail(
	struct fsal_export *export,
	const struct gsh_buffdesc *file,
	void *owner,
	fsal_lock_param_t *lock_param)
{
	cache_entry_t *entry = NULL;
	int rc = 0;

	rc = up_get(file, &entry);
	if (rc == 0) {
		available_blocked_lock_upcall(entry,
					      owner,
					      lock_param);

		if (entry) {
			cache_inode_put(entry);
		}
	} else {
		rc = STATE_NOT_FOUND;
	}

	return rc;
}

/**
 * @brief Add a link to a directory
 *
 * Add a link to a directory and, if the entry's attributes are valid,
 * increment the link count by one.
 *
 * @param[in] export FSAL export
 * @param[in] dirkey The directory holding the new link
 * @param[in] name   The name of the newly created link
 * @param[in] tarkey The target of the link, may be NULL if unknown
 *
 * @returns CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t up_link(
	struct fsal_export *export,
	const struct gsh_buffdesc *dirkey,
	const char *name,
	const struct gsh_buffdesc *tarkey)
{
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;
	/* The entry to look up */
	cache_entry_t *entry = NULL;
	/* Return code */
	cache_inode_status_t rc = CACHE_INODE_SUCCESS;

	rc = up_get(dirkey, &parent);
	if (rc != 0) {
		goto out;
	}

	if (!tarkey) {
		/* Don't let it serve a negative lookup from cache */
		atomic_clear_uint32_t_bits(&parent->flags,
					   CACHE_INODE_DIR_POPULATED);
		rc = cache_inode_lookup(parent,
					name,
					&synthetic_context,
					&entry);
		if (entry == NULL) {
			cache_inode_invalidate(
				parent,
				CACHE_INODE_INVALIDATE_CONTENT);
		}
		goto out;
	}

	rc = up_get(tarkey, &entry);
	if (rc != CACHE_INODE_SUCCESS) {
		cache_inode_invalidate(
			parent,
			CACHE_INODE_INVALIDATE_CONTENT);
		goto out;
	}

	rc = cache_inode_add_cached_dirent(parent,
					   name,
					   entry,
					   NULL);

	if (rc != CACHE_INODE_SUCCESS) {
		cache_inode_invalidate(
			parent,
			CACHE_INODE_INVALIDATE_CONTENT);
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	if (entry->flags &
	    CACHE_INODE_TRUST_ATTRS) {
		++entry->obj_handle->attributes.numlinks;
	}
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:

	if (entry) {
		cache_inode_put(entry);
	}

	if (parent) {
		cache_inode_put(parent);
	}

	return rc;
}

/**
 * @brief Remove a link from a directory
 *
 * Remove the name from the directory, and if the entry is cached,
 * decrement its link count.
 *
 * @param[in] export FSAL export
 * @param[in] dirkey Directory holding the link
 * @param[in] name   The name to be removed
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t up_unlink(
	struct fsal_export *export,
	const struct gsh_buffdesc *dirkey,
	const char *name)
{
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;
	/* Return code */
	cache_inode_status_t rc = CACHE_INODE_SUCCESS;
	/* The looked up directory entry */
	cache_inode_dir_entry_t *dirent = NULL;

	rc = up_get(dirkey, &parent);
	if (rc != CACHE_INODE_SUCCESS) {
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);
	dirent = cache_inode_avl_qp_lookup_s(parent, name, 1);
	if (dirent &&
	    ~(dirent->flags & DIR_ENTRY_FLAG_DELETED)) {
		cih_latch_t latch;
		cache_entry_t *entry =
			cih_get_by_key_latched(&dirent->ckey,
					       &latch,
					       CIH_GET_UNLOCK_ON_MISS,
					       __func__, __LINE__);
		if (entry) {
			cache_inode_lru_ref(entry, 0);
			PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
			if (entry->flags &
			    CACHE_INODE_TRUST_ATTRS) {
				/* No underflow, just in case */
				if (entry->obj_handle->attributes.numlinks) {
					--entry->obj_handle
						->attributes.numlinks;
				}
				if (entry->obj_handle->attributes
				    .numlinks == 0) {
					cih_remove_latched(entry, &latch,
							   CIH_REMOVE_UNLOCK);
				}
			}
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			cache_inode_put(entry);
		}
		rc = cache_inode_remove_cached_dirent(parent,
						      name,
						      &synthetic_context);
	}
	PTHREAD_RWLOCK_unlock(&parent->content_lock);

	out:

	if (parent) {
		cache_inode_put(parent);
	}

	return rc;
}

/**
 * @brief Move-from action
 *
 * This function removes the name from the directory, but does not
 * modify the link count.
 *
 * @param[in] export FSAL export
 * @param[in] dirkey Directory holding the name
 * @param[in] name The name to be moved
 *
 * @return CACHE_INODE_STATUS or errors.
 */

cache_inode_status_t move_from(
	struct fsal_export *export,
	const struct gsh_buffdesc *dirkey,
	const char *name)
{
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;
	/* Return code */
	cache_inode_status_t rc = CACHE_INODE_SUCCESS;

	rc = up_get(dirkey, &parent);
	if (rc != 0) {
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);
	cache_inode_remove_cached_dirent(parent,
					 name,
					 &synthetic_context);
	PTHREAD_RWLOCK_unlock(&parent->content_lock);
	cache_inode_put(parent);

out:
	if (parent) {
		cache_inode_put(parent);
	}

	return rc;
}

/**
 * @brief Move-to handler
 *
 * This function adds a name to a directory, but does not touch the
 * number of links on the entry.
 *
 * @param[in] export FSAL export
 * @param[in] dirkey Directory receiving the link
 * @param[in] name   The name of the link
 * @param[in] tarkey The target of the link, may be NULL if unknown
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t move_to(
	struct fsal_export *export,
	const struct gsh_buffdesc *dirkey,
	const char *name,
	const struct gsh_buffdesc *tarkey)
{
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;
	/* Return code */
	cache_inode_status_t rc = CACHE_INODE_SUCCESS;
	/* The entry to look up */
	cache_entry_t *entry = NULL;

	rc = up_get(dirkey, &parent);

	if (rc != CACHE_INODE_SUCCESS) {
		goto out;
	}

	if (!tarkey) {
		/* If the FSAL didn't specify a target, just do a
		   lookup and let it cache. */

		/* Don't let it serve a negative lookup from cache */
		atomic_clear_uint32_t_bits(&parent->flags,
					   CACHE_INODE_DIR_POPULATED);
		rc = cache_inode_lookup(parent,
					name,
					&synthetic_context,
					&entry);
		if (entry == NULL) {
			cache_inode_invalidate(
				parent,
				CACHE_INODE_INVALIDATE_CONTENT);
		}
		goto out;
	}
	rc = up_get(tarkey, &entry);
	if (rc != CACHE_INODE_SUCCESS) {
		cache_inode_invalidate(
			parent,
			CACHE_INODE_INVALIDATE_CONTENT);
		goto out;
	}

	rc = cache_inode_add_cached_dirent(parent,
					   name,
					   entry,
					   NULL);
	if (rc != CACHE_INODE_SUCCESS) {
		cache_inode_invalidate(
			parent,
			CACHE_INODE_INVALIDATE_CONTENT);
		goto out;
	}
out:
	if (entry) {
		cache_inode_put(entry);
	}

	if (parent) {
		cache_inode_put(parent);
	}

	return rc;
}

/**
 * @brief Rename operation
 *
 * If a parent directory is in the cache, rename the given entry.  On
 * error, invalidate the whole thing.
 *
 *
 * @param[in] export FSAL export
 * @param[in] dirkey Directory holding the names
 * @param[in] old    The original name
 * @param[in] new    The new name
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t up_rename(
	struct fsal_export *export,
	const struct gsh_buffdesc *dirkey,
	const char *old,
	const char *new)
{
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;
	/* Return code */
	cache_inode_status_t rc = CACHE_INODE_SUCCESS;

	rc = up_get(dirkey, &parent);
	if (rc != 0) {
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&parent->content_lock);
	rc = cache_inode_rename_cached_dirent(parent,
					      old,
					      new,
					      &synthetic_context);
	if (rc != CACHE_INODE_SUCCESS) {
		cache_inode_invalidate(parent,
				       CACHE_INODE_INVALIDATE_CONTENT);
	}
	PTHREAD_RWLOCK_unlock(&parent->content_lock);


out:
	if (parent) {
		cache_inode_put(parent);
	}

	return rc;
}

/**
 * @brief Create layout recall state
 *
 * This function creates the layout recall state and work list for a
 * LAYOUTRECALL operation on a file.  The state lock on the entry must
 * be held for write when this function is called.
 *
 * @param[in,out] entry   The entry on which to send the recall
 * @param[in]     type    The layout type
 * @param[in]     offset  The offset of the interval to recall
 * @param[in]     length  The length of the interval to recall
 * @param[in]     cookie  The recall cookie (to be returned to the FSAL
 *                        on the final return satisfying this recall.)
 * @param[in]     spec    Lets us be fussy about what clients we send
 *                        to. May be NULL.
 * @param[out]    recout  The recall object
 *
 * @retval STATE_SUCCESS if successfully queued.
 * @retval STATE_INVALID_ARGUMENT if the range is zero or overflows.
 * @retval STATE_NOT_FOUND if no layouts satisfying the range exist.
 * @retval STATE_MALLOC_ERROR if there was insufficient memory to construct the
 *         recall state.
 */

static state_status_t create_file_recall(
	cache_entry_t *entry,
	layouttype4 type,
	const struct pnfs_segment *segment,
	void *cookie,
	struct layoutrecall_spec *spec,
	struct state_layout_recall_file **recout)
{
	/* True if no layouts matching the request have been found */
	bool none = true;
	/* Iterator over all states on the cache entry */
	struct glist_head *state_iter = NULL;
	/* Error return code */
	state_status_t rc = STATE_SUCCESS;
	/* The recall object referenced by future returns */
	struct state_layout_recall_file *recall
		= gsh_malloc(sizeof(struct state_layout_recall_file));

    if (!recall) {
        rc = STATE_MALLOC_ERROR;
        goto out;
    }

	init_glist(&recall->entry_link);
	init_glist(&recall->state_list);
	recall->entry = entry;
	recall->type = type;
	recall->segment = *segment;
	recall->recall_cookie = cookie;

	if ((segment->length == 0) ||
	    ((segment->length != UINT64_MAX) &&
	     (segment->offset <= UINT64_MAX - segment->length))) {
		rc = STATE_INVALID_ARGUMENT;
		goto out;
	}

	glist_for_each(state_iter,
		       &entry->state_list) {
		/* Entry in the state list */
		struct recall_state_list *list_entry = NULL;
		/* Iterator over segments on this state */
		struct glist_head *seg_iter = NULL;
		/* The state under examination */
		state_t *s = glist_entry(state_iter,
					 state_t,
					 state_list);
		/* Does this state have a matching segment? */
		bool match = false;

		if (spec) {
			switch (spec->how) {
			case layoutrecall_howspec_exactly:
				if (spec->u.client !=
				    s->state_owner->so_owner.so_nfs4_owner
				    .so_clientid) {
					continue;
				}
				break;

			case layoutrecall_howspec_complement:
				if (spec->u.client ==
				    s->state_owner->so_owner.so_nfs4_owner
				    .so_clientid) {
					continue;
				}
				break;

			case layoutrecall_not_specced:
				break;
			}
		}

		if ((s->state_type != STATE_TYPE_LAYOUT) ||
		    (s->state_data.layout.state_layout_type !=
		     type)) {
			continue;
		}
		glist_for_each(seg_iter,
			       &s->state_data.layout.state_segments) {
			state_layout_segment_t *g
				= glist_entry(seg_iter,
					      state_layout_segment_t,
					      sls_state_segments);
			pthread_mutex_lock(&g->sls_mutex);
			if (pnfs_segments_overlap(segment,
						  &g->sls_segment)) {
				match = true;
			}
			pthread_mutex_unlock(&g->sls_mutex);
		}
		if (match) {
			/**
			 * @todo This is where you would record that a
			 * recall was initiated.  The range recalled
			 * is specified in @c segment.  The clientid
			 * is in
			 * s->state_owner->so_owner.so_nfs4_owner.so_clientid
			 * But you may want to ignore this location entirely.
			 */
			list_entry = gsh_malloc(
				sizeof(struct recall_state_list));
			if (!list_entry) {
				rc = STATE_MALLOC_ERROR;
				goto out;
			}
			init_glist(&list_entry->link);
			list_entry->state = s;
			glist_add_tail(&recall->state_list, &list_entry->link);
			none = false;
		}
	}

	if (none) {
		rc = STATE_NOT_FOUND;
	}

	if (!recall) {
		rc = STATE_MALLOC_ERROR;
	}

out:

	if ((rc != STATE_SUCCESS) &&
	    recall) {
		/* Iterator over work queue, for disposing of it. */
		struct glist_head *wi = NULL;
		/* Preserved next over work queue. */
		struct glist_head *wn = NULL;

		glist_for_each_safe(wi,
				    wn,
				    &recall->state_list) {
			struct recall_state_list *list_entry
				= glist_entry(wi,
					      struct recall_state_list,
					      link);
			glist_del(&list_entry->link);
			gsh_free(list_entry);
		}
		gsh_free(recall);
	} else {
		glist_add_tail(&entry->layoutrecall_list,
			       &recall->entry_link);
		*recout = recall;
	}

	return rc;
}

static void layoutrecall_one_call(void *arg);

/**
 * @brief Data used to handle the response to CB_LAYOUTRECALL
 */

struct layoutrecall_cb_data {
	char stateid_other[OTHERSIZE];  /*< "Other" part of state id */
	struct pnfs_segment segment; /*< Segment to recall */
	nfs_cb_argop4 arg; /*< So we don't free */
	nfs_client_id_t *client; /*< The client we're calling. */
	struct timespec first_recall; /*< Time of first recall */
	uint32_t attempts; /*< Number of times we've recalled */
};


/**
 * @brief Initiate layout recall
 *
 * This function validates the recall, creates the recall object, and
 * sends out CB_LAYOUTRECALL messages.
 *
 * @param[in] export      FSAL export
 * @param[in] handle      Handle on which the layout is held
 * @param[in] layout_type The type of layout to recall
 * @param[in] changed     Whether the layout has changed and the
 *                        client ought to finish writes through MDS
 * @param[in] segment     Segment to recall
 * @param[in] cookie      A cookie returned with the return that
 *                        completely satisfies a recall
 * @param[in] spec        Lets us be fussy about what clients we send
 *                        to. May be NULL.
 *
 * @retval STATE_SUCCESS if scheduled.
 * @retval STATE_NOT_FOUND if no matching layouts exist.
 * @retval STATE_INVALID_ARGUMENT if a nonsensical layout recall has
 *         been specified.
 * @retval STATE_MALLOC_ERROR if there was insufficient memory to construct the
 *         recall state.
 */
state_status_t layoutrecall(
	struct fsal_export *export,
	const struct gsh_buffdesc *handle,
	layouttype4 layout_type,
	bool changed,
	const struct pnfs_segment *segment,
	void *cookie,
	struct layoutrecall_spec *spec)
{
	/* Return code */
	state_status_t rc = STATE_SUCCESS;
	/* Cache entry on which to operate */
	cache_entry_t *entry = NULL;
	/* The recall object */
	struct state_layout_recall_file *recall = NULL;
	/* Iterator over the work list */
	struct glist_head *wi = NULL;

	rc = cache_inode_status_to_state_status(up_get(handle, &entry));
	if (rc != STATE_SUCCESS) {
		return rc;
	}

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	/* We build up the list before consuming it so that we have
	   every state on the list before we start executing returns. */
	rc = create_file_recall(entry, layout_type, segment, cookie,
				spec, &recall);
	PTHREAD_RWLOCK_unlock(&entry->state_lock);
	if (rc != STATE_SUCCESS) {
		goto out;
	}

	/**
	 * @todo This leaves us open to a race if a return comes in
	 * while we're traversing the work list.
	 */
	glist_for_each(wi,
		       &recall->state_list) {
		/* The current entry in the queue */
		struct recall_state_list *g
			= glist_entry(wi,
				      struct recall_state_list,
				      link);
		struct state_t *s = g->state;
		struct layoutrecall_cb_data *cb_data;
		cache_entry_t *entry = s->state_entry;
		nfs_cb_argop4 *arg;
		CB_LAYOUTRECALL4args *cb_layoutrec;

		cb_data = gsh_malloc(sizeof(struct layoutrecall_cb_data));
		arg = &cb_data ->arg;
		arg->argop = NFS4_OP_CB_LAYOUTRECALL;
		cb_layoutrec = &arg->nfs_cb_argop4_u.opcblayoutrecall;
		PTHREAD_RWLOCK_wrlock(&entry->state_lock);
		cb_layoutrec->clora_type = layout_type;
		cb_layoutrec->clora_iomode = segment->io_mode;
		cb_layoutrec->clora_changed = changed;
		cb_layoutrec->clora_recall.lor_recalltype
			= LAYOUTRECALL4_FILE;
		cb_layoutrec->clora_recall.layoutrecall4_u
			.lor_layout.lor_offset = segment->offset;
		cb_layoutrec->clora_recall.layoutrecall4_u
			.lor_layout.lor_length = segment->length;
		cb_layoutrec->clora_recall.layoutrecall4_u
			.lor_layout.lor_fh.nfs_fh4_len
			= sizeof(struct alloc_file_handle_v4);
		cb_layoutrec->clora_recall.layoutrecall4_u
			.lor_layout.lor_fh.nfs_fh4_val
			= gsh_malloc(sizeof(struct alloc_file_handle_v4));
		nfs4_FSALToFhandle(&cb_layoutrec->clora_recall.
				   layoutrecall4_u.lor_layout.lor_fh,
				   entry->obj_handle);
		update_stateid(s,
			       &cb_layoutrec->clora_recall.layoutrecall4_u
			       .lor_layout.lor_stateid,
			       NULL,
			       "LAYOUTRECALL");
		memcpy(cb_data->stateid_other,
		       s->stateid_other,
		       OTHERSIZE);
		cb_data->segment = *segment;
		cb_data->client = s->state_owner->so_owner
			.so_nfs4_owner.so_clientrec;
		cb_data->attempts = 0;
		PTHREAD_RWLOCK_unlock(&entry->state_lock);
		layoutrecall_one_call(cb_data);
	}

out:
	cache_inode_put(entry);
	return rc;
}

/**
 * @brief Free a CB_LAYOUTRECALL
 *
 * @param[in] op Operation to free
 */

static void free_layoutrec(nfs_cb_argop4 *op)
{
	gsh_free(op->nfs_cb_argop4_u.opcblayoutrecall.clora_recall
		 .layoutrecall4_u.lor_layout.lor_fh.nfs_fh4_val);
}

/**
 * @brief Complete a CB_LAYOUTRECALL
 *
 * This function handles the client response to a layoutrecall.  In
 * the event of success it does nothing.  In the case of most errors,
 * it revokes the layout.
 *
 * For NOMATCHINGLAYOUT, under the agreed-upon interpretation of the
 * forgetful model, it acts as if the client had returned a layout
 * exactly matching the recall.
 *
 * For DELAY, it backs off in plateaus, then revokes the layout if the
 * period of delay has surpassed the lease period.
 *
 * @param[in] call  The RPC call being completed
 * @param[in] hook  The hook itself
 * @param[in] arg   Supplied argument (the callback data)
 * @param[in] flags There are no flags.
 *
 * @return 0, constantly.
 */

static int32_t layoutrec_completion(rpc_call_t* call, rpc_call_hook hook,
				    void* arg, uint32_t flags)
{
	struct layoutrecall_cb_data *cb_data = arg;
	bool deleted = false;
	state_t *state = NULL;

	LogFullDebug(COMPONENT_NFS_CB,"status %d arg %p",
		     call->cbt.v_u.v4.res.status, arg);

	/* Get this out of the way up front */
	if (hook != RPC_CALL_COMPLETE) {
		goto revoke;
	}

	if (call->cbt.v_u.v4.res.status == NFS4_OK) {
		/**
		 * @todo This is where you would record that a
		 * recall was acknowledged and that a layoutreturn
		 * will be sent later.
		 * The number of times we retried the call is
		 * specified in cb_data->attempts and the time we
		 * specified the first call is in
		 * cb_data->first_recall.
		 * We don't have the clientid here.  If you want it,
		 * we could either move the stateid look up to be
		 * above this point in the function, or we could stash
		 * the clientid in cb_data.
		 */
		gsh_free(cb_data);
		free_layoutrec(&call->cbt.v_u.v4.args.argarray.argarray_val[1]);
		nfs41_complete_single(call, hook, arg, flags);
		return 0;
	} else if (call->cbt.v_u.v4.res.status == NFS4ERR_DELAY) {
		struct timespec current;
		nsecs_elapsed_t delay;

		now(&current);
		if (timespec_diff(&cb_data->first_recall,
				  &current) >
		    (nfs_param.nfsv4_param.lease_lifetime *
		     NS_PER_SEC)) {
			goto revoke;
		}
		if (cb_data->attempts < 5) {
			delay = 0;
		} else if (cb_data->attempts < 10) {
			delay = 1 * NS_PER_MSEC;
		} else if (cb_data->attempts < 20) {
			delay = 10 * NS_PER_MSEC;
		} else if (cb_data->attempts < 30) {
			delay = 100 * NS_PER_MSEC;
		} else {
			delay = 1 * NS_PER_SEC;
		}
		free_layoutrec(&call->cbt.v_u.v4.args.argarray.
			       argarray_val[1]);
		nfs41_complete_single(call, hook, arg, flags);
		delayed_submit(layoutrecall_one_call,
			       cb_data,
			       delay);
		return 0;
	}

	/**
	 * @todo Better error handling later when we have more
	 * session/revocation infrastructure.
	 */

revoke:
	/* If we don't find the state, there's nothing to return. */
	if (nfs4_State_Get_Pointer(cb_data->stateid_other,
				   &state)) {
		struct req_op_context return_context = {
			.creds = &synthetic_creds,
			.caller_addr = NULL
		};
		/**
		 * @todo This is where you would record that a
		 * recall was completed, one way or the other.
		 * The clientid is specified in
		 * state->state_owner->so_owner.so_nfs4_owner.so_clientid
		 * The number of times we retried the call is
		 * specified in cb_data->attempts and the time we
		 * specified the first call is in
		 * cb_data->first_recall.  If
		 * call->cbt.v_u.v4.res.status is
		 * NFS4ERR_NOMATCHING_LAYOUT it was a successful
		 * return, otherwise we count it as an error.
		 */

		PTHREAD_RWLOCK_wrlock(&state->state_entry->state_lock);
		return_context.clientid = (&state->state_owner->so_owner.
					   so_nfs4_owner.so_clientid);
		nfs4_return_one_state(state->state_entry,
				      &return_context,
				      LAYOUTRETURN4_FILE,
				      (call->cbt.v_u.v4.res.status ==
				       NFS4ERR_NOMATCHING_LAYOUT) ?
				      circumstance_client :
				      circumstance_revoke,
				      state,
				      cb_data->segment,
				      0,
				      NULL,
				      &deleted,
				      false);
		PTHREAD_RWLOCK_unlock(&state->state_entry->state_lock);
	}
	gsh_free(cb_data);
	free_layoutrec(&call->cbt.v_u.v4.args.argarray.argarray_val[1]);
	nfs41_complete_single(call, hook, arg, flags);
	return 0;
}

/**
 * @brief Return one layout on error
 *
 * This is only invoked in the case of a send error on the first
 * attempt to issue a CB_LAYOUTRECALL, so that we don't call into the
 * FSAL's layoutreturn function while its layoutrecall function may be
 * holding locks.
 *
 * @param[in] arg Structure holding all arguments, so we can queue
 *                this function in delayed_exec.
 */

static void return_one_async(void *arg)
{
	struct layoutrecall_cb_data *cb_data = arg;
	state_t *s;
	bool deleted = false;
	struct req_op_context return_context = {
		.creds = &synthetic_creds,
		.caller_addr = NULL
	};


	if (nfs4_State_Get_Pointer(cb_data->stateid_other,
				   &s)) {
		PTHREAD_RWLOCK_wrlock(&s->state_entry->state_lock);
		return_context.clientid =
			&s->state_owner->so_owner.so_nfs4_owner.so_clientid;

		nfs4_return_one_state(s->state_entry,
				      &return_context,
				      LAYOUTRETURN4_FILE,
				      circumstance_revoke,
				      s,
				      cb_data->segment,
				      0,
				      NULL,
				      &deleted,
				      false);
	}
	gsh_free(cb_data);
}

/**
 * @brief Send one layoutrecall to one client
 *
 * @param[in] arg Structure holding all arguments, so we can queue
 *                this function in delayed_exec for retry on NFS4ERR_DELAY.
 */

static void layoutrecall_one_call(void *arg)
{
	struct layoutrecall_cb_data *cb_data = arg;
	state_t *s;
	int code;

	if (cb_data->attempts == 0) {
		now(&cb_data->first_recall);
	}
	if (nfs4_State_Get_Pointer(cb_data->stateid_other,
				   &s)) {
		PTHREAD_RWLOCK_wrlock(&s->state_entry->state_lock);
		code = nfs_rpc_v41_single(cb_data->client,
					  &cb_data->arg,
					  &s->state_refer,
					  layoutrec_completion,
					  cb_data,
					  free_layoutrec);
		if (code != 0) {
			/**
			 * @todo On failure to submit a callback, we
			 * ought to give the client at least one lease
			 * period to establish a back channel before
			 * we start revoking state.  We don't have the
			 * infrasturcture to properly handle layout
			 * revocation, however.  Once we get the
			 * capability to revoke layouts we should
			 * queue requests on the clientid, obey the
			 * retransmission rule, and provide a callback
			 * to dispose of a call and revoke state after
			 * some number of lease periods.
			 *
			 * At present we just assume the client has
			 * gone completely out to lunch and fake a
			 * return.
			 */

			/**
			 * @todo This is where you would record that a
			 * recall failed.  (It indicates a transport error.)
			 * The clientid is specified in
			 * s->state_owner->so_owner.so_nfs4_owner.so_clientid
			 * The number of times we retried the call is
			 * specified in cb_data->attempts and the time
			 * we specified the first call is in
			 * cb_data->first_recall.
			 */
			if (cb_data->attempts == 0) {
				delayed_submit(return_one_async,
					       cb_data,
					       0);
			} else {
				bool deleted = false;

				struct req_op_context return_context = {
					.creds = &synthetic_creds,
					.caller_addr = NULL,
					.clientid = (&s->state_owner->so_owner.
						     so_nfs4_owner.so_clientid)
				};
				nfs4_return_one_state(s->state_entry,
						      &return_context,
						      LAYOUTRETURN4_FILE,
						      circumstance_revoke,
						      s,
						      cb_data->segment,
						      0,
						      NULL,
						      &deleted,
						      false);
				gsh_free(cb_data);
			}
		} else {
			++cb_data->attempts;
		}
		PTHREAD_RWLOCK_unlock(&s->state_entry->state_lock);
	} else {
		gsh_free(cb_data);
	}
}

/**
 * @brief Data for CB_NOTIFY and CB_NOTIFY_DEVICEID response handler
 */

struct cb_notify {
	nfs_cb_argop4 arg; /*< Arguments (so we can free them) */
	struct notify4 notify; /*< For notify response */
	struct notify_deviceid_delete4 notify_del; /*< For notify_deviceid
						       response.*/
};

/**
 * @brief Handle CB_NOTIFY_DEVICE response
 *
 * @param[in] call  The RPC call being completed
 * @param[in] hook  The hook itself
 * @param[in] arg   Supplied argument (the callback data)
 * @param[in] flags There are no flags.
 *
 * @return 0, constantly.
 */

static int32_t notifydev_completion(rpc_call_t* call, rpc_call_hook hook,
				    void* arg, uint32_t flags)
{
	LogFullDebug(COMPONENT_NFS_CB,"status %d arg %p",
		     call->cbt.v_u.v4.res.status, arg);
	gsh_free(arg);
	return 0;
}

/**
 * The arguments for devnotify_client_callback packed up in a struct
 */

struct devnotify_cb_data {
	uint64_t exportid;
	notify_deviceid_type4 notify_type;
	layouttype4 layout_type;
	uint64_t devid;
};

/**
 * @brief Send a single notifydev to a single client
 *
 * @param[in] clientid  The client record
 * @param[in] devnotify The device notify args
 *
 * @return True on success, false on error.
 */

static bool devnotify_client_callback(nfs_client_id_t *clientid,
				      void *devnotify)
{
	int code = 0;
	CB_NOTIFY_DEVICEID4args *cb_notify_dev;
	struct cb_notify *arg;
	struct devnotify_cb_data *devicenotify = devnotify;
	uint64_t *quad;

	if (clientid) {
		LogFullDebug(COMPONENT_NFS_CB,
			     "CliP %p ClientID=%"PRIx64" ver %d",
			     clientid, clientid->cid_clientid,
			     clientid->cid_minorversion);
	} else {
		return false;
	}

	/* free in notifydev_completion */
	arg = gsh_malloc(sizeof(struct cb_notify));
	if (arg == NULL) {
		return false;
	}

	cb_notify_dev = &arg->arg.nfs_cb_argop4_u.opcbnotify_deviceid;

	arg->arg.argop = NFS4_OP_CB_NOTIFY_DEVICEID;

	cb_notify_dev->cnda_changes.cnda_changes_len = 1;
	cb_notify_dev->cnda_changes.cnda_changes_val = &arg->notify;
	arg->notify.notify_mask.bitmap4_len = 1;
	arg->notify.notify_mask.map[0] = devicenotify->notify_type;
	arg->notify.notify_vals.notifylist4_len
		= sizeof(struct notify_deviceid_delete4);

	arg->notify.notify_vals.notifylist4_val = (char *)&arg->notify_del;
	arg->notify_del.ndd_layouttype = devicenotify->layout_type;
	quad = (uint64_t *)&arg->notify_del.ndd_deviceid;
	*quad = nfs_htonl64(devicenotify->exportid);
	++quad;
	*quad = nfs_htonl64(devicenotify->devid);
	code = nfs_rpc_v41_single(clientid,
				  &arg->arg,
				  NULL,
				  notifydev_completion,
				  &arg->arg,
				  NULL);
	if (code != 0)
		gsh_free(arg);

	return true;
}


/**
 * @brief Remove or change a deviceid
 *
 * @param[in] export      Export responsible for the device ID
 * @param[in] notify_type Change or remove
 * @param[in] layout_type The layout type affected
 * @param[in] devid       The lower quad of the device id, unique
 *                        within this export
 * @param[in] immediate   Whether the change is immediate (in the case
 *                        of a change.)
 *
 * @return STATE_SUCCESS or errors.
 */

state_status_t notify_device(
	struct fsal_export *export,
	notify_deviceid_type4 notify_type,
	layouttype4 layout_type,
	uint64_t devid,
	bool immediate)
{
	struct devnotify_cb_data cb_data = {
		.exportid = export->exp_entry->id,
		.notify_type = notify_type,
		.layout_type = layout_type,
		.devid = devid
	};

	nfs41_foreach_client_callback(devnotify_client_callback,
				      &cb_data);

	return STATE_SUCCESS;
}

/**
 * @brief Handle the reply to a DELEGRECALL
 *
 * @param[in] call  The RPC call being completed
 * @param[in] hook  The hook itself
 * @param[in] arg   Supplied argument (the callback data)
 * @param[in] flags There are no flags.
 *
 * @return 0, constantly.
 */

static int32_t delegrecall_completion_func(rpc_call_t* call,
					   rpc_call_hook hook,
					   void* arg, uint32_t flags)
{
	char *fh;

	LogDebug(COMPONENT_NFS_CB, "%p %s", call,
		 (hook == RPC_CALL_ABORT) ?
		 "RPC_CALL_ABORT" :
		 "RPC_CALL_COMPLETE");
	switch (hook) {
	case RPC_CALL_COMPLETE:
		/* potentially, do something more interesting here */
		LogDebug(COMPONENT_NFS_CB, "call result: %d", call->stat);
		fh = call->cbt.v_u.v4.args.argarray.argarray_val
			->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val;
		gsh_free(fh);
		cb_compound_free(&call->cbt);
		break;
	default:
		LogDebug(COMPONENT_NFS_CB, "%p unknown hook %d", call, hook);
		break;
	}
	return (0);
}

/**
 * @brief Send one delegation recall to one client
 *
 * @param[in] found_entry Lock entry covering the delegation
 * @param[in] entry       File on which the delegation is held
 */

static void delegrecall_one(state_lock_entry_t *found_entry,
			    cache_entry_t *entry)
{
	char *maxfh;
	int32_t code = 0;
	rpc_call_channel_t *chan;
	rpc_call_t *call;
	nfs_client_id_t *clid = NULL;
	nfs_cb_argop4 argop[1];

	maxfh = gsh_malloc(NFS4_FHSIZE);     // free in cb_completion_func()
	if (maxfh == NULL) {
		LogDebug(COMPONENT_FSAL_UP,
			 "FSAL_UP_DELEG: no mem, failed.");
		/* Not an error. Expecting some nodes will not have it
		 * in cache in a cluster. */
		return;
	}
	code  =
	nfs_client_id_get_confirmed(found_entry->sle_owner->so_owner
				    .so_nfs4_owner.so_clientid, &clid);
	if (code != CLIENT_ID_SUCCESS) {
		LogCrit(COMPONENT_NFS_CB,
			"No clid record  code %d", code);
        gsh_free(maxfh);
		return;
	}
	chan = nfs_rpc_get_chan(clid, NFS_RPC_FLAG_NONE);
	if (!chan) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
        gsh_free(maxfh);
		return;
	}
	if (!chan->clnt) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
        gsh_free(maxfh);
		return;
	}
	/* allocate a new call--freed in completion hook */
	call = alloc_rpc_call();
	call->chan = chan;

	/* setup a compound */
	cb_compound_init_v4(&call->cbt, 6, 0,
			    clid->cid_cb.v40.cb_callback_ident,
			    "brrring!!!", 10);

	memset(argop, 0, sizeof(nfs_cb_argop4));
	argop->argop = NFS4_OP_CB_RECALL;
	argop->nfs_cb_argop4_u.opcbrecall.stateid.seqid
		= found_entry->sle_state->state_seqid;
	memcpy(argop->nfs_cb_argop4_u.opcbrecall.stateid.other,
	       found_entry->sle_state->stateid_other, OTHERSIZE);
	argop->nfs_cb_argop4_u.opcbrecall.truncate = TRUE;

	/* Convert it to a file handle */
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_len = 0;
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val = maxfh;

	/* Building a new fh */
	if (!nfs4_FSALToFhandle(&argop->nfs_cb_argop4_u.opcbrecall.fh,
				entry->obj_handle)) {
        gsh_free(call);
		return;
	}

	/* add ops, till finished (dont exceed count) */
	cb_compound_add_op(&call->cbt, argop);

	/* set completion hook */
	call->call_hook = delegrecall_completion_func;

	/* call it (here, in current thread context) */
	code = nfs_rpc_submit_call(call, NULL, NFS_RPC_FLAG_NONE);

	return;
};

/**
 * @brief Recall a delegation
 *
 * @param[in] export FSAL export
 * @param[in] handle Handle on which the delegation is held
 *
 * @return STATE_SUCCESS or errors.
 */

state_status_t delegrecall(
	struct fsal_export *export,
	const struct gsh_buffdesc *handle)
{
	cache_entry_t *entry = NULL;
	struct glist_head  *glist;
	state_lock_entry_t *found_entry = NULL;
	state_status_t rc = 0;

	rc = cache_inode_status_to_state_status(up_get(handle, &entry));
	if (rc != STATE_SUCCESS) {
		LogDebug(COMPONENT_FSAL_UP,
			 "FSAL_UP_DELEG: cache inode get failed, rc %d", rc);
		/* Not an error. Expecting some nodes will not have it
		 * in cache in a cluster. */
		return rc;
	}

	LogDebug(COMPONENT_FSAL_UP,
		 "FSAL_UP_DELEG: Invalidate cache found entry %p type %u",
		 entry, entry->type);

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	glist_for_each(glist, &entry->object.file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		if (found_entry != NULL && found_entry->sle_state != NULL) {
			LogDebug(COMPONENT_NFS_CB,"found_entry %p",
				 found_entry);
			delegrecall_one(found_entry, entry);
		}
	}
	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_put(entry);

	return rc;
}

/**
 * @brief The top level vector of operations
 */

struct fsal_up_vector fsal_up_top = {
	.lock_grant = lock_grant,
	.lock_avail = lock_avail,
	.invalidate = invalidate,
	.update = update,
	.link = up_link,
	.unlink = up_unlink,
	.move_from = move_from,
	.move_to = move_to,
	.rename = up_rename,
	.layoutrecall = layoutrecall,
	.notify_device = notify_device,
	.delegrecall =delegrecall
};

/** @} */
