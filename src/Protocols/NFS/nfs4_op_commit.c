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
 * @file    nfs4_op_commit.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
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
#include <sys/file.h>
#include "hashtable.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "fsal_pnfs.h"

static enum nfs_req_result op_dscommit(struct nfs_argop4 *op,
				       compound_data_t *data,
				       struct nfs_resop4 *resp);

/**
 * @brief Implemtation of NFS4_OP_COMMIT
 *
 * This function implemtats NFS4_OP_COMMIT.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661 p. 362-3
 *
 */

enum nfs_req_result nfs4_op_commit(struct nfs_argop4 *op, compound_data_t *data,
				   struct nfs_resop4 *resp)
{
	COMMIT4args * const arg_COMMIT4 = &op->nfs_argop4_u.opcommit;
	COMMIT4res * const res_COMMIT4 = &resp->nfs_resop4_u.opcommit;
	fsal_status_t fsal_status = { 0, 0 };
	struct gsh_buffdesc verf_desc;

	resp->resop = NFS4_OP_COMMIT;
	res_COMMIT4->status = NFS4_OK;

	LogFullDebug(COMPONENT_NFS_V4,
		     "Commit order over offset = %" PRIu64", size = %" PRIu32,
		     arg_COMMIT4->offset,
		     arg_COMMIT4->count);

	if ((nfs4_Is_Fh_DSHandle(&data->currentFH)))
		return op_dscommit(op, data, resp);

	/*
	 * Do basic checks on a filehandle Commit is done only on a file
	 */
	res_COMMIT4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, true);

	if (res_COMMIT4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	fsal_status = fsal_commit(data->current_obj, arg_COMMIT4->offset,
				  arg_COMMIT4->count);
	if (FSAL_IS_ERROR(fsal_status)) {
		res_COMMIT4->status = nfs4_Errno_status(fsal_status);
		return NFS_REQ_ERROR;
	}

	verf_desc.addr = &res_COMMIT4->COMMIT4res_u.resok4.writeverf;
	verf_desc.len = sizeof(verifier4);

	op_ctx->fsal_export->exp_ops.get_write_verifier(op_ctx->fsal_export,
							&verf_desc);

	LogFullDebug(COMPONENT_NFS_V4,
		     "Commit verifier %d-%d",
		     ((int *)verf_desc.addr)[0], ((int *)verf_desc.addr)[1]);

	/* If you reach this point, then an error occured */
	res_COMMIT4->status = NFS4_OK;
	return NFS_REQ_OK;
}				/* nfs4_op_commit */

/**
 * @brief Free memory allocated for COMMIT result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_COMMIT operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_commit_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}

/**
 *
 * @brief Call pNFS data server commit
 *
 * This function bypasses mdcache and calls down the FSAL to
 * perform a data-server commit.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661 p. 362-3
 *
 */

static enum nfs_req_result op_dscommit(struct nfs_argop4 *op,
				       compound_data_t *data,
				       struct nfs_resop4 *resp)
{
	COMMIT4args * const arg_COMMIT4 = &op->nfs_argop4_u.opcommit;
	COMMIT4res * const res_COMMIT4 = &resp->nfs_resop4_u.opcommit;

	/* Construct the FSAL file handle */

	/* Call the commit operation */
	res_COMMIT4->status = op_ctx->ctx_pnfs_ds->s_ops.dsh_commit(
				data->current_ds,
				arg_COMMIT4->offset,
				arg_COMMIT4->count,
				&res_COMMIT4->COMMIT4res_u.resok4.writeverf);

	return nfsstat4_to_nfs_req_result(res_COMMIT4->status);
}
