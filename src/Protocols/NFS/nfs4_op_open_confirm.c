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
 * \file    nfs4_op_open_confirm.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_open_confirm.c : Routines used for managing the NFS4 COMPOUND functions.
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

/**
 * nfs4_op_open_confirm: The NFS4_OP_OPEN_CONFIRM
 * 
 * Implements the NFS4_OP_OPEN_CONFIRM
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if ok, any other value show an error.
 *
 */
#define arg_OPEN_CONFIRM4 op->nfs_argop4_u.opopen_confirm
#define res_OPEN_CONFIRM4 resp->nfs_resop4_u.opopen_confirm

int nfs4_op_open_confirm(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp)
{
  int              rc = 0;
  state_t        * pstate_found = NULL;
  state_owner_t  * popen_owner;
  const char     * tag = "OPEN_CONFIRM";

  resp->resop = NFS4_OP_OPEN_CONFIRM;
  res_OPEN_CONFIRM4.status = NFS4_OK;

  /*
   * Do basic checks on a filehandle
   * Should not operate on non-file objects
   */
  res_OPEN_CONFIRM4.status = nfs4_sanity_check_FH(data, REGULAR_FILE);
  if(res_OPEN_CONFIRM4.status != NFS4_OK)
    return res_OPEN_CONFIRM4.status;

  /* Check stateid correctness and get pointer to state */
  if((rc = nfs4_Check_Stateid(&arg_OPEN_CONFIRM4.open_stateid,
                              data->current_entry,
                              &pstate_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              arg_OPEN_CONFIRM4.seqid,
                              TRUE,
                              tag)) != NFS4_OK)
    {
      if ((rc == NFS4ERR_REPLAY) && (pstate_found != NULL) && (pstate_found->state_powner != NULL))
      {
        popen_owner = pstate_found->state_powner;
        inc_state_owner_ref(popen_owner);
        goto check_seqid;
      }
      res_OPEN_CONFIRM4.status = rc;
      return res_OPEN_CONFIRM4.status;
    }

  popen_owner = pstate_found->state_powner;
  inc_state_owner_ref(popen_owner);

  P(popen_owner->so_mutex);

check_seqid:
  /* Check seqid */
  if(!Check_nfs4_seqid(popen_owner, arg_OPEN_CONFIRM4.seqid, op, data, resp, tag))
    {
      /* Response is all setup for us and LogDebug told what was wrong */
      V(popen_owner->so_mutex);
      dec_state_owner_ref(popen_owner);
      return res_OPEN_CONFIRM4.status;
    }

  /* If opened file is already confirmed, retrun NFS4ERR_BAD_STATEID */
  if(popen_owner->so_owner.so_nfs4_owner.so_confirmed == TRUE)
    {
      V(popen_owner->so_mutex);
      res_OPEN_CONFIRM4.status = NFS4ERR_BAD_STATEID;
      return res_OPEN_CONFIRM4.status;
    }

  /* Set the state as confirmed */
  popen_owner->so_owner.so_nfs4_owner.so_confirmed = TRUE;
  V(popen_owner->so_mutex);

  /* Handle stateid/seqid for success */
  update_stateid(pstate_found,
                 &res_OPEN_CONFIRM4.OPEN_CONFIRM4res_u.resok4.open_stateid,
                 data,
                 tag);

  /* Save the response in the open owner */
  Copy_nfs4_state_req(popen_owner, arg_OPEN_CONFIRM4.seqid, op, data, resp, tag);

  dec_state_owner_ref(popen_owner);
                
  return res_OPEN_CONFIRM4.status;
}                               /* nfs4_op_open_confirm */

/**
 * nfs4_op_open_confirm_Free: frees what was allocared to handle nfs4_op_open_confirm.
 * 
 * Frees what was allocared to handle nfs4_op_open_confirm.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_open_confirm_Free(OPEN_CONFIRM4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_open_confirm_Free */

void nfs4_op_open_confirm_CopyRes(OPEN_CONFIRM4res * resp_dst, OPEN_CONFIRM4res * resp_src)
{
  /* Nothing to be done */
  return;
}
