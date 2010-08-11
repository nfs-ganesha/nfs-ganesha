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
 * \file    nfs41_op_lockt.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_lockt.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs41_op_lockt: The NFS4_OP_LOCKT operation. 
 *
 * This function implements the NFS4_OP_LOCKT operation.
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

#define arg_LOCKT4 op->nfs_argop4_u.oplockt
#define res_LOCKT4 resp->nfs_resop4_u.oplockt

int nfs41_op_lockt(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs41_op_lockt";

  cache_inode_status_t cache_status;
  cache_inode_state_t *pstate_found = NULL;
  uint64_t a, b, a1, b1;
  unsigned int overlap = FALSE;

  /* Lock are not supported */
  resp->resop = NFS4_OP_LOCKT;

#ifndef _WITH_NFSV4_LOCKS
  res_LOCKT4.status = NFS4ERR_LOCK_NOTSUPP;
  return res_LOCKT4.status;
#else

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LOCKT4.status = NFS4ERR_NOFILEHANDLE;
      return res_LOCKT4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LOCKT4.status = NFS4ERR_BADHANDLE;
      return res_LOCKT4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LOCKT4.status = NFS4ERR_FHEXPIRED;
      return res_LOCKT4.status;
    }

  /* Commit is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
        {
        case DIR_BEGINNING:
        case DIR_CONTINUE:
          res_LOCKT4.status = NFS4ERR_ISDIR;
          break;
        default:
          res_LOCKT4.status = NFS4ERR_INVAL;
          break;
        }
      return res_LOCKT4.status;
    }

  /* Lock length should not be 0 */
  if(arg_LOCKT4.length == 0LL)
    {
      res_LOCKT4.status = NFS4ERR_INVAL;
      return res_LOCKT4.status;
    }

  /* Check for range overflow 
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if(arg_LOCKT4.length != 0xffffffffffffffffLL)
    {
      /* Comparing beyond 2^64 is not possible int 64 bits precision, 
       * but off+len > 2^64 is equivalent to len > 2^64 - off */
      if(arg_LOCKT4.length > (0xffffffffffffffffLL - arg_LOCKT4.offset))
        {
          res_LOCKT4.status = NFS4ERR_INVAL;
          return res_LOCKT4.status;
        }
    }

  /* loop into the states related to this pentry to find the related lock */
  pstate_found = NULL;
  do
    {
      cache_inode_state_iterate(data->current_entry,
                                &pstate_found,
                                pstate_found,
                                data->pclient, data->pcontext, &cache_status);
      if((cache_status == CACHE_INODE_STATE_ERROR)
         || (cache_status == CACHE_INODE_INVALID_ARGUMENT))
        {
          res_LOCKT4.status = NFS4ERR_INVAL;
          return res_LOCKT4.status;
        }

      if(pstate_found != NULL)
        {
          if(pstate_found->state_type == CACHE_INODE_STATE_LOCK)
            {

              /* We found a lock, check is they overlap */
              a = pstate_found->state_data.lock.offset;
              b = pstate_found->state_data.lock.offset +
                  pstate_found->state_data.lock.length;
              a1 = arg_LOCKT4.offset;
              b1 = arg_LOCKT4.offset + arg_LOCKT4.length;

              /* Locks overlap is a <= a1 < b or a < b1 <= b */
              overlap = FALSE;
              if(a <= a1)
                {
                  if(a1 < b)
                    overlap = TRUE;
                }
              else
                {
                  if(a < b1)
                    {
                      if(b1 <= b)
                        overlap = TRUE;
                    }
                }

              if(overlap == TRUE)
                {
                  if((arg_LOCKT4.locktype != READ_LT)
                     || (pstate_found->state_data.lock.lock_type != READ_LT))
                    {
                      /* Overlapping lock is found, if owner is different than the calling owner, return NFS4ERR_DENIED */
                      if((arg_LOCKT4.owner.owner.owner_len ==
                          pstate_found->powner->owner_len)
                         &&
                         (!memcmp
                          (arg_LOCKT4.owner.owner.owner_val,
                           pstate_found->powner->owner_val,
                           pstate_found->powner->owner_len)))
                        {
                          /* The calling state owner is the same. There is a discussion on this case at page 161 of RFC3530. I choose to ignore this
                           * lock and continue iterating on the other states */
                        }
                      else
                        {
                          /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
                          res_LOCKT4.LOCKT4res_u.denied.offset =
                              pstate_found->state_data.lock.offset;
                          res_LOCKT4.LOCKT4res_u.denied.length =
                              pstate_found->state_data.lock.length;
                          res_LOCKT4.LOCKT4res_u.denied.locktype =
                              pstate_found->state_data.lock.lock_type;
                          res_LOCKT4.LOCKT4res_u.denied.owner.owner.owner_len =
                              pstate_found->powner->owner_len;
                          res_LOCKT4.LOCKT4res_u.denied.owner.owner.owner_val =
                              pstate_found->powner->owner_val;
                          res_LOCKT4.status = NFS4ERR_DENIED;
                          return res_LOCKT4.status;
                        }
                    }
                }
            }
        }
    }
  while(pstate_found != NULL);

  /* Succssful exit, no conflicting lock were found */
  res_LOCKT4.status = NFS4_OK;
  return res_LOCKT4.status;

#endif
}                               /* nfs41_op_lockt */

/**
 * nfs41_op_lockt_Free: frees what was allocared to handle nfs41_op_lockt.
 * 
 * Frees what was allocared to handle nfs41_op_lockt.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_lockt_Free(LOCKT4res * resp)
{
  /* Nothing to Mem_Free */
  return;
}                               /* nfs41_op_lockt_Free */
