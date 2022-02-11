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
 * ---------------------------------------*/
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
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"

/**
 * @brief The NFS4_OP_GETFH operation
 *
 * Gets the currentFH for the current compound requests.  This
 * operation returns the current FH in the reply structure.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 366
 *
 * @see nfs4_Compound
 */

enum nfs_req_result nfs4_op_getfh(struct nfs_argop4 *op, compound_data_t *data,
				  struct nfs_resop4 *resp)
{
	GETFH4res * const res_GETFH = &resp->nfs_resop4_u.opgetfh;
	struct fsal_attrlist attrs;
	bool result;

	resp->resop = NFS4_OP_GETFH;
	res_GETFH->status = NFS4_OK;

	LogHandleNFS4("NFS4 GETFH BEFORE: ", &data->currentFH);

	/* Do basic checks on a filehandle */
	res_GETFH->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, true);

	if (res_GETFH->status != NFS4_OK)
		goto out;

	/* Fill in and check response size and make sure it fits. */
	data->op_resp_size = sizeof(nfsstat4) + sizeof(uint32_t) +
		((data->currentFH.nfs_fh4_len + sizeof(uint32_t) - 1) &
		~(sizeof(uint32_t) - 1));

	res_GETFH->status = check_resp_room(data, data->op_resp_size);

	if (res_GETFH->status != NFS4_OK)
		goto out;

	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export));

	/* Do not require ACLs and FS_LOCATIONS */
	attrs.request_mask &= ~(ATTR_ACL | ATTR4_FS_LOCATIONS);

	result = data->current_obj->obj_ops->is_referral(data->current_obj,
				&attrs, true);

	fsal_release_attrs(&attrs);

	if (result) {
		res_GETFH->status = NFS4ERR_MOVED;
		goto out;
	}

	/* Copy the filehandle to the reply structure */
	nfs4_AllocateFH(&res_GETFH->GETFH4res_u.resok4.object);

	/* Put the data in place */
	res_GETFH->GETFH4res_u.resok4.object.nfs_fh4_len =
	    data->currentFH.nfs_fh4_len;

	memcpy(res_GETFH->GETFH4res_u.resok4.object.nfs_fh4_val,
	       data->currentFH.nfs_fh4_val,
	       data->currentFH.nfs_fh4_len);

	LogHandleNFS4("NFS4 GETFH AFTER: ",
		      &res_GETFH->GETFH4res_u.resok4.object);

out:

	if (res_GETFH->status != NFS4_OK) {
		/* Indicate the failed response size. */
		data->op_resp_size = sizeof(nfsstat4);
	}

	return nfsstat4_to_nfs_req_result(res_GETFH->status);
}				/* nfs4_op_getfh */

/**
 * @brief Free memory allocated for GETFH result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_GETFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_getfh_Free(nfs_resop4 *res)
{
	GETFH4res *resp = &res->nfs_resop4_u.opgetfh;

	if (resp->status == NFS4_OK)
		gsh_free(resp->GETFH4res_u.resok4.object.nfs_fh4_val);
}				/* nfs4_op_getfh_Free */
