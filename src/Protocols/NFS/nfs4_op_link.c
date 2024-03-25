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
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs4_op_link.c
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
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
#include "fsal.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"

/**
 * @brief The NFS4_OP_LINK operation.
 *
 * This functions handles the NFS4_OP_LINK operation in NFSv4. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 367
 */

enum nfs_req_result nfs4_op_link(struct nfs_argop4 *op, compound_data_t *data,
				 struct nfs_resop4 *resp)
{
	LINK4args *const arg_LINK4 = &op->nfs_argop4_u.oplink;
	LINK4res *const res_LINK4 = &resp->nfs_resop4_u.oplink;
	struct fsal_obj_handle *dir_obj = NULL;
	struct fsal_obj_handle *file_obj = NULL;
	fsal_status_t status = { 0, 0 };
	struct fsal_attrlist destdir_pre_attrs, destdir_post_attrs;
	bool is_destdir_pre_attrs_valid, is_destdir_post_attrs_valid;

	resp->resop = NFS4_OP_LINK;
	res_LINK4->status = NFS4_OK;

	fsal_prepare_attrs(&destdir_pre_attrs, ATTR_CHANGE);
	fsal_prepare_attrs(&destdir_post_attrs, ATTR_CHANGE);

	/* Do basic checks on a filehandle */
	res_LINK4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_LINK4->status != NFS4_OK)
		goto out;

	res_LINK4->status = nfs4_sanity_check_saved_FH(data, -DIRECTORY, false);

	if (res_LINK4->status != NFS4_OK)
		goto out;

	/* Check that both handles are in the same export. */
	if (op_ctx->ctx_export != NULL && data->saved_export != NULL &&
	    op_ctx->ctx_export->export_id != data->saved_export->export_id) {
		res_LINK4->status = NFS4ERR_XDEV;
		goto out;
	}

	/*
	 * This operation creates a hard link, for the file
	 * represented by the saved FH, in directory represented by
	 * currentFH under the name arg_LINK4.target
	 */

	/* Validate and convert the UFT8 objname to a regular string */
	res_LINK4->status =
		nfs4_utf8string_scan(&arg_LINK4->newname, UTF8_SCAN_PATH_COMP);

	if (res_LINK4->status != NFS4_OK)
		goto out;

	/* get info from compound data */
	dir_obj = data->current_obj;

	res_LINK4->LINK4res_u.resok4.cinfo.before = fsal_get_changeid4(dir_obj);

	file_obj = data->saved_obj;

	/* make the link */
	status = fsal_link(file_obj, dir_obj, arg_LINK4->newname.utf8string_val,
			   &destdir_pre_attrs, &destdir_post_attrs);

	if (FSAL_IS_ERROR(status)) {
		res_LINK4->status = nfs4_Errno_status(status);
		goto out;
	}

	is_destdir_pre_attrs_valid =
		FSAL_TEST_MASK(destdir_pre_attrs.valid_mask, ATTR_CHANGE);
	if (is_destdir_pre_attrs_valid) {
		res_LINK4->LINK4res_u.resok4.cinfo.before =
			(changeid4)destdir_pre_attrs.change;
	}

	is_destdir_post_attrs_valid =
		FSAL_TEST_MASK(destdir_post_attrs.valid_mask, ATTR_CHANGE);
	if (is_destdir_post_attrs_valid) {
		res_LINK4->LINK4res_u.resok4.cinfo.after =
			(changeid4)destdir_post_attrs.change;
	} else {
		res_LINK4->LINK4res_u.resok4.cinfo.after =
			fsal_get_changeid4(dir_obj);
	}

	res_LINK4->LINK4res_u.resok4.cinfo.atomic =
		is_destdir_pre_attrs_valid && is_destdir_post_attrs_valid ?
			TRUE :
			FALSE;

	res_LINK4->status = NFS4_OK;

out:
	fsal_release_attrs(&destdir_pre_attrs);
	fsal_release_attrs(&destdir_post_attrs);

	return nfsstat4_to_nfs_req_result(res_LINK4->status);
} /* nfs4_op_link */

/**
 * @brief Free memory allocated for LINK result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LINK operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_link_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
