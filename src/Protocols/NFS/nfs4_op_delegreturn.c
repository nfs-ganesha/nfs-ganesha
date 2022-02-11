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
 * @file    nfs4_op_delegreturn.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"

/**
 * @brief NFS4_OP_DELEGRETURN
 *
 * This function implements the NFS4_OP_DELEGRETURN operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC 5661, p. 364
 */
enum nfs_req_result nfs4_op_delegreturn(struct nfs_argop4 *op,
					compound_data_t *data,
					struct nfs_resop4 *resp)
{
	DELEGRETURN4args * const arg_DELEGRETURN4 =
	    &op->nfs_argop4_u.opdelegreturn;
	DELEGRETURN4res * const res_DELEGRETURN4 =
	    &resp->nfs_resop4_u.opdelegreturn;

	state_status_t state_status;
	state_t *state_found;
	const char *tag = "DELEGRETURN";
	state_owner_t *owner;

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Entering NFS v4 DELEGRETURN handler -----------------------------------------------------");

	/* Initialize to sane default */
	resp->resop = NFS4_OP_DELEGRETURN;

	/* If the filehandle is invalid. Delegations are only supported on
	 * regular files at the moment.
	 */
	res_DELEGRETURN4->status = nfs4_sanity_check_FH(data,
							REGULAR_FILE,
							false);

	if (res_DELEGRETURN4->status != NFS4_OK) {
		if (res_DELEGRETURN4->status == NFS4ERR_ISDIR)
			res_DELEGRETURN4->status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	/* Check stateid correctness and get pointer to state */
	res_DELEGRETURN4->status =
		nfs4_Check_Stateid(&arg_DELEGRETURN4->deleg_stateid,
				   data->current_obj, &state_found, data,
				   STATEID_SPECIAL_FOR_LOCK, 0, false, tag);

	if (res_DELEGRETURN4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	owner = get_state_owner_ref(state_found);

	if (owner == NULL) {
		/* Something has gone stale. */
		LogDebug(COMPONENT_NFS_V4_LOCK, "Stale state");
		res_DELEGRETURN4->status = NFS4ERR_STALE;
		goto out_unlock;
	}

	deleg_heuristics_recall(data->current_obj, owner, state_found);
	reset_cbgetattr_stats(data->current_obj);

	/* Release reference taken above. */
	dec_state_owner_ref(owner);

	STATELOCK_lock(data->current_obj);
	/* Now we have a lock owner and a stateid.
	 * Go ahead and push unlock into SAL (and FSAL) to return
	 * the delegation.
	 */
	state_status = release_lease_lock(data->current_obj, state_found);

	res_DELEGRETURN4->status = nfs4_Errno_state(state_status);

	if (state_status == STATE_SUCCESS) {
		/* Successful exit */
		LogDebug(COMPONENT_NFS_V4_LOCK, "Successful exit");

		state_del_locked(state_found);
	}
	STATELOCK_unlock(data->current_obj);

 out_unlock:

	dec_state_t_ref(state_found);

	return nfsstat4_to_nfs_req_result(res_DELEGRETURN4->status);
}				/* nfs4_op_delegreturn */

/**
 * @brief Free memory allocated for DELEGRETURN result
 *
 * This function frees any memory allocated for the result of the
 * DELEGRETURN operation.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 */
void nfs4_op_delegreturn_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
