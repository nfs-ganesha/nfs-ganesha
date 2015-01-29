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
 * @file    nfs4_op_lockt.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "hashtable.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFS4_OP_LOCKT operation
 *
 * This function implements the NFS4_OP_LOCKT operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 368
 *
 * @see nfs4_Compound
 */

int nfs4_op_lockt(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	/* Alias for arguments */
	LOCKT4args * const arg_LOCKT4 = &op->nfs_argop4_u.oplockt;
	/* Alias for response */
	LOCKT4res * const res_LOCKT4 = &resp->nfs_resop4_u.oplockt;
	/* Return code from state calls */
	state_status_t state_status = STATE_SUCCESS;
	/* Client id record */
	nfs_client_id_t *clientid = NULL;
	/* Lock owner name */
	state_nfs4_owner_name_t owner_name;
	/* Lock owner record */
	state_owner_t *lock_owner = NULL;
	/* Owner of conflicting lock */
	state_owner_t *conflict_owner = NULL;
	/* Description of lock to test */
	fsal_lock_param_t lock_desc = { FSAL_NO_LOCK, 0, 0 };
	/* Description of conflicting lock */
	fsal_lock_param_t conflict_desc;
	/* return code from id confirm calls */
	int rc;

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Entering NFS v4 LOCKT handler ----------------------------");

	/* Initialize to sane default */
	resp->resop = NFS4_OP_LOCKT;

	res_LOCKT4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (res_LOCKT4->status != NFS4_OK)
		return res_LOCKT4->status;


	/* Lock length should not be 0 */
	if (arg_LOCKT4->length == 0LL) {
		res_LOCKT4->status = NFS4ERR_INVAL;
		return res_LOCKT4->status;
	}

	if (nfs_in_grace()) {
		res_LOCKT4->status = NFS4ERR_GRACE;
		return res_LOCKT4->status;
	}

	/* Convert lock parameters to internal types */
	switch (arg_LOCKT4->locktype) {
	case READ_LT:
	case READW_LT:
		lock_desc.lock_type = FSAL_LOCK_R;
		break;

	case WRITE_LT:
	case WRITEW_LT:
		lock_desc.lock_type = FSAL_LOCK_W;
		break;
	default:
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "Invalid lock type");
		res_LOCKT4->status = NFS4ERR_INVAL;
		return res_LOCKT4->status;
	}

	lock_desc.lock_start = arg_LOCKT4->offset;

	if (arg_LOCKT4->length != STATE_LOCK_OFFSET_EOF)
		lock_desc.lock_length = arg_LOCKT4->length;
	else
		lock_desc.lock_length = 0;

	/* Check for range overflow.  Comparing beyond 2^64 is not
	 * possible in 64 bit precision, but off+len > 2^64-1 is
	 * equivalent to len > 2^64-1 - off
	 */

	if (lock_desc.lock_length >
	    (STATE_LOCK_OFFSET_EOF - lock_desc.lock_start)) {
		res_LOCKT4->status = NFS4ERR_INVAL;
		return res_LOCKT4->status;
	}

	/* Check clientid */
	rc = nfs_client_id_get_confirmed(data->minorversion == 0 ?
						arg_LOCKT4->owner.clientid :
						data->session->clientid,
					 &clientid);

	if (rc != CLIENT_ID_SUCCESS) {
		res_LOCKT4->status = clientid_error_to_nfsstat(rc);
		return res_LOCKT4->status;
	}

	pthread_mutex_lock(&clientid->cid_mutex);

	if (!reserve_lease(clientid)) {
		pthread_mutex_unlock(&clientid->cid_mutex);
		dec_client_id_ref(clientid);
		res_LOCKT4->status = NFS4ERR_EXPIRED;
		return res_LOCKT4->status;
	}

	pthread_mutex_unlock(&clientid->cid_mutex);

	/* Is this lock_owner known ? */
	convert_nfs4_lock_owner(&arg_LOCKT4->owner, &owner_name);

	/* This lock owner is not known yet, allocated and set up a new one */
	lock_owner = create_nfs4_owner(&owner_name,
				       clientid,
				       STATE_LOCK_OWNER_NFSV4,
				       NULL,
				       0,
				       NULL,
				       CARE_ALWAYS);

	if (lock_owner == NULL) {
		LogEvent(COMPONENT_NFS_V4_LOCK,
			 "LOCKT unable to create lock owner");
		res_LOCKT4->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG, "LOCKT",
		data->current_entry, lock_owner, &lock_desc);

	if (data->minorversion == 0) {
		op_ctx->clientid =
		    &lock_owner->so_owner.so_nfs4_owner.so_clientid;
	}

	/* Now we have a lock owner and a stateid.  Go ahead and test
	 * the lock in SAL (and FSAL).
	 */

	state_status = state_test(data->current_entry,
				  lock_owner,
				  &lock_desc,
				  &conflict_owner,
				  &conflict_desc);

	if (state_status == STATE_LOCK_CONFLICT) {
		/* A conflicting lock from a different lock_owner,
		 * returns NFS4ERR_DENIED
		 */
		Process_nfs4_conflict(&res_LOCKT4->LOCKT4res_u.denied,
				      conflict_owner,
				      &conflict_desc);
	}

	if (data->minorversion == 0)
		op_ctx->clientid = NULL;

	/* Release NFS4 Open Owner reference */
	dec_state_owner_ref(lock_owner);

	/* Return result */
	res_LOCKT4->status = nfs4_Errno_state(state_status);

 out:

	/* Update the lease before exit */
	if (data->minorversion == 0) {
		pthread_mutex_lock(&clientid->cid_mutex);
		update_lease(clientid);
		pthread_mutex_unlock(&clientid->cid_mutex);
	}

	dec_client_id_ref(clientid);

	return res_LOCKT4->status;
}				/* nfs4_op_lockt */

/**
 * @brief Free memory allocated for LOCKT result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOCKT operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_lockt_Free(nfs_resop4 *res)
{
	LOCKT4res *resp = &res->nfs_resop4_u.oplockt;

	if (resp->status == NFS4ERR_DENIED)
		Release_nfs4_denied(&resp->LOCKT4res_u.denied);
}				/* nfs4_op_lockt_Free */
