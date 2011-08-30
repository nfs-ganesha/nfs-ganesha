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
 * \file    nfs41_op_close.c
 * \author  $Author: deniel $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_close.c : Routines used for managing the NFS4 COMPOUND functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <pthread.h>
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"

/**
 *
 * nfs41_op_close: Implemtation of NFS4_OP_CLOSE
 * 
 * Implemtation of NFS4_OP_CLOSE. Implementation is partial for now, so it always returns NFS4_OK.  
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

#define arg_CLOSE4 op->nfs_argop4_u.opclose
#define res_CLOSE4 resp->nfs_resop4_u.opclose

int nfs41_op_close(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_close";
  state_t              * pstate_found = NULL;
  cache_inode_status_t   cache_status;
  state_status_t         state_status;
  int                    rc;

  memset(&res_CLOSE4, 0, sizeof(res_CLOSE4));
  resp->resop = NFS4_OP_CLOSE;

  /* If the filehandle is Empty */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_CLOSE4.status = NFS4ERR_NOFILEHANDLE;
      return res_CLOSE4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_CLOSE4.status = NFS4ERR_BADHANDLE;
      return res_CLOSE4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_CLOSE4.status = NFS4ERR_FHEXPIRED;
      return res_CLOSE4.status;
    }

  if(data->current_entry == NULL)
    {
      res_CLOSE4.status = NFS4ERR_SERVERFAULT;
      return res_CLOSE4.status;
    }

  /* Should not operate on directories */
  if(data->current_entry->internal_md.type == DIR_BEGINNING ||
     data->current_entry->internal_md.type == DIR_CONTINUE)
    {
      res_CLOSE4.status = NFS4ERR_ISDIR;
      return res_CLOSE4.status;
    }

  /* Object should be a file */
  if(data->current_entry->internal_md.type != REGULAR_FILE)
    {
      res_CLOSE4.status = NFS4ERR_INVAL;
      return res_CLOSE4.status;
    }

  /* Check stateid correctness and get pointer to state */
  if((rc = nfs4_Check_Stateid(&arg_CLOSE4.open_stateid,
                              data->current_entry,
                              0LL,
                              &pstate_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              "CLOSE")) != NFS4_OK)
    {
      res_CLOSE4.status = rc;
      LogDebug(COMPONENT_STATE,
               "CLOSE failed nfs4_Check_Stateid");
      return res_CLOSE4.status;
    }

#ifdef _TOTO
  /* Check is held locks remain */
  if(pstate_found->state_data.share.lockheld > 0)
    {
      res_CLOSE4.status = NFS4ERR_LOCKS_HELD;
      return res_CLOSE4.status;
    }
#endif

  /* Update the seqid for the open_owner */
  P(pstate_found->state_powner->so_mutex);
  pstate_found->state_powner->so_owner.so_nfs4_owner.so_seqid += 1;
  V(pstate_found->state_powner->so_mutex);

  /* Prepare the result */
  res_CLOSE4.CLOSE4res_u.open_stateid.seqid = pstate_found->state_seqid + 1;

  /* Close the file in FSAL through the cache inode */
  P_w(&data->current_entry->lock);
  if(cache_inode_close(data->current_entry,
                       data->pclient,
                       &cache_status) != CACHE_INODE_SUCCESS)
    {
      V_w(&data->current_entry->lock);

      res_CLOSE4.status = nfs4_Errno(cache_status);
      return res_CLOSE4.status;
    }
  V_w(&data->current_entry->lock);

  /* File is closed, release the corresponding state */
  if(state_del_by_key(arg_CLOSE4.open_stateid.other,
                      data->pclient,
                      &state_status) != STATE_SUCCESS)
    {
      res_CLOSE4.status = nfs4_Errno_state(state_status);
      return res_CLOSE4.status;
    }

  memcpy(res_CLOSE4.CLOSE4res_u.open_stateid.other, arg_CLOSE4.open_stateid.other, OTHERSIZE);;

  res_CLOSE4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs41_op_close */

/**
 * nfs41_op_close_Free: frees what was allocared to handle nfs4_op_close.
 * 
 * Frees what was allocared to handle nfs4_op_close.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_close_Free(CLOSE4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs41_op_close_Free */

void nfs4_op_close_CopyRes(CLOSE4res * resp_dst, CLOSE4res * resp_src)
{
  /* Nothing to be done */
  return;
}
