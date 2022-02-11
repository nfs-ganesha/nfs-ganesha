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
 * @file    nfs4_op_delegpurge.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_exports.h"
#include "nfs_proto_functions.h"

/**
 * @brief NFS4_OP_DELEGPURGE
 *
 * This function implements the NFS4_OP_DELEGPURGE operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC 5661, pp. 363-4
 *
 */
enum nfs_req_result nfs4_op_delegpurge(struct nfs_argop4 *op,
				       compound_data_t *data,
				       struct nfs_resop4 *resp)
{
	/* Unused for now, but when we actually implement this function it
	   won't be, so remove the attribute. */
	DELEGPURGE4args * const arg_DELEGPURGE4 __attribute__ ((unused))
	    = &op->nfs_argop4_u.opdelegpurge;
	DELEGPURGE4res * const res_DELEGPURGE4 =
	    &resp->nfs_resop4_u.opdelegpurge;

	/* Lock are not supported */
	resp->resop = NFS4_OP_DELEGPURGE;
	res_DELEGPURGE4->status = NFS4ERR_NOTSUPP;

	return NFS_REQ_ERROR;
}				/* nfs4_op_delegpurge */

/**
 * @brief Free memory allocated for DELEGPURGE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_DELEGPURGE operation.
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_delegpurge_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
