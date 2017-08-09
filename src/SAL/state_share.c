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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file state_share.c
 * @brief Share reservation management
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "fsal.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
/*#include "nlm_util.h"*/
#include "export_mgr.h"

static void state_share_update_counter(struct state_hdl *hstate,
				       int old_access, int old_deny,
				       int new_access, int new_deny, bool v4);

/**
 * @brief Update the previously access and deny modes
 *
 * @param[in] state      State to update
 * @param[in] state_data Previous modes to add
 */
state_status_t state_share_set_prev(state_t *state,
				    union state_data *state_data)
{
	state_status_t status = STATE_SUCCESS;

	state->state_data.share.share_access_prev |=
	    (1 << state_data->share.share_access);

	state->state_data.share.share_deny_prev |=
	    (1 << state_data->share.share_deny);

	return status;
}

/**
 * @brief Check for share conflict
 *
 * The state lock _must_ be held for this call.
 *
 * @param[in] hstate       File state to query
 * @param[in] share_access Desired access mode
 * @param[in] share_deny   Desired deny mode
 * @param[in] bypass       Indicates if any bypass is to be used
 *
 * @return State status.
 */
state_status_t state_share_check_conflict(struct state_hdl *hstate,
					  int share_access,
					  int share_deny,
					  enum share_bypass_modes bypass)
{
	char *cause = "";

	if ((share_access & OPEN4_SHARE_ACCESS_READ) != 0
	    && hstate->file.share_state.share_deny_read > 0
	    && bypass != SHARE_BYPASS_READ) {
		cause = "access read denied by existing deny read";
		goto out_conflict;
	}

	if ((share_access & OPEN4_SHARE_ACCESS_WRITE) != 0
	    && (hstate->file.share_state.share_deny_write_v4 > 0 ||
		(bypass != SHARE_BYPASS_V3_WRITE &&
		 hstate->file.share_state.share_deny_write > 0))) {
		cause = "access write denied by existing deny write";
		goto out_conflict;
	}

	if ((share_deny & OPEN4_SHARE_DENY_READ) != 0
	    && hstate->file.share_state.share_access_read > 0) {
		cause = "deny read denied by existing access read";
		goto out_conflict;
	}

	if ((share_deny & OPEN4_SHARE_DENY_WRITE) != 0
	    && hstate->file.share_state.share_access_write > 0) {
		cause = "deny write denied by existing access write";
		goto out_conflict;
	}

	return STATE_SUCCESS;

 out_conflict:

	LogDebug(COMPONENT_STATE, "Share conflict detected: %s", cause);
	return STATE_SHARE_DENIED;
}

/**
 * @brief Update the ref counter of share state
 *
 * This function should be called with the state lock held
 *
 * @param[in] hstate     File state to update
 * @param[in] old_access Previous access mode
 * @param[in] old_deny   Previous deny mode
 * @param[in] new_access Current access mode
 * @param[in] new_deny   Current deny mode
 * @param[in] v4         True if this is a v4 share/open
 */
static void state_share_update_counter(struct state_hdl *hstate, int
				       old_access, int old_deny, int new_access,
				       int new_deny, bool v4)
{
	int access_read_inc =
	    ((new_access & OPEN4_SHARE_ACCESS_READ) !=
	     0) - ((old_access & OPEN4_SHARE_ACCESS_READ) != 0);
	int access_write_inc =
	    ((new_access & OPEN4_SHARE_ACCESS_WRITE) !=
	     0) - ((old_access & OPEN4_SHARE_ACCESS_WRITE) != 0);
	int deny_read_inc =
	    ((new_deny & OPEN4_SHARE_DENY_READ) !=
	     0) - ((old_deny & OPEN4_SHARE_DENY_READ) != 0);
	int deny_write_inc =
	    ((new_deny & OPEN4_SHARE_DENY_WRITE) !=
	     0) - ((old_deny & OPEN4_SHARE_DENY_WRITE) != 0);

	hstate->file.share_state.share_access_read += access_read_inc;
	hstate->file.share_state.share_access_write += access_write_inc;
	hstate->file.share_state.share_deny_read += deny_read_inc;
	hstate->file.share_state.share_deny_write += deny_write_inc;
	if (v4)
		hstate->file.share_state.share_deny_write_v4 += deny_write_inc;

	LogFullDebug(COMPONENT_STATE,
		     "obj %p: share counter: access_read %u, access_write %u, deny_read %u, deny_write %u, deny_write_v4 %u",
		     hstate->file.obj,
		     hstate->file.share_state.share_access_read,
		     hstate->file.share_state.share_access_write,
		     hstate->file.share_state.share_deny_read,
		     hstate->file.share_state.share_deny_write,
		     hstate->file.share_state.share_deny_write_v4);
}

/**
 * @brief Start I/O by an anonymous stateid
 *
 * This function checks for conflicts with existing deny modes and
 * marks the I/O as in process to conflicting shares won't be granted.
 *
 * @brief[in]     obj          File on which to operate
 * @brief[in]     share_access Access matching I/O done
 * @brief[in]     bypass       Indicates if any bypass is to be used
 *
 * @return State status.
 */
state_status_t state_share_anonymous_io_start(struct fsal_obj_handle *obj,
					      int share_access,
					      enum share_bypass_modes bypass)
{
	/** @todo FSF: This is currently unused, but I think there is
	 *             some additional work to make the conflict check
	 *             work for v3 and v4, and in fact, this function
	 *             should be called indicating v3 or v4...
	 */
	state_status_t status = 0;

	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);

	status = state_share_check_conflict(obj->state_hdl,
					    share_access,
					    OPEN4_SHARE_DENY_NONE,
					    bypass);
	if (status != STATE_SUCCESS) {
		/* Need to convert the error from STATE_SHARE_CONFLICT */
		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
		status = STATE_LOCKED;
		return status;
	}

	if (state_deleg_conflict(obj,
				 share_access & OPEN4_SHARE_ACCESS_WRITE)) {
		/* Delegations are being recalled. Delay client until that
		 * process finishes. */
		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
		return STATE_FSAL_DELAY;
	}

	/* update a counter that says we are processing an anonymous
	 * request and can't currently grant a new delegation */
	(void) atomic_inc_uint32_t(&obj->state_hdl->file.anon_ops);

	/* Temporarily bump the access counters, v4 mode doesn't matter
	 * since there is no deny mode associated with anonymous I/O.
	 */
	state_share_update_counter(obj->state_hdl, OPEN4_SHARE_ACCESS_NONE,
				   OPEN4_SHARE_DENY_NONE, share_access,
				   OPEN4_SHARE_DENY_NONE, false);

	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
	return status;
}

/**
 * @brief Finish an anonymous I/O
 *
 * @param[in,out] obj          File on which to operate
 * @param[in]     share_access Access bits indicating I/O type
 */
void state_share_anonymous_io_done(struct fsal_obj_handle *obj,
				   int share_access)
{
	/* Undo the temporary bump to the access counters, v4 mode doesn't
	 * matter since there is no deny mode associated with anonymous I/O.
	 */
	state_share_update_counter(obj->state_hdl, share_access,
				   OPEN4_SHARE_DENY_NONE,
				   OPEN4_SHARE_ACCESS_NONE,
				   OPEN4_SHARE_DENY_NONE, false);

	/* If we are this far, then delegations weren't recalled and we
	 * incremented this variable. */
	(void) atomic_dec_uint32_t(&obj->state_hdl->file.anon_ops);
}

#ifdef _USE_NLM
/**
 * @brief Remove an NLM share
 *
 * @param[in]     state	The state_t describing the share to remove
 *
 */
void remove_nlm_share(state_t *state)
{
	state_owner_t *owner = state->state_owner;
	state_nlm_client_t *client = owner->so_owner.so_nlm_owner.so_client;

	/* Remove from share list for export */
	PTHREAD_RWLOCK_wrlock(&op_ctx->ctx_export->lock);
	glist_del(&state->state_export_list);
	PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

	/* Remove the share from the list for the file. */
	glist_del(&state->state_list);

	/* Remove the share from the NSM Client list */
	PTHREAD_MUTEX_lock(&client->slc_nsm_client->ssc_mutex);

	glist_del(&state->state_data.nlm_share.share_perclient);

	PTHREAD_MUTEX_unlock(&client->slc_nsm_client->ssc_mutex);

	dec_nsm_client_ref(client->slc_nsm_client);

	/* Remove the share from the NLM Owner list */
	PTHREAD_MUTEX_lock(&owner->so_mutex);

	glist_del(&state->state_owner_list);

	PTHREAD_MUTEX_unlock(&owner->so_mutex);

	/* Release the state_t reference for active share. If extended FSAL
	 * operations are supported, this will close the file when the last
	 * reference is released.
	 */
	dec_state_t_ref(state);
}

/**
 * @brief Implement NLM share call with FSAL extended ops
 *
 * @param[in,out] obj          File on which to operate
 * @param[in]     export       Export through which file is accessed
 * @param[in]     share_access Share mode requested
 * @param[in]     share_deny   Deny mode requested
 * @param[in]     owner        Share owner
 * @param[in]     state        state_t to manage the share
 * @param[in]     reclaim      Indicates if this is a reclaim
 * @param[in]     unshare      Indicates if this was an unshare
 *
 * @return State status.
 */
state_status_t state_nlm_share(struct fsal_obj_handle *obj,
			       int share_access,
			       int share_deny,
			       state_owner_t *owner,
			       state_t *state,
			       bool reclaim,
			       bool unshare)
{
	fsal_status_t fsal_status = {0, 0};
	fsal_openflags_t openflags = 0;
	state_nlm_client_t *client = owner->so_owner.so_nlm_owner.so_client;

	if (unshare) {
		unsigned int old_access;
		unsigned int old_deny;

		old_access = state->state_data.nlm_share.share_access;
		old_deny = state->state_data.nlm_share.share_deny;

		/* Remove share_access from old_access */
		share_access = old_access - (old_access & share_access);

		/* Remove share_deny from old_deny */
		share_deny = old_deny - (old_deny & share_deny);
	}

	/* Assume new access/deny is update in determining the openflags. */
	if ((share_access & fsa_R) != 0)
		openflags |= FSAL_O_READ;

	if ((share_access & fsa_W) != 0)
		openflags |= FSAL_O_WRITE;

	if (openflags == FSAL_O_CLOSED) {
		/* This share or unshare is removing the final share. The file
		 * will be closed when the final reference to the state is
		 * released.
		 */
		remove_nlm_share(state);
		goto out_unlock;
	}

	if ((share_deny & fsm_DR) != 0)
		openflags |= FSAL_O_DENY_READ;

	if ((share_deny & fsm_DW) != 0)
		openflags |= FSAL_O_DENY_WRITE;

	if (reclaim)
		openflags |= FSAL_O_RECLAIM;

	/* Use reopen2 to open or re-open the file and check for share
	 * conflict.
	 */
	fsal_status = fsal_reopen2(obj, state, openflags, true);

	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_STATE,
			 "fsal_reopen2 failed with %s",
			 fsal_err_txt(fsal_status));
		goto out_unlock;
	}

	/* Add share to list for NLM Owner */
	PTHREAD_MUTEX_lock(&owner->so_mutex);

	glist_add_tail(&owner->so_owner.so_nlm_owner.so_nlm_shares,
		       &state->state_owner_list);

	PTHREAD_MUTEX_unlock(&owner->so_mutex);

	/* Add share to list for NSM Client */
	inc_nsm_client_ref(client->slc_nsm_client);

	PTHREAD_MUTEX_lock(&client->slc_nsm_client->ssc_mutex);

	glist_add_tail(&client->slc_nsm_client->ssc_share_list,
		       &state->state_data.nlm_share.share_perclient);

	PTHREAD_MUTEX_unlock(&client->slc_nsm_client->ssc_mutex);

	/* Add share to list for file. */
	glist_add_tail(&obj->state_hdl->file.nlm_share_list,
		       &state->state_list);

	/* Add to share list for export */
	PTHREAD_RWLOCK_wrlock(&op_ctx->ctx_export->lock);
	glist_add_tail(&op_ctx->ctx_export->exp_nlm_share_list,
		       &state->state_export_list);
	PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

	/* If we had never had a share, take a reference on the state_t
	 * to retain it.
	 */
	if (state->state_data.nlm_share.share_access != OPEN4_SHARE_ACCESS_NONE)
		inc_state_t_ref(state);

	/* Update the current share type */
	state->state_data.nlm_share.share_access = share_access;
	state->state_data.nlm_share.share_deny = share_deny;

	LogFullDebug(COMPONENT_STATE, "added share_access %u, share_deny %u",
		     share_access, share_deny);

 out_unlock:

	return state_error_convert(fsal_status);
}

/**
 * @brief Remove all share state from a file
 *
 * @param[in] obj File to wipe
 */
void state_share_wipe(struct state_hdl *hstate)
{
	state_t *state;
	struct glist_head *glist;
	struct glist_head *glistn;

	glist_for_each_safe(glist, glistn, &hstate->file.nlm_share_list) {
		state = glist_entry(glist, state_t, state_list);

		remove_nlm_share(state);
	}
}

void state_export_unshare_all(void)
{
	int errcnt = 0;
	state_t *state;
	state_owner_t *owner;
	struct fsal_obj_handle *obj;
	state_status_t status;

	while (errcnt < STATE_ERR_MAX) {
		PTHREAD_RWLOCK_wrlock(&op_ctx->ctx_export->lock);

		state = glist_first_entry(&op_ctx->ctx_export->exp_nlm_share_list,
					  state_t,
					  state_export_list);

		if (state == NULL) {
			PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);
			break;
		}

		obj = get_state_obj_ref(state);

		if (obj == NULL) {
			LogDebug(COMPONENT_STATE,
				 "Entry for state is stale");
			PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);
			break;
		}

		owner = state->state_owner;

		/* Get a reference to the state_t */
		inc_state_t_ref(state);

		/* get a reference to the owner */
		inc_state_owner_ref(owner);

		/* Drop the export mutex to call unshare */
		PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

		/* Remove all shares held by this Owner on this export */
		status = state_nlm_share(obj,
					 OPEN4_SHARE_ACCESS_BOTH,
					 OPEN4_SHARE_DENY_BOTH,
					 owner,
					 state,
					 false,
					 true);

		/* Release references taken above. Should free the state_t. */
		dec_state_owner_ref(owner);
		obj->obj_ops.put_ref(obj);
		dec_state_t_ref(state);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next share,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogCrit(COMPONENT_STATE,
				"state_unlock failed %s",
				state_err_str(status));
			errcnt++;
		}
	}

	if (errcnt == STATE_ERR_MAX) {
		LogFatal(COMPONENT_STATE,
			 "Could not complete cleanup of NLM shares for %s",
			 export_path(op_ctx->ctx_export));
	}
}
#endif /* _USE_NLM */

/** @} */
