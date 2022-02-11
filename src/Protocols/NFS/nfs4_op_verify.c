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
 * \file    nfs4_op_verify.c
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"

/**
 *
 * @brief Implemtation of NFS4_OP_VERIFY
 *
 * This function implemtats the NFS4_OP_VERIFY operation.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661, p. 375
 */

enum nfs_req_result nfs4_op_verify(struct nfs_argop4 *op,
				   compound_data_t *data,
				   struct nfs_resop4 *resp)
{
	VERIFY4args * const arg_VERIFY4 = &op->nfs_argop4_u.opverify;
	VERIFY4res * const res_VERIFY4 = &resp->nfs_resop4_u.opverify;
	fattr4 file_attr4;
	int rc = 0;
	struct fsal_attrlist attrs;

	resp->resop = NFS4_OP_VERIFY;
	res_VERIFY4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_VERIFY4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_VERIFY4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access
	    (&arg_VERIFY4->obj_attributes, FATTR4_ATTR_READ)) {
		res_VERIFY4->status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	/* Ask only for supported attributes */
	if (!nfs4_Fattr_Supported(&arg_VERIFY4->obj_attributes)) {
		res_VERIFY4->status = NFS4ERR_ATTRNOTSUPP;
		return NFS_REQ_ERROR;
	}

	fsal_prepare_attrs(&attrs, 0);

	res_VERIFY4->status =
		bitmap4_to_attrmask_t(&arg_VERIFY4->obj_attributes.attrmask,
				      &attrs.request_mask);

	if (res_VERIFY4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	res_VERIFY4->status =
		file_To_Fattr(data, attrs.request_mask, &attrs, &file_attr4,
			      &arg_VERIFY4->obj_attributes.attrmask);

	if (res_VERIFY4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	rc = nfs4_Fattr_cmp(&(arg_VERIFY4->obj_attributes), &file_attr4);

	if (rc == 1)
		res_VERIFY4->status = NFS4_OK;
	else if (rc == -1)
		res_VERIFY4->status = NFS4ERR_INVAL;
	else
		res_VERIFY4->status = NFS4ERR_NOT_SAME;

	nfs4_Fattr_Free(&file_attr4);
	return nfsstat4_to_nfs_req_result(res_VERIFY4->status);
}				/* nfs4_op_verify */

/**
 * @brief Frees memory allocated for VERIFY result.
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_VERIFY operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_verify_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
