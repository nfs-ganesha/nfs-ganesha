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
 * @file nfs4_op_destroy_clientid.c
 * @brief Provides NFS4_OP_DESTROY_CLIENTID implementation
 */

#include "config.h"
#include <pthread.h>
#include "log.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"

/**
 *
 * @brief The NFS4_OP_DESTROY_CLIENTID operation.
 *
 * @param[in]     op   nfs4_op arguments
 * @param[in,out] data Compound request data
 * @param[out]    resp nfs4_op results
 *
 * @retval NFS4_OK or errors for NFSv4.1.
 * @retval NFS4ERR_NOTSUPP for NFSv4.0.
 *
 */

enum nfs_req_result nfs4_op_destroy_clientid(struct nfs_argop4 *op,
					     compound_data_t *data,
					     struct nfs_resop4 *resp)
{
	DESTROY_CLIENTID4args * const arg_DESTROY_CLIENTID4 =
	    &op->nfs_argop4_u.opdestroy_clientid;
	DESTROY_CLIENTID4res * const res_DESTROY_CLIENTID4 =
	    &resp->nfs_resop4_u.opdestroy_clientid;
	nfs_client_record_t *client_record = NULL;
	nfs_client_id_t *conf = NULL, *unconf = NULL, *found = NULL;
	clientid4 clientid;
	int rc;

	resp->resop = NFS4_OP_DESTROY_CLIENTID;

	clientid = arg_DESTROY_CLIENTID4->dca_clientid;

	if (isDebug(COMPONENT_CLIENTID)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_clientid(&dspbuf, clientid);

		LogDebug(COMPONENT_CLIENTID,
			 "DESTROY_CLIENTID clientid=%s",
			 str);
	}

	res_DESTROY_CLIENTID4->dcr_status = NFS4_OK;

	/* First try to look up confirmed record */
	rc = nfs_client_id_get_confirmed(clientid, &conf);

	if (rc == CLIENT_ID_SUCCESS) {
		client_record = conf->cid_client_record;
		found = conf;
	} else {
		/* fall back to unconfirmed */
		rc = nfs_client_id_get_unconfirmed(clientid, &unconf);

		if (rc == CLIENT_ID_SUCCESS) {
			client_record = unconf->cid_client_record;
			found = unconf;
		}

		/* handle the perverse case of a clientid being confirmed
		 * in the above interval */
		rc = nfs_client_id_get_confirmed(clientid, &conf);

		if (rc == CLIENT_ID_SUCCESS) {
			if (found != NULL)
				dec_client_id_ref(found);
			client_record = conf->cid_client_record;
			found = conf;
		}
	}

	/* ref +1 */
	if (client_record == NULL) {
		/* Fine.  We're done. */
		res_DESTROY_CLIENTID4->dcr_status = NFS4ERR_STALE_CLIENTID;
		goto out;
	}

	(void) inc_client_record_ref(client_record);

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

	/* per Frank, we must check the confirmed and unconfirmed
	 * state of client_record again now that we hold cr_mutex
	 */
	conf = client_record->cr_confirmed_rec;
	unconf = client_record->cr_unconfirmed_rec;

	if ((!conf) && (!unconf)) {
		/* We raced a thread destroying clientid, and lost.
		 * We're done. */
		goto cleanup;
	}

	if (conf) {
		/* We MUST NOT destroy a clientid that has nfsv41 sessions or
		 * state. Since the minorversion is 4.1 or higher, this is
		 * equivalent to a session check.
		 */
		PTHREAD_MUTEX_lock(&conf->cid_mutex);
		if (!glist_empty(&conf->cid_cb.v41.cb_session_list)) {
			res_DESTROY_CLIENTID4->dcr_status =
							NFS4ERR_CLIENTID_BUSY;
			PTHREAD_MUTEX_unlock(&conf->cid_mutex);
			goto cleanup;
		}
		PTHREAD_MUTEX_unlock(&conf->cid_mutex);

		/* Delete the confirmed clientid record. Because we
		 * have the cr_mutex, we have won any race to deal
		 * with this clientid record.
		 */
		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, conf);

			LogDebug(COMPONENT_CLIENTID,
				 "Removing confirmed clientid %s", str);
		}

		/* remove stable-storage record (if any) */
		nfs4_rm_clid(conf);

		/* unhash the clientid record */
		(void)remove_confirmed_client_id(conf);
	}

	if (unconf) {
		/* Delete the unconfirmed clientid record. Because we
		 * have the cr_mutex, we have won any race to deal
		 * with this clientid record.
		 */
		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_client_id_rec(&dspbuf, unconf);

			LogDebug(COMPONENT_CLIENTID,
				 "Removing unconfirmed clientid %s", str);
		}

		/* unhash the clientid record */
		(void)remove_unconfirmed_client_id(unconf);
	}

 cleanup:

	PTHREAD_MUTEX_unlock(&client_record->cr_mutex);
	dec_client_record_ref(client_record);	/* ref +0 */

	if (found != NULL)
		dec_client_id_ref(found);

 out:

	return nfsstat4_to_nfs_req_result(res_DESTROY_CLIENTID4->dcr_status);
}

/**
 * @brief Free DESTROY_CLIENTID result
 *
 * @param[in,out] resp nfs4_op results
 */

void nfs4_op_destroy_clientid_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
