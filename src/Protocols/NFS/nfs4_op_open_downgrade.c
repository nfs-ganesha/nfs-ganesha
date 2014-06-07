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
 * @file    nfs4_op_open_downgrade.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 * @brief NFS4_OP_OPEN_DOWNGRADE
 *
 * This function implements the NFS4_OP_OPEN_DOWNGRADE operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 370
 *
 */
static nfsstat4 nfs4_do_open_downgrade(struct nfs_argop4 *op,
				       compound_data_t *data,
				       cache_entry_t *entry_file,
				       state_owner_t *owner, state_t *state,
				       char **cause);

int nfs4_op_open_downgrade(struct nfs_argop4 *op, compound_data_t *data,
			   struct nfs_resop4 *resp)
{
	OPEN_DOWNGRADE4args * const arg_OPEN_DOWNGRADE4 =
	    &op->nfs_argop4_u.opopen_downgrade;
	OPEN_DOWNGRADE4res * const res_OPEN_DOWNGRADE4 =
	    &resp->nfs_resop4_u.opopen_downgrade;
	state_t *state_found = NULL;
	state_owner_t *open_owner;
	int rc;
	const char *tag = "OPEN_DOWNGRADE";

	resp->resop = NFS4_OP_OPEN_DOWNGRADE;
	res_OPEN_DOWNGRADE4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_OPEN_DOWNGRADE4->status =
	    nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_OPEN_DOWNGRADE4->status != NFS4_OK)
		return res_OPEN_DOWNGRADE4->status;

	/* Open downgrade is done only on a file */
	if (data->current_filetype != REGULAR_FILE) {
		res_OPEN_DOWNGRADE4->status = NFS4ERR_INVAL;
		return res_OPEN_DOWNGRADE4->status;
	}

	/* Check stateid correctness and get pointer to state */
	rc = nfs4_Check_Stateid(&arg_OPEN_DOWNGRADE4->open_stateid,
				data->current_entry,
				&state_found,
				data,
				STATEID_SPECIAL_FOR_LOCK,
				0,
				false,
				tag);

	if (rc != NFS4_OK) {
		res_OPEN_DOWNGRADE4->status = rc;
		LogDebug(COMPONENT_STATE,
			 "OPEN_DOWNGRADE failed nfs4_Check_Stateid");
		return res_OPEN_DOWNGRADE4->status;
	}

	open_owner = state_found->state_owner;

	pthread_mutex_lock(&open_owner->so_mutex);

	/* Check seqid */
	if (!Check_nfs4_seqid(open_owner,
			      arg_OPEN_DOWNGRADE4->seqid,
			      op,
			      data->current_entry,
			      resp,
			      tag)) {
		/* Response is all setup for us and LogDebug told what was wrong
		 */
		pthread_mutex_unlock(&open_owner->so_mutex);
		return res_OPEN_DOWNGRADE4->status;
	}

	pthread_mutex_unlock(&open_owner->so_mutex);

	/* What kind of open is it ? */
	LogFullDebug(COMPONENT_STATE,
		     "OPEN_DOWNGRADE: Share Deny = %d Share Access = %d ",
		     arg_OPEN_DOWNGRADE4->share_deny,
		     arg_OPEN_DOWNGRADE4->share_access);

	if (data->minorversion > 0) {	/* NFSv4.1 or NFSv4.2 */
		/* NFSv4.1 */
		if ((state_found->state_data.share.share_access &
		     arg_OPEN_DOWNGRADE4->share_access) !=
		    arg_OPEN_DOWNGRADE4->share_access) {
			/* Open share access is not a superset of
			 * downgrade share access
			 */
			res_OPEN_DOWNGRADE4->status = NFS4ERR_INVAL;
			return res_OPEN_DOWNGRADE4->status;
		}

		if ((state_found->state_data.share.share_deny &
		     arg_OPEN_DOWNGRADE4->share_deny) !=
		    arg_OPEN_DOWNGRADE4->share_deny) {
			/* Open share deny is not a superset of
			 * downgrade share deny
			 */
			res_OPEN_DOWNGRADE4->status = NFS4ERR_INVAL;
			return res_OPEN_DOWNGRADE4->status;
		}

		state_found->state_data.share.share_access =
		    arg_OPEN_DOWNGRADE4->share_access;
		state_found->state_data.share.share_deny =
		    arg_OPEN_DOWNGRADE4->share_deny;
	} else {
		/* NFSv4.0 */
		nfsstat4 status4;
		char *cause = "";
		status4 = nfs4_do_open_downgrade(op,
						 data,
						 state_found->state_entry,
						 state_found->state_owner,
						 state_found,
						 &cause);

		if (status4 != NFS4_OK) {
			LogEvent(COMPONENT_STATE,
				 "Failed to open downgrade: %s",
				 cause);
			res_OPEN_DOWNGRADE4->status = status4;
			return res_OPEN_DOWNGRADE4->status;
		}
	}

	/* Successful exit */
	res_OPEN_DOWNGRADE4->status = NFS4_OK;

	/* Handle stateid/seqid for success */
	update_stateid(state_found,
		       &res_OPEN_DOWNGRADE4->OPEN_DOWNGRADE4res_u.resok4.
		       open_stateid,
		       data,
		       tag);

	/* Save the response in the open owner */
	Copy_nfs4_state_req(state_found->state_owner,
			    arg_OPEN_DOWNGRADE4->seqid,
			    op,
			    data->current_entry,
			    resp,
			    tag);

	return res_OPEN_DOWNGRADE4->status;
}				/* nfs4_op_opendowngrade */

/**
 * @brief Free memory allocated for OPEN_DOWNGRADE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_OPEN_DOWNGRADE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_open_downgrade_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_open_downgrade_Free */

void nfs4_op_open_downgrade_CopyRes(OPEN_DOWNGRADE4res *res_dst,
				    OPEN_DOWNGRADE4res *res_src)
{
	/* Nothing to be done */
	return;
}

static nfsstat4 nfs4_do_open_downgrade(struct nfs_argop4 *op,
				       compound_data_t *data,
				       cache_entry_t *entry_file,
				       state_owner_t *owner, state_t *state,
				       char **cause)
{
	state_data_t candidate_data;
	state_status_t state_status;
	OPEN_DOWNGRADE4args *args = &op->nfs_argop4_u.opopen_downgrade;

	candidate_data.share.share_access = args->share_access;
	candidate_data.share.share_deny = args->share_deny;

	PTHREAD_RWLOCK_wrlock(&data->current_entry->state_lock);

	/* Check if given share access is subset of current share access */
	if ((state->state_data.share.share_access & args->share_access) !=
	    (args->share_access)) {
		/* Open share access is not a superset of
		 * downgrade share access
		 */
		*cause = " (invalid share access for downgrade)";
		PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
		return NFS4ERR_INVAL;
	}

	/* Check if given share deny is subset of current share deny */
	if ((state->state_data.share.share_deny & args->share_deny) !=
	    (args->share_deny)) {
		/* Open share deny is not a superset of
		 * downgrade share deny
		 */
		*cause = " (invalid share deny for downgrade)";
		PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
		return NFS4ERR_INVAL;
	}

	/* Check if given share access is previously seen */
	if (state_share_check_prev(state, &candidate_data) != STATE_SUCCESS) {
		*cause = " (share access or deny never seen before)";
		PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
		return NFS4ERR_INVAL;
	}

	state_status = state_share_downgrade(entry_file,
					     &candidate_data, owner, state);

	if (state_status != STATE_SUCCESS) {
		*cause = " (state_share_downgrade failed)";
		PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
		return NFS4ERR_SERVERFAULT;
	}

	PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
	return NFS4_OK;
}
