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
 * @file fsal_up_thread.c
 *
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

/* Fake root credentials for caching lookup */
static struct user_cred synthetic_creds = {
	.caller_uid = 0,
	.caller_gid = 0,
	.caller_glen = 0,
	.caller_garray = NULL
};

/* Synthetic request context */
static struct req_op_context synthetic_context = {
	.creds = &synthetic_creds,
	.caller_addr = NULL,
	.clientid = NULL
};

static int32_t cb_completion_func(rpc_call_t* call, rpc_call_hook hook,
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
 * @brief Invalidate cached attributes and content
 *
 * We call into the cache and invalidate at once, since the operation
 * is inexpensive by design.
 *
 * @param[in,out] ctx Thread context, holding event
 *
 * @retval 0 on success.
 * @retval ENOENT if the entry is not in the cache.  (Harmless, since
 *         if it's not cached, there's nothing to invalidate.)
 */

static int invalidate_imm(struct fsal_up_event *e)

{
	struct fsal_up_file *file = &e->file;
	cache_entry_t *entry = NULL;
	int rc = 0;

	LogDebug(COMPONENT_FSAL_UP,
		 "Calling cache_inode_invalidate()");

	rc = up_get(&file->key,
		    &entry);
	if (rc == 0) {
		cache_inode_invalidate(entry,
				       CACHE_INODE_INVALIDATE_ATTRS |
				       CACHE_INODE_INVALIDATE_CONTENT);
		cache_inode_put(entry);
	}

	return rc;
}

/**
 * @brief Immediate attribute function
 *
 * This function just performs basic validation of the parameters.
 *
 * @param[in,out] ctx Thread context, containing event
 *
 * @retval 0 on success.
 * @retval EINVAL if the update data are invalid.
 */

static int update_imm(struct fsal_up_event *e)
{
	struct fsal_up_event_update *update = &e->data.update;
	struct fsal_up_file *file = &e->file;
	cache_entry_t *entry = NULL;
	int rc = 0;

	/* These cannot be updated, changing any of them is
	   tantamount to destroying and recreating the file. */
	if (FSAL_TEST_MASK(update->attr.mask,
			   ATTR_SUPPATTR   | ATTR_TYPE	      |
			   ATTR_FSID       | ATTR_FILEID      |
			   ATTR_RAWDEV     | ATTR_MOUNTFILEID |
			   ATTR_RDATTR_ERR | ATTR_GENERATION)) {
		return EINVAL;
	}

	/* Filter out garbage flags */

	if (update->flags & ~(fsal_up_update_filesize_inc |
			      fsal_up_update_atime_inc    |
			      fsal_up_update_creation_inc |
			      fsal_up_update_ctime_inc    |
			      fsal_up_update_mtime_inc    |
			      fsal_up_update_chgtime_inc  |
			      fsal_up_update_spaceused_inc)) {
		return EINVAL;
	}

	LogDebug(COMPONENT_FSAL_UP,
		 "Calling cache_inode_invalidate()");

	rc = up_get(&file->key, &entry);
	if (rc == 0) {
		if ((update->flags & fsal_up_nlink) &&
		    (update->attr.numlinks == 0) ) {
			LogDebug(COMPONENT_FSAL_UP,
				 "nlink has become zero; close fds");
			cache_inode_invalidate(entry,
					       (CACHE_INODE_INVALIDATE_ATTRS |
						CACHE_INODE_INVALIDATE_CLOSE));
		} else {
			cache_inode_invalidate(entry,
					       CACHE_INODE_INVALIDATE_ATTRS);
		}

		cache_inode_put(entry);
	}
	return 0;
}

/**
 * @brief Execute the delayed attribute update
 *
 * Update the entry attributes in accord with the supplied attributes
 * and control flags.
 *
 * @param[in,out] ctx Thread context, containing event
 */

static void update_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_update *update = &e->data.update;
	struct fsal_up_file *file = &e->file;
	/* The cache entry upon which to operate */
	cache_entry_t *entry = NULL;
	/* Have necessary changes been made? */
	bool mutatis_mutandis = false;

	if (up_get(&file->key, &entry) == 0) {
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_SIZE) &&
		    ((update->flags & ~fsal_up_update_filesize_inc) ||
		     (entry->obj_handle->attributes.filesize <=
		      update->attr.filesize))) {
			entry->obj_handle->attributes.filesize =
				update->attr.filesize;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_ACL)) {
			/**
                         * @todo Someone who knows the ACL code,
                         * please look over this.  We assume that the
                         * FSAL takes a reference on the supplied ACL
                         * that we can then hold onto.  This seems the
                         * most reasonable approach in an asynchronous
                         * call.
                         */

			/* This idiom is evil. */
			fsal_acl_status_t acl_status;

			nfs4_acl_release_entry(
				entry->obj_handle->attributes.acl,
				&acl_status);

			entry->obj_handle->attributes.acl =
				update->attr.acl;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_MODE)) {
			entry->obj_handle->attributes.mode =
				update->attr.mode;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_NUMLINKS)) {
			entry->obj_handle->attributes.numlinks =
				update->attr.numlinks;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_OWNER)) {
			entry->obj_handle->attributes.owner =
				update->attr.owner;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_GROUP)) {
			entry->obj_handle->attributes.group =
				update->attr.group;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_ATIME) &&
		    ((update->flags & ~fsal_up_update_atime_inc) ||
		     (gsh_time_cmp(
			     &update->attr.atime,
			     &entry->obj_handle->attributes.atime) == 1))) {
			entry->obj_handle->attributes.atime =
				update->attr.atime;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_CREATION) &&
		    ((update->flags & ~fsal_up_update_creation_inc) ||
		     (gsh_time_cmp(
			     &update->attr.creation,
			     &entry->obj_handle->attributes.creation) == 1))) {
			entry->obj_handle->attributes.creation =
				update->attr.creation;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_CTIME) &&
		    ((update->flags & ~fsal_up_update_ctime_inc) ||
		     (gsh_time_cmp(
			     &update->attr.ctime,
			     &entry->obj_handle->attributes.ctime) == 1))) {
			entry->obj_handle->attributes.ctime =
				update->attr.ctime;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_MTIME) &&
		    ((update->flags & ~fsal_up_update_mtime_inc) ||
		     (gsh_time_cmp(
			     &update->attr.mtime,
			     &entry->obj_handle->attributes.mtime) == 1))) {
			entry->obj_handle->attributes.mtime =
				update->attr.mtime;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_SPACEUSED) &&
		    ((update->flags & ~fsal_up_update_spaceused_inc) ||
		     (entry->obj_handle->attributes.spaceused <=
		      update->attr.spaceused))) {
			entry->obj_handle->attributes.spaceused =
				update->attr.spaceused;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_CHGTIME) &&
		    ((update->flags & ~fsal_up_update_chgtime_inc) ||
		     (gsh_time_cmp(
			     &update->attr.chgtime,
			     &entry->obj_handle->attributes.chgtime) == 1))) {
			entry->obj_handle->attributes.chgtime =
				update->attr.chgtime;
			mutatis_mutandis = true;
		}

		if (FSAL_TEST_MASK(update->attr.mask,
				   ATTR_CHANGE)) {
			entry->obj_handle->attributes.change =
				update->attr.change;
			mutatis_mutandis = true;
		}

		if (mutatis_mutandis) {
			cache_inode_fixup_md(entry);
		}

		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		cache_inode_put(entry);
	}
}

/**
 * @brief Initiate a lock grant
 *
 * This function calls out to the SAL to grant a lock.  This is
 * handled in the immediate phase, because NSM operations have their
 * own queue.
 *
 * @param[in,out] ctx Thread context, containing event
 *
 * @retval 0 if successfully queued.
 * @retval ENOENT if the entry is not in the cache (probably can't
 *         happen).
 */

static int lock_grant_imm(struct fsal_up_event *e)
{
	struct fsal_up_event_lock_grant *grant
		= &e->data.lock_grant;
	struct fsal_up_file *file = &e->file;
	cache_entry_t *entry = NULL;
	int rc = 0;

	LogDebug(COMPONENT_FSAL_UP,
		 "calling cache_inode_get()");

	rc = up_get(&file->key,
		    &entry);
	if (rc == 0) {
		LogDebug(COMPONENT_FSAL_UP,
			 "Lock Grant found entry %p",
			 entry);

		grant_blocked_lock_upcall(entry,
					  grant->lock_owner,
					  &grant->lock_param);

		if (entry) {
			cache_inode_put(entry);
		}
	}

	return rc;
}

/**
 * @brief Signal lock availability
 *
 * Since the SAL has its own queue for such operations, we simply
 * queue there.
 *
 * @param[in,out] ctx Thread context, containing event
 *
 * @retval 0 on success.
 * @retval ENOENT if the file isn't in the cache (this shouldn't
 *         happen, since the SAL should have files awaiting locks
 *         pinned.)
 */

static int lock_avail_imm(struct fsal_up_event *e)
{
	struct fsal_up_event_lock_avail *avail
		= &e->data.lock_avail;
	struct fsal_up_file *file = &e->file;
	cache_entry_t *entry = NULL;
	int rc = 0;

	rc = up_get(&file->key,
		    &entry);
	if (rc == 0) {
		LogDebug(COMPONENT_FSAL_UP,
			 "Lock Grant found entry %p",
			 entry);

		available_blocked_lock_upcall(entry,
					      avail->lock_owner,
					      &avail->lock_param);

		if (entry) {
			cache_inode_put(entry);
		}
	}

	return rc;
}

/**
 * @brief Execute delayed link
 *
 * Add a link to a directory and, if the entry's attributes are valid,
 * increment the link count by one.
 *
 * @param[in,out] ctx Thread context, containing event
 */

static void link_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_link *link = &e->data.link;
	struct fsal_up_file *file = &e->file;
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;

	if (up_get(&file->key, &parent) == 0) {
		/* The entry to look up */
		cache_entry_t *entry = NULL;

		if (!link->target.key.addr) {
			/* If the FSAL didn't specify a target, just
                           do a lookup and let it cache. */
			cache_inode_lookup(parent,
					   link->name,
					   &synthetic_context,
					   &entry);
			if (entry == NULL) {
				cache_inode_invalidate(
					parent,
					CACHE_INODE_INVALIDATE_CONTENT);
			} else {
				cache_inode_put(entry);
			}
		} else {
			if (up_get(&link->target.key, &entry) == 0) {
				cache_inode_add_cached_dirent(
					parent,
					link->name,
					entry,
					NULL);
				PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
				if (entry->flags &
				    CACHE_INODE_TRUST_ATTRS) {
					++entry->obj_handle
						->attributes.numlinks;
				}
				PTHREAD_RWLOCK_unlock(&entry->attr_lock);
				cache_inode_put(entry);
			} else {
				cache_inode_invalidate(
					parent,
					CACHE_INODE_INVALIDATE_CONTENT);
			}
		}
	}
	gsh_free(link->name);
}

/**
 * @brief Delayed unlink action
 *
 * Remove the name from the directory, and if the entry is cached,
 * decrement its link count.
 *
 * @param[in,out] ctx Thread context, containing event
 */

static void unlink_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_unlink *unlink
		= &e->data.unlink;
	struct fsal_up_file *file = &e->file;
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;

	if (up_get(&file->key, &parent) == 0) {
		/* The looked up directory entry */
		cache_inode_dir_entry_t *dirent;

		PTHREAD_RWLOCK_wrlock(&parent->content_lock);
		dirent = cache_inode_avl_qp_lookup_s(parent, unlink->name, 1);
		if (dirent &&
		    ~(dirent->flags & DIR_ENTRY_FLAG_DELETED)) {
			cih_latch_t latch;
			cache_entry_t *entry =
				cih_get_by_key_latched(&dirent->ckey,
						       &latch,
						       CIH_GET_UNLOCK_ON_MISS,
                                                       __func__, __LINE__);
			if (entry)
				cih_remove_latched(entry, &latch,
						   CIH_REMOVE_UNLOCK);
			cache_inode_remove_cached_dirent(parent,
							 unlink->name,
							 &synthetic_context);
		}
		PTHREAD_RWLOCK_unlock(&parent->content_lock);
		cache_inode_put(parent);
	}
	gsh_free(unlink->name);
}

/**
 * @brief Delayed move-from action
 *
 * Remove the name from the directory, do not modify the link count.
 *
 * @param[in,out] ctx Thread context, containing event
 */

static void move_from_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_move_from *move_from
		= &e->data.move_from;
	struct fsal_up_file *file = &e->file;
	/* The cache entry for the parent directory */
	cache_entry_t *parent = NULL;

	if (up_get(&file->key, &parent) == 0) {
		PTHREAD_RWLOCK_wrlock(&parent->content_lock);
		cache_inode_remove_cached_dirent(parent,
						 move_from->name,
						 &synthetic_context);
		PTHREAD_RWLOCK_unlock(&parent->content_lock);
		cache_inode_put(parent);
	}
	gsh_free(move_from->name);
}

/**
 * @brief Execute delayed move-to
 *
 * Add a link to a directory, do not touch the number of links.
 *
 * @param[in,out] ctx Thread context, containing event
 */

static void move_to_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_move_to *move_to = &e->data.move_to;
	struct fsal_up_file *file = &e->file;
	cache_entry_t *parent = NULL;

	if (up_get(&file->key, &parent) == 0) {
		/* The entry to look up */
		cache_entry_t *entry = NULL;

		if (!move_to->target.key.addr) {
			/* If the FSAL didn't specify a target, just
			   do a lookup and let it cache. */
			cache_inode_lookup(parent,
					   move_to->name,
					   &synthetic_context,
					   &entry);
			if (entry == NULL) {
				cache_inode_invalidate(
					parent,
					CACHE_INODE_INVALIDATE_CONTENT);
			} else {
				cache_inode_put(entry);
			}
		} else {
			if (up_get(&move_to->target.key, &entry) == 0) {
				cache_inode_add_cached_dirent(parent,
							      move_to->name,
							      entry,
							      NULL);
				cache_inode_put(entry);
			} else {
				cache_inode_invalidate(
					parent,
					CACHE_INODE_INVALIDATE_CONTENT);
			}
		}
		cache_inode_put(parent);
	}
	gsh_free(move_to->name);
}

/**
 * @brief Delayed rename operation
 *
 * If a parent directory is in the queue, rename the given entry.  On
 * error, invalidate the whole thing.
 *
 * @param[in,out] ctx Thread context, containing event
 */

static void rename_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_rename *rename
		= &e->data.rename;
	struct fsal_up_file *file = &e->file;
	cache_entry_t *parent = NULL;

	if (up_get(&file->key, &parent) == 0) {
		/* Cache inode status */
		cache_inode_status_t status = CACHE_INODE_SUCCESS;

		PTHREAD_RWLOCK_wrlock(&parent->content_lock);
		status = cache_inode_rename_cached_dirent(
			parent,
			rename->old,
			rename->new,
			&synthetic_context);
		if (status != CACHE_INODE_SUCCESS) {
			cache_inode_invalidate(parent,
					       CACHE_INODE_INVALIDATE_CONTENT);
		}
		PTHREAD_RWLOCK_unlock(&parent->content_lock);
		cache_inode_put(parent);
	}
	gsh_free(rename->old);
	gsh_free(rename->new);
}

/**
 * @brief Create layout recall state
 *
 * This function creates the layout recall state and work queue for a
 * LAYOUTRECALL operation on a file.  The state lock on the entry must
 * be held for write when this function is called.
 *
 * This is made somewhat more problematic by the fact that every
 * layout state that has some number of matching segments should
 * receive a single LAYOUTRECALL for the entire range, while each
 * segment matching a recall should be returned individually to the
 * FSAL.
 *
 * LAYOUTRECALL event MUST NOT be initiated from the layoutreturn or
 * layoutcommit functions.
 *
 * @param[in,out] entry   The entry on which to send the recall
 * @param[in]     type    The layout type
 * @param[in]     offset  The offset of the interval to recall
 * @param[in]     length  The length of the interval to recall
 * @param[in]     cookie  The recall cookie (to be returned to the FSAL
 *                        on the final return satisfying this recall.)
 * @param[out]    private The work queue
 *
 * @retval 0 if successfully queued.
 * @retval EINVAL if the range is zero or overflows.
 * @retval ENOENT if no layouts satisfying the range exist.
 * @retval ENOMEM if there was insufficient memory to construct the
 *         recall state.
 */

static int create_file_recall(cache_entry_t *entry,
			      layouttype4 type,
			      const struct pnfs_segment *segment,
			      void *cookie,
			      void **private)
{
	/* True if no layouts matching the request have been found */
	bool none = true;
	/* Head of the work queue */
	struct glist_head *queue
		= gsh_malloc(sizeof(struct glist_head));
	/* Iterator over all states on the cache entry */
	struct glist_head *state_iter = NULL;
	/* Error return code */
	int rc = 0;

	if (!queue) {
		rc = ENOMEM;
		goto out;
	}

	init_glist(queue);
	if ((segment->length == 0) ||
	    ((segment->length != UINT64_MAX) &&
	     (segment->offset <= UINT64_MAX - segment->length))) {
		rc = EINVAL;
		goto out;
	}

	glist_for_each(state_iter,
		       &entry->state_list) {
		struct recall_work_queue *work_entry = NULL;;
		/* Iterator over segments on this state */
		struct glist_head *seg_iter = NULL;
		/* The state under examination */
		state_t *s = glist_entry(state_iter,
					 state_t,
					 state_list);
		bool match = false;

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
			work_entry = gsh_malloc(
				sizeof(struct recall_work_queue));
			if (!work_entry) {
				rc = ENOMEM;
				goto out;
			}
			init_glist(&work_entry->link);
			work_entry->state = s;
			work_entry->recalled = false;
			glist_add_tail(queue, &work_entry->link);
			none = false;
		}
	}

	if (none) {
		rc = ENOENT;
	}

out:

	if (rc != 0) {
		if (queue) {
			/* Entry in the queue we're disposing */
			struct glist_head *queue_iter = NULL;
			/* Placeholder so we can delete entries without
			   facing untold misery */
			struct glist_head *holder = NULL;
			glist_for_each_safe(queue_iter,
					    holder,
					    queue) {
				struct recall_work_queue *g
					= glist_entry(queue_iter,
						      struct recall_work_queue,
						      link);
				glist_del(queue_iter);
				gsh_free(g);
			}
			gsh_free(queue);
		}
	} else {
		struct state_layout_recall_file *recall = gsh_malloc(
			sizeof(struct state_layout_recall_file));

		if (!recall) {
			rc = ENOMEM;
			goto out;
		}
		init_glist(&recall->entry_link);
		recall->entry = entry;
		recall->type = type;
		recall->segment = *segment;
		recall->state_list = queue;
		recall->recall_cookie = cookie;
		glist_add_tail(&entry->layoutrecall_list,
			       &recall->entry_link);
		*private = queue;
	}

	return rc;
}

/**
 * @brief Initiate layout recall
 *
 * This function validates the recall, creates the recall object, and
 * produces a work queue of layout states to which to send a
 * CB_LAYOUTRECALL.
 *
 * @param[in,out] ctx Thread context, containing event
 *
 * @retval 0 if scheduled.
 * @retval ENOENT if no matching layouts exist.
 * @retval ENOTSUP if an unsupported recall type has been provided.
 * @retval EINVAL if a nonsensical layout recall has been specified.
 */

static int layoutrecall_imm(struct fsal_up_event *e)
{
	struct fsal_up_event_layoutrecall *layoutrecall
		= &e->data.layoutrecall;
	struct fsal_up_file *file = &e->file;
	cache_entry_t *entry = NULL;
	int rc = 0;

	if (!file->export) {
		return EINVAL;
	}

	switch (layoutrecall->recall_type) {
	case LAYOUTRECALL4_ALL:
		LogCrit(COMPONENT_FSAL_UP,
			"LAYOUTRECALL4_ALL is not supported as a "
			"recall type and never will be.  Called from "
			"export %d.", file->export->exp_entry->id);
		return ENOTSUP;

	case LAYOUTRECALL4_FSID:
		LogCrit(COMPONENT_FSAL_UP,
			"LAYOUTRECALL4_FSID is not currently supported.  "
			"Called from export %d.", file->export->exp_entry->id);
		return ENOTSUP;

	case LAYOUTRECALL4_FILE:
		rc = up_get(&file->key,
			    &entry);
		if (rc != 0) {
			return rc;
		}
		PTHREAD_RWLOCK_wrlock(&entry->state_lock);
		/* We create the file recall state here and link it
                   to the cache entry, but actually send out the
                   messages from the queued function.  We do the
                   build here so that the FSAL can be notified if no
                   layouts matching the recall exist. */
		rc = create_file_recall(entry,
					layoutrecall->layout_type,
					&layoutrecall->segment,
					layoutrecall->cookie,
					&e->private);
		PTHREAD_RWLOCK_unlock(&entry->state_lock);
		cache_inode_put(entry);
		break;

	default:
		LogCrit(COMPONENT_FSAL_UP,
			"Invalid recall type %d. Called from export %d.",
			layoutrecall->recall_type,
			file->export->exp_entry->id);
		return EINVAL;
	}

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

struct layoutrecall_cb_data {
	char stateid_other[OTHERSIZE];  /*< "Other" part of state id */
	struct pnfs_segment segment; /*< Segment to recall */
	nfs_cb_argop4 arg; /*< So we don't free */
	nfs_client_id_t *client; /*< The client we're calling. */
	struct timespec first_recall; /*< Time of first recall */
	uint32_t attempts; /*< Number of times we've recalled */
};

static void layoutrecall_one_call(void *arg);

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
		PTHREAD_RWLOCK_wrlock(&state->state_entry->state_lock);
		nfs4_return_one_state(state->state_entry,
				      &synthetic_context,
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

static void layoutrecall_one_call(void *arg)
{
	struct layoutrecall_cb_data *cb_data = arg;
	state_t *s;
	int code;

	if (nfs4_State_Get_Pointer(cb_data->stateid_other,
				   &s)) {
		PTHREAD_RWLOCK_wrlock(&s->state_entry->state_lock);
		now(&cb_data->first_recall);
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
			bool deleted = false;

			nfs4_return_one_state(s->state_entry,
					      &synthetic_context,
					      LAYOUTRETURN4_FILE,
					      circumstance_revoke,
					      s,
					      cb_data->segment,
					      0,
					      NULL,
					      &deleted,
					      false);
			gsh_free(cb_data);
		} else {
			++cb_data->attempts;
		}
		PTHREAD_RWLOCK_unlock(&s->state_entry->state_lock);
	} else {
		gsh_free(cb_data);
	}
}

/**
 * @brief Delayed action for layoutrecall
 *
 * @note This function lacks robustness.  However, improving this
 * matter would require a general improvement to both callback
 * handling and queued function support.  Rather than putting a hack
 * together now, I'm going to make something that works when it works
 * and come back and handle the error cases more generally later.
 *
 * @param[in,out] ctx Thread context, containing event
 */

static void layoutrecall_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_layoutrecall *layoutrecall
		= &e->data.layoutrecall;
	struct glist_head *queue
		= (struct glist_head *)e->private;
	/* Entry in the queue we're disposing */
	struct glist_head *queue_iter = NULL;

	if (glist_empty(queue)) {
		/* One or more LAYOUTRETURNs raced us and emptied out
                   the queue */
		gsh_free(queue);
		return;
	}

	glist_for_each(queue_iter,
		       queue) {
		/* The current entry in the queue */
		struct recall_work_queue *g
			= glist_entry(queue_iter,
				      struct recall_work_queue,
				      link);
		struct state_t *s = g->state;
		struct layoutrecall_cb_data *cb_data;
		cache_entry_t *entry = s->state_entry;
		nfs_cb_argop4 *arg;
		CB_LAYOUTRECALL4args *cb_layoutrec;
		cb_data = gsh_malloc(
			sizeof(struct layoutrecall_cb_data));

		arg = &cb_data ->arg;
		arg->argop = NFS4_OP_CB_LAYOUTRECALL;
		cb_layoutrec = &arg->nfs_cb_argop4_u.opcblayoutrecall;
		PTHREAD_RWLOCK_wrlock(&entry->state_lock);
		cb_layoutrec->clora_type = layoutrecall->layout_type;
		cb_layoutrec->clora_iomode
			= layoutrecall->segment.io_mode;
		cb_layoutrec->clora_changed
			= layoutrecall->changed;
		/* Fine for now, if anyone tries a bulk recall, they
		   get an error. */
		cb_layoutrec->clora_recall.lor_recalltype
			= LAYOUTRECALL4_FILE;
		cb_layoutrec->clora_recall.layoutrecall4_u
			.lor_layout.lor_offset = layoutrecall->segment.offset;
		cb_layoutrec->clora_recall.layoutrecall4_u
			.lor_layout.lor_length = layoutrecall->segment.length;
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
		g->recalled = true;
		memcpy(cb_data->stateid_other,
		       s->stateid_other,
		       OTHERSIZE);
		cb_data->segment = layoutrecall->segment;
		cb_data->client = s->state_owner->so_owner
			.so_nfs4_owner.so_clientrec;
		cb_data->attempts = 0;
		PTHREAD_RWLOCK_unlock(&entry->state_lock);
		layoutrecall_one_call(cb_data);
	}
}

static int32_t recallany_completion(rpc_call_t* call, rpc_call_hook hook,
				    void* arg, uint32_t flags)
{
	LogFullDebug(COMPONENT_NFS_CB,"status %d arg %p",
		     call->cbt.v_u.v4.res.status, arg);
	gsh_free(arg);
	return 0;
}

static void recallany_one(state_t *s,
			  struct fsal_up_event_recallany *recallany)
{
	int i, code = 0;
	cache_entry_t *entry = s->state_entry;
	CB_RECALL_ANY4args *cb_layoutrecany;
	nfs_cb_argop4 *arg;

	// free in recallany_completion
	arg = gsh_malloc(sizeof(struct nfs_cb_argop4));
	if (arg == NULL) {
		return;
	}

	cb_layoutrecany = &arg->nfs_cb_argop4_u.opcbrecall_any;

	arg->argop = NFS4_OP_CB_RECALL_ANY;

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	cb_layoutrecany->craa_objects_to_keep = recallany->objects_to_keep;
	cb_layoutrecany->craa_type_mask.bitmap4_len =
		recallany->type_mask.bitmap4_len;
	for (i = 0; i < recallany->type_mask.bitmap4_len; i++) {
		cb_layoutrecany->craa_type_mask.map[i]
			= recallany->type_mask.map[i];
	}
	code = nfs_rpc_v41_single(s->state_owner->so_owner
				  .so_nfs4_owner.so_clientrec,
				  arg,
				  &s->state_refer,
				  recallany_completion,
				  arg,
				  NULL);
	if (code != 0) {
		/* TODO: ? */
	}
	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	return;
}

struct cb_notify {
	nfs_cb_argop4 arg;
	struct notify4 notify;
	struct notify_deviceid_delete4 notify_del;
};

static int32_t notifydev_completion(rpc_call_t* call, rpc_call_hook hook,
				    void* arg, uint32_t flags)
{
	LogFullDebug(COMPONENT_NFS_CB,"status %d arg %p",
		     call->cbt.v_u.v4.res.status, arg);
	gsh_free(arg);
	return 0;
}



static bool client_callback(nfs_client_id_t *pclientid, void *devnotify)
{
	int code = 0;
	CB_NOTIFY_DEVICEID4args *cb_notify_dev;
	struct cb_notify *arg;
	struct fsal_up_event_notifydevice *devicenotify = devnotify;
	
        if (pclientid)
        {
	  LogFullDebug(COMPONENT_NFS_CB,"CliP %p ClientID=%"PRIx64" ver %d",
	     pclientid, pclientid->cid_clientid, pclientid->cid_minorversion);
        }
        else
          return false;

	arg = gsh_malloc(sizeof(struct cb_notify)); /* free in notifydev_completion */
	if (arg == NULL) {
		return false;
	}

	cb_notify_dev = &arg->arg.nfs_cb_argop4_u.opcbnotify_deviceid;

	arg->arg.argop = NFS4_OP_CB_NOTIFY_DEVICEID;

	cb_notify_dev->cnda_changes.cnda_changes_len = 1;
	cb_notify_dev->cnda_changes.cnda_changes_val = &arg->notify;
	arg->notify.notify_mask.bitmap4_len = 1;
	arg->notify.notify_mask.map[0] = devicenotify->notify_type;
	arg->notify.notify_vals.notifylist4_len = sizeof(struct notify_deviceid_delete4);

	arg->notify.notify_vals.notifylist4_val = (char *)&arg->notify_del;
	arg->notify_del.ndd_layouttype = devicenotify->layout_type;
	memcpy(&arg->notify_del.ndd_deviceid,
	       &devicenotify->device_id,
	       NFS4_DEVICEID4_SIZE);

	code = nfs_rpc_v41_single(pclientid,
				  &arg->arg,
				  NULL,
				  notifydev_completion,
				  &arg->arg,
				  NULL);
	if (code != 0)
		gsh_free(arg);

	return true;
}

static void recallany_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_recallany *recallany =
		&e->data.recallany;
	struct fsal_up_file *file = &e->file;
	struct exportlist *exp;
	struct glist_head *glist;
	state_t *state;

	exp = file->export->exp_entry;

	pthread_mutex_lock(&exp->exp_state_mutex);

	/* TODO: loop over list and mark clients that are called so we
	   call them only once */
	glist_for_each(glist, &exp->exp_state_list) {
		state = glist_entry(glist, struct state_t, state_export_list);

		if (state != NULL && state->state_type == STATE_TYPE_LAYOUT) {
			LogDebug(COMPONENT_NFS_CB,"state %p type %d",
				 state, state->state_type);
			recallany_one(state, recallany);
		}
	}
	pthread_mutex_unlock(&exp->exp_state_mutex);

	return;
}

static void notifydevice_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_event_notifydevice *devicenotify
		= &e->data.notifydevice;

	nfs41_foreach_client_callback(client_callback, (void *)devicenotify);

	return;
}

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
		return;
	}
	chan = nfs_rpc_get_chan(clid, NFS_RPC_FLAG_NONE);
	if (!chan) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
		return;
	}
	if (!chan->clnt) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
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
		return;
	}

	/* add ops, till finished (dont exceed count) */
	cb_compound_add_op(&call->cbt, argop);

	/* set completion hook */
	call->call_hook = cb_completion_func;

	/* call it (here, in current thread context) */
	code = nfs_rpc_submit_call(call, NULL, NFS_RPC_FLAG_NONE);

	return;
};

static void delegrecall_queue(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	struct fsal_up_file *file = &e->file;
	cache_entry_t *entry = NULL;
	struct glist_head  *glist;
	state_lock_entry_t *found_entry = NULL;
	int rc = 0;

	rc = up_get(&file->key, &entry);
	if (rc != 0 || entry == NULL) {
		LogDebug(COMPONENT_FSAL_UP,
			 "FSAL_UP_DELEG: cache inode get failed, rc %d", rc);
		/* Not an error. Expecting some nodes will not have it
		 * in cache in a cluster. */
		return;
	}

	LogDebug(COMPONENT_FSAL_UP,
		 "FSAL_UP_DELEG: Invalidate cache found entry %p type %u",
		 entry, entry->type);

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	glist_for_each(glist, &entry->object.file.lock_list) {
		found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

		if (found_entry != NULL && found_entry->sle_state != NULL) {
			LogDebug(COMPONENT_NFS_CB,"found_entry %p", found_entry);
			delegrecall_one(found_entry, entry);
		}
	}
	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_put(entry);

	return;
}

struct fsal_up_vector fsal_up_top = {
	.imm = {
		[FSAL_UP_EVENT_LOCK_GRANT] = lock_grant_imm,
		[FSAL_UP_EVENT_LOCK_AVAIL] = lock_avail_imm,
		[FSAL_UP_EVENT_INVALIDATE] = invalidate_imm,
		[FSAL_UP_EVENT_UPDATE] = update_imm,
		[FSAL_UP_EVENT_LINK] = NULL,
		[FSAL_UP_EVENT_UNLINK] = NULL,
		[FSAL_UP_EVENT_MOVE_FROM] = NULL,
		[FSAL_UP_EVENT_MOVE_TO] = NULL,
		[FSAL_UP_EVENT_RENAME] = NULL,
		[FSAL_UP_EVENT_LAYOUTRECALL] = layoutrecall_imm,
		[FSAL_UP_EVENT_RECALL_ANY] = NULL,
		[FSAL_UP_EVENT_NOTIFY_DEVICE] = NULL,
		[FSAL_UP_EVENT_DELEGATION_RECALL] = NULL
	},
	.queue = {
		[FSAL_UP_EVENT_LOCK_GRANT] = NULL,
		[FSAL_UP_EVENT_LOCK_AVAIL] = NULL,
		[FSAL_UP_EVENT_INVALIDATE] = NULL,
		[FSAL_UP_EVENT_UPDATE] = update_queue,
		[FSAL_UP_EVENT_LINK] = link_queue,
		[FSAL_UP_EVENT_UNLINK] = unlink_queue,
		[FSAL_UP_EVENT_MOVE_FROM] = move_from_queue,
		[FSAL_UP_EVENT_MOVE_TO] = move_to_queue,
		[FSAL_UP_EVENT_RENAME] = rename_queue,
		[FSAL_UP_EVENT_LAYOUTRECALL] = layoutrecall_queue,
		[FSAL_UP_EVENT_RECALL_ANY] = recallany_queue,
		[FSAL_UP_EVENT_NOTIFY_DEVICE] = notifydevice_queue,
		[FSAL_UP_EVENT_DELEGATION_RECALL] = delegrecall_queue
	}
};
