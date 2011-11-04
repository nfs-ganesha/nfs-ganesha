/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs41_op_free_stateid.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_free_stateid.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nlm_list.h"

/**
 *
 * nfs41_op_free_stateid: The NFS4_OP_FREE_STATEID operation.
 *
 * This function implements the NFS4_OP_FREE_STATEID operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 * @see all the nfs41_op_<*> function
 * @see nfs4_Compound
 *
 */

#define arg_FREE_STATEID4 op->nfs_argop4_u.opfree_stateid
#define res_FREE_STATEID4 resp->nfs_resop4_u.opfree_stateid

int nfs41_op_free_stateid(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs41_op_free_stateid";

  /* Lock are not supported */
  resp->resop = NFS4_OP_FREE_STATEID;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_FREE_STATEID4.fsr_status = NFS4ERR_NOFILEHANDLE;
      LogDebug(COMPONENT_NFS_V4,
               "LOCK failed nfs4_Is_Fh_Empty");
      return res_FREE_STATEID4.fsr_status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_FREE_STATEID4.fsr_status = NFS4ERR_BADHANDLE;
      LogDebug(COMPONENT_NFS_V4,
               "LOCK failed nfs4_Is_Fh_Invalid");
      return res_FREE_STATEID4.fsr_status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_FREE_STATEID4.fsr_status = NFS4ERR_FHEXPIRED;
      LogDebug(COMPONENT_NFS_V4,
               "LOCK failed nfs4_Is_Fh_Expired");
      return res_FREE_STATEID4.fsr_status;
    }

  res_FREE_STATEID4.fsr_status = NFS4_OK;

  return res_FREE_STATEID4.fsr_status;
}                               /* nfs41_op_lock */

/**
 * nfs41_op_free_stateid_Free: frees what was allocared to handle nfs41_op_free_stateid.
 *
 * Frees what was allocared to handle nfs41_op_free_stateid
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs41_op_free_stateid_Free(FREE_STATEID4res * resp)
{
  return;
}                               /* nfs41_op_lock_Free */
