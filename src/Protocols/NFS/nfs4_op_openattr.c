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
 * @file    nfs4_op_openattr.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 */

#include "config.h"
#include "hashtable.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"

/**
 *
 * @brief NFS4_OP_OPENATTR
 *
 * This function implements the NFS4_OP_OPENATTRR operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 370-1
 *
 */
enum nfs_req_result nfs4_op_openattr(struct nfs_argop4 *op,
				     compound_data_t *data,
				     struct nfs_resop4 *resp)
{
	OPENATTR4args * const arg_OPENATTR4 __attribute__ ((unused))
	    = &op->nfs_argop4_u.opopenattr;
	OPENATTR4res * const res_OPENATTR4 = &resp->nfs_resop4_u.opopenattr;

	resp->resop = NFS4_OP_OPENATTR;
	res_OPENATTR4->status = NFS4ERR_NOTSUPP;

	return NFS_REQ_ERROR;
}				/* nfs4_op_openattr */

/**
 * @brief Free memory allocated for OPENATTR result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_OPENATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_openattr_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
