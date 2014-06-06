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
 * @file    nfs4_op_savefh.c
 * @brief   Routines used for managing the NFS4_OP_SAVEFH operation.
 *
 * Routines used for managing the NFS4_OP_SAVEFH operation.
 */

#include "config.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"

/**
 *
 * @brief the NFS4_OP_SAVEFH operation
 *
 * This functions handles the NFS4_OP_SAVEFH operation in NFSv4. This
 * function can be called only from nfs4_Compound.  The operation set
 * the savedFH with the value of the currentFH.
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

int nfs4_op_savefh(struct nfs_argop4 *op, compound_data_t *data,
		   struct nfs_resop4 *resp)
{
	SAVEFH4res * const res_SAVEFH = &resp->nfs_resop4_u.opsavefh;

	/* First of all, set the reply to zero to make sure it contains no
	 * parasite information
	 */
	memset(resp, 0, sizeof(struct nfs_resop4));
	resp->resop = NFS4_OP_SAVEFH;
	res_SAVEFH->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_SAVEFH->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, true);

	if (res_SAVEFH->status != NFS4_OK)
		return res_SAVEFH->status;

	/* If the savefh is not allocated, do it now */
	if (data->savedFH.nfs_fh4_val == NULL) {
		res_SAVEFH->status = nfs4_AllocateFH(&(data->savedFH));
		if (res_SAVEFH->status != NFS4_OK)
			return res_SAVEFH->status;
	}

	/* Copy the data from current FH to saved FH */
	memcpy(data->savedFH.nfs_fh4_val,
	       data->currentFH.nfs_fh4_val,
	       data->currentFH.nfs_fh4_len);

	data->savedFH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

	/* Save the current stateid */
	data->saved_stateid = data->current_stateid;
	data->saved_stateid_valid = data->current_stateid_valid;

	/* If old SavedFH had a related export, release reference. */
	if (data->saved_export != NULL)
		put_gsh_export(data->saved_export);

	/* Save the export information by taking a reference since
	 * currentFH is still active.  Assert this just to be sure...
	 */
	if (op_ctx->export != NULL) {
		data->saved_export = op_ctx->export;
		/* Get a reference to the export for the new SavedFH
		 * independent of CurrentFH if appropriate.
		 */
		get_gsh_export_ref(data->saved_export);
	} else
		data->saved_export = NULL;

	data->saved_export_perms = *op_ctx->export_perms;

	/* If saved and current entry are equal, skip the following. */
	if (data->saved_entry == data->current_entry)
		goto out;

	if (data->saved_entry) {
		cache_inode_put(data->saved_entry);
		data->saved_entry = NULL;
	}

	if (data->saved_ds) {
		ds_put(data->saved_ds);
		data->saved_ds = NULL;
	}

	data->saved_entry = data->current_entry;
	data->saved_filetype = data->current_filetype;

	/* Make SAVEFH work right for DS handle */
	if (data->current_ds != NULL) {
		data->saved_ds = data->current_ds;
		ds_get(data->saved_ds);
	}

	/* Take another reference.  As of now the filehandle is both saved
	 * and current and both must be counted.  Guard this, in case we
	 * have a pseudofs handle.
	 */
	if (data->saved_entry)
		cache_inode_lru_ref(data->saved_entry, LRU_FLAG_NONE);

 out:

	if (isFullDebug(COMPONENT_NFS_V4)) {
		char str[LEN_FH_STR];
		sprint_fhandle4(str, &data->savedFH);
		LogFullDebug(COMPONENT_NFS_V4, "SAVE FH: Saved FH %s", str);
	}

	return NFS4_OK;
}				/* nfs4_op_savefh */

/**
 * @brief Free memory allocated for SAVEFH result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SAVEFH function.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_savefh_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_savefh_Free */
