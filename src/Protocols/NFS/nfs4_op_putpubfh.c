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
 * @file    nfs4_op_putpubfh.c
 * @brief   Routines used for managing the NFS4_OP_PUTPUBFH operation.
 *
 * Routines used for managing the NFS4_OP_PUTPUBFH operation.
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"

/**
 * @brief The NFS4_OP_PUTFH operation
 *
 * This function sets the publicFH for the current compound requests
 * as the current FH.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 371
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_putpubfh(struct nfs_argop4 *op, compound_data_t *data,
		     struct nfs_resop4 *resp)
{
	PUTPUBFH4res * const res_PUTPUBFH4 = &resp->nfs_resop4_u.opputpubfh;

	/* PUTPUBFH really isn't used, just make PUTROOTFH do our work and
	 * call it our own...
	 */
	res_PUTPUBFH4->status = nfs4_op_putrootfh(op, data, resp);
	resp->resop = NFS4_OP_PUTPUBFH;
	return res_PUTPUBFH4->status;
}

/**
 * @brief Free memory allocated for PUTPUBFH result
 *
 * This function frees the memory allocated for the result of the
 * NFS4_OP_PUTPUBFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_putpubfh_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_putpubfh_Free */
