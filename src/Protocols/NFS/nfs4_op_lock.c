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
 * \file    nfs4_op_lock.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lock.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs4_op_lock: The NFS4_OP_LOCK operation.
 *
 * This function implements the NFS4_OP_LOCK operation.
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

#define arg_LOCK4 op->nfs_argop4_u.oplock
#define res_LOCK4 resp->nfs_resop4_u.oplock

int nfs4_op_lock(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lock";

#ifndef _WITH_NFSV4_LOCKS
  /* Lock are not supported */
  resp->resop = NFS4_OP_LOCK;
  res_LOCK4.status = NFS4ERR_LOCK_NOTSUPP;

  return res_LOCK4.status;
#else

  state_status_t            state_status;
  state_data_t              candidate_data;
  state_type_t              candidate_type;
  int                       rc = 0;
  seqid4                    seqid;
  state_t                 * plock_state;    /* state for the lock */
  state_t                 * pstate_open;    /* state for the open owner */
  state_owner_t           * plock_owner;
  state_owner_t           * popen_owner;
  state_owner_t           * presp_owner;    /* Owner to store response in */
  state_owner_t           * conflict_owner = NULL;
  state_nfs4_owner_name_t   owner_name;
  nfs_client_id_t           nfs_client_id;
  state_lock_desc_t         lock_desc, conflict_desc;
  state_blocking_t          blocking = STATE_NON_BLOCKING;
  const char              * tag = "LOCK";

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Entering NFS v4 LOCK handler -----------------------------------------------------");

  /* Initialize to sane starting values */
  resp->resop = NFS4_OP_LOCK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LOCK4.status = NFS4ERR_NOFILEHANDLE;
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK failed nfs4_Is_Fh_Empty");
      return res_LOCK4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LOCK4.status = NFS4ERR_BADHANDLE;
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK failed nfs4_Is_Fh_Invalid");
      return res_LOCK4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LOCK4.status = NFS4ERR_FHEXPIRED;
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK failed nfs4_Is_Fh_Expired");
      return res_LOCK4.status;
    }

  /* Lock is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
        {
        case DIRECTORY:
          res_LOCK4.status = NFS4ERR_ISDIR;
          break;
        default:
          res_LOCK4.status = NFS4ERR_INVAL;
          break;
        }
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK failed wrong file type");
      return res_LOCK4.status;
    }

  /* Convert lock parameters to internal types */
  switch(arg_LOCK4.locktype)
    {
      case READ_LT:
        lock_desc.sld_type = STATE_LOCK_R;
        blocking           = STATE_NON_BLOCKING;
        break;

      case WRITE_LT:
        lock_desc.sld_type = STATE_LOCK_W;
        blocking           = STATE_NON_BLOCKING;
        break;

      case READW_LT:
        lock_desc.sld_type = STATE_LOCK_R;
        blocking           = STATE_NFSV4_BLOCKING;
        break;

      case WRITEW_LT:
        lock_desc.sld_type = STATE_LOCK_W;
        blocking           = STATE_NFSV4_BLOCKING;
        break;
    }

  lock_desc.sld_offset = arg_LOCK4.offset;

  if(arg_LOCK4.length != STATE_LOCK_OFFSET_EOF)
    lock_desc.sld_length = arg_LOCK4.length;
  else
    lock_desc.sld_length = 0;

  if(arg_LOCK4.locker.new_lock_owner)
    {
      /* New lock owner, Find the open owner */
      tag = "LOCK (new owner)";

      /* Check stateid correctness and get pointer to state */
      if((rc = nfs4_Check_Stateid(&arg_LOCK4.locker.locker4_u.open_owner.open_stateid,
                                  data->current_entry,
                                  0LL,
                                  &pstate_open,
                                  data,
                                  STATEID_SPECIAL_FOR_LOCK,
                                  tag)) != NFS4_OK)
        {
          res_LOCK4.status = rc;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCK failed nfs4_Check_Stateid for open owner");
          return res_LOCK4.status;
        }

      popen_owner = pstate_open->state_powner;
      plock_state = NULL;
      plock_owner = NULL;
      presp_owner = popen_owner;
      seqid       = arg_LOCK4.locker.locker4_u.open_owner.open_seqid;

      LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
              "LOCK New lock owner from open owner",
              data->current_entry,
              data->pcontext,
              popen_owner,
              &lock_desc);

      /* Check is the clientid is known or not */
      if(nfs_client_id_get(arg_LOCK4.locker.locker4_u.open_owner.lock_owner.clientid,
                           &nfs_client_id) == CLIENT_ID_NOT_FOUND)
        {
          res_LOCK4.status = NFS4ERR_STALE_CLIENTID;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCK failed nfs_client_id_get");
          return res_LOCK4.status;
        }

      /* The related stateid is already stored in pstate_open */

      /* An open state has been found. Check its type */
      if(pstate_open->state_type != STATE_TYPE_SHARE)
        {
          res_LOCK4.status = NFS4ERR_BAD_STATEID;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCK failed open stateid is not a SHARE");
          return res_LOCK4.status;
        }

      /* Lock seqid (seqid wanted for new lock) should be 0 (see newpynfs test LOCK8c)  */
      if(arg_LOCK4.locker.locker4_u.open_owner.lock_seqid != 0)
        {
          res_LOCK4.status = NFS4ERR_BAD_SEQID;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCK failed new lock stateid is not 0");
          return res_LOCK4.status;
        }

      /* Is this lock_owner known ? */
      convert_nfs4_lock_owner(&arg_LOCK4.locker.locker4_u.open_owner.lock_owner,
                              &owner_name);
    }
  else
    {
      /* Existing lock owner
       * Find the lock stateid
       * From that, get the open_owner
       */
      tag = "LOCK (existing owner)";

      /* There was code here before to handle all-0 stateid, but that
       * really doesn't apply - when we handle temporary locks for
       * I/O operations (which is where we will see all-0 or all-1
       * stateid, those will not come in through nfs4_op_lock.
       */

      /* Check stateid correctness and get pointer to state */
      if((rc = nfs4_Check_Stateid(&arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid,
                                  data->current_entry,
                                  0LL,
                                  &plock_state,
                                  data,
                                  STATEID_SPECIAL_FOR_LOCK,
                                  tag)) != NFS4_OK)
        {
          res_LOCK4.status = rc;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCK failed nfs4_Check_Stateid for existing lock owner");
          return res_LOCK4.status;
        }

      /* An lock state has been found. Check its type */
      if(plock_state->state_type != STATE_TYPE_LOCK)
        {
          res_LOCK4.status = NFS4ERR_BAD_STATEID;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCK failed existing lock owner, state type is not LOCK");
          return res_LOCK4.status;
        }

      /* Get the old lockowner. We can do the following 'cast', in NFSv4 lock_owner4 and open_owner4
       * are different types but with the same definition*/
      plock_owner = plock_state->state_powner;
      popen_owner = plock_owner->so_owner.so_nfs4_owner.so_related_owner;
      pstate_open = plock_state->state_data.lock.popenstate;
      presp_owner = plock_owner;
      seqid       = arg_LOCK4.locker.locker4_u.lock_owner.lock_seqid;

      LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
              "LOCK Existing lock owner",
              data->current_entry,
              data->pcontext,
              plock_owner,
              &lock_desc);

#ifdef _CONFORM_TO_TEST_LOCK8c
      /* Check validity of the seqid */
      if(arg_LOCK4.locker.locker4_u.lock_owner.lock_seqid != 0)
        {
          res_LOCK4.status = NFS4ERR_BAD_SEQID;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "LOCK failed existing lock owner, lock seqid != 0");
          return res_LOCK4.status;
        }
#endif
    }                           /* if( arg_LOCK4.locker.new_lock_owner ) */

  /* Check seqid (lock_seqid or open_seqid) */
  if(!Check_nfs4_seqid(presp_owner, seqid, op, data, resp, tag))
    {
      /* Response is all setup for us and LogDebug told what was wrong */
      return res_LOCK4.status;
    }

  /* Lock length should not be 0 */
  if(arg_LOCK4.length == 0LL)
    {
      res_LOCK4.status = NFS4ERR_INVAL;
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK failed length == 0");

      /* Save the response in the lock or open owner */
      Copy_nfs4_state_req(presp_owner, seqid, op, data, resp, tag);

      return res_LOCK4.status;
    }

  /* Check for range overflow.
   * Comparing beyond 2^64 is not possible int 64 bits precision,
   * but off+len > 2^64-1 is equivalent to len > 2^64-1 - off
   */
  if(lock_desc.sld_length > (STATE_LOCK_OFFSET_EOF - lock_desc.sld_offset))
    {
      res_LOCK4.status = NFS4ERR_INVAL;
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK failed length overflow");

      /* Save the response in the lock or open owner */
      Copy_nfs4_state_req(presp_owner, seqid, op, data, resp, tag);

      return res_LOCK4.status;
    }

  /* check if open state has correct access for type of lock.
   * Don't need to check for conflicting states since this open
   * state assures there are no conflicting states.
   */
  if(((arg_LOCK4.locktype == WRITE_LT || arg_LOCK4.locktype == WRITEW_LT) &&
      ((pstate_open->state_data.share.share_access & OPEN4_SHARE_ACCESS_WRITE) == 0)) ||
     ((arg_LOCK4.locktype == READ_LT || arg_LOCK4.locktype == READW_LT) &&
      ((pstate_open->state_data.share.share_access & OPEN4_SHARE_ACCESS_READ) == 0)))
    {
      /* The open state doesn't allow access based on the type of lock */
      LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
              "LOCK failed, SHARE doesn't allow access",
              data->current_entry,
              data->pcontext,
              plock_owner,
              &lock_desc);

      res_LOCK4.status = NFS4ERR_OPENMODE;

      /* Save the response in the lock or open owner */
      Copy_nfs4_state_req(presp_owner, seqid, op, data, resp, tag);

      return res_LOCK4.status;
    }

  if(arg_LOCK4.locker.new_lock_owner)
    {
      /* A lock owner is always associated with a previously made open
       * which has itself a previously made stateid
       */

      /* Get reference to open owner */
      inc_state_owner_ref(popen_owner);

      if(nfs4_owner_Get_Pointer(&owner_name, &plock_owner))
        {
          /* Lock owner already existsc, check lock_seqid if it's not 0 */
          if(!Check_nfs4_seqid(plock_owner,
                               arg_LOCK4.locker.locker4_u.open_owner.lock_seqid,
                               op,
                               data,
                               resp,
                               "LOCK (new owner but owner exists)"))
            {
              /* Response is all setup for us and LogDebug told what was wrong */
              return res_LOCK4.status;
            }
        }
      else
        {
          /* This lock owner is not known yet, allocated and set up a new one */
          plock_owner = create_nfs4_owner(data->pclient,
                                          &owner_name,
                                          STATE_LOCK_OWNER_NFSV4,
                                          popen_owner,
                                          0);

          if(plock_owner == NULL)
            {
              res_LOCK4.status = NFS4ERR_RESOURCE;

              LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
                      "LOCK failed to create new lock owner",
                      data->current_entry,
                      data->pcontext,
                      popen_owner,
                      &lock_desc);

              return res_LOCK4.status;
            }
        }

      /* Prepare state management structure */
      memset(&candidate_type, 0, sizeof(candidate_type));
      candidate_type = STATE_TYPE_LOCK;
      candidate_data.lock.popenstate = pstate_open;

      /* Add the lock state to the lock table */
      if(state_add(data->current_entry,
                   candidate_type,
                   &candidate_data,
                   plock_owner,
                   data->pclient,
                   data->pcontext,
                   &plock_state, &state_status) != STATE_SUCCESS)
        {
          res_LOCK4.status = NFS4ERR_RESOURCE;

          LogLock(COMPONENT_NFS_V4_LOCK, NIV_DEBUG,
                  "LOCK failed to add new stateid",
                  data->current_entry,
                  data->pcontext,
                  plock_owner,
                  &lock_desc);

          dec_state_owner_ref(plock_owner, data->pclient);

          return res_LOCK4.status;
        }

      init_glist(&plock_state->state_data.lock.state_locklist);

      /* Add lock state to the list of lock states belonging to the open state */
      glist_add_tail(&pstate_open->state_data.share.share_lockstates,
                     &plock_state->state_data.lock.state_sharelist);
    }                           /* if( arg_LOCK4.locker.new_lock_owner ) */

  /* Now we have a lock owner and a stateid.
   * Go ahead and push lock into SAL (and FSAL).
   */
  if(state_lock(data->current_entry,
                data->pcontext,
                plock_owner,
                plock_state,
                blocking,
                NULL,     /* No block data for now */
                &lock_desc,
                &conflict_owner,
                &conflict_desc,
                data->pclient,
                &state_status) != STATE_SUCCESS)
    {
      if(state_status == STATE_LOCK_CONFLICT)
        {
          /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
          Process_nfs4_conflict(&res_LOCK4.LOCK4res_u.denied,
                                conflict_owner,
                                &conflict_desc,
                                data->pclient);
        }

      LogDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK failed with status %s",
               state_err_str(state_status));

      res_LOCK4.status = nfs4_Errno_state(state_status);

      /* Save the response in the lock or open owner */
      if(res_LOCK4.status != NFS4ERR_RESOURCE &&
         res_LOCK4.status != NFS4ERR_BAD_STATEID)
        Copy_nfs4_state_req(presp_owner, seqid, op, data, resp, tag);

      if(arg_LOCK4.locker.new_lock_owner)
        {
          /* Need to destroy lock owner and state */
          if(state_del(plock_state,
                       data->pclient,
                       &state_status) != STATE_SUCCESS)
            LogDebug(COMPONENT_NFS_V4_LOCK,
                     "state_del failed with status %s",
                     state_err_str(state_status));
        }

      return res_LOCK4.status;
    }

  res_LOCK4.status = NFS4_OK;

  /* Handle stateid/seqid for success */
  update_stateid(plock_state,
                 &res_LOCK4.LOCK4res_u.resok4.lock_stateid,
                 data,
                 tag);

  LogFullDebug(COMPONENT_NFS_V4_LOCK,
               "LOCK state_seqid = %u, plock_state = %p",
               plock_state->state_seqid,
               plock_state);

  /* Save the response in the lock or open owner */
  Copy_nfs4_state_req(presp_owner, seqid, op, data, resp, tag);

  LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
          "LOCK applied",
          data->current_entry,
          data->pcontext,
          plock_owner,
          &lock_desc);

  return res_LOCK4.status;
#endif
}                               /* nfs4_op_lock */

/**
 * nfs4_op_lock_Free: frees what was allocared to handle nfs4_op_lock.
 *
 * Frees what was allocared to handle nfs4_op_lock.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_lock_Free(LOCK4res * resp)
{
  if(resp->status == NFS4ERR_DENIED)
    Release_nfs4_denied(&resp->LOCK4res_u.denied);
}                               /* nfs4_op_lock_Free */

void nfs4_op_lock_CopyRes(LOCK4res * resp_dst, LOCK4res * resp_src)
{
  if(resp_src->status == NFS4ERR_DENIED)
    Copy_nfs4_denied(&resp_dst->LOCK4res_u.denied,
                     &resp_src->LOCK4res_u.denied);
}
