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
 * @file    nfs4_op_rename.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"
#include "fsal.h"

/**
 * @brief The NFS4_OP_RENAME operation
 *
 * This function implemenats the NFS4_OP_RENAME operation. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373
 */

enum nfs_req_result nfs4_op_rename(struct nfs_argop4 *op, compound_data_t *data,
				   struct nfs_resop4 *resp)
{
	RENAME4args * const arg_RENAME4 = &op->nfs_argop4_u.oprename;
	RENAME4res * const res_RENAME4 = &resp->nfs_resop4_u.oprename;
	struct fsal_obj_handle *dst_obj = NULL;
	struct fsal_obj_handle *src_obj = NULL;
	char *oldname = NULL;
	char *newname = NULL;

	resp->resop = NFS4_OP_RENAME;
	res_RENAME4->status = NFS4_OK;

	/* Read and validate oldname and newname from uft8 strings. */
	res_RENAME4->status = nfs4_utf8string2dynamic(&arg_RENAME4->oldname,
						      UTF8_SCAN_ALL,
						      &oldname);

	if (res_RENAME4->status != NFS4_OK)
		goto out;

	res_RENAME4->status = nfs4_utf8string2dynamic(&arg_RENAME4->newname,
						      UTF8_SCAN_ALL,
						      &newname);

	if (res_RENAME4->status != NFS4_OK)
		goto out;

	/* Do basic checks on a filehandle */
	res_RENAME4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_RENAME4->status != NFS4_OK)
		goto out;

	res_RENAME4->status =
	    nfs4_sanity_check_saved_FH(data, DIRECTORY, false);

	if (res_RENAME4->status != NFS4_OK)
		goto out;

	/* Check that both handles are in the same export. */
	if (op_ctx->ctx_export != NULL && data->saved_export != NULL &&
	    op_ctx->ctx_export->export_id != data->saved_export->export_id) {
		res_RENAME4->status = NFS4ERR_XDEV;
		goto out;
	}

	if (!nfs_get_grace_status(false)) {
		res_RENAME4->status = NFS4ERR_GRACE;
		goto out;
	}

	dst_obj = data->current_obj;
	src_obj = data->saved_obj;

	res_RENAME4->RENAME4res_u.resok4.source_cinfo.before =
	    fsal_get_changeid4(src_obj);
	res_RENAME4->RENAME4res_u.resok4.target_cinfo.before =
	    fsal_get_changeid4(dst_obj);

	res_RENAME4->status = nfs4_Errno_status(fsal_rename(src_obj, oldname,
							    dst_obj, newname));
	if (res_RENAME4->status == NFS4_OK) {
		/* If you reach this point, then everything was alright
		 * For the change_info4, get the 'change' attributes
		 * for both directories
		 */
		res_RENAME4->RENAME4res_u.resok4.source_cinfo.after =
		    fsal_get_changeid4(src_obj);
		res_RENAME4->RENAME4res_u.resok4.target_cinfo.after =
		    fsal_get_changeid4(dst_obj);
		res_RENAME4->RENAME4res_u.resok4.target_cinfo.atomic = FALSE;
		res_RENAME4->RENAME4res_u.resok4.source_cinfo.atomic = FALSE;
	}
	nfs_put_grace_status();
 out:
	if (oldname)
		gsh_free(oldname);

	if (newname)
		gsh_free(newname);

	return nfsstat4_to_nfs_req_result(res_RENAME4->status);
}

/**
 * @brief Free memory allocated for RENAME result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_RENAME operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_rename_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
