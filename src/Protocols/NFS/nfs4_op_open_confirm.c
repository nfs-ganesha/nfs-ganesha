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
 * @file    nfs4_op_open_confirm.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 * @brief NFS4_OP_OPEN_CONFIRM
 *
 * This function implements the NFS4_OP_OPEN_CONFIRM operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @retval NFS4_OK or errors for NFSv4.0
 * @retval NFS4ERR_NOTSUPP for NFSv4.1
 *
 */
#define arg_OPEN_CONFIRM4 op->nfs_argop4_u.opopen_confirm
#define res_OPEN_CONFIRM4 resp->nfs_resop4_u.opopen_confirm

int nfs4_op_open_confirm(struct nfs_argop4 *op,
                         compound_data_t *data,
                         struct nfs_resop4 *resp)
{
  int              rc = 0;
  state_t        * state_found = NULL;
  state_owner_t  * open_owner;
  const char     * tag = "OPEN_CONFIRM";

  resp->resop = NFS4_OP_OPEN_CONFIRM;
  res_OPEN_CONFIRM4.status = NFS4_OK;

  if (data->minorversion > 0)
    {
      res_OPEN_CONFIRM4.status = NFS4ERR_NOTSUPP;
      return res_OPEN_CONFIRM4.status;
    }

  /*
   * Do basic checks on a filehandle
   * Should not operate on non-file objects
   */
  res_OPEN_CONFIRM4.status = nfs4_sanity_check_FH(data, REGULAR_FILE,
                                                  FALSE);
  if(res_OPEN_CONFIRM4.status != NFS4_OK)
    return res_OPEN_CONFIRM4.status;

  /* This can't be done on the pseudofs */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_OPEN_CONFIRM4.status = NFS4ERR_ROFS;
      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN_CONFIRM returning NFS4ERR_ROFS");
      return res_OPEN_CONFIRM4.status;
    }

  if (nfs_export_check_security(data->reqp, data->pexport) == FALSE)
    {
      res_OPEN_CONFIRM4.status = NFS4ERR_PERM;
      return res_OPEN_CONFIRM4.status;
    }

  /* Check stateid correctness and get pointer to state */
  if((rc = nfs4_Check_Stateid(&arg_OPEN_CONFIRM4.open_stateid,
                              data->current_entry,
                              &state_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              tag)) != NFS4_OK)
    {
      res_OPEN_CONFIRM4.status = rc;
      return res_OPEN_CONFIRM4.status;
    }

  open_owner = state_found->state_powner;

  P(open_owner->so_mutex);

  /* Check seqid */
  if(!Check_nfs4_seqid(open_owner, arg_OPEN_CONFIRM4.seqid, op, data,
                       resp, tag))
    {
      /* Response is all setup for us and LogDebug told what was wrong */
      V(open_owner->so_mutex);
      return res_OPEN_CONFIRM4.status;
    }

  /* If opened file is already confirmed, retrun NFS4ERR_BAD_STATEID */
  if(open_owner->so_owner.so_nfs4_owner.so_confirmed == TRUE)
    {
      V(open_owner->so_mutex);
      res_OPEN_CONFIRM4.status = NFS4ERR_BAD_STATEID;
      return res_OPEN_CONFIRM4.status;
    }

  /* Set the state as confirmed */
  open_owner->so_owner.so_nfs4_owner.so_confirmed = TRUE;
  V(open_owner->so_mutex);

  /* Handle stateid/seqid for success */
  update_stateid(state_found,
                 &res_OPEN_CONFIRM4.OPEN_CONFIRM4res_u.resok4.open_stateid,
                 data,
                 tag);

  /* Save the response in the open owner */
  Copy_nfs4_state_req(open_owner, arg_OPEN_CONFIRM4.seqid, op, data,
                      resp, tag);

  return res_OPEN_CONFIRM4.status;
} /* nfs4_op_open_confirm */

/**
 * @brief Free memory allocated for OPEN_CONFIRM result
 *
 * Thisf unction frees any memory allocated for the result of the
 * NFS4_OP_OPEN_CONFIRM operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_open_confirm_Free(OPEN_CONFIRM4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_open_confirm_Free */

void nfs4_op_open_confirm_CopyRes(OPEN_CONFIRM4res * resp_dst, OPEN_CONFIRM4res * resp_src)
{
  /* Nothing to be done */
  return;
}
