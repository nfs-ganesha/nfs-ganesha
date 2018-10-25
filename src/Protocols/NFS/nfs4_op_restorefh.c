/*
 * Vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @file    nfs4_op_restorefh.c
 * @brief   The NFS4_OP_RESTOREFH operation.
 *
 * Routines used for managing the NFS4_OP_RESTOREFH operation.
 */

#include "config.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFS4_OP_RESTOREFH operation.
 *
 * This functions handles the NFS4_OP_RESTOREFH operation in
 * NFSv4. This function can be called only from nfs4_Compound.  This
 * operation replaces the current FH with the previously saved FH.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373
 *
 * @see nfs4_Compound
 *
 */

enum nfs_req_result nfs4_op_restorefh(struct nfs_argop4 *op,
				      compound_data_t *data,
				      struct nfs_resop4 *resp)
{
	RESTOREFH4res * const res_RESTOREFH = &resp->nfs_resop4_u.oprestorefh;
	/* First of all, set the reply to zero to make sure it contains no
	   parasite information */
	memset(resp, 0, sizeof(struct nfs_resop4));

	resp->resop = NFS4_OP_RESTOREFH;
	res_RESTOREFH->status = NFS4_OK;

	LogFullDebugOpaque(COMPONENT_FILEHANDLE,
			   "Saved FH %s",
			   LEN_FH_STR,
			   data->savedFH.nfs_fh4_val,
			   data->savedFH.nfs_fh4_len);

	/* If there is no savedFH, then return an error */
	if (nfs4_Is_Fh_Empty(&(data->savedFH)) == NFS4ERR_NOFILEHANDLE) {
		/* There is no current FH, return NFS4ERR_RESTOREFH
		 * (cg RFC3530, page 202)
		 */
		res_RESTOREFH->status = NFS4ERR_RESTOREFH;
		return NFS_REQ_ERROR;
	}

	/* Do basic checks on saved filehandle */
	res_RESTOREFH->status =
	    nfs4_sanity_check_saved_FH(data, NO_FILE_TYPE, true);

	if (res_RESTOREFH->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Determine if we can get a new export reference. If there is
	 * no saved export, don't get a reference to it.
	 */
	if (data->saved_export != NULL) {
		if (!export_ready(data->saved_export)) {
			/* The SavedFH export has gone bad. */
			res_RESTOREFH->status = NFS4ERR_STALE;
			return NFS_REQ_ERROR;
		}
		get_gsh_export_ref(data->saved_export);
	}

	/* Copy the data from saved FH to current FH */
	memcpy(data->currentFH.nfs_fh4_val, data->savedFH.nfs_fh4_val,
	       data->savedFH.nfs_fh4_len);

	data->currentFH.nfs_fh4_len = data->savedFH.nfs_fh4_len;

	if (op_ctx->ctx_export != NULL)
		put_gsh_export(op_ctx->ctx_export);

	/* Restore the export information */
	op_ctx->ctx_export = data->saved_export;
	if (op_ctx->ctx_export != NULL)
		op_ctx->fsal_export = op_ctx->ctx_export->fsal_export;

	*op_ctx->export_perms = data->saved_export_perms;

	/* No need to call nfs4_SetCompoundExport or nfs4_MakeCred
	 * because we are restoring saved information, and the
	 * credential checking may be skipped.
	 */

	/* Update the current entry */
	set_current_entry(data, data->saved_obj);

	/* Restore the saved stateid */
	data->current_stateid = data->saved_stateid;
	data->current_stateid_valid = data->saved_stateid_valid;

	/* Make RESTOREFH work right for DS handle */
	if (data->current_ds != NULL) {
		data->current_ds = data->saved_ds;
		data->current_filetype = data->saved_filetype;
		ds_handle_get_ref(data->current_ds);
	}

	if (isFullDebug(COMPONENT_NFS_V4)) {
		char str[LEN_FH_STR];

		sprint_fhandle4(str, &data->currentFH);
		LogFullDebug(COMPONENT_NFS_V4,
			     "RESTORE FH: Current FH %s",
			     str);
	}

	return NFS_REQ_OK;
}				/* nfs4_op_restorefh */

/**
 * @brief Free memory allocated for RESTOREFH result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_RESTOREFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_restorefh_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
