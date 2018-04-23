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
int nfs4_op_sequence(struct nfs_argop4 *op, compound_data_t *data,
		     struct nfs_resop4 *resp)
{
	SEQUENCE4args * const arg_SEQUENCE4 = &op->nfs_argop4_u.opsequence;
	SEQUENCE4res * const res_SEQUENCE4 = &resp->nfs_resop4_u.opsequence;
	nfs41_session_t *session;
	nfs41_session_slot_t *slot;

	resp->resop = NFS4_OP_SEQUENCE;
	res_SEQUENCE4->sr_status = NFS4_OK;

	if (data->minorversion == 0) {
		res_SEQUENCE4->sr_status = NFS4ERR_INVAL;
		return res_SEQUENCE4->sr_status;
	}

	/* OP_SEQUENCE is always the first operation of the request */
	if (data->oppos != 0) {
		res_SEQUENCE4->sr_status = NFS4ERR_SEQUENCE_POS;
		return res_SEQUENCE4->sr_status;
	}

	if (!nfs41_Session_Get_Pointer(arg_SEQUENCE4->sa_sessionid, &session)) {
		res_SEQUENCE4->sr_status = NFS4ERR_BADSESSION;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));

		return res_SEQUENCE4->sr_status;
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
		return res_SEQUENCE4->sr_status;
	}

	data->preserved_clientid = session->clientid_record;

	PTHREAD_MUTEX_unlock(&session->clientid_record->cid_mutex);

	/* Check is slot is compliant with ca_maxrequests */
	if (arg_SEQUENCE4->sa_slotid >=
	    session->fore_channel_attrs.ca_maxrequests) {
		dec_session_ref(session);
		res_SEQUENCE4->sr_status = NFS4ERR_BADSLOT;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));
		return res_SEQUENCE4->sr_status;
	}

	/* By default, no DRC replay */
	data->use_drc = false;
	slot = &session->fc_slots[arg_SEQUENCE4->sa_slotid];

	/* Serialize use of this slot. */
	PTHREAD_MUTEX_lock(&slot->lock);

	if (slot->sequence + 1 != arg_SEQUENCE4->sa_sequenceid) {
		if (slot->sequence == arg_SEQUENCE4->sa_sequenceid) {
#if IMPLEMENT_CACHETHIS
			/** @todo
			 *
			 * Ganesha always caches result anyway so ignore
			 * cachethis
			 */
			if (slot->cache_used) {
#endif
				/* Replay operation through the DRC */
				data->use_drc = true;
				data->cached_res = &slot->cached_result;

				LogFullDebugAlt(COMPONENT_SESSIONS,
						COMPONENT_CLIENTID,
						"Use sesson slot %" PRIu32
						"=%p for DRC",
						arg_SEQUENCE4->sa_slotid,
						data->cached_res);

				PTHREAD_MUTEX_unlock(&slot->lock);
				dec_session_ref(session);
				res_SEQUENCE4->sr_status = NFS4_OK;
				return res_SEQUENCE4->sr_status;
#if IMPLEMENT_CACHETHIS
			} else {
				/* Illegal replay */
				PTHREAD_MUTEX_unlock(&slot->lock);
				dec_session_ref(session);
				res_SEQUENCE4->sr_status =
				    NFS4ERR_RETRY_UNCACHED_REP;
				LogDebugAlt(COMPONENT_SESSIONS,
					    COMPONENT_CLIENTID,
					    "SEQUENCE returning status %s",
					    nfsstat4_to_str(
						res_SEQUENCE4->sr_status));
				return res_SEQUENCE4->sr_status;
			}
#endif
		}

		PTHREAD_MUTEX_unlock(&slot->lock);
		dec_session_ref(session);
		res_SEQUENCE4->sr_status = NFS4ERR_SEQ_MISORDERED;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "SEQUENCE returning status %s",
			    nfsstat4_to_str(res_SEQUENCE4->sr_status));
		return res_SEQUENCE4->sr_status;
	}

	/* Keep memory of the session in the COMPOUND's data */
	data->session = session;

	/* Record the sequenceid and slotid in the COMPOUND's data */
	data->sequence = arg_SEQUENCE4->sa_sequenceid;
	data->slot = arg_SEQUENCE4->sa_slotid;

	/* Update the sequence id within the slot */
	slot->sequence += 1;

	memcpy(res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sessionid,
	       arg_SEQUENCE4->sa_sessionid, NFS4_SESSIONID_SIZE);
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sequenceid = slot->sequence;
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_slotid =
	    arg_SEQUENCE4->sa_slotid;
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_highest_slotid =
	    session->nb_slots - 1;
	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_target_highest_slotid =
	    session->fore_channel_attrs.ca_maxrequests - 1;

	res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags = 0;

	if (nfs_rpc_get_chan(session->clientid_record, 0) == NULL) {
		res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags |=
		    SEQ4_STATUS_CB_PATH_DOWN;
	}

#if IMPLEMENT_CACHETHIS
/* Ganesha always caches result anyway so ignore cachethis */
	if (arg_SEQUENCE4->sa_cachethis) {
#endif
		data->cached_res = &slot->cached_result;
		slot->cache_used = true;

		LogFullDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
				"Use sesson slot %" PRIu32 "=%p for DRC",
				arg_SEQUENCE4->sa_slotid, data->cached_res);
#if IMPLEMENT_CACHETHIS
	} else {
		data->cached_res = NULL;
		slot->cache_used = false;

		LogFullDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
				"Don't use sesson slot %" PRIu32
				"=NULL for DRC", arg_SEQUENCE4->sa_slotid);
	}
#endif

	/* If we were successful, stash the clientid in the request
	 * context.
	 */
	op_ctx->clientid = &data->session->clientid;

	res_SEQUENCE4->sr_status = NFS4_OK;
	return res_SEQUENCE4->sr_status;
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
