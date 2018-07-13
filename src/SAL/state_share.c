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
	unsigned int old_access;
	unsigned int old_deny;
	unsigned int new_access = 0;
	unsigned int new_deny = 0;
	struct state_nlm_share *nlm_share = &state->state_data.nlm_share;
	int i, acount = 0, dcount = 0;

	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);

	old_access = nlm_share->share_access;
	old_deny = nlm_share->share_deny;

	LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
			"%s access %d, deny %d",
			unshare ? "UNSHARE" : "SHARE",
			share_access, share_deny);

	if (unshare) {
		if (nlm_share->share_access_counts[share_access] > 0)
			nlm_share->share_access_counts[share_access]--;
		else
			LogDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
				    "UNSHARE access %d did not match",
				    share_access);

		if (nlm_share->share_deny_counts[share_deny] > 0)
			nlm_share->share_deny_counts[share_deny]--;
		else
			LogDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
				    "UNSHARE deny %d did not match",
				    share_access);
	} else {
		nlm_share->share_access_counts[share_access]++;
		nlm_share->share_deny_counts[share_deny]++;
	}

	/* Compute new share_access as union of all remaining shares. */
	for (i = 0; i <= fsa_RW; i++) {
		if (nlm_share->share_access_counts[i] != 0) {
			new_access |= i;
			acount += nlm_share->share_access_counts[i];
		}
	}

	/* Compute new share_deny as union of all remaining shares. */
	for (i = 0; i <= fsm_DRW; i++) {
		if (nlm_share->share_deny_counts[i] != 0) {
			new_deny |= i;
			dcount += nlm_share->share_deny_counts[i];
		}
	}

	LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
			"%s share_access_counts[%d] = %d, total = %d, share_deny_counts[%d] = %d, total = %d",
			unshare ? "UNSHARE" : "SHARE",
			share_access,
			nlm_share->share_access_counts[share_access],
			acount,
			share_deny,
			nlm_share->share_deny_counts[share_deny],
			dcount);

	if (new_access == old_access && new_deny == old_deny) {
		/* The share or unshare did not affect the union of shares so
		 * there is no more work to do.
		 */
		LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
				"%s union share did not change from access %d, deny %d",
				unshare ? "UNSHARE" : "SHARE",
				old_access, old_deny);
		goto out_unlock;
	}

	/* Assume new access/deny is update in determining the openflags. */
	if ((new_access & fsa_R) != 0)
		openflags |= FSAL_O_READ;

	if ((new_access & fsa_W) != 0)
		openflags |= FSAL_O_WRITE;

	if (openflags == FSAL_O_CLOSED && unshare) {
		/* This unshare is removing the final share. The file will be
		 * closed when the final reference to the state is released.
		 */
		LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
				"UNSHARE removed state_t %p, share_access %u, share_deny %u",
				state, old_access, old_deny);

		remove_nlm_share(state);
		goto out_unlock;
	}

	if (openflags == FSAL_O_CLOSED) {
		LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
				"SHARE with access none, deny %d and file is not already open, modify to read",
				share_deny);
		openflags |= FSAL_O_READ;
	}

	if ((new_deny & fsm_DR) != 0)
		openflags |= FSAL_O_DENY_READ;

	if ((new_deny & fsm_DW) != 0)
		openflags |= FSAL_O_DENY_WRITE;

	if (reclaim)
		openflags |= FSAL_O_RECLAIM;

	/* Use reopen2 to open or re-open the file and check for share
	 * conflict.
	 */
	fsal_status = fsal_reopen2(obj, state, openflags, true);

	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
			    "fsal_reopen2 failed with %s",
			    fsal_err_txt(fsal_status));
		goto out_unlock;
	} else {
		LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
				"fsal_reopen2 succeeded");
	}

	/* If we already had a share, skip all the book keeping. */
	if (old_access != OPEN4_SHARE_ACCESS_NONE) {
		LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
				"%s updated state_t %p, share_access %u, share_deny %u",
				unshare ? "UNSHARE" : "SHARE",
				state, new_access, new_deny);
		goto update;
	}

	/* Take a reference on the state_t. */
	inc_state_t_ref(state);

	/* Add share to list for NLM Owner */
	PTHREAD_MUTEX_lock(&owner->so_mutex);

	glist_add_tail(&owner->so_owner.so_nlm_owner.so_nlm_shares,
		       &state->state_owner_list);

	PTHREAD_MUTEX_unlock(&owner->so_mutex);

	/* Add share to list for NSM Client */
	inc_nsm_client_ref(client->slc_nsm_client);

	PTHREAD_MUTEX_lock(&client->slc_nsm_client->ssc_mutex);

	glist_add_tail(&client->slc_nsm_client->ssc_share_list,
		       &nlm_share->share_perclient);

	PTHREAD_MUTEX_unlock(&client->slc_nsm_client->ssc_mutex);

	/* Add share to list for file. */
	glist_add_tail(&obj->state_hdl->file.nlm_share_list,
		       &state->state_list);

	/* Add to share list for export */
	PTHREAD_RWLOCK_wrlock(&op_ctx->ctx_export->lock);
	glist_add_tail(&op_ctx->ctx_export->exp_nlm_share_list,
		       &state->state_export_list);
	PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

	LogFullDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
			"SHARE added state_t %p, share_access %u, share_deny %u",
			state, new_access, new_deny);

 update:

	/* Update the current share type */
	nlm_share->share_access = new_access;
	nlm_share->share_deny = new_deny;

 out_unlock:

	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

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

		state = glist_first_entry(
				&op_ctx->ctx_export->exp_nlm_share_list,
				state_t,
				state_export_list);

		if (state == NULL) {
			PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);
			break;
		}

		obj = get_state_obj_ref(state);

		if (obj == NULL) {
			LogDebugAlt(COMPONENT_STATE, COMPONENT_NLM,
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
		obj->obj_ops->put_ref(obj);
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
