// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * @file nfs4_op_sequence.c
 * @brief Routines used for managing the NFS4_OP_SEQUENCE operation
 */

#include "config.h"
#include "fsal.h"
#include "sal_functions.h"
#include "nfs_rpc_callback.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "nfs_proto_functions.h"

#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs4.h"
#endif

/**
 * @brief check whether request is real a replay
 *
 * Check whether current request is a replay of slot's last request,
 * by checking whether the operations are same.
 *
 *  @param[in]  data        Compound request's data
 *  @param[in]  rq_slot     Current request's slot
 *  @param[in]  slotid      current request's slotid
 *  @param[in]  req_seq_id  current request's sequence id
 *
 */

void check_replay_request(compound_data_t *data, nfs41_session_slot_t *rq_slot,
			  sequenceid4 req_seq_id, uint32_t slotid)
{
	nfs_opnum4 opcodes[NFS4_MAX_OPERATIONS] = { 0 };
	uint32_t req_xid, i, opcode_num;

	req_xid = data->req->rq_msg.rm_xid;
	if (rq_slot->last_req.seq_id != req_seq_id)
		return;

	opcode_num = get_nfs4_opcodes(data, opcodes, NFS4_MAX_OPERATIONS);

	if (opcode_num != rq_slot->last_req.opcode_num)
		goto errout;

	for (i = 0; i < opcode_num; i++)
		if (opcodes[i] != rq_slot->last_req.opcodes[i])
			goto errout;

	return;

errout:

	/* If this happens, maybe cause client hangs forever, and can not
	 * recover, unless restart ganesha.
	 * For example, if client use kernel 4.14.81 in https://kernel.org/,
	 * OP_SEQUENCE comes first, and then comes OP_GETATTR which share
	 * the same slot and sequenceid with the former OP_SEQUENCE, and
	 * ganesha reply NFS4ERR_RETRY_UNCACHED_REP. Then nfs-client will
	 * still send the OP_GETATTR with the same slot and sequenceid,
	 * and ganesha still reply NFS4ERR_RETRY_UNCACHED_REP, ..., this
	 * will last forever. This bug, i.e. different requests share the
	 * same slot and sequenceid, disapper in kernel 5.4.xx, fixed
	 * by some former version.
	 */
	if (likely(component_log_level[COMPONENT_SESSIONS] >= NIV_EVENT)) {
		char last_operations[NFS4_COMPOUND_OPERATIONS_STR_LEN] = "\0";
		char curr_operations[NFS4_COMPOUND_OPERATIONS_STR_LEN] = "\0";

		struct display_buffer last_buf = { sizeof(last_operations),
						   last_operations,
						   last_operations };
		struct display_buffer curr_buf = { sizeof(curr_operations),
						   curr_operations,
						   curr_operations };

		display_nfs4_operations(&last_buf, rq_slot->last_req.opcodes,
					rq_slot->last_req.opcode_num);
		display_nfs4_operations(&curr_buf, opcodes, opcode_num);

		LogEvent(
			COMPONENT_SESSIONS,
			"Not a replay request, maybe caused by nfs-client's bug, please try upgrade the nfs-client's kernel");
		LogEvent(COMPONENT_SESSIONS,
			 "Last request %s slotid %" PRIu32 " seqid %" PRIu32
			 " xid %" PRIu32 " finish time_ms %" PRIu64,
			 last_operations, slotid, rq_slot->last_req.seq_id,
			 rq_slot->last_req.xid,
			 rq_slot->last_req.finish_time_ms);
		LogEvent(COMPONENT_SESSIONS,
			 "Current request %s slotid %" PRIu32 " seqid %" PRIu32
			 " xid %" PRIu32,
			 curr_operations, slotid, req_seq_id, req_xid);
	}
}

/**
 * @brief the NFS4_OP_SEQUENCE operation
 *
 * @param[in]     op   nfs4_op arguments
 * @param[in,out] data Compound request's data
 * @param[out]    resp nfs4_op results
 *
 * @return per RFC5661, p. 374
 *
 * @see nfs4_Compound
 *
 */
enum nfs_req_result nfs4_op_sequence(struct nfs_argop4 *op,
				     compound_data_t *data,
				     struct nfs_resop4 *resp)
{
	SEQUENCE4args *const arg_SEQUENCE4 = &op->nfs_argop4_u.opsequence;
	SEQUENCE4res *const res_SEQUENCE4 = &resp->nfs_resop4_u.opsequence;
	uint32_t slotid;
	uint32_t request_size;

	nfs41_session_t *session;
	nfs41_session_slot_t *sess_slot;

	GSH_AUTO_TRACEPOINT(
		nfs4, op_sequence_start, TRACE_INFO,
		"SEQUENCE arg: session={} seq={} slotid={} highest_slotid={} cachethis={}",
		TP_SESSION(arg_SEQUENCE4->sa_sessionid),
		arg_SEQUENCE4->sa_sequenceid, arg_SEQUENCE4->sa_slotid,
		arg_SEQUENCE4->sa_highest_slotid, arg_SEQUENCE4->sa_cachethis);

	resp->resop = NFS4_OP_SEQUENCE;
	res_SEQUENCE4->sr_status = NFS4_OK;

	if (data->minorversion == 0) {
		res_SEQUENCE4->sr_status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	if (!nfs41_Session_Get_Pointer(arg_SEQUENCE4->sa_sessionid, &session)) {
		res_SEQUENCE4->sr_status = NFS4ERR_BADSESSION;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));

		return NFS_REQ_ERROR;
	}

	/* session->refcount +1 */

	LogDebug(COMPONENT_SESSIONS, "SEQUENCE session=%p", session);

	/* Check if lease is expired and reserve it */
	if (!reserve_lease_or_expire(session->clientid_record, false, NULL)) {
		dec_session_ref(session);
		res_SEQUENCE4->sr_status = NFS4ERR_EXPIRED;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));
		return NFS_REQ_ERROR;
	}

	data->preserved_clientid = session->clientid_record;
#ifdef USE_MACOS_QUIRKS
	/* @todo Remove with the last macOS quirk. */
	op_ctx->client_is_macos = session->clientid_record->cid_macos;
#endif

	/* Take a reference on the clientid to prevent it from being freed
	 * during compound execution.
	 */
	inc_client_id_ref(data->preserved_clientid);

	/* Check if request is compliant with ca_maxrequestsize */
	request_size = xdr_getpos(data->req->rq_xdrs);

	if (request_size > session->fore_channel_attrs.ca_maxrequestsize) {
		res_SEQUENCE4->sr_status = NFS4ERR_REQ_TOO_BIG;
		LogDebugAlt(
			COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			"SEQUENCE returning status %s, request size %" PRIu32
			" exceeds ca_maxrequestsize %" PRIu32,
			nfsstat4_to_str(res_SEQUENCE4->sr_status), request_size,
			session->fore_channel_attrs.ca_maxrequestsize);
		dec_session_ref(session);
		return NFS_REQ_ERROR;
	}

	slotid = arg_SEQUENCE4->sa_slotid;

	/* Check is slot is compliant with ca_maxrequests */
	if (slotid >= session->fore_channel_attrs.ca_maxrequests) {
		dec_session_ref(session);
		res_SEQUENCE4->sr_status = NFS4ERR_BADSLOT;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));
		return NFS_REQ_ERROR;
	}

	sess_slot = &session->fc_slots[slotid];

	/* Serialize use of this slot. */
	PTHREAD_MUTEX_lock(&sess_slot->slot_lock);

	if (sess_slot->in_use) {
		/* Somehow we have a race, cases:
		 *
		 * First, note that because of mutex, racing SEQUENCE ops on the
		 * same slot will be serialized, one will "win" and set in_use
		 * thus forcing all the others down this path. If in_use was
		 * cleared before this thread wakes up, then the slot cache
		 * has been updated, and the below logic will all work.
		 *
		 * sa_sequenceid < sess_slot->sequence:
		 *    Really we should not see a replay request once the client
		 *    has started a new request. So this should be invalid.
		 *
		 * sa_sequenceid == sess_slot->sequence:
		 *    This is a possibly legit replay request, but we haven't
		 *    finished processing the original request. Per RFC 8881
		 *    Section 2.6.10.2 (last paragraph) we should send
		 *    NFS4ERR_DELAY.
		 *
		 * Anything else:
		 *    Absolutely invalid.
		 */
		if (sess_slot->sequence != arg_SEQUENCE4->sa_sequenceid)
			res_SEQUENCE4->sr_status = NFS4ERR_SEQ_MISORDERED;
		else
			res_SEQUENCE4->sr_status = NFS4ERR_DELAY;

		LogDebugAlt(
			COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			"SEQUENCE in use, returning status %s with slot seqid=%"
			PRIu32 " op seqid=%" PRIu32,
			nfsstat4_to_str(res_SEQUENCE4->sr_status),
			sess_slot->sequence, arg_SEQUENCE4->sa_sequenceid);

		PTHREAD_MUTEX_unlock(&sess_slot->slot_lock);

		dec_session_ref(session);
		return NFS_REQ_ERROR;
	}

	if (sess_slot->sequence + 1 != arg_SEQUENCE4->sa_sequenceid) {
		/* This sequence is NOT the next sequence */
		if (sess_slot->sequence == arg_SEQUENCE4->sa_sequenceid) {
			check_replay_request(data, sess_slot,
					     arg_SEQUENCE4->sa_sequenceid,
					     slotid);
			/* But it is the previous sequence - this is a replay */
			if (sess_slot->cached_result != NULL) {
				int32_t refcnt;

				/* Free the reply allocated originally */
				release_nfs4_res_compound(
					data->res->res_compound4_extended);

				refcnt = atomic_inc_int32_t(
					&sess_slot->cached_result->res_refcnt);

				/* Copy the reply from the cache (the reference
				 * is already taken by SEQUENCE.
				 */
				data->res->res_compound4_extended =
					sess_slot->cached_result;

				LogFullDebugAlt(
					COMPONENT_SESSIONS, COMPONENT_CLIENTID,
					"Use sesson slot %" PRIu32
					"=%p for replay refcnt=%" PRIi32,
					slotid, sess_slot->cached_result,
					refcnt);

				PTHREAD_MUTEX_unlock(&sess_slot->slot_lock);

				dec_session_ref(session);
				return NFS_REQ_REPLAY;
			} else if (arg_SEQUENCE4->sa_sequenceid != 0) {
				/* Uncached replay - this should actually never
				 * happen because we always create an "uncached"
				 * response if sa_cachethis was false.
				 * If sa_sequenceid was 0, this was an
				 * improper sa_sequenceid of 0 on an unused
				 * slot, which is a misordered error not some
				 * weird uncached error.
				 */
				res_SEQUENCE4->sr_status =
					NFS4ERR_RETRY_UNCACHED_REP;
				LogCrit(COMPONENT_SESSIONS,
					"SEQUENCE returning unexpected NFS4ERR_RETRY_UNCACHED_REP with slot seqid=%"
					PRIu32 " op seqid=%" PRIu32,
					sess_slot->sequence,
					arg_SEQUENCE4->sa_sequenceid);
				PTHREAD_MUTEX_unlock(&sess_slot->slot_lock);

				dec_session_ref(session);
				return NFS_REQ_ERROR;
			}
		}

		PTHREAD_MUTEX_unlock(&sess_slot->slot_lock);

		dec_session_ref(session);
		res_SEQUENCE4->sr_status = NFS4ERR_SEQ_MISORDERED;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));
		return NFS_REQ_ERROR;
	}

	/* Keep memory of the session in the COMPOUND's data */
	data->session = session;

	/* Record the sequenceid and slotid in the COMPOUND's data */
	data->sequence = arg_SEQUENCE4->sa_sequenceid;
	data->slotid = slotid;

	/* Update the sequence id within the slot */
	sess_slot->sequence += 1;

	/* If the slot cache was in use, free it. We will either fill it with a
	 * cached response if sa_cachethis is true, otherwise we will build a
	 * special uncached response.
	 */
	release_slot(sess_slot);

	/* Set up the response */
	memcpy(res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sessionid,
	       arg_SEQUENCE4->sa_sessionid, NFS4_SESSIONID_SIZE);
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sequenceid =
		sess_slot->sequence;
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_slotid = slotid;
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_highest_slotid =
		session->nb_slots - 1;
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_target_highest_slotid =
		session->fore_channel_attrs.ca_maxrequests - 1;

	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags = 0;

	if (nfs_rpc_get_chan(session->clientid_record, 0) == NULL) {
		res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags |=
			SEQ4_STATUS_CB_PATH_DOWN;
	}

	LogFullDebug(COMPONENT_SESSIONS,
		     "Check revoked delegations flag for session %p, flag=%s",
		     session,
		     session->has_revoked_delegations ? "true" : "false");

	/* Check and update revoked delegation status for a session.
	 * This block verifies if the session's revoked delegation flag is set.
	 * If set it confirms whether revoked delegations remain for the client.
	 * It updates the SEQUENCE response flags accordingly and clears the
	 * session flag if no revoked delegations remain.
	 */
	if (session->has_revoked_delegations) {
		/* Check if there are actually any revoked delegations left
		 * for this client.
		 */
		bool has_revoked = has_revoked_delegations_for_client(
			session->clientid_record);

		if (has_revoked) {
			/* Set the flag to indicate there are revoked
			 * delegations.
			 */
			res_SEQUENCE4->SEQUENCE4res_u.sr_resok4
				.sr_status_flags |=
				SEQ4_STATUS_RECALLABLE_STATE_REVOKED;
			LogDebug(
				COMPONENT_SESSIONS,
				"Setting flag for session %p (clientid=0x%llx)",
				session, (unsigned long long)session->clientid);
		} else {
			/* No revoked delegations clear the session flag */
			session->has_revoked_delegations = false;
			LogDebug(COMPONENT_SESSIONS,
				 "Clear flag for session %p (clientid=0x%llx)",
				 session,
				 (unsigned long long)session->clientid);
		}
	} else {
		LogDebug(COMPONENT_SESSIONS,
			 "Not setting the flag for session %p", session);
	}

	/* Remember if we are caching result and set position to cache. */
	data->sa_cachethis = arg_SEQUENCE4->sa_cachethis;
	data->cd_slot = sess_slot;

	LogFullDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			"%s session slot %" PRIu32 "=%p for DRC",
			arg_SEQUENCE4->sa_cachethis ? "Use" : "Don't use",
			slotid, sess_slot);

	/* If we were successful, stash the clientid in the request
	 * context.
	 */
	op_ctx->clientid = &data->session->clientid;

	/* Protect the slot so we can release the slot_lock, even if we fail
	 * the response room check, we will still want to cache that result.
	 * so we want to do this now.
	 */
	sess_slot->in_use = true;

	PTHREAD_MUTEX_unlock(&sess_slot->slot_lock);

	/* Now check the response size (we check here because we couldn't check
	 * in nfs4_Compound because the session wasn't established yet).
	 */
	res_SEQUENCE4->sr_status = check_resp_room(data, data->op_resp_size);

	if (res_SEQUENCE4->sr_status != NFS4_OK) {
		/* Indicate the failed response size. Note that we will cache
		 * this response if sa_cachethis is true. If sa_cachethis
		 * is not true, we will "cache" an "uncached" response. Don't
		 * drop the session reference, we need it to cache the response.
		 */
		data->op_resp_size = sizeof(nfsstat4);
		return NFS_REQ_ERROR;
	}

	(void)check_session_conn(session, data, true);

	const SEQUENCE4resok *const reok =
		&res_SEQUENCE4->SEQUENCE4res_u.sr_resok4;
	GSH_AUTO_TRACEPOINT(
		nfs4, op_sequence_end, TRACE_INFO,
		"SEQUENCE res: status={} session={} seq={} slotid={} status_flags={}",
		res_SEQUENCE4->sr_status, TP_SESSION(reok->sr_sessionid),
		reok->sr_sequenceid, reok->sr_slotid, reok->sr_status_flags);
	return NFS_REQ_OK;
} /* nfs41_op_sequence */

/**
 * @brief Free memory allocated for SEQUENCE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SEQUENCE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_sequence_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
