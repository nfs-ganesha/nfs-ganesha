// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "nfs_proto_functions.h"

/**
 * @brief Bind connection to the session's backchannel
 */
static nfsstat4 bind_conn_to_session_backchannel(SVCXPRT *rq_xprt,
	nfs41_session_t *session)
{
	char session_str[NFS4_SESSIONID_BUFFER_SIZE] = "\0";
	struct display_buffer db_session = {
		sizeof(session_str), session_str, session_str};
	char xprt_addr_str[SOCK_NAME_MAX] = "\0";
	struct display_buffer db_xprt = {
		sizeof(xprt_addr_str), xprt_addr_str, xprt_addr_str};

	display_session_id(&db_session, session->session_id);
	display_xprt_sockaddr(&db_xprt, rq_xprt);

	LogInfo(COMPONENT_SESSIONS,
		"Set up session: %s backchannel and bind it to current xprt FD: %d socket-address: %s",
		session_str, rq_xprt->xp_fd, xprt_addr_str);

	/* For state-protection other than SP4_NONE, there needs to be further
	 * validation (RFC 5661 section-2.10.8.3) before backchannel setup.
	 * Since Ganesha supports only SP4_NONE as of now, we skip processing
	 * of other mechanisms.
	 */
	if (session->clientid_record->cid_state_protect_how == SP4_NONE) {
		int rc;

		LogInfo(COMPONENT_SESSIONS,
			"Creating backchannel for session: %s", session_str);

		/* Create backchannel */
		rc = nfs_rpc_create_chan_v41(rq_xprt, session,
			session->cb_sec_parms.sec_parms_len,
			session->cb_sec_parms.sec_parms_val);

		if (unlikely(rc == EINVAL || rc == EPERM))
			return NFS4ERR_INVAL;

		if (unlikely(rc != 0))
			return NFS4ERR_SERVERFAULT;

		LogInfo(COMPONENT_SESSIONS,
			"Created backchannel for session: %s", session_str);
		return NFS4_OK;
	}
	/* We always set SP4_NONE during client-record creation */
	LogFatal(COMPONENT_SESSIONS,
		"Only SP4_NONE state protection is supported. Code flow should not reach here");
	return NFS4ERR_SERVERFAULT;
}

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
enum nfs_req_result nfs4_op_bind_conn(struct nfs_argop4 *op,
				      compound_data_t *data,
				      struct nfs_resop4 *resp)
{
	BIND_CONN_TO_SESSION4args * const arg_BIND_CONN_TO_SESSION4 =
	    &op->nfs_argop4_u.opbind_conn_to_session;
	BIND_CONN_TO_SESSION4res * const res_BIND_CONN_TO_SESSION4 =
	    &resp->nfs_resop4_u.opbind_conn_to_session;
	BIND_CONN_TO_SESSION4resok * const resok_BIND_CONN_TO_SESSION4 =
	    &res_BIND_CONN_TO_SESSION4->BIND_CONN_TO_SESSION4res_u.bctsr_resok4;
	nfs41_session_t *session;
	channel_dir_from_client4 client_channel_dir;
	channel_dir_from_server4 server_channel_dir;
	nfsstat4 bind_to_backchannel;
	bool added_conn_to_session;

	resp->resop = NFS4_OP_BIND_CONN_TO_SESSION;
	res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4_OK;

	if (data->minorversion == 0) {
		res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	if (!nfs41_Session_Get_Pointer(arg_BIND_CONN_TO_SESSION4->bctsa_sessid,
				       &session)) {
		res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4ERR_BADSESSION;
		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "BIND_CONN_TO_SESSION returning status %s",
			    nfsstat4_to_str(
				res_BIND_CONN_TO_SESSION4->bctsr_status));

		return NFS_REQ_ERROR;
	}

	/* session->refcount +1 */

	LogDebug(COMPONENT_SESSIONS,
		 "BIND_CONN_TO_SESSION session=%p", session);

	/* Check if lease is expired and reserve it */
	if (!reserve_lease_or_expire(session->clientid_record, false, NULL)) {
		dec_session_ref(session);
		res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4ERR_EXPIRED;

		LogDebugAlt(COMPONENT_SESSIONS, COMPONENT_CLIENTID,
			    "BIND_CONN_TO_SESSION returning status %s",
			    nfsstat4_to_str(
				res_BIND_CONN_TO_SESSION4->bctsr_status));

		return NFS_REQ_ERROR;
	}

	data->preserved_clientid = session->clientid_record;

	/* Keep memory of the session in the COMPOUND's data and indicate no
	 * slot in use. We assume the server will never support UINT32_MAX + 1
	 * slots...
	 */
	data->session = session;
	data->slotid = UINT32_MAX;

	/* Check and bind the connection to session */
	added_conn_to_session = check_session_conn(session, data, true);
	if (!added_conn_to_session) {
		LogWarn(COMPONENT_SESSIONS,
			"Unable to add connection to the session");
		res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	memcpy(resok_BIND_CONN_TO_SESSION4->bctsr_sessid,
	       arg_BIND_CONN_TO_SESSION4->bctsa_sessid,
	       sizeof(resok_BIND_CONN_TO_SESSION4->bctsr_sessid));

	client_channel_dir = arg_BIND_CONN_TO_SESSION4->bctsa_dir;

	switch (client_channel_dir) {
	case CDFC4_FORE:
		server_channel_dir = CDFS4_FORE;
		break;
	case CDFC4_BACK:
	case CDFC4_FORE_OR_BOTH:
	case CDFC4_BACK_OR_BOTH:
		bind_to_backchannel = bind_conn_to_session_backchannel(
			data->req->rq_xprt, session);

		if (bind_to_backchannel != NFS4_OK) {
			if (client_channel_dir == CDFC4_FORE_OR_BOTH) {
				/* Since it is not mandatory to bind connection
				 * to backchannel in this scenario, we return
				 * only successful forechannel creation.
				 */
				server_channel_dir = CDFS4_FORE;
				break;
			}
			LogCrit(COMPONENT_SESSIONS,
				"Mandatory backchannel creation failed");
			res_BIND_CONN_TO_SESSION4->bctsr_status =
				bind_to_backchannel;
			return NFS_REQ_ERROR;
		}
		if (client_channel_dir == CDFC4_BACK_OR_BOTH ||
			client_channel_dir == CDFC4_FORE_OR_BOTH) {
			server_channel_dir = CDFS4_BOTH;
			break;
		}
		server_channel_dir = CDFS4_BACK;
		break;
	}

	resok_BIND_CONN_TO_SESSION4->bctsr_dir = server_channel_dir;
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

	res_BIND_CONN_TO_SESSION4->bctsr_status = NFS4_OK;
	return NFS_REQ_OK;
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
