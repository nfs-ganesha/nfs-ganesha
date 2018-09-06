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
 * @file    nfs4_op_open.c
 * @brief   NFS4_OP_OPEN
 *
 * Function implementing the NFS4_OP_OPEN operation and support code.
 */

#include "config.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "fsal_convert.h"
#include "nfs_creds.h"
#include "export_mgr.h"
#include "nfs_rpc_callback.h"

static const char *open_tag = "OPEN";

/**
 * @brief Copy an OPEN result
 *
 * This function copies an open result to the supplied destination.
 *
 * @param[out] res_dst Buffer to which to copy the result
 * @param[in]  res_src The result to copy
 */
void nfs4_op_open_CopyRes(OPEN4res *res_dst, OPEN4res *res_src)
{
	res_dst->OPEN4res_u.resok4.attrset = res_src->OPEN4res_u.resok4.attrset;
}

/**
 * @brief Create an NFSv4 filehandle
 *
 * This function creates an NFSv4 filehandle from the supplied file
 * and sets it to be the current filehandle.
 *
 * @note This calls @ref set_current_entry which takes a ref; this then drops
 * it's ref.
 *
 * @param[in,out] data   Compound's data
 * @param[in]     obj    File
 *
 * @retval NFS4_OK on success.
 * @retval Valid errors for NFS4_OP_OPEN.
 */

static nfsstat4 open4_create_fh(compound_data_t *data,
				struct fsal_obj_handle *obj,
				bool state_lock_held)
{
	bool set_no_cleanup = false;

	/* Building a new fh */
	if (!nfs4_FSALToFhandle(false, &data->currentFH, obj,
					op_ctx->ctx_export)) {
		obj->obj_ops->put_ref(obj);
		return NFS4ERR_SERVERFAULT;
	}

	/* Update the current entry */
	set_current_entry(data, obj);

	if (state_lock_held) {
		/* Make sure we don't do cleanup holding the state_lock.
		 * there will be an additional put_ref without the state_lock
		 * being held.
		 */
		obj->state_hdl->no_cleanup = true;
		set_no_cleanup = true;
	}

	/* Put our ref */
	obj->obj_ops->put_ref(obj);

	if (set_no_cleanup) {
		/* And clear the no_cleanup we set above. */
		obj->state_hdl->no_cleanup = false;
	}

	return NFS4_OK;
}

/**
 * @brief Validate claim type
 *
 * Check that the claim type specified is allowed and return the
 * appropriate error code if not.
 *
 * @param[in]  data         Compound's data
 * @param[in]  claim        Claim type
 *
 * @retval NFS4_OK claim is valid.
 * @retval NFS4ERR_GRACE new open not allowed in grace period.
 * @retval NFS4ERR_NO_GRACE reclaim not allowed after grace period or
 *         reclaim complete.
 * @retval NFS4ERR_NOTSUPP claim type not supported by minor version.
 * @retval NFS4ERR_INVAL unknown claim type.
 */

static nfsstat4 open4_validate_claim(compound_data_t *data,
				     open_claim_type4 claim,
				     nfs_client_id_t *clientid)
{
	/* Return code */
	nfsstat4 status = NFS4_OK;
	/* Indicate if we let FSAL to handle requests during grace. */
	bool_t fsal_grace = false;

	/* Pick off erroneous claims so we don't have to deal with
	   them later. */

	switch (claim) {
	case CLAIM_NULL:
		if (nfs_in_grace() || ((data->minorversion > 0)
		    && !clientid->cid_cb.v41.cid_reclaim_complete))
			status = NFS4ERR_GRACE;
		break;

	case CLAIM_FH:
		if (data->minorversion == 0)
			status = NFS4ERR_NOTSUPP;

		if (op_ctx->fsal_export->exp_ops.fs_supports(
					op_ctx->fsal_export, fso_grace_method))
			fsal_grace = true;
		if (!fsal_grace && nfs_in_grace())
			status = NFS4ERR_GRACE;
		break;

	case CLAIM_DELEGATE_PREV:
		status = NFS4ERR_NOTSUPP;
		break;

	case CLAIM_PREVIOUS:
		if (!clientid->cid_allow_reclaim || !nfs_in_grace()
		    || ((data->minorversion > 0)
		    && clientid->cid_cb.v41.cid_reclaim_complete))
			status = NFS4ERR_NO_GRACE;
		break;

	case CLAIM_DELEGATE_CUR:
		break;

	case CLAIM_DELEG_CUR_FH:
	case CLAIM_DELEG_PREV_FH:
		status = NFS4ERR_NOTSUPP;
		break;

	default:
		status = NFS4ERR_INVAL;
	}

	return status;
}

/**
 * @brief Validate and create an open owner
 *
 * This function finds or creates an owner to be associated with the
 * requested open state.
 *
 * @param[in]     arg      Arguments to OPEN4 operation
 * @param[in,out] data     Compound's data
 * @param[out]    res      Response to OPEN4 operation
 * @param[in]     clientid Clientid record for this request
 * @param[out]    owner    The found/created owner owner
 *
 * @return false if error or replay (res_OPEN4->status is already set),
 *         true otherwise.
 */

bool open4_open_owner(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *res, nfs_client_id_t *clientid,
		      state_owner_t **owner)
{
	/* Shortcut to open args */
	OPEN4args * const arg_OPEN4 = &(op->nfs_argop4_u.opopen);
	/* Shortcut to open args */
	OPEN4res * const res_OPEN4 = &(res->nfs_resop4_u.opopen);
	/* The parsed-out name of the open owner */
	state_nfs4_owner_name_t owner_name;
	/* Indicates if the owner is new */
	bool_t isnew;
	/* Return value of FSAL operations */
	fsal_status_t status = {0, 0};
	struct fsal_obj_handle *obj_lookup = NULL;

	/* Is this open_owner known? If so, get it so we can use
	 * replay cache
	 */
	convert_nfs4_open_owner(&arg_OPEN4->owner, &owner_name);

	/* If this open owner is not known yet, allocate and set up a new one */
	*owner = create_nfs4_owner(&owner_name,
				   clientid,
				   STATE_OPEN_OWNER_NFSV4,
				   NULL,
				   0,
				   &isnew,
				   CARE_ALWAYS, data->minorversion != 0);

	LogStateOwner("Open: ", *owner);

	if (*owner == NULL) {
		res_OPEN4->status = NFS4ERR_RESOURCE;
		LogEvent(COMPONENT_STATE,
			 "NFS4 OPEN returning NFS4ERR_RESOURCE for CLAIM_NULL (could not create NFS4 Owner");
		return false;
	}

	/* Seqid checking is only proper for reused NFSv4.0 owner */
	if (isnew || (data->minorversion != 0))
		return true;

	if (arg_OPEN4->seqid == 0) {
		LogDebug(COMPONENT_STATE,
			 "Previously known open_owner is used with seqid=0, ask the client to confirm it again");
		(*owner)->so_owner.so_nfs4_owner.so_confirmed = false;
		return true;
	}

	/* Check for replay */
	if (Check_nfs4_seqid(*owner,
			     arg_OPEN4->seqid,
			     op,
			     data->current_obj,
			     res,
			     open_tag)) {
		/* No replay */
		return true;
	}

	/* Response is setup for us and LogDebug told what was
	 * wrong.
	 *
	 * Or if this is a seqid replay, find the file entry
	 * and update currentFH
	 */
	if (res_OPEN4->status == NFS4_OK) {
		utf8string *utfile;
		open_claim4 *oc = &arg_OPEN4->claim;
		char *filename;

		/* Load up CLAIM_DELEGATE_CUR file */

		switch (oc->claim) {
		case CLAIM_NULL:
			utfile = &oc->open_claim4_u.file;
			break;
		case CLAIM_DELEGATE_CUR:
			utfile = &oc->open_claim4_u.delegate_cur_info.file;
			break;
		case CLAIM_DELEGATE_PREV:
		default:
			return false;
		}
		/* Check if filename is correct */
		res_OPEN4->status = nfs4_utf8string2dynamic(
					utfile, UTF8_SCAN_ALL, &filename);

		if (res_OPEN4->status != NFS4_OK)
			return false;

		status = fsal_lookup(data->current_obj,
				     filename,
				     &obj_lookup,
				     NULL);

		gsh_free(filename);

		if (obj_lookup == NULL) {
			res_OPEN4->status = nfs4_Errno_status(status);
			return false;
		}
		res_OPEN4->status = open4_create_fh(data, obj_lookup, false);
	}

	return false;
}

/**
 * @brief Check delegation claims while opening a file
 *
 * This function implements the CLAIM_DELEGATE_CUR claim.
 *
 * @param[in]     arg   OPEN4 arguments
 * @param[in,out] data  Comopund's data
 */
static nfsstat4 open4_claim_deleg(OPEN4args *arg, compound_data_t *data)
{
	open_claim_type4 claim = arg->claim.claim;
	stateid4 *rcurr_state;
	struct fsal_obj_handle *obj_lookup;
	const utf8string *utfname;
	char *filename;
	fsal_status_t fsal_status;
	nfsstat4 status;
	state_t *found_state = NULL;

	if (!(op_ctx->fsal_export->exp_ops.fs_supports(
					op_ctx->fsal_export, fso_delegations_w)
	      || op_ctx->fsal_export->exp_ops.fs_supports(
					op_ctx->fsal_export, fso_delegations_r))
	      ) {
		LogDebug(COMPONENT_STATE,
			 "NFS4 OPEN returning NFS4ERR_NOTSUPP for CLAIM_DELEGATE");
		return NFS4ERR_NOTSUPP;
	}

	assert(claim == CLAIM_DELEGATE_CUR);
	utfname = &arg->claim.open_claim4_u.delegate_cur_info.file;
	rcurr_state =
		&arg->claim.open_claim4_u.delegate_cur_info.delegate_stateid;

	LogDebug(COMPONENT_NFS_V4, "file name: %.*s",
		 utfname->utf8string_len, utfname->utf8string_val);

	/* Check if filename is correct */
	status = nfs4_utf8string2dynamic(utfname, UTF8_SCAN_ALL, &filename);
	if (status != NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4, "Invalid filename");
		return status;
	}

	/* Does a file with this name already exist ? */
	fsal_status = fsal_lookup(data->current_obj, filename,
				  &obj_lookup, NULL);

	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_NFS_V4, "%s lookup failed.", filename);
		gsh_free(filename);
		return nfs4_Errno_status(fsal_status);
	}
	gsh_free(filename);

	status = open4_create_fh(data, obj_lookup, false);
	if (status != NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4, "open4_create_fh failed");
		return status;
	}

	found_state = nfs4_State_Get_Pointer(rcurr_state->other);

	if (found_state == NULL) {
		LogDebug(COMPONENT_NFS_V4,
			 "state not found with CLAIM_DELEGATE_CUR");
		return NFS4ERR_BAD_STATEID;
	} else {
		if (isFullDebug(COMPONENT_NFS_V4)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_stateid(&dspbuf, found_state);

			LogFullDebug(COMPONENT_NFS_V4,
				     "found matching %s", str);
		}
		dec_state_t_ref(found_state);
	}

	LogFullDebug(COMPONENT_NFS_V4, "done with CLAIM_DELEGATE_CUR");

	return NFS4_OK;
}

/**
 * @brief Create a new delegation state then get the delegation.
 *
 * Create a new delegation state for this client and file.
 * Then attempt to get a LEASE lock to delegate the file
 * according to whether the client opened READ or READ/WRITE.
 *
 * @note state_lock must be held for WRITE
 *
 * @param[in] data Compound data for this request
 * @param[in] op NFS arguments for the request
 * @param[in] open_state Open state for the inode to be delegated.
 * @param[in] openowner Open owner of the open state.
 * @param[in] client Client that will own the delegation.
 * @param[in/out] resok Delegation attempt result to be returned to client.
 * @param[in] prerecall flag for reclaims.
 */
static void get_delegation(compound_data_t *data, OPEN4args *args,
			   state_t *open_state, state_owner_t *openowner,
			   nfs_client_id_t *client, OPEN4resok *resok,
			   bool prerecall)
{
	state_status_t state_status;
	union state_data state_data;
	open_delegation_type4 deleg_type;
	state_owner_t *clientowner = &client->cid_owner;
	struct state_refer refer;
	state_t *new_state = NULL;
	struct state_hdl *ostate;
	open_write_delegation4 *writeres =
		&resok->delegation.open_delegation4_u.write;
	open_read_delegation4 *readres =
		&resok->delegation.open_delegation4_u.read;
	open_none_delegation4 *whynone =
		&resok->delegation.open_delegation4_u.od_whynone;

	ostate = data->current_obj->state_hdl;
	if (!ostate) {
		LogFullDebug(COMPONENT_NFS_V4_LOCK, "Could not get file state");
		whynone->ond_why = WND4_RESOURCE;
		return;
	}

	/* Record the sequence info */
	if (data->minorversion > 0) {
		memcpy(refer.session,
		       data->session->session_id,
		       sizeof(sessionid4));
		refer.sequence = data->sequence;
		refer.slot = data->slot;
	}

	if (args->share_access & OPEN4_SHARE_ACCESS_WRITE) {
		deleg_type = OPEN_DELEGATE_WRITE;
	} else {
		assert(args->share_access & OPEN4_SHARE_ACCESS_READ);
		deleg_type = OPEN_DELEGATE_READ;
	}

	LogDebug(COMPONENT_STATE, "Attempting to grant %s delegation",
		 deleg_type == OPEN_DELEGATE_WRITE ? "WRITE" : "READ");

	init_new_deleg_state(&state_data, deleg_type, client);

	/* Add the delegation state */
	state_status = state_add_impl(data->current_obj, STATE_TYPE_DELEG,
				      &state_data,
				      clientowner, &new_state,
				      data->minorversion > 0 ? &refer : NULL);
	if (state_status != STATE_SUCCESS) {
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "get delegation call failed to add state with status %s",
			 state_err_str(state_status));
		whynone->ond_why = WND4_RESOURCE;
		return;
	}
	new_state->state_seqid++;

	LogFullDebugOpaque(COMPONENT_STATE,
			   "delegation state added, stateid: %s",
			   100, new_state->stateid_other, OTHERSIZE);

	/* acquire_lease_lock() gets the delegation from FSAL */
	state_status = acquire_lease_lock(ostate, clientowner, new_state);
	if (state_status != STATE_SUCCESS) {
		if (args->claim.claim != CLAIM_PREVIOUS) {
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "get delegation call added state but failed to lock with status %s",
				 state_err_str(state_status));
			state_del_locked(new_state);
			dec_state_t_ref(new_state);
			if (state_status == STATE_LOCK_CONFLICT)
				whynone->ond_why = WND4_CONTENTION;
			else
				whynone->ond_why = WND4_RESOURCE;
			return;
		}
		prerecall = true;
	}

	resok->delegation.delegation_type = deleg_type;
	ostate->file.fdeleg_stats.fds_deleg_type = deleg_type;
	if (deleg_type == OPEN_DELEGATE_WRITE) {
		nfs_space_limit4 *space_limit = &writeres->space_limit;

		space_limit->limitby = NFS_LIMIT_SIZE;
		space_limit->nfs_space_limit4_u.filesize =
				DELEG_SPACE_LIMIT_FILESZ;
		COPY_STATEID(&writeres->stateid, new_state);
		writeres->recall = prerecall;
		get_deleg_perm(&writeres->permissions, deleg_type);
	} else {
		assert(deleg_type == OPEN_DELEGATE_READ);
		COPY_STATEID(&readres->stateid, new_state);
		readres->recall = prerecall;
		get_deleg_perm(&readres->permissions, deleg_type);
	}

	if (isDebug(COMPONENT_NFS_V4_LOCK)) {
		char str1[LOG_BUFF_LEN / 2] = "\0";
		char str2[LOG_BUFF_LEN / 2] = "\0";
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_nfs4_owner(&dspbuf1, openowner);
		display_nfs4_owner(&dspbuf2, clientowner);

		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "get delegation openowner %s clientowner %s status %s",
			 str1, str2, state_err_str(state_status));
	}

	dec_state_t_ref(new_state);
}

static void do_delegation(OPEN4args *arg_OPEN4, OPEN4res *res_OPEN4,
			  compound_data_t *data, state_owner_t *owner,
			  state_t *open_state, nfs_client_id_t *clientid)
{
	OPEN4resok *resok = &res_OPEN4->OPEN4res_u.resok4;
	bool prerecall;
	struct state_hdl *ostate;

	ostate = data->current_obj->state_hdl;
	if (!ostate) {
		LogFullDebug(COMPONENT_STATE, "Could not get file state");
		return;
	}

	/* This will be updated later if we actually delegate */
	if (clientid->cid_minorversion == 0)
		resok->delegation.delegation_type = OPEN_DELEGATE_NONE;
	else
		resok->delegation.delegation_type = OPEN_DELEGATE_NONE_EXT;

	/* Client doesn't want a delegation. */
	if (arg_OPEN4->share_access & OPEN4_SHARE_ACCESS_WANT_NO_DELEG) {
		resok->delegation.open_delegation4_u.od_whynone.ond_why =
								WND4_NOT_WANTED;
		LogFullDebug(COMPONENT_STATE, "Client didn't want delegation.");
		return;
	}

	/* Check if delegations are supported */
	if (!deleg_supported(data->current_obj, op_ctx->fsal_export,
			     op_ctx->export_perms, arg_OPEN4->share_access)) {
		resok->delegation.open_delegation4_u.od_whynone.ond_why =
							WND4_NOT_SUPP_FTYPE;
		LogFullDebug(COMPONENT_STATE, "Delegation type not supported.");
		return;
	}

	/* Decide if we should delegate, then add it. */
	if (can_we_grant_deleg(ostate, open_state) &&
	    should_we_grant_deleg(ostate, clientid, open_state,
				  arg_OPEN4, resok, owner, &prerecall)) {
		/* Update delegation open stats */
		if (ostate->file.fdeleg_stats.fds_num_opens == 0)
			ostate->file.fdeleg_stats.fds_first_open = time(NULL);
		ostate->file.fdeleg_stats.fds_num_opens++;

		LogDebug(COMPONENT_STATE, "Attempting to grant delegation");
		get_delegation(data, arg_OPEN4, open_state, owner, clientid,
			       resok, prerecall);
	}
}

/**
 * @brief NFS4_OP_OPEN create processing for use with extended FSAL API
 *
 * @param[in]     arg         Arguments for nfs4_op
 * @param[in,out] data        Compound request's data
 * @param[out]    res_OPEN4   Results for nfs4_op
 * @param[in,out] verifier    The verifier for exclusive create
 * @param[in,out] createmode  The method of create
 * @param[in,out] sattr       The attributes to set
 *
 */

static void open4_ex_create_args(OPEN4args *arg,
				 compound_data_t *data,
				 OPEN4res *res_OPEN4,
				 void *verifier,
				 enum fsal_create_mode *createmode,
				 struct attrlist *sattr)
{
	createhow4 *createhow = &arg->openhow.openflag4_u.how;
	fattr4 *arg_attrs = NULL;

	*createmode = nfs4_createmode_to_fsal(createhow->mode);

	if (createhow->mode == EXCLUSIVE4_1) {
		memcpy(verifier,
		       createhow->createhow4_u.ch_createboth.cva_verf,
		       sizeof(fsal_verifier_t));
	} else if (createhow->mode == EXCLUSIVE4) {
		memcpy(verifier,
		       createhow->createhow4_u.createverf,
		       sizeof(fsal_verifier_t));
	}

	/* Select the correct attributes */
	if (createhow->mode == GUARDED4 || createhow->mode == UNCHECKED4)
		arg_attrs = &createhow->createhow4_u.createattrs;
	else if (createhow->mode == EXCLUSIVE4_1)
		arg_attrs = &createhow->createhow4_u.ch_createboth.cva_attrs;

	if (arg_attrs != NULL) {
		/* Check if asked attributes are correct */
		if (!nfs4_Fattr_Supported(arg_attrs)) {
			res_OPEN4->status = NFS4ERR_ATTRNOTSUPP;
			return;
		}

		if (!nfs4_Fattr_Check_Access(arg_attrs, FATTR4_ATTR_WRITE)) {
			res_OPEN4->status = NFS4ERR_INVAL;
			return;
		}

		/* Convert the attributes */
		if (arg_attrs->attrmask.bitmap4_len != 0) {
			/* Convert fattr4 so nfs4_sattr */
			res_OPEN4->status =
			    nfs4_Fattr_To_FSAL_attr(sattr, arg_attrs, data);

			if (res_OPEN4->status != NFS4_OK)
				return;
		}

		if (createhow->mode == EXCLUSIVE4_1) {
			/** @todo FSF: this needs to be corrected in case FSAL
			 *             uses different attributes for the
			 *             verifier.
			 */
			/* Check that we aren't trying to set the verifier
			 * attributes.
			 */
			if (FSAL_TEST_MASK(sattr->valid_mask, ATTR_ATIME) ||
			    FSAL_TEST_MASK(sattr->valid_mask, ATTR_MTIME)) {
				res_OPEN4->status = NFS4ERR_INVAL;
				return;
			}
		}

		/* If owner or owner_group are set, and the credential was
		 * squashed, then we must squash the set owner and owner_group.
		 */
		squash_setattr(sattr);
	}

	if (!(sattr->valid_mask & ATTR_MODE)) {
		/* Make sure mode is set, even for exclusive create. */
		sattr->mode = 0600;
		sattr->valid_mask |= ATTR_MODE;
	}
}

/**
 * @brief NFS4_OP_OPEN processing using extended FSAL API
 *
 * This function impelments the NFS4_OP_OPEN operation, which
 * potentially creates and opens a regular file.
 *
 * @param[in]     arg         Arguments for nfs4_op
 * @param[in,out] data        Compound request's data
 * @param[out]    res_OPEN4   Results for nfs4_op
 * @param[out]    file_state  state_t created for this operation
 * @param[out]    new_state   Indicates if the state_t is new
 *
 */

static void open4_ex(OPEN4args *arg,
		     compound_data_t *data,
		     OPEN4res *res_OPEN4,
		     nfs_client_id_t *clientid,
		     state_owner_t *owner,
		     state_t **file_state,
		     bool *new_state)
{
	/* Parent directory in which to open the file. */
	struct fsal_obj_handle *parent = NULL;
	/* The entry we associated with the desired file before open. */
	struct fsal_obj_handle *file_obj = NULL;
	/* Indicator that file_obj came from lookup. */
	bool looked_up_file_obj = false;
	/* The in_obj to pass to fsal_open2. */
	struct fsal_obj_handle *in_obj = NULL;
	/* The entry open associated with the file. */
	struct fsal_obj_handle *out_obj = NULL;
	fsal_openflags_t openflags = 0;
	fsal_openflags_t old_openflags = 0;
	enum fsal_create_mode createmode = FSAL_NO_CREATE;
	/* The filename to create */
	char *filename = NULL;
	/* The supplied calim type */
	open_claim_type4 claim = arg->claim.claim;
	fsal_verifier_t verifier;
	struct attrlist sattr;
	/* Status for fsal calls */
	fsal_status_t status = {0, 0};
	/* The open state for the file */
	bool state_lock_held = false;

	/* Make sure the attributes are initialized */
	memset(&sattr, 0, sizeof(sattr));

	/* Make sure... */
	*file_state = NULL;
	*new_state = false;

	/* Pre-process the claim type */
	switch (claim) {
	case CLAIM_NULL:
		/* Check parent */
		parent = data->current_obj;
		in_obj = parent;

		/* Parent must be a directory */
		if (parent->type != DIRECTORY) {
			if (parent->type == SYMBOLIC_LINK) {
				res_OPEN4->status = NFS4ERR_SYMLINK;
				goto out;
			} else {
				res_OPEN4->status = NFS4ERR_NOTDIR;
				goto out;
			}
		}

		/* Validate and convert the utf8 filename */
		res_OPEN4->status =
		    nfs4_utf8string2dynamic(&arg->claim.open_claim4_u.file,
					    UTF8_SCAN_ALL, &filename);

		if (res_OPEN4->status != NFS4_OK)
			goto out;

		/* Set the createmode if appropriate) */
		if (arg->openhow.opentype == OPEN4_CREATE) {
			open4_ex_create_args(arg, data, res_OPEN4, verifier,
					     &createmode, &sattr);

			if (res_OPEN4->status != NFS4_OK)
				goto out;
		}

		status = fsal_lookup(parent, filename, &file_obj, NULL);

		if (!FSAL_IS_ERROR(status)) {
			/* Check create situations. */
			if (arg->openhow.opentype == OPEN4_CREATE) {
				if (createmode >= FSAL_EXCLUSIVE) {
					/* Could be a replay, need to continue.
					 */
					LogFullDebug(COMPONENT_STATE,
						     "EXCLUSIVE open with existing file %s",
						     filename);
				} else if (createmode == FSAL_GUARDED) {
					/* This will be a failure no matter'
					 * what.
					 */
					looked_up_file_obj = true;
					res_OPEN4->status = NFS4ERR_EXIST;
					goto out;
				} else {
					/* FSAL_UNCHECKED, may be a truncate
					 * and we need to pass in the case
					 * of fsal_reopen2 case.
					 */
					if (FSAL_TEST_MASK(sattr.valid_mask,
							   ATTR_SIZE) &&
					    sattr.filesize == 0) {
						LogFullDebug(COMPONENT_STATE,
							     "Truncate");
						openflags |= FSAL_O_TRUNC;
					}
				}
			}

			/* We found the file by lookup, discard the filename
			 * and remember that we found the entry by lookup.
			 */
			looked_up_file_obj = true;
			gsh_free(filename);
			filename = NULL;
		} else if (status.major != ERR_FSAL_NOENT ||
			   arg->openhow.opentype != OPEN4_CREATE) {
			/* A real error occurred */
			res_OPEN4->status = nfs4_Errno_status(status);
			goto out;
		}

		break;

		/* Both of these just use the current filehandle. */
	case CLAIM_PREVIOUS:
		owner->so_owner.so_nfs4_owner.so_confirmed = true;
		if (!nfs4_check_deleg_reclaim(clientid, &data->currentFH)) {
			/* It must have been revoked. Can't reclaim.*/
			LogInfo(COMPONENT_NFS_V4, "Can't reclaim delegation");
			res_OPEN4->status = NFS4ERR_RECLAIM_BAD;
			goto out;
		}
		openflags |= FSAL_O_RECLAIM;
		file_obj = data->current_obj;
		break;

	case CLAIM_FH:
		file_obj = data->current_obj;
		break;

	case CLAIM_DELEGATE_PREV:
		/* FIXME: Remove this when we have full support
		 * for CLAIM_DELEGATE_PREV and delegpurge operations
		 */
		res_OPEN4->status = NFS4ERR_NOTSUPP;
		goto out;

	case CLAIM_DELEGATE_CUR:
		res_OPEN4->status = open4_claim_deleg(arg, data);
		if (res_OPEN4->status != NFS4_OK)
			goto out;
		openflags |= FSAL_O_RECLAIM;
		file_obj = data->current_obj;
		break;

	default:
		LogFatal(COMPONENT_STATE,
			 "Programming error.  Invalid claim after check.");
	}

	if ((arg->share_access & OPEN4_SHARE_ACCESS_READ) != 0)
		openflags |= FSAL_O_READ;

	if ((arg->share_access & OPEN4_SHARE_ACCESS_WRITE) != 0)
		openflags |= FSAL_O_WRITE;

	if ((arg->share_deny & OPEN4_SHARE_DENY_READ) != 0)
		openflags |= FSAL_O_DENY_READ;

	if ((arg->share_deny & OPEN4_SHARE_DENY_WRITE) != 0)
		openflags |= FSAL_O_DENY_WRITE_MAND;

	/* Check if file_obj a REGULAR_FILE */
	if (file_obj != NULL && file_obj->type != REGULAR_FILE) {
		LogDebug(COMPONENT_NFS_V4,
			 "Wrong file type expected REGULAR_FILE actual %s",
			 object_file_type_to_str(file_obj->type));

		if (file_obj->type == DIRECTORY) {
			res_OPEN4->status = NFS4ERR_ISDIR;
		} else {
			/* All special nodes must return NFS4ERR_SYMLINK for
			 * proper client behavior per this linux-nfs post:
			 * http://marc.info/?l=linux-nfs&m=131342421825436&w=2
			 */
			res_OPEN4->status = NFS4ERR_SYMLINK;
		}

		goto out;
	}

	if (file_obj != NULL) {
		/* Go ahead and take the state lock now. */
		PTHREAD_RWLOCK_wrlock(&file_obj->state_hdl->state_lock);
		state_lock_held = true;
		in_obj = file_obj;

		/* Check if any existing delegations conflict with this open.
		 * Delegation recalls will be scheduled if there is a conflict.
		 */
		if (state_deleg_conflict(file_obj,
					  (arg->share_access &
					   OPEN4_SHARE_ACCESS_WRITE) != 0)) {
			res_OPEN4->status = NFS4ERR_DELAY;
			goto out;
		}

		/* Check if there is already a state for this entry and owner.
		 */
		*file_state = nfs4_State_Get_Obj(file_obj, owner);

		if (isFullDebug(COMPONENT_STATE) && *file_state != NULL) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_stateid(&dspbuf, *file_state);

			LogFullDebug(COMPONENT_STATE,
				     "Found existing state %s",
				     str);
		}

		/* Check if open from another export */
		if (*file_state != NULL &&
		    !state_same_export(*file_state, op_ctx->ctx_export)) {
			LogEvent(COMPONENT_STATE,
				 "Lock Owner Export Conflict, Lock held for export %"
				 PRIu16" request for export %"PRIu16,
				 state_export_id(*file_state),
				 op_ctx->ctx_export->export_id);
			res_OPEN4->status = NFS4ERR_INVAL;
			goto out;
		}
	}

	/* If that did not succeed, allocate a state from the FSAL. */
	if (*file_state == NULL) {
		*file_state = op_ctx->fsal_export->exp_ops.alloc_state(
							op_ctx->fsal_export,
							STATE_TYPE_SHARE,
							NULL);

		/* Remember we allocated a new state */
		*new_state = true;

		/* We are ready to perform the open (with possible create).
		 * in_obj has been set to the file itself or the parent.
		 * filename is NULL if in_obj is the file itself.
		 *
		 * Permission check has been done on directory if appropriate,
		 * otherwise fsal_open2 will do a directory permission
		 * check.
		 *
		 * fsal_open2 handles the permission check on the file
		 * itself and also handles all the share reservation stuff.
		 *
		 * fsal_open2 returns with a ref on out_obj, which should be
		 * passed to the state.
		 */
		LogFullDebug(COMPONENT_STATE,
			     "Calling open2 for %s", filename);

		status = fsal_open2(in_obj,
				    *file_state,
				    openflags,
				    createmode,
				    filename,
				    &sattr,
				    verifier,
				    &out_obj,
				    NULL);

		if (FSAL_IS_ERROR(status)) {
			res_OPEN4->status = nfs4_Errno_status(status);
			goto out;
		}
	} else if (createmode >= FSAL_EXCLUSIVE) {
		/* We have an EXCLUSIVE create with an existing
		 * state. We still need to verify it, but no need
		 * to call reopen2.
		 */
		LogFullDebug(COMPONENT_STATE, "Calling verify2 ");

		status = fsal_verify2(file_obj, verifier);

		if (FSAL_IS_ERROR(status)) {
			res_OPEN4->status = nfs4_Errno_status(status);
			goto out;
		}

		/* We need an extra reference below. */
		file_obj->obj_ops->get_ref(file_obj);
	} else {
		old_openflags =
			file_obj->obj_ops->status2(file_obj, *file_state);

		/* Open upgrade */
		LogFullDebug(COMPONENT_STATE, "Calling reopen2");

		status = fsal_reopen2(file_obj, *file_state,
				      openflags | old_openflags,
				      false);

		if (FSAL_IS_ERROR(status)) {
			res_OPEN4->status = nfs4_Errno_status(status);
			goto out;
		}

		/* We need an extra reference below. */
		file_obj->obj_ops->get_ref(file_obj);
	}

	if (file_obj == NULL) {
		/* We have a new cache inode entry, take the state lock. */
		file_obj = out_obj;
		PTHREAD_RWLOCK_wrlock(&file_obj->state_hdl->state_lock);
		state_lock_held = true;
	}

	/* Now the state_lock is held for sure and we have an extra LRU
	 * reference to file_obj, which is the opened file.
	 */

	if (*new_state) {
		/* The state data to be added */
		union state_data candidate_data;
		/* Tracking data for the open state */
		struct state_refer refer, *p_refer = NULL;
		state_status_t state_status;

		candidate_data.share.share_access =
		    arg->share_access & OPEN4_SHARE_ACCESS_BOTH;
		candidate_data.share.share_deny = arg->share_deny;
		candidate_data.share.share_access_prev =
			(1 << candidate_data.share.share_access);
		candidate_data.share.share_deny_prev =
			(1 << candidate_data.share.share_deny);

		LogFullDebug(COMPONENT_STATE,
			     "Creating new state access=%x deny=%x access_prev=%x deny_prev=%x",
			     candidate_data.share.share_access,
			     candidate_data.share.share_deny,
			     candidate_data.share.share_access_prev,
			     candidate_data.share.share_deny_prev);

		/* Record the sequence info */
		if (data->minorversion > 0) {
			memcpy(refer.session,
			       data->session->session_id,
			       sizeof(sessionid4));
			refer.sequence = data->sequence;
			refer.slot = data->slot;
			p_refer = &refer;
		}

		/* We need to register this state now. */
		state_status = state_add_impl(file_obj,
					      STATE_TYPE_SHARE,
					      &candidate_data,
					      owner,
					      file_state,
					      p_refer);

		if (state_status != STATE_SUCCESS) {
			/* state_add_impl failure closed and freed state.
			 * file_state will also be NULL at this point. Also
			 * release the ref on file_obj, since the state add
			 * failed.
			 */
			file_obj->obj_ops->put_ref(file_obj);
			res_OPEN4->status = nfs4_Errno_state(state_status);
			*new_state = false;
			goto out;
		}

		glist_init(&(*file_state)->state_data.share.share_lockstates);
	}

	res_OPEN4->status = open4_create_fh(data, file_obj, true);

	if (res_OPEN4->status != NFS4_OK) {
		if (*new_state) {
			/* state_del_locked will close the file. */
			state_del_locked(*file_state);
			*file_state = NULL;
			*new_state = false;
		} else {
			/*Do an open downgrade to the old open flags */
			status = file_obj->obj_ops->reopen2(file_obj,
							   *file_state,
							   old_openflags);
			if (FSAL_IS_ERROR(status)) {
				LogCrit(COMPONENT_NFS_V4,
					"Failed to allocate handle, reopen2 failed with %s",
					fsal_err_txt(status));
			}

			/* Need to release the state_lock before the put_ref
			 * call.
			 */
			PTHREAD_RWLOCK_unlock(&file_obj->state_hdl->state_lock);
			state_lock_held = false;

			/* Release the extra LRU reference on file_obj. */
			file_obj->obj_ops->put_ref(file_obj);
			goto out;
		}
	}

	/* Since open4_create_fh succeeded the LRU reference to file_obj was
	 * consumed by data->current_obj.
	 */

	if (!(*new_state)) {
		LogFullDebug(COMPONENT_STATE,
			     "Open upgrade old access=%x deny=%x access_prev=%x deny_prev=%x",
			     (*file_state)->state_data.share.share_access,
			     (*file_state)->state_data.share.share_deny,
			     (*file_state)->state_data.share.share_access_prev,
			     (*file_state)->state_data.share.share_deny_prev);

		LogFullDebug(COMPONENT_STATE,
			     "Open upgrade to access=%x deny=%x",
			     arg->share_access,
			     arg->share_deny);

		/* Update share_access and share_deny */
		(*file_state)->state_data.share.share_access |=
			arg->share_access & OPEN4_SHARE_ACCESS_BOTH;

		(*file_state)->state_data.share.share_deny |=
			arg->share_deny;

		/* Update share_access_prev and share_deny_prev */
		(*file_state)->state_data.share.share_access_prev |=
			(1 << (arg->share_access & OPEN4_SHARE_ACCESS_BOTH));

		(*file_state)->state_data.share.share_deny_prev |=
			(1 << arg->share_deny);

		LogFullDebug(COMPONENT_STATE,
			     "Open upgrade new access=%x deny=%x access_prev=%x deny_prev=%x",
			     (*file_state)->state_data.share.share_access,
			     (*file_state)->state_data.share.share_deny,
			     (*file_state)->state_data.share.share_access_prev,
			     (*file_state)->state_data.share.share_deny_prev);
	}

	do_delegation(arg, res_OPEN4, data, owner, *file_state, clientid);
 out:

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	if (state_lock_held)
		PTHREAD_RWLOCK_unlock(&file_obj->state_hdl->state_lock);

	if (filename)
		gsh_free(filename);

	if (res_OPEN4->status != NFS4_OK) {
		/* Cleanup state on error */
		if (*new_state)
			(*file_state)
				->state_exp->exp_ops.free_state(
					(*file_state)->state_exp, *file_state);
		else if (*file_state != NULL)
			dec_state_t_ref(*file_state);
		*file_state = NULL;
	}

	if (looked_up_file_obj) {
		/* We got file_obj via lookup, we need to unref it. */
		file_obj->obj_ops->put_ref(file_obj);
	}
}

/**
 * @brief NFS4_OP_OPEN
 *
 * This function impelments the NFS4_OP_OPEN operation, which
 * potentially creates and opens a regular file.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 369-70
 */

int nfs4_op_open(struct nfs_argop4 *op, compound_data_t *data,
		 struct nfs_resop4 *resp)
{
	/* Shorter alias for OPEN4 arguments */
	OPEN4args * const arg_OPEN4 = &(op->nfs_argop4_u.opopen);
	/* Shorter alias for OPEN4 response */
	OPEN4res * const res_OPEN4 = &(resp->nfs_resop4_u.opopen);
	/* The handle from which the change_info4 is to be
	 * generated.  Every mention of change_info4 in RFC5661
	 * speaks of the parent directory of the file being opened.
	 * However, with CLAIM_FH, CLAIM_DELEG_CUR_FH, and
	 * CLAIM_DELEG_PREV_FH, there is no way to derive the parent
	 * directory from the file handle.  It is Unclear what the
	 * correct behavior is.  In our implementation, we take the
	 * change_info4 of whatever filehandle is current when the
	 * OPEN operation is invoked.
	 */
	struct fsal_obj_handle *obj_change = NULL;
	/* The found client record */
	nfs_client_id_t *clientid = NULL;
	/* The found or created state owner for this open */
	state_owner_t *owner = NULL;
	/* The supplied calim type */
	open_claim_type4 claim = arg_OPEN4->claim.claim;
	/* The open state for the file */
	state_t *file_state = NULL;
	/* True if the state was newly created */
	bool new_state = false;
	int retval;

	LogDebug(COMPONENT_STATE,
		 "Entering NFS v4 OPEN handler -----------------------------");

	/* What kind of open is it ? */
	LogFullDebug(COMPONENT_STATE,
		     "OPEN: Claim type = %d, Open Type = %d, Share Deny = %d, Share Access = %d ",
		     arg_OPEN4->claim.claim,
		     arg_OPEN4->openhow.opentype,
		     arg_OPEN4->share_deny,
		     arg_OPEN4->share_access);

	resp->resop = NFS4_OP_OPEN;
	res_OPEN4->status = NFS4_OK;
	res_OPEN4->OPEN4res_u.resok4.rflags = 0;

	/* Check export permissions if OPEN4_CREATE */
	if ((arg_OPEN4->openhow.opentype == OPEN4_CREATE) &&
	    ((op_ctx->export_perms->options &
	      EXPORT_OPTION_MD_WRITE_ACCESS) == 0)) {
		res_OPEN4->status = NFS4ERR_ROFS;

		LogDebug(COMPONENT_NFS_V4,
			 "Status of OP_OPEN due to export permissions = %s",
			 nfsstat4_to_str(res_OPEN4->status));
		return res_OPEN4->status;
	}

	/* Check export permissions if OPEN4_SHARE_ACCESS_WRITE */
	if (((arg_OPEN4->share_access & OPEN4_SHARE_ACCESS_WRITE) != 0) &&
	    ((op_ctx->export_perms->options &
	      EXPORT_OPTION_WRITE_ACCESS) == 0)) {
		res_OPEN4->status = NFS4ERR_ROFS;

		LogDebug(COMPONENT_NFS_V4,
			 "Status of OP_OPEN due to export permissions = %s",
			 nfsstat4_to_str(res_OPEN4->status));

		return res_OPEN4->status;
	}

	/* Do basic checks on a filehandle */
	res_OPEN4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_OPEN4->status != NFS4_OK)
		return res_OPEN4->status;

	if (data->current_obj == NULL) {
		/* This should be impossible, as PUTFH fills in the
		 * current entry and previous checks weed out handles
		 * in the PseudoFS and DS handles.
		 */
		res_OPEN4->status = NFS4ERR_SERVERFAULT;
		LogCrit(COMPONENT_NFS_V4,
			"Impossible condition in compound data at %s:%u.",
			__FILE__, __LINE__);
		goto out3;
	}

	/* It this a known client id? */
	LogDebug(COMPONENT_STATE,
		 "OPEN Client id = %" PRIx64,
		 arg_OPEN4->owner.clientid);

	retval = nfs_client_id_get_confirmed(
		data->minorversion == 0 ?
			arg_OPEN4->owner.clientid :
			data->session->clientid,
		&clientid);

	if (retval != CLIENT_ID_SUCCESS) {
		res_OPEN4->status = clientid_error_to_nfsstat(retval);
		LogDebug(COMPONENT_NFS_V4,
			 "nfs_client_id_get_confirmed failed");
		return res_OPEN4->status;
	}

	/* Check if lease is expired and reserve it */
	PTHREAD_MUTEX_lock(&clientid->cid_mutex);

	if (data->minorversion == 0 && !reserve_lease(clientid)) {
		PTHREAD_MUTEX_unlock(&clientid->cid_mutex);
		res_OPEN4->status = NFS4ERR_EXPIRED;
		LogDebug(COMPONENT_NFS_V4, "Lease expired");
		goto out3;
	}

	PTHREAD_MUTEX_unlock(&clientid->cid_mutex);

	/* Get the open owner */
	if (!open4_open_owner(op, data, resp, clientid, &owner)) {
		LogDebug(COMPONENT_NFS_V4, "open4_open_owner failed");
		goto out2;
	}

	/* Do the claim check here, so we can save the result in the
	 * owner for NFSv4.0.
	 */
	res_OPEN4->status = open4_validate_claim(data, claim, clientid);

	if (res_OPEN4->status != NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4, "open4_validate_claim failed");
		goto out;
	}

	/* After this point we know we have only CLAIM_NULL,
	 * CLAIM_FH, or CLAIM_PREVIOUS, and that our grace period and
	 * minor version are appropriate for the claim specified.
	 */
	if ((arg_OPEN4->openhow.opentype == OPEN4_CREATE)
	    && (claim != CLAIM_NULL)) {
		res_OPEN4->status = NFS4ERR_INVAL;
		LogDebug(COMPONENT_NFS_V4, "OPEN4_CREATE but not CLAIM_NULL");
		goto out2;
	}

	/* So we still have a reference even after we repalce the
	 * current FH.
	 */
	obj_change = data->current_obj;
	obj_change->obj_ops->get_ref(obj_change);

	/* Update the change info for entry_change. */
	res_OPEN4->OPEN4res_u.resok4.cinfo.before =
		fsal_get_changeid4(obj_change);

	/* Check if share_access does not have any access set, or has
	 * invalid bits that are set.  check that share_deny doesn't
	 * have any invalid bits set.
	 */
	if (!(arg_OPEN4->share_access & OPEN4_SHARE_ACCESS_BOTH)
	    || (data->minorversion == 0
		&& arg_OPEN4->share_access & ~OPEN4_SHARE_ACCESS_BOTH)
	    || (arg_OPEN4->share_access & (~OPEN4_SHARE_ACCESS_WANT_DELEG_MASK &
					   ~OPEN4_SHARE_ACCESS_BOTH))
	    || (arg_OPEN4->share_deny & ~OPEN4_SHARE_DENY_BOTH)) {
		res_OPEN4->status = NFS4ERR_INVAL;
		LogDebug(COMPONENT_NFS_V4,
			 "Invalid SHARE_ACCESS or SHARE_DENY");
		goto out;
	}

	/* Utilize the extended FSAL APU functionality to perform the open. */
	open4_ex(arg_OPEN4, data, res_OPEN4, clientid,
		 owner, &file_state, &new_state);

	if (res_OPEN4->status != NFS4_OK)
		goto out;

	memset(&res_OPEN4->OPEN4res_u.resok4.attrset,
	       0,
	       sizeof(struct bitmap4));

	if (arg_OPEN4->openhow.openflag4_u.how.mode == EXCLUSIVE4 ||
	    arg_OPEN4->openhow.openflag4_u.how.mode == EXCLUSIVE4_1) {
		struct bitmap4 *bits = &res_OPEN4->OPEN4res_u.resok4.attrset;

		set_attribute_in_bitmap(bits, FATTR4_TIME_ACCESS);
		set_attribute_in_bitmap(bits, FATTR4_TIME_MODIFY);
	}

	/* If server use OPEN_CONFIRM4, set the correct flag,
	 * but not for 4.1 */
	if (owner->so_owner.so_nfs4_owner.so_confirmed == false)
		res_OPEN4->OPEN4res_u.resok4.rflags |= OPEN4_RESULT_CONFIRM;

	res_OPEN4->OPEN4res_u.resok4.rflags |= OPEN4_RESULT_LOCKTYPE_POSIX;

	LogFullDebug(COMPONENT_STATE, "NFS4 OPEN returning NFS4_OK");

	/* regular exit */
	res_OPEN4->status = NFS4_OK;

	/* Update change_info4 */
	res_OPEN4->OPEN4res_u.resok4.cinfo.after =
		fsal_get_changeid4(obj_change);
	res_OPEN4->OPEN4res_u.resok4.cinfo.atomic = FALSE;

	/* Handle open stateid/seqid for success */
	update_stateid(file_state,
		       &res_OPEN4->OPEN4res_u.resok4.stateid,
		       data,
		       open_tag);

 out:

	if (res_OPEN4->status != NFS4_OK) {
		LogDebug(COMPONENT_STATE, "failed with status %s",
			 nfsstat4_to_str(res_OPEN4->status));
	}

	/* Save the response in the open owner.
	 * obj_change is either the parent directory or for a CLAIM_PREV is
	 * the entry itself. In either case, it's the right entry to use in
	 * saving the request results.
	 */
	if (data->minorversion == 0) {
		Copy_nfs4_state_req(owner,
				    arg_OPEN4->seqid,
				    op,
				    obj_change,
				    resp,
				    open_tag);
	}

 out2:

	/* Update the lease before exit */
	if (data->minorversion == 0) {
		PTHREAD_MUTEX_lock(&clientid->cid_mutex);
		update_lease(clientid);
		PTHREAD_MUTEX_unlock(&clientid->cid_mutex);
	}

	if (file_state != NULL)
		dec_state_t_ref(file_state);

	/* Clean up if we have an error exit */
	if ((file_state != NULL) && new_state &&
	    (res_OPEN4->status != NFS4_OK)) {
		/* Need to destroy open owner and state */
		state_del(file_state);
	}

	if (obj_change)
		obj_change->obj_ops->put_ref(obj_change);

	if (owner != NULL) {
		/* Need to release the open owner for this call */
		dec_state_owner_ref(owner);
	}

 out3:

	dec_client_id_ref(clientid);

	return res_OPEN4->status;
}				/* nfs4_op_open */

/**
 * @brief Free memory allocated for OPEN result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_OPEN function.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_open_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
