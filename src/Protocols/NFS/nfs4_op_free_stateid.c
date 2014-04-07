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
 * @file    nfs4_op_free_stateid.c
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
#include "hashtable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "ganesha_list.h"

/**
 *
 * @brief The NFS4_OP_FREE_STATEID operation.
 *
 * This function implements the NFS4_OP_FREE_STATEID operation in
 * nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661 pp. 364-5
 *
 * @see nfs4_Compound
 */

int nfs4_op_free_stateid(struct nfs_argop4 *op, compound_data_t *data,
			 struct nfs_resop4 *resp)
{
	FREE_STATEID4args * const arg_FREE_STATEID4 __attribute__ ((unused))
	    = &op->nfs_argop4_u.opfree_stateid;
	FREE_STATEID4res * const res_FREE_STATEID4 =
	    &resp->nfs_resop4_u.opfree_stateid;
	state_t *state;

	resp->resop = NFS4_OP_FREE_STATEID;
	res_FREE_STATEID4->fsr_status = NFS4_OK;

	if (data->minorversion == 0)
		return res_FREE_STATEID4->fsr_status = NFS4ERR_INVAL;

	res_FREE_STATEID4->fsr_status =
	    nfs4_Check_Stateid(&arg_FREE_STATEID4->fsa_stateid,
			       NULL,
			       &state,
			       data,
			       STATEID_SPECIAL_FOR_FREE,
			       0,
			       false,
			       "FREE_STATEID");

	if (res_FREE_STATEID4->fsr_status != NFS4_OK)
		return res_FREE_STATEID4->fsr_status;

	state_del(state, false);

	return res_FREE_STATEID4->fsr_status;

}				/* nfs41_op_free_stateid */

/**
 * @brief free memory allocated for FREE_STATEID result
 *
 * This function frees memory allocated for the NFS4_OP_FREE_STATEID
 * result.
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_free_stateid_Free(nfs_resop4 *resp)
{
	return;
}				/* nfs41_op_free_stateid_Free */
