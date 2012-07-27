/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nlm_list.h"

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

#define arg_FREE_STATEID4 op->nfs_argop4_u.opfree_stateid
#define res_FREE_STATEID4 resp->nfs_resop4_u.opfree_stateid

int nfs4_op_free_stateid(struct nfs_argop4 *op,
                         compound_data_t *data,
                         struct nfs_resop4 *resp)
{
  resp->resop = NFS4_OP_FREE_STATEID;
  res_FREE_STATEID4.fsr_status = NFS4_OK;

  if (data->minorversion == 0)
    {
      return (res_FREE_STATEID4.fsr_status = NFS4ERR_INVAL);
    }

  /**
   * @todo ACE: This function needs to be implemented.
   */

  return res_FREE_STATEID4.fsr_status;
} /* nfs41_op_free_stateid */

/**
 * @brief free memory allocated for FREE_STATEID result
 *
 * This function frees memory allocated for the NFS4_OP_FREE_STATEID
 * result.
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_free_stateid_Free(FREE_STATEID4res * resp)
{
  return;
} /* nfs41_op_free_stateid_Free */
