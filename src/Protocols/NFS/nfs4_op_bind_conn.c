/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017 Red Hat, Inc.
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
 * @file nfs4_op_bind_conn.c
 * @brief Routines used for managing the NFS4_OP_BIND_CONN_TO_SESSION operation
 */

#include "config.h"
#include "fsal.h"
#include "sal_functions.h"
#include "nfs_rpc_callback.h"
#include "nfs_convert.h"

/**
 * @brief the NFS4_OP_BIND_CONN_TO_SESSION operation
 *
 * @param[in]     op   nfs4_op arguments
 * @param[in,out] data Compound request's data
 * @param[out]    resp nfs4_op results
 *
 * @return per RFC5661, p. 492
 *
 * @see nfs4_Compound
 *
 */
int nfs4_op_bind_conn(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *resp)
{
	BIND_CONN_TO_SESSION4args * const arg_BIND_CONN_TO_SESSION4 =
	    &op->nfs_argop4_u.opbind_conn_to_session;
	BIND_CONN_TO_SESSION4res * const res_BIND_CONN_TO_SESSION4 =
	    &resp->nfs_resop4_u.opbind_conn_to_session;
	BIND_CONN_TO_SESSION4resok * const resok_BIND_CONN_TO_SESSION4 =
	    &res_BIND_CONN_TO_SESSION4->BIND_CONN_TO_SESSION4res_u.bctsr_resok4;
	nfs41_session_t *session;

	resp->resop = NFS4_OP_BIND_CONN_TO_SESSION;
	res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4_OK;

	if (data->minorversion == 0) {
		res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4ERR_INVAL;
		return res_BIND_CONN_TO_SESSION4->bctsr_status;
	}

	if (!nfs41_Session_Get_Pointer(arg_BIND_CONN_TO_SESSION4->bctsa_sessid,
				       &session)) {
		res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4ERR_BADSESSION;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "BIND_CONN_TO_SESSION returning status %s",
			    nfsstat4_to_str(
				res_BIND_CONN_TO_SESSION4->bctsr_status));

		return res_BIND_CONN_TO_SESSION4->bctsr_status;
	}

	/* session->refcount +1 */

	LogDebug(COMPONENT_SESSIONS,
		 "BIND_CONN_TO_SESSION session=%p", session);

	/* Check if lease is expired and reserve it */
	PTHREAD_MUTEX_lock(&session->clientid_record->cid_mutex);

	if (!reserve_lease(session->clientid_record)) {
		PTHREAD_MUTEX_unlock(&session->clientid_record->cid_mutex);

		dec_session_ref(session);
		res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4ERR_EXPIRED;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "BIND_CONN_TO_SESSION returning status %s",
			    nfsstat4_to_str(
				res_BIND_CONN_TO_SESSION4->bctsr_status));
		return res_BIND_CONN_TO_SESSION4->bctsr_status;
	}

	data->preserved_clientid = session->clientid_record;

	PTHREAD_MUTEX_unlock(&session->clientid_record->cid_mutex);

	/* By default, no DRC replay */
	data->use_slot_cached_result = false;

	/* Keep memory of the session in the COMPOUND's data and indicate no
	 * slot in use. We assume the server will never support UINT32_MAX + 1
	 * slots...
	 */
	data->session = session;
	data->slot = UINT32_MAX;

	memcpy(resok_BIND_CONN_TO_SESSION4->bctsr_sessid,
	       arg_BIND_CONN_TO_SESSION4->bctsa_sessid,
	       sizeof(resok_BIND_CONN_TO_SESSION4->bctsr_sessid));

	switch (arg_BIND_CONN_TO_SESSION4->bctsa_dir) {
	case CDFC4_FORE:
		resok_BIND_CONN_TO_SESSION4->bctsr_dir = CDFS4_FORE;
		break;
	case CDFC4_BACK:
		resok_BIND_CONN_TO_SESSION4->bctsr_dir = CDFS4_BACK;
		break;
	case CDFC4_FORE_OR_BOTH:
		resok_BIND_CONN_TO_SESSION4->bctsr_dir = CDFS4_BOTH;
		break;
	case CDFC4_BACK_OR_BOTH:
		resok_BIND_CONN_TO_SESSION4->bctsr_dir = CDFS4_BOTH;
		break;
	}

	resok_BIND_CONN_TO_SESSION4->bctsr_use_conn_in_rdma_mode =
			arg_BIND_CONN_TO_SESSION4->bctsa_use_conn_in_rdma_mode;

#if 0
	if (nfs_rpc_get_chan(session->clientid_record, 0) == NULL) {
		res_BIND_CONN_TO_SESSION4->BIND_CONN_TO_SESSION4res_u
					.bctsr_resok4.bctsr_status_flags |=
						SEQ4_STATUS_CB_PATH_DOWN;
	}
#endif

	/* If we were successful, stash the clientid in the request
	 * context.
	 */
	op_ctx->clientid = &data->session->clientid;

	/* Add the session connection */
	(void) check_session_conn(session, data, true);

	res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4_OK;
	return res_BIND_CONN_TO_SESSION4->bctsr_status;
}				/* nfs4_op_bind_conn */

/**
 * @brief Free memory allocated for BIND_CONN_TO_SESSION result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_BIND_CONN_TO_SESSION operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_nfs4_op_bind_conn_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
