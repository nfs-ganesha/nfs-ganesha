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
 * \file    nfs4_op_lockt.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lockt.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs4_op_lockt: The NFS4_OP_LOCKT operation.
 *
 * This function implements the NFS4_OP_LOCKT operation.
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

#define arg_LOCKT4 op->nfs_argop4_u.oplockt
#define res_LOCKT4 resp->nfs_resop4_u.oplockt

int nfs4_op_lockt(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
#ifndef _WITH_NFSV4_LOCKS
  resp->resop = NFS4_OP_LOCKT;
  res_LOCKT4.status = NFS4ERR_LOCK_NOTSUPP;
  return res_LOCKT4.status;
#else

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lockt";

  state_status_t            state_status;
  nfs_client_id_t           nfs_client_id;
  state_nfs4_owner_name_t   owner_name;
  state_owner_t           * popen_owner;
  state_owner_t           * conflict_owner = NULL;
  state_lock_desc_t         lock_desc, conflict_desc;

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Entering NFS v4 LOCKT handler -----------------------------------------------------");

  /* Initialize to sane default */
  resp->resop = NFS4_OP_LOCKT;

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

  /* LOCKT is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
        {
        case DIRECTORY:
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

  /* Convert lock parameters to internal types */
  switch(arg_LOCKT4.locktype)
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

  lock_desc.sld_offset = arg_LOCKT4.offset;

  if(arg_LOCKT4.length != STATE_LOCK_OFFSET_EOF)
    lock_desc.sld_length = arg_LOCKT4.length;
  else
    lock_desc.sld_length = 0;

  /* Check for range overflow.
   * Comparing beyond 2^64 is not possible int 64 bits precision,
   * but off+len > 2^64-1 is equivalent to len > 2^64-1 - off
   */
  if(lock_desc.sld_length > (STATE_LOCK_OFFSET_EOF - lock_desc.sld_offset))
    {
      res_LOCKT4.status = NFS4ERR_INVAL;
      return res_LOCKT4.status;
    }

  /* Check clientid */
  if(nfs_client_id_get(arg_LOCKT4.owner.clientid, &nfs_client_id) != CLIENT_ID_SUCCESS)
    {
      res_LOCKT4.status = NFS4ERR_STALE_CLIENTID;
      return res_LOCKT4.status;
    }

  /* The client id should be confirmed */
  if(nfs_client_id.confirmed != CONFIRMED_CLIENT_ID)
    {
      res_LOCKT4.status = NFS4ERR_STALE_CLIENTID;
      return res_LOCKT4.status;
    }

  /* Is this lock_owner known ? */
  convert_nfs4_open_owner(&arg_LOCKT4.owner, &owner_name);

  if(!nfs4_owner_Get_Pointer(&owner_name, &popen_owner))
    {
      /* This open owner is not known yet, allocated and set up a new one */
      popen_owner = create_nfs4_owner(data->pclient,
                                      &owner_name,
                                      STATE_OPEN_OWNER_NFSV4,
                                      NULL,
                                      0);

      if(popen_owner == NULL)
        {
          LogFullDebug(COMPONENT_NFS_V4_LOCK,
                       "LOCKT unable to create open owner");
          res_LOCKT4.status = NFS4ERR_SERVERFAULT;
          return res_LOCKT4.status;
        }
    }
  else if(isFullDebug(COMPONENT_NFS_V4_LOCK))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      DisplayOwner(popen_owner, str);
      
      LogFullDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCKT A previously known owner is used %s",
                   str);
    }

  LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
          "LOCKT",
          data->current_entry,
          data->pcontext,
          popen_owner,
          &lock_desc);

  /* Now we have a lock owner and a stateid.
   * Go ahead and test the lock in SAL (and FSAL).
   */
  if(state_test(data->current_entry,
                data->pcontext,
                popen_owner,
                &lock_desc,
                &conflict_owner,
                &conflict_desc,
                data->pclient,
                &state_status) == STATE_LOCK_CONFLICT)
    {
      /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
      Process_nfs4_conflict(&res_LOCKT4.LOCKT4res_u.denied,
                            conflict_owner,
                            &conflict_desc,
                            data->pclient);
    }

  /* Release NFS4 Open Owner reference */
  dec_state_owner_ref(popen_owner, data->pclient);

  /* Return result */
  res_LOCKT4.status = nfs4_Errno_state(state_status);
  return res_LOCKT4.status;

#endif
}                               /* nfs4_op_lockt */

/**
 * nfs4_op_lockt_Free: frees what was allocared to handle nfs4_op_lockt.
 *
 * Frees what was allocared to handle nfs4_op_lockt.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_lockt_Free(LOCKT4res * resp)
{
  if(resp->status == NFS4ERR_DENIED)
    Release_nfs4_denied(&resp->LOCKT4res_u.denied);
}                               /* nfs4_op_lockt_Free */
