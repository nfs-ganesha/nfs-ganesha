/*
 * Copyright (C) 2013, The Linux Box Corporation
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * MERchantability or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file cache_inode_destroyer.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Support for obliterating the content of the cache at the end
 * of the world.
 */

#include "sal_functions.h"
#include "ht_shutdown.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_proto_tools.h"
#include "abstract_atomic.h"
#include "cache_inode_hash.h"
#include "nsm.h"
#include "export_mgr.h"

static void dec_nsm_client_ref_for_shutdown(state_nsm_client_t *client);
static void dec_state_owner_ref_for_shutdown(state_owner_t *owner);
static void dec_client_id_ref_for_shutdown(nfs_client_id_t *clientid);
static void free_client_id_for_shutdown(nfs_client_id_t *clientid);
static void remove_from_locklist_for_shutdown(state_lock_entry_t *lock_entry);
static void state_del_for_shutdown(state_t *state, cache_entry_t *entry);

/**
 * @brief Relinquish a reference on an NSM client without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] client The client to release
 */

void
dec_nsm_client_ref_for_shutdown(state_nsm_client_t *client)
{
	struct gsh_buffdesc key = {
		.addr = client,
		.len = sizeof(state_nsm_client_t)
	};
	int32_t refcount = atomic_dec_int32_t(&client->ssc_refcount);

	if (refcount > 0)
		return;

	ht_unsafe_zap_by_key(ht_nsm_client, &key);

	nsm_unmonitor(client);
	free_nsm_client(client);
}

/**
 * @brief Decrement the refcount on a client owner record without
 * taking locks.
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] record Record on which to release a reference
 */

void
dec_client_record_ref_for_shutdown(nfs_client_record_t *record)
{
	struct gsh_buffdesc key = {
		.addr = record,
		.len = sizeof(nfs_client_record_t)
	};
	int32_t refcount = atomic_dec_int32_t(&record->cr_refcount);

	if (refcount > 0)
		return;

	ht_unsafe_zap_by_key(ht_client_record, &key);
	free_client_record(record);
}

/**
 * @brief Deconstruct and free a client record without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] clientid The client record to free
 */
void
free_client_id_for_shutdown(nfs_client_id_t *clientid)
{
	if (clientid->cid_client_record != NULL)
		dec_client_record_ref_for_shutdown(clientid->cid_client_record);

	if (pthread_mutex_destroy(&clientid->cid_mutex) != 0)
		LogDebug(COMPONENT_CLIENTID,
			 "pthread_mutex_destroy returned errno %d (%s)",
			 errno, strerror(errno));

	/* For NFSv4.1 clientids, destroy all associated sessions */
	if (clientid->cid_minorversion > 0) {
		struct glist_head *glist = NULL;
		struct glist_head *glistn = NULL;

		glist_for_each_safe(glist, glistn,
				    &clientid->cid_cb.v41.cb_session_list) {
			nfs41_session_t *session = glist_entry(glist,
							       nfs41_session_t,
							       session_link);
			struct gsh_buffdesc key = {
				.addr = session->session_id,
				.len = NFS4_SESSIONID_SIZE
			};

			ht_unsafe_zap_by_key(ht_session_id, &key);

			/* Unlink the session from the client's list of
			   sessions */
			glist_del(&session->session_link);
			/* Decrement our reference to the clientid record */
			dec_client_id_ref_for_shutdown
			    (session->clientid_record);
			/* Destroy the session's back channel (if any) */
			if (session->flags & session_bc_up)
				nfs_rpc_destroy_chan(&session->cb_chan);
			/* Free the memory for the session */
			pool_free(nfs41_session_pool, session);
		}
	}
	pool_free(client_id_pool, clientid);
}

/**
 * @brief Decrement the clientid refcount without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] clientid Client record
 */

void
dec_client_id_ref_for_shutdown(nfs_client_id_t *clientid)
{
	int32_t cid_refcount;

	cid_refcount = atomic_dec_int32_t(&clientid->cid_refcount);

	if (cid_refcount > 0)
		return;

	free_client_id_for_shutdown(clientid);
}

/**
 * @brief Relinquish a reference on an NLM client without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] client Client to release
 */

void
dec_nlm_client_ref_for_shutdown(state_nlm_client_t *client)
{
	struct gsh_buffdesc key = {
		.addr = client,
		.len = sizeof(state_nlm_client_t)
	};
	int32_t refcount = atomic_dec_int32_t(&client->slc_refcount);

	if (refcount > 0)
		return;

	ht_unsafe_zap_by_key(ht_nlm_client, &key);

	if (client->slc_nsm_client != NULL)
		dec_nsm_client_ref_for_shutdown(client->slc_nsm_client);

	if (client->slc_nlm_caller_name != NULL)
		gsh_free(client->slc_nlm_caller_name);

	gsh_free(client);
}

/**
 * @brief Remove an NLM owner from the hash table without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] owner Owner to remove
 */

static
void remove_nlm_owner_for_shutdown(state_owner_t *owner)
{
	struct gsh_buffdesc key = {
		.addr = owner,
		.len = sizeof(state_owner_t)
	};
	ht_unsafe_zap_by_key(ht_nlm_owner, &key);

	dec_nlm_client_ref_for_shutdown(owner->so_owner.so_nlm_owner.so_client);
	gsh_free(owner);
}

/**
 * @brief Remove a 9p owner from the hash table without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] owner Owner to remove
 */

#ifdef _USE_9P
void remove_9p_owner_for_shutdown(state_owner_t *owner)
{
	struct gsh_buffdesc key = {
		.addr = owner,
		.len = sizeof(state_owner_t)
	};

	ht_unsafe_zap_by_key(ht_9p_owner, &key);
	gsh_free(owner);
}
#endif

/**
 * @brief Remove an NFSv4 owner from the hash table without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] owner Owner to remove
 */

void
remove_nfs4_owner_for_shutdown(state_owner_t *owner)
{
#ifdef _USE_9P
	state_nfs4_owner_name_t oname = {
		.son_owner_len = owner->so_owner_len,
		.son_owner_val = gsh_malloc(sizeof(char) * owner->so_owner_len),
	};

	memcpy(oname.son_owner_val, owner->so_owner_val, owner->so_owner_len);

	struct gsh_buffdesc key = {
		.addr = &oname,
		.len = sizeof(state_nfs4_owner_name_t)
	};
	ht_unsafe_zap_by_key(ht_9p_owner, &key);
#endif

	if (owner->so_type == STATE_LOCK_OWNER_NFSV4
	    && owner->so_owner.so_nfs4_owner.so_related_owner != NULL)
		dec_state_owner_ref_for_shutdown(owner->so_owner.so_nfs4_owner.
						 so_related_owner);

	nfs4_Compound_FreeOne(&owner->so_owner.so_nfs4_owner.so_resp);
	glist_del(&owner->so_owner.so_nfs4_owner.so_perclient);
	dec_client_id_ref_for_shutdown(owner->so_owner.so_nfs4_owner.
				       so_clientrec);
	pool_free(state_owner_pool, owner);
}

/**
 * @brief Relinquish a reference on a state owner without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] owner The owner to release
 */

void
dec_state_owner_ref_for_shutdown(state_owner_t *owner)
{
	owner->so_refcount--;
	if (owner->so_refcount > 0)
		return;

	switch (owner->so_type) {
	case STATE_LOCK_OWNER_NLM:
		remove_nlm_owner_for_shutdown(owner);
		break;

#ifdef _USE_9P
	case STATE_LOCK_OWNER_9P:
		remove_9p_owner_for_shutdown(owner);
#endif
		break;
	case STATE_OPEN_OWNER_NFSV4:
	case STATE_LOCK_OWNER_NFSV4:
	case STATE_CLIENTID_OWNER_NFSV4:
		remove_nfs4_owner_for_shutdown(owner);
		break;

	case STATE_LOCK_OWNER_UNKNOWN:
		break;
	}
}

/**
 * @brief Relinquish a reference on a lock entry without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in,out] lock_entry Entry to release
 */
void
lock_entry_dec_ref_for_shutdown(state_lock_entry_t *lock_entry)
{
	lock_entry->sle_ref_count--;
	if (lock_entry->sle_ref_count > 0)
		return;

	/* Release block data if present */
	if (lock_entry->sle_block_data != NULL)
		gsh_free(lock_entry->sle_block_data);
	gsh_free(lock_entry);
}

/**
 * @brief Remove an entry from the lock lists without taking locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in,out] lock_entry Entry to remove
 */
void
remove_from_locklist_for_shutdown(state_lock_entry_t *lock_entry)
{
	state_owner_t *owner = lock_entry->sle_owner;
	if (owner != NULL) {
		if (owner->so_type == STATE_LOCK_OWNER_NLM) {
			/* Remove from list of locks owned by client
			   that owner belongs to */
			glist_del(&lock_entry->sle_client_locks);
			dec_nsm_client_ref_for_shutdown(owner->so_owner.
							so_nlm_owner.so_client->
							slc_nsm_client);
		}
		glist_del(&lock_entry->sle_export_locks);
		if (owner->so_type == STATE_LOCK_OWNER_NFSV4)
			glist_del(&lock_entry->sle_state_locks);
		glist_del(&lock_entry->sle_owner_locks);
		dec_state_owner_ref_for_shutdown(owner);
	}
	lock_entry->sle_owner = NULL;
	glist_del(&lock_entry->sle_list);
	lock_entry_dec_ref_for_shutdown(lock_entry);
}

/**
 * @brief Remove a state from a cache entry while taking no locks
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in]     state The state to remove
 * @param[in,out] entry The cache entry to modify
 */

void
state_del_for_shutdown(state_t *state, cache_entry_t *entry)
{
	struct gsh_buffdesc key = {
		.addr = state->stateid_other,
		.len = OTHERSIZE
	};

	ht_unsafe_zap_by_key(ht_state_id, &key);

	/* Release the state owner reference */
	if (state->state_owner != NULL) {
		glist_del(&state->state_owner_list);
		dec_state_owner_ref_for_shutdown(state->state_owner);
	}

	/* Remove from the list of states for a particular cache entry */
	glist_del(&state->state_list);

	/* Remove from the list of lock states for a particular open state */
	if (state->state_type == STATE_TYPE_LOCK)
		glist_del(&state->state_data.lock.state_sharelist);

	/* Remove from list of states for a particular export */
	glist_del(&state->state_export_list);
	pool_free(state_v4_pool, state);
}

/**
 * @brief Clear all locks on the FSAL
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] entry Entry to clear
 */

static void
clear_fsal_locks(cache_entry_t *entry, struct req_op_context *req_ctx)
{
	/* FSAL object handle */
	struct fsal_obj_handle *handle = entry->obj_handle;

	if (req_ctx->fsal_export->ops->fs_supports(req_ctx->fsal_export,
						   fso_lock_support)) {
		/* Lock that covers the whole file - type doesn't
		   matter for unlock */
		fsal_lock_param_t lock = {
			.lock_sle_type = FSAL_POSIX_LOCK,
			.lock_type = FSAL_LOCK_R,
			.lock_start = 0,
			.lock_length = 0
		};
		/* FSAL return status */
		fsal_status_t fsal_status;
		/* Conflicting lock */
		fsal_lock_param_t conflicting_lock;

		memset(&conflicting_lock, 0, sizeof(conflicting_lock));
		fsal_status =
		    handle->ops->lock_op(handle, req_ctx, NULL,
					 FSAL_OP_UNLOCK, &lock,
					 &conflicting_lock);
		if (FSAL_IS_ERROR(fsal_status)) {
			LogMajor(COMPONENT_CACHE_INODE,
				 "Couldn't release share: major=%u",
				 fsal_status.major);
		}
	}
}

/**
 * @brief Clear all shares on the FSAL
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] entry Entry to clear
 */

static void
clear_fsal_shares(cache_entry_t *entry, struct req_op_context *req_ctx)
{
	/* FSAL object handle */
	struct fsal_obj_handle *handle = entry->obj_handle;

	if (req_ctx->fsal_export->ops->fs_supports(req_ctx->fsal_export,
						   fso_share_support)) {
		/* Fully released shares */
		fsal_share_param_t releaser = {
			.share_access = 0,
			.share_deny = 0
		};

		/* FSAL return status */
		fsal_status_t fsal_status =
		    handle->ops->share_op(entry->obj_handle,
					  req_ctx,
					  NULL,
					  releaser);
		if (FSAL_IS_ERROR(fsal_status))
			LogMajor(COMPONENT_CACHE_INODE,
				 "Couldn't release share: major=%u",
				 fsal_status.major);
	}
}

/**
 * @brief Destroy all NLM shares on a file
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] entry File to wipe
 *
 * @return true if there were shares to destroy.
 * @return false if there weren't.
 */

static bool
destroy_nlm_shares(cache_entry_t *entry, struct req_op_context *req_ctx)
{
	/* Iterator for NSM shares */
	struct glist_head *nsi;
	/* Next for safe iteration */
	struct glist_head *nsn;
	/* If we found any */
	bool there_were_shares = false;

	glist_for_each_safe(nsi, nsn, &entry->object.file.nlm_share_list) {
		state_nlm_share_t *nlm_share =
		    glist_entry(nsi, state_nlm_share_t,
				sns_share_per_file);
		state_owner_t *owner = nlm_share->sns_owner;
		glist_del(&nlm_share->sns_share_per_file);
		if (glist_empty(&entry->object.file.nlm_share_list))
			cache_inode_dec_pin_ref(entry, false);
		glist_del(&nlm_share->sns_share_per_client);
		glist_del(&nlm_share->sns_share_per_export);
		dec_nsm_client_ref_for_shutdown(owner->so_owner.so_nlm_owner.
						so_client->slc_nsm_client);
		glist_del(&nlm_share->sns_share_per_owner);
		dec_state_owner_ref_for_shutdown(owner);

		/* Free the NLM Share (and continue to look for more) */
		gsh_free(nlm_share);
		there_were_shares = true;
	}

	return there_were_shares;
}

/**
 * @brief Destroy all locks on a file
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] entry File to wipe
 */

static void
destroy_locks(cache_entry_t *entry, struct req_op_context *req_ctx)
{
	/* Lock entry iterator */
	struct glist_head *lei = NULL;
	/* Next lock entry for safe iteration */
	struct glist_head *len = NULL;

	if (glist_empty(&entry->object.file.lock_list))
		return;

	glist_for_each_safe(lei, len, &entry->object.file.lock_list) {
		state_lock_entry_t *found_entry =
		    glist_entry(lei, state_lock_entry_t, sle_list);
		remove_from_locklist_for_shutdown(found_entry);
	}
	cache_inode_dec_pin_ref(entry, false);

	clear_fsal_locks(entry, req_ctx);
}

/**
 * @brief Remove all state from a cache entry without taking locks
 *
 * Used by cache_inode_kill_entry in the event that the FSAL says a
 * handle is stale.
 *
 * @param[in,out] entry The entry to wipe
 *
 * @retval true if share states exist.
 * @retval false if they don't.
 */

static bool
destroy_nfs4_state(cache_entry_t *entry, struct req_op_context *req_ctx)
{
	/* NFSv4 state iterator */
	struct glist_head *si = NULL;
	/* Next NFSv4 state for safe iteration */
	struct glist_head *sn = NULL;
	/* A list of open states so that we can process them last,
	   after disposing of any lock states that may depend on them. */
	struct glist_head opens;

	glist_init(&opens);

	if (glist_empty(&entry->state_list))
		return false;

	glist_for_each_safe(si, sn, &entry->state_list) {
		/* Current state */
		state_t *state = glist_entry(si, state_t, state_list);
		switch (state->state_type) {
		case STATE_TYPE_NONE:
			break;
		case STATE_TYPE_DELEG:
			LogMajor(COMPONENT_CACHE_INODE,
				 "Impossible state found.");
			break;

		case STATE_TYPE_SHARE:
			/* Queue to deal with later */
			glist_del(&state->state_list);
			glist_add_tail(&opens, &state->state_list);
			break;

		case STATE_TYPE_LOCK:
			if (!glist_empty
			    (&state->state_data.lock.state_locklist)) {
				LogMajor(COMPONENT_CACHE_INODE,
					 "Locks should have been freed by "
					 "this point.");
			}
			break;

		case STATE_TYPE_LAYOUT:{
				/* Iterator along segment list */
				struct glist_head *seg_iter = NULL;
				/* Saved 'next' pointer for iterating over
				   segment list */
				struct glist_head *seg_next = NULL;
				/* Input arguments to FSAL_layoutreturn */
				struct fsal_layoutreturn_arg *arg;
				/* Number of recalls currently on the entry */
				size_t recalls = 0;
				/* The current segment in iteration */
				state_layout_segment_t *g = NULL;
				struct pnfs_segment entire = {
					.io_mode = LAYOUTIOMODE4_ANY,
					.offset = 0,
					.length = NFS4_UINT64_MAX
				};

				recalls =
				    glist_length(&entry->layoutrecall_list);

				arg =
				    alloca(sizeof(struct fsal_layoutreturn_arg)
					   + sizeof(void *) * (recalls - 1));

				memset(arg, 0,
				       sizeof(struct fsal_layoutreturn_arg));
				arg->lo_type =
				    state->state_data.layout.state_layout_type;
				arg->circumstance = circumstance_shutdown;
				arg->spec_segment = entire;
				arg->ncookies = 0;

				glist_for_each_safe(seg_iter, seg_next,
						    &state->state_data.layout.
						    state_segments) {
					/* The current segment in iteration */
					g = glist_entry(seg_iter,
							state_layout_segment_t,
							sls_state_segments);
					arg->cur_segment = g->sls_segment;
					arg->fsal_seg_data = g->sls_fsal_data;
					arg->last_segment =
					    (seg_next->next == seg_next);
					arg->dispose = true;
					handle_recalls(arg, state,
						       &g->sls_segment);
					entry->obj_handle->ops->
					    layoutreturn(entry->obj_handle,
							 req_ctx,
							 NULL, arg);

					glist_del(&g->sls_state_segments);
					gsh_free(g);
				}
				break;
			}
		}
		state_del_for_shutdown(state, entry);
	}

	if (glist_empty(&opens)) {
		cache_inode_dec_pin_ref(entry, false);
		return false;
	}

	glist_for_each_safe(si, sn, &entry->state_list) {
		/* Current state */
		state_t *state = glist_entry(si, state_t, state_list);
		/* Now that we know that no lock states depend on
		   them, blow all the share states away. */
		state_del_for_shutdown(state, entry);
	}

	cache_inode_dec_pin_ref(entry, false);

	return true;
}

static void
destroy_file_state(cache_entry_t *entry, struct req_op_context *req_ctx)
{
	/* If NSM shares were found */
	bool nsm_shares = false;
	/* If nfs4 shares were found */
	bool nfs4_shares = false;
	nsm_shares = destroy_nlm_shares(entry, req_ctx);
	destroy_locks(entry, req_ctx);
	nfs4_shares = destroy_nfs4_state(entry, req_ctx);
	if (nsm_shares || nfs4_shares)
		clear_fsal_shares(entry, req_ctx);
	if (entry->obj_handle->ops->status(entry->obj_handle)
	    != FSAL_O_CLOSED) {
		fsal_status_t fsal_status =
		    entry->obj_handle->ops->close(entry->obj_handle);
		if (FSAL_IS_ERROR(fsal_status))
			LogMajor(COMPONENT_CACHE_INODE,
				 "Couldn't close file: major=%u",
				 fsal_status.major);
	}
}

/**
 *
 * @brief Cleans up the export mappings for this entry without locks
 *
 * @param[in]  entry     The cache inode
 * @param[in]  export    The active export
 *
 */

static void destroy_mapping(cache_entry_t *entry)
{
	struct glist_head *glist;
	struct glist_head *glistn;

	/* Entry is unreachable and not referenced so no need to hold attr_lock
	 * to cleanup the export map.
	 */
	glist_for_each_safe(glist, glistn, &entry->export_list) {
		struct entry_export_map *expmap;
		expmap = glist_entry(glist,
				     struct entry_export_map,
				     export_per_entry);

		/* Remove from list of exports for this entry */
		glist_del(&expmap->export_per_entry);

		/* Remove from list of entries for this export */
		glist_del(&expmap->entry_per_export);

		gsh_free(expmap);
	}
}

/**
 * @brief Destroy a single cache entry
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 *
 * @param[in] entry The entry to be destroyed
 */

static void
destroy_entry(cache_entry_t *entry)
{
	/* FSAL Error Code */
	fsal_status_t fsal_status = { 0, 0 };
	struct root_op_context root_op_context;
	struct gsh_export *export = entry->first_export;
	struct fsal_export *fsal_export = NULL;

	if (export != NULL)
		fsal_export = export->export.export_hdl;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, export, fsal_export,
			     0, 0, UNKNOWN_REQUEST);

	if (entry->type == REGULAR_FILE)
		destroy_file_state(entry, &root_op_context.req_ctx);

	if (entry->type == DIRECTORY)
		cache_inode_release_dirents(entry, CACHE_INODE_AVL_BOTH);

	if (entry->obj_handle) {
		fsal_status =
		    (entry->obj_handle->ops->release(entry->obj_handle));
		if (FSAL_IS_ERROR(fsal_status)) {
			LogMajor(COMPONENT_CACHE_INODE,
				 "Couldn't free FSAL ressources "
				 "fsal_status.major=%u", fsal_status.major);
		}
	}
	entry->obj_handle = NULL;

	destroy_mapping(entry);
}

/**
 * @brief Iterate over entries in the cache hash tree and destroy them
 *
 * @note This function is intended to be called only at shutdown.  It
 * takes no locks to avoid a potential hang in the event that a thread
 * was cancelled while holding one.  It *must not* be called while any
 * threads accessing SAL, cache_inode, or FSAL are running.
 */

void
cache_inode_destroyer(void)
{
	/* Index over partitions */
	uint32_t i = 0;

	for (i = 0; i < cih_fhcache.npart; ++i) {
		struct avltree_node *node =
		    avltree_first(&cih_fhcache.partition[i].t);
		while (node) {
			/* The current entry to destroy */
			cache_entry_t *entry =
				avltree_container_of(node,
						     cache_entry_t,
						     fh_hk.
						     node_k);
			destroy_entry(entry);
			avltree_remove(node, &cih_fhcache.partition[i].t);
			pool_free(cache_inode_entry_pool, entry);
			node = avltree_first(&cih_fhcache.partition[i].t);
		}
	}
}

/** @} */
