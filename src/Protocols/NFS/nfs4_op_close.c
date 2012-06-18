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
 * @file  nfs4_op_close.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <pthread.h>
#include "log.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_proto_functions.h"

/**
 *
 * Brief Implemtation of NFS4_OP_CLOSE
 *
 * This function implemtats the NFS4_OP_CLOSE
 * operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 362
 */

#define arg_CLOSE4 op->nfs_argop4_u.opclose
#define res_CLOSE4 resp->nfs_resop4_u.opclose

int nfs4_op_close(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  int                    rc = 0;
  state_t              * state_found = NULL;
  cache_inode_status_t   cache_status;
  state_status_t         state_status;
  state_owner_t        * popen_owner;
  const char           * tag = "CLOSE";
  struct glist_head    * glist, * glistn;

  LogDebug(COMPONENT_STATE,
           "Entering NFS v4 CLOSE handler -----------------------------------------------------");

  memset(&res_CLOSE4, 0, sizeof(res_CLOSE4));
  resp->resop = NFS4_OP_CLOSE;
  res_CLOSE4.status = NFS4_OK;

  /*
   * Do basic checks on a filehandle
   * Object should be a file
   */
  res_CLOSE4.status = nfs4_sanity_check_FH(data, REGULAR_FILE);
  if(res_CLOSE4.status != NFS4_OK)
    return res_CLOSE4.status;

  if(data->current_entry == NULL)
    {
      res_CLOSE4.status = NFS4ERR_SERVERFAULT;
      return res_CLOSE4.status;
    }


  /* Check stateid correctness and get pointer to state */
  if((rc = nfs4_Check_Stateid(&arg_CLOSE4.open_stateid,
                              data->current_entry,
                              &state_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              tag)) != NFS4_OK)
    {
      res_CLOSE4.status = rc;
      LogDebug(COMPONENT_STATE,
               "CLOSE failed nfs4_Check_Stateid");
      return res_CLOSE4.status;
    }

  popen_owner = state_found->state_powner;

  P(popen_owner->so_mutex);

  /* Check seqid */
  if(!Check_nfs4_seqid(popen_owner, arg_CLOSE4.seqid, op, data, resp, tag))
    {
      /* Response is all setup for us and LogDebug told what was wrong */
      V(popen_owner->so_mutex);
      return res_CLOSE4.status;
    }

  V(popen_owner->so_mutex);

  pthread_rwlock_wrlock(&data->current_entry->state_lock);
  /* Check is held locks remain */
  glist_for_each(glist, &state_found->state_data.share.share_lockstates)
    {
      state_t * plock_state = glist_entry(glist,
                                          state_t,
                                          state_data.lock.state_sharelist);

      if(!glist_empty(&plock_state->state_data.lock.state_locklist))
        {
          res_CLOSE4.status = NFS4ERR_LOCKS_HELD;

          pthread_rwlock_unlock(&data->current_entry->state_lock);
          LogDebug(COMPONENT_STATE,
                   "NFS4 Close with existing locks");

          /* Save the response in the open owner */
          Copy_nfs4_state_req(popen_owner, arg_CLOSE4.seqid, op, data, resp, tag);

          return res_CLOSE4.status;
        }
    }


  /* Handle stateid/seqid for success */
  update_stateid(state_found,
                 &res_CLOSE4.CLOSE4res_u.open_stateid,
                 data,
                 tag);

  /* Save the response in the open owner */
  Copy_nfs4_state_req(popen_owner, arg_CLOSE4.seqid, op, data, resp, tag);

  /* File is closed, release the corresponding lock states */
  glist_for_each_safe(glist, glistn, &state_found->state_data.share.share_lockstates)
    {
      state_t * plock_state = glist_entry(glist,
                                          state_t,
                                          state_data.lock.state_sharelist);

      if((state_status
          = state_del_locked(plock_state,
                             data->current_entry)) != STATE_SUCCESS)
        {
          LogDebug(COMPONENT_STATE,
                   "CLOSE failed to release lock stateid error %s",
                   state_err_str(state_status));
        }
    }

  /* File is closed, release the share state */
  if(state_found->state_type == STATE_TYPE_SHARE)
    {
      if(state_share_remove(state_found->state_pentry,
                            popen_owner,
                            state_found,
                            &state_status) != STATE_SUCCESS)
        {
          LogDebug(COMPONENT_STATE,
                   "CLOSE failed to release share state error %s",
                   state_err_str(state_status));
        }
    }

  /* File is closed, release the corresponding state */
  if((state_status
      = state_del_locked(state_found,
                         data->current_entry)) != STATE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "CLOSE failed to release stateid error %s",
               state_err_str(state_status));
    }

  /* Close the file in FSAL through the cache inode */
  if(cache_inode_close(data->current_entry,
                       0,
                       &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_CLOSE4.status = nfs4_Errno(cache_status);
      pthread_rwlock_unlock(&data->current_entry->state_lock);

      /* Save the response in the open owner */
      Copy_nfs4_state_req(popen_owner, arg_CLOSE4.seqid, op, data, resp, tag);

      return res_CLOSE4.status;
    }

  pthread_rwlock_unlock(&data->current_entry->state_lock);
  res_CLOSE4.status = NFS4_OK;

  if(isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
    {
      nfs_State_PrintAll();
      nfs4_owner_PrintAll();
    }

  return NFS4_OK;
}                               /* nfs4_op_close */

/**
 * @brief Free memory allocated for CLOSE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_CLOSE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_close_Free(CLOSE4res *resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_close_Free */

void nfs4_op_close_CopyRes(CLOSE4res *res_dst, CLOSE4res *res_src)
{
  /* Nothing to be done */
  return;
}
