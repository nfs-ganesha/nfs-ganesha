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
 * \file nfs4_op_illegal.c
 * \brief Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"

/**
 * @brief Always fail
 *
 * This function is the designated ILLEGAL operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op results
 *
 * @retval NFS4ERR_OP_ILLEGAL always.
 *
 */
int nfs4_op_illegal(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	resp->resop = NFS4_OP_ILLEGAL;
	resp->nfs_resop4_u.opillegal.status = NFS4ERR_OP_ILLEGAL;

	return NFS4ERR_OP_ILLEGAL;
}				/* nfs4_op_illegal */

/**
 * @brief Free memory allocated for ILLEGAL result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_ILLEGAL operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_illegal_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_illegal_Free */

/**
 * @brief Always fail
 *
 * This function is the designated NOTSUPP operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op results
 *
 * @retval NFS4ERR_NOTSUPP always.
 *
 */
int nfs4_op_notsupp(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	resp->resop = op->argop;
	resp->nfs_resop4_u.opillegal.status = NFS4ERR_NOTSUPP;

	return NFS4ERR_NOTSUPP;
}				/* nfs4_op_notsupp */

/**
 * @brief Free memory allocated for NOTSUPP result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_NOTSUPP operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_notsupp_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_notsupp_Free */
