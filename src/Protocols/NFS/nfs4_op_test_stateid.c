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
 * @file    nfs4_op_test_stateid.c
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
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "gsh_list.h"

/**
 * @brief The NFS4_OP_TEST_STATEID operation.
 *
 * This function implements the NFS4_OP_TEST_STATEID operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return RFC 5661, p. 375
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_test_stateid(struct nfs_argop4 *op, compound_data_t *data,
			 struct nfs_resop4 *resp)
{
	TEST_STATEID4args * const arg_TEST_STATEID4 =
	    &op->nfs_argop4_u.optest_stateid;
	TEST_STATEID4res * const res_TEST_STATEID4 =
	    &resp->nfs_resop4_u.optest_stateid;
	u_int nr_stateids, i;
	state_t *state;
	nfsstat4 ret;
	TEST_STATEID4resok *res;

	/* Lock are not supported */
	resp->resop = NFS4_OP_TEST_STATEID;
	res_TEST_STATEID4->tsr_status = NFS4_OK;

	if (data->minorversion == 0) {
		res_TEST_STATEID4->tsr_status = NFS4ERR_INVAL;
		return res_TEST_STATEID4->tsr_status;
	}

	nr_stateids = arg_TEST_STATEID4->ts_stateids.ts_stateids_len;
	res = &res_TEST_STATEID4->TEST_STATEID4res_u.tsr_resok4;
	res->tsr_status_codes.tsr_status_codes_val =
		gsh_calloc(nr_stateids, sizeof(nfsstat4));

	for (i = 0; i < nr_stateids; i++) {
		ret = nfs4_Check_Stateid(
			&arg_TEST_STATEID4->ts_stateids.ts_stateids_val[i],
			NULL, &state, data, STATEID_NO_SPECIAL,
			0, false, "TEST_STATEID");
		if (ret == NFS4_OK)
			dec_state_t_ref(state);

		res->tsr_status_codes.tsr_status_codes_val[i] = ret;
	}

	res->tsr_status_codes.tsr_status_codes_len = nr_stateids;

	return res_TEST_STATEID4->tsr_status;
}				/* nfs41_op_lock */

/**
 * @brief Free memory allocated for TEST_STATEID result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_TEST_STATEID operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_test_stateid_Free(nfs_resop4 *resp)
{
	TEST_STATEID4res * const res_TEST_STATEID4 =
	    &resp->nfs_resop4_u.optest_stateid;
	TEST_STATEID4resok *res =
	    &res_TEST_STATEID4->TEST_STATEID4res_u.tsr_resok4;

	if (res_TEST_STATEID4->tsr_status == NFS4_OK) {
		gsh_free(res->tsr_status_codes.tsr_status_codes_val);
	}
}
