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
 * @file    nfs4_op_reclaim_complete.c
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
#include "gsh_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "sal_data.h"
#include "sal_functions.h"

/**
 *
 * @brief The NFS4_OP_RECLAIM_COMPLETE4 operation.
 *
 * This function implements the NFS4_OP_RECLAIM_COMPLETE4 operation.
 *
 * @param[in]     op    Arguments for the nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Retuls for the nfs4_op
 *
 * @return per RFC5661 p. 372
 *
 * @see nfs4_Compound
 *
 */

enum nfs_req_result nfs4_op_reclaim_complete(struct nfs_argop4 *op,
					     compound_data_t *data,
					     struct nfs_resop4 *resp)
{
	RECLAIM_COMPLETE4args * const arg_RECLAIM_COMPLETE4
	    __attribute__ ((unused))
	    = &op->nfs_argop4_u.opreclaim_complete;
	RECLAIM_COMPLETE4res * const res_RECLAIM_COMPLETE4 =
	    &resp->nfs_resop4_u.opreclaim_complete;
	nfs_client_id_t	*clientid = data->session->clientid_record;

	resp->resop = NFS4_OP_RECLAIM_COMPLETE;

	res_RECLAIM_COMPLETE4->rcr_status = NFS4_OK;

	if (data->minorversion == 0) {
		res_RECLAIM_COMPLETE4->rcr_status = NFS4ERR_INVAL;
		return NFS_REQ_ERROR;
	}

	if (data->session == NULL) {
		res_RECLAIM_COMPLETE4->rcr_status = NFS4ERR_OP_NOT_IN_SESSION;
		return NFS_REQ_ERROR;
	}

	/* For now, we don't handle rca_one_fs, so we won't complain about
	 * complete already for it.
	 */
	if (clientid->cid_cb.v41.cid_reclaim_complete &&
	    !arg_RECLAIM_COMPLETE4->rca_one_fs) {
		res_RECLAIM_COMPLETE4->rcr_status = NFS4ERR_COMPLETE_ALREADY;
		return NFS_REQ_ERROR;
	}

	if (!arg_RECLAIM_COMPLETE4->rca_one_fs) {
		clientid->cid_cb.v41.cid_reclaim_complete = true;
		if (clientid->cid_allow_reclaim)
			atomic_inc_int32_t(&reclaim_completes);
	}

	return NFS_REQ_OK;
}				/* nfs41_op_reclaim_complete */

/**
 * @brief Free memory allocated for RECLAIM_COMPLETE result
 *
 * This function frees anty memory allocated for the result of the
 * NFS4_OP_RECLAIM_COMPLETE operation.
 */
void nfs4_op_reclaim_complete_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
