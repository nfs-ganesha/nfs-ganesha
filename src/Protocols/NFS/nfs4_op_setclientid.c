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
 * @file nfs4_op_setclientid.c
 * @brief Routines used for managing the NFS4_OP_SETCLIENTID operation
 */

#include "config.h"
#include <pthread.h>
#include "log.h"
#include "fsal.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"
#include "nfs_creds.h"
#include "nfs_file_handle.h"
#include "client_mgr.h"
#include "fsal.h"

/**
 *
 * @brief The NFS4_OP_SETCLIENTID operation.
 *
 * @param[in]     op   nfs4_op arguments
 * @param[in,out] data Compound request's data
 * @param[out]    resp nfs4_op results
 *
 * @retval NFS4_OK or errors for NFSv4.0.
 * @retval NFS4ERR_NOTSUPP for NFSv4.1.
 *
 */

enum nfs_req_result nfs4_op_setclientid(struct nfs_argop4 *op,
					compound_data_t *data,
					struct nfs_resop4 *resp)
{
	SETCLIENTID4args * const arg_SETCLIENTID4 =
	    &op->nfs_argop4_u.opsetclientid;
	SETCLIENTID4res * const res_SETCLIENTID4 =
	    &resp->nfs_resop4_u.opsetclientid;
	clientaddr4 * const res_SETCLIENTID4_INUSE =
	    &resp->nfs_resop4_u.opsetclientid.SETCLIENTID4res_u.client_using;
	char str_verifier[OPAQUE_BYTES_SIZE(NFS4_VERIFIER_SIZE)];
	struct display_buffer dspbuf_verifier = {
			sizeof(str_verifier), str_verifier, str_verifier};
	char str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
	struct display_buffer dspbuf_client = {
			sizeof(str_client), str_client, str_client};
	const char *str_client_addr = "(unknown)";
	/* The clientid4 broken down into fields */
	char str_clientid4[DISPLAY_CLIENTID_SIZE];
	/* Display buffer for clientid4 */
	struct display_buffer dspbuf_clientid4 = {
		sizeof(str_clientid4), str_clientid4, str_clientid4};
	nfs_client_record_t *client_record;
	nfs_client_id_t *conf;
	nfs_client_id_t *unconf;
	clientid4 clientid;
	verifier4 verifier;
	int rc;

	resp->resop = NFS4_OP_SETCLIENTID;

	if (data->minorversion > 0) {
		res_SETCLIENTID4->status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	if (op_ctx->client != NULL)
		str_client_addr = op_ctx->client->hostaddr_str;

	if (isDebug(COMPONENT_CLIENTID)) {
		display_opaque_value(&dspbuf_client,
				     arg_SETCLIENTID4->client.id.id_val,
				     arg_SETCLIENTID4->client.id.id_len);

		display_opaque_bytes(&dspbuf_verifier,
				     arg_SETCLIENTID4->client.verifier,
				     NFS4_VERIFIER_SIZE);
	} else {
		str_client[0] = '\0';
		str_verifier[0] = '\0';
	}

	LogDebug(COMPONENT_CLIENTID,
		 "SETCLIENTID Client addr=%s id=%s verf=%s callback={program=%u r_addr=%s r_netid=%s} ident=%u",
		 str_client_addr, str_client, str_verifier,
		 arg_SETCLIENTID4->callback.cb_program,
		 arg_SETCLIENTID4->callback.cb_location.r_addr,
		 arg_SETCLIENTID4->callback.cb_location.r_netid,
		 arg_SETCLIENTID4->callback_ident);

	/* Do we already have one or more records for client id (x)? */
	client_record = get_client_record(arg_SETCLIENTID4->client.id.id_val,
					  arg_SETCLIENTID4->client.id.id_len,
					  0,
					  0);

	if (client_record == NULL) {
		/* Some major failure */
		LogCrit(COMPONENT_CLIENTID, "SETCLIENTID failed");

		res_SETCLIENTID4->status = NFS4ERR_SERVERFAULT;
		return NFS_REQ_ERROR;
	}

	/* The following checks are based on RFC3530bis draft 16
	 *
	 * This attempts to implement the logic described in
	 * 15.35.5. IMPLEMENTATION Consider the major bullets as CASE
	 * 1, CASE 2, CASE 3, CASE 4, and CASE 5.
	 */

	PTHREAD_MUTEX_lock(&client_record->cr_mutex);

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

	conf = client_record->cr_confirmed_rec;

	if (conf != NULL) {
		bool credmatch;

		/* Need a reference to the confirmed record for below */
		inc_client_id_ref(conf);

		clientid = conf->cid_clientid;
		display_clientid(&dspbuf_clientid4, clientid);

		credmatch = nfs_compare_clientcred(&conf->cid_credential,
						   &data->credential)
			    && op_ctx->client != NULL
			    && conf->gsh_client != NULL
			    && op_ctx->client == conf->gsh_client;

		/* Only reject if the principal doesn't match and the
		 * clientid has live state associated. Per RFC 7530
		 * Section 9.1.2. Server Release of Client ID.
		 */
		if (!credmatch && clientid_has_state(conf)) {
			/* CASE 1:
			 *
			 * Confirmed record exists and not the same principal
			 */
			if (isDebug(COMPONENT_CLIENTID)) {
				char *confirmed_addr = "(unknown)";

				if (conf->gsh_client != NULL)
					confirmed_addr =
					    conf->gsh_client->hostaddr_str;

				LogDebug(COMPONENT_CLIENTID,
					 "Confirmed ClientId %s->'%s': Principals do not match... confirmed addr=%s Return NFS4ERR_CLID_INUSE",
					 str_clientid4, str_client,
					 confirmed_addr);
			}

			res_SETCLIENTID4->status = NFS4ERR_CLID_INUSE;
			res_SETCLIENTID4_INUSE->r_netid =
			    (char *)netid_nc_table[conf->cid_cb.v40.cb_addr.nc]
			    .netid;
			res_SETCLIENTID4_INUSE->r_addr =
			    gsh_strdup(conf->cid_cb.v40.cb_client_r_addr);

			/* Release our reference to the confirmed clientid. */
			dec_client_id_ref(conf);
			goto out;
		}

		/* Check if confirmed record is for (v, x, c, l, s) */
		if (credmatch && memcmp(arg_SETCLIENTID4->client.verifier,
					conf->cid_incoming_verifier,
					NFS4_VERIFIER_SIZE) == 0) {
			/* CASE 2:
			 *
			 * A confirmed record exists for this long
			 * form client id and verifier.
			 *
			 * Consider this to be a possible update of
			 * the call-back information.
			 *
			 * Remove any pre-existing unconfirmed record
			 * for (v, x, c).
			 *
			 * Return the same short form client id (c),
			 * but a new setclientid_confirm verifier (t).
			 */
			LogFullDebug(COMPONENT_CLIENTID,
				     "Update ClientId %s->%s",
				     str_clientid4, str_client);

			new_clientid_verifier(verifier);
		} else {
			/* Must be CASE 3 or CASE 4
			 *
			 * Confirmed record is for (u, x, c, l, s).
			 *
			 * These are actually the same, doesn't really
			 * matter if an unconfirmed record exists or
			 * not. Any existing unconfirmed record will
			 * be removed and a new unconfirmed record
			 * added.
			 *
			 * Return a new short form clientid (d) and a
			 * new setclientid_confirm verifier (t). (Note
			 * the spec calls the values e and r for CASE
			 * 4).
			 */
			LogFullDebug(COMPONENT_CLIENTID,
				     "Replace ClientId %s->%s",
				     str_clientid4, str_client);

			clientid = new_clientid();
			new_clientid_verifier(verifier);
		}

		/* Release our reference to the confirmed clientid. */
		dec_client_id_ref(conf);
	} else {
		/* CASE 5:
		 *
		 *
		 * Remove any existing unconfirmed record.
		 *
		 * Return a new short form clientid (d) and a new
		 * setclientid_confirm verifier (t).
		 */
		clientid = new_clientid();
		display_clientid(&dspbuf_clientid4, clientid);
		LogFullDebug(COMPONENT_CLIENTID,
			     "New client %s", str_clientid4);
		new_clientid_verifier(verifier);
	}

	/* At this point, no matter what the case was above, we should
	 * remove any pre-existing unconfirmed record.
	 */

	unconf = client_record->cr_unconfirmed_rec;

	if (unconf != NULL) {
		/* Delete the unconfirmed clientid record. Because we
		 * have the cr_mutex, we have won any race to deal
		 * with this clientid record (whether we raced with a
		 * SETCLIENTID_CONFIRM or the reaper thread (if either
		 * of those operations had won the race,
		 * cr_punconfirmed_id would have been NULL).
		 */
		if (isDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, unconf);

			LogDebug(COMPONENT_CLIENTID, "Replacing %s", str);
		}

		/* unhash the clientid record */
		remove_unconfirmed_client_id(unconf);
		unconf = NULL;
	}

	/* Now we can proceed to build the new unconfirmed record. We
	 * have determined the clientid and setclientid_confirm values
	 * above.
	 */

	unconf = create_client_id(clientid,
				  client_record,
				  &data->credential,
				  0);

	if (unconf == NULL) {
		/* Error already logged, return */
		res_SETCLIENTID4->status = NFS4ERR_RESOURCE;
		goto out;
	}

	if (strlcpy(unconf->cid_cb.v40.cb_client_r_addr,
		    arg_SETCLIENTID4->callback.cb_location.r_addr,
		    sizeof(unconf->cid_cb.v40.cb_client_r_addr))
	    >= sizeof(unconf->cid_cb.v40.cb_client_r_addr)) {
		LogCrit(COMPONENT_CLIENTID, "Callback r_addr %s too long",
			arg_SETCLIENTID4->callback.cb_location.r_addr);

		res_SETCLIENTID4->status = NFS4ERR_INVAL;

		free_client_id(unconf);

		goto out;
	}

	nfs_set_client_location(unconf,
				&arg_SETCLIENTID4->callback.cb_location);

	memcpy(unconf->cid_incoming_verifier,
	       arg_SETCLIENTID4->client.verifier,
	       NFS4_VERIFIER_SIZE);

	memcpy(unconf->cid_verifier, verifier, sizeof(NFS4_write_verifier));

	unconf->cid_cb.v40.cb_program = arg_SETCLIENTID4->callback.cb_program;
	unconf->cid_cb.v40.cb_callback_ident = arg_SETCLIENTID4->callback_ident;

	rc = nfs_client_id_insert(unconf);

	if (rc != CLIENT_ID_SUCCESS) {
		/* Record is already freed, return. */
		res_SETCLIENTID4->status =
					clientid_error_to_nfsstat_no_expire(rc);
		goto out;
	}

	if (isDebug(COMPONENT_CLIENTID)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_cat(&dspbuf, "Verifier=");
		display_opaque_bytes(&dspbuf, verifier, NFS4_VERIFIER_SIZE);
		display_cat(&dspbuf, " ");
		display_client_id_rec(&dspbuf, unconf);

		LogDebug(COMPONENT_CLIENTID, "SETCLIENTID reply %s", str);
	}

	res_SETCLIENTID4->status = NFS4_OK;
	res_SETCLIENTID4->SETCLIENTID4res_u.resok4.clientid = clientid;
	memcpy(res_SETCLIENTID4->SETCLIENTID4res_u.resok4.setclientid_confirm,
	       &verifier, NFS4_VERIFIER_SIZE);

 out:

	PTHREAD_MUTEX_unlock(&client_record->cr_mutex);

	/* Release our reference to the client record */
	dec_client_record_ref(client_record);

	return nfsstat4_to_nfs_req_result(res_SETCLIENTID4->status);
}

/**
 * @brief Free memory alocated for SETCLIENTID result
 *
 * @param[in,out] resp nfs4_op results
 */

void nfs4_op_setclientid_Free(nfs_resop4 *res)
{
	SETCLIENTID4res *resp = &res->nfs_resop4_u.opsetclientid;

	if (resp->status == NFS4ERR_CLID_INUSE) {
		if (resp->SETCLIENTID4res_u.client_using.r_addr != NULL)
			gsh_free(resp->SETCLIENTID4res_u.client_using.r_addr);
	}
}
