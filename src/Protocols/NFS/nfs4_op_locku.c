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
 * \file    nfs4_op_locku.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_locku.c : Routines used for managing the NFS4 COMPOUND functions.
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

/**
 *
 * nfs4_op_locku: The NFS4_OP_LOCKU operation.
 *
 * This function implements the NFS4_OP_LOCKU operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 * @see all the nfs4_op_<*> function
 * @see nfs4_Compound
 *
 */

#define arg_LOCKU4 op->nfs_argop4_u.oplocku
#define res_LOCKU4 resp->nfs_resop4_u.oplocku

int nfs4_op_locku(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
#ifndef _WITH_NFSV4_LOCKS
  resp->resop = NFS4_OP_LOCKU;
  res_LOCKU4.status = NFS4ERR_LOCK_NOTSUPP;
  return res_LOCKU4.status;
#else

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_locku";
  state_status_t      state_status;
  state_t           * pstate_found = NULL;
  state_owner_t     * plock_owner;
  state_lock_desc_t   lock_desc;
  unsigned int        rc = 0;
  const char        * tag = "LOCKU";

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Entering NFS v4 LOCKU handler -----------------------------------------------------");

  /* Initialize to sane default */
  resp->resop = NFS4_OP_LOCKU;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LOCKU4.status = NFS4ERR_NOFILEHANDLE;
      return res_LOCKU4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LOCKU4.status = NFS4ERR_BADHANDLE;
      return res_LOCKU4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LOCKU4.status = NFS4ERR_FHEXPIRED;
      return res_LOCKU4.status;
    }

  /* LOCKU is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
        {
        case DIRECTORY:
          res_LOCKU4.status = NFS4ERR_ISDIR;
          return res_LOCKU4.status;

        default:
          res_LOCKU4.status = NFS4ERR_INVAL;
          return res_LOCKU4.status;
        }
    }

  /* Convert lock parameters to internal types */
  switch(arg_LOCKU4.locktype)
    {
      case READ_LT:
      case READW_LT:
        lock_desc.sld_type = STATE_LOCK_R;
        break;

      case WRITE_LT:
      case WRITEW_LT:
        lock_desc.sld_type = STATE_LOCK_W;
        break;
    }

  lock_desc.sld_offset = arg_LOCKU4.offset;

  if(arg_LOCKU4.length != STATE_LOCK_OFFSET_EOF)
    lock_desc.sld_length = arg_LOCKU4.length;
  else
    lock_desc.sld_length = 0;

  /* Check stateid correctness and get pointer to state */
  if((rc = nfs4_Check_Stateid(&arg_LOCKU4.lock_stateid,
                              data->current_entry,
                              0LL,
                              &pstate_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              tag)) != NFS4_OK)
    {
      res_LOCKU4.status = rc;
      return res_LOCKU4.status;
    }

  plock_owner = pstate_found->state_powner;

  /* Check seqid (lock_seqid or open_seqid) */
  if(!Check_nfs4_seqid(plock_owner,
                       arg_LOCKU4.seqid,
                       op,
                       data,
                       resp,
                       tag))
    {
      /* Response is all setup for us and LogDebug told what was wrong */
      return res_LOCKU4.status;
    }

  /* Lock length should not be 0 */
  if(arg_LOCKU4.length == 0LL)
    {
      res_LOCKU4.status = NFS4ERR_INVAL;

      /* Save the response in the lock owner */
      Copy_nfs4_state_req(plock_owner, arg_LOCKU4.seqid, op, data, resp, tag);

      return res_LOCKU4.status;
    }

  /* Check for range overflow
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if(lock_desc.sld_length > (STATE_LOCK_OFFSET_EOF - lock_desc.sld_offset))
    {
      res_LOCKU4.status = NFS4ERR_INVAL;

      /* Save the response in the lock owner */
      Copy_nfs4_state_req(plock_owner, arg_LOCKU4.seqid, op, data, resp, tag);

      return res_LOCKU4.status;
    }

  LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
          tag,
          data->current_entry,
          data->pcontext,
          plock_owner,
          &lock_desc);

  /* Now we have a lock owner and a stateid.
   * Go ahead and push unlock into SAL (and FSAL).
   */
  if(state_unlock(data->current_entry,
                  data->pcontext,
                  plock_owner,
                  pstate_found,
                  &lock_desc,
                  data->pclient,
                  &state_status) != STATE_SUCCESS)
    {
      res_LOCKU4.status = nfs4_Errno_state(state_status);

      /* Save the response in the lock owner */
      Copy_nfs4_state_req(plock_owner, arg_LOCKU4.seqid, op, data, resp, tag);

      return res_LOCKU4.status;
    }

  /* Successful exit */
  res_LOCKU4.status = NFS4_OK;

  /* Handle stateid/seqid for success */
  update_stateid(pstate_found,
                 &res_LOCKU4.LOCKU4res_u.lock_stateid,
                 data,
                 tag);

  /* Save the response in the lock owner */
  Copy_nfs4_state_req(plock_owner, arg_LOCKU4.seqid, op, data, resp, tag);

  return res_LOCKU4.status;
#endif
}                               /* nfs4_op_locku */

/**
 * nfs4_op_locku_Free: frees what was allocared to handle nfs4_op_locku.
 *
 * Frees what was allocared to handle nfs4_op_locku.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_locku_Free(LOCKU4res * resp)
{
  /* Nothing to Mem_Free */
  return;
}                               /* nfs4_op_locku_Free */

void nfs4_op_locku_CopyRes(LOCKU4res * resp_dst, LOCKU4res * resp_src)
{
  /* Nothing to deep copy */
  return;
}
