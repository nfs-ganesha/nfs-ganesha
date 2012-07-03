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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

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
  state_status_t            state_status;
  nfs_client_id_t         * pclientid;
  state_nfs4_owner_name_t   owner_name;
  state_owner_t           * plock_owner;
  state_owner_t           * conflict_owner = NULL;
  fsal_lock_param_t         lock_desc, conflict_desc;

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Entering NFS v4 LOCKT handler -----------------------------------------------------");

  /* Initialize to sane default */
  resp->resop = NFS4_OP_LOCKT;
  res_LOCKT4.status = NFS4_OK;

  /*
   * Do basic checks on a filehandle
   * LOCKT is done only on a file
   */
  res_LOCKT4.status = nfs4_sanity_check_FH(data, REGULAR_FILE);
  if(res_LOCKT4.status != NFS4_OK)
    return res_LOCKT4.status;

  /* This can't be done on the pseudofs */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    { 
      res_LOCKT4.status = NFS4ERR_ROFS;
      LogDebug(COMPONENT_STATE,
               "NFS4 LOCKT returning NFS4ERR_ROFS");
      return res_LOCKT4.status;
    }

  if (nfs_export_check_security(data->reqp, data->pexport) == FALSE)
    {
      res_LOCKT4.status = NFS4ERR_PERM;
      return res_LOCKT4.status;
    }

  /* Lock length should not be 0 */
  if(arg_LOCKT4.length == 0LL)
    {
      res_LOCKT4.status = NFS4ERR_INVAL;
      return res_LOCKT4.status;
    }

  if (nfs_in_grace())
    {
      res_LOCKT4.status = NFS4ERR_GRACE;
      return res_LOCKT4.status;
    }

  /* Convert lock parameters to internal types */
  switch(arg_LOCKT4.locktype)
    {
      case READ_LT:
      case READW_LT:
        lock_desc.lock_type = FSAL_LOCK_R;
        break;

      case WRITE_LT:
      case WRITEW_LT:
        lock_desc.lock_type = FSAL_LOCK_W;
        break;
    }

  lock_desc.lock_start = arg_LOCKT4.offset;

  if(arg_LOCKT4.length != STATE_LOCK_OFFSET_EOF)
    lock_desc.lock_length = arg_LOCKT4.length;
  else
    lock_desc.lock_length = 0;

  /* Check for range overflow.
   * Comparing beyond 2^64 is not possible int 64 bits precision,
   * but off+len > 2^64-1 is equivalent to len > 2^64-1 - off
   */
  if(lock_desc.lock_length > (STATE_LOCK_OFFSET_EOF - lock_desc.lock_start))
    {
      res_LOCKT4.status = NFS4ERR_INVAL;
      return res_LOCKT4.status;
    }

  /* Check clientid */
  if(nfs_client_id_get_confirmed(arg_LOCKT4.owner.clientid,
                                 &pclientid) != CLIENT_ID_SUCCESS)
    {
      res_LOCKT4.status = NFS4ERR_STALE_CLIENTID;
      return res_LOCKT4.status;
    }

  /* The protocol doesn't allow for EXPIRED, so return STALE_CLIENTID */
  P(pclientid->cid_mutex);

  if(!reserve_lease(pclientid))
    {
      V(pclientid->cid_mutex);

      dec_client_id_ref(pclientid);

      res_LOCKT4.status = NFS4ERR_STALE_CLIENTID;
      return res_LOCKT4.status;
    }

  V(pclientid->cid_mutex);

  /* Is this lock_owner known ? */
  convert_nfs4_lock_owner(&arg_LOCKT4.owner, &owner_name, 0LL);

  if(!nfs4_owner_Get_Pointer(&owner_name, &plock_owner))
    {
      /* This lock owner is not known yet, allocated and set up a new one */
      plock_owner = create_nfs4_owner(&owner_name,
                                      pclientid,
                                      STATE_LOCK_OWNER_NFSV4,
                                      NULL,
                                      0);

      if(plock_owner == NULL)
        {
          LogEvent(COMPONENT_NFS_V4_LOCK,
                       "LOCKT unable to create lock owner");
          res_LOCKT4.status = NFS4ERR_SERVERFAULT;
          goto out;
        }
    }
  else if(isFullDebug(COMPONENT_NFS_V4_LOCK))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      DisplayOwner(plock_owner, str);
      
      LogFullDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCKT A previously known owner is used %s",
                   str);
    }

  LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
          "LOCKT",
          data->current_entry,
          data->pcontext,
          plock_owner,
          &lock_desc);

  /* Now we have a lock owner and a stateid.
   * Go ahead and test the lock in SAL (and FSAL).
   */
  if(state_test(data->current_entry,
                data->pcontext,
                data->pexport,
                plock_owner,
                &lock_desc,
                &conflict_owner,
                &conflict_desc,
                &state_status) == STATE_LOCK_CONFLICT)
    {
      /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
      Process_nfs4_conflict(&res_LOCKT4.LOCKT4res_u.denied,
                            conflict_owner,
                            &conflict_desc);
    }

  /* Release NFS4 Open Owner reference */
  dec_state_owner_ref(plock_owner);

  /* Return result */
  res_LOCKT4.status = nfs4_Errno_state(state_status);

 out:
 
  /* Update the lease before exit */
  P(pclientid->cid_mutex);

  update_lease(pclientid);

  V(pclientid->cid_mutex);

  dec_client_id_ref(pclientid);

  return res_LOCKT4.status;

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
