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
	SEQUENCE4args * const arg_SEQUENCE4 = &op->nfs_argop4_u.opsequence;
	SEQUENCE4res * const res_SEQUENCE4 = &resp->nfs_resop4_u.opsequence;
	uint32_t slotid;

	nfs41_session_t *session;
	nfs41_session_slot_t *slot;

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
	PTHREAD_MUTEX_lock(&session->clientid_record->cid_mutex);

	if (!reserve_lease(session->clientid_record)) {
		PTHREAD_MUTEX_unlock(&session->clientid_record->cid_mutex);

		dec_session_ref(session);
		res_SEQUENCE4->sr_status = NFS4ERR_EXPIRED;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));
		return NFS_REQ_ERROR;
	}

	data->preserved_clientid = session->clientid_record;

	PTHREAD_MUTEX_unlock(&session->clientid_record->cid_mutex);

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

	slot = &session->fc_slots[slotid];

	/* Serialize use of this slot. */
	PTHREAD_MUTEX_lock(&slot->lock);

	if (slot->sequence + 1 != arg_SEQUENCE4->sa_sequenceid) {
		/* This sequence is NOT the next sequence */
		if (slot->sequence == arg_SEQUENCE4->sa_sequenceid) {
			/* But it is the previous sequence */
			if (slot->cached_result != NULL) {
				int32_t refcnt;

				/* And has a cached response.
				 * Replay operation through the DRC
				 * Take a reference to the slot cached response.
				 */
				data->slot = slot;
				refcnt = atomic_inc_int32_t(
					&slot->cached_result->res_refcnt);

				LogFullDebugAlt(COMPONENT_SESSIONS,
						COMPONENT_CLIENTID,
						"Use sesson slot %" PRIu32
						"=%p for replay refcnt=%"PRIi32,
						slotid,
						slot->cached_result,
						refcnt);

				PTHREAD_MUTEX_unlock(&slot->lock);

				dec_session_ref(session);
				res_SEQUENCE4->sr_status = NFS4_OK;
				return NFS_REQ_REPLAY;
			} else {
				/* Illegal replay */
				PTHREAD_MUTEX_unlock(&slot->lock);

				dec_session_ref(session);
				res_SEQUENCE4->sr_status =
				    NFS4ERR_RETRY_UNCACHED_REP;
				LogDebugAlt(COMPONENT_SESSIONS,
					    COMPONENT_CLIENTID,
					    "SEQUENCE returning status %s with slot seqid=%"
					    PRIu32" op seqid=%"PRIu32,
					    nfsstat4_to_str(
						res_SEQUENCE4->sr_status),
					    slot->sequence,
					    arg_SEQUENCE4->sa_sequenceid);
				return NFS_REQ_ERROR;
			}
		}

		PTHREAD_MUTEX_unlock(&slot->lock);

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
	slot->sequence += 1;

	/* If the slot cache was in use, free it. */
	release_slot(slot);

	/* Set up the response */
	memcpy(res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sessionid,
	       arg_SEQUENCE4->sa_sessionid, NFS4_SESSIONID_SIZE);
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sequenceid = slot->sequence;
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

	/* Remember if we are caching result and set position to cache. */
	data->sa_cachethis = arg_SEQUENCE4->sa_cachethis;
	data->slot = slot;

	LogFullDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			"%s sesson slot %" PRIu32 "=%p for DRC",
			arg_SEQUENCE4->sa_cachethis
				? "Use"
				: "Don't use",
			slotid, data->slot);

	/* If we were successful, stash the clientid in the request
	 * context.
	 */
	op_ctx->clientid = &data->session->clientid;

	/* Now check the response size (we check here because we could't check
	 * in nfs4_Compound because the session wasn't established yet).
	 */
	res_SEQUENCE4->sr_status = check_resp_room(data, data->op_resp_size);

	if (res_SEQUENCE4->sr_status != NFS4_OK) {
		/* Indicate the failed response size. */
		data->op_resp_size = sizeof(nfsstat4);

		PTHREAD_MUTEX_unlock(&slot->lock);

		dec_session_ref(session);
		data->session = NULL;
		return NFS_REQ_ERROR;
	}

	/* We keep the slot lock to serialize use of the slot. */

	(void) check_session_conn(session, data, true);

	return NFS_REQ_OK;
}				/* nfs41_op_sequence */

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
