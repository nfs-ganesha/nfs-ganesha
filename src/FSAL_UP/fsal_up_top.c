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
#include "hashtable.h"
#include "fsal_up.h"
#include "sal_functions.h"
#include "pnfs_utils.h"
#include "nfs_rpc_callback.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "delayed_exec.h"
#include "export_mgr.h"
#include "server_stats.h"

struct delegrecall_context {
	cache_entry_t *drc_entry;
	nfs_client_id_t *drc_clid;
	struct deleg_data *drc_deleg_entry;
	stateid4 drc_stateid;
	struct gsh_export *drc_exp;
};

enum recall_resp_action {
	DELEG_RECALL_SCHED,
	DELEG_RET_WAIT,
	REVOKE
};

static int schedule_delegrevoke_check(struct delegrecall_context *ctx,
				      uint32_t delay);
static int schedule_delegrecall_task(struct delegrecall_context *ctx,
				     uint32_t delay);

/**
 * @brief Invalidate a cached entry
 *
 * @param[in] key    Key to specify object
 * @param[in] flags  Flags to pass to cache_inode_invalidate
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

static cache_inode_status_t invalidate_close(struct fsal_module *fsal,
					    const struct fsal_up_vector *up_ops,
					    struct gsh_buffdesc *handle,
					    uint32_t flags)
{
	cache_entry_t *entry = NULL;
	cache_inode_status_t rc = 0;

	rc = up_get(fsal, handle, &entry);
	if (rc == 0) {
		if (is_open(entry))
			rc = up_async_invalidate(general_fridge, up_ops, fsal,
						 handle,
						 CACHE_INODE_INVALIDATE_CLOSE,
						 NULL, NULL);
		else
			(void) cache_inode_invalidate(entry, flags);
		cache_inode_put(entry);
	}

	return rc;
}

cache_inode_status_t fsal_invalidate(struct fsal_module *fsal,
				     struct gsh_buffdesc *handle,
				     uint32_t flags)
{
	cache_entry_t *entry = NULL;
	cache_inode_status_t rc = 0;

	rc = up_get(fsal, handle, &entry);
	if (rc == 0) {
		rc = cache_inode_invalidate(entry, flags);
		cache_inode_put(entry);
	}

	return rc;
}

/**
 * @brief Update cached attributes
 *
 * @param[in] obj    Key to specify object
 * @param[in] attr   New attributes
 * @param[in] flags  Flags to govern update
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

static cache_inode_status_t update(struct fsal_module *fsal,
				   struct gsh_buffdesc *obj,
				   struct attrlist *attr, uint32_t flags)
{
	cache_entry_t *entry = NULL;
	int rc = 0;
	/* Have necessary changes been made? */
	bool mutatis_mutandis = false;

	/* These cannot be updated, changing any of them is
	   tantamount to destroying and recreating the file. */
	if (FSAL_TEST_MASK
	    (attr->mask,
	     ATTR_TYPE | ATTR_FSID | ATTR_FILEID | ATTR_RAWDEV | ATTR_RDATTR_ERR
	     | ATTR_GENERATION)) {
		return CACHE_INODE_INVALID_ARGUMENT;
	}

	/* Filter out garbage flags */

	if (flags &
	    ~(fsal_up_update_filesize_inc | fsal_up_update_atime_inc |
	      fsal_up_update_creation_inc | fsal_up_update_ctime_inc |
	      fsal_up_update_mtime_inc | fsal_up_update_chgtime_inc |
	      fsal_up_update_spaceused_inc | fsal_up_nlink)) {
		return CACHE_INODE_INVALID_ARGUMENT;
	}

	rc = up_get(fsal, obj, &entry);
	if (rc != 0)
		return rc;

	/* Knock things out if the link count falls to 0. */

	if ((flags & fsal_up_nlink) && (attr->numlinks == 0)) {
		rc = cache_inode_invalidate(entry,
					    (CACHE_INODE_INVALIDATE_ATTRS |
					     CACHE_INODE_INVALIDATE_CLOSE));
	}

	if (rc != 0 || attr->mask == 0)
		goto out;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	if (attr->expire_time_attr != 0)
		entry->obj_handle->attributes.expire_time_attr =
							attr->expire_time_attr;
	if (FSAL_TEST_MASK(attr->mask, ATTR_SIZE)) {
		if (flags & fsal_up_update_filesize_inc) {
			if (attr->filesize >
			    entry->obj_handle->attributes.filesize) {
				entry->obj_handle->attributes.filesize =
					attr->filesize;
				mutatis_mutandis = true;
			}
		} else {
			entry->obj_handle->attributes.filesize = attr->filesize;
			mutatis_mutandis = true;
		}
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_SPACEUSED)) {
		if (flags & fsal_up_update_spaceused_inc) {
			if (attr->spaceused >
			    entry->obj_handle->attributes.spaceused) {
				entry->obj_handle->attributes.spaceused =
					attr->spaceused;
				mutatis_mutandis = true;
			}
		} else {
			entry->obj_handle->attributes.spaceused =
			   attr->spaceused;
			mutatis_mutandis = true;
		}
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_ACL)) {
		/**
		 * @todo Someone who knows the ACL code, please look
		 * over this.  We assume that the FSAL takes a
		 * reference on the supplied ACL that we can then hold
		 * onto.  This seems the most reasonable approach in
		 * an asynchronous call.
		 */

		/* This idiom is evil. */
		fsal_acl_status_t acl_status;

		nfs4_acl_release_entry(entry->obj_handle->attributes.acl,
				       &acl_status);

		entry->obj_handle->attributes.acl = attr->acl;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_MODE)) {
		entry->obj_handle->attributes.mode = attr->mode;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_NUMLINKS)) {
		entry->obj_handle->attributes.numlinks = attr->numlinks;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_OWNER)) {
		entry->obj_handle->attributes.owner = attr->owner;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_GROUP)) {
		entry->obj_handle->attributes.group = attr->group;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_ATIME)
	    && ((flags & ~fsal_up_update_atime_inc)
		||
		(gsh_time_cmp
		 (&attr->atime, &entry->obj_handle->attributes.atime) == 1))) {
		entry->obj_handle->attributes.atime = attr->atime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CREATION)
	    && ((flags & ~fsal_up_update_creation_inc)
		||
		(gsh_time_cmp
		 (&attr->creation,
		  &entry->obj_handle->attributes.creation) == 1))) {
		entry->obj_handle->attributes.creation = attr->creation;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CTIME)
	    && ((flags & ~fsal_up_update_ctime_inc)
		||
		(gsh_time_cmp
		 (&attr->ctime, &entry->obj_handle->attributes.ctime) == 1))) {
		entry->obj_handle->attributes.ctime = attr->ctime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_MTIME)
	    && ((flags & ~fsal_up_update_mtime_inc)
		||
		(gsh_time_cmp
		 (&attr->mtime, &entry->obj_handle->attributes.mtime) == 1))) {
		entry->obj_handle->attributes.mtime = attr->mtime;
		mutatis_mutandis = true;
	}

	if (FSAL_TEST_MASK(attr->mask, ATTR_CHGTIME)
	    && ((flags & ~fsal_up_update_chgtime_inc)
		||
		(gsh_time_cmp
		 (&attr->chgtime,
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
 * @param[in] file       The file in question
 * @param[in] owner      The lock owner
 * @param[in] lock_param description of the lock
 *
 * @return STATE_SUCCESS or errors.
 */

static state_status_t lock_grant(struct fsal_module *fsal,
				 struct gsh_buffdesc *file,
				 void *owner,
				 fsal_lock_param_t *lock_param)
{
	cache_entry_t *entry;
	cache_inode_status_t cache_status;

	cache_status = up_get(fsal, file, &entry);
	if (cache_status != CACHE_INODE_SUCCESS)
		return STATE_NOT_FOUND;

	grant_blocked_lock_upcall(entry, owner, lock_param);
	cache_inode_put(entry);
	return STATE_SUCCESS;
}

/**
 * @brief Signal lock availability
 *
 * @param[in] file       The file in question
 * @param[in] owner      The lock owner
 * @param[in] lock_param description of the lock
 *
 * @return STATE_SUCCESS or errors.
 */

static state_status_t lock_avail(struct fsal_module *fsal,
				 struct gsh_buffdesc *file,
				 void *owner,
				 fsal_lock_param_t *lock_param)
{
	cache_entry_t *entry;
	cache_inode_status_t cache_status;

	cache_status = up_get(fsal, file, &entry);
	if (cache_status != CACHE_INODE_SUCCESS)
		return STATE_NOT_FOUND;

	available_blocked_lock_upcall(entry, owner, lock_param);
	cache_inode_put(entry);
	return STATE_SUCCESS;
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

static state_status_t create_file_recall(cache_entry_t *entry,
					 layouttype4 type,
					 const struct pnfs_segment *segment,
					 void *cookie,
					 struct layoutrecall_spec *spec,
					 struct state_layout_recall_file
					 **recout)
{
	/* True if no layouts matching the request have been found */
	bool none = true;
	/* Iterator over all states on the cache entry */
	struct glist_head *state_iter = NULL;
	/* Error return code */
	state_status_t rc = STATE_SUCCESS;
	/* The recall object referenced by future returns */
	struct state_layout_recall_file *recall =
	    gsh_malloc(sizeof(struct state_layout_recall_file));

	if (!recall) {
		rc = STATE_MALLOC_ERROR;
		goto out;
	}

	glist_init(&recall->state_list);
	recall->entry = entry;
	recall->type = type;
	recall->segment = *segment;
	recall->recall_cookie = cookie;

	if ((segment->length == 0)
	    || ((segment->length != UINT64_MAX)
		&& (segment->offset <= UINT64_MAX - segment->length))) {
		rc = STATE_INVALID_ARGUMENT;
		goto out;
	}

	glist_for_each(state_iter, &entry->list_of_states) {
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
				    s->state_owner->so_owner.so_nfs4_owner.
				    so_clientid) {
					continue;
				}
				break;

			case layoutrecall_howspec_complement:
				if (spec->u.client ==
				    s->state_owner->so_owner.so_nfs4_owner.
				    so_clientid) {
					continue;
				}
				break;

			case layoutrecall_not_specced:
				break;
			}
		}

		if ((s->state_type != STATE_TYPE_LAYOUT)
		    || (s->state_data.layout.state_layout_type != type)) {
			continue;
		}
		glist_for_each(seg_iter,
			       &s->state_data.layout.state_segments) {
			state_layout_segment_t *g = glist_entry(
				seg_iter,
				state_layout_segment_t,
				sls_state_segments);
			pthread_mutex_lock(&g->sls_mutex);
			if (pnfs_segments_overlap(segment, &g->sls_segment))
				match = true;
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
			list_entry =
			    gsh_malloc(sizeof(struct recall_state_list));
			if (!list_entry) {
				rc = STATE_MALLOC_ERROR;
				goto out;
			}
			list_entry->state = s;
			glist_add_tail(&recall->state_list, &list_entry->link);
			none = false;
		}
	}

	if (none)
		rc = STATE_NOT_FOUND;

 out:

	if ((rc != STATE_SUCCESS) && recall) {
		/* Iterator over work queue, for disposing of it. */
		struct glist_head *wi = NULL;
		/* Preserved next over work queue. */
		struct glist_head *wn = NULL;

		glist_for_each_safe(wi, wn, &recall->state_list) {
			struct recall_state_list *list_entry
				= glist_entry(wi, struct recall_state_list,
					      link);
			glist_del(&list_entry->link);
			gsh_free(list_entry);
		}
		gsh_free(recall);
	} else {
		glist_add_tail(&entry->layoutrecall_list, &recall->entry_link);
		*recout = recall;
	}

	return rc;
}

static void layoutrecall_one_call(void *arg);

/**
 * @brief Data used to handle the response to CB_LAYOUTRECALL
 */

struct layoutrecall_cb_data {
	char stateid_other[OTHERSIZE];	/*< "Other" part of state id */
	struct pnfs_segment segment;	/*< Segment to recall */
	nfs_cb_argop4 arg;	/*< So we don't free */
	nfs_client_id_t *client;	/*< The client we're calling. */
	struct timespec first_recall;	/*< Time of first recall */
	uint32_t attempts;	/*< Number of times we've recalled */
};

/**
 * @brief Initiate layout recall
 *
 * This function validates the recall, creates the recall object, and
 * sends out CB_LAYOUTRECALL messages.
 *
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
state_status_t layoutrecall(struct fsal_module *fsal,
			    struct gsh_buffdesc *handle,
			    layouttype4 layout_type, bool changed,
			    const struct pnfs_segment *segment, void *cookie,
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

	rc = cache_inode_status_to_state_status(up_get(fsal, handle, &entry));
	if (rc != STATE_SUCCESS)
		return rc;

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	/* We build up the list before consuming it so that we have
	   every state on the list before we start executing returns. */
	rc = create_file_recall(entry, layout_type, segment, cookie, spec,
				&recall);
	PTHREAD_RWLOCK_unlock(&entry->state_lock);
	if (rc != STATE_SUCCESS)
		goto out;

	/**
	 * @todo This leaves us open to a race if a return comes in
	 * while we're traversing the work list.
	 */
	glist_for_each(wi, &recall->state_list) {
		/* The current entry in the queue */
		struct recall_state_list *g = glist_entry(wi,
							  struct
							  recall_state_list,
							  link);
		struct state_t *s = g->state;
		struct layoutrecall_cb_data *cb_data;
		struct gsh_export *exp = s->state_export;
		cache_entry_t *entry = s->state_entry;
		nfs_cb_argop4 *arg;
		CB_LAYOUTRECALL4args *cb_layoutrec;

		cb_data = gsh_malloc(sizeof(struct layoutrecall_cb_data));
		if (cb_data == NULL) {
			rc = STATE_MALLOC_ERROR;
			goto out;
		}
		arg = &cb_data->arg;
		arg->argop = NFS4_OP_CB_LAYOUTRECALL;
		cb_layoutrec = &arg->nfs_cb_argop4_u.opcblayoutrecall;
		PTHREAD_RWLOCK_wrlock(&entry->state_lock);
		cb_layoutrec->clora_type = layout_type;
		cb_layoutrec->clora_iomode = segment->io_mode;
		cb_layoutrec->clora_changed = changed;
		cb_layoutrec->clora_recall.lor_recalltype = LAYOUTRECALL4_FILE;
		cb_layoutrec->clora_recall.layoutrecall4_u.lor_layout.
		    lor_offset = segment->offset;
		cb_layoutrec->clora_recall.layoutrecall4_u.lor_layout.
		    lor_length = segment->length;
		cb_layoutrec->clora_recall.layoutrecall4_u.lor_layout.lor_fh.
		    nfs_fh4_len = sizeof(struct alloc_file_handle_v4);
		cb_layoutrec->clora_recall.layoutrecall4_u.lor_layout.lor_fh.
		    nfs_fh4_val =
		    gsh_malloc(sizeof(struct alloc_file_handle_v4));
		if (cb_layoutrec->clora_recall.layoutrecall4_u.lor_layout.
				lor_fh.nfs_fh4_val == NULL) {
			PTHREAD_RWLOCK_unlock(&entry->state_lock);
			gsh_free(cb_data);
			rc = STATE_MALLOC_ERROR;
			goto out;
		}
		if (!nfs4_FSALToFhandle(
				&cb_layoutrec->clora_recall.layoutrecall4_u
				.lor_layout.lor_fh,
				entry->obj_handle,
				exp)) {
			PTHREAD_RWLOCK_unlock(&entry->state_lock);
			gsh_free(cb_data);
			gsh_free(cb_layoutrec->clora_recall.layoutrecall4_u.
						lor_layout.lor_fh.nfs_fh4_val);
			rc = STATE_MALLOC_ERROR;
			goto out;
		}
		update_stateid(s,
			       &cb_layoutrec->clora_recall.layoutrecall4_u.
			       lor_layout.lor_stateid, NULL, "LAYOUTRECALL");
		memcpy(cb_data->stateid_other, s->stateid_other, OTHERSIZE);
		cb_data->segment = *segment;
		cb_data->client =
		    s->state_owner->so_owner.so_nfs4_owner.so_clientrec;
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
	gsh_free(op->nfs_cb_argop4_u.opcblayoutrecall.clora_recall.
		 layoutrecall4_u.lor_layout.lor_fh.nfs_fh4_val);
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

static int32_t layoutrec_completion(rpc_call_t *call, rpc_call_hook hook,
				    void *arg, uint32_t flags)
{
	struct layoutrecall_cb_data *cb_data = arg;
	bool deleted = false;
	state_t *state = NULL;
	struct root_op_context root_op_context;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	LogFullDebug(COMPONENT_NFS_CB, "status %d cb_data %p",
		     call->cbt.v_u.v4.res.status, cb_data);

	/* Get this out of the way up front */
	if (hook != RPC_CALL_COMPLETE)
		goto revoke;

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
		free_layoutrec(&call->cbt.v_u.v4.args.argarray.argarray_val[1]);
		nfs41_complete_single(call, hook, cb_data, flags);
		gsh_free(cb_data);
		goto out;
	} else if (call->cbt.v_u.v4.res.status == NFS4ERR_DELAY) {
		struct timespec current;
		nsecs_elapsed_t delay;

		now(&current);
		if (timespec_diff(&cb_data->first_recall, &current) >
		    (nfs_param.nfsv4_param.lease_lifetime * NS_PER_SEC)) {
			goto revoke;
		}
		if (cb_data->attempts < 5)
			delay = 0;
		else if (cb_data->attempts < 10)
			delay = 1 * NS_PER_MSEC;
		else if (cb_data->attempts < 20)
			delay = 10 * NS_PER_MSEC;
		else if (cb_data->attempts < 30)
			delay = 100 * NS_PER_MSEC;
		else
			delay = 1 * NS_PER_SEC;

		/* We don't free the argument here, because we'll be
		   re-using that to make the queued call. */
		nfs41_complete_single(call, hook, cb_data, flags);
		delayed_submit(layoutrecall_one_call, cb_data, delay);
		goto out;
	}

	/**
	 * @todo Better error handling later when we have more
	 * session/revocation infrastructure.
	 */

 revoke:
	/* If we don't find the state, there's nothing to return. */
	state = nfs4_State_Get_Pointer(cb_data->stateid_other);

	if (state != NULL) {
		enum fsal_layoutreturn_circumstance circumstance;

		if (hook == RPC_CALL_COMPLETE &&
		    call->cbt.v_u.v4.res.status ==
		    NFS4ERR_NOMATCHING_LAYOUT)
			circumstance = circumstance_client;
		else
			circumstance = circumstance_revoke;

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

		root_op_context.req_ctx.clientid = &state->state_owner
			->so_owner.so_nfs4_owner.so_clientid;
		root_op_context.req_ctx.export = state->state_export;
		root_op_context.req_ctx.fsal_export =
			root_op_context.req_ctx.export->fsal_export;

		nfs4_return_one_state(state->state_entry,
				      LAYOUTRETURN4_FILE, circumstance,
				      state, cb_data->segment, 0, NULL,
				      &deleted);
		PTHREAD_RWLOCK_unlock(&state->state_entry->state_lock);

		dec_state_t_ref(state);
	}
	free_layoutrec(&call->cbt.v_u.v4.args.argarray.argarray_val[1]);
	nfs41_complete_single(call, hook, cb_data, flags);
	gsh_free(cb_data);

out:
	release_root_op_context();
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
	struct root_op_context root_op_context;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	s = nfs4_State_Get_Pointer(cb_data->stateid_other);

	if (s != NULL) {
		PTHREAD_RWLOCK_wrlock(&s->state_entry->state_lock);

		root_op_context.req_ctx.clientid = &s->state_owner
			->so_owner.so_nfs4_owner.so_clientid;
		root_op_context.req_ctx.export = s->state_export;
		root_op_context.req_ctx.fsal_export =
			root_op_context.req_ctx.export->fsal_export;

		nfs4_return_one_state(s->state_entry,
				      LAYOUTRETURN4_FILE, circumstance_revoke,
				      s, cb_data->segment, 0, NULL, &deleted);
		PTHREAD_RWLOCK_unlock(&s->state_entry->state_lock);

		dec_state_t_ref(s);
	}
	release_root_op_context();
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
	struct root_op_context root_op_context;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	if (cb_data->attempts == 0)
		now(&cb_data->first_recall);

	s = nfs4_State_Get_Pointer(cb_data->stateid_other);

	if (s != NULL) {
		PTHREAD_RWLOCK_wrlock(&s->state_entry->state_lock);
		code =
		    nfs_rpc_v41_single(cb_data->client, &cb_data->arg,
				       &s->state_refer, layoutrec_completion,
				       cb_data, free_layoutrec);
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
				delayed_submit(return_one_async, cb_data, 0);
			} else {
				bool deleted = false;

				root_op_context.req_ctx.clientid =
					&s->state_owner->so_owner.so_nfs4_owner
					.so_clientid;
				root_op_context.req_ctx.export =
					s->state_export;
				root_op_context.req_ctx.fsal_export =
				    root_op_context.req_ctx.export->fsal_export;

				nfs4_return_one_state(s->state_entry,
						      LAYOUTRETURN4_FILE,
						      circumstance_revoke, s,
						      cb_data->segment, 0, NULL,
						      &deleted);
				gsh_free(cb_data);
			}
		} else {
			++cb_data->attempts;
		}
		PTHREAD_RWLOCK_unlock(&s->state_entry->state_lock);

		dec_state_t_ref(s);
	} else {
		gsh_free(cb_data);
	}
	release_root_op_context();
}

/**
 * @brief Data for CB_NOTIFY and CB_NOTIFY_DEVICEID response handler
 */

struct cb_notify {
	nfs_cb_argop4 arg;	/*< Arguments (so we can free them) */
	struct notify4 notify;	/*< For notify response */
	struct notify_deviceid_delete4 notify_del;	/*< For notify_deviceid
							   response. */
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

static int32_t notifydev_completion(rpc_call_t *call, rpc_call_hook hook,
				    void *arg, uint32_t flags)
{
	LogFullDebug(COMPONENT_NFS_CB, "status %d arg %p",
		     call->cbt.v_u.v4.res.status, arg);
	gsh_free(arg);
	return 0;
}

/**
 * The arguments for devnotify_client_callback packed up in a struct
 */

struct devnotify_cb_data {
	notify_deviceid_type4 notify_type;
	layouttype4 layout_type;
	struct pnfs_deviceid devid;
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

	if (clientid) {
		LogFullDebug(COMPONENT_NFS_CB,
			     "CliP %p ClientID=%" PRIx64 " ver %d", clientid,
			     clientid->cid_clientid,
			     clientid->cid_minorversion);
	} else {
		return false;
	}

	/* free in notifydev_completion */
	arg = gsh_malloc(sizeof(struct cb_notify));
	if (arg == NULL)
		return false;

	cb_notify_dev = &arg->arg.nfs_cb_argop4_u.opcbnotify_deviceid;

	arg->arg.argop = NFS4_OP_CB_NOTIFY_DEVICEID;

	cb_notify_dev->cnda_changes.cnda_changes_len = 1;
	cb_notify_dev->cnda_changes.cnda_changes_val = &arg->notify;
	arg->notify.notify_mask.bitmap4_len = 1;
	arg->notify.notify_mask.map[0] = devicenotify->notify_type;
	arg->notify.notify_vals.notifylist4_len =
	    sizeof(struct notify_deviceid_delete4);

	arg->notify.notify_vals.notifylist4_val = (char *)&arg->notify_del;
	arg->notify_del.ndd_layouttype = devicenotify->layout_type;
	memcpy(arg->notify_del.ndd_deviceid,
	       &devicenotify->devid,
	       sizeof(arg->notify_del.ndd_deviceid));
	code =
	    nfs_rpc_v41_single(clientid, &arg->arg, NULL, notifydev_completion,
			       &arg->arg, NULL);
	if (code != 0)
		gsh_free(arg);

	return true;
}

/**
 * @brief Remove or change a deviceid
 *
 * @param[in] dev_exportid Export responsible for the device ID
 * @param[in] notify_type Change or remove
 * @param[in] layout_type The layout type affected
 * @param[in] devid       The lower quad of the device id, unique
 *                        within this export
 * @param[in] immediate   Whether the change is immediate (in the case
 *                        of a change.)
 *
 * @return STATE_SUCCESS or errors.
 */

state_status_t notify_device(notify_deviceid_type4 notify_type,
			     layouttype4 layout_type,
			     struct pnfs_deviceid devid,
			     bool immediate)
{
	struct devnotify_cb_data *cb_data;

	cb_data = gsh_malloc(sizeof(struct devnotify_cb_data));
	if (cb_data == NULL) {
		LogCrit(COMPONENT_NFS_CB, "malloc failed for notify_device");
		return STATE_MALLOC_ERROR;
	}
	cb_data->notify_type = notify_type;
	cb_data->layout_type = layout_type;
	cb_data->devid = devid;

	nfs41_foreach_client_callback(devnotify_client_callback, cb_data);

	return STATE_SUCCESS;
}

/**
 * @brief Check if the delegation needs to be revoked.
 *
 * @param[in] deleg_entry SLE entry for the delegaion
 *
 * @return true, if the delegation need to be revoked.
 * @return false, if the delegation should not be revoked.
 */

bool eval_deleg_revoke(struct deleg_data *deleg_entry)
{
	struct cf_deleg_stats *clfl_stats;
	time_t curr_time;
	time_t recall_success_time, first_recall_time;
	uint32_t lease_lifetime = nfs_param.nfsv4_param.lease_lifetime;
	open_delegation_type4 sd_type;

	sd_type = deleg_entry->dd_state->state_data.deleg.sd_type;
	assert(sd_type == OPEN_DELEGATE_READ || sd_type == OPEN_DELEGATE_WRITE);

	clfl_stats = &deleg_entry->dd_state->state_data.deleg.sd_clfile_stats;

	curr_time = time(NULL);
	recall_success_time = clfl_stats->cfd_rs_time;
	first_recall_time = clfl_stats->cfd_r_time;

	if ((recall_success_time > 0) &&
	    (curr_time - recall_success_time) > lease_lifetime) {
		LogInfo(COMPONENT_STATE,
			 "More than one lease time has passed since recall was successfully sent");
		return true;
	}

	if ((first_recall_time > 0) &&
	    (curr_time - first_recall_time) > (2 * lease_lifetime)) {
		LogInfo(COMPONENT_STATE,
			 "More than two lease times have passed since recall was attempted");
		return true;
	}

	return false;
}

/**
 * @brief Handle recall response
 *
 * @param[in] call  The RPC call being completed
 * @param[in] clfl_stats  client-file deleg heuristics
 * @param[in] p_cargs deleg recall context
 *
 */

static enum recall_resp_action handle_recall_response(
				struct delegrecall_context *p_cargs,
				rpc_call_t *call)
{
	enum recall_resp_action resp_action;
	struct deleg_data *deleg_entry = p_cargs->drc_deleg_entry;

	struct cf_deleg_stats *clfl_stats =
		&deleg_entry->dd_state->state_data.deleg.sd_clfile_stats;

	switch (call->cbt.v_u.v4.res.status) {
	case NFS4_OK:
		LogDebug(COMPONENT_NFS_CB,
		"Delegation successfully recalled");
		resp_action = DELEG_RET_WAIT;
		clfl_stats->cfd_rs_time =
					time(NULL);
		break;
	case NFS4ERR_BADHANDLE:
		LogDebug(COMPONENT_NFS_CB,
			 "Client sent NFS4ERR_BADHANDLE response, retrying recall for(%p)",
			 p_cargs->drc_deleg_entry);
		resp_action = DELEG_RECALL_SCHED;
		break;
	case NFS4ERR_DELAY:
		LogDebug(COMPONENT_NFS_CB,
			 "Client sent NFS4ERR_DELAY response, retrying recall for(%p)",
			 p_cargs->drc_deleg_entry);
		resp_action = DELEG_RECALL_SCHED;
		break;
	case  NFS4ERR_BAD_STATEID:
		LogDebug(COMPONENT_NFS_CB,
			 "Client sent NFS4ERR_BAD_STATEID response, retrying recall for (%p)",
			 p_cargs->drc_deleg_entry);
		resp_action = DELEG_RECALL_SCHED;
		break;
	default:
		/* some other NFS error, consider the recall failed */
		LogDebug(COMPONENT_NFS_CB,
			 "Client sent %d response, retrying recall for (%p)",
			 call->cbt.v_u.v4.res.status,
			 p_cargs->drc_deleg_entry);
		resp_action = DELEG_RECALL_SCHED;
		break;
	}
	return resp_action;
}

/**
 * @brief Handle the reply to a CB_RECALL
 *
 * @param[in] call  The RPC call being completed
 * @param[in] hook  The hook itself
 * @param[in] arg   Supplied argument (the callback data)
 * @param[in] flags There are no flags.
 *
 * @return 0, constantly.
 */

static int32_t delegrecall_completion_func(rpc_call_t *call,
					   rpc_call_hook hook, void *arg,
					   uint32_t flags)
{
	char *fh = NULL;
	enum recall_resp_action resp_act;
	state_status_t rc = STATE_SUCCESS;
	struct delegrecall_context *deleg_ctx =
				(struct delegrecall_context *) arg;
	struct deleg_data *deleg_entry, *tdentry;
	cache_entry_t *entry = deleg_ctx->drc_entry;
	struct glist_head *glist, *glist_n;
	nfs_client_id_t *clientid;

	LogDebug(COMPONENT_NFS_CB, "%p %s", call,
		 (hook == RPC_CALL_COMPLETE) ? "Success" : "Failed");

	assert(entry != NULL);
	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	deleg_entry = NULL;
	glist_for_each_safe(glist, glist_n, &entry->object.file.deleg_list) {
		tdentry = glist_entry(glist, struct deleg_data, dd_list);
		if (deleg_ctx->drc_deleg_entry == tdentry &&
		    SAME_STATEID(&deleg_ctx->drc_stateid, tdentry->dd_state)) {
			deleg_entry = tdentry;
			break;
		}
	}

	if (!deleg_entry) {
		LogDebug(COMPONENT_NFS_CB, "Delegation is already returned");
		cache_inode_put(deleg_ctx->drc_entry);
		pthread_mutex_lock(&deleg_ctx->drc_clid->cid_mutex);
		update_lease(deleg_ctx->drc_clid);
		pthread_mutex_unlock(&deleg_ctx->drc_clid->cid_mutex);
		put_gsh_export(deleg_ctx->drc_exp);
		dec_client_id_ref(deleg_ctx->drc_clid);
		gsh_free(deleg_ctx);
		goto out_free;
	}

	LogDebug(COMPONENT_NFS_CB, "deleg_entry %p", deleg_entry);

	clientid = deleg_entry->dd_owner->so_owner.so_nfs4_owner.so_clientrec;

	switch (hook) {
	case RPC_CALL_COMPLETE:
		LogMidDebug(COMPONENT_NFS_CB, "call result: %d", call->stat);
		fh = call->cbt.v_u.v4.args.argarray.argarray_val->
				nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val;
		if (call->stat != RPC_SUCCESS) {
			LogEvent(COMPONENT_NFS_CB, "Callback channel down");
			set_cb_chan_down(deleg_ctx->drc_clid, true);
			resp_act = DELEG_RECALL_SCHED;
		} else
			resp_act = handle_recall_response(deleg_ctx, call);
		break;
	default:
		LogDebug(COMPONENT_NFS_CB, "%p unknown hook %d", call, hook);

		set_cb_chan_down(clientid, true);
		/* Mark the recall as failed */
		resp_act = DELEG_RECALL_SCHED;
		break;
	}
	switch (resp_act) {
	case DELEG_RECALL_SCHED:
		if (eval_deleg_revoke(deleg_entry))
			goto out_revoke;
		else {
			if (schedule_delegrecall_task(deleg_ctx, 1))
				goto out_revoke;
			goto out_free;
		}
		break;
	case DELEG_RET_WAIT:
		if (schedule_delegrevoke_check(deleg_ctx, 1))
			goto out_revoke;
		goto out_free;
		break;
	case REVOKE:
		goto out_revoke;
	}
out_revoke:
	LogCrit(COMPONENT_NFS_V4,
		"Revoking delegation(%p)", deleg_entry);
	clientid->num_revokes++;
	inc_revokes(clientid->gsh_client);

	rc = deleg_revoke(deleg_entry);
	if (rc != STATE_SUCCESS) {
		LogCrit(COMPONENT_NFS_V4,
			"Delegation could not be revoked(%p)",
			deleg_entry);
	} else {
		LogDebug(COMPONENT_NFS_V4,
			 "Delegation revoked(%p)", deleg_entry);
	}
	cache_inode_put(deleg_ctx->drc_entry);
	pthread_mutex_lock(&clientid->cid_mutex);
	update_lease(clientid);
	pthread_mutex_unlock(&clientid->cid_mutex);
	put_gsh_export(deleg_ctx->drc_exp);
	dec_client_id_ref(clientid);
	gsh_free(deleg_ctx);

out_free:
	PTHREAD_RWLOCK_unlock(&entry->state_lock);
	fh = call->cbt.v_u.v4.args.argarray.argarray_val->
				nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val;
	gsh_free(fh);
	free_rpc_call(call);
	return 0; /*Always return zero, the delegation is recalled or revoked */
}

/**
 * @brief Send one delegation recall to one client.
 *
 * This function sends a cb_recall for one delegation, the caller has to lock
 * cache_entry->state_lock before calling this function.
 *
 * @param[in] deleg_entry Lock entry covering the delegation
 * @param[in] delegrecall_context
 */

static uint32_t delegrecall_one(struct deleg_data *deleg_entry,
				struct delegrecall_context *p_cargs)
{
	char *maxfh = NULL;
	uint32_t ret = 1;
	rpc_call_channel_t *chan;
	rpc_call_t *call = NULL;
	nfs_cb_argop4 argop[1];
	struct gsh_export *exp;
	struct cf_deleg_stats *clfl_stats;
	nfs_client_id_t *clientid;
	clientid = deleg_entry->dd_owner->so_owner.so_nfs4_owner.so_clientrec;
	clfl_stats = &deleg_entry->dd_state->state_data.deleg.sd_clfile_stats;

	/* record the first attempt to recall this delegation */
	if (clfl_stats->cfd_r_time == 0)
		clfl_stats->cfd_r_time = time(NULL);

	cache_entry_t *entry = deleg_entry->dd_entry;

	LogFullDebug(COMPONENT_FSAL_UP, "Recalling delegation:%p", deleg_entry);

	inc_recalls(clientid->gsh_client);

	exp = deleg_entry->dd_state->state_export;

	/* Attempt a recall only if channel state is UP */
	if (get_cb_chan_down(clientid)) {
		LogCrit(COMPONENT_NFS_CB,
			"Call back channel down, not issuing a recall");
		goto out;
	}

	chan = nfs_rpc_get_chan(clientid, NFS_RPC_FLAG_NONE);
	if (!chan) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
		/* TODO: move this to nfs_rpc_get_chan ? */
		set_cb_chan_down(clientid, true);
		goto out;
	}
	if (!chan->clnt) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
		set_cb_chan_down(clientid, true);
		goto out;
	}
	/* allocate a new call--freed in completion hook */
	call = alloc_rpc_call();

	if (!call) {
		LogCrit(COMPONENT_NFS_CB, "Could not allocate rpc call");
		goto out;
	}

	call->chan = chan;

	/* setup a compound */
	cb_compound_init_v4(&call->cbt, 1, 0,
			    clientid->cid_cb.v40.cb_callback_ident,
			    "brrring!!!", 10);

	argop->argop = NFS4_OP_CB_RECALL;
	COPY_STATEID(&argop->nfs_cb_argop4_u.opcbrecall.stateid,
			deleg_entry->dd_state);
	argop->nfs_cb_argop4_u.opcbrecall.truncate = false;

	maxfh = gsh_malloc(NFS4_FHSIZE); /* free in cb_completion_func() */
	if (maxfh == NULL) {
		LogDebug(COMPONENT_FSAL_UP, "FSAL_UP_DELEG: no mem, aborting.");
		goto out;
	}

	/* Convert it to a file handle */
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_len = 0;
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val = maxfh;

	/* Building a new fh */
	if (!nfs4_FSALToFhandle(&argop->nfs_cb_argop4_u.opcbrecall.fh,
				entry->obj_handle,
				exp)) {
		LogCrit(COMPONENT_FSAL_UP,
			"nfs4_FSALToFhandle failed, can not process recall");
		goto out;
	}

	/* add ops, till finished */
	cb_compound_add_op(&call->cbt, argop);

	/* set completion hook */
	call->call_hook = delegrecall_completion_func;

	if (!p_cargs) {
		p_cargs = gsh_malloc(sizeof(struct delegrecall_context));
		if (!p_cargs) {
			LogDebug(COMPONENT_FSAL_UP,
				 "FSAL_UP_DELEG: no mem, aborting.");
			goto out;
		}

		p_cargs->drc_clid = clientid;
		p_cargs->drc_entry = entry;
		p_cargs->drc_deleg_entry = deleg_entry;
		p_cargs->drc_exp = exp;
		COPY_STATEID(&p_cargs->drc_stateid, deleg_entry->dd_state);
	}

	/* call it (here, in current thread context)
	   ret is always 0 for async calls, might change in future */
	ret = nfs_rpc_submit_call(call, p_cargs, NFS_RPC_CALL_NONE);
	if (!ret)
		return ret;

out:
	inc_failed_recalls(clientid->gsh_client);
	if (maxfh)
		gsh_free(maxfh);
	if (call)
		free_rpc_call(call);

	if (eval_deleg_revoke(deleg_entry)) {
		ret = 0;
		goto out_revoke;
	} else {
		if (!p_cargs || schedule_delegrecall_task(p_cargs, 1)) {
			ret = 1;
			goto out_revoke;
		}
		ret = 0;
	}

	/* Keep the delegation in p_cargs */
	return ret;

out_revoke:
	LogCrit(COMPONENT_STATE, "Delegation(%p) will be revoked", deleg_entry);
	clientid->num_revokes++;
	inc_revokes(clientid->gsh_client);

	if (deleg_revoke(deleg_entry) != STATE_SUCCESS) {
		LogDebug(COMPONENT_FSAL_UP,
			 "Failed to revoke delegation(%p).", deleg_entry);
	} else {
		LogDebug(COMPONENT_FSAL_UP,
			 "Delegation revoked(%p)", deleg_entry);
	}
	cache_inode_put(entry);
	pthread_mutex_lock(&clientid->cid_mutex);
	update_lease(clientid);
	pthread_mutex_unlock(&clientid->cid_mutex);
	put_gsh_export(exp);
	dec_client_id_ref(clientid);
	if (p_cargs)
		gsh_free(p_cargs);

	return ret;

}

/**
 * @brief Check if the delegation needs to be revoked.
 *
 * @param[in] found_entry Lock entry covering the delegation
 * @param[in] entry File on which the delegation is held
 */

static void delegrevoke_check(void *ctx)
{
	uint32_t rc = 0;
	struct glist_head *glist, *glist_n;
	struct delegrecall_context *deleg_ctx = ctx;
	cache_entry_t *entry = deleg_ctx->drc_entry;
	struct deleg_data *deleg_entry = NULL;
	nfs_client_id_t *clid = deleg_ctx->drc_clid;

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	glist_for_each_safe(glist, glist_n, &entry->object.file.deleg_list) {
		deleg_entry = glist_entry(glist, struct deleg_data, dd_list);
		if (deleg_ctx->drc_deleg_entry == deleg_entry &&
			SAME_STATEID(&deleg_ctx->drc_stateid,
				     deleg_entry->dd_state)) {
			if (eval_deleg_revoke(deleg_ctx->drc_deleg_entry)) {
				LogDebug(COMPONENT_STATE,
					"Revoking delegation(%p)", deleg_entry);
				rc = deleg_revoke(deleg_entry);
				if (rc != STATE_SUCCESS) {
					LogCrit(COMPONENT_NFS_V4,
					  "Delegation could not be revoked(%p)",
						deleg_entry);
				} else {
					LogDebug(COMPONENT_NFS_V4,
						 "Delegation revoked(%p)",
						 deleg_entry);
				}
				cache_inode_put(entry);
				pthread_mutex_lock(&clid->cid_mutex);
				update_lease(clid);
				pthread_mutex_unlock(&clid->cid_mutex);
				put_gsh_export(deleg_ctx->drc_exp);
				dec_client_id_ref(clid);
				gsh_free(deleg_ctx);
			} else {
				LogFullDebug(COMPONENT_STATE,
					     "Not revoking the delegation(%p)",
					     deleg_entry);
				schedule_delegrevoke_check(deleg_ctx, 1);
			}
			break;
		} else {
			deleg_entry = NULL;
		}
	}
	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	if (!deleg_entry) {
		LogDebug(COMPONENT_NFS_CB, "Delgation is already returned");
		cache_inode_put(entry);
		pthread_mutex_lock(&clid->cid_mutex);
		update_lease(clid);
		pthread_mutex_unlock(&clid->cid_mutex);
		put_gsh_export(deleg_ctx->drc_exp);
		dec_client_id_ref(clid);
		gsh_free(deleg_ctx);
	}

	return;
}

static void delegrecall_task(void *ctx)
{
	struct glist_head *glist, *glist_n;
	struct delegrecall_context *deleg_ctx = ctx;
	cache_entry_t *entry = deleg_ctx->drc_entry;
	struct deleg_data *deleg_entry = NULL;

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);
	glist_for_each_safe(glist, glist_n, &entry->object.file.deleg_list) {
		deleg_entry = glist_entry(glist, struct deleg_data, dd_list);
		if (deleg_ctx->drc_deleg_entry == deleg_entry &&
			SAME_STATEID(&deleg_ctx->drc_stateid,
				     deleg_entry->dd_state)) {
			delegrecall_one(deleg_entry, deleg_ctx);
			break;
		} else {
			deleg_entry = NULL;
		}
	}
	PTHREAD_RWLOCK_unlock(&entry->state_lock);
	if (!deleg_entry) {
		LogDebug(COMPONENT_NFS_CB, "Delgation is already returned");
		cache_inode_put(entry);
		pthread_mutex_lock(&deleg_ctx->drc_clid->cid_mutex);
		update_lease(deleg_ctx->drc_clid);
		pthread_mutex_unlock(&deleg_ctx->drc_clid->cid_mutex);
		dec_client_id_ref(deleg_ctx->drc_clid);
		put_gsh_export(deleg_ctx->drc_exp);
		gsh_free(deleg_ctx);
	}
	return;
}

static int schedule_delegrecall_task(struct delegrecall_context *ctx,
				     uint32_t delay)
{
	int rc = 0;

	assert(ctx);

	rc = delayed_submit(delegrecall_task, ctx, delay * NS_PER_SEC);
	if (rc)
		LogDebug(COMPONENT_THREAD,
			 "delayed_submit failed with rc = %d", rc);

	return rc;
}

static int schedule_delegrevoke_check(struct delegrecall_context *ctx,
				      uint32_t delay)
{
	int rc = 0;

	assert(ctx);

	rc = delayed_submit(delegrevoke_check, ctx, delay * NS_PER_SEC);
	if (rc)
		LogDebug(COMPONENT_THREAD,
			 "delayed_submit failed with rc = %d", rc);

	return rc;
}

state_status_t delegrecall_impl(cache_entry_t *entry)
{
	struct glist_head *glist, *glist_n;
	struct deleg_data *deleg_entry = NULL;
	state_status_t rc = 0;
	uint32_t *deleg_state = NULL;
	nfs_client_id_t *clid = NULL;
	struct gsh_export *probe_exp;

	LogDebug(COMPONENT_FSAL_UP,
		 "FSAL_UP_DELEG: entry %p type %u",
		 entry, entry->type);

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	glist_for_each_safe(glist, glist_n, &entry->object.file.deleg_list) {
		deleg_entry = glist_entry(glist, struct deleg_data, dd_list);

		LogDebug(COMPONENT_NFS_CB, "deleg_entry %p", deleg_entry);
		deleg_state =
			&deleg_entry->dd_state->state_data.deleg.sd_state;
		if (*deleg_state != DELEG_GRANTED) {
			LogDebug(COMPONENT_FSAL_UP,
				 "Delegation already being recalled, NOOP");
			continue;
		}
		*deleg_state = DELEG_RECALL_WIP;

		entry->object.file.fdeleg_stats.fds_last_recall = time(NULL);

		/* Recall is an asynchrons operation, hence keep a hold on
		 * the export to prevent racing with remove_gsh_expor()
		 */
		probe_exp = get_gsh_export(deleg_entry->dd_export_id);
		if (probe_exp == NULL) {
			/* Remove export is underway, which will
			 * do the actual work, no need to do anything.
			 */
			LogDebug(COMPONENT_FSAL_UP,
				 "Racing with delete export. Do nothing");
			continue;
		}
		if (probe_exp != deleg_entry->dd_export) {
			/* Whole world changed under us and a new export
			 * with same old export-id has been added.
			 * Do nothing.
			 */
			LogWarn(COMPONENT_FSAL_UP,
				"Export changed prebe_exp:%p sle_exp: %p\n",
				probe_exp, deleg_entry->dd_export);
			put_gsh_export(probe_exp);
			continue;
		}

		if (nfs_client_id_get_confirmed(deleg_entry->
						dd_owner->so_owner.
						so_nfs4_owner.so_clientid,
						&clid) != CLIENT_ID_SUCCESS) {
			LogCrit(COMPONENT_NFS_CB,
				"No clid record, code %d", rc);
			put_gsh_export(probe_exp);
			continue;
		}

		/* Prevent client's lease expiring until we complete
		 * this recall/revoke operation. If the client's lease
		 * has already expired, let the reaper thread handling
		 * expired clients revoke this delegation, and we just
		 * skip it here.
		 */
		pthread_mutex_lock(&clid->cid_mutex);
		if (!reserve_lease(clid)) {
			pthread_mutex_unlock(&clid->cid_mutex);
			put_gsh_export(probe_exp);
			dec_client_id_ref(clid);
			continue;
		}
		pthread_mutex_unlock(&clid->cid_mutex);

		cache_inode_lru_ref(entry, LRU_FLAG_NONE);

		rc = delegrecall_one(deleg_entry, NULL);

		if (rc)
			LogCrit(COMPONENT_FSAL_UP,
				"delegrecall_one(%p) failed", deleg_entry);

	}
	PTHREAD_RWLOCK_unlock(&entry->state_lock);
	return rc;
}

/**
 * @brief Recall a delegation
 *
 * @param[in] handle Handle on which the delegation is held
 *
 * @return STATE_SUCCESS or errors.
 */
state_status_t delegrecall(struct fsal_module *fsal,
			   struct gsh_buffdesc *handle)
{
	cache_entry_t *entry = NULL;
	state_status_t rc = 0;

	if (!nfs_param.nfsv4_param.allow_delegations) {
		LogCrit(COMPONENT_FSAL_UP,
			"BUG: Got BREAK_DELEGATION: upcall when delegations are disabled, ignoring");
		return STATE_SUCCESS;
	}

	rc = cache_inode_status_to_state_status(up_get(fsal, handle, &entry));
	if (rc != STATE_SUCCESS) {
		LogDebug(COMPONENT_FSAL_UP,
			 "FSAL_UP_DELEG: cache inode get failed, rc %d", rc);
		/* Not an error. Expecting some nodes will not have it
		 * in cache in a cluster. */
		return rc;
	}

	rc = delegrecall_impl(entry);

	/* up_get() took a reference on the entry */
	cache_inode_put(entry);

	return rc;
}



/**
 * @brief The top level vector of operations
 */

struct fsal_up_vector fsal_up_top = {
	.lock_grant = lock_grant,
	.lock_avail = lock_avail,
	.invalidate = fsal_invalidate,
	.update = update,
	.layoutrecall = layoutrecall,
	.notify_device = notify_device,
	.delegrecall = delegrecall,
	.invalidate_close = invalidate_close
};

/** @} */
