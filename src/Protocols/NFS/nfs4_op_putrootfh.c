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
 * @file    nfs4_op_putrootfh.c
 * @brief   Routines used for managing the NFS4_OP_PUTROOTFH operation.
 *
 * Routines used for managing the NFS4_OP_PUTROOTFH operation.
 */
#include "config.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_convert.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"
#include "nfs_creds.h"

/**
 *
 * @brief The NFS4_OP_PUTROOTFH operation.
 *
 * This functions handles the NFS4_OP_PUTROOTFH operation in
 * NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 371
 *
 * @see CreateROOTFH4
 *
 */

int nfs4_op_putrootfh(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *resp)
{
	fsal_status_t status = {0, 0};
	struct fsal_obj_handle *file_obj;

	PUTROOTFH4res * const res_PUTROOTFH4 = &resp->nfs_resop4_u.opputrootfh;

	/* First of all, set the reply to zero to make sure
	 * it contains no parasite information
	 */
	memset(resp, 0, sizeof(struct nfs_resop4));
	resp->resop = NFS4_OP_PUTROOTFH;

	/* Clear out current entry for now */
	set_current_entry(data, NULL);

	/* Release any old export reference */
	if (op_ctx->ctx_export != NULL)
		put_gsh_export(op_ctx->ctx_export);

	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;

	/* Get the root export of the Pseudo FS */
	op_ctx->ctx_export = get_gsh_export_by_pseudo("/", true);

	if (op_ctx->ctx_export == NULL) {
		LogCrit(COMPONENT_EXPORT,
			"Could not get export for Pseudo Root");

		res_PUTROOTFH4->status = NFS4ERR_NOENT;
		return res_PUTROOTFH4->status;
	}

	op_ctx->fsal_export = op_ctx->ctx_export->fsal_export;

	/* Build credentials */
	res_PUTROOTFH4->status = nfs4_export_check_access(data->req);

	/* Test for access error (export should not be visible). */
	if (res_PUTROOTFH4->status == NFS4ERR_ACCESS) {
		/* Client has no access at all */
		LogDebug(COMPONENT_EXPORT,
			 "Client doesn't have access to Pseudo Root");
		return res_PUTROOTFH4->status;
	}

	if (res_PUTROOTFH4->status != NFS4_OK) {
		LogMajor(COMPONENT_EXPORT,
			 "Failed to get FSAL credentials Pseudo Root");
		return res_PUTROOTFH4->status;
	}

	/* Get the Pesudo Root inode of the mounted on export */
	status = nfs_export_get_root_entry(op_ctx->ctx_export, &file_obj);
	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_EXPORT,
			"Could not get root inode for Pseudo Root");

		res_PUTROOTFH4->status = nfs4_Errno_status(status);
		return res_PUTROOTFH4->status;
	}

	LogMidDebug(COMPONENT_EXPORT,
		    "Root node %p", data->current_obj);

	set_current_entry(data, file_obj);

	/* Put our ref */
	file_obj->obj_ops->put_ref(file_obj);

	/* Convert it to a file handle */
	if (!nfs4_FSALToFhandle(data->currentFH.nfs_fh4_val == NULL,
				&data->currentFH,
				data->current_obj,
				op_ctx->ctx_export)) {
		LogCrit(COMPONENT_EXPORT,
			"Could not get handle for Pseudo Root");

		res_PUTROOTFH4->status = NFS4ERR_SERVERFAULT;
		return res_PUTROOTFH4->status;
	}

	LogHandleNFS4("NFS4 PUTROOTFH CURRENT FH: ", &data->currentFH);

	res_PUTROOTFH4->status = NFS4_OK;
	return res_PUTROOTFH4->status;
}				/* nfs4_op_putrootfh */

/**
 * @brief Free memory allocated for PUTROOTFH result
 *
 * This function frees any memory allocated for the result of
 * the NFS4_OP_PUTROOTFH function.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_putrootfh_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
