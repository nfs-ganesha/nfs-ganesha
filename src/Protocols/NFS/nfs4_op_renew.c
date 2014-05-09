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
 * @file    nfs4_op_renew.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"

/**
 * @brief The NFS4_OP_RENEW operation.
 *
 * This function implements the NFS4_OP_RENEW operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @retval NFS4_OK or errors for NFSv4.0.
 * @retval NFS4ERR_NOTSUPP for NFSv4.1.
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_renew(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	RENEW4args * const arg_RENEW4 = &op->nfs_argop4_u.oprenew;
	RENEW4res * const res_RENEW4 = &resp->nfs_resop4_u.oprenew;
	nfs_client_id_t *clientid;
	int rc;

	/* Lock are not supported */
	memset(resp, 0, sizeof(struct nfs_resop4));
	resp->resop = NFS4_OP_RENEW;

	if (data->minorversion > 0) {
		res_RENEW4->status = NFS4ERR_NOTSUPP;
		return res_RENEW4->status;
	}

	/* Tell the admin what I am doing... */
	LogFullDebug(COMPONENT_CLIENTID,
		     "RENEW Client id = %" PRIx64,
		     arg_RENEW4->clientid);

	/* Is this an existing client id ? */
	rc = nfs_client_id_get_confirmed(arg_RENEW4->clientid, &clientid);

	if (rc != CLIENT_ID_SUCCESS) {
		/* Unknown client id */
		res_RENEW4->status = clientid_error_to_nfsstat(rc);
		return res_RENEW4->status;
	}

	pthread_mutex_lock(&clientid->cid_mutex);

	if (!reserve_lease(clientid)) {
		res_RENEW4->status = NFS4ERR_EXPIRED;
	} else {
		update_lease(clientid);
		/* update the lease, check the state of callback path and return
		 * correct error
		 */
		if (nfs_param.nfsv4_param.allow_delegations &&
		    clientid->cb_chan_down)
			res_RENEW4->status = NFS4ERR_CB_PATH_DOWN;
		else
			res_RENEW4->status = NFS4_OK;
	}

	pthread_mutex_unlock(&clientid->cid_mutex);

	dec_client_id_ref(clientid);

	return res_RENEW4->status;
}				/* nfs4_op_renew */

/**
 * @brief Free memory allocated for RENEW result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_RENEW operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_renew_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_renew_Free */
