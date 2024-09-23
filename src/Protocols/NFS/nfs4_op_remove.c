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
 * \file    nfs4_op_remove.c
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
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

#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs4.h"
#endif

/**
 * @brief The NFS4_OP_REMOVE operation.
 *
 * This function implements the NFS4_OP_REMOVE operation in
 * NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 372-3
 */

enum nfs_req_result nfs4_op_remove(struct nfs_argop4 *op, compound_data_t *data,
				   struct nfs_resop4 *resp)
{
	REMOVE4args *const arg_REMOVE4 = &op->nfs_argop4_u.opremove;
	REMOVE4res *const res_REMOVE4 = &resp->nfs_resop4_u.opremove;
	struct fsal_obj_handle *parent_obj = NULL;
	fsal_status_t fsal_status = { 0, 0 };
	struct fsal_attrlist parent_pre_attrs, parent_post_attrs;
	bool is_parent_pre_attrs_valid, is_parent_post_attrs_valid;

	GSH_AUTO_TRACEPOINT(nfs4, op_remove_start, TRACE_INFO,
			    "REMOVE args: target[{}]={}",
			    arg_REMOVE4->target.utf8string_len,
			    TP_UTF8STR_TRUNCATED(arg_REMOVE4->target));

	resp->resop = NFS4_OP_REMOVE;

	fsal_prepare_attrs(&parent_pre_attrs, ATTR_CHANGE);
	fsal_prepare_attrs(&parent_post_attrs, ATTR_CHANGE);

	/* Do basic checks on a filehandle
	 * Delete arg_REMOVE4.target in directory pointed by currentFH
	 * Make sure the currentFH is pointed a directory
	 */
	res_REMOVE4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);
	if (res_REMOVE4->status != NFS4_OK)
		goto out;

	/* Validate and convert the UFT8 target to a regular string */
	res_REMOVE4->status =
		nfs4_utf8string_scan(&arg_REMOVE4->target, UTF8_SCAN_PATH_COMP);

	if (res_REMOVE4->status != NFS4_OK)
		goto out;

	if (!nfs_get_grace_status(false)) {
		res_REMOVE4->status = NFS4ERR_GRACE;
		goto out;
	}

	/* Get the parent obj (aka the current one in the compound data) */
	parent_obj = data->current_obj;

	/* We have to keep track of the 'change' file attribute
	 * for reply structure
	 */
	memset(&res_REMOVE4->REMOVE4res_u.resok4.cinfo.before, 0,
	       sizeof(changeid4));

	res_REMOVE4->REMOVE4res_u.resok4.cinfo.before =
		fsal_get_changeid4(parent_obj);

	fsal_status = fsal_remove(parent_obj,
				  arg_REMOVE4->target.utf8string_val,
				  &parent_pre_attrs, &parent_post_attrs);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_REMOVE4->status = nfs4_Errno_status(fsal_status);
		goto out_put_grace;
	}

	is_parent_pre_attrs_valid =
		FSAL_TEST_MASK(parent_pre_attrs.valid_mask, ATTR_CHANGE);
	if (is_parent_pre_attrs_valid) {
		res_REMOVE4->REMOVE4res_u.resok4.cinfo.before =
			(changeid4)parent_pre_attrs.change;
	}

	is_parent_post_attrs_valid =
		FSAL_TEST_MASK(parent_post_attrs.valid_mask, ATTR_CHANGE);
	if (is_parent_post_attrs_valid) {
		res_REMOVE4->REMOVE4res_u.resok4.cinfo.after =
			(changeid4)parent_post_attrs.change;
	} else {
		res_REMOVE4->REMOVE4res_u.resok4.cinfo.after =
			fsal_get_changeid4(parent_obj);
	}

	res_REMOVE4->REMOVE4res_u.resok4.cinfo.atomic =
		is_parent_pre_attrs_valid && is_parent_post_attrs_valid ? TRUE :
									  FALSE;

out_put_grace:
	fsal_release_attrs(&parent_pre_attrs);
	fsal_release_attrs(&parent_post_attrs);

	nfs_put_grace_status();
out:
	GSH_AUTO_TRACEPOINT(
		nfs4, op_remove_end, TRACE_INFO,
		"REMOVE res: status={} " TP_CINFO_FORMAT, res_REMOVE4->status,
		TP_CINFO_ARGS_EXPAND(res_REMOVE4->REMOVE4res_u.resok4.cinfo));
	return nfsstat4_to_nfs_req_result(res_REMOVE4->status);
} /* nfs4_op_remove */

/**
 * @brief Free memory allocated for REMOVE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_REMOVE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_remove_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
