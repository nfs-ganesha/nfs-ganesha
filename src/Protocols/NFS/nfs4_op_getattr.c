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
 * --------------------------------------- */
/**
 * @file    nfs4_op_getattr.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"

/**
 * @brief Gets attributes for an entry in the FSAL.
 *
 * Impelments the NFS4_OP_GETATTR operation, which gets attributes for
 * an entry in the FSAL.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 365
 *
 */
int nfs4_op_getattr(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	GETATTR4args * const arg_GETATTR4 = &op->nfs_argop4_u.opgetattr;
	GETATTR4res * const res_GETATTR4 = &resp->nfs_resop4_u.opgetattr;

	/* This is a NFS4_OP_GETTAR */
	resp->resop = NFS4_OP_GETATTR;
	res_GETATTR4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_GETATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_GETATTR4->status != NFS4_OK)
		return res_GETATTR4->status;

	/* Sanity check: if no attributes are wanted, nothing is to be
	 * done.  In this case NFS4_OK is to be returned */
	if (arg_GETATTR4->attr_request.bitmap4_len == 0) {
		res_GETATTR4->status = NFS4_OK;
		return res_GETATTR4->status;
	}

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access_Bitmap(&arg_GETATTR4->attr_request,
					    FATTR4_ATTR_READ)) {
		res_GETATTR4->status = NFS4ERR_INVAL;
		return res_GETATTR4->status;
	}

	nfs4_bitmap4_Remove_Unsupported(&arg_GETATTR4->attr_request);

	res_GETATTR4->status =
		   cache_entry_To_Fattr(data->current_entry,
					&res_GETATTR4->GETATTR4res_u.resok4.
					obj_attributes,
					data,
					&data->currentFH,
					&arg_GETATTR4->attr_request);

	return res_GETATTR4->status;
}				/* nfs4_op_getattr */

/**
 * @brief Free memory allocated for GETATTR result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_GETATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_getattr_Free(nfs_resop4 *res)
{
	GETATTR4res *resp = &res->nfs_resop4_u.opgetattr;

	if (resp->status == NFS4_OK)
		nfs4_Fattr_Free(&resp->GETATTR4res_u.resok4.obj_attributes);
}				/* nfs4_op_getattr_Free */
