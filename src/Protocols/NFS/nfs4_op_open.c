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
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "cache_inode_lru.h"
#include "fsal_convert.h"
#include "nfs_creds.h"
#include "export_mgr.h"

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
 * @brief Perform the open operation
 *
 * This function performs the actual open operation in cache_inode and
 * the State Abstraction layer.
 *
 * @param[in]     op        Arguments to the OPEN operation
 * @param[in,out] data      Compound's data
 * @param[in]     owner     The open owner
 * @param[out]    state     The created or found open state
 * @param[out]    new_state True if the state was newly created
 * @param[in]     openflags Open flags for the FSAL
 *
 * @retval NFS4_OK on success.
 * @retval Valid errors for NFS4_OP_OPEN.
 */

static nfsstat4 open4_do_open(struct nfs_argop4 *op, compound_data_t *data,
			      state_owner_t *owner, state_t **state,
			      bool *new_state, fsal_openflags_t openflags)
{
	/* The arguments to the open operation */
	OPEN4args *args = &op->nfs_argop4_u.opopen;
	/* The state to be added */
	state_data_t candidate_data;
	/* The type of state to add */
	state_type_t candidate_type = STATE_TYPE_SHARE;
	/* Return value of state operations */
	state_status_t state_status = STATE_SUCCESS;
	/* Return value of Cache inode operations */
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	/* Iterator for state list */
	struct glist_head *glist = NULL;
	/* Current state being investigated */
	state_t *state_iterate = NULL;
	/* The open state for the file */
	state_t *file_state = NULL;
	/* Tracking data for the open state */
	struct state_refer refer;
	fsal_accessflags_t access_mask = 0;

	*state = NULL;
	*new_state = true;

	/* Record the sequence info */
	if (data->minorversion > 0) {
		memcpy(refer.session,
		       data->session->session_id,
		       sizeof(sessionid4));
		refer.sequence = data->sequence;
		refer.slot = data->slot;
	}

	if (args->share_access & OPEN4_SHARE_ACCESS_WRITE)
		access_mask |= FSAL_WRITE_ACCESS;

	if (args->share_access & OPEN4_SHARE_ACCESS_READ)
		access_mask |= FSAL_READ_ACCESS;

	cache_status =
	    cache_inode_access(data->current_entry, access_mask);

	if (cache_status != CACHE_INODE_SUCCESS) {
		/* If non-permission error, return it. */
		if (cache_status != CACHE_INODE_FSAL_EACCESS) {
			LogDebug(COMPONENT_STATE,
				 "cache_inode_access returned %s",
				 cache_inode_err_str(cache_status));
			return nfs4_Errno(cache_status);
		}

		/* If WRITE access is requested, return permission error */
		if (args->share_access & OPEN4_SHARE_ACCESS_WRITE) {
			LogDebug(COMPONENT_STATE,
				 "cache_inode_access returned %s with ACCESS_WRITE",
				 cache_inode_err_str(cache_status));
			return nfs4_Errno(cache_status);
		}

		/* If just a permission error and file was opend read only,
		 * try execute permission.
		 */
		cache_status = cache_inode_access(
			data->current_entry,
			FSAL_MODE_MASK_SET(FSAL_X_OK) |
				FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE));

		if (cache_status != CACHE_INODE_SUCCESS) {
			LogDebug(COMPONENT_STATE,
				 "cache_inode_access returned %s after checking for executer permission",
				 cache_inode_err_str(cache_status));
			return nfs4_Errno(cache_status);
		}
	}

	candidate_data.share.share_access =
	    args->share_access & OPEN4_SHARE_ACCESS_BOTH;
	candidate_data.share.share_deny = args->share_deny;
	candidate_data.share.share_access_prev = 0;
	candidate_data.share.share_deny_prev = 0;

	state_status =
	    state_share_check_conflict(data->current_entry,
				       candidate_data.share.share_access,
				       candidate_data.share.share_deny);

	/* Quick exit if there is any share conflict */
	if (state_status != STATE_SUCCESS)
		return nfs4_Errno_state(state_status);

	/* Try to find if the same open_owner already has acquired a
	 * stateid for this file
	 */
	glist_for_each(glist, &data->current_entry->state_list) {
		state_iterate = glist_entry(glist, state_t, state_list);

		if (state_iterate->state_type != STATE_TYPE_SHARE)
			continue;

		if (isFullDebug(COMPONENT_STATE)) {
			char str1[HASHTABLE_DISPLAY_STRLEN];
			char str2[HASHTABLE_DISPLAY_STRLEN];

			DisplayOwner(state_iterate->state_owner, str1);
			DisplayOwner(owner, str2);

			LogFullDebug(COMPONENT_STATE,
				     "Comparing state %p owner %s to open owner %s",
				     state_iterate, str1, str2);
		}

		/* Check if open_owner is the same.  Since owners are
		 * created/looked up we should be able to just
		 * compare pointers.
		 */
		if (state_iterate->state_owner == owner) {
			/* We'll be re-using the found state */
			file_state = state_iterate;
			*new_state = false;

			/* If we are re-using stateid, then release
			 * extra reference to open owner
			 */
			break;
		}
	}

	if (*new_state) {
		state_status = state_add_impl(data->current_entry,
					      candidate_type,
					      &candidate_data,
					      owner,
					      &file_state,
					      data->minorversion > 0 ?
							&refer : NULL);

		if (state_status != STATE_SUCCESS)
			return nfs4_Errno_state(state_status);

		glist_init(&(file_state->state_data.share.share_lockstates));

		/* Attach this open to an export */
		file_state->state_export = op_ctx->export;
		PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
		glist_add_tail(&op_ctx->export->exp_state_list,
			       &file_state->state_export_list);
		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);
	} else {
		/* Check if open from another export */
		if (file_state->state_export != op_ctx->export) {
			LogEvent(COMPONENT_STATE,
				 "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
				 file_state->state_export->export_id,
				 file_state->state_export->fullpath,
				 op_ctx->export->export_id,
				 op_ctx->export->fullpath);
			return STATE_INVALID_ARGUMENT;
		}
	}

	/* Fill in the clientid for NFSv4.0 */
	if (data->minorversion == 0) {
		op_ctx->clientid =
		    &owner->so_owner.so_nfs4_owner.so_clientid;
	}

	cache_status = cache_inode_open(data->current_entry, openflags, 0);

	if (cache_status != CACHE_INODE_SUCCESS)
		return nfs4_Errno(cache_status);

	/* Clear the clientid for NFSv4.0 */

	if (data->minorversion == 0)
		op_ctx->clientid = NULL;

	/* Push share state to SAL (and FSAL) and update the union of
	   file share state. */

	if (*new_state) {
		state_status = state_share_add(data->current_entry,
					       owner,
					       file_state,
					       (openflags & FSAL_O_RECLAIM));

		if (state_status != STATE_SUCCESS) {
			cache_status =
			    cache_inode_close(data->current_entry, 0);

			if (cache_status != CACHE_INODE_SUCCESS) {
				/* Log bad close and continue. */
				LogEvent(COMPONENT_STATE,
					 "Failed to close cache inode: "
					 "status=%d", cache_status);
			}

			return nfs4_Errno_state(state_status);
		}
	} else {
		/* If we find the previous share state, update share state. */
		if ((candidate_type == STATE_TYPE_SHARE)
		    && (file_state->state_type == STATE_TYPE_SHARE)) {
			LogFullDebug(COMPONENT_STATE,
				     "Update existing share state");
			state_status = state_share_upgrade(
						   data->current_entry,
						   &candidate_data,
						   owner,
						   file_state,
						   openflags & FSAL_O_RECLAIM);

			if (state_status != STATE_SUCCESS) {
				cache_status =
				    cache_inode_close(data->current_entry, 0);

				if (cache_status != CACHE_INODE_SUCCESS) {
					/* Log bad close and continue. */
					LogEvent(COMPONENT_STATE,
						 "Failed to close cache inode: status=%d",
						 cache_status);
				}
				LogEvent(COMPONENT_STATE,
					 "Failed to update existing share state");
				return nfs4_Errno_state(state_status);
			}
		}
	}

	*state = file_state;
	return NFS4_OK;
}

/**
 * @brief Create an NFSv4 filehandle
 *
 * This function creates an NFSv4 filehandle from the supplied cache
 * entry and sets it to be the current filehandle.
 *
 * @param[in,out] data   Compound's data
 * @param[in]     entry  Cache entry corresponding to the file
 *
 * @retval NFS4_OK on success.
 * @retval Valid errors for NFS4_OP_OPEN.
 */

static nfsstat4 open4_create_fh(compound_data_t *data, cache_entry_t *entry)
{
	nfs_fh4 newfh4;
	struct alloc_file_handle_v4 new_handle;

	newfh4.nfs_fh4_val = (caddr_t) &new_handle;
	newfh4.nfs_fh4_len = sizeof(struct alloc_file_handle_v4);

	/* Building a new fh */
	if (!nfs4_FSALToFhandle(&newfh4,
				entry->obj_handle,
				op_ctx->export))
		return NFS4ERR_SERVERFAULT;

	/* This new fh replaces the current FH */
	data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
	memcpy(data->currentFH.nfs_fh4_val,
	       newfh4.nfs_fh4_val,
	       newfh4.nfs_fh4_len);

	/* Update the current entry */
	set_current_entry(data, entry, true);

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

		if (!fsal_grace() && nfs_in_grace())
			status = NFS4ERR_GRACE;
		break;

	case CLAIM_PREVIOUS:
		if ((clientid->cid_allow_reclaim != 1) || !nfs_in_grace()
		    || ((data->minorversion > 0)
		    && clientid->cid_cb.v41.cid_reclaim_complete))
			status = NFS4ERR_NO_GRACE;
		break;

	case CLAIM_DELEGATE_CUR:
		break;

	case CLAIM_DELEGATE_PREV:
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
	/* Return value of Cache inode operations */
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	/* The filename to create */
	char *filename = NULL;
	cache_entry_t *entry_lookup = NULL;

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
				   CARE_ALWAYS);

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
	} else {
		/* Check for replay */
		if (!Check_nfs4_seqid(*owner,
				      arg_OPEN4->seqid,
				      op,
				      data->current_entry,
				      res,
				      open_tag)) {
			/* Response is setup for us and LogDebug told what was
			 * wrong.
			 *
			 * Or if this is a seqid replay, find the file entry
			 * and update currentFH
			 */
			if (res_OPEN4->status == NFS4_OK) {
				/* Check if filename is correct */
				res_OPEN4->status = nfs4_utf8string2dynamic(
				     &arg_OPEN4->claim.open_claim4_u.file,
				     UTF8_SCAN_ALL,
				     &filename);

				if (res_OPEN4->status != NFS4_OK)
					return false;

				cache_status =
				    cache_inode_lookup(data->current_entry,
						       filename,
						       &entry_lookup);
				if (filename) {
					gsh_free(filename);
					filename = NULL;
				}

				if (entry_lookup == NULL) {
					res_OPEN4->status =
					    nfs4_Errno(cache_status);
					return false;
				}
				res_OPEN4->status =
				    open4_create_fh(data, entry_lookup);
			}

			return false;
		}
	}

	return true;
}

/**
 * @brief Create a named file
 *
 * This function implements the OPEN4_CREATE alternative of
 * CLAIM_NULL.
 *
 * @param[in]      arg      OPEN4 arguments
 * @param[in,out]  data     Comopund's data
 * @param[out]     res      OPEN4 response
 * @param[in]      parent   Directory in which to create the file
 * @param[out]     entry    Entry to be opened
 * @param[in]      filename filename
 */

static nfsstat4 open4_create(OPEN4args *arg, compound_data_t *data,
			     OPEN4res *res, cache_entry_t *parent,
			     cache_entry_t **entry, const char *filename)
{
	/* Newly created file */
	cache_entry_t *entry_newfile = NULL;
	/* Return code from calls made directly to the FSAL. */
	fsal_status_t fsal_status = { 0, 0 };
	/* Convertedattributes to set */
	struct attrlist sattr;
	/* Whether the client supplied any attributes */
	bool sattr_provided = false;
	uint32_t mode = 0600;
	/* Return from Cache Inode calls */
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	/* True if a verifier has been specified and we are
	   performing exclusive creation semantics. */
	bool verf_provided = false;
	/* Client provided verifier, split into two piees */
	uint32_t verf_hi = 0, verf_lo = 0;

	*entry = NULL;

	/* if quota support is active, then we should check is
	   the FSAL allows inode creation or not */
	fsal_status = op_ctx->fsal_export->ops->check_quota(
						op_ctx->fsal_export,
						op_ctx->export->fullpath,
						FSAL_QUOTA_INODES);

	if (FSAL_IS_ERROR(fsal_status))
		return NFS4ERR_DQUOT;

	/* Check if asked attributes are correct */
	if (arg->openhow.openflag4_u.how.mode == GUARDED4
	    || arg->openhow.openflag4_u.how.mode == UNCHECKED4) {
		if (!nfs4_Fattr_Supported(
		     &arg->openhow.openflag4_u.how.createhow4_u.createattrs))
			return NFS4ERR_ATTRNOTSUPP;

		if (!nfs4_Fattr_Check_Access(
		     &arg->openhow.openflag4_u.how.createhow4_u.createattrs,
		     FATTR4_ATTR_WRITE)) {
			return NFS4ERR_INVAL;
		}
		if (arg->openhow.openflag4_u.how.createhow4_u.createattrs.
		    attrmask.bitmap4_len != 0) {
			/* Convert fattr4 so nfs4_sattr */
			res->status = nfs4_Fattr_To_FSAL_attr(
				&sattr,
				&arg->openhow.openflag4_u.how.createhow4_u.
				  createattrs,
				data);

			if (res->status != NFS4_OK)
				return res->status;

			sattr_provided = true;

			if (sattr.mask & ATTR_MODE)
				mode = sattr.mode;

		}
	} else if (arg->openhow.openflag4_u.how.mode == EXCLUSIVE4_1) {
		/**
		 * @note EXCLUSIVE4_1 gets its own attribute check,
		 * because they're stored in a different spot.
		 *
		 * @todo ACE: This can be refactored later.
		 */
		if (!nfs4_Fattr_Supported(
		     &arg->openhow.openflag4_u.how.createhow4_u.ch_createboth.
		     cva_attrs)) {
			return NFS4ERR_ATTRNOTSUPP;
		}
		if (!nfs4_Fattr_Check_Access(
		     &arg->openhow.openflag4_u.how.createhow4_u.ch_createboth.
			cva_attrs,
		     FATTR4_ATTR_WRITE)) {
			return NFS4ERR_INVAL;
		}
		if (arg->openhow.openflag4_u.how.createhow4_u.ch_createboth.
		    cva_attrs.attrmask.bitmap4_len != 0) {
			/* Convert fattr4 so nfs4_sattr */
			res->status = nfs4_Fattr_To_FSAL_attr(
				&sattr,
				&arg->openhow.openflag4_u.how.createhow4_u.
				  ch_createboth.cva_attrs,
				data);

			if (res->status != NFS4_OK)
				return res->status;

			sattr_provided = true;

			if (sattr.mask & ATTR_MODE)
				mode = sattr.mode;
		}
		if (sattr_provided
		    && ((FSAL_TEST_MASK(sattr.mask, ATTR_ATIME))
		    || (FSAL_TEST_MASK(sattr.mask, ATTR_MTIME)))) {
			res->status = NFS4ERR_INVAL;
			return res->status;
		}
	}

	if ((arg->openhow.openflag4_u.how.mode == EXCLUSIVE4_1)
	    || (arg->openhow.openflag4_u.how.mode == EXCLUSIVE4)) {
		char *verf =
		    (char *)((arg->openhow.openflag4_u.how.mode == EXCLUSIVE4_1)
			     ? &arg->openhow.openflag4_u.how.createhow4_u.
				 ch_createboth.cva_verf :
			       &arg->openhow.openflag4_u.how.createhow4_u.
				 createverf);

		verf_provided = true;

		/* If we knew all our FSALs could store a 64 bit
		 * atime, we could just use that and there would be
		 * no need to split the verifier up.
		 */
		memcpy(&verf_hi, verf, sizeof(uint32_t));
		memcpy(&verf_lo, verf + sizeof(uint32_t), sizeof(uint32_t));

		if (!sattr_provided) {
			memset(&sattr, 0, sizeof(struct attrlist));
			sattr.mode = mode;
			sattr_provided = true;
		}

		cache_inode_create_set_verifier(&sattr, verf_hi, verf_lo);
	}

	cache_status = cache_inode_create(parent,
					  filename,
					  REGULAR_FILE,
					  mode,
					  NULL,
					  &entry_newfile);

	/* Complete failure */
	if ((cache_status != CACHE_INODE_SUCCESS)
	    && (cache_status != CACHE_INODE_ENTRY_EXISTS)) {
		return nfs4_Errno(cache_status);
	}

	if (cache_status == CACHE_INODE_ENTRY_EXISTS) {
		if (arg->openhow.openflag4_u.how.mode == GUARDED4) {
			cache_inode_put(entry_newfile);
			entry_newfile = NULL;
			return nfs4_Errno(cache_status);
		} else if (verf_provided
			   && !cache_inode_create_verify(entry_newfile,
							 verf_hi,
							 verf_lo)) {
			cache_inode_put(entry_newfile);
			entry_newfile = NULL;
			return nfs4_Errno(cache_status);
		}

		/* If the object exists already size is the only attribute we
		 * set.
		 */
		if (sattr_provided && (FSAL_TEST_MASK(sattr.mask, ATTR_SIZE))
		    && (sattr.filesize == 0)) {
			FSAL_CLEAR_MASK(sattr.mask);
			FSAL_SET_MASK(sattr.mask, ATTR_SIZE);
		} else {
			sattr_provided = false;
		}

		/* Clear error code */
		cache_status = CACHE_INODE_SUCCESS;
	}

	if (sattr_provided) {
		/* If owner or owner_group are set, and the credential was
		 * squashed, then we must squash the set owner and owner_group.
		 */
		squash_setattr(&sattr);

		/* Skip setting attributes if all asked attributes
		 * are handled by create
		 */
		if ((sattr.mask &
		     (ATTR_ACL | ATTR_ATIME | ATTR_MTIME | ATTR_CTIME |
		      ATTR_SIZE)) ||
		    ((sattr.mask & ATTR_OWNER)
			&& (op_ctx->creds->caller_uid != sattr.owner))
		    || ((sattr.mask & ATTR_GROUP)
			&& (op_ctx->creds->caller_gid != sattr.group))) {

			cache_status =
			    cache_inode_setattr(entry_newfile, &sattr,
					(arg->share_access &
					 OPEN4_SHARE_ACCESS_WRITE) != 0);

			if (cache_status != CACHE_INODE_SUCCESS)
				return nfs4_Errno(cache_status);
		}
	}

	*entry = entry_newfile;
	return nfs4_Errno(cache_status);
}

/**
 * @brief Open or create a named file
 *
 * This function implements the CLAIM_NULL type, which is used to
 * create a new or open a preëxisting file.
 *
 * entry has +1 refcount
 *
 * @param[in]     arg   OPEN4 arguments
 * @param[in,out] data  Comopund's data
 * @param[out]    res   OPEN4 rsponse
 * @param[out]    entry Entry to open
 */

static nfsstat4 open4_claim_null(OPEN4args *arg, compound_data_t *data,
				 OPEN4res *res, cache_entry_t **entry)
{
	/* Parent directory in which to open the file. */
	cache_entry_t *parent = NULL;
	/* Status for cache_inode calls */
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	/* NFS Status from function calls */
	nfsstat4 nfs_status = NFS4_OK;
	/* The filename to create */
	char *filename = NULL;

	/* Validate and convert the utf8 filename */
	nfs_status =
	    nfs4_utf8string2dynamic(&arg->claim.open_claim4_u.file,
				    UTF8_SCAN_ALL,
				    &filename);

	if (nfs_status != NFS4_OK)
		goto out;

	/* Check parent */
	parent = data->current_entry;

	/* Parent must be a directory */
	if ((parent->type != DIRECTORY)) {
		if (parent->type == SYMBOLIC_LINK) {
			nfs_status = NFS4ERR_SYMLINK;
			goto out;
		} else {
			nfs_status = NFS4ERR_NOTDIR;
			goto out;
		}
	}

	switch (arg->openhow.opentype) {
	case OPEN4_CREATE:
		nfs_status =
		    open4_create(arg, data, res, parent, entry, filename);
		break;

	case OPEN4_NOCREATE:
		cache_status =
		    cache_inode_lookup(parent, filename, entry);

		if (cache_status != CACHE_INODE_SUCCESS)
			nfs_status = nfs4_Errno(cache_status);
		break;

	default:
		nfs_status = NFS4ERR_INVAL;
	}

 out:
	if (filename)
		gsh_free(filename);

	return nfs_status;
}


/**
 * @brief Create a new delegation state then get the delegation.
 *
 * Create a new delegation state for this client and file.
 * Then attempt to get a LEASE lock to delegate the file
 * according to whether the client opened READ or READ/WRITE.
 * Note: Entry state needs to be locked before executing this function!
 *
 * @param[in] data Compound data for this request
 * @param[in] op NFS arguments for the request
 * @param[in] open_state Open state for the inode to be delegated.
 * @param[in] openowner Open owner of the open state.
 * @param[in] client Client that will own the delegation.
 * @param[in/out] resok Delegation attempt result to be returned to client.
 */
static void get_delegation(compound_data_t *data, struct nfs_argop4 *op,
			   state_t *open_state, state_owner_t *openowner,
			   nfs_client_id_t *client, OPEN4resok *resok)
{
	state_status_t state_status;
	fsal_lock_param_t lock_desc;
	state_data_t deleg_data, candidate_data, *saved_data;
	open_delegation_type4 deleg_type;
	state_owner_t *clientowner = &client->cid_owner;
	OPEN4args *args = &op->nfs_argop4_u.opopen;
	struct state_refer refer;
	state_t *new_state;

	resok->delegation.delegation_type = OPEN_DELEGATE_NONE;

	/* Record the sequence info */
	if (data->minorversion > 0) {
		memcpy(refer.session,
		       data->session->session_id,
		       sizeof(sessionid4));
		refer.sequence = data->sequence;
		refer.slot = data->slot;
	}

	if (args->share_access & OPEN4_SHARE_ACCESS_WRITE) {
		if (!(op_ctx->export_perms->options &
		      EXPORT_OPTION_WRITE_DELEG)) {
			LogDebug(COMPONENT_STATE,
				 "WRITE delegs not allowed by export.");
			return;
		}
		lock_desc.lock_type = FSAL_LOCK_W;
		deleg_type = OPEN_DELEGATE_WRITE;
	} else if (args->share_access & OPEN4_SHARE_ACCESS_READ) {
		if (!(op_ctx->export_perms->options &
		      EXPORT_OPTION_READ_DELEG)) {
			LogDebug(COMPONENT_STATE,
				 "READ delegs not allowed by export.");
			return;
		}
		lock_desc.lock_type = FSAL_LOCK_R;
		deleg_type = OPEN_DELEGATE_READ;
	} else {
		return;
	}

	LogDebug(COMPONENT_STATE, "Attempting to grant %s delegation",
		 lock_desc.lock_type == FSAL_LOCK_W ? "WRITE" : "READ");

	lock_desc.lock_start = 0;
	lock_desc.lock_length = 0;
	lock_desc.lock_sle_type = FSAL_LEASE_LOCK;

	init_new_deleg_state(&deleg_data, open_state, deleg_type, client);

	/* Check for conflict. */
	candidate_data.share.share_access =
	    args->share_access & OPEN4_SHARE_ACCESS_BOTH;
	candidate_data.share.share_deny = args->share_deny;
	candidate_data.share.share_access_prev = 0;
	candidate_data.share.share_deny_prev = 0;

	state_status =
	    state_share_check_conflict(data->current_entry,
				       candidate_data.share.share_access,
				       candidate_data.share.share_deny);

	/* Add the delegation state */
	state_status = state_add_impl(data->current_entry, STATE_TYPE_DELEG,
				      &deleg_data,
				      clientowner, &new_state,
				      data->minorversion > 0 ? &refer : NULL);
	if (state_status != STATE_SUCCESS) {
		LogDebug(COMPONENT_NFS_V4_LOCK,
			 "get_delegation call failed to add state with status %s",
			 state_err_str(state_status));
		return;
	} else {
		saved_data = &new_state->state_data;
		saved_data->deleg.sd_stateid.seqid = ++new_state->state_seqid;
		memcpy(saved_data->deleg.sd_stateid.other,
		       new_state->stateid_other,
		       sizeof(saved_data->deleg.sd_stateid.other));

		LogFullDebugOpaque(COMPONENT_STATE,
				   "delegation state added, stateid: %s",
				   100, &saved_data->deleg.sd_stateid.other,
				   sizeof(saved_data->deleg.sd_stateid.other));

		/* Attach this open to an export */
		new_state->state_export = op_ctx->export;

		PTHREAD_RWLOCK_wrlock(&op_ctx->export->lock);
		glist_add_tail(&op_ctx->export->exp_state_list,
			       &new_state->state_export_list);
		PTHREAD_RWLOCK_unlock(&op_ctx->export->lock);


		/* PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock); */
		state_status = state_lock(data->current_entry,
					  clientowner,
					  new_state,
					  STATE_NON_BLOCKING,
					  NULL,	/* No block data */
					  &lock_desc,
					  NULL,
					  NULL,
					  LEASE_LOCK);
		if (state_status != STATE_SUCCESS) {
			LogDebug(COMPONENT_NFS_V4_LOCK,
				 "get_delegation call added state but failed to"
				 " lock with status %s",
				 state_err_str(state_status));
			state_del(new_state, false);
			return;
		} else {
			resok->delegation.delegation_type = deleg_type;
			if (deleg_type == OPEN_DELEGATE_WRITE) {
				open_write_delegation4 *writeres =
				&resok->delegation.open_delegation4_u.write;
				writeres->
					space_limit.limitby = NFS_LIMIT_SIZE;
				writeres->
				space_limit.nfs_space_limit4_u.filesize =
									100000;
				writeres->stateid =
						saved_data->deleg.sd_stateid;
				writeres->recall = FALSE;
				get_deleg_perm(data->current_entry,
					       &writeres->permissions,
					       deleg_type);
			} else {
				assert(deleg_type == OPEN_DELEGATE_READ);
				open_read_delegation4 *readres =
				&resok->delegation.open_delegation4_u.read;
				readres->stateid = saved_data->deleg.sd_stateid;
				readres->recall = FALSE;
				get_deleg_perm(data->current_entry,
					       &readres->permissions,
					       deleg_type);
			}
		}
	}

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "get_delegation openowner %p clientowner %p status %s",
		 openowner, clientowner, state_err_str(state_status));
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
	/* The cache entry from which the change_info4 is to be
	 * generated.  Every mention of change_info4 in RFC5661
	 * speaks of the parent directory of the file being opened.
	 * However, with CLAIM_FH, CLAIM_DELEG_CUR_FH, and
	 * CLAIM_DELEG_PREV_FH, there is no way to derive the parent
	 * directory from the file handle.  It is Unclear what the
	 * correct behavior is.  In our implementation, we take the
	 * change_info4 of whatever filehandle is current when the
	 * OPEN operation is invoked.
	 */
	cache_entry_t *entry_change = NULL;
	/* Open flags to be passed to the FSAL */
	fsal_openflags_t openflags;
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
	cache_inode_status_t cache_status;
	char *filename;
	cache_entry_t *entry_parent = data->current_entry;
	cache_entry_t *entry_lookup = NULL;
	struct glist_head *glist;
	state_lock_entry_t *found_entry = NULL;
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

	if (data->current_entry == NULL) {
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
		goto out3;
	}

	/* Check if lease is expired and reserve it */
	pthread_mutex_lock(&clientid->cid_mutex);

	if (!reserve_lease(clientid)) {
		pthread_mutex_unlock(&clientid->cid_mutex);
		res_OPEN4->status = NFS4ERR_EXPIRED;
		LogDebug(COMPONENT_NFS_V4, "Lease expired");
		goto out3;
	}

	pthread_mutex_unlock(&clientid->cid_mutex);

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
	entry_change = data->current_entry;
	cache_inode_lru_ref(entry_change, LRU_FLAG_NONE);

	res_OPEN4->OPEN4res_u.resok4.cinfo.before =
	    cache_inode_get_changeid4(entry_change);

	/* Check if share_access does not have any access set, or has
	 * invalid bits that are set.  check that share_deny doesn't
	 * have any invalid bits set.
	 */
	if (!(arg_OPEN4->share_access & OPEN4_SHARE_ACCESS_BOTH)
	    || (data->minorversion == 0
		&& arg_OPEN4->share_access & ~OPEN4_SHARE_ACCESS_BOTH)
	    || (arg_OPEN4->
		share_access & (~OPEN4_SHARE_ACCESS_WANT_DELEG_MASK &
				~OPEN4_SHARE_ACCESS_BOTH))
	    || (arg_OPEN4->share_deny & ~OPEN4_SHARE_DENY_BOTH)) {
		res_OPEN4->status = NFS4ERR_INVAL;
		LogDebug(COMPONENT_NFS_V4,
			 "Invalid SHARE_ACCESS or SHARE_DENY");
		goto out;
	}

	/* Set the current entry to the file to be opened */
	switch (claim) {
	case CLAIM_NULL:
		{
			cache_entry_t *entry = NULL;
			res_OPEN4->status =
			    open4_claim_null(arg_OPEN4, data, res_OPEN4,
					     &entry);
			if (res_OPEN4->status == NFS4_OK) {
				/* Decrement the current entry here, because
				 * nfs4_create_fh replaces the current fh.
				 */
				set_current_entry(data, NULL, false);
				res_OPEN4->status =
				    open4_create_fh(data, entry);
			}
		}
		break;

		/* Both of these just use the current filehandle. */
	case CLAIM_PREVIOUS:
		owner->so_owner.so_nfs4_owner.so_confirmed = true;
	case CLAIM_FH:
		break;

	case CLAIM_DELEGATE_CUR:

		if (!op_ctx->fsal_export->ops->fs_supports(
				op_ctx->fsal_export, fso_delegations)) {
			res_OPEN4->status = NFS4ERR_NOTSUPP;
			LogDebug(COMPONENT_STATE,
				 "NFS4 OPEN returning NFS4ERR_NOTSUPP for CLAIM_DELEGATE");
			return res_OPEN4->status;
		}

		/* Check for name length */
		if (arg_OPEN4->claim.open_claim4_u.delegate_cur_info.file.
		    utf8string_len > MAXNAMLEN) {
			res_OPEN4->status = NFS4ERR_NAMETOOLONG;
			LogDebug(COMPONENT_STATE,
				 "NFS4 OPEN returning NFS4ERR_NAMETOOLONG for CLAIM_DELEGATE");
			return res_OPEN4->status;
		}

		/* get the filename from the argument, it should not be empty */
		if (arg_OPEN4->claim.open_claim4_u.delegate_cur_info.file.
		    utf8string_len == 0) {
			res_OPEN4->status = NFS4ERR_INVAL;
			LogDebug(COMPONENT_STATE,
				 "NFS4 OPEN returning NFS4ERR_INVAL for CLAIM_DELEGATE");
			return res_OPEN4->status;
		}

		/* get the filename from the argument, it should not be empty */
		if (arg_OPEN4->claim.open_claim4_u.delegate_cur_info.file.
		    utf8string_len == 0) {
			res_OPEN4->status = NFS4ERR_INVAL;
			LogDebug(COMPONENT_NFS_V4, "Invalid filename");
			goto out;
		}

		LogDebug(COMPONENT_NFS_CB,
			 "name len %d %s",
			 arg_OPEN4->claim.open_claim4_u.delegate_cur_info.file.
			 utf8string_len,
			 arg_OPEN4->claim.open_claim4_u.delegate_cur_info.file.
			 utf8string_val);

		/* Check if filename is correct */
		res_OPEN4->status = nfs4_utf8string2dynamic(
			&arg_OPEN4->claim.open_claim4_u.delegate_cur_info.file,
			UTF8_SCAN_ALL,
			&filename);

		if (res_OPEN4->status != NFS4_OK) {
			LogDebug(COMPONENT_NFS_V4, "Invalid filename");
			goto out;
		}

		/* Does a file with this name already exist ? */
		cache_status = cache_inode_lookup(entry_parent,
						  filename,
						  &entry_lookup);

		gsh_free(filename);

		if (cache_status == CACHE_INODE_SUCCESS) {
			PTHREAD_RWLOCK_wrlock(&entry_lookup->state_lock);
			glist_for_each(glist,
				       &entry_lookup->object.file.lock_list) {
				found_entry = glist_entry(glist,
							  state_lock_entry_t,
							  sle_list);

				LogDebug(COMPONENT_NFS_CB, "found_entry %p",
						found_entry);
				file_state = found_entry->sle_state;
				break;
			}
			PTHREAD_RWLOCK_unlock(&entry_lookup->state_lock);

			if (file_state == NULL) {
				res_OPEN4->status = nfs4_Errno(cache_status);
				goto out;
			}
			res_OPEN4->OPEN4res_u.resok4.stateid.seqid =
			    file_state->state_seqid;
			memcpy(res_OPEN4->OPEN4res_u.resok4.stateid.other,
			       file_state->stateid_other, OTHERSIZE);

			res_OPEN4->status = open4_create_fh(data, entry_lookup);
			if (res_OPEN4->status != NFS4_OK) {
				LogDebug(COMPONENT_NFS_V4,
					 "open4_create_fh failed");
				goto out;
			}

			LogDebug(COMPONENT_NFS_V4,
				 "done with CLAIM_DELEGATE_CUR");
			goto out;
		} else if (cache_status == CACHE_INODE_NOT_FOUND) {
			LogDebug(COMPONENT_NFS_CB, "did not find entry");
			break;
		} else {
			res_OPEN4->status = nfs4_Errno(cache_status);
			return res_OPEN4->status;
		}

	default:
		LogFatal(COMPONENT_STATE,
			 "Programming error.  Invalid claim after check.");
		break;
	}

	if (res_OPEN4->status != NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4, "general failure");
		goto out;
	}

	/* OPEN4 is to be done on a file */
	if (data->current_filetype != REGULAR_FILE) {
		LogDebug(COMPONENT_NFS_V4,
			 "Wrong file type expected REGULAR_FILE actual %s",
			 object_file_type_to_str(data->current_filetype));

		if (data->current_filetype == DIRECTORY) {
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

	/* Set openflags. */
	switch (arg_OPEN4->share_access & OPEN4_SHARE_ACCESS_BOTH) {
	case OPEN4_SHARE_ACCESS_READ:
		openflags = FSAL_O_READ;
		break;
	case OPEN4_SHARE_ACCESS_WRITE:
		/* clients may read as well due to buffer cache constraints */
		/* Fallthrough */
	case OPEN4_SHARE_ACCESS_BOTH:
	default:
		openflags = FSAL_O_RDWR;
		break;
	}

	if (arg_OPEN4->claim.claim)
		openflags = FSAL_O_RECLAIM;

	PTHREAD_RWLOCK_wrlock(&data->current_entry->state_lock);

	res_OPEN4->status = open4_do_open(op,
					  data,
					  owner,
					  &file_state,
					  &new_state,
					  openflags);

	PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);

	if (res_OPEN4->status != NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4, "open4_do_open failed");
		goto out;
	}

	memset(&res_OPEN4->OPEN4res_u.resok4.attrset,
	       0,
	       sizeof(struct bitmap4));

	/* If server use OPEN_CONFIRM4, set the correct flag,
	 * but not for 4.1 */
	if (data->minorversion == 0
	    && owner->so_owner.so_nfs4_owner.so_confirmed == false) {
		res_OPEN4->OPEN4res_u.resok4.rflags |= OPEN4_RESULT_CONFIRM;
	}

	res_OPEN4->OPEN4res_u.resok4.rflags |= OPEN4_RESULT_LOCKTYPE_POSIX;

	LogFullDebug(COMPONENT_STATE, "NFS4 OPEN returning NFS4_OK");

	/* regular exit */
	res_OPEN4->status = NFS4_OK;

	/* Update change_info4 */
	res_OPEN4->OPEN4res_u.resok4.cinfo.after =
		cache_inode_get_changeid4(entry_change);
	cache_inode_put(entry_change);
	entry_change = NULL;
	res_OPEN4->OPEN4res_u.resok4.cinfo.atomic = FALSE;

	/* This will be updated laster if we actually delegate */
	res_OPEN4->OPEN4res_u.resok4.delegation.delegation_type =
		OPEN_DELEGATE_NONE;

	/* Handle open stateid/seqid for success */
	update_stateid(file_state,
		       &res_OPEN4->OPEN4res_u.resok4.stateid,
		       data,
		       open_tag);

	/* Update delegation open stats */
	if (data->current_entry->type == REGULAR_FILE) {
		if (data->current_entry->object.file.deleg_heuristics.num_opens
		    == 0) {
			data->current_entry->object.file.deleg_heuristics
						.first_open = time(NULL);
		}
		data->current_entry->object.file.deleg_heuristics.num_opens++;
	}

	pthread_mutex_lock(&clientid->cid_mutex);
	/* Decide if we should delegate, then add it. */
	if (nfs_param.nfsv4_param.allow_delegations &&
	    data->current_entry->type != DIRECTORY
	    && op_ctx->fsal_export->ops->fs_supports(
						op_ctx->fsal_export,
						fso_delegations)
	    && (op_ctx->export_perms->options &
		EXPORT_OPTION_DELEGATIONS)
	    && owner->so_owner.so_nfs4_owner.so_confirmed == TRUE
	    && !clientid->cb_chan_down
	    && claim != CLAIM_DELEGATE_CUR
	    && should_we_grant_deleg(data->current_entry,
				     clientid,
				     file_state)) {
		LogDebug(COMPONENT_STATE, "Attempting to grant delegation");
		pthread_mutex_unlock(&clientid->cid_mutex);
		get_delegation(data, op, file_state, owner, clientid,
			       &res_OPEN4->OPEN4res_u.resok4);
	} else {
		pthread_mutex_unlock(&clientid->cid_mutex);
		res_OPEN4->OPEN4res_u.resok4.delegation.open_delegation4_u.
			od_whynone.ond_why = WND4_NOT_WANTED;
	}

 out:

	if (res_OPEN4->status != NFS4_OK) {
		LogDebug(COMPONENT_STATE, "failed with status %s",
			 nfsstat4_to_str(res_OPEN4->status));
	}

	/* Save the response in the open owner.
	 * entry_parent is either the parent directory or for a CLAIM_PREV is
	 * the entry itself. In either case, it's the right entry to use in
	 * saving the request results.
	 */
	if (data->minorversion == 0) {
		Copy_nfs4_state_req(owner,
				    arg_OPEN4->seqid,
				    op,
				    entry_parent,
				    resp,
				    open_tag);
	}

 out2:

	/* Update the lease before exit */
	if (data->minorversion == 0) {
		pthread_mutex_lock(&clientid->cid_mutex);
		update_lease(clientid);
		pthread_mutex_unlock(&clientid->cid_mutex);
	}

 out3:

	if (clientid != NULL)
		dec_client_id_ref(clientid);

	/* Clean up if we have an error exit */
	if ((file_state != NULL) && new_state &&
	    (res_OPEN4->status != NFS4_OK)) {
		/* Need to destroy open owner and state */
		state_del(file_state, false);
	}

	if (entry_change)
		cache_inode_put(entry_change);

	if (owner != NULL) {
		/* Need to release the open owner for this call */
		dec_state_owner_ref(owner);
	}

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
	return;
}				/* nfs4_op_open_Free */
