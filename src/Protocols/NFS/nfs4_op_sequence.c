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
 * @file nfs4_op_sequence.c
 * @brief Routines used for managing the NFS4_OP_SEQUENCE operation
 */

#include "config.h"
#include "sal_functions.h"
#include "nfs_rpc_callback.h"

/**
 * @brief the NFS4_OP_SEQUENCE operation
 *
 * @param[in]     op   nfs4_op arguments
 * @param[in,out] data Compound request's data
 * @param[out]    resp nfs4_op results
 *
 * @return per RFC5661, p. 374
 *
 * @see nfs4_Compound
 *
 */
int nfs4_op_sequence(struct nfs_argop4 *op,
                     compound_data_t *data,
                     struct nfs_resop4 *resp)
{
  SEQUENCE4args *const arg_SEQUENCE4 = &op->nfs_argop4_u.opsequence;
  SEQUENCE4res *const res_SEQUENCE4 = &resp->nfs_resop4_u.opsequence;

  nfs41_session_t *session;

  resp->resop = NFS4_OP_SEQUENCE;
  res_SEQUENCE4->sr_status = NFS4_OK;
  if (data->minorversion == 0)
    {
      res_SEQUENCE4->sr_status = NFS4ERR_INVAL;
      return res_SEQUENCE4->sr_status;
    }

  /* OP_SEQUENCE is always the first operation of the request */
  if(data->oppos != 0)
    {
      res_SEQUENCE4->sr_status = NFS4ERR_SEQUENCE_POS;
      return res_SEQUENCE4->sr_status;
    }

  if(!nfs41_Session_Get_Pointer(arg_SEQUENCE4->sa_sessionid, &session))
    {
      if (nfs_in_grace())
      {
        memcpy(res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sessionid,
               arg_SEQUENCE4->sa_sessionid, NFS4_SESSIONID_SIZE);
        res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sequenceid =
                                               arg_SEQUENCE4->sa_sequenceid;
        res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_slotid =
                                               arg_SEQUENCE4->sa_slotid;
        res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_highest_slotid =
                                               NFS41_NB_SLOTS - 1;
        res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_target_highest_slotid =
                                               arg_SEQUENCE4->sa_slotid;
        res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags =
                                             SEQ4_STATUS_RESTART_RECLAIM_NEEDED;
        LogDebug(COMPONENT_SESSIONS,
                 "SEQUENCE returning status %d flags 0x%X",
                  res_SEQUENCE4->sr_status,
                  res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags);
      }
      else
        res_SEQUENCE4->sr_status = NFS4ERR_BADSESSION;

      return res_SEQUENCE4->sr_status;
    }

  /* session->refcount +1 */

  /* Check if lease is expired and reserve it */
  P(session->clientid_record->cid_mutex);

  if(!reserve_lease(session->clientid_record))
    {
      V(session->clientid_record->cid_mutex);

      if(isDebug(COMPONENT_SESSIONS))
        LogDebug(COMPONENT_SESSIONS,
                 "SEQUENCE returning NFS4ERR_EXPIRED");
      else
        LogDebug(COMPONENT_CLIENTID,
                 "SEQUENCE returning NFS4ERR_EXPIRED");

      dec_session_ref(session);
      res_SEQUENCE4->sr_status = NFS4ERR_EXPIRED;
      return res_SEQUENCE4->sr_status;
    }

  data->preserved_clientid = session->clientid_record;

  V(session->clientid_record->cid_mutex);

  /* Check is slot is compliant with ca_maxrequests */
  if(arg_SEQUENCE4->sa_slotid >= session->fore_channel_attrs.ca_maxrequests)
    {
      dec_session_ref(session);
      res_SEQUENCE4->sr_status = NFS4ERR_BADSLOT;
      return res_SEQUENCE4->sr_status;
    }

  /* By default, no DRC replay */
  data->use_drc = false;

  P(session->slots[arg_SEQUENCE4->sa_slotid].lock);
  if(session->slots[arg_SEQUENCE4->sa_slotid].sequence + 1 !=arg_SEQUENCE4->sa_sequenceid)
    {
      if(session->slots[arg_SEQUENCE4->sa_slotid].sequence == arg_SEQUENCE4->sa_sequenceid)
        {
          if(session->slots[arg_SEQUENCE4->sa_slotid].cache_used)
            {
              /* Replay operation through the DRC */
              data->use_drc = true;
              data->pcached_res = &session->slots[arg_SEQUENCE4->sa_slotid].cached_result;

              LogFullDebug(COMPONENT_SESSIONS,
                           "Use sesson slot %"PRIu32"=%p for DRC",
                           arg_SEQUENCE4->sa_slotid, data->pcached_res);

              dec_session_ref(session);
              res_SEQUENCE4->sr_status = NFS4_OK;
              return res_SEQUENCE4->sr_status;
            }
          else
            {
              /* Illegal replay */
              dec_session_ref(session);
              res_SEQUENCE4->sr_status = NFS4ERR_RETRY_UNCACHED_REP;
              return res_SEQUENCE4->sr_status;
            }
        }
      V(session->slots[arg_SEQUENCE4->sa_slotid].lock);
      dec_session_ref(session);
      res_SEQUENCE4->sr_status = NFS4ERR_SEQ_MISORDERED;
      return res_SEQUENCE4->sr_status;
    }

  /* Keep memory of the session in the COMPOUND's data */
  data->psession = session;

  /* Record the sequenceid and slotid in the COMPOUND's data*/
  data->sequence = arg_SEQUENCE4->sa_sequenceid;
  data->slot = arg_SEQUENCE4->sa_slotid;

  /* Update the sequence id within the slot */
  session->slots[arg_SEQUENCE4->sa_slotid].sequence += 1;

  memcpy(res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sessionid,
         arg_SEQUENCE4->sa_sessionid, NFS4_SESSIONID_SIZE);
  res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_sequenceid =
      session->slots[arg_SEQUENCE4->sa_slotid].sequence;
  res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_slotid = arg_SEQUENCE4->sa_slotid;
  res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_highest_slotid
       = NFS41_NB_SLOTS - 1;
  res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_target_highest_slotid
       = arg_SEQUENCE4->sa_slotid; /* Maybe not the best choice */

  res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags
       = 0;

  if (nfs_rpc_get_chan(session->clientid_record, 0) == NULL)
    {
      res_SEQUENCE4->SEQUENCE4res_u.sr_resok4.sr_status_flags
	|= SEQ4_STATUS_CB_PATH_DOWN;
    }

  if(arg_SEQUENCE4->sa_cachethis)
    {
      data->pcached_res = &session->slots[arg_SEQUENCE4->sa_slotid].cached_result;
      session->slots[arg_SEQUENCE4->sa_slotid].cache_used = true;

      LogFullDebug(COMPONENT_SESSIONS,
                   "Use sesson slot %"PRIu32"=%p for DRC",
                   arg_SEQUENCE4->sa_slotid, data->pcached_res);
    }
  else
    {
      data->pcached_res = NULL;
      session->slots[arg_SEQUENCE4->sa_slotid].cache_used = false;

      LogFullDebug(COMPONENT_SESSIONS,
                   "Don't use sesson slot %"PRIu32"=NULL for DRC",
                   arg_SEQUENCE4->sa_slotid);
    }
  V(session->slots[arg_SEQUENCE4->sa_slotid].lock);

  /* If we were successful, stash the clientid in the request
     context. */

  data->req_ctx->clientid = &data->psession->clientid;

  res_SEQUENCE4->sr_status = NFS4_OK;
  return res_SEQUENCE4->sr_status;
} /* nfs41_op_sequence */

/**
 * @brief Free memory allocated for SEQUENCE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SEQUENCE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_sequence_Free(nfs_resop4 *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_sequence_Free */
