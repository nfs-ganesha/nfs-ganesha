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
 * @file  nfs4_op_setclientid_confirm.c
 * @brief Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
 */
#include "config.h"
#include <pthread.h>
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_rpc_callback.h"
#include "nfs_creds.h"
#include "nfs_file_handle.h"
#include "client_mgr.h"
#include "fsal.h"

/**
 *
 * @brief The NFS4_OP_SETCLIENTID_CONFIRM operation
 *
 * @param[in]     op    nfs4_op arguments
 * @param[in,out] data  The compound request's data
 * @param[out]    resp  nfs4_op results
 *
 * @retval NFS4_OK or errors for NFSv4.0.
 * @retval NFS4ERR_NOTSUPP for NFSv4.1.
 *
 * @see nfs4_Compound
 */

enum nfs_req_result nfs4_op_setclientid_confirm(struct nfs_argop4 *op,
						compound_data_t *data,
						struct nfs_resop4 *resp)
{
	SETCLIENTID_CONFIRM4args * const arg_SETCLIENTID_CONFIRM4 =
	    &op->nfs_argop4_u.opsetclientid_confirm;
	SETCLIENTID_CONFIRM4res * const res_SETCLIENTID_CONFIRM4 =
	    &resp->nfs_resop4_u.opsetclientid_confirm;
	nfs_client_id_t *conf = NULL;
	nfs_client_id_t *unconf = NULL;
	nfs_client_record_t *client_record;
	clientid4 clientid = 0;
	char str_verifier[NFS4_VERIFIER_SIZE * 2 + 1];
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
	int rc;

	/* Make sure str_client is always printable even
	 * if log level changes midstream.
	 */
	display_printf(&dspbuf_client, "(unknown)");
	display_reset_buffer(&dspbuf_client);

	resp->resop = NFS4_OP_SETCLIENTID_CONFIRM;
	res_SETCLIENTID_CONFIRM4->status = NFS4_OK;
	clientid = arg_SETCLIENTID_CONFIRM4->clientid;

	display_clientid(&dspbuf_clientid4, clientid);

	if (data->minorversion > 0) {
		res_SETCLIENTID_CONFIRM4->status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	if (op_ctx->client != NULL)
		str_client_addr = op_ctx->client->hostaddr_str;

	if (isDebug(COMPONENT_CLIENTID)) {
		sprint_mem(str_verifier,
			   arg_SETCLIENTID_CONFIRM4->setclientid_confirm,
			   NFS4_VERIFIER_SIZE);
	} else {
		str_verifier[0] = '\0';
	}

	LogDebug(COMPONENT_CLIENTID,
		 "SETCLIENTID_CONFIRM client addr=%s clientid=%s setclientid_confirm=%s",
		 str_client_addr, str_clientid4, str_verifier);

	/* First try to look up unconfirmed record */
	rc = nfs_client_id_get_unconfirmed(clientid, &unconf);

	if (rc == CLIENT_ID_SUCCESS) {
		client_record = unconf->cid_client_record;

		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, unconf);
			LogFullDebug(COMPONENT_CLIENTID, "Found %s", str);
		}
	} else {
		rc = nfs_client_id_get_confirmed(clientid, &conf);

		if (rc != CLIENT_ID_SUCCESS) {
			/* No record whatsoever of this clientid */
			LogDebug(COMPONENT_CLIENTID,
				 "%s clientid = %s",
				 clientid_error_to_str(rc), str_clientid4);
			res_SETCLIENTID_CONFIRM4->status =
			    clientid_error_to_nfsstat_no_expire(rc);

			return NFS_REQ_ERROR;
		}

		client_record = conf->cid_client_record;

		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, conf);
			LogFullDebug(COMPONENT_CLIENTID, "Found %s", str);
		}
	}

	PTHREAD_MUTEX_lock(&client_record->cr_mutex);

	inc_client_record_ref(client_record);

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_record(&dspbuf, client_record);

		LogFullDebug(COMPONENT_CLIENTID,
			     "Client Record %s cr_confirmed_rec=%p cr_unconfirmed_rec=%p",
			     str,
			     client_record->cr_confirmed_rec,
			     client_record->cr_unconfirmed_rec);
	}

	/* At this point one and only one of pconf and punconf is non-NULL */

	if (unconf != NULL) {
		/* First must match principal */
		if (!nfs_compare_clientcred(&unconf->cid_credential,
					    &data->credential)
		    || op_ctx->client == NULL
		    || unconf->gsh_client == NULL
		    || op_ctx->client != unconf->gsh_client) {
			if (isDebug(COMPONENT_CLIENTID)) {
				char *unconfirmed_addr = "(unknown)";

				if (unconf->gsh_client != NULL)
					unconfirmed_addr =
					    unconf->gsh_client->hostaddr_str;

				LogDebug(COMPONENT_CLIENTID,
					 "Unconfirmed ClientId %s->'%s': Principals do not match... unconfirmed addr=%s Return NFS4ERR_CLID_INUSE",
					 str_clientid4,
					 str_client_addr,
					 unconfirmed_addr);
			}

			res_SETCLIENTID_CONFIRM4->status = NFS4ERR_CLID_INUSE;
			dec_client_id_ref(unconf);
			goto out;
		} else if (unconf->cid_confirmed == CONFIRMED_CLIENT_ID &&
			   memcmp(unconf->cid_verifier,
				  arg_SETCLIENTID_CONFIRM4->setclientid_confirm,
				  NFS4_VERIFIER_SIZE) == 0) {
			/* We must have raced with another
			   SETCLIENTID_CONFIRM */
			if (isDebug(COMPONENT_CLIENTID)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
					sizeof(str), str, str};

				display_client_id_rec(&dspbuf, unconf);
				LogDebug(COMPONENT_CLIENTID,
					 "Race against confirm for %s", str);
			}

			res_SETCLIENTID_CONFIRM4->status = NFS4_OK;
			dec_client_id_ref(unconf);

			goto out;
		} else if (unconf->cid_confirmed != UNCONFIRMED_CLIENT_ID) {
			/* We raced with another thread that dealt
			 * with this unconfirmed record.  Release our
			 * reference, and pretend we didn't find a
			 * record.
			 */
			if (isDebug(COMPONENT_CLIENTID)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
					sizeof(str), str, str};

				display_client_id_rec(&dspbuf, unconf);

				LogDebug(COMPONENT_CLIENTID,
					 "Race against expire for %s", str);
			}

			res_SETCLIENTID_CONFIRM4->status =
			    NFS4ERR_STALE_CLIENTID;

			dec_client_id_ref(unconf);

			goto out;
		}
	}

	if (conf != NULL) {
		if (isDebug(COMPONENT_CLIENTID) && conf != NULL)
			display_clientid_name(&dspbuf_client, conf);

		/* First must match principal */
		if (!nfs_compare_clientcred(&conf->cid_credential,
					    &data->credential)
		    || op_ctx->client == NULL
		    || conf->gsh_client == NULL
		    || op_ctx->client != conf->gsh_client) {
			if (isDebug(COMPONENT_CLIENTID)) {
				char *confirmed_addr = "(unknown)";

				if (conf->gsh_client != NULL)
					confirmed_addr =
					    conf->gsh_client->hostaddr_str;

				LogDebug(COMPONENT_CLIENTID,
					 "Confirmed ClientId %s->%s addr=%s: Principals do not match...  confirmed addr=%s Return NFS4ERR_CLID_INUSE",
					 str_clientid4,
					 str_client,
					 str_client_addr,
					 confirmed_addr);
			}

			res_SETCLIENTID_CONFIRM4->status = NFS4ERR_CLID_INUSE;
		} else if (memcmp(
				conf->cid_verifier,
				arg_SETCLIENTID_CONFIRM4->setclientid_confirm,
				NFS4_VERIFIER_SIZE) == 0) {
			/* In this case, the record was confirmed and
			 * we have received a retry
			 */
			if (isDebug(COMPONENT_CLIENTID)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
					sizeof(str), str, str};

				display_client_id_rec(&dspbuf, conf);
				LogDebug(COMPONENT_CLIENTID,
					 "Retry confirm for %s", str);
			}

			res_SETCLIENTID_CONFIRM4->status = NFS4_OK;
		} else {
			/* This is a case not covered... Return
			 * NFS4ERR_CLID_INUSE
			 */
			if (isDebug(COMPONENT_CLIENTID)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
					sizeof(str), str, str};
				char str_conf_verifier[NFS4_VERIFIER_SIZE * 2 +
						       1];

				sprint_mem(str_conf_verifier,
					   conf->cid_verifier,
					   NFS4_VERIFIER_SIZE);

				display_client_id_rec(&dspbuf, conf);

				LogDebug(COMPONENT_CLIENTID,
					 "Confirm verifier=%s doesn't match verifier=%s for %s",
					 str_conf_verifier, str_verifier, str);
			}

			res_SETCLIENTID_CONFIRM4->status = NFS4ERR_CLID_INUSE;
		}

		/* Release our reference to the confirmed clientid. */
		dec_client_id_ref(conf);
		goto out;
	}

	/* We don't need to do any further principal checks, we can't
	 * have a confirmed clientid record with a different principal
	 * than the unconfirmed record.  Also, at this point, we have
	 * a matching unconfirmed clientid (punconf != NULL and pconf
	 * == NULL).
	 */

	/* Make sure we have a reference to the confirmed clientid
	 * record if any
	 */
	if (conf == NULL) {
		conf = client_record->cr_confirmed_rec;

		if (isDebug(COMPONENT_CLIENTID) && conf != NULL)
			display_clientid_name(&dspbuf_client, conf);

		/* Need a reference to the confirmed record for below */
		if (conf != NULL)
			inc_client_id_ref(conf);
	}

	if (conf != NULL && conf->cid_clientid != clientid) {
		/* Old confirmed record - need to expire it */
		if (isDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, conf);
			LogDebug(COMPONENT_CLIENTID, "Expiring %s", str);
		}

		/* Expire clientid and release our reference. */
		nfs_client_id_expire(conf, false);

		dec_client_id_ref(conf);

		conf = NULL;
	}

	if (conf != NULL) {
		/* At this point we are updating the confirmed
		 * clientid.  Update the confirmed record from the
		 * unconfirmed record.
		 */
		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, unconf);
			LogFullDebug(COMPONENT_CLIENTID, "Updating from %s",
				     str);
		}

		/* Copy callback information into confirmed clientid record */
		memcpy(conf->cid_cb.v40.cb_client_r_addr,
		       unconf->cid_cb.v40.cb_client_r_addr,
		       sizeof(conf->cid_cb.v40.cb_client_r_addr));

		conf->cid_cb.v40.cb_addr = unconf->cid_cb.v40.cb_addr;
		conf->cid_cb.v40.cb_program = unconf->cid_cb.v40.cb_program;

		conf->cid_cb.v40.cb_callback_ident =
		    unconf->cid_cb.v40.cb_callback_ident;

		nfs_rpc_destroy_chan(&conf->cid_cb.v40.cb_chan);

		/* Bump the lease timer*/
		conf->cid_last_renew = time(NULL);

		memcpy(conf->cid_verifier, unconf->cid_verifier,
		       NFS4_VERIFIER_SIZE);

		/* unhash the unconfirmed clientid record */
		remove_unconfirmed_client_id(unconf);

		/* Release our reference to the unconfirmed entry */
		dec_client_id_ref(unconf);

		if (isDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, conf);
			LogDebug(COMPONENT_CLIENTID, "Updated %s", str);
		}
		/* Check and update call back channel state */
		if (nfs_param.nfsv4_param.allow_delegations &&
		    nfs_test_cb_chan(conf) != RPC_SUCCESS) {
			set_cb_chan_down(conf, true);
			LogCrit(COMPONENT_CLIENTID,
				"setclid confirm: Callback channel is down");
		} else {
			set_cb_chan_down(conf, false);
			LogDebug(COMPONENT_CLIENTID,
				"setclid confirm: Callback channel is UP");
		}

		/* Release our reference to the confirmed clientid. */
		dec_client_id_ref(conf);
	} else {
		/* This is a new clientid */
		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, unconf);
			LogFullDebug(COMPONENT_CLIENTID,
				     "Confirming new %s",
				     str);
		}

		rc = nfs_client_id_confirm(unconf, COMPONENT_CLIENTID);

		if (rc != CLIENT_ID_SUCCESS) {
			res_SETCLIENTID_CONFIRM4->status =
			    clientid_error_to_nfsstat_no_expire(rc);

			LogEvent(COMPONENT_CLIENTID,
				 "FAILED to confirm client");

			/* Release our reference to the unconfirmed record */
			dec_client_id_ref(unconf);

			goto out;
		}

		/* check if the client can perform reclaims */
		nfs4_chk_clid(unconf);

		if (isDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, unconf);

			LogDebug(COMPONENT_CLIENTID, "Confirmed %s", str);
		}

		/* Check and update call back channel state */
		if (nfs_param.nfsv4_param.allow_delegations &&
		    nfs_test_cb_chan(unconf) != RPC_SUCCESS) {
			set_cb_chan_down(unconf, true);
			LogCrit(COMPONENT_CLIENTID,
				"setclid confirm: Callback channel is down");
		} else {
			set_cb_chan_down(unconf, false);
			LogDebug(COMPONENT_CLIENTID,
				"setclid confirm: Callback channel is UP");
		}

		/* Release our reference to the now confirmed record */
		dec_client_id_ref(unconf);
	}

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_record(&dspbuf, client_record);
		LogFullDebug(COMPONENT_CLIENTID,
			     "Client Record %s cr_confirmed_rec=%p cr_unconfirmed_rec=%p",
			     str,
			     client_record->cr_confirmed_rec,
			     client_record->cr_unconfirmed_rec);
	}

	/* Successful exit */
	res_SETCLIENTID_CONFIRM4->status = NFS4_OK;

 out:

	PTHREAD_MUTEX_unlock(&client_record->cr_mutex);
	/* Release our reference to the client record and return */
	dec_client_record_ref(client_record);
	return nfsstat4_to_nfs_req_result(res_SETCLIENTID_CONFIRM4->status);
}

/**
 * @brief Free memory allocated for SETCLIENTID_CONFIRM result
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_setclientid_confirm_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
