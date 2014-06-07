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
#include "nlm_util.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"

static void state_share_update_counter(cache_entry_t *entry, int old_access,
				       int old_deny, int new_access,
				       int new_deny, bool v4);

static unsigned int state_share_get_share_access(cache_entry_t *entry);

static unsigned int state_share_get_share_deny(cache_entry_t *entry);

/**
 * @brief Push share state down to FSAL
 *
 * Only the union of share states should be passed to this function.
 *
 * @param[in] entry File to access
 * @param[in] owner Open owner
 * @param[in] share Share description
 *
 * @return State status.
 */
static state_status_t do_share_op(cache_entry_t *entry,
				  state_owner_t *owner,
				  fsal_share_param_t *share)
{
	fsal_status_t fsal_status;
	state_status_t status = STATE_SUCCESS;

	/* Quick exit if share reservation is not supported by FSAL */
	if (!op_ctx->fsal_export->ops->
	    fs_supports(op_ctx->fsal_export, fso_share_support))
		return STATE_SUCCESS;

	fsal_status = entry->obj_handle->ops->share_op(entry->obj_handle,
						       NULL,
						       *share);

	if (fsal_status.major == ERR_FSAL_STALE)
		cache_inode_kill_entry(entry);

	status = state_error_convert(fsal_status);

	LogFullDebug(COMPONENT_STATE, "FSAL_share_op returned %s",
		     state_err_str(status));

	return status;
}

/**
 * @brief Add new share state
 *
 * The state lock _must_ be held for this call.
 *
 * @param[in,out] entry File on which to operate
 * @param[in]     owner Open owner
 * @param[in]     state State that holds the share bits to be added
 *
 * @return State status.
 */
state_status_t state_share_add(cache_entry_t *entry,
			       state_owner_t *owner,
			       state_t *state, bool reclaim)
{
	state_status_t status = STATE_SUCCESS;
	unsigned int old_entry_share_access = 0;
	unsigned int old_entry_share_deny = 0;
	unsigned int new_entry_share_access = 0;
	unsigned int new_entry_share_deny = 0;
	unsigned int new_share_access = 0;
	unsigned int new_share_deny = 0;
	fsal_share_param_t share_param;

	/* Check if new share state has conflicts. */
	status =
	    state_share_check_conflict(entry,
				       state->state_data.share.share_access,
				       state->state_data.share.share_deny);
	if (status != STATE_SUCCESS) {
		LogEvent(COMPONENT_STATE,
			 "Share conflicts detected during add");
		status = STATE_STATE_CONFLICT;
		return status;
	}

	/* Get the current union of share states of this file. */
	old_entry_share_access = state_share_get_share_access(entry);
	old_entry_share_deny = state_share_get_share_deny(entry);

	/* Share state to be added. */
	new_share_access = state->state_data.share.share_access;
	new_share_deny = state->state_data.share.share_deny;

	/* Update the ref counted share state of this file. */
	state_share_update_counter(entry, OPEN4_SHARE_ACCESS_NONE,
				   OPEN4_SHARE_DENY_NONE, new_share_access,
				   new_share_deny, true);

	/* Get the updated union of share states of this file. */
	new_entry_share_access = state_share_get_share_access(entry);
	new_entry_share_deny = state_share_get_share_deny(entry);

	/* If this file's share bits are different from the supposed value,
	 * update it.
	 */
	if ((new_entry_share_access != old_entry_share_access)
	    || (new_entry_share_deny != old_entry_share_deny)) {
		/* Try to push to FSAL. */
		share_param.share_access = new_entry_share_access;
		share_param.share_deny = new_entry_share_deny;
		share_param.share_reclaim = reclaim;

		status = do_share_op(entry, owner, &share_param);

		if (status != STATE_SUCCESS) {
			/* Revert the ref counted share state of this file. */
			state_share_update_counter(entry, new_share_access,
						   new_share_deny,
						   OPEN4_SHARE_ACCESS_NONE,
						   OPEN4_SHARE_DENY_NONE, true);
			LogDebug(COMPONENT_STATE, "do_share_op failed");
			return status;
		}
	}

	LogFullDebug(COMPONENT_STATE,
		     "state %p: added share_access %u, " "share_deny %u", state,
		     new_share_access, new_share_deny);

	/* Update previously seen share state in the bitmap. */
	state_share_set_prev(state, &(state->state_data));

	return status;
}

/**
 * Remove a share state
 *
 * The state lock _must_ be held for this call.
 *
 * @param[in,out] entry File to modify
 * @param[in]     owner Open owner
 * @param[in]     state State that holds the share bits to be removed
 *
 * @return State status.
 */
state_status_t state_share_remove(cache_entry_t *entry,
				  state_owner_t *owner,
				  state_t *state)
{
	state_status_t status = STATE_SUCCESS;
	unsigned int old_entry_share_access = 0;
	unsigned int old_entry_share_deny = 0;
	unsigned int new_entry_share_access = 0;
	unsigned int new_entry_share_deny = 0;
	unsigned int removed_share_access = 0;
	unsigned int removed_share_deny = 0;
	fsal_share_param_t share_param;

	/* Get the current union of share states of this file. */
	old_entry_share_access = state_share_get_share_access(entry);
	old_entry_share_deny = state_share_get_share_deny(entry);

	/* Share state to be removed. */
	removed_share_access = state->state_data.share.share_access;
	removed_share_deny = state->state_data.share.share_deny;

	/* Update the ref counted share state of this file. */
	state_share_update_counter(entry, removed_share_access,
				   removed_share_deny, OPEN4_SHARE_ACCESS_NONE,
				   OPEN4_SHARE_DENY_NONE, true);

	/* Get the updated union of share states of this file. */
	new_entry_share_access = state_share_get_share_access(entry);
	new_entry_share_deny = state_share_get_share_deny(entry);

	/* If this file's share bits are different from the supposed value,
	 * update it.
	 */
	if ((new_entry_share_access != old_entry_share_access)
	    || (new_entry_share_deny != old_entry_share_deny)) {
		/* Try to push to FSAL. */
		share_param.share_access = new_entry_share_access;
		share_param.share_deny = new_entry_share_deny;
		share_param.share_reclaim = false;

		status = do_share_op(entry, owner, &share_param);

		if (status != STATE_SUCCESS) {
			/* Revert the ref counted share state of this file. */
			state_share_update_counter(entry,
						   OPEN4_SHARE_ACCESS_NONE,
						   OPEN4_SHARE_DENY_NONE,
						   removed_share_access,
						   removed_share_deny, true);
			LogDebug(COMPONENT_STATE, "do_share_op failed");
			return status;
		}
	}

	/* state has been removed, so adjust open flags */
	cache_inode_adjust_openflags(entry);

	LogFullDebug(COMPONENT_STATE,
		     "state %p: removed share_access %u, " "share_deny %u",
		     state, removed_share_access, removed_share_deny);

	return status;
}

/**
 * @brief Upgrade share modes
 *
 * The state lock _must_ be held for this call.
 *
 * @param[in,out] entry      File to modify
 * @param[in]     state_data New share bits
 * @param[in]     owner      Open owner
 * @param[in,out] state      State that holds current share bits
 *
 * @return State status.
 */
state_status_t state_share_upgrade(cache_entry_t *entry,
				   state_data_t *state_data,
				   state_owner_t *owner, state_t *state,
				   bool reclaim)
{
	state_status_t status = STATE_SUCCESS;
	unsigned int old_entry_share_access = 0;
	unsigned int old_entry_share_deny = 0;
	unsigned int new_entry_share_access = 0;
	unsigned int new_entry_share_deny = 0;
	unsigned int old_share_access = 0;
	unsigned int old_share_deny = 0;
	unsigned int new_share_access = 0;
	unsigned int new_share_deny = 0;
	fsal_share_param_t share_param;

	/* Check if new share state has conflicts. */
	status =
	    state_share_check_conflict(entry, state_data->share.share_access,
				       state_data->share.share_deny);
	if (status != STATE_SUCCESS) {
		LogEvent(COMPONENT_STATE,
			 "Share conflicts detected during upgrade");
		status = STATE_STATE_CONFLICT;
		return status;
	}

	/* Get the current union of share states of this file. */
	old_entry_share_access = state_share_get_share_access(entry);
	old_entry_share_deny = state_share_get_share_deny(entry);

	/* Old share state. */
	old_share_access = state->state_data.share.share_access;
	old_share_deny = state->state_data.share.share_deny;

	/* New share state. */
	new_share_access = state_data->share.share_access | old_share_access;
	new_share_deny = state_data->share.share_deny | old_share_deny;

	/* Update the ref counted share state of this file. */
	state_share_update_counter(entry, old_share_access, old_share_deny,
				   new_share_access, new_share_deny, true);

	/* Get the updated union of share states of this file. */
	new_entry_share_access = state_share_get_share_access(entry);
	new_entry_share_deny = state_share_get_share_deny(entry);

	/* If this file's share bits are different from the supposed value,
	 * update it.
	 */
	if ((new_entry_share_access != old_entry_share_access)
	    || (new_entry_share_deny != old_entry_share_deny)) {
		/* Try to push to FSAL. */
		share_param.share_access = new_entry_share_access;
		share_param.share_deny = new_entry_share_deny;
		share_param.share_reclaim = reclaim;

		status = do_share_op(entry, owner, &share_param);

		if (status != STATE_SUCCESS) {
			/* Revert the ref counted share state of this file. */
			state_share_update_counter(entry, new_share_access,
						   new_share_deny,
						   old_share_access,
						   old_share_deny, true);
			LogDebug(COMPONENT_STATE, "do_share_op failed");
			return status;
		}
	}

	/* Update share state. */
	state->state_data.share.share_access = new_share_access;
	state->state_data.share.share_deny = new_share_deny;
	LogFullDebug(COMPONENT_STATE,
		     "state %p: upgraded share_access %u, share_deny %u", state,
		     state->state_data.share.share_access,
		     state->state_data.share.share_deny);

	/* Update previously seen share state. */
	state_share_set_prev(state, state_data);

	return status;
}

/**
 * @brief Downgrade share mode
 *
 * The state lock _must_ be held for this call.
 *
 * @param[in,out] entry      File to modify
 * @param[in]     state_data New share bits
 * @param[in]     owner      Open owner
 * @param[in]     state      State that holds current share bits
 *
 * @return State status.
 */
state_status_t state_share_downgrade(cache_entry_t *entry,
				     state_data_t *state_data,
				     state_owner_t *owner, state_t *state)
{
	state_status_t status = STATE_SUCCESS;
	unsigned int old_entry_share_access = 0;
	unsigned int old_entry_share_deny = 0;
	unsigned int new_entry_share_access = 0;
	unsigned int new_entry_share_deny = 0;
	unsigned int old_share_access = 0;
	unsigned int old_share_deny = 0;
	unsigned int new_share_access = 0;
	unsigned int new_share_deny = 0;
	fsal_share_param_t share_param;

	/* Get the current union of share states of this file. */
	old_entry_share_access = state_share_get_share_access(entry);
	old_entry_share_deny = state_share_get_share_deny(entry);

	/* Old share state. */
	old_share_access = state->state_data.share.share_access;
	old_share_deny = state->state_data.share.share_deny;

	/* New share state. */
	new_share_access = state_data->share.share_access;
	new_share_deny = state_data->share.share_deny;

	/* Update the ref counted share state of this file. */
	state_share_update_counter(entry, old_share_access, old_share_deny,
				   new_share_access, new_share_deny, true);

	/* Get the updated union of share states of this file. */
	new_entry_share_access = state_share_get_share_access(entry);
	new_entry_share_deny = state_share_get_share_deny(entry);

	/* If this file's share bits are different from the supposed value,
	 * update it.
	 */
	if ((new_entry_share_access != old_entry_share_access)
	    || (new_entry_share_deny != old_entry_share_deny)) {
		/* Try to push to FSAL. */
		share_param.share_access = new_entry_share_access;
		share_param.share_deny = new_entry_share_deny;
		share_param.share_reclaim = false;

		status = do_share_op(entry, owner, &share_param);

		if (status != STATE_SUCCESS) {
			/* Revert the ref counted share state of this file. */
			state_share_update_counter(entry, new_share_access,
						   new_share_deny,
						   old_share_access,
						   old_share_deny, true);
			LogDebug(COMPONENT_STATE, "do_share_op failed");
			return status;
		}
	}

	/* Update share state. */
	state->state_data.share.share_access = new_share_access;
	state->state_data.share.share_deny = new_share_deny;

	/* state is downgraded, so adjust open flags */
	cache_inode_adjust_openflags(entry);

	LogFullDebug(COMPONENT_STATE,
		     "state %p: downgraded share_access %u, " "share_deny %u",
		     state, state->state_data.share.share_access,
		     state->state_data.share.share_deny);

	return status;
}

/**
 * @brief Update the previously access and deny modes
 *
 * @param[in] state      State to update
 * @param[in] state_data Previous modes to add
 */
state_status_t state_share_set_prev(state_t *state, state_data_t *state_data)
{
	state_status_t status = STATE_SUCCESS;

	state->state_data.share.share_access_prev |=
	    (1 << state_data->share.share_access);

	state->state_data.share.share_deny_prev |=
	    (1 << state_data->share.share_deny);

	return status;
}

/**
 * @brief Check if the state has seen the share modes before
 *
 * This is needed when we check validity of open downgrade.
 *
 * @param[in] state      State to check
 * @param[in] state_data Alleged previous mode
 */
state_status_t state_share_check_prev(state_t *state,
				      state_data_t *state_data)
{
	state_status_t status = STATE_SUCCESS;

	if ((state->state_data.share.
	     share_access_prev & (1 << state_data->share.share_access)) == 0)
		return STATE_STATE_ERROR;

	if ((state->state_data.share.
	     share_deny_prev & (1 << state_data->share.share_deny)) == 0)
		return STATE_STATE_ERROR;

	return status;
}

/**
 * @brief Check for share conflict
 *
 * The state lock _must_ be held for this call.
 *
 * @param[in] entry        File to query
 * @param[in] share_access Desired access mode
 * @param[in] share_deny   Desired deny mode
 *
 * @return State status.
 */
state_status_t state_share_check_conflict(cache_entry_t *entry,
					  int share_access, int share_deny)
{
	char *cause = "";

	if ((share_access & OPEN4_SHARE_ACCESS_READ) != 0
	    && entry->object.file.share_state.share_deny_read > 0) {
		cause = "access read denied by existing deny read";
		goto out_conflict;
	}

	if ((share_access & OPEN4_SHARE_ACCESS_WRITE) != 0
	    && entry->object.file.share_state.share_deny_write > 0) {
		cause = "access write denied by existing deny write";
		goto out_conflict;
	}

	if ((share_deny & OPEN4_SHARE_DENY_READ) != 0
	    && entry->object.file.share_state.share_access_read > 0) {
		cause = "deny read denied by existing access read";
		goto out_conflict;
	}

	if ((share_deny & OPEN4_SHARE_DENY_WRITE) != 0
	    && entry->object.file.share_state.share_access_write > 0) {
		cause = "deny write denied by existing access write";
		goto out_conflict;
	}

	return STATE_SUCCESS;

 out_conflict:

	LogDebug(COMPONENT_STATE, "Share conflict detected: %s", cause);
	return STATE_STATE_CONFLICT;
}

/**
 * @brief Update the ref counter of share state
 *
 * This function should be called with the state lock held
 *
 * @param[in] entry      File to update
 * @param[in] old_access Previous access mode
 * @param[in] old_deny   Previous deny mode
 * @param[in] new_access Current access mode
 * @param[in] new_deny   Current deny mode
 * @param[in] v4         True if this is a v4 share/open
 */
static void state_share_update_counter(cache_entry_t *entry, int old_access,
				       int old_deny, int new_access,
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

	entry->object.file.share_state.share_access_read += access_read_inc;
	entry->object.file.share_state.share_access_write += access_write_inc;
	entry->object.file.share_state.share_deny_read += deny_read_inc;
	entry->object.file.share_state.share_deny_write += deny_write_inc;
	if (v4)
		entry->object.file.share_state.share_deny_write_v4 +=
		    deny_write_inc;

	LogFullDebug(COMPONENT_STATE,
		     "entry %p: share counter: "
		     "access_read %u, access_write %u, "
		     "deny_read %u, deny_write %u, deny_write_v4 %u", entry,
		     entry->object.file.share_state.share_access_read,
		     entry->object.file.share_state.share_access_write,
		     entry->object.file.share_state.share_deny_read,
		     entry->object.file.share_state.share_deny_write,
		     entry->object.file.share_state.share_deny_write_v4);
}

/**
 * @brief Calculate the union of share access of given file
 *
 * @param[in] entry File to check
 *
 * @return Calculated access.
 */
static unsigned int state_share_get_share_access(cache_entry_t *entry)
{
	unsigned int share_access = 0;

	if (entry->object.file.share_state.share_access_read > 0)
		share_access |= OPEN4_SHARE_ACCESS_READ;

	if (entry->object.file.share_state.share_access_write > 0)
		share_access |= OPEN4_SHARE_ACCESS_WRITE;

	LogFullDebug(COMPONENT_STATE, "entry %p: union share access = %u",
		     entry, share_access);

	return share_access;
}

/**
 * @brief Calculate the union of share deny of given file
 *
 * @param[in] entry File to check
 *
 * @return Deny mode union.
 */
static unsigned int state_share_get_share_deny(cache_entry_t *entry)
{
	unsigned int share_deny = 0;

	if (entry->object.file.share_state.share_deny_read > 0)
		share_deny |= OPEN4_SHARE_DENY_READ;

	if (entry->object.file.share_state.share_deny_write > 0)
		share_deny |= OPEN4_SHARE_DENY_WRITE;

	LogFullDebug(COMPONENT_STATE, "entry %p: union share deny = %u", entry,
		     share_deny);

	return share_deny;
}

/**
 * @brief Start I/O by an anonymous stateid
 *
 * This function checks for conflicts with existing deny modes and
 * marks the I/O as in process to conflicting shares won't be granted.
 *
 * @brief[in,out] entry        File on which to operate
 * @brief[in]     share_access Access matching I/O done
 *
 * @return State status.
 */
state_status_t state_share_anonymous_io_start(cache_entry_t *entry,
					      int share_access)
{
	state_status_t status = 0;
	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	status = state_share_check_conflict(entry, share_access, 0);
	if (status == STATE_SUCCESS) {
		/* Temporarily bump the access counters, v4 mode doesn't matter
		 * since there is no deny mode associated with anonymous I/O.
		 */
		state_share_update_counter(entry, OPEN4_SHARE_ACCESS_NONE,
					   OPEN4_SHARE_DENY_NONE, share_access,
					   OPEN4_SHARE_DENY_NONE, false);
	}

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	return status;
}

/**
 * @brief Finish an anonymous I/O
 *
 * @param[in,out] entry        Entry on which to operate
 * @param[in]     share_access Access bits indicating I/O type
 */
void state_share_anonymous_io_done(cache_entry_t *entry, int share_access)
{
	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	/* Undo the temporary bump to the access counters, v4 mode doesn't
	 * matter since there is no deny mode associated with anonymous I/O.
	 */
	state_share_update_counter(entry, share_access, OPEN4_SHARE_DENY_NONE,
				   OPEN4_SHARE_ACCESS_NONE,
				   OPEN4_SHARE_DENY_NONE, false);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);
}

/**
 * @brief Implement NLM share call
 *
 * @param[in,out] entry        File on which to operate
 * @param[in]     export       Export through which file is accessed
 * @param[in]     share_access Share mode requested
 * @param[in]     share_deny   Deny mode requested
 * @param[in]     owner        Share owner
 *
 * @return State status.
 */
state_status_t state_nlm_share(cache_entry_t *entry,
			       int share_access,
			       int share_deny, state_owner_t *owner,
			       bool reclaim)
{
	unsigned int old_entry_share_access;
	unsigned int old_entry_share_deny;
	unsigned int new_entry_share_access;
	unsigned int new_entry_share_deny;
	fsal_share_param_t share_param;
	state_nlm_share_t *nlm_share;
	cache_inode_status_t cache_status;
	fsal_openflags_t openflags;
	state_status_t status = 0;
	struct fsal_export *fsal_export = op_ctx->fsal_export;

	cache_status = cache_inode_inc_pin_ref(entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		LogDebug(COMPONENT_STATE, "Could not pin file");
		status = cache_inode_status_to_state_status(cache_status);
		return status;
	}

	/* If FSAL supports reopen method, we open read-only if the access
	 * needs read only. If not, a later request may need read-write
	 * open that needs closing and then opening the file again. The
	 * act of closing the file may remove shared lock state, so we
	 * open read-write now itself for all access needs.
	 */
	if (share_access == fsa_R &&
	    fsal_export->ops->fs_supports(fsal_export, fso_reopen_method))
		openflags = FSAL_O_READ;
	else
		openflags = FSAL_O_RDWR;
	if (reclaim)
		openflags |= FSAL_O_RECLAIM;
	cache_status = cache_inode_open(entry, openflags, 0);
	if (cache_status != CACHE_INODE_SUCCESS) {
		cache_inode_dec_pin_ref(entry, true);

		LogFullDebug(COMPONENT_STATE, "Could not open file");

		status = cache_inode_status_to_state_status(cache_status);
		return status;
	}

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	/* Check if new share state has conflicts. */
	status = state_share_check_conflict(entry, share_access, share_deny);
	if (status != STATE_SUCCESS) {
		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		cache_inode_dec_pin_ref(entry, true);

		LogEvent(COMPONENT_STATE,
			 "Share conflicts detected during add");

		return status;
	}

	/* Create a new NLM Share object */
	nlm_share = gsh_calloc(1, sizeof(state_nlm_share_t));

	if (nlm_share == NULL) {
		PTHREAD_RWLOCK_unlock(&entry->state_lock);

		cache_inode_dec_pin_ref(entry, true);

		LogEvent(COMPONENT_STATE, "Can not allocate memory for share");

		status = STATE_MALLOC_ERROR;

		return status;
	}

	nlm_share->sns_owner = owner;
	nlm_share->sns_entry = entry;
	nlm_share->sns_access = share_access;
	nlm_share->sns_deny = share_deny;
	nlm_share->sns_export = op_ctx->export;

	/* Add share to list for NLM Owner */
	inc_state_owner_ref(owner);

	pthread_mutex_lock(&owner->so_mutex);

	glist_add_tail(&owner->so_owner.so_nlm_owner.so_nlm_shares,
		       &nlm_share->sns_share_per_owner);

	pthread_mutex_unlock(&owner->so_mutex);

	dec_state_owner_ref(owner);

	/* Add share to list for NSM Client */
	inc_nsm_client_ref(owner->so_owner.so_nlm_owner.so_client->
			   slc_nsm_client);

	pthread_mutex_lock(&owner->so_owner.so_nlm_owner.so_client
			   ->slc_nsm_client->ssc_mutex);

	glist_add_tail(&owner->so_owner.so_nlm_owner.so_client->slc_nsm_client->
		       ssc_share_list, &nlm_share->sns_share_per_client);

	pthread_mutex_unlock(&owner->so_owner.so_nlm_owner.so_client
			     ->slc_nsm_client->ssc_mutex);

	/* Add share to list for file, if list was empty take a pin ref to
	 * keep this file pinned in the inode cache.
	 */
	if (glist_empty(&entry->object.file.nlm_share_list))
		cache_inode_inc_pin_ref(entry);

	glist_add_tail(&entry->object.file.nlm_share_list,
		       &nlm_share->sns_share_per_file);

	/* Add to share list for export */
	PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
	glist_add_tail(&op_ctx->export->exp_nlm_share_list,
		       &nlm_share->sns_share_per_export);
	PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

	/* Get the current union of share states of this file. */
	old_entry_share_access = state_share_get_share_access(entry);
	old_entry_share_deny = state_share_get_share_deny(entry);

	/* Update the ref counted share state of this file. */
	state_share_update_counter(entry, OPEN4_SHARE_ACCESS_NONE,
				   OPEN4_SHARE_DENY_NONE, share_access,
				   share_deny, true);

	/* Get the updated union of share states of this file. */
	new_entry_share_access = state_share_get_share_access(entry);
	new_entry_share_deny = state_share_get_share_deny(entry);

	/* If this file's share bits are different from the supposed value,
	 * update it.
	 */
	if ((new_entry_share_access != old_entry_share_access)
	    || (new_entry_share_deny != old_entry_share_deny)) {
		/* Try to push to FSAL. */
		share_param.share_access = new_entry_share_access;
		share_param.share_deny = new_entry_share_deny;
		share_param.share_reclaim = reclaim;

		status = do_share_op(entry, owner, &share_param);

		if (status != STATE_SUCCESS) {
			/* Revert the ref counted share state of this file. */
			state_share_update_counter(entry, share_access,
						   share_deny,
						   OPEN4_SHARE_ACCESS_NONE,
						   OPEN4_SHARE_DENY_NONE, true);

			/* Remove from share list for export */
			PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
			glist_del(&nlm_share->sns_share_per_export);
			PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

			/* Remove the share from the list for the file. If the
			 * list is now empty also remove the extra pin ref.
			 */
			glist_del(&nlm_share->sns_share_per_file);

			if (glist_empty(&entry->object.file.nlm_share_list))
				cache_inode_dec_pin_ref(entry, true);

			/* Remove the share from the NSM Client list */
			pthread_mutex_lock(&owner->so_owner.so_nlm_owner
					   .so_client->slc_nsm_client
					   ->ssc_mutex);

			glist_del(&nlm_share->sns_share_per_client);

			pthread_mutex_unlock(&owner->so_owner.so_nlm_owner
					     .so_client->slc_nsm_client
					     ->ssc_mutex);

			dec_nsm_client_ref(owner->so_owner.so_nlm_owner.
					   so_client->slc_nsm_client);

			/* Remove the share from the NLM Owner list */
			pthread_mutex_lock(&owner->so_mutex);

			glist_del(&nlm_share->sns_share_per_owner);

			pthread_mutex_unlock(&owner->so_mutex);

			dec_state_owner_ref(owner);

			/* Free the NLM Share and exit */
			gsh_free(nlm_share);

			PTHREAD_RWLOCK_unlock(&entry->state_lock);

			cache_inode_dec_pin_ref(entry, true);

			LogDebug(COMPONENT_STATE, "do_share_op failed");

			return status;
		}
	}

	LogFullDebug(COMPONENT_STATE, "added share_access %u, " "share_deny %u",
		     share_access, share_deny);

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_dec_pin_ref(entry, true);

	return status;
}

/**
 * @brief Implement NLM unshare procedure
 *
 * @param[in,out] entry        File on which to opwerate
 * @param[in]     share_access Access mode to relinquish
 * @param[in]     share_deny   Deny mode to relinquish
 * @param[in]     owner        Share owner
 *
 * @return State status.
 */
state_status_t state_nlm_unshare(cache_entry_t *entry,
				 int share_access,
				 int share_deny,
				 state_owner_t *owner)
{
	struct glist_head *glist, *glistn;
	unsigned int old_entry_share_access;
	unsigned int old_entry_share_deny;
	unsigned int new_entry_share_access;
	unsigned int new_entry_share_deny;
	unsigned int removed_share_access;
	unsigned int removed_share_deny;
	fsal_share_param_t share_param;
	state_nlm_share_t *nlm_share;
	cache_inode_status_t cache_status;
	state_status_t status = 0;

	cache_status = cache_inode_inc_pin_ref(entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		status = cache_inode_status_to_state_status(cache_status);
		LogDebug(COMPONENT_STATE, "Could not pin file");
		return status;
	}

	PTHREAD_RWLOCK_wrlock(&entry->state_lock);

	glist_for_each_safe(glist, glistn, &entry->object.file.nlm_share_list) {
		nlm_share =
		    glist_entry(glist, state_nlm_share_t, sns_share_per_file);

		if (different_owners(owner, nlm_share->sns_owner))
			continue;

		if ((op_ctx->export != NULL) &&
		    (op_ctx->export != nlm_share->sns_export))
			continue;

		/* share_access == OPEN4_SHARE_ACCESS_NONE indicates that
		 * any share should be matched for unshare.
		 */
		if (share_access != OPEN4_SHARE_ACCESS_NONE
		    && (nlm_share->sns_access != share_access
			|| nlm_share->sns_deny != share_deny))
			continue;

		/* Get the current union of share states of this file. */
		old_entry_share_access = state_share_get_share_access(entry);
		old_entry_share_deny = state_share_get_share_deny(entry);

		/* Share state to be removed. */
		removed_share_access = nlm_share->sns_access;
		removed_share_deny = nlm_share->sns_deny;

		/* Update the ref counted share state of this file. */
		state_share_update_counter(entry, removed_share_access,
					   removed_share_deny,
					   OPEN4_SHARE_ACCESS_NONE,
					   OPEN4_SHARE_DENY_NONE, true);

		/* Get the updated union of share states of this file. */
		new_entry_share_access = state_share_get_share_access(entry);
		new_entry_share_deny = state_share_get_share_deny(entry);

		/* If this file's share bits are different from the supposed
		 * value, update it.
		 */
		if ((new_entry_share_access != old_entry_share_access)
		    || (new_entry_share_deny != old_entry_share_deny)) {
			/* Try to push to FSAL. */
			share_param.share_access = new_entry_share_access;
			share_param.share_deny = new_entry_share_deny;
			share_param.share_reclaim = false;

			status = do_share_op(entry, owner,
					     &share_param);

			if (status != STATE_SUCCESS) {
				/* Revert the ref counted share state
				 * of this file.
				 */
				state_share_update_counter(
					entry,
					OPEN4_SHARE_ACCESS_NONE,
					OPEN4_SHARE_DENY_NONE,
					removed_share_access,
					removed_share_deny,
					true);

				PTHREAD_RWLOCK_unlock(&entry->state_lock);

				cache_inode_dec_pin_ref(entry, true);

				LogDebug(COMPONENT_STATE, "do_share_op failed");

				return status;
			}
		}

		LogFullDebug(COMPONENT_STATE,
			     "removed share_access %u, share_deny %u",
			     removed_share_access, removed_share_deny);

		/* Remove from share list for export */
		PTHREAD_RWLOCK_wrlock(&nlm_share->sns_export->lock);
		glist_del(&nlm_share->sns_share_per_export);
		PTHREAD_RWLOCK_unlock(&nlm_share->sns_export->lock);

		/* Remove the share from the list for the file. If the list
		 * is now empty also remove the extra pin ref.
		 */
		glist_del(&nlm_share->sns_share_per_file);

		if (glist_empty(&entry->object.file.nlm_share_list))
			cache_inode_dec_pin_ref(entry, true);

		/* Remove the share from the NSM Client list */
		pthread_mutex_lock(&owner->so_owner.so_nlm_owner.so_client
				   ->slc_nsm_client->ssc_mutex);

		glist_del(&nlm_share->sns_share_per_client);

		pthread_mutex_unlock(&owner->so_owner.so_nlm_owner.so_client
				     ->slc_nsm_client->ssc_mutex);

		dec_nsm_client_ref(owner->so_owner.so_nlm_owner.so_client->
				   slc_nsm_client);

		/* Remove the share from the NLM Owner list */
		pthread_mutex_lock(&owner->so_mutex);

		glist_del(&nlm_share->sns_share_per_owner);

		pthread_mutex_unlock(&owner->so_mutex);

		dec_state_owner_ref(owner);

		/* Free the NLM Share (and continue to look for more) */
		gsh_free(nlm_share);
	}

	PTHREAD_RWLOCK_unlock(&entry->state_lock);

	cache_inode_dec_pin_ref(entry, true);

	return status;
}

/**
 * @brief Remove all share state from a file
 *
 * @param[in] entry File to wipe
 */
void state_share_wipe(cache_entry_t *entry)
{
	state_nlm_share_t *nlm_share;
	struct glist_head *glist;
	struct glist_head *glistn;
	state_owner_t *owner;

	glist_for_each_safe(glist, glistn, &entry->object.file.nlm_share_list) {
		nlm_share =
		    glist_entry(glist, state_nlm_share_t, sns_share_per_file);

		owner = nlm_share->sns_owner;

		/* Remove from share list for export */
		PTHREAD_RWLOCK_wrlock(&nlm_share->sns_export->lock);
		glist_del(&nlm_share->sns_share_per_export);
		PTHREAD_RWLOCK_unlock(&nlm_share->sns_export->lock);

		/* Remove the share from the list for the file. If the list
		 * is now empty also remove the extra pin ref.
		 */
		glist_del(&nlm_share->sns_share_per_file);

		if (glist_empty(&entry->object.file.nlm_share_list))
			cache_inode_dec_pin_ref(entry, false);

		/* Remove the share from the NSM Client list */
		pthread_mutex_lock(&owner->so_owner.so_nlm_owner.so_client
				   ->slc_nsm_client->ssc_mutex);

		glist_del(&nlm_share->sns_share_per_client);

		pthread_mutex_unlock(&owner->so_owner.so_nlm_owner.so_client
				     ->slc_nsm_client->ssc_mutex);

		dec_nsm_client_ref(owner->so_owner.so_nlm_owner.so_client->
				   slc_nsm_client);

		/* Remove the share from the NLM Owner list */
		pthread_mutex_lock(&owner->so_mutex);

		glist_del(&nlm_share->sns_share_per_owner);

		pthread_mutex_unlock(&owner->so_mutex);

		dec_state_owner_ref(owner);

		/* Free the NLM Share (and continue to look for more) */
		gsh_free(nlm_share);
	}
}

void state_export_unshare_all(void)
{
	int errcnt = 0;
	state_nlm_share_t *nlm_share;
	state_owner_t *owner;
	cache_entry_t *entry;
	state_status_t status;

	while (errcnt < STATE_ERR_MAX) {
		PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);

		nlm_share =
		    glist_first_entry(&op_ctx->export->exp_nlm_share_list,
				      state_nlm_share_t,
				      sns_share_per_export);

		if (nlm_share == NULL) {
			PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
			break;
		}

		entry = nlm_share->sns_entry;
		owner = nlm_share->sns_owner;

		/* get a reference to the owner */
		inc_state_owner_ref(owner);

		cache_inode_lru_ref(entry, LRU_FLAG_NONE);

		/* Drop the export mutex to call unshare */
		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

		/* Remove all shares held by this Owner on this export */
		status = state_nlm_unshare(entry,
					   OPEN4_SHARE_ACCESS_NONE,
					   OPEN4_SHARE_DENY_NONE,
					   owner);

		dec_state_owner_ref(owner);
		cache_inode_put(entry);

		if (!state_unlock_err_ok(status)) {
			/* Increment the error count and try the next share,
			 * with any luck the memory pressure which is causing
			 * the problem will resolve itself.
			 */
			LogFullDebug(COMPONENT_STATE,
				     "state_unlock returned %s",
				     state_err_str(status));
			errcnt++;
		}
	}
}

/** @} */
