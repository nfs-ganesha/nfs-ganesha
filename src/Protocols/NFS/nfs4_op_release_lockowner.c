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
 * @file    nfs4_op_release_lockowner.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"

/**
 * @brief NFS4_OP_RELEASE_LOCKOWNER
 *
 * This function implements the NFS4_OP_RELEASE_LOCKOWNER function.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @retval NFS4_OK or errors for NFSv4.0.
 * @retval NFS4ERR_NOTSUPP for NFSv4.1.
 */

enum nfs_req_result nfs4_op_release_lockowner(struct nfs_argop4 *op,
					      compound_data_t *data,
					      struct nfs_resop4 *resp)
{
	RELEASE_LOCKOWNER4args * const arg_RELEASE_LOCKOWNER4 =
	    &op->nfs_argop4_u.oprelease_lockowner;
	RELEASE_LOCKOWNER4res * const res_RELEASE_LOCKOWNER4 =
	    &resp->nfs_resop4_u.oprelease_lockowner;
	nfs_client_id_t *nfs_client_id;
	state_owner_t *lock_owner;
	state_nfs4_owner_name_t owner_name;
	int rc;

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Entering NFS v4 RELEASE_LOCKOWNER handler ----------------------");

	resp->resop = NFS4_OP_RELEASE_LOCKOWNER;
	res_RELEASE_LOCKOWNER4->status = NFS4_OK;

	if (data->minorversion > 0) {
		res_RELEASE_LOCKOWNER4->status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	/* Check clientid */
	rc = nfs_client_id_get_confirmed(
		arg_RELEASE_LOCKOWNER4->lock_owner.clientid, &nfs_client_id);

	if (rc != CLIENT_ID_SUCCESS) {
		res_RELEASE_LOCKOWNER4->status = clientid_error_to_nfsstat(rc);
		goto out2;
	}

	PTHREAD_MUTEX_lock(&nfs_client_id->cid_mutex);

	if (!reserve_lease(nfs_client_id)) {
		PTHREAD_MUTEX_unlock(&nfs_client_id->cid_mutex);

		dec_client_id_ref(nfs_client_id);

		res_RELEASE_LOCKOWNER4->status = NFS4ERR_EXPIRED;
		goto out2;
	}

	PTHREAD_MUTEX_unlock(&nfs_client_id->cid_mutex);

	/* look up the lock owner and see if we can find it */
	convert_nfs4_lock_owner(&arg_RELEASE_LOCKOWNER4->lock_owner,
				&owner_name);

	/* If this lock owner is not known yet, allocated
	 * and set up a new one
	 */
	lock_owner = create_nfs4_owner(&owner_name,
				       nfs_client_id,
				       STATE_LOCK_OWNER_NFSV4,
				       NULL,
				       0,
				       NULL,
				       CARE_NOT, true);

	if (lock_owner == NULL) {
		/* the owner doesn't exist, we are done */
		LogDebug(COMPONENT_NFS_V4_LOCK, "lock owner does not exist");
		res_RELEASE_LOCKOWNER4->status = NFS4_OK;
		goto out1;
	}

	res_RELEASE_LOCKOWNER4->status = release_lock_owner(lock_owner);

	/* Release the reference to the lock owner acquired
	 * via create_nfs4_owner
	 */
	dec_state_owner_ref(lock_owner);

 out1:

	/* Update the lease before exit */
	PTHREAD_MUTEX_lock(&nfs_client_id->cid_mutex);

	update_lease(nfs_client_id);

	PTHREAD_MUTEX_unlock(&nfs_client_id->cid_mutex);

	dec_client_id_ref(nfs_client_id);

 out2:

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Leaving NFS v4 RELEASE_LOCKOWNER handler -----------------------");

	return nfsstat4_to_nfs_req_result(res_RELEASE_LOCKOWNER4->status);
}				/* nfs4_op_release_lock_owner */

/**
 * @brief Free memory allocated for REELASE_LOCKOWNER result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_REELASE_LOCKOWNER operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_release_lockowner_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
