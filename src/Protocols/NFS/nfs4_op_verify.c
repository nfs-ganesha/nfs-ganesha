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
#include "cache_inode.h"
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

int nfs4_op_verify(struct nfs_argop4 *op, compound_data_t *data,
		   struct nfs_resop4 *resp)
{
	VERIFY4args * const arg_VERIFY4 = &op->nfs_argop4_u.opverify;
	VERIFY4res * const res_VERIFY4 = &resp->nfs_resop4_u.opverify;
	fattr4 file_attr4;
	int rc = 0;

	resp->resop = NFS4_OP_VERIFY;
	res_VERIFY4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_VERIFY4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_VERIFY4->status != NFS4_OK)
		return res_VERIFY4->status;

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access
	    (&arg_VERIFY4->obj_attributes, FATTR4_ATTR_READ)) {
		res_VERIFY4->status = NFS4ERR_INVAL;
		return res_VERIFY4->status;
	}

	/* Ask only for supported attributes */
	if (!nfs4_Fattr_Supported(&arg_VERIFY4->obj_attributes)) {
		res_VERIFY4->status = NFS4ERR_ATTRNOTSUPP;
		return res_VERIFY4->status;
	}

	res_VERIFY4->status =
	    cache_entry_To_Fattr(data->current_entry,
				 &file_attr4,
				 data,
				 &data->currentFH,
				 &arg_VERIFY4->obj_attributes.attrmask);

	if (res_VERIFY4->status != NFS4_OK)
		return res_VERIFY4->status;

	rc = nfs4_Fattr_cmp(&(arg_VERIFY4->obj_attributes), &file_attr4);

	if (rc == true)
		res_VERIFY4->status = NFS4_OK;
	else if (rc == -1)
		res_VERIFY4->status = NFS4ERR_INVAL;
	else
		res_VERIFY4->status = NFS4ERR_NOT_SAME;

	nfs4_Fattr_Free(&file_attr4);
	return res_VERIFY4->status;
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
	return;
}				/* nfs4_op_verify_Free */
