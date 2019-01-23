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
#include "fsal.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "gsh_list.h"
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

#define SUCCESS_RESP_SIZE (sizeof(nfsstat4) + sizeof(stateid4))

enum nfs_req_result nfs4_op_lock(struct nfs_argop4 *op,
				 compound_data_t *data,
				 struct nfs_resop4 *resp)
{
	/* Shorter alias for arguments */
	LOCK4args * const arg_LOCK4 = &op->nfs_argop4_u.oplock;
	open_to_lock_owner4 *arg_open_owner =
		&arg_LOCK4->locker.locker4_u.open_owner;
	/* Shorter alias for response */
	LOCK4res * const res_LOCK4 = &resp->nfs_resop4_u.oplock;
	/* Status code from state calls */
	state_status_t state_status = STATE_SUCCESS;
	/* Data for lock state to be created */
	union state_data candidate_data;
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
	/* Indicate if we let FSAL to handle requests during grace. */
	bool_t fsal_grace = false;
	bool_t have_grace_ref = false;
	int rc;
	struct fsal_obj_handle *obj = data->current_obj;
	bool state_lock_held = false;
	uint64_t maxfilesize =
	    op_ctx->fsal_export->exp_ops.fs_maxfilesize(op_ctx->fsal_export);

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Entering NFS v4 LOCK handler ----------------------");

	/* Initialize to sane starting values */
	resp->resop = NFS4_OP_LOCK;

	/* Before starting, make sure we have room for succeseful response so
	 * we don't have to undo a successful lock operation (that may not be
	 * reversible if it overlaps an existing lock).
	 */
	res_LOCK4->status = check_resp_room(data, SUCCESS_RESP_SIZE);

	if (res_LOCK4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Record the sequence info */
	if (data->minorversion > 0) {
		memcpy(refer.session,
		       data->session->session_id,
		       sizeof(sessionid4));
		refer.sequence = data->sequence;
		refer.slot = data->slotid;
	}

	res_LOCK4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (res_LOCK4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Convert lock parameters to internal types */
	switch (arg_LOCK4->locktype) {
	case READW_LT:
		blocking = STATE_NFSV4_BLOCKING;
		/* Fall through */

	case READ_LT:
		lock_desc.lock_type = FSAL_LOCK_R;
		break;

	case WRITEW_LT:
		blocking = STATE_NFSV4_BLOCKING;
		/* Fall through */

	case WRITE_LT:
		lock_desc.lock_type = FSAL_LOCK_W;
		break;

	default:
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "Invalid lock type");
		res_LOCK4->status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	lock_desc.lock_start = arg_LOCK4->offset;
	lock_desc.lock_sle_type = FSAL_POSIX_LOCK;
	lock_desc.lock_reclaim = arg_LOCK4->reclaim;

	if (arg_LOCK4->length != STATE_LOCK_OFFSET_EOF)
		lock_desc.lock_length = arg_LOCK4->length;
	else
		lock_desc.lock_length = 0;

	if (arg_LOCK4->locker.new_lock_owner) {
		/* Check stateid correctness and get pointer to state */
		nfs_status = nfs4_Check_Stateid(
			&arg_open_owner->open_stateid,
			obj,
			&state_open,
			data,
			STATEID_SPECIAL_FOR_LOCK,
			arg_open_owner->open_seqid,
			data->minorversion == 0,
			lock_tag);

		if (nfs_status != NFS4_OK) {
			if (nfs_status == NFS4ERR_REPLAY) {
				open_owner = get_state_owner_ref(state_open);

				LogStateOwner("Open: ", open_owner);

				if (open_owner != NULL) {
					resp_owner = open_owner;
					seqid = arg_LOCK4->locker.locker4_u
						.open_owner.open_seqid;
					goto check_seqid;
				}
			}

			res_LOCK4->status = nfs_status;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed nfs4_Check_Stateid for open owner");
			return NFS_REQ_ERROR;
		}

		open_owner = get_state_owner_ref(state_open);

		LogStateOwner("Open: ", open_owner);

		if (open_owner == NULL) {
			/* State is going stale. */
			res_LOCK4->status = NFS4ERR_STALE;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed nfs4_Check_Stateid, stale open owner");
			goto out2;
		}

		lock_state = NULL;
		lock_owner = NULL;
		resp_owner = open_owner;
		seqid = arg_open_owner->open_seqid;

		LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
			"LOCK New lock owner from open owner",
			obj, open_owner, &lock_desc);

		/* Check is the clientid is known or not */
		rc = nfs_client_id_get_confirmed(
			data->minorversion == 0
				? arg_open_owner->lock_owner.clientid
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
			char str_open[LOG_BUFF_LEN / 2] = "\0";
			struct display_buffer dspbuf_open = {
				sizeof(str_open), str_open, str_open};
			char str_lock[LOG_BUFF_LEN / 2] = "\0";
			struct display_buffer dspbuf_lock = {
				sizeof(str_lock), str_lock, str_lock};

			display_client_id_rec(&dspbuf_open,
					      open_owner->so_owner
					      .so_nfs4_owner.so_clientrec);
			display_client_id_rec(&dspbuf_lock, clientid);

			LogDebug(COMPONENT_CLIENTID,
				 "Unexpected, new lock owner clientid {%s} doesn't match open owner clientid {%s}",
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
		convert_nfs4_lock_owner(&arg_open_owner->lock_owner,
					&owner_name);
		LogStateOwner("Lock: ", lock_owner);
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
			obj,
			&lock_state,
			data,
			STATEID_SPECIAL_FOR_LOCK,
			arg_LOCK4->locker.locker4_u.lock_owner.lock_seqid,
			data->minorversion == 0,
			lock_tag);

		if (nfs_status != NFS4_OK) {
			if (nfs_status == NFS4ERR_REPLAY) {
				lock_owner = get_state_owner_ref(lock_state);

				LogStateOwner("Lock: ", lock_owner);

				if (lock_owner != NULL) {
					open_owner = lock_owner->so_owner
						.so_nfs4_owner.so_related_owner;
					inc_state_owner_ref(open_owner);
					resp_owner = lock_owner;
					seqid = arg_LOCK4->locker.locker4_u
						.lock_owner.lock_seqid;
					goto check_seqid;
				}
			}

			res_LOCK4->status = nfs_status;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed nfs4_Check_Stateid for existing lock owner");
			return NFS_REQ_ERROR;
		}

		/* Check if lock state belongs to same export */
		if (!state_same_export(lock_state, op_ctx->ctx_export)) {
			LogEvent(COMPONENT_STATE,
				 "Lock Owner Export Conflict, Lock held for export %"
				 PRIu16" request for export %"PRIu16,
				 state_export_id(lock_state),
				 op_ctx->ctx_export->export_id);
			res_LOCK4->status = NFS4ERR_INVAL;
			goto out2;
		}

		/* A lock state has been found. Check its type */
		if (lock_state->state_type != STATE_TYPE_LOCK) {
			res_LOCK4->status = NFS4ERR_BAD_STATEID;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed existing lock owner,  state type is not LOCK");
			goto out2;
		}

		/* Get the old lockowner. We can do the following
		 * 'cast', in NFSv4 lock_owner4 and open_owner4 are
		 * different types but with the same definition
		 */
		lock_owner = get_state_owner_ref(lock_state);

		LogStateOwner("Lock: ", lock_owner);

		if (lock_owner == NULL) {
			/* State is going stale. */
			res_LOCK4->status = NFS4ERR_STALE;
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "LOCK failed nfs4_Check_Stateid, stale open owner");
			goto out2;
		}

		open_owner =
			lock_owner->so_owner.so_nfs4_owner.so_related_owner;
		LogStateOwner("Open: ", open_owner);
		inc_state_owner_ref(open_owner);
		state_open = lock_state->state_data.lock.openstate;
		inc_state_t_ref(state_open);
		resp_owner = lock_owner;
		seqid = arg_LOCK4->locker.locker4_u.lock_owner.lock_seqid;

		LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
			"LOCK Existing lock owner", obj,
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
				      obj,
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
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "LOCK failed length overflow start %"PRIx64
			 " length %"PRIx64,
			 lock_desc.lock_start, lock_desc.lock_length);
		goto out;
	}

	/* Check for range overflow past maxfilesize.  Comparing beyond 2^64 is
	 * not possible in 64 bits precision, but off+len > maxfilesize is
	 * equivalent to len > maxfilesize - off
	 */
	if (lock_desc.lock_length > (maxfilesize - lock_desc.lock_start)) {
		res_LOCK4->status = NFS4ERR_BAD_RANGE;
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "LOCK failed past maxfilesize %"PRIx64" start %"PRIx64
			 " length %"PRIx64,
			 maxfilesize,
			 lock_desc.lock_start, lock_desc.lock_length);
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
			obj, lock_owner, &lock_desc);

		res_LOCK4->status = NFS4ERR_OPENMODE;

		goto out;
	}


	fsal_grace = op_ctx->fsal_export->exp_ops.fs_supports(
					op_ctx->fsal_export, fso_grace_method);

	/* Do grace period checking (use resp_owner below since a new
	 * lock request with a new lock owner doesn't have a lock owner
	 * yet, but does have an open owner - resp_owner is always one or
	 * the other and non-NULL at this point - so makes for a better log).
	 */
	if (!fsal_grace) {
		if (arg_LOCK4->reclaim) {
			if (!clientid->cid_allow_reclaim) {
				LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
					"LOCK failed, invalid reclaim while in grace",
					obj, resp_owner, &lock_desc);
				res_LOCK4->status = NFS4ERR_NO_GRACE;
				goto out;
			}
			if (!nfs_get_grace_status(true)) {
				LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
					"LOCK failed, reclaim while not in grace",
					obj, resp_owner, &lock_desc);
				res_LOCK4->status = NFS4ERR_NO_GRACE;
				goto out;
			}
		} else {
			if (!nfs_get_grace_status(false)) {
				LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
					"LOCK failed, non-reclaim while in grace",
					obj, resp_owner, &lock_desc);
				res_LOCK4->status = NFS4ERR_GRACE;
				goto out;
			}
		}
		have_grace_ref = true;
	}

	/* Test if this request is attempting to create a new lock owner */
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
					       CARE_ALWAYS, true);

		LogStateOwner("Lock: ", lock_owner);

		if (lock_owner == NULL) {
			res_LOCK4->status = NFS4ERR_RESOURCE;
			LogLock(COMPONENT_NFS_V4_LOCK, NIV_EVENT,
				"LOCK failed to create new lock owner",
				obj, open_owner, &lock_desc);
			goto out2;
		}

		if (!isnew) {
			PTHREAD_MUTEX_lock(&lock_owner->so_mutex);
			/* Check lock_seqid if it has attached locks. */
			if (!glist_empty(&lock_owner->so_lock_list)
			    && (data->minorversion == 0)
			    && !Check_nfs4_seqid(lock_owner,
						 arg_open_owner->lock_seqid,
						 op,
						 obj,
						 resp,
						 lock_tag)) {
				LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
					"LOCK failed to create new lock owner, re-use",
					obj,
					open_owner, &lock_desc);
				dump_all_locks(
					"All locks (re-use of lock owner)");

				PTHREAD_MUTEX_unlock(&lock_owner->so_mutex);
				/* Response is all setup for us and
				 * LogDebug told what was wrong
				 */
				goto out2;
			}

			PTHREAD_MUTEX_unlock(&lock_owner->so_mutex);

			/* Lock owner is known, see if we also already have
			 * a stateid. Do this here since it's impossible for
			 * there to be such a state if the lock owner was
			 * previously unknown.
			 * This handles 4.0 replay locks with an open stateid
			 * and new_lock_owner == true. It also handles
			 * situations where all locks have been released and
			 * the client is claiming a new lock owner to get a
			 * new stateid, we will attempt to recycle.
			 */
			PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
			state_lock_held = true;
			lock_state = nfs4_State_Get_Obj(obj, lock_owner);
		} else {
			/* Take the state_lock now */
			PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
			state_lock_held = true;
		}

		if (lock_state == NULL) {
			/* Prepare state management structure */
			memset(&candidate_data, 0, sizeof(candidate_data));
			candidate_data.lock.openstate = state_open;

			/* Add the lock state to the lock table */
			state_status = state_add_impl(obj,
						      STATE_TYPE_LOCK,
						      &candidate_data,
						      lock_owner,
						      &lock_state,
						      data->minorversion > 0 ?
								&refer : NULL);

			if (state_status != STATE_SUCCESS) {
				res_LOCK4->status = NFS4ERR_RESOURCE;

				LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
					"LOCK failed to add new stateid",
					obj, lock_owner,
					&lock_desc);

				goto out2;
			}

			glist_init(&lock_state->state_data.lock.state_locklist);

			/* Add lock state to the list of lock states belonging
			   to the open state */
			glist_add_tail(
				&state_open->state_data.share.share_lockstates,
				&lock_state->state_data.lock.state_sharelist);

		}
	} else {
		/* Take the state_lock now */
		PTHREAD_RWLOCK_wrlock(&obj->state_hdl->state_lock);
		state_lock_held = true;
	}

	if (data->minorversion == 0) {
		op_ctx->clientid =
		    &lock_owner->so_owner.so_nfs4_owner.so_clientid;
	}

	/* Now we have a lock owner and a stateid.  Go ahead and push
	 * lock into SAL (and FSAL). */
	state_status = state_lock(obj,
				  lock_owner,
				  lock_state,
				  blocking,
				  NULL,	/* No block data for now */
				  &lock_desc,
				  &conflict_owner,
				  &conflict_desc);

	if (state_status != STATE_SUCCESS) {
		LogDebug(COMPONENT_NFS_V4_LOCK, "LOCK failed with status %s",
			 state_err_str(state_status));

		if (state_status == STATE_LOCK_CONFLICT) {
			/* A conflicting lock from a different lock_owner,
			 * returns NFS4ERR_DENIED, but check that the
			 * response will fit, if not, return response error.
			 */
			res_LOCK4->status = Process_nfs4_conflict(
						&res_LOCK4->LOCK4res_u.denied,
						conflict_owner,
						&conflict_desc,
						data);
		} else {
			res_LOCK4->status = nfs4_Errno_state(state_status);
		}

		/* Save the response in the lock or open owner */
		if (res_LOCK4->status != NFS4ERR_RESOURCE
		    && res_LOCK4->status != NFS4ERR_BAD_STATEID
		    && data->minorversion == 0) {
			Copy_nfs4_state_req(resp_owner,
					    seqid,
					    op,
					    obj,
					    resp,
					    lock_tag);
		}

		if (arg_LOCK4->locker.new_lock_owner) {
			/* Need to destroy new state */
			state_del_locked(lock_state);
		}

		goto out2;
	}

	if (data->minorversion == 0)
		op_ctx->clientid = NULL;

	res_LOCK4->status = NFS4_OK;
	data->op_resp_size = SUCCESS_RESP_SIZE;

	/* Handle stateid/seqid for success */
	update_stateid(lock_state,
		       &res_LOCK4->LOCK4res_u.resok4.lock_stateid,
		       data,
		       lock_tag);

	if (arg_LOCK4->locker.new_lock_owner) {
		/* Also save the response in the lock owner */
		Copy_nfs4_state_req(lock_owner,
				    arg_open_owner->lock_seqid,
				    op,
				    obj,
				    resp,
				    lock_tag);

	}

	if (isFullDebug(COMPONENT_NFS_V4_LOCK)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_stateid(&dspbuf, lock_state);

		LogFullDebug(COMPONENT_NFS_V4_LOCK, "LOCK stateid %s", str);
	}

	LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG, "LOCK applied",
		obj, lock_owner, &lock_desc);

 out:

	if (data->minorversion == 0) {
		/* Save the response in the lock or open owner */
		Copy_nfs4_state_req(resp_owner,
				    seqid,
				    op,
				    obj,
				    resp,
				    lock_tag);
	}

 out2:
	if (have_grace_ref)
		nfs_put_grace_status();

	if (state_lock_held) {
		/* Now release the state_lock */
		PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
	}

	if (state_open != NULL)
		dec_state_t_ref(state_open);

	if (lock_state != NULL)
		dec_state_t_ref(lock_state);

	LogStateOwner("Open: ", open_owner);
	LogStateOwner("Lock: ", lock_owner);

	if (open_owner != NULL)
		dec_state_owner_ref(open_owner);

	if (lock_owner != NULL)
		dec_state_owner_ref(lock_owner);

	if (clientid != NULL)
		dec_client_id_ref(clientid);

	return nfsstat4_to_nfs_req_result(res_LOCK4->status);
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
