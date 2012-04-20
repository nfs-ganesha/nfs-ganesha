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
 * \file    nfs4_op_open_downgrade.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_open_downgrade.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "log.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 * nfs4_op_open_downgrade: The NFS4_OP_OPEN_DOWNGRADE
 * 
 * Implements the NFS4_OP_OPEN_DOWNGRADE
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if ok, any other value show an error.
 *
 */
#define arg_OPEN_DOWNGRADE4 op->nfs_argop4_u.opopen_downgrade
#define res_OPEN_DOWNGRADE4 resp->nfs_resop4_u.opopen_downgrade

static nfsstat4 nfs4_do_open_downgrade(struct nfs_argop4  * op,
                                       compound_data_t    * data,
                                       cache_entry_t      * pentry_file,
                                       state_owner_t      * powner,
                                       state_t           ** statep,
                                       char              ** cause);

int nfs4_op_open_downgrade(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_open_downgrade";

  state_t    * pstate_found = NULL;
  int          rc;
  const char * tag = "OPEN_DOWNGRADE";

  resp->resop = NFS4_OP_OPEN_DOWNGRADE;
  res_OPEN_DOWNGRADE4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_OPEN_DOWNGRADE4.status = NFS4ERR_NOFILEHANDLE;
      return res_OPEN_DOWNGRADE4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_OPEN_DOWNGRADE4.status = NFS4ERR_BADHANDLE;
      return res_OPEN_DOWNGRADE4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_OPEN_DOWNGRADE4.status = NFS4ERR_FHEXPIRED;
      return res_OPEN_DOWNGRADE4.status;
    }

  /* Commit is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      res_OPEN_DOWNGRADE4.status = NFS4ERR_INVAL;
      return res_OPEN_DOWNGRADE4.status;
    }

  /* Check stateid correctness and get pointer to state */
  if((rc = nfs4_Check_Stateid(&arg_OPEN_DOWNGRADE4.open_stateid,
                              data->current_entry,
                              0LL,
                              &pstate_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              tag)) != NFS4_OK)
    {
      res_OPEN_DOWNGRADE4.status = rc;
      LogDebug(COMPONENT_STATE,
               "OPEN_DOWNGRADE failed nfs4_Check_Stateid");
      return res_OPEN_DOWNGRADE4.status;
    }

  /* What kind of open is it ? */
  LogFullDebug(COMPONENT_STATE,
               "OPEN_DOWNGRADE: Share Deny = %d   Share Access = %d ",
               arg_OPEN_DOWNGRADE4.share_deny,
               arg_OPEN_DOWNGRADE4.share_access);

  if(data->minorversion == 1)  /* NFSv4.1 */
    {
  if((pstate_found->state_data.share.share_access & arg_OPEN_DOWNGRADE4.share_access) !=
     (arg_OPEN_DOWNGRADE4.share_access))
    {
      /* Open share access is not a superset of downgrade share access */
      res_OPEN_DOWNGRADE4.status = NFS4ERR_INVAL;
      return res_OPEN_DOWNGRADE4.status;
    }

  if((pstate_found->state_data.share.share_deny & arg_OPEN_DOWNGRADE4.share_deny) !=
     (arg_OPEN_DOWNGRADE4.share_deny))
    {
      /* Open share deny is not a superset of downgrade share deny */
      res_OPEN_DOWNGRADE4.status = NFS4ERR_INVAL;
      return res_OPEN_DOWNGRADE4.status;
    }

  pstate_found->state_data.share.share_access = arg_OPEN_DOWNGRADE4.share_access;
  pstate_found->state_data.share.share_deny   = arg_OPEN_DOWNGRADE4.share_deny;
    }
  else  /* NFSv4.0 */
    {
      nfsstat4     status4;
      char       * cause = "";
      status4 = nfs4_do_open_downgrade(op, data, pstate_found->state_pentry,
                                       pstate_found->state_powner, &pstate_found,
                                       &cause);
      if(status4 != NFS4_OK)
        {
          LogEvent(COMPONENT_STATE, "Failed to open downgrade: %s", cause);
          res_OPEN_DOWNGRADE4.status = status4;
          return res_OPEN_DOWNGRADE4.status;
        }
    }

  /* Successful exit */
  res_OPEN_DOWNGRADE4.status = NFS4_OK;

  /* Handle stateid/seqid for success */
  update_stateid(pstate_found,
                 &res_OPEN_DOWNGRADE4.OPEN_DOWNGRADE4res_u.resok4.open_stateid,
                 data,
                 tag);

  /* Save the response in the open owner */
  Copy_nfs4_state_req(pstate_found->state_powner, arg_OPEN_DOWNGRADE4.seqid, op, data, resp, tag);
                
  return res_OPEN_DOWNGRADE4.status;
}                               /* nfs4_op_opendowngrade */

/**
 * nfs4_op_open_downgrade_Free: frees what was allocared to handle nfs4_op_open_downgrade.
 * 
 * Frees what was allocared to handle nfs4_op_open_downgrade.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_open_downgrade_Free(OPEN_DOWNGRADE4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_open_downgrade_Free */

void nfs4_op_open_downgrade_CopyRes(OPEN_DOWNGRADE4res * resp_dst, OPEN_DOWNGRADE4res * resp_src)
{
  /* Nothing to be done */
  return;
}

static nfsstat4 nfs4_do_open_downgrade(struct nfs_argop4  * op,
                                       compound_data_t    * data,
                                       cache_entry_t      * pentry_file,
                                       state_owner_t      * powner,
                                       state_t           ** statep,
                                       char              ** cause)
{
  state_data_t candidate_data;
  state_status_t state_status;
  OPEN_DOWNGRADE4args *args = &op->nfs_argop4_u.opopen_downgrade;

  candidate_data.share.share_access = args->share_access;
  candidate_data.share.share_deny = args->share_deny;

  /* Check if given share access is subset of current share access */
  if(((*statep)->state_data.share.share_access & args->share_access) !=
     (args->share_access))
    {
      /* Open share access is not a superset of downgrade share access */
      *cause = " (invalid share access for downgrade)";
      return NFS4ERR_INVAL;
    }

  /* Check if given share deny is subset of current share deny */
  if(((*statep)->state_data.share.share_deny & args->share_deny) !=
     (args->share_deny))
    {
      /* Open share deny is not a superset of downgrade share deny */
      *cause = " (invalid share deny for downgrade)";
      return NFS4ERR_INVAL;
    }

  /* Check if given share access is previously seen */
  if(state_share_check_prev(*statep, &candidate_data) != STATE_SUCCESS)
    {
      *cause = " (share access or deny never seen before)";
      return NFS4ERR_INVAL;
    }

  if(state_share_downgrade(pentry_file, data->pcontext, &candidate_data,
                           powner, *statep,
                           data->pclient, &state_status) != STATE_SUCCESS)
    {
      *cause = " (state_share_downgrade failed)";
      return NFS4ERR_SERVERFAULT;
    }

  return NFS4_OK;
}
