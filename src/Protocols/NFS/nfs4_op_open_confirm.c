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
 * @file    nfs4_op_open_confirm.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 * @brief NFS4_OP_OPEN_CONFIRM
 *
 * This function implements the NFS4_OP_OPEN_CONFIRM operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @retval NFS4_OK or errors for NFSv4.0
 * @retval NFS4ERR_NOTSUPP for NFSv4.1
 *
 */
int nfs4_op_open_confirm(struct nfs_argop4 *op, compound_data_t *data,
			 struct nfs_resop4 *resp)
{
	OPEN_CONFIRM4args * const arg_OPEN_CONFIRM4 =
	    &op->nfs_argop4_u.opopen_confirm;
	OPEN_CONFIRM4res * const res_OPEN_CONFIRM4 =
	    &resp->nfs_resop4_u.opopen_confirm;
	int rc = 0;
	state_t *state_found = NULL;
	state_owner_t *open_owner;
	const char *tag = "OPEN_CONFIRM";

	resp->resop = NFS4_OP_OPEN_CONFIRM;
	res_OPEN_CONFIRM4->status = NFS4_OK;

	if (data->minorversion > 0) {
		res_OPEN_CONFIRM4->status = NFS4ERR_NOTSUPP;
		return res_OPEN_CONFIRM4->status;
	}

	/* Do basic checks on a filehandle
	 * Should not operate on non-file objects
	 */
	res_OPEN_CONFIRM4->status =
	    nfs4_sanity_check_FH(data, REGULAR_FILE, false);

	if (res_OPEN_CONFIRM4->status != NFS4_OK)
		return res_OPEN_CONFIRM4->status;

	/* Check stateid correctness and get pointer to state */
	rc =  nfs4_Check_Stateid(&arg_OPEN_CONFIRM4->open_stateid,
				 data->current_entry,
				 &state_found,
				 data,
				 STATEID_SPECIAL_FOR_LOCK,
				 arg_OPEN_CONFIRM4->seqid,
				 data->minorversion == 0,
				 tag);

	if (rc != NFS4_OK && rc != NFS4ERR_REPLAY) {
		res_OPEN_CONFIRM4->status = rc;
		return res_OPEN_CONFIRM4->status;
	}

	open_owner = state_found->state_owner;
	inc_state_owner_ref(open_owner);

	pthread_mutex_lock(&open_owner->so_mutex);

	/* Check seqid */
	if (!Check_nfs4_seqid(open_owner,
			      arg_OPEN_CONFIRM4->seqid,
			      op,
			      data->current_entry,
			      resp,
			      tag)) {
		/* Response is all setup for us and LogDebug
		 * told what was wrong
		 */
		pthread_mutex_unlock(&open_owner->so_mutex);
		dec_state_owner_ref(open_owner);
		return res_OPEN_CONFIRM4->status;
	}

	/* If opened file is already confirmed, retrun NFS4ERR_BAD_STATEID */
	if (open_owner->so_owner.so_nfs4_owner.so_confirmed) {
		pthread_mutex_unlock(&open_owner->so_mutex);
		dec_state_owner_ref(open_owner);
		res_OPEN_CONFIRM4->status = NFS4ERR_BAD_STATEID;
		return res_OPEN_CONFIRM4->status;
	}

	/* Set the state as confirmed */
	open_owner->so_owner.so_nfs4_owner.so_confirmed = true;
	pthread_mutex_unlock(&open_owner->so_mutex);

	/* Handle stateid/seqid for success */
	update_stateid(state_found,
		       &res_OPEN_CONFIRM4->OPEN_CONFIRM4res_u.resok4.
		       open_stateid, data, tag);

	/* Save the response in the open owner */
	Copy_nfs4_state_req(open_owner,
			    arg_OPEN_CONFIRM4->seqid,
			    op,
			    data->current_entry,
			    resp,
			    tag);

	dec_state_owner_ref(open_owner);

	return res_OPEN_CONFIRM4->status;
}				/* nfs4_op_open_confirm */

/**
 * @brief Free memory allocated for OPEN_CONFIRM result
 *
 * Thisf unction frees any memory allocated for the result of the
 * NFS4_OP_OPEN_CONFIRM operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_open_confirm_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_open_confirm_Free */

void nfs4_op_open_confirm_CopyRes(OPEN_CONFIRM4res *resp_dst,
				  OPEN_CONFIRM4res *resp_src)
{
	/* Nothing to be done */
	return;
}
