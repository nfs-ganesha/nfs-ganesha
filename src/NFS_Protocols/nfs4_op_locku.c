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
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"

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
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_locku";
  cache_inode_status_t cache_status;
  cache_inode_state_t *pstate_found = NULL;
  cache_inode_state_t *pstate_open = NULL;
  unsigned int rc = 0;

  /* Lock are not supported */
  resp->resop = NFS4_OP_LOCKU;

#ifndef _WITH_NFSV4_LOCKS
  res_LOCKU4.status = NFS4ERR_LOCK_NOTSUPP;
  return res_LOCKU4.status;
#else

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

  /* Commit is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
        {
        case DIR_BEGINNING:
        case DIR_CONTINUE:
          res_LOCKU4.status = NFS4ERR_ISDIR;
          break;
        default:
          res_LOCKU4.status = NFS4ERR_INVAL;
          break;
        }
    }

  /* Lock length should not be 0 */
  if(arg_LOCKU4.length == 0LL)
    {
      res_LOCKU4.status = NFS4ERR_INVAL;
      return res_LOCKU4.status;
    }

  /* Check for range overflow 
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if(arg_LOCKU4.length != 0xffffffffffffffffLL)
    {
      /* Comparing beyond 2^64 is not possible int 64 bits precision, but off+len > 2^64 is equivalent to len > 2^64 - off */
      if(arg_LOCKU4.length > (0xffffffffffffffffLL - arg_LOCKU4.offset))
        {
          res_LOCKU4.status = NFS4ERR_INVAL;
          return res_LOCKU4.status;
        }
    }

  /* Check for correctness of the provided stateid */
  if((rc =
      nfs4_Check_Stateid(&arg_LOCKU4.lock_stateid, data->current_entry, 0LL)) != NFS4_OK)
    {
      res_LOCKU4.status = rc;
      return res_LOCKU4.status;
    }

  /* Get the related state */
  if(cache_inode_get_state(arg_LOCKU4.lock_stateid.other,
                           &pstate_found,
                           data->pclient, &cache_status) != CACHE_INODE_SUCCESS)
    {
      if(cache_status == CACHE_INODE_NOT_FOUND)
        res_LOCKU4.status = NFS4ERR_LOCK_RANGE;
      else
        res_LOCKU4.status = nfs4_Errno(cache_status);

      return res_LOCKU4.status;
    }

  /* Check the seqid */
  if((arg_LOCKU4.seqid != pstate_found->powner->seqid) &&
     (arg_LOCKU4.seqid != pstate_found->powner->seqid + 1))
    {
      res_LOCKU4.status = NFS4ERR_BAD_SEQID;
      return res_LOCKU4.status;
    }

  /* Check the seqid for the lock */
  if((arg_LOCKU4.lock_stateid.seqid != pstate_found->seqid) &&
     (arg_LOCKU4.lock_stateid.seqid != pstate_found->seqid + 1))
    {
      res_LOCKU4.status = NFS4ERR_BAD_SEQID;
      return res_LOCKU4.status;
    }

  /* Increment the seqid for the open-stateid related to this lock */
  pstate_open = (cache_inode_state_t *) (pstate_found->state_data.lock.popenstate);
  if(pstate_open != NULL)
    {
      pstate_open->seqid += 1;    /** @todo BUGAZOMEU may not be useful */
      if(pstate_open->state_data.share.lockheld > 0)
        pstate_open->state_data.share.lockheld -= 1;
    }

  /* Increment the seqid */
  pstate_found->seqid += 1;
  res_LOCKU4.LOCKU4res_u.lock_stateid.seqid = pstate_found->seqid;
  memcpy(res_LOCKU4.LOCKU4res_u.lock_stateid.other, pstate_found->stateid_other, 12);

  P(pstate_found->powner->lock);
  pstate_found->powner->seqid += 1;
  V(pstate_found->powner->lock);

  /* Increment the seqid for the related open_owner */
  P(pstate_found->powner->related_owner->lock);
  pstate_found->powner->related_owner->seqid += 1;
  V(pstate_found->powner->related_owner->lock);

  /* Remove the state associated with the lock */
  if(cache_inode_del_state(pstate_found,
                           data->pclient, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_LOCKU4.status = nfs4_Errno(cache_status);
      return res_LOCKU4.status;
    }

  /* Successful exit */
  res_LOCKU4.status = NFS4_OK;
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
