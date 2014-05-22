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
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_rpc_callback.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"
#include "nfs_creds.h"

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

int nfs4_op_create_session(struct nfs_argop4 *op, compound_data_t *data,
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
	/* The client's network address */
	sockaddr_t client_addr;
	/* The client address as a string, for gratuitous logging */
	char str_client_addr[SOCK_NAME_MAX + 1];
	/* The client name, for gratuitous logging */
	char str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
	/* Return code from clientid calls */
	int rc = 0;
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

	if (isDebug(COMPONENT_SESSIONS))
		component = COMPONENT_SESSIONS;

	resp->resop = NFS4_OP_CREATE_SESSION;
	res_CREATE_SESSION4->csr_status = NFS4_OK;
	clientid = arg_CREATE_SESSION4->csa_clientid;

	if (data->minorversion == 0)
		return res_CREATE_SESSION4->csr_status = NFS4ERR_INVAL;

	copy_xprt_addr(&client_addr, data->req->rq_xprt);

	if (isDebug(component))
		sprint_sockip(&client_addr, str_client_addr,
			      sizeof(str_client_addr));

	LogDebug(component,
		 "CREATE_SESSION client addr=%s clientid=%" PRIx64
		 " -------------------",
		 str_client_addr, clientid);

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
				 "%s clientid = %" PRIx64,
				 clientid_error_to_str(rc), clientid);

			if (rc == CLIENT_ID_EXPIRED)
				rc = CLIENT_ID_STALE;

			res_CREATE_SESSION4->csr_status =
			    clientid_error_to_nfsstat(rc);

			return res_CREATE_SESSION4->csr_status;
		}

		client_record = conf->cid_client_record;
		found = conf;
	}

	pthread_mutex_lock(&client_record->cr_mutex);

	inc_client_record_ref(client_record);

	if (isFullDebug(component)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_record(client_record, str);

		LogFullDebug(component,
			     "Client Record %s cr_confirmed_rec=%p "
			     "cr_unconfirmed_rec=%p",
			     str,
			     client_record->cr_confirmed_rec,
			     client_record->cr_unconfirmed_rec);
	}

	/* At this point one and only one of conf and unconf is
	 * non-NULL, and found also references the single clientid
	 * record that was found.
	 */

	LogDebug(component,
		 "CREATE_SESSION clientid=%" PRIx64 " csa_sequence=%" PRIu32
		 " clientid_cs_seq=%" PRIu32 " data_oppos=%d data_use_drc=%d",
		 clientid, arg_CREATE_SESSION4->csa_sequence,
		 found->cid_create_session_sequence, data->oppos,
		 data->use_drc);

	if (isFullDebug(component)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_id_rec(found, str);
		LogFullDebug(component, "Found %s", str);
	}

	data->use_drc = false;

	if (data->oppos == 0) {
		/* Special case : the request is used without use of
		 * OP_SEQUENCE
		 */
		if ((arg_CREATE_SESSION4->csa_sequence + 1 ==
		     found->cid_create_session_sequence)
		    && (found->cid_create_session_slot.cache_used)) {
			data->use_drc = true;
			data->cached_res =
			    &found->cid_create_session_slot.cached_result;

			res_CREATE_SESSION4->csr_status = NFS4_OK;

			dec_client_id_ref(found);

			LogDebug(component,
				 "CREATE_SESSION replay=%p special case",
				 data->cached_res);

			goto out;
		} else if (arg_CREATE_SESSION4->csa_sequence !=
			   found->cid_create_session_sequence) {
			res_CREATE_SESSION4->csr_status =
			    NFS4ERR_SEQ_MISORDERED;

			dec_client_id_ref(found);

			LogDebug(component,
				 "CREATE_SESSION returning NFS4ERR_SEQ_MISORDERED");

			goto out;
		}

	}

	if (unconf != NULL) {
		/* First must match principal */
		if (!nfs_compare_clientcred(&unconf->cid_credential,
					    &data->credential)) {
			if (isDebug(component)) {
				char unconfirmed_addr[SOCK_NAME_MAX + 1];

				sprint_sockip(&unconf->cid_client_addr,
					      unconfirmed_addr,
					      sizeof(unconfirmed_addr));

				LogDebug(component,
					 "Unconfirmed ClientId %" PRIx64
					 "->'%s': Principals do not match... unconfirmed addr=%s Return NFS4ERR_CLID_INUSE",
					 clientid,
					 str_client_addr,
					 unconfirmed_addr);
			}

			dec_client_id_ref(unconf);
			res_CREATE_SESSION4->csr_status = NFS4ERR_CLID_INUSE;
			goto out;
		}
	}

	if (conf != NULL) {
		if (isDebug(component) && conf != NULL)
			display_clientid_name(conf, str_client);

		/* First must match principal */
		if (!nfs_compare_clientcred(&conf->cid_credential,
					    &data->credential)) {
			if (isDebug(component)) {
				char confirmed_addr[SOCK_NAME_MAX + 1];

				sprint_sockip(&conf->cid_client_addr,
					      confirmed_addr,
					      sizeof(confirmed_addr));

				LogDebug(component,
					 "Confirmed ClientId %" PRIx64 "->%s "
					 "addr=%s: Principals do not match... confirmed addr=%s Return NFS4ERR_CLID_INUSE",
					 clientid, str_client,
					 str_client_addr, confirmed_addr);
			}

			/* Release our reference to the confirmed clientid. */
			dec_client_id_ref(conf);
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
		dec_client_id_ref(found);
		res_CREATE_SESSION4->csr_status = NFS4ERR_INVAL;
		goto out;
	}

	/* Record session related information at the right place */
	nfs41_session = pool_alloc(nfs41_session_pool, NULL);

	if (nfs41_session == NULL) {
		LogCrit(component, "Could not allocate memory for a session");
		dec_client_id_ref(found);
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
	nfs41_session->xprt = data->req->rq_xprt;
	nfs41_session->flags = false;
	nfs41_session->cb_program = 0;
	pthread_mutex_init(&nfs41_session->cb_mutex, NULL);
	pthread_cond_init(&nfs41_session->cb_cond, NULL);

	/* Take reference to clientid record */
	inc_client_id_ref(found);

	/* add to head of session list (encapsulate?) */
	pthread_mutex_lock(&found->cid_mutex);
	glist_add(&found->cid_cb.v41.cb_session_list,
		  &nfs41_session->session_link);
	pthread_mutex_unlock(&found->cid_mutex);

	/* Set ca_maxrequests */
	nfs41_session->fore_channel_attrs.ca_maxrequests = NFS41_NB_SLOTS;
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

	/* Create Session replay cache */
	data->cached_res = &found->cid_create_session_slot.cached_result;
	found->cid_create_session_slot.cache_used = true;

	LogDebug(component, "CREATE_SESSION replay=%p", data->cached_res);

	if (!nfs41_Session_Set(nfs41_session)) {
		LogDebug(component, "Could not insert session into table");

		pthread_mutex_lock(&found->cid_mutex);
		glist_del(&nfs41_session->session_link);
		pthread_mutex_unlock(&found->cid_mutex);

		/* Decrement our reference to the clientid record and
		   the one for the session */
		dec_client_id_ref(found);
		dec_client_id_ref(found);

		/* Free the memory for the session */
		pool_free(nfs41_session_pool, nfs41_session);

		/* Maybe a more precise status would be better */
		res_CREATE_SESSION4->csr_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	/* Make sure we have a reference to the confirmed clientid
	   record if any */
	if (conf == NULL) {
		conf = client_record->cr_confirmed_rec;

		if (isDebug(component) && conf != NULL)
			display_clientid_name(conf, str_client);

		/* Need a reference to the confirmed record for below */
		if (conf != NULL)
			inc_client_id_ref(conf);
	}

	if (conf != NULL && conf->cid_clientid != clientid) {
		/* Old confirmed record - need to expire it */
		if (isDebug(component)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(conf, str);
			LogDebug(component, "Expiring %s", str);
		}

		/* Expire clientid and release our reference. */
		nfs_client_id_expire(conf);
		dec_client_id_ref(conf);
		conf = NULL;
	}

	if (conf != NULL) {
		/* At this point we are updating the confirmed
		 * clientid.  Update the confirmed record from the
		 * unconfirmed record.
		 */
		LogDebug(component,
			 "Updating clientid %" PRIx64 "->%s cb_program=%u",
			 conf->cid_clientid, str_client,
			 arg_CREATE_SESSION4->csa_cb_program);

		if (unconf != NULL) {
			/* unhash the unconfirmed clientid record */
			remove_unconfirmed_client_id(unconf);
			/* Release our reference to the unconfirmed entry */
			dec_client_id_ref(unconf);
		}

		if (isDebug(component)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(conf, str);
			LogDebug(component, "Updated %s", str);
		}
	} else {
		/* This is a new clientid */
		if (isFullDebug(component)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(unconf, str);
			LogFullDebug(component, "Confirming new %s", str);
		}

		rc = nfs_client_id_confirm(unconf, component);

		if (rc != CLIENT_ID_SUCCESS) {
			res_CREATE_SESSION4->csr_status =
			    clientid_error_to_nfsstat(rc);

			/* Need to destroy the session */
			if (!nfs41_Session_Del(nfs41_session->session_id))
				LogDebug(component,
					 "Oops nfs41_Session_Del failed");

			/* Release our reference to the unconfirmed record */
			dec_client_id_ref(unconf);
			goto out;
		}
		nfs4_chk_clid(unconf);

		conf = unconf;
		unconf = NULL;

		if (isDebug(component)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(conf, str);
			LogDebug(component, "Confirmed %s", str);
		}
	}
	conf->cid_create_session_sequence++;

	/* Bump the lease timer */
	conf->cid_last_renew = time(NULL);

	/* Release our reference to the confirmed record */
	dec_client_id_ref(conf);

	/* And to the session */
	dec_session_ref(nfs41_session);

	if (isFullDebug(component)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_record(client_record, str);
		LogFullDebug(component,
			     "Client Record %s cr_confirmed_rec=%p cr_unconfirmed_rec=%p",
			     str,
			     client_record->cr_confirmed_rec,
			     client_record->cr_unconfirmed_rec);
	}

	/* Handle the creation of the back channel, if the client
	   requested one. */
	if (arg_CREATE_SESSION4->csa_flags &
	    CREATE_SESSION4_FLAG_CONN_BACK_CHAN) {
		nfs41_session->cb_program = arg_CREATE_SESSION4->csa_cb_program;
		if (nfs_rpc_create_chan_v41(
			nfs41_session,
			arg_CREATE_SESSION4->csa_sec_parms.csa_sec_parms_len,
			arg_CREATE_SESSION4->csa_sec_parms.csa_sec_parms_val)
		    == 0) {
			res_CREATE_SESSION4ok->csr_flags |=
			    CREATE_SESSION4_FLAG_CONN_BACK_CHAN;
		}
	}

	LogDebug(component, "CREATE_SESSION success session=%p", nfs41_session);

	/* Successful exit */
	res_CREATE_SESSION4->csr_status = NFS4_OK;

 out:
	pthread_mutex_unlock(&client_record->cr_mutex);
	/* Release our reference to the client record and return */
	dec_client_record_ref(client_record);
	return res_CREATE_SESSION4->csr_status;
}

/**
 * @brief free what was allocated to handle nfs41_op_create_session
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_create_session_Free(nfs_resop4 *resp)
{
	return;
}
