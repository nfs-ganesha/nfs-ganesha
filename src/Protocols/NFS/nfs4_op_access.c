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
 * @file  nfs4_op_access.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "log.h"
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"

/**
 * @brief NFS4_OP_ACCESS, checks for file's accessibility.
 *
 * This function impelments the NFS4_OP_ACCESS operation, which checks
 * for file's accessibility.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 362
 *
 */
enum nfs_req_result nfs4_op_access(struct nfs_argop4 *op, compound_data_t *data,
				   struct nfs_resop4 *resp)
{
	ACCESS4args * const arg_ACCESS4 = &op->nfs_argop4_u.opaccess;
	ACCESS4res * const res_ACCESS4 = &resp->nfs_resop4_u.opaccess;
	fsal_status_t status;
	uint32_t max_access = (ACCESS4_READ | ACCESS4_LOOKUP |
			       ACCESS4_MODIFY | ACCESS4_EXTEND |
			       ACCESS4_DELETE | ACCESS4_EXECUTE);

	/* xattrs are a v4.2+ feature */
	if (data->minorversion >= 2) {
		max_access |= ACCESS4_XAREAD |
			      ACCESS4_XAWRITE |
			      ACCESS4_XALIST;
	}

	/* initialize output */
	res_ACCESS4->ACCESS4res_u.resok4.supported = 0;
	res_ACCESS4->ACCESS4res_u.resok4.access = 0;
	resp->resop = NFS4_OP_ACCESS;
	res_ACCESS4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_ACCESS4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_ACCESS4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Check for input parameter's sanity */
	if (arg_ACCESS4->access > max_access) {
		res_ACCESS4->status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	/* Perform the 'access' call */
	status = nfs_access_op(data->current_obj, arg_ACCESS4->access,
			  &res_ACCESS4->ACCESS4res_u.resok4.access,
			  &res_ACCESS4->ACCESS4res_u.resok4.supported);

	if (status.major == ERR_FSAL_NO_ERROR
	    || status.major == ERR_FSAL_ACCESS)
		res_ACCESS4->status = NFS4_OK;
	else
		res_ACCESS4->status = nfs4_Errno_status(status);

	return nfsstat4_to_nfs_req_result(res_ACCESS4->status);
}				/* nfs4_op_access */

/**
 * @brief Free memory allocated for ACCESS result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_ACCESS operatino.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_access_Free(nfs_resop4 *resp)
{
	/* Nothing to do */
}
