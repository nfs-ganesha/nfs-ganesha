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
#include "sal_data.h"

struct delegrecall_context {
	/* Reserve lease during delegation recall */
	nfs_client_id_t *drc_clid;
	/* Preserve the stateid we are recalling */
	stateid4 drc_stateid;
	/* Hold a reference to the export during delegation recall */
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

/** Invalidate some or all of a cache entry and close if open
 *
 * This version should NOT be used if an FSAL supports extended
 * operations, instead, the FSAL may directly close the file as
 * necessary.
 *
 * @param[in] vec    Up ops vector
 * @param[in] handle Handle being invalidated
 * @param[in] flags  Flags governing invalidation
 *
 * @return FSAL status
 *
 */

static fsal_status_t invalidate_close(const struct fsal_up_vector *vec,
				      struct gsh_buffdesc *handle,
				      uint32_t flags)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** Invalidate some or all of a cache entry
 *
 * @param[in] vec    Up ops vector
 * @param[in] handle Handle being invalidated
 * @param[in] flags  Flags governing invalidation
 *
 * @return FSAL status
 *
 */

fsal_status_t invalidate(const struct fsal_up_vector *vec,
			 struct gsh_buffdesc *handle,
			 uint32_t flags)
{
	/* No need to invalidate with no cache */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Update cached attributes
 *
 * @param[in] vec    Up ops vector
 * @param[in] obj    Key to specify object
 * @param[in] attr   New attributes
 * @param[in] flags  Flags to govern update
 *
 * @return FSAL status
 */

static fsal_status_t update(const struct fsal_up_vector *vec,
			    struct gsh_buffdesc *obj,
			    struct attrlist *attr, uint32_t flags)
{
	/* No need to update with no cache */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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

static state_status_t lock_grant(const struct fsal_up_vector *vec,
				 struct gsh_buffdesc *file,
				 void *owner,
				 fsal_lock_param_t *lock_param)
{
	struct fsal_obj_handle *obj;
	fsal_status_t status;
	struct fsal_export *export = vec->up_fsal_export;

	status = export->exp_ops.create_handle(export, file, &obj, NULL);
	if (FSAL_IS_ERROR(status))
		return STATE_NOT_FOUND;

	grant_blocked_lock_upcall(obj, owner, lock_param);
	obj->obj_ops->put_ref(obj);
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

static state_status_t lock_avail(const struct fsal_up_vector *vec,
				 struct gsh_buffdesc *file,
				 void *owner,
				 fsal_lock_param_t *lock_param)
{
	struct fsal_obj_handle *obj;
	fsal_status_t status;
	struct fsal_export *export = vec->up_fsal_export;

	status = export->exp_ops.create_handle(export, file, &obj, NULL);
	if (FSAL_IS_ERROR(status))
		return STATE_NOT_FOUND;

	available_blocked_lock_upcall(obj, owner, lock_param);
	obj->obj_ops->put_ref(obj);
	return STATE_SUCCESS;
}

/* @note The state_lock MUST be held for write */
static void destroy_recall(struct state_layout_recall_file *recall)
{
	if (recall == NULL)
		return;

	while (!glist_empty(&recall->state_list)) {
		struct recall_state_list *list_entry;
		/* The first entry in the queue */
		list_entry = glist_first_entry(&recall->state_list,
					       struct recall_state_list,
					       link);
		dec_state_t_ref(list_entry->state);
		glist_del(&list_entry->link);
		gsh_free(list_entry);
	}

	/* Remove from entry->layoutrecall_list */
	glist_del(&recall->entry_link);
	gsh_free(recall);
}

/**
 * @brief Create layout recall state
 *
 * This function creates the layout recall state and work list for a
 * LAYOUTRECALL operation on a file.
 *
 * @note the state_lock MUST be held for write
 *
 * @param[in,out] obj     The file on which to send the recall
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
 */

static state_status_t create_file_recall(struct fsal_obj_handle *obj,
					 layouttype4 type,
					 const struct pnfs_segment *segment,
					 void *cookie,
					 struct layoutrecall_spec *spec,
					 struct state_layout_recall_file
					 **recout)
{
	/* True if a layout matching the request has been found */
	bool found = false;
	/* Iterator over all states on the cache entry */
	struct glist_head *state_iter = NULL;
	/* Error return code */
	state_status_t rc = STATE_SUCCESS;
	/* The recall object referenced by future returns */
	struct state_layout_recall_file *recall =
	    gsh_malloc(sizeof(struct state_layout_recall_file));

	glist_init(&recall->state_list);
	recall->entry_link.next = NULL;
	recall->entry_link.prev = NULL;
	recall->obj = obj;
	recall->type = type;
	recall->segment = *segment;
	recall->recall_cookie = cookie;

	if ((segment->length == 0)
	    || ((segment->length != UINT64_MAX)
		&& (segment->offset <= UINT64_MAX - segment->length))) {
		rc = STATE_INVALID_ARGUMENT;
		goto out;
	}

	glist_for_each(state_iter, &obj->state_hdl->file.list_of_states) {
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
		/* referenced owner */
		state_owner_t *owner = get_state_owner_ref(s);

		if (owner == NULL) {
			/* This state is going stale, skip */
			continue;
		}

		if ((s->state_type != STATE_TYPE_LAYOUT)
		    || (s->state_data.layout.state_layout_type != type)) {
			continue;
		}

		if (spec) {
			switch (spec->how) {
			case layoutrecall_howspec_exactly:
				if (spec->u.client !=
				    owner->so_owner.so_nfs4_owner.so_clientid) {
					dec_state_owner_ref(owner);
					continue;
				}
				break;

			case layoutrecall_howspec_complement:
				if (spec->u.client ==
				    owner->so_owner.so_nfs4_owner.so_clientid) {
					dec_state_owner_ref(owner);
					continue;
				}
				break;

			case layoutrecall_not_specced:
				break;
			}
		}

		dec_state_owner_ref(owner);

		glist_for_each(seg_iter,
			       &s->state_data.layout.state_segments) {
			state_layout_segment_t *g = glist_entry(
				seg_iter,
				state_layout_segment_t,
				sls_state_segments);
			if (pnfs_segments_overlap(segment, &g->sls_segment))
				match = true;
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

			list_entry->state = s;
			glist_add_tail(&recall->state_list, &list_entry->link);
			inc_state_t_ref(s);
			found = true;
		}
	}

	if (!found)
		rc = STATE_NOT_FOUND;

 out:

	if (rc == STATE_SUCCESS) {
		glist_add_tail(&obj->state_hdl->file.layoutrecall_list,
			       &recall->entry_link);
		*recout = recall;
	} else {
		/* Destroy the recall list constructed so far. */
		destroy_recall(recall);
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
state_status_t layoutrecall(const struct fsal_up_vector *vec,
			    struct gsh_buffdesc *handle,
			    layouttype4 layout_type, bool changed,
			    const struct pnfs_segment *segment, void *cookie,
			    struct layoutrecall_spec *spec)
{
	/* Return code */
	state_status_t rc = STATE_SUCCESS;
	/* file on which to operate */
	struct fsal_obj_handle *obj = NULL;
	/* The recall object */
	struct state_layout_recall_file *recall = NULL;
	/* Iterator over the work list */
	struct glist_head *wi = NULL;
	struct gsh_export *exp = NULL;
	state_owner_t *owner = NULL;
	struct fsal_export *export = vec->up_fsal_export;

	rc = state_error_convert(export->exp_ops.create_handle(export, handle,
							       &obj, NULL));
	if (rc != STATE_SUCCESS)
		return rc;

	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
	/* We build up the list before consuming it so that we have
	   every state on the list before we start executing returns. */
	rc = create_file_recall(obj, layout_type, segment, cookie, spec,
				&recall);
	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
	if (rc != STATE_SUCCESS)
		goto out;

	/**
	 * @todo This leaves us open to a race if a return comes in
	 * while we're traversing the work list. However, the race may now
	 * be harmless since everything is refcounted.
	 */
	glist_for_each(wi, &recall->state_list) {
		/* The current entry in the queue */
		struct recall_state_list *g = glist_entry(wi,
							  struct
							  recall_state_list,
							  link);
		struct state_t *s = g->state;
		struct layoutrecall_cb_data *cb_data;
		nfs_cb_argop4 *arg;
		CB_LAYOUTRECALL4args *cb_layoutrec;
		layoutrecall_file4 *layout;

		cb_data = gsh_malloc(sizeof(struct layoutrecall_cb_data));

		arg = &cb_data->arg;
		arg->argop = NFS4_OP_CB_LAYOUTRECALL;
		cb_layoutrec = &arg->nfs_cb_argop4_u.opcblayoutrecall;
		layout = &cb_layoutrec->clora_recall.layoutrecall4_u.lor_layout;

		cb_layoutrec->clora_type = layout_type;
		cb_layoutrec->clora_iomode = segment->io_mode;
		cb_layoutrec->clora_changed = changed;
		cb_layoutrec->clora_recall.lor_recalltype = LAYOUTRECALL4_FILE;
		layout->lor_offset = segment->offset;
		layout->lor_length = segment->length;


		if (!get_state_obj_export_owner_refs(s, NULL, &exp, &owner)) {
			/* The export, owner, or state_t has gone stale,
			 * skip this entry
			 */
			gsh_free(cb_data);
			continue;
		}

		if (!nfs4_FSALToFhandle(true, &layout->lor_fh, obj, exp)) {
			gsh_free(cb_data);
			put_gsh_export(exp);
			dec_state_owner_ref(owner);
			rc = STATE_MALLOC_ERROR;
			goto out;
		}

		put_gsh_export(exp);

		update_stateid(s, &layout->lor_stateid, NULL, "LAYOUTRECALL");

		memcpy(cb_data->stateid_other, s->stateid_other, OTHERSIZE);
		cb_data->segment = *segment;
		cb_data->client = owner->so_owner.so_nfs4_owner.so_clientrec;
		cb_data->attempts = 0;

		dec_state_owner_ref(owner);

		layoutrecall_one_call(cb_data);
	}

 out:

	/* Free the recall list resources */
	destroy_recall(recall);
	obj->obj_ops->put_ref(obj);

	return rc;
}

/**
 * @brief Free a CB_LAYOUTRECALL
 *
 * @param[in] op Operation to free
 */

static void free_layoutrec(nfs_cb_argop4 *op)
{
	layoutrecall4 *clora_recall =
		&op->nfs_cb_argop4_u.opcblayoutrecall.clora_recall;

	gsh_free(clora_recall->layoutrecall4_u.lor_layout.lor_fh.nfs_fh4_val);
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
 */

static void layoutrec_completion(rpc_call_t *call)
{
	struct layoutrecall_cb_data *cb_data = call->call_arg;
	bool deleted = false;
	state_t *state = NULL;
	struct root_op_context root_op_context;
	struct fsal_obj_handle *obj = NULL;
	struct gsh_export *export = NULL;
	state_owner_t *owner = NULL;
	bool ok = false;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	LogFullDebug(COMPONENT_NFS_CB, "status %d cb_data %p",
		     call->cbt.v_u.v4.res.status, cb_data);

	/* Get this out of the way up front */
	if (call->states & NFS_CB_CALL_ABORTED)
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
		nfs41_release_single(call);
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
		nfs41_release_single(call);
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

	ok = get_state_obj_export_owner_refs(state, &obj, &export, &owner);

	if (ok) {
		enum fsal_layoutreturn_circumstance circumstance;

		if (!(call->states & NFS_CB_CALL_ABORTED)
		 && call->cbt.v_u.v4.res.status == NFS4ERR_NOMATCHING_LAYOUT)
			circumstance = circumstance_client;
		else
			circumstance = circumstance_revoke;

		/**
		 * @todo This is where you would record that a
		 * recall was completed, one way or the other.
		 * The clientid is specified in
		 * owner->so_owner.so_nfs4_owner.so_clientid
		 * The number of times we retried the call is
		 * specified in cb_data->attempts and the time we
		 * specified the first call is in
		 * cb_data->first_recall.  If
		 * call->cbt.v_u.v4.res.status is
		 * NFS4ERR_NOMATCHING_LAYOUT it was a successful
		 * return, otherwise we count it as an error.
		 */

		PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);

		root_op_context.req_ctx.clientid =
			&owner->so_owner.so_nfs4_owner.so_clientid;
		root_op_context.req_ctx.ctx_export = export;
		root_op_context.req_ctx.fsal_export = export->fsal_export;

		nfs4_return_one_state(obj,
				      LAYOUTRETURN4_FILE, circumstance,
				      state, cb_data->segment, 0, NULL,
				      &deleted);

		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
	}

	if (state != NULL) {
		/* Release the reference taken above */
		dec_state_t_ref(state);
	}

	free_layoutrec(&call->cbt.v_u.v4.args.argarray.argarray_val[1]);
	nfs41_release_single(call);
	gsh_free(cb_data);

out:
	release_root_op_context();

	if (ok) {
		/* Release the export */
		put_gsh_export(export);

		/* Release object ref */
		obj->obj_ops->put_ref(obj);

		/* Release the owner */
		dec_state_owner_ref(owner);
	}
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
	state_t *state;
	bool deleted = false;
	struct root_op_context root_op_context;
	struct fsal_obj_handle *obj = NULL;
	struct gsh_export *export = NULL;
	state_owner_t *owner = NULL;
	bool ok = false;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	state = nfs4_State_Get_Pointer(cb_data->stateid_other);

	ok = get_state_obj_export_owner_refs(state, &obj, &export, &owner);

	if (ok) {
		PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);

		root_op_context.req_ctx.clientid =
			&owner->so_owner.so_nfs4_owner.so_clientid;
		root_op_context.req_ctx.ctx_export = export;
		root_op_context.req_ctx.fsal_export = export->fsal_export;

		nfs4_return_one_state(obj, LAYOUTRETURN4_FILE,
				      circumstance_revoke, state,
				      cb_data->segment, 0, NULL, &deleted);

		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
	}

	release_root_op_context();
	gsh_free(cb_data);

	if (state != NULL) {
		/* Release the reference taken above */
		dec_state_t_ref(state);
	}

	if (ok) {
		/* Release the export */
		put_gsh_export(export);

		/* Release object ref */
		obj->obj_ops->put_ref(obj);

		/* Release the owner */
		dec_state_owner_ref(owner);
	}
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
	state_t *state;
	int code;
	struct root_op_context root_op_context;
	struct fsal_obj_handle *obj = NULL;
	struct gsh_export *export = NULL;
	state_owner_t *owner = NULL;
	bool ok = false;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	if (cb_data->attempts == 0)
		now(&cb_data->first_recall);

	state = nfs4_State_Get_Pointer(cb_data->stateid_other);

	ok = get_state_obj_export_owner_refs(state, &obj, &export, &owner);

	if (ok) {
		PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);

		root_op_context.req_ctx.clientid =
		    &owner->so_owner.so_nfs4_owner.so_clientid;
		root_op_context.req_ctx.ctx_export = export;
		root_op_context.req_ctx.fsal_export = export->fsal_export;

		code = nfs_rpc_cb_single(cb_data->client, &cb_data->arg,
					  &state->state_refer,
					  layoutrec_completion,
					  cb_data);

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

				nfs4_return_one_state(obj,
						      LAYOUTRETURN4_FILE,
						      circumstance_revoke,
						      state, cb_data->segment,
						      0, NULL, &deleted);
				free_layoutrec(&cb_data->arg);
				gsh_free(cb_data);
			}
		} else {
			++cb_data->attempts;
		}

		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

	} else {
		gsh_free(cb_data);
	}

	release_root_op_context();

	if (state != NULL) {
		/* Release the reference taken above */
		dec_state_t_ref(state);
	}

	if (ok) {
		/* Release the export */
		put_gsh_export(export);

		/* Release object ref */
		obj->obj_ops->put_ref(obj);

		/* Release the owner */
		dec_state_owner_ref(owner);
	}
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
 */

static void notifydev_completion(rpc_call_t *call)
{
	LogFullDebug(COMPONENT_NFS_CB, "status %d arg %p",
		     call->cbt.v_u.v4.res.status, call->call_arg);
	gsh_free(call->call_arg);
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
	code = nfs_rpc_cb_single(clientid, &arg->arg, NULL,
				 notifydev_completion, &arg->arg);
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

bool eval_deleg_revoke(struct state_t *deleg_state)
{
	struct cf_deleg_stats *clfl_stats;
	time_t curr_time;
	time_t recall_success_time, first_recall_time;
	uint32_t lease_lifetime = nfs_param.nfsv4_param.lease_lifetime;

	clfl_stats = &deleg_state->state_data.deleg.sd_clfile_stats;

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
				struct state_t *state,
				rpc_call_t *call)
{
	enum recall_resp_action resp_action;
	char str[DISPLAY_STATEID_OTHER_SIZE] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;

	if (isDebug(COMPONENT_NFS_CB)) {
		display_stateid_other(&dspbuf, p_cargs->drc_stateid.other);
		str_valid = true;
	}

	struct cf_deleg_stats *clfl_stats =
		&state->state_data.deleg.sd_clfile_stats;

	switch (call->cbt.v_u.v4.res.status) {
	case NFS4_OK:
		if (str_valid)
			LogDebug(COMPONENT_NFS_CB,
				 "Delegation %s successfully recalled", str);
		resp_action = DELEG_RET_WAIT;
		clfl_stats->cfd_rs_time =
					time(NULL);
		break;
	case NFS4ERR_BADHANDLE:
		if (str_valid)
			LogDebug(COMPONENT_NFS_CB,
				 "Client sent NFS4ERR_BADHANDLE response, retrying recall for Delegation %s",
				 str);
		resp_action = DELEG_RECALL_SCHED;
		break;
	case NFS4ERR_DELAY:
		if (str_valid)
			LogDebug(COMPONENT_NFS_CB,
				 "Client sent NFS4ERR_DELAY response, retrying recall for Delegation %s",
				 str);
		resp_action = DELEG_RECALL_SCHED;
		break;
	case  NFS4ERR_BAD_STATEID:
		if (str_valid)
			LogDebug(COMPONENT_NFS_CB,
				 "Client sent NFS4ERR_BAD_STATEID response, retrying recall for  Delegation %s",
				 str);
		resp_action = DELEG_RECALL_SCHED;
		break;
	default:
		/* some other NFS error, consider the recall failed */
		if (str_valid)
			LogDebug(COMPONENT_NFS_CB,
				 "Client sent %d response, retrying recall for Delegation %s",
				 call->cbt.v_u.v4.res.status,
				 str);
		resp_action = DELEG_RECALL_SCHED;
		break;
	}
	return resp_action;
}

static inline void
free_delegrecall_context(struct delegrecall_context *deleg_ctx)
{
	PTHREAD_MUTEX_lock(&deleg_ctx->drc_clid->cid_mutex);
	update_lease(deleg_ctx->drc_clid);
	PTHREAD_MUTEX_unlock(&deleg_ctx->drc_clid->cid_mutex);

	put_gsh_export(deleg_ctx->drc_exp);

	dec_client_id_ref(deleg_ctx->drc_clid);

	gsh_free(deleg_ctx);
}

/**
 * @brief Handle the reply to a CB_RECALL
 *
 * @param[in] call  The RPC call being completed
 *
 * @return 0, constantly.
 */

static void delegrecall_completion_func(rpc_call_t *call)
{
	enum recall_resp_action resp_act;
	nfsstat4 rc = NFS4_OK;
	struct delegrecall_context *deleg_ctx = call->call_arg;
	uint32_t minorversion = deleg_ctx->drc_clid->cid_minorversion;
	struct state_t *state;
	struct fsal_obj_handle *obj = NULL;
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	CB_RECALL4args *opcbrecall;
	struct req_op_context *save_ctx = op_ctx, req_ctx = {0};
	struct gsh_export *export = NULL;
	bool ret = false;

	LogDebug(COMPONENT_NFS_CB, "%p %s", call,
		 !(call->states & NFS_CB_CALL_ABORTED) ? "Success" : "Failed");

	state = nfs4_State_Get_Pointer(deleg_ctx->drc_stateid.other);

	if (state == NULL) {
		LogDebug(COMPONENT_NFS_CB, "Delegation is already returned");
		goto out_free_drc;
	}

	op_ctx = &req_ctx;

	ret = get_state_obj_export_owner_refs(state, &obj,
					     &export,
					     NULL);

	if (!ret || obj == NULL) {
		LogDebug(COMPONENT_NFS_CB, "Stale file");
		goto out_free_drc;
	}

	op_ctx->ctx_export = export;
	op_ctx->fsal_export = export->fsal_export;

	if (isDebug(COMPONENT_NFS_CB)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_stateid(&dspbuf, state);
		LogDebug(COMPONENT_NFS_CB, "deleg_entry %s", str);
	}

	if (!(call->states & NFS_CB_CALL_ABORTED)) {
		LogMidDebug(COMPONENT_NFS_CB, "call result: %d",
			    call->call_req.cc_error.re_status);
		if (call->call_req.cc_error.re_status != RPC_SUCCESS) {
			LogEvent(COMPONENT_NFS_CB,
				 "call result: %d, marking CB channel down",
				 call->call_req.cc_error.re_status);
			set_cb_chan_down(deleg_ctx->drc_clid, true);
			resp_act = DELEG_RECALL_SCHED;
		} else
			resp_act = handle_recall_response(deleg_ctx,
							  state,
							  call);
	} else {
		LogEvent(COMPONENT_NFS_CB,
			 "Aborted: %d, marking CB channel down",
			 call->call_req.cc_error.re_status);
		set_cb_chan_down(deleg_ctx->drc_clid, true);
		/* Mark the recall as failed */
		resp_act = DELEG_RECALL_SCHED;
	}
	switch (resp_act) {
	case DELEG_RECALL_SCHED:
		if (eval_deleg_revoke(state))
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
	case REVOKE:
		goto out_revoke;
	}

out_revoke:

	display_stateid(&dspbuf, state);

	LogCrit(COMPONENT_NFS_V4,
		"Revoking delegation for %s", str);

	deleg_ctx->drc_clid->num_revokes++;
	inc_revokes(deleg_ctx->drc_clid->gsh_client);

	/* state_del_locked is called from deleg_revoke, take the lock. */
	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
	rc = deleg_revoke(obj, state);
	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

	if (rc != NFS4_OK) {
		LogCrit(COMPONENT_NFS_V4,
			"Delegation could not be revoked for %s", str);
	} else {
		LogDebug(COMPONENT_NFS_V4,
			 "Delegation revoked for %s", str);
	}

out_free_drc:

	free_delegrecall_context(deleg_ctx);

out_free:
	if (minorversion == 0) {
		opcbrecall = &call->cbt.v_u.v4.args.argarray.argarray_val[0]
				.nfs_cb_argop4_u.opcbrecall;
		nfs4_freeFH(&opcbrecall->fh);
	} else {
		opcbrecall = &call->cbt.v_u.v4.args.argarray.argarray_val[1]
				.nfs_cb_argop4_u.opcbrecall;
		nfs4_freeFH(&opcbrecall->fh);
		nfs41_release_single(call);
	}

	if (state != NULL)
		dec_state_t_ref(state);

	/* Release the obj ref and export ref. */
	if (obj != NULL)
		obj->obj_ops->put_ref(obj);

	if (export != NULL)
		put_gsh_export(export);

	op_ctx = save_ctx;
}

/**
 * @brief Send one delegation recall to one client.
 *
 * This function sends a cb_recall for one delegation, the caller has to lock
 * cache_entry->state_lock before calling this function.
 *
 * @param[in] obj The file being delegated
 * @param[in] deleg_entry Lock entry covering the delegation
 * @param[in] delegrecall_context
 */

void delegrecall_one(struct fsal_obj_handle *obj,
		     struct state_t *state,
		     struct delegrecall_context *p_cargs)
{
	int ret;
	nfs_cb_argop4 argop;
	struct cf_deleg_stats *clfl_stats;
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;

	clfl_stats = &state->state_data.deleg.sd_clfile_stats;

	if (isDebug(COMPONENT_FSAL_UP)) {
		display_stateid(&dspbuf, state);
		str_valid = true;
	}

	/* record the first attempt to recall this delegation */
	if (clfl_stats->cfd_r_time == 0)
		clfl_stats->cfd_r_time = time(NULL);

	if (str_valid)
		LogFullDebug(COMPONENT_FSAL_UP, "Recalling delegation %s", str);

	inc_recalls(p_cargs->drc_clid->gsh_client);

	argop.argop = NFS4_OP_CB_RECALL;
	COPY_STATEID(&argop.nfs_cb_argop4_u.opcbrecall.stateid, state);
	argop.nfs_cb_argop4_u.opcbrecall.truncate = false;

	/* Convert it to a file handle */
	if (!nfs4_FSALToFhandle(true, &argop.nfs_cb_argop4_u.opcbrecall.fh,
				obj, p_cargs->drc_exp)) {
		LogCrit(COMPONENT_FSAL_UP,
			"nfs4_FSALToFhandle failed, can not process recall");
		goto out;
	}

	ret = nfs_rpc_cb_single(p_cargs->drc_clid, &argop, &state->state_refer,
				delegrecall_completion_func, p_cargs);
	if (ret == 0)
		return;
	LogDebug(COMPONENT_FSAL_UP, "nfs_rpc_cb_single returned %d", ret);

out:
	inc_failed_recalls(p_cargs->drc_clid->gsh_client);
	nfs4_freeFH(&argop.nfs_cb_argop4_u.opcbrecall.fh);

	if (!eval_deleg_revoke(state) &&
	    !schedule_delegrecall_task(p_cargs, 1)) {
		/* Keep the delegation in p_cargs */
		if (str_valid)
			LogDebug(COMPONENT_FSAL_UP,
				 "Retry delegation for %s", str);
		return;
	}

	if (!str_valid)
		display_stateid(&dspbuf, state);

	LogCrit(COMPONENT_STATE, "Delegation will be revoked for %s", str);

	p_cargs->drc_clid->num_revokes++;
	inc_revokes(p_cargs->drc_clid->gsh_client);

	if (deleg_revoke(obj, state) != NFS4_OK) {
		LogDebug(COMPONENT_FSAL_UP,
			 "Failed to revoke delegation %s.", str);
	} else {
		LogDebug(COMPONENT_FSAL_UP,
			 "Delegation revoked %s", str);
	}

	free_delegrecall_context(p_cargs);
}

/**
 * @brief Check if the delegation needs to be revoked.
 *
 * @param[in] ctx Delegation recall context describing the delegation
 */

static void delegrevoke_check(void *ctx)
{
	nfsstat4 rc = NFS4_OK;
	struct delegrecall_context *deleg_ctx = ctx;
	struct fsal_obj_handle *obj = NULL;
	struct state_t *state = NULL;
	bool free_drc = true;
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	struct req_op_context *save_ctx = op_ctx, req_ctx = {0};
	struct gsh_export *export = NULL;
	bool   ret = false;

	state = nfs4_State_Get_Pointer(deleg_ctx->drc_stateid.other);

	if (state == NULL) {
		LogDebug(COMPONENT_NFS_CB, "Delegation is already returned");
		goto out;
	}

	if (isDebug(COMPONENT_NFS_CB)) {
		display_stateid(&dspbuf, state);
		str_valid = true;
	}

	/* op_ctx may be used by state_del_locked and others */
	op_ctx = &req_ctx;

	ret = get_state_obj_export_owner_refs(state, &obj,
					     &export,
					     NULL);

	if (!ret || obj == NULL) {
		LogDebug(COMPONENT_NFS_CB, "Stale file");
		goto out;
	}

	op_ctx->ctx_export = export;
	op_ctx->fsal_export = export->fsal_export;

	if (eval_deleg_revoke(state)) {
		if (str_valid)
			LogDebug(COMPONENT_STATE,
				"Revoking delegation for %s", str);

		/* state_del_locked is called from deleg_revoke,
		 * take the lock.
		 */
		PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
		rc = deleg_revoke(obj, state);
		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

		if (rc != NFS4_OK) {
			if (!str_valid)
				display_stateid(&dspbuf, state);

			LogCrit(COMPONENT_NFS_V4,
				"Delegation could not be revoked for %s",
				str);
		} else {
			if (str_valid)
				LogDebug(COMPONENT_NFS_V4,
					 "Delegation revoked for %s",
					 str);
		}
	} else {
		if (str_valid)
			LogFullDebug(COMPONENT_STATE,
				     "Not yet revoking the delegation for %s",
				     str);

		schedule_delegrevoke_check(deleg_ctx, 1);
		free_drc = false;
	}

 out:

	if (free_drc)
		free_delegrecall_context(deleg_ctx);

	if (state != NULL)
		dec_state_t_ref(state);

	/* Release the obj ref and export ref. */
	if (obj != NULL)
		obj->obj_ops->put_ref(obj);

	if (export != NULL)
		put_gsh_export(export);

	op_ctx = save_ctx;
}

static void delegrecall_task(void *ctx)
{
	struct delegrecall_context *deleg_ctx = ctx;
	struct state_t *state;
	struct fsal_obj_handle *obj;
	bool   ret = false;
	struct req_op_context *save_ctx, req_ctx = {0};
	struct gsh_export *export = NULL;

	state = nfs4_State_Get_Pointer(deleg_ctx->drc_stateid.other);

	if (state == NULL) {
		LogDebug(COMPONENT_NFS_CB, "Delgation is already returned");
		free_delegrecall_context(deleg_ctx);
		return;
	}

	/* op_ctx may be used by state_del_locked and others */
	save_ctx = op_ctx;
	op_ctx = &req_ctx;

	ret = get_state_obj_export_owner_refs(state, &obj,
					     &export,
					     NULL);
	if (!ret || obj == NULL) {
		LogDebug(COMPONENT_NFS_CB,
			 "Delgation recall skipped due to stale file");
		goto out;
	}

	op_ctx->ctx_export = export;
	op_ctx->fsal_export = export->fsal_export;

	/* state_del_locked is called from deleg_revoke, take the lock. */
	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
	delegrecall_one(obj, state, deleg_ctx);
	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

	/* Release the obj ref and export ref. */
	obj->obj_ops->put_ref(obj);
	put_gsh_export(export);
	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;
out:
	dec_state_t_ref(state);
	op_ctx = save_ctx;
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

state_status_t delegrecall_impl(struct fsal_obj_handle *obj)
{
	struct glist_head *glist, *glist_n;
	state_status_t rc = 0;
	uint32_t *deleg_state = NULL;
	struct state_t *state;
	state_owner_t *owner;
	struct delegrecall_context *drc_ctx;
	struct req_op_context *save_ctx = op_ctx, req_ctx = {0};

	LogDebug(COMPONENT_FSAL_UP,
		 "FSAL_UP_DELEG: obj %p type %u",
		 obj, obj->type);

	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
	glist_for_each_safe(glist, glist_n,
			    &obj->state_hdl->file.list_of_states) {
		state = glist_entry(glist, struct state_t, state_list);

		if (state->state_type != STATE_TYPE_DELEG)
			continue;

		if (isDebug(COMPONENT_NFS_CB)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_stateid(&dspbuf, state);
			LogDebug(COMPONENT_NFS_CB, "Delegation for %s", str);
		}

		deleg_state = &state->state_data.deleg.sd_state;
		if (*deleg_state != DELEG_GRANTED) {
			LogDebug(COMPONENT_FSAL_UP,
				 "Delegation already being recalled, NOOP");
			continue;
		}
		*deleg_state = DELEG_RECALL_WIP;

		drc_ctx = gsh_malloc(sizeof(struct delegrecall_context));

		/* Get references on the owner and the the export. The
		 * export reference we will hold while we perform the recall.
		 * The owner reference will be used to get access to the
		 * clientid and reserve the lease.
		 */
		if (!get_state_obj_export_owner_refs(state, NULL,
						     &drc_ctx->drc_exp,
						     &owner)) {
			LogDebug(COMPONENT_FSAL_UP,
				 "Something is going stale, no need to recall delegation");
			gsh_free(drc_ctx);
			continue;
		}

		/* op_ctx may be used by state_del_locked and others */
		op_ctx = &req_ctx;
		op_ctx->ctx_export = drc_ctx->drc_exp;
		op_ctx->fsal_export = drc_ctx->drc_exp->fsal_export;

		drc_ctx->drc_clid = owner->so_owner.so_nfs4_owner.so_clientrec;
		COPY_STATEID(&drc_ctx->drc_stateid, state);
		inc_client_id_ref(drc_ctx->drc_clid);
		dec_state_owner_ref(owner);

		obj->state_hdl->file.fdeleg_stats.fds_last_recall = time(NULL);

		/* Prevent client's lease expiring until we complete
		 * this recall/revoke operation. If the client's lease
		 * has already expired, let the reaper thread handling
		 * expired clients revoke this delegation, and we just
		 * skip it here.
		 */
		PTHREAD_MUTEX_lock(&drc_ctx->drc_clid->cid_mutex);
		if (!reserve_lease(drc_ctx->drc_clid)) {
			PTHREAD_MUTEX_unlock(&drc_ctx->drc_clid->cid_mutex);
			put_gsh_export(drc_ctx->drc_exp);
			dec_client_id_ref(drc_ctx->drc_clid);
			gsh_free(drc_ctx);
			continue;
		}
		PTHREAD_MUTEX_unlock(&drc_ctx->drc_clid->cid_mutex);

		delegrecall_one(obj, state, drc_ctx);
	}
	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

	op_ctx = save_ctx;
	return rc;
}

/**
 * @brief Recall a delegation
 *
 * @param[in] handle Handle on which the delegation is held
 *
 * @return STATE_SUCCESS or errors.
 */
state_status_t delegrecall(const struct fsal_up_vector *vec,
			   struct gsh_buffdesc *handle)
{
	struct fsal_export *export = vec->up_fsal_export;
	struct fsal_obj_handle *obj = NULL;
	state_status_t rc = 0;

	if (!nfs_param.nfsv4_param.allow_delegations) {
		LogCrit(COMPONENT_FSAL_UP,
			"BUG: Got BREAK_DELEGATION: upcall when delegations are disabled, ignoring");
		return STATE_SUCCESS;
	}

	rc = state_error_convert(export->exp_ops.create_handle(export, handle,
							       &obj, NULL));
	if (rc != STATE_SUCCESS) {
		LogDebug(COMPONENT_FSAL_UP,
			 "FSAL_UP_DELEG: cache inode get failed, rc %d", rc);
		/* Not an error. Expecting some nodes will not have it
		 * in cache in a cluster. */
		return rc;
	}

	rc = delegrecall_impl(obj);
	obj->obj_ops->put_ref(obj);
	return rc;
}

struct cbgetattr_context {
	struct fsal_obj_handle *obj;
	nfs_client_id_t *clid; /* client holding deleg */
	struct gsh_export *ctx_export;	/*< current export */
};

/*
 * release cbgetattr_context args reference and
 * free the memory allocated.
 */
static inline void
free_cbgetattr_context(struct cbgetattr_context *cbgetattr_ctx)
{
	PTHREAD_MUTEX_lock(&cbgetattr_ctx->clid->cid_mutex);
	update_lease(cbgetattr_ctx->clid);
	PTHREAD_MUTEX_unlock(&cbgetattr_ctx->clid->cid_mutex);

	put_gsh_export(cbgetattr_ctx->ctx_export);

	dec_client_id_ref(cbgetattr_ctx->clid);
	cbgetattr_ctx->obj->obj_ops->put_ref(cbgetattr_ctx->obj);

	gsh_free(cbgetattr_ctx);
}

/**
 * @brief act on response received to CB_GETATTR request
 *
 * On successful rpc callback request, read and store
 * the attributes sent by client and mark cbgetattr_state
 * as CB_GETATTR_RESP_OK
 *
 * @param[in] cbgetattr_context
 * @param[in] rpc_call
 *
 * @return CB_GETATTR_RESP_OK on success
 *	 CB_GETATTR_FAILED on failure
 */
enum
cbgetattr_state handle_getattr_response(struct cbgetattr_context *cbg_ctx,
					rpc_call_t *call)
{
	fattr4 attr;
	struct attrlist rsp_attr;
	int rc = 0;
	enum cbgetattr_state cb_state;
	struct fsal_obj_handle *obj = cbg_ctx->obj;
	nfs_client_id_t *clid = cbg_ctx->clid;
	nfs_cb_resop4 *cbr = NULL;
	CB_GETATTR4res *res = NULL;
	const struct fsal_up_vector *event_func;
	struct attrlist up_attr = {0, };
	uint32_t upflags = 0;
	struct gsh_buffdesc key;
	fsal_status_t fsal_status = {0,};
	struct root_op_context root_ctx;
	struct timespec	timeo = { .tv_sec = time(NULL) + 5,
				  .tv_nsec = 0 };

	if (clid->cid_minorversion == 0)
		cbr = &call->cbt.v_u.v4.res.resarray.resarray_val[0];
	else
		cbr = &call->cbt.v_u.v4.res.resarray.resarray_val[1];

	res = &cbr->nfs_cb_resop4_u.opcbgetattr;
	attr = res->CB_GETATTR4res_u.resok4.obj_attributes;

	rc = nfs4_Fattr_To_FSAL_attr(&rsp_attr, &attr, NULL);

	if (rc) {
		cb_state = CB_GETATTR_FAILED;
		goto out;
	}

	cb_state = CB_GETATTR_RSP_OK;

	/* Determine if the file is modified the very first time */
	if (!obj->state_hdl->file.cbgetattr.modified &&
	   ((obj->state_hdl->file.cbgetattr.change ==
		rsp_attr.change) &&
	    (obj->state_hdl->file.cbgetattr.filesize ==
		rsp_attr.filesize))) {
		goto out;
	} else {
		/* One/more attributes changed */
		obj->state_hdl->file.cbgetattr.modified = true;
	}

	/* everytime we need to update change number to let
	 * other clients know that file contents could have
	 * been modified even if the filesize remains same
	 */
	obj->state_hdl->file.cbgetattr.change++;
	obj->state_hdl->file.cbgetattr.filesize = rsp_attr.filesize;

	event_func = (cbg_ctx->ctx_export->fsal_export->up_ops);

	init_root_op_context(&root_ctx, cbg_ctx->ctx_export,
			 cbg_ctx->ctx_export->fsal_export,
			 0, 0, UNKNOWN_REQUEST);

	/* @todo : log message if event_func is NULL */
	obj->obj_ops->handle_to_key(obj, &key);

	up_attr.valid_mask |= (ATTR_CHANGE | ATTR_SIZE | ATTR_MTIME
			      | ATTR_CTIME);
	up_attr.request_mask = up_attr.valid_mask;

	up_attr.filesize = obj->state_hdl->file.cbgetattr.filesize;
	up_attr.change = obj->state_hdl->file.cbgetattr.change;
	up_attr.mtime = timeo;
	up_attr.ctime = timeo;

	fsal_status = event_func->update(event_func, &key,
					 &up_attr, upflags);

	release_root_op_context();

	if (FSAL_IS_ERROR(fsal_status)) {
		cb_state = CB_GETATTR_FAILED;
		/* log message */
		return CB_GETATTR_FAILED;
	}
out:
	return cb_state;
}

/**
 * @brief Handle the reply to a CB_GETATTR
 *
 * @param[in] call  The RPC call being completed
 *
 */

static void cbgetattr_completion_func(rpc_call_t *call)
{
	struct cbgetattr_context *cbg_ctx = call->call_arg;
	enum cbgetattr_state *cbgetattr_state;
	CB_GETATTR4args *opcbgetattr;

	LogDebug(COMPONENT_NFS_CB, "%p %s", call,
		 !(call->states & NFS_CB_CALL_ABORTED) ? "Success" : "Failed");

	PTHREAD_RWLOCK_wrlock(&cbg_ctx->obj->state_hdl->state_lock);

	cbgetattr_state = &cbg_ctx->obj->state_hdl->file.cbgetattr.state;

	if (!(call->states & NFS_CB_CALL_ABORTED)) {
		LogMidDebug(COMPONENT_NFS_CB, "call result: %d",
			    call->call_req.cc_error.re_status);

		if (call->call_req.cc_error.re_status != RPC_SUCCESS) {
			LogEvent(COMPONENT_NFS_CB,
				 "CB_GETATTR call result: %d, marking CB channel down",
				 call->call_req.cc_error.re_status);
			set_cb_chan_down(cbg_ctx->clid, true);
			*cbgetattr_state = CB_GETATTR_FAILED;
			goto out;
		}

	} else {
		LogEvent(COMPONENT_NFS_CB,
			 "CB_GETATTR Aborted: %d, marking CB channel down",
			 call->call_req.cc_error.re_status);
		set_cb_chan_down(cbg_ctx->clid, true);
		/* Mark the getattr as failed */
		*cbgetattr_state = CB_GETATTR_FAILED;
		goto out;
	}
	switch (call->cbt.v_u.v4.res.status) {
	case NFS4_OK:
		LogDebug(COMPONENT_NFS_CB,
			 "CB_GEATTR succeded for client(%s)",
			 cbg_ctx->clid->gsh_client->hostaddr_str);
		*cbgetattr_state = handle_getattr_response(cbg_ctx,
							   call);
		break;
	default:
		*cbgetattr_state = CB_GETATTR_FAILED;
	}

out:
	PTHREAD_RWLOCK_unlock(&cbg_ctx->obj->state_hdl->state_lock);
	if (cbg_ctx->clid->cid_minorversion == 0) {
		opcbgetattr = &call->cbt.v_u.v4.args.argarray.argarray_val[0]
				.nfs_cb_argop4_u.opcbgetattr;
		nfs4_freeFH(&opcbgetattr->fh);
	} else {
		opcbgetattr = &call->cbt.v_u.v4.args.argarray.argarray_val[1]
				.nfs_cb_argop4_u.opcbgetattr;
		nfs4_freeFH(&opcbgetattr->fh);
		nfs41_release_single(call);
	}
	free_cbgetattr_context(cbg_ctx);
}

/**
 * @brief Send CB_GETATTR to write_delegated client.
 *
 * This function sends a CB_GETATTR, the caller has to lock
 * cache_entry->state_lock before calling this function.
 *
 * @param[in] obj The file holding delegation
 * @param[in] cbgetattr_context
 *
 * @return 0, success
 *	   otherwise, failure
 */
int send_cbgetattr(struct fsal_obj_handle *obj,
		   struct cbgetattr_context *p_cargs)
{
	int ret = 0;
	nfs_cb_argop4 argop;
	bitmap4	*cb_attr_req;
	struct root_op_context root_ctx;

	init_root_op_context(&root_ctx, p_cargs->ctx_export,
			     p_cargs->ctx_export->fsal_export,
			     0, 0, UNKNOWN_REQUEST);

	LogDebug(COMPONENT_FSAL_UP, "Sending CB_GETATTR to client %s",
		 p_cargs->clid->gsh_client->hostaddr_str);

	argop.argop = NFS4_OP_CB_GETATTR;

	/* Convert it to a file handle */
	if (!nfs4_FSALToFhandle(true, &argop.nfs_cb_argop4_u.opcbgetattr.fh,
				obj, p_cargs->ctx_export)) {
		LogCrit(COMPONENT_FSAL_UP,
			"nfs4_FSALToFhandle failed, can not process recall");
		goto out;
	}

	cb_attr_req = &argop.nfs_cb_argop4_u.opcbgetattr.attr_request;

	memset(cb_attr_req, 0, sizeof(*cb_attr_req));

	/*
	 * If we already know that client has modified data, no need to
	 * query for change attribute
	 */
	if (!obj->state_hdl->file.cbgetattr.modified)
		set_attribute_in_bitmap(cb_attr_req, FATTR4_CHANGE);

	set_attribute_in_bitmap(cb_attr_req, FATTR4_SIZE);

	ret = nfs_rpc_cb_single(p_cargs->clid, &argop, NULL,
				cbgetattr_completion_func, p_cargs);
	if (ret == 0)
		return 0;
	LogDebug(COMPONENT_FSAL_UP, "CB_GETATTR nfs_rpc_cb_single returned %d",
		 ret);

out:
	nfs4_freeFH(&argop.nfs_cb_argop4_u.opcbgetattr.fh);

	LogCrit(COMPONENT_STATE, "CB_GETATTR failed for %s",
		p_cargs->clid->gsh_client->hostaddr_str);

	free_cbgetattr_context(p_cargs);
	release_root_op_context();
	return ret;
}

/**
 * @brief Implement CB_GETATTR op
 *
 * By making use of cbgetattr_context, prepare and
 * send CB_GETATTR request to the client. On success,
 * set cbgetattr_state to CB_GETATTR_WIP state,
 * otherwise mark it as CB_GETATTR_FAILED.
 *
 * @param[in] obj file being operated on
 * @param[in] client client holding delegation
 * @param[in] ctx_exp current export reference
 *
 * @return: 0, success
 *	    otherwise, failure
 */
int cbgetattr_impl(struct fsal_obj_handle *obj,
		   nfs_client_id_t *client,
		   struct gsh_export *ctx_exp)
{
	int rc = 0;
	enum  cbgetattr_state *cb_state = NULL;
	struct cbgetattr_context *cbg_ctx;

	LogDebug(COMPONENT_FSAL_UP, "CB_GETATTR: obj %p type %u",
		 obj, obj->type);

	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);

	cb_state = &obj->state_hdl->file.cbgetattr.state;

	if (*cb_state != CB_GETATTR_NONE) {
		/* Either its already WIP or got error */
		goto out;
	}
	*cb_state = CB_GETATTR_WIP;

	cbg_ctx = gsh_malloc(sizeof(struct cbgetattr_context));

	obj->obj_ops->get_ref(obj);
	cbg_ctx->obj = obj;

	inc_client_id_ref(client);
	cbg_ctx->clid = client;
	/* Prevent client's lease expiring until we complete
	 * this cb_getattr operation. If the client's lease
	 * has already expired, let the reaper thread handling
	 * expired clients revoke this delegation, and we just
	 * skip it here.
	 */
	PTHREAD_MUTEX_lock(&cbg_ctx->clid->cid_mutex);
	if (!reserve_lease(cbg_ctx->clid)) {
		gsh_free(cbg_ctx);
		*cb_state = CB_GETATTR_FAILED;
		PTHREAD_MUTEX_unlock(&cbg_ctx->clid->cid_mutex);
		goto out;
	}
	PTHREAD_MUTEX_unlock(&cbg_ctx->clid->cid_mutex);
	get_gsh_export_ref(ctx_exp);
	cbg_ctx->ctx_export = ctx_exp;

	rc = send_cbgetattr(obj, cbg_ctx);
out:
	if (rc)
		*cb_state = CB_GETATTR_FAILED;

	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

	return rc;
}

void up_ready_init(struct fsal_up_vector *up_ops)
{
	up_ops->up_ready = false;
	up_ops->up_cancel = false;
	PTHREAD_MUTEX_init(&up_ops->up_mutex, NULL);
	PTHREAD_COND_init(&up_ops->up_cond, NULL);
}

/* This should be called when a module is ready to take upcalls */
void up_ready_set(struct fsal_up_vector *up_ops)
{
	PTHREAD_MUTEX_lock(&up_ops->up_mutex);
	up_ops->up_ready = true;
	pthread_cond_broadcast(&up_ops->up_cond);
	PTHREAD_MUTEX_unlock(&up_ops->up_mutex);
}

/* This is to cancel waiting and resume Upcall threads */
void up_ready_cancel(struct fsal_up_vector *up_ops)
{
	PTHREAD_MUTEX_lock(&up_ops->up_mutex);
	up_ops->up_cancel = true;
	pthread_cond_broadcast(&up_ops->up_cond);
	PTHREAD_MUTEX_unlock(&up_ops->up_mutex);
}

/* Upcall threads should call this to wait for upcall readiness */
void up_ready_wait(struct fsal_up_vector *up_ops)
{
	PTHREAD_MUTEX_lock(&up_ops->up_mutex);
	while (!up_ops->up_ready && !up_ops->up_cancel)
		pthread_cond_wait(&up_ops->up_cond, &up_ops->up_mutex);
	PTHREAD_MUTEX_unlock(&up_ops->up_mutex);
}

/**
 * @brief The top level vector of operations
 *
 * This is the basis for UP calls.  It should not be used directly, but copied
 * into a per-export structure, overridden if necessary, and fsal_export set.
 * FSAL_MDCACHE does this, so any cached FSALs don't need to worry.
 */

struct fsal_up_vector fsal_up_top = {
	.up_gsh_export = NULL,
	.up_fsal_export = NULL,
	.lock_grant = lock_grant,
	.lock_avail = lock_avail,
	.invalidate = invalidate,
	.update = update,
	.layoutrecall = layoutrecall,
	.notify_device = notify_device,
	.delegrecall = delegrecall,
	.invalidate_close = invalidate_close
};

/** @} */
