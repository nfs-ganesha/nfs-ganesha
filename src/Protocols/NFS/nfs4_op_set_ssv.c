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
 * @file    nfs4_op_set_ssv.c
 * @brief   Routines for the NFS4_OP_SET_SSV operation
 *
 * Routines for the NFS4_OP_SEQUENCE operation.
 *
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"

/**
 *
 * @brief The NFS4_OP_SET_SSV operation
 *
 * This functions handles the NFS4_OP_SET_SSV operation in NFSv4. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661, pp. 374-5
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_set_ssv(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	SET_SSV4args * const arg_SET_SSV4 __attribute__ ((unused))
	    = &op->nfs_argop4_u.opset_ssv;
	SET_SSV4res * const res_SET_SSV4 = &resp->nfs_resop4_u.opset_ssv;
	resp->resop = NFS4_OP_SET_SSV;
	res_SET_SSV4->ssr_status = NFS4_OK;

	if (data->minorversion == 0) {
		res_SET_SSV4->ssr_status = NFS4ERR_INVAL;
		return res_SET_SSV4->ssr_status;
	}

	/* I know this is pretty dirty...
	 * But this is an early implementation...
	 */
	return res_SET_SSV4->ssr_status;
}				/* nfs41_op_set_ssv */

/**
 * @brief Free memory allocated for SET_SSV result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SET_SSV operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_set_ssv_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_set_ssv_Free */
