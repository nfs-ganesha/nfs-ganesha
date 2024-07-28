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
 * @file    nfs4_op_create_session.c
 * @brief   Routines used for managing the NFS4_OP_CREATE_SESSION operation.
 */

#include "config.h"
#include <pthread.h>
#include <assert.h>
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_rpc_callback.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"
#include "nfs_creds.h"
#include "client_mgr.h"
#include "fsal.h"

#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs4.h"
#endif

#define log_channel_attributes(component, chan_attrs, name) \
	LogFullDebug(component,					\
		     "%s attributes ca_headerpadsize %"PRIu32	\
		     " ca_maxrequestsize %"PRIu32		\
		     " ca_maxresponsesize %"PRIu32		\
		     " ca_maxresponsesize_cached %"PRIu32	\
		     " ca_maxoperations %"PRIu32		\
		     " ca_maxrequests %"PRIu32,			\
		     name,					\
		     (chan_attrs)->ca_headerpadsize,		\
		     (chan_attrs)->ca_maxrequestsize,		\
		     (chan_attrs)->ca_maxresponsesize,		\
		     (chan_attrs)->ca_maxresponsesize_cached,	\
		     (chan_attrs)->ca_maxoperations,		\
		     (chan_attrs)->ca_maxrequests)

/**
 * @brief Populate nfs41_session with callback params
 */
static void populate_callback_params_in_session(
	uint32_t sec_parms_len, const callback_sec_parms4 *sec_parms_val,
	uint32_t cb_program, nfs41_session_t *nfs41_session,
	log_components_t component)
{
	int sp_itr;
	callback_sec_parms4 * const extracted_sec_params = gsh_malloc(
		sec_parms_len * sizeof(callback_sec_parms4));

	for (sp_itr = 0; sp_itr < sec_parms_len; ++sp_itr) {
		const callback_sec_parms4 input_sp = sec_parms_val[sp_itr];
		callback_sec_parms4 * const curr_sp =
			&extracted_sec_params[sp_itr];

		curr_sp->cb_secflavor = input_sp.cb_secflavor;
		if (curr_sp->cb_secflavor == AUTH_NONE) {
			/* Do nothing */
		} else if (curr_sp->cb_secflavor == AUTH_SYS) {
			int gids_itr;
			size_t machname_len;
			struct authunix_parms curr_cb_sys_creds;
			struct authunix_parms input_cb_sys_creds =
				input_sp.callback_sec_parms4_u.cbsp_sys_cred;

			curr_cb_sys_creds.aup_uid = input_cb_sys_creds.aup_uid;
			curr_cb_sys_creds.aup_gid = input_cb_sys_creds.aup_gid;
			curr_cb_sys_creds.aup_time =
				input_cb_sys_creds.aup_time;

			/* Populate aup_machname */
			machname_len = strnlen(input_cb_sys_creds.aup_machname,
				MAX_MACHINE_NAME);
			curr_cb_sys_creds.aup_machname =
				gsh_malloc(machname_len + 1);
			memcpy(curr_cb_sys_creds.aup_machname,
				input_cb_sys_creds.aup_machname, machname_len);
			curr_cb_sys_creds.aup_machname[machname_len] = '\0';

			curr_cb_sys_creds.aup_len = input_cb_sys_creds.aup_len;
			curr_cb_sys_creds.aup_gids = gsh_malloc(
				curr_cb_sys_creds.aup_len * sizeof(gid_t));

			for (gids_itr = 0;
				gids_itr < input_cb_sys_creds.aup_len;
				++gids_itr) {
				curr_cb_sys_creds.aup_gids[gids_itr] =
					input_cb_sys_creds.aup_gids[gids_itr];
			}
			curr_sp->callback_sec_parms4_u.cbsp_sys_cred =
				curr_cb_sys_creds;

#ifdef _HAVE_GSSAPI
		} else if (curr_sp->cb_secflavor == RPCSEC_GSS) {
			LogWarn(component,
				"We do not support GSS callbacks, skip GSS callback setup");
#endif
		}
	}
	nfs41_session->cb_sec_parms.sec_parms_val = extracted_sec_params;
	nfs41_session->cb_sec_parms.sec_parms_len = sec_parms_len;
	nfs41_session->cb_program = cb_program;
}

/**
 *
 * @brief The NFS4_OP_CREATE_SESSION operation
 *
 * @param[in]     op    nfs4_op arguments
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  nfs4_op results
 *
 * @return Values as per RFC5661 p. 363
 *
 * @see nfs4_Compound
 *
 */

enum nfs_req_result nfs4_op_create_session(struct nfs_argop4 *op,
					   compound_data_t *data,
					   struct nfs_resop4 *resp)
{
	/* Result of looking up the clientid in the confirmed ID
	   table */
	nfs_client_id_t *conf = NULL;	/* XXX these are not good names */
	/* Result of looking up the clientid in the unconfirmed ID
	   table */
	nfs_client_id_t *unconf = NULL;
	/* The found clientid (either one of the preceding) */
	nfs_client_id_t *found = NULL;
	/* The found client record */
	nfs_client_record_t *client_record;
	/* The created session */
	nfs41_session_t *nfs41_session = NULL;
	/* Client supplied clientid */
	clientid4 clientid = 0;
	/* The client address as a string, for gratuitous logging */
	const char *str_client_addr = "(unknown)";
	/* The client name, for gratuitous logging */
	char str_client[CLIENTNAME_BUFSIZE];
	/* Display buffer for client name */
	struct display_buffer dspbuf_client = {
		sizeof(str_client), str_client, str_client};
	/* The clientid4 broken down into fields */
	char str_clientid4[DISPLAY_CLIENTID_SIZE];
	/* Display buffer for clientid4 */
	struct display_buffer dspbuf_clientid4 = {
		sizeof(str_clientid4), str_clientid4, str_clientid4};
	/* Return code from clientid calls */
	int i, rc = 0;
	/* Component for logging */
	log_components_t component = COMPONENT_CLIENTID;
	/* Abbreviated alias for arguments */
	CREATE_SESSION4args * const arg_CREATE_SESSION4 =
	    &op->nfs_argop4_u.opcreate_session;
	/* Abbreviated alias for response */
	CREATE_SESSION4res * const res_CREATE_SESSION4 =
	    &resp->nfs_resop4_u.opcreate_session;
	/* Abbreviated alias for successful response */
	CREATE_SESSION4resok * const res_CREATE_SESSION4ok =
	    &res_CREATE_SESSION4->CREATE_SESSION4res_u.csr_resok4;
	bool added_conn_to_session;

	/* Make sure str_client is always printable even
	 * if log level changes midstream.
	 */
	display_printf(&dspbuf_client, "(unknown)");
	display_reset_buffer(&dspbuf_client);

	if (op_ctx->client != NULL)
		str_client_addr = op_ctx->client->hostaddr_str;

	if (isDebug(COMPONENT_SESSIONS))
		component = COMPONENT_SESSIONS;

	resp->resop = NFS4_OP_CREATE_SESSION;
	res_CREATE_SESSION4->csr_status = NFS4_OK;
	clientid = arg_CREATE_SESSION4->csa_clientid;

	display_clientid(&dspbuf_clientid4, clientid);

	if (data->minorversion == 0) {
		res_CREATE_SESSION4->csr_status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	LogInfo(component,
		 "CREATE_SESSION client addr=%s clientid=%s -------------------",
		 str_client_addr, str_clientid4);

	/* First try to look up unconfirmed record */
	rc = nfs_client_id_get_unconfirmed(clientid, &unconf);

	if (rc == CLIENT_ID_SUCCESS) {
		client_record = unconf->cid_client_record;
		found = unconf;
	} else {
		rc = nfs_client_id_get_confirmed(clientid, &conf);
		if (rc != CLIENT_ID_SUCCESS) {
			/* No record whatsoever of this clientid */
			LogDebug(component,
				 "%s clientid=%s",
				 clientid_error_to_str(rc), str_clientid4);

			if (rc == CLIENT_ID_EXPIRED)
				rc = CLIENT_ID_STALE;

			res_CREATE_SESSION4->csr_status =
			    clientid_error_to_nfsstat_no_expire(rc);

			return NFS_REQ_ERROR;
		}

		client_record = conf->cid_client_record;
		found = conf;
	}

	PTHREAD_MUTEX_lock(&client_record->cr_mutex);

	inc_client_record_ref(client_record);

	if (isInfo(component)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_record(&dspbuf, client_record);

		LogInfo(component,
			     "Client Record %s cr_confirmed_rec=%p cr_unconfirmed_rec=%p",
			     str,
			     client_record->cr_confirmed_rec,
			     client_record->cr_unconfirmed_rec);
	}

	/* At this point one and only one of conf and unconf is
	 * non-NULL, and found also references the single clientid
	 * record that was found.
	 */

	LogDebug(component,
		 "CREATE_SESSION clientid=%s csa_sequence=%" PRIu32
		 " clientid_cs_seq=%" PRIu32
		 " data_oppos=%d",
		 str_clientid4, arg_CREATE_SESSION4->csa_sequence,
		 found->cid_create_session_sequence, data->oppos);

	if (isFullDebug(component)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_id_rec(&dspbuf, found);
		LogFullDebug(component, "Found %s", str);
	}

	if ((arg_CREATE_SESSION4->csa_sequence + 1) ==
	     found->cid_create_session_sequence) {
		*res_CREATE_SESSION4 = found->cid_create_session_slot;

		LogDebug(component,
			 "CREATE_SESSION special replay case, used response in cid_create_session_slot");
		goto out;
	} else if (arg_CREATE_SESSION4->csa_sequence !=
		   found->cid_create_session_sequence) {
		res_CREATE_SESSION4->csr_status = NFS4ERR_SEQ_MISORDERED;

		LogDebug(component,
			 "CREATE_SESSION returning NFS4ERR_SEQ_MISORDERED");
		goto out;
	}

	if (unconf != NULL) {
		/* First must match principal */
		if (!nfs_compare_clientcred(&unconf->cid_credential,
					    &data->credential)) {
			if (isDebug(component)) {
				char *unconfirmed_addr = "(unknown)";

				if (unconf->gsh_client != NULL)
					unconfirmed_addr =
					    unconf->gsh_client->hostaddr_str;

				LogDebug(component,
					 "Unconfirmed ClientId %s->'%s': Principals do not match... unconfirmed addr=%s Return NFS4ERR_CLID_INUSE",
					 str_clientid4,
					 str_client_addr,
					 unconfirmed_addr);
			}

			res_CREATE_SESSION4->csr_status = NFS4ERR_CLID_INUSE;
			goto out;
		}
	}

	if (conf != NULL) {
		if (isDebug(component) && conf != NULL)
			display_clientid_name(&dspbuf_client, conf);

		/* First must match principal */
		if (!nfs_compare_clientcred(&conf->cid_credential,
					    &data->credential)) {
			if (isDebug(component)) {
				char *confirmed_addr = "(unknown)";

				if (conf->gsh_client != NULL)
					confirmed_addr =
					    conf->gsh_client->hostaddr_str;

				LogDebug(component,
					 "Confirmed ClientId %s->%s addr=%s: Principals do not match... confirmed addr=%s Return NFS4ERR_CLID_INUSE",
					 str_clientid4, str_client,
					 str_client_addr, confirmed_addr);
			}

			res_CREATE_SESSION4->csr_status = NFS4ERR_CLID_INUSE;
			goto out;
		}

		/* In this case, the record was confirmed proceed with
		   CREATE_SESSION */
	}

	/* We don't need to do any further principal checks, we can't
	 * have a confirmed clientid record with a different principal
	 * than the unconfirmed record.
	 */

	/* At this point, we need to try and create the session before
	 * we modify the confirmed and/or unconfirmed clientid
	 * records.
	 */

	/* Check flags value (test CSESS15) */
	if (arg_CREATE_SESSION4->csa_flags &
			~(CREATE_SESSION4_FLAG_PERSIST |
			  CREATE_SESSION4_FLAG_CONN_BACK_CHAN |
			  CREATE_SESSION4_FLAG_CONN_RDMA)) {
		LogDebug(component,
			 "Invalid create session flags %" PRIu32,
			 arg_CREATE_SESSION4->csa_flags);
		res_CREATE_SESSION4->csr_status = NFS4ERR_INVAL;
		goto out;
	}

	log_channel_attributes(component,
			       &arg_CREATE_SESSION4->csa_fore_chan_attrs,
			       "Fore Channel");
	log_channel_attributes(component,
			       &arg_CREATE_SESSION4->csa_back_chan_attrs,
			       "Back Channel");

	/* Let's verify the channel attributes for the session first. */
	if (arg_CREATE_SESSION4->csa_fore_chan_attrs.ca_maxrequestsize <
						NFS41_MIN_REQUEST_SIZE ||
	    arg_CREATE_SESSION4->csa_fore_chan_attrs.ca_maxresponsesize <
						NFS41_MIN_RESPONSE_SIZE ||
	    arg_CREATE_SESSION4->csa_fore_chan_attrs.ca_maxoperations <
						NFS41_MIN_OPERATIONS ||
	    arg_CREATE_SESSION4->csa_fore_chan_attrs.ca_maxrequests == 0 ||
	    arg_CREATE_SESSION4->csa_back_chan_attrs.ca_maxrequestsize <
						NFS41_MIN_REQUEST_SIZE ||
	    arg_CREATE_SESSION4->csa_back_chan_attrs.ca_maxresponsesize <
						NFS41_MIN_RESPONSE_SIZE ||
	    arg_CREATE_SESSION4->csa_back_chan_attrs.ca_maxoperations <
						NFS41_MIN_OPERATIONS ||
	    arg_CREATE_SESSION4->csa_back_chan_attrs.ca_maxrequests == 0) {
		LogWarnLimited(component,
			       "Invalid channel attributes for %s",
			       data->tagname);
		res_CREATE_SESSION4->csr_status = NFS4ERR_TOOSMALL;
		goto out;
	}

	/* Record session related information at the right place */
	nfs41_session = pool_alloc(nfs41_session_pool);

	if (nfs41_session == NULL) {
		LogCrit(component, "Could not allocate memory for a session");
		res_CREATE_SESSION4->csr_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	nfs41_session->clientid = clientid;
	nfs41_session->clientid_record = found;
	nfs41_session->refcount = 2;	/* sentinel ref + call path ref */
	nfs41_session->fore_channel_attrs =
	    arg_CREATE_SESSION4->csa_fore_chan_attrs;
	nfs41_session->back_channel_attrs =
	    arg_CREATE_SESSION4->csa_back_chan_attrs;
	nfs41_session->flags = false;
	nfs41_session->cb_program = 0;

	PTHREAD_MUTEX_init(&nfs41_session->cb_mutex, NULL);
	PTHREAD_COND_init(&nfs41_session->cb_cond, NULL);
	PTHREAD_RWLOCK_init(&nfs41_session->conn_lock, NULL);
	PTHREAD_MUTEX_init(&nfs41_session->cb_chan.chan_mtx, NULL);

	nfs41_session->nb_slots = MIN(nfs_param.nfsv4_param.nb_slots,
			nfs41_session->fore_channel_attrs.ca_maxrequests);
	nfs41_session->fc_slots = gsh_calloc(nfs41_session->nb_slots,
					     sizeof(nfs41_session_slot_t));
	nfs41_session->bc_slots = gsh_calloc(nfs41_session->nb_slots,
					     sizeof(nfs41_cb_session_slot_t));
	for (i = 0; i < nfs41_session->nb_slots; i++)
		PTHREAD_MUTEX_init(&nfs41_session->fc_slots[i].slot_lock, NULL);

	/* Take reference to clientid record on behalf the session. */
	inc_client_id_ref(found);

	/* add to head of session list (encapsulate?) */
	PTHREAD_MUTEX_lock(&found->cid_mutex);
	glist_add(&found->cid_cb.v41.cb_session_list,
		  &nfs41_session->session_link);
	PTHREAD_MUTEX_unlock(&found->cid_mutex);

	/* Set ca_maxrequests */
	nfs41_session->fore_channel_attrs.ca_maxrequests =
		nfs41_session->nb_slots;
	nfs41_Build_sessionid(&clientid, nfs41_session->session_id);

	res_CREATE_SESSION4ok->csr_sequence = arg_CREATE_SESSION4->csa_sequence;

	/* return the input for wanting of something better (will
	 * change in later versions)
	 */
	res_CREATE_SESSION4ok->csr_fore_chan_attrs =
	    nfs41_session->fore_channel_attrs;
	res_CREATE_SESSION4ok->csr_back_chan_attrs =
	    nfs41_session->back_channel_attrs;
	res_CREATE_SESSION4ok->csr_flags = 0;

	memcpy(res_CREATE_SESSION4ok->csr_sessionid,
	       nfs41_session->session_id,
	       NFS4_SESSIONID_SIZE);

	GSH_AUTO_TRACEPOINT(nfs4, session_create, TRACE_INFO,
		"Create session. Session: {}, refcount: 2", nfs41_session);

	if (!nfs41_Session_Set(nfs41_session)) {
		LogDebug(component, "Could not insert session into table");

		/* Release the sentinel session resource (our reference will
		 * be dropped on exit.
		 */
		dec_session_ref(nfs41_session);

		/* Maybe a more precise status would be better */
		res_CREATE_SESSION4->csr_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	nfs41_session->num_conn = 0;
	glist_init(&nfs41_session->connection_xprts);

	/* Add the connection to the session */
	added_conn_to_session = check_session_conn(nfs41_session, data, true);

	if (!added_conn_to_session) {
		LogCrit(component,
			"Unable to add connection FD: %d to the session",
			data->req->rq_xprt->xp_fd);

		/* Need to destroy the session */
		if (!nfs41_Session_Del(nfs41_session))
			LogDebug(component,
				"nfs41_Session_Del failed during cleanup");

		res_CREATE_SESSION4->csr_status = NFS4ERR_INVAL;
		goto out;
	}

	/* Make sure we have a reference to the confirmed clientid
	   record if any */
	if (conf == NULL) {
		conf = client_record->cr_confirmed_rec;

		if (isDebug(component) && conf != NULL)
			display_clientid_name(&dspbuf_client, conf);

		/* Need a reference to the confirmed record for below */
		if (conf != NULL) {
			/* This is the only point at which we have BOTH an
			 * unconfirmed AND confirmed record. found MUST be
			 * the unconfirmed record.
			 */
			inc_client_id_ref(conf);
		}
	}

	if (conf != NULL && conf->cid_clientid != clientid) {
		/* Old confirmed record - need to expire it */
		if (isDebug(component)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, conf);
			LogDebug(component, "Expiring %s", str);
		}

		/* Expire clientid and release our reference.
		 * NOTE: found MUST NOT BE conf (otherwise clientid would have
		 *       matched).
		 */
		nfs_client_id_expire(conf, false, true);
		dec_client_id_ref(conf);
		conf = NULL;
	}

	if (conf != NULL) {
		/* At this point we are updating the confirmed
		 * clientid.  Update the confirmed record from the
		 * unconfirmed record.
		 */
		display_clientid(&dspbuf_clientid4, conf->cid_clientid);
		LogDebug(component,
			 "Updating clientid %s->%s cb_program=%u",
			 str_clientid4, str_client,
			 arg_CREATE_SESSION4->csa_cb_program);

		if (unconf != NULL) {
			/* Deal with the ONLY situation where we have both a
			 * confirmed and unconfirmed record by unhashing
			 * the unconfirmed clientid record
			 */
			remove_unconfirmed_client_id(unconf);

			/* Release our reference to the unconfirmed entry */
			dec_client_id_ref(unconf);

			/* And now to keep code simple, set found to the
			 * confirmed record.
			 */
			found = conf;
		}

		if (isDebug(component)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, conf);
			LogDebug(component, "Updated %s", str);
		}
	} else {
		/* This is a new clientid */
		if (isFullDebug(component)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, unconf);
			LogFullDebug(component, "Confirming new %s", str);
		}

		rc = nfs_client_id_confirm(unconf, component);

		if (rc != CLIENT_ID_SUCCESS) {
			res_CREATE_SESSION4->csr_status =
			    clientid_error_to_nfsstat_no_expire(rc);

			/* Need to destroy the session */
			if (!nfs41_Session_Del(nfs41_session))
				LogDebug(component,
					 "Oops nfs41_Session_Del failed");

			goto out;
		}
		nfs4_chk_clid(unconf);

		conf = unconf;
		unconf = NULL;

		if (isDebug(component)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, conf);
			LogDebug(component, "Confirmed %s", str);
		}
	}
	conf->cid_create_session_sequence++;

	/* Bump the lease timer */
	conf->cid_last_renew = time(NULL);
	/* Once the lease timer is updated then the client is active and
	 * if the unresponsive client was marked as expired earlier,
	 * then moving it out of the expired client list
	 */
	if (conf->marked_for_delayed_cleanup)
		remove_client_from_expired_client_list(conf);

	if (isFullDebug(component)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_record(&dspbuf, client_record);
		LogFullDebug(component,
			     "Client Record %s cr_confirmed_rec=%p cr_unconfirmed_rec=%p",
			     str,
			     client_record->cr_confirmed_rec,
			     client_record->cr_unconfirmed_rec);
	}

	populate_callback_params_in_session(
		arg_CREATE_SESSION4->csa_sec_parms.csa_sec_parms_len,
		arg_CREATE_SESSION4->csa_sec_parms.csa_sec_parms_val,
		arg_CREATE_SESSION4->csa_cb_program, nfs41_session, component);

	/* Handle the creation of the back channel, if the client
	   requested one. */
	if (arg_CREATE_SESSION4->csa_flags &
	    CREATE_SESSION4_FLAG_CONN_BACK_CHAN) {

		if (nfs_rpc_create_chan_v41(
			data->req->rq_xprt, nfs41_session,
			nfs41_session->cb_sec_parms.sec_parms_len,
			nfs41_session->cb_sec_parms.sec_parms_val) == 0) {

			res_CREATE_SESSION4ok->csr_flags |=
			    CREATE_SESSION4_FLAG_CONN_BACK_CHAN;
			LogDebug(component, "Session backchannel created");
		}
	}

	if (isDebug(component)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_session(&dspbuf, nfs41_session);

		LogDebug(component,
			 "success %s csa_flags 0x%X csr_flags 0x%X",
			  str, arg_CREATE_SESSION4->csa_flags,
			  res_CREATE_SESSION4ok->csr_flags);
	}

	/* Successful exit */
	res_CREATE_SESSION4->csr_status = NFS4_OK;

	/* Cache response */
	/** @todo: Warning, if we ever have ca_rdma_ird_len == 1, we need to be
	 *         more careful since there would be allocated memory attached
	 *         to the response.
	 */
	conf->cid_create_session_slot = *res_CREATE_SESSION4;

 out:

	/* Release our reference to the found record (confirmed or unconfirmed)
	 */
	dec_client_id_ref(found);

	if (nfs41_session != NULL) {
		/* Release our reference to the session */
		dec_session_ref(nfs41_session);
	}

	PTHREAD_MUTEX_unlock(&client_record->cr_mutex);
	/* Release our reference to the client record and return */
	dec_client_record_ref(client_record);
	return nfsstat4_to_nfs_req_result(res_CREATE_SESSION4->csr_status);
}

/**
 * @brief free what was allocated to handle nfs41_op_create_session
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_create_session_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
