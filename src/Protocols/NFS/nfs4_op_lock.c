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
 * @file    nfs4_op_lock.c
 * @brief   Implementation of NFS4_OP_LOCK
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "ganesha_list.h"
#include "export_mgr.h"

static const char *lock_tag = "LOCK";

/**
 * @brief The NFS4_OP_LOCK operation.
 *
 * This function implements the NFS4_OP_LOCK operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC 5661, pp. 367-8
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_lock(struct nfs_argop4 *op, compound_data_t *data,
		 struct nfs_resop4 *resp)
{
	/* Shorter alias for arguments */
	LOCK4args * const arg_LOCK4 = &op->nfs_argop4_u.oplock;
	/* Shorter alias for response */
	LOCK4res * const res_LOCK4 = &resp->nfs_resop4_u.oplock;
	/* Status code from state calls */
	state_status_t state_status = STATE_SUCCESS;
	/* Data for lock state to be created */
	state_data_t candidate_data;
	/* Type of lock state */
	state_type_t candidate_type = STATE_TYPE_LOCK;
	/* Status code for protocol functions */
	nfsstat4 nfs_status = 0;
	/* Created or found lock state */
	state_t *lock_state = NULL;
	/* Associated open state */
	state_t *state_open = NULL;
	/* The lock owner */
	state_owner_t *lock_owner = NULL;
	/* The open owner */
	state_owner_t *open_owner = NULL;
	/* The owner of a conflicting lock */
	state_owner_t *conflict_owner = NULL;
	/* The owner in which to store the response for NFSv4.0 */
	state_owner_t *resp_owner = NULL;
	/* Sequence ID, for NFSv4.0 */
	seqid4 seqid = 0;
	/* The client performing these operations */
	nfs_client_id_t *clientid = NULL;
	/* Name for the lock owner */
	state_nfs4_owner_name_t owner_name;
	/* Description of requrested lock */
	fsal_lock_param_t lock_desc;
	/* Description of conflicting lock */
	fsal_lock_param_t conflict_desc;
	/* Whether to block */
	state_blocking_t blocking = STATE_NON_BLOCKING;
	/* Tracking data for the lock state */
	struct state_refer refer;
	/* Indicate if we have a lock owner that must be released. */
	bool_t release_lock_owner = FALSE;
	/* Indicate if we have a open owner that must be released. */
	bool_t release_open_owner = FALSE;
	int rc;

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Entering NFS v4 LOCK handler ----------------------");

	/* Initialize to sane starting values */
	resp->resop = NFS4_OP_LOCK;
	res_LOCK4->status = NFS4_OK;

	/* Record the sequence info */
	if (data->minorversion > 0) {
		memcpy(refer.session,
		       data->session->session_id,
		       sizeof(sessionid4));
		refer.sequence = data->sequence;
		refer.slot = data->slot;
	}

	res_LOCK4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (res_LOCK4->status != NFS4_OK)
		return res_LOCK4->status;

	/* Convert lock parameters to internal types */
	switch (arg_LOCK4->locktype) {
	case READW_LT:
		blocking = STATE_NFSV4_BLOCKING;
	case READ_LT:
		lock_desc.lock_type = FSAL_LOCK_R;
		break;

	case WRITEW_LT:
		blocking = STATE_NFSV4_BLOCKING;
	case WRITE_LT:
		lock_desc.lock_type = FSAL_LOCK_W;
		break;
	default:
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "Invalid lock type");
		res_LOCK4->status = NFS4ERR_INVAL;
		return res_LOCK4->status;
	}

	lock_desc.lock_start = arg_LOCK4->offset;

	if (arg_LOCK4->length != STATE_LOCK_OFFSET_EOF)
		lock_desc.lock_length = arg_LOCK4->length;
	else
		lock_desc.lock_length = 0;

	if (arg_LOCK4->locker.new_lock_owner) {
		/* Check stateid correctness and get pointer to state */
		nfs_status = nfs4_Check_Stateid(
			&arg_LOCK4->locker.locker4_u.open_owner.open_stateid,
			data->current_entry,
			&state_open,
			data,
			STATEID_SPECIAL_FOR_LOCK,
			arg_LOCK4->locker.locker4_u.open_owner.open_seqid,
			data->minorversion == 0,
			lock_tag);

		if (nfs_status != NFS4_OK) {
			if ((nfs_status == NFS4ERR_REPLAY)
			    && (state_open != NULL)
			    && (state_open->state_owner != NULL)) {
				open_owner = state_open->state_owner;
				resp_owner = open_owner;
				inc_state_owner_ref(open_owner);
				release_open_owner = TRUE;
				seqid =
				    arg_LOCK4->locker.locker4_u.open_owner.
				    open_seqid;
				goto check_seqid;
			}

			res_LOCK4->status = nfs_status;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed nfs4_Check_Stateid for"
				 " open owner");
			return res_LOCK4->status;
		}

		open_owner = state_open->state_owner;
		lock_state = NULL;
		lock_owner = NULL;
		resp_owner = open_owner;
		seqid = arg_LOCK4->locker.locker4_u.open_owner.open_seqid;

		inc_state_owner_ref(open_owner);
		release_open_owner = TRUE;

		LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
			"LOCK New lock owner from open owner",
			data->current_entry, open_owner, &lock_desc);

		/* Check is the clientid is known or not */
		rc = nfs_client_id_get_confirmed(
			data->minorversion == 0 ? arg_LOCK4->locker.
				  locker4_u.open_owner.lock_owner.clientid
				: data->session->clientid,
			&clientid);

		if (rc != CLIENT_ID_SUCCESS) {
			res_LOCK4->status = clientid_error_to_nfsstat(rc);
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed nfs_client_id_get");
			goto out2;
		}

		if (isDebug(COMPONENT_CLIENTID) && (clientid !=
		    open_owner->so_owner.so_nfs4_owner.so_clientrec)) {
			char str_open[HASHTABLE_DISPLAY_STRLEN];
			char str_lock[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(open_owner->so_owner.
					      so_nfs4_owner.so_clientrec,
					      str_open);
			display_client_id_rec(clientid, str_lock);

			LogDebug(COMPONENT_CLIENTID,
				 "Unexpected, new lock owner clientid {%s} "
				 "doesn't match open owner clientid {%s}",
				 str_lock, str_open);
		}

		/* The related stateid is already stored in state_open */

		/* An open state has been found. Check its type */
		if (state_open->state_type != STATE_TYPE_SHARE) {
			res_LOCK4->status = NFS4ERR_BAD_STATEID;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed open stateid is not a SHARE");
			goto out2;
		}

		/* Is this lock_owner known ? */
		convert_nfs4_lock_owner(&arg_LOCK4->locker.locker4_u.open_owner.
					lock_owner, &owner_name);
	} else {
		/* Existing lock owner Find the lock stateid From
		 * that, get the open_owner
		 *
		 * There was code here before to handle all-0 stateid,
		 * but that really doesn't apply - when we handle
		 * temporary locks for I/O operations (which is where
		 * we will see all-0 or all-1 stateid, those will not
		 * come in through nfs4_op_lock.
		 *
		 * Check stateid correctness and get pointer to state
		 */
		nfs_status = nfs4_Check_Stateid(
			&arg_LOCK4->locker.locker4_u.lock_owner.lock_stateid,
			data->current_entry,
			&lock_state,
			data,
			STATEID_SPECIAL_FOR_LOCK,
			arg_LOCK4->locker.locker4_u.lock_owner.lock_seqid,
			data->minorversion == 0,
			lock_tag);

		if (nfs_status != NFS4_OK) {
			if ((nfs_status == NFS4ERR_REPLAY)
			    && (lock_state != NULL)
			    && (lock_state->state_owner != NULL)) {
				lock_owner = lock_state->state_owner;
				open_owner = lock_owner->so_owner.so_nfs4_owner.
						so_related_owner;
				resp_owner = lock_owner;
				inc_state_owner_ref(lock_owner);
				release_lock_owner = TRUE;
				seqid = arg_LOCK4->locker.locker4_u.lock_owner.
						lock_seqid;
				goto check_seqid;
			}

			res_LOCK4->status = nfs_status;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed nfs4_Check_Stateid for existing "
				 "lock owner");
			return res_LOCK4->status;
		}

		/* Check if lock state belongs to same export */
		if (lock_state->state_export != op_ctx->export) {
			LogEvent(COMPONENT_STATE,
				 "Lock Owner Export Conflict, Lock held "
				 "for export %d (%s), request for "
				 "export %d (%s)",
				 lock_state->state_export->export_id,
				 lock_state->state_export->fullpath,
				 op_ctx->export->export_id,
				 op_ctx->export->fullpath);
			res_LOCK4->status = STATE_INVALID_ARGUMENT;
			return res_LOCK4->status;
		}

		/* A lock state has been found. Check its type */
		if (lock_state->state_type != STATE_TYPE_LOCK) {
			res_LOCK4->status = NFS4ERR_BAD_STATEID;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed existing lock owner, "
				 "state type is not LOCK");
			return res_LOCK4->status;
		}

		/* Get the old lockowner. We can do the following
		 * 'cast', in NFSv4 lock_owner4 and open_owner4 are
		 * different types but with the same definition
		 */
		lock_owner = lock_state->state_owner;
		open_owner =
			lock_owner->so_owner.so_nfs4_owner.so_related_owner;
		state_open = lock_state->state_data.lock.openstate;
		resp_owner = lock_owner;
		seqid = arg_LOCK4->locker.locker4_u.lock_owner.lock_seqid;

		inc_state_owner_ref(lock_owner);
		release_lock_owner = TRUE;

		LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
			"LOCK Existing lock owner", data->current_entry,
			lock_owner, &lock_desc);

		/* Get the client for this open owner */
		clientid = open_owner->so_owner.so_nfs4_owner.so_clientrec;
		inc_client_id_ref(clientid);
	}

 check_seqid:

	/* Check seqid (lock_seqid or open_seqid) */
	if (data->minorversion == 0) {
		if (!Check_nfs4_seqid(resp_owner,
				      seqid,
				      op,
				      data->current_entry,
				      resp,
				      lock_tag)) {
			/* Response is all setup for us and LogDebug
			 * told what was wrong
			 */
			goto out2;
		}
	}

	/* Lock length should not be 0 */
	if (arg_LOCK4->length == 0LL) {
		res_LOCK4->status = NFS4ERR_INVAL;
		LogDebug(COMPONENT_NFS_V4_LOCK, "LOCK failed length == 0");
		goto out;
	}

	/* Check for range overflow.  Comparing beyond 2^64 is not
	 * possible int 64 bits precision, but off+len > 2^64-1 is
	 * equivalent to len > 2^64-1 - off
	 */

	if (lock_desc.lock_length >
	    (STATE_LOCK_OFFSET_EOF - lock_desc.lock_start)) {
		res_LOCK4->status = NFS4ERR_INVAL;
		LogDebug(COMPONENT_NFS_V4_LOCK, "LOCK failed length overflow");
		goto out;
	}

	/* Check if open state has correct access for type of lock.
	 *
	 * Don't need to check for conflicting states since this open
	 * state assures there are no conflicting states.
	 */
	if (((arg_LOCK4->locktype == WRITE_LT ||
	      arg_LOCK4->locktype == WRITEW_LT)
	      &&
	      ((state_open->state_data.share.share_access &
		OPEN4_SHARE_ACCESS_WRITE) == 0))
	    ||
	    ((arg_LOCK4->locktype == READ_LT ||
	      arg_LOCK4->locktype == READW_LT)
	      &&
	      ((state_open->state_data.share.share_access &
		OPEN4_SHARE_ACCESS_READ) == 0))) {
		/* The open state doesn't allow access based on the
		 * type of lock
		 */
		LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
			"LOCK failed, SHARE doesn't allow access",
			data->current_entry, lock_owner, &lock_desc);

		res_LOCK4->status = NFS4ERR_OPENMODE;

		goto out;
	}

	/* Do grace period checking */
	if (!fsal_grace() && nfs_in_grace() && !arg_LOCK4->reclaim) {
		LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
			"LOCK failed, non-reclaim while in grace",
			data->current_entry, lock_owner, &lock_desc);
		res_LOCK4->status = NFS4ERR_GRACE;
		goto out;
	}

	if (!fsal_grace() && nfs_in_grace()
	    && arg_LOCK4->reclaim
	    && !clientid->cid_allow_reclaim) {
		LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
			"LOCK failed, invalid reclaim while in grace",
			data->current_entry, lock_owner, &lock_desc);
		res_LOCK4->status = NFS4ERR_NO_GRACE;
		goto out;
	}

	if (!fsal_grace() && !nfs_in_grace() && arg_LOCK4->reclaim) {
		LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
			"LOCK failed, reclaim while not in grace",
			data->current_entry, lock_owner, &lock_desc);
		res_LOCK4->status = NFS4ERR_NO_GRACE;
		goto out;
	}

	if (arg_LOCK4->locker.new_lock_owner) {
		bool_t isnew;

		/* A lock owner is always associated with a previously
		   made open which has itself a previously made
		   stateid */

		/* This lock owner is not known yet, allocated and set
		   up a new one */
		lock_owner = create_nfs4_owner(&owner_name,
					       clientid,
					       STATE_LOCK_OWNER_NFSV4,
					       open_owner,
					       0,
					       &isnew,
					       CARE_ALWAYS);

		if (lock_owner == NULL) {
			res_LOCK4->status = NFS4ERR_RESOURCE;
			LogLock(COMPONENT_NFS_V4_LOCK, NIV_EVENT,
				"LOCK failed to create new lock owner",
				data->current_entry, open_owner, &lock_desc);
			goto out2;
		}

		release_lock_owner = TRUE;

		if (!isnew) {
			pthread_mutex_lock(&lock_owner->so_mutex);
			/* Check lock_seqid if it has attached locks. */
			if (!glist_empty(&lock_owner->so_lock_list)
			    && (data->minorversion == 0)
			    && !Check_nfs4_seqid(lock_owner,
						 arg_LOCK4->locker.locker4_u.
						     open_owner.lock_seqid,
						 op,
						 data->current_entry,
						 resp,
						 lock_tag)) {
				LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
					"LOCK failed to create new lock owner, re-use",
					data->current_entry,
					open_owner, &lock_desc);
				dump_all_locks(
					"All locks (re-use of lock owner)");

				pthread_mutex_unlock(&lock_owner->so_mutex);
				/* Response is all setup for us and
				 * LogDebug told what was wrong
				 */
				goto out2;
			}

			pthread_mutex_unlock(&lock_owner->so_mutex);

		}

		/* Prepare state management structure */
		memset(&candidate_data, 0, sizeof(candidate_data));
		candidate_data.lock.openstate = state_open;

		/* Add the lock state to the lock table */
		state_status = state_add(data->current_entry,
					 candidate_type,
					 &candidate_data,
					 lock_owner,
					 &lock_state,
					 data->minorversion > 0 ?
						&refer : NULL);

		if (state_status != STATE_SUCCESS) {
			res_LOCK4->status = NFS4ERR_RESOURCE;

			LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
				"LOCK failed to add new stateid",
				data->current_entry, lock_owner, &lock_desc);

			goto out2;
		}

		glist_init(&lock_state->state_data.lock.state_locklist);

		/* Attach this lock to an export */
		lock_state->state_export = op_ctx->export;

		PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);

		glist_add_tail(&op_ctx->export->exp_state_list,
			       &lock_state->state_export_list);

		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);

		/* Add lock state to the list of lock states belonging
		   to the open state */
		glist_add_tail(&state_open->state_data.share.share_lockstates,
			       &lock_state->state_data.lock.state_sharelist);
	}

	if (data->minorversion == 0) {
		op_ctx->clientid =
		    &lock_owner->so_owner.so_nfs4_owner.so_clientid;
	}

	/* Now we have a lock owner and a stateid.  Go ahead and push
	   lock into SAL (and FSAL). */
	state_status = state_lock(data->current_entry,
				  lock_owner,
				  lock_state,
				  blocking,
				  NULL,	/* No block data for now */
				  &lock_desc,
				  &conflict_owner,
				  &conflict_desc,
				  POSIX_LOCK);

	if (state_status != STATE_SUCCESS) {
		if (state_status == STATE_LOCK_CONFLICT) {
			/* A conflicting lock from a different
			   lock_owner, returns NFS4ERR_DENIED */
			Process_nfs4_conflict(&res_LOCK4->LOCK4res_u.denied,
					      conflict_owner,
					      &conflict_desc);
		}

		LogDebug(COMPONENT_NFS_V4_LOCK, "LOCK failed with status %s",
			 state_err_str(state_status));

		res_LOCK4->status = nfs4_Errno_state(state_status);

		/* Save the response in the lock or open owner */
		if (res_LOCK4->status != NFS4ERR_RESOURCE
		    && res_LOCK4->status != NFS4ERR_BAD_STATEID
		    && data->minorversion == 0) {
			Copy_nfs4_state_req(resp_owner,
					    seqid,
					    op,
					    data->current_entry,
					    resp,
					    lock_tag);
		}

		if (arg_LOCK4->locker.new_lock_owner) {
			/* Need to destroy new state */
			state_del(lock_state, false);
		}
		goto out2;
	}

	if (data->minorversion == 0)
		op_ctx->clientid = NULL;

	res_LOCK4->status = NFS4_OK;

	/* Handle stateid/seqid for success */
	update_stateid(lock_state,
		       &res_LOCK4->LOCK4res_u.resok4.lock_stateid,
		       data,
		       lock_tag);

	if (arg_LOCK4->locker.new_lock_owner) {
		/* Also save the response in the lock owner */
		Copy_nfs4_state_req(lock_owner,
				    arg_LOCK4->locker.locker4_u.open_owner.
				    lock_seqid,
				    op,
				    data->current_entry,
				    resp,
				    lock_tag);

	}

	LogFullDebug(COMPONENT_NFS_V4_LOCK,
		     "LOCK state_seqid = %u, lock_state = %p",
		     lock_state->state_seqid, lock_state);

	LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG, "LOCK applied",
		data->current_entry, lock_owner, &lock_desc);
 out:

	if (data->minorversion == 0) {
		/* Save the response in the lock or open owner */
		Copy_nfs4_state_req(resp_owner,
				    seqid,
				    op,
				    data->current_entry,
				    resp,
				    lock_tag);
	}

 out2:

	if (release_open_owner)
		dec_state_owner_ref(open_owner);

	if (release_lock_owner)
		dec_state_owner_ref(lock_owner);

	if (clientid != NULL)
		dec_client_id_ref(clientid);

	return res_LOCK4->status;
}				/* nfs4_op_lock */

/**
 * @brief Free memory allocated for LOCK result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOCK operation.
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_lock_Free(nfs_resop4 *res)
{
	LOCK4res *resp = &res->nfs_resop4_u.oplock;

	if (resp->status == NFS4ERR_DENIED)
		Release_nfs4_denied(&resp->LOCK4res_u.denied);
}				/* nfs4_op_lock_Free */

void nfs4_op_lock_CopyRes(LOCK4res *res_dst, LOCK4res *res_src)
{
	if (res_src->status == NFS4ERR_DENIED)
		Copy_nfs4_denied(&res_dst->LOCK4res_u.denied,
				 &res_src->LOCK4res_u.denied);
}
