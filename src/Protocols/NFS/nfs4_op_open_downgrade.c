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
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "fsal.h"

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
				       state_owner_t *owner,
				       state_t *state,
				       char **cause);

enum nfs_req_result nfs4_op_open_downgrade(struct nfs_argop4 *op,
					   compound_data_t *data,
					   struct nfs_resop4 *resp)
{
	OPEN_DOWNGRADE4args * const arg_OPEN_DOWNGRADE4 =
		&op->nfs_argop4_u.opopen_downgrade;
	OPEN_DOWNGRADE4res * const res_OPEN_DOWNGRADE4 =
		&resp->nfs_resop4_u.opopen_downgrade;
	OPEN_DOWNGRADE4resok *resok =
		&res_OPEN_DOWNGRADE4->OPEN_DOWNGRADE4res_u.resok4;
	state_t *state_found = NULL;
	state_owner_t *open_owner;
	int rc;
	const char *tag = "OPEN_DOWNGRADE";
	char *cause = "";

	resp->resop = NFS4_OP_OPEN_DOWNGRADE;
	res_OPEN_DOWNGRADE4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_OPEN_DOWNGRADE4->status =
	    nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_OPEN_DOWNGRADE4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Open downgrade is done only on a file */
	if (data->current_filetype != REGULAR_FILE) {
		res_OPEN_DOWNGRADE4->status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	/* Check stateid correctness and get pointer to state */
	rc = nfs4_Check_Stateid(&arg_OPEN_DOWNGRADE4->open_stateid,
				data->current_obj,
				&state_found,
				data,
				STATEID_SPECIAL_FOR_LOCK,
				arg_OPEN_DOWNGRADE4->seqid,
				data->minorversion == 0,
				tag);

	if (rc != NFS4_OK && rc != NFS4ERR_REPLAY) {
		res_OPEN_DOWNGRADE4->status = rc;
		LogDebug(COMPONENT_STATE,
			 "OPEN_DOWNGRADE failed nfs4_Check_Stateid");
		return NFS_REQ_ERROR;
	}

	open_owner = get_state_owner_ref(state_found);

	if (open_owner == NULL) {
		/* Unexpected, but something just went stale. */
		res_OPEN_DOWNGRADE4->status = NFS4ERR_STALE;
		goto out2;
	}

	PTHREAD_MUTEX_lock(&open_owner->so_mutex);

	/* Check seqid */
	if (data->minorversion == 0 &&
	    !Check_nfs4_seqid(open_owner,
			      arg_OPEN_DOWNGRADE4->seqid,
			      op,
			      data->current_obj,
			      resp,
			      tag)) {
		/* Response is all setup for us and LogDebug told what was wrong
		 */
		PTHREAD_MUTEX_unlock(&open_owner->so_mutex);
		goto out;
	}

	PTHREAD_MUTEX_unlock(&open_owner->so_mutex);

	/* What kind of open is it ? */
	LogFullDebug(COMPONENT_STATE,
		     "OPEN_DOWNGRADE: Share Deny = %d Share Access = %d ",
		     arg_OPEN_DOWNGRADE4->share_deny,
		     arg_OPEN_DOWNGRADE4->share_access);

	res_OPEN_DOWNGRADE4->status = nfs4_do_open_downgrade(op,
							     data,
							     open_owner,
							     state_found,
							     &cause);

	if (res_OPEN_DOWNGRADE4->status != NFS4_OK) {
		LogEvent(COMPONENT_STATE,
			 "Failed to open downgrade: %s",
			 cause);
		goto out;
	}

	/* Successful exit */
	res_OPEN_DOWNGRADE4->status = NFS4_OK;

	/* Handle stateid/seqid for success */
	update_stateid(state_found, &resok->open_stateid, data, tag);

	/* Save the response in the open owner */
	if (data->minorversion == 0) {
		Copy_nfs4_state_req(open_owner,
				    arg_OPEN_DOWNGRADE4->seqid,
				    op,
				    data->current_obj,
				    resp,
				    tag);
	}

 out:

	dec_state_owner_ref(open_owner);

 out2:

	dec_state_t_ref(state_found);

	return nfsstat4_to_nfs_req_result(res_OPEN_DOWNGRADE4->status);
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
}

void nfs4_op_open_downgrade_CopyRes(OPEN_DOWNGRADE4res *res_dst,
				    OPEN_DOWNGRADE4res *res_src)
{
	/* Nothing to deep copy */
}

static nfsstat4 nfs4_do_open_downgrade(struct nfs_argop4 *op,
				       compound_data_t *data,
				       state_owner_t *owner, state_t *state,
				       char **cause)
{
	state_status_t state_status;
	OPEN_DOWNGRADE4args *args = &op->nfs_argop4_u.opopen_downgrade;
	fsal_status_t fsal_status;
	fsal_openflags_t openflags = 0;

	LogFullDebug(COMPONENT_STATE,
		     "Open downgrade current access=%x deny=%x access_prev=%x deny_prev=%x",
		     state->state_data.share.share_access,
		     state->state_data.share.share_deny,
		     state->state_data.share.share_access_prev,
		     state->state_data.share.share_deny_prev);

	LogFullDebug(COMPONENT_STATE,
		     "Open downgrade to access=%x deny=%x",
		     args->share_access,
		     args->share_deny);

	PTHREAD_RWLOCK_wrlock(&data->current_obj->state_hdl->state_lock);

	/* Check if given share access is subset of current share access */
	if ((state->state_data.share.share_access & args->share_access) !=
	    (args->share_access)) {
		/* Open share access is not a superset of
		 * downgrade share access
		 */
		*cause = " (invalid share access for downgrade)";
		PTHREAD_RWLOCK_unlock(
			&data->current_obj->state_hdl->state_lock);
		return NFS4ERR_INVAL;
	}

	/* Check if given share deny is subset of current share deny */
	if ((state->state_data.share.share_deny & args->share_deny) !=
	    (args->share_deny)) {
		/* Open share deny is not a superset of
		 * downgrade share deny
		 */
		*cause = " (invalid share deny for downgrade)";
		PTHREAD_RWLOCK_unlock(
			&data->current_obj->state_hdl->state_lock);
		return NFS4ERR_INVAL;
	}

	/* Check if given share access is previously seen */
	if (((state->state_data.share.share_access_prev &
	      (1 << args->share_access)) == 0) ||
	     ((state->state_data.share.share_deny_prev &
	      (1 << args->share_deny)) == 0)) {
		*cause = " (share access or deny never seen before)";
		PTHREAD_RWLOCK_unlock(
			&data->current_obj->state_hdl->state_lock);
		return NFS4ERR_INVAL;
	}

	if ((args->share_access & OPEN4_SHARE_ACCESS_READ) != 0)
		openflags |= FSAL_O_READ;

	if ((args->share_access & OPEN4_SHARE_ACCESS_WRITE) != 0)
		openflags |= FSAL_O_WRITE;

	if ((args->share_deny & OPEN4_SHARE_DENY_READ) != 0)
		openflags |= FSAL_O_DENY_READ;

	if ((args->share_deny & OPEN4_SHARE_DENY_WRITE) != 0)
		openflags |= FSAL_O_DENY_WRITE_MAND;


	fsal_status = fsal_reopen2(data->current_obj,
				   state,
				   openflags,
				   true);

	state_status = state_error_convert(fsal_status);

	PTHREAD_RWLOCK_unlock(&data->current_obj->state_hdl->state_lock);

	if (state_status != STATE_SUCCESS) {
		*cause = " (state_share_downgrade failed)";
		return NFS4ERR_SERVERFAULT;
	}
	return NFS4_OK;
}
