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
#include <stdint.h>
#include "log.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 *
 * nfs41_op_close: Implemtation of NFS4_OP_CLOSE
 *
 * Implemtation of NFS4_OP_CLOSE.
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

int nfs41_op_close(struct nfs_argop4 *op, compound_data_t * data,
                   struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_close";

  int                    rc = 0;
  state_t              * pstate_found = NULL;
  cache_inode_status_t   cache_status;
  state_status_t         state_status;
  const char           * tag = "CLOSE";
  struct glist_head    * glist, * glistn;
#ifdef _PNFS_MDS
  bool_t                 last_close = TRUE;
#endif /* _PNFS_MDS */

  LogDebug(COMPONENT_STATE,
           "Entering NFS v4.1 CLOSE handler -----------------------------------------------------");

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
                              0LL,
                              &pstate_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              tag)) != NFS4_OK)
    {
      res_CLOSE4.status = rc;
      LogDebug(COMPONENT_STATE,
               "CLOSE failed nfs4_Check_Stateid");
      return res_CLOSE4.status;
    }

  /* Check is held locks remain */
  glist_for_each(glist, &pstate_found->state_data.share.share_lockstates)
    {
      state_t * plock_state = glist_entry(glist,
                                          state_t,
                                          state_data.lock.state_sharelist);

      if(!glist_empty(&plock_state->state_data.lock.state_locklist))
        {
          res_CLOSE4.status = NFS4ERR_LOCKS_HELD;

          LogDebug(COMPONENT_STATE,
                   "NFS4 Close with existing locks");

          return res_CLOSE4.status;
        }
    }


  /* Handle stateid/seqid for success */
  update_stateid(pstate_found,
                 &res_CLOSE4.CLOSE4res_u.open_stateid,
                 data,
                 tag);

  /* File is closed, release the corresponding lock states */
  glist_for_each_safe(glist, glistn, &pstate_found->state_data.share.share_lockstates)
    {
      state_t * plock_state = glist_entry(glist,
                                          state_t,
                                          state_data.lock.state_sharelist);

      if(state_del(plock_state,
                   data->pclient,
                   &state_status) != STATE_SUCCESS)
        {
          LogDebug(COMPONENT_STATE,
                   "CLOSE failed to release lock stateid error %s",
                   state_err_str(state_status));
        }
    }

  /* File is closed, release the corresponding state */
  if(state_del(pstate_found,
               data->pclient,
               &state_status) != STATE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "CLOSE failed to release stateid error %s",
               state_err_str(state_status));
    }

#ifdef _PNFS_MDS
  /* We can't simply grab a pointer to a layout state and free it
     later, since a client could have multiple layout states (since a
     layout state covers layouts of only one layout type) each marked
     return_on_close. */

  glist_for_each(glist, &data->current_entry->object.file.state_list)
    {
      state_t *pstate = glist_entry(glist, state_t, state_list);

      if ((pstate->state_type == STATE_TYPE_SHARE) &&
          (pstate->state_powner->so_type == STATE_OPEN_OWNER_NFSV4) &&
          (pstate->state_powner->so_owner.so_nfs4_owner.so_clientid ==
           data->psession->clientid))
        {
          last_close = FALSE;
          break;
        }
    }

  if (last_close)
    {
      glist_for_each_safe(glist,
                          glistn,
                          &data->current_entry->object.file.state_list)
        {
          state_t *pstate = glist_entry(glist, state_t, state_list);
          bool_t deleted = FALSE;
          struct pnfs_segment entire = {
               .io_mode = LAYOUTIOMODE4_ANY,
               .offset = 0,
               .length = NFS4_UINT64_MAX
          };

          if ((pstate->state_type == STATE_TYPE_LAYOUT) &&
              (pstate->state_powner->so_type == STATE_CLIENTID_OWNER_NFSV4) &&
              (pstate->state_powner->so_owner.so_nfs4_owner.so_clientid ==
               data->psession->clientid) &&
              pstate->state_data.layout.state_return_on_close)
            {
              nfs4_return_one_state(data->current_entry,
                                    data->pclient,
                                    data->pcontext,
                                    TRUE,
                                    FALSE,
                                    0,
                                    pstate,
                                    entire,
                                    0,
                                    NULL,
                                    &deleted);
              if (!deleted)
                {
                  LogCrit(COMPONENT_PNFS,
                          "Layout state not destroyed on last close return.");
                }
            }
        }
    }
#endif /* _PNFS_MDS */


  /* Close the file in FSAL through the cache inode */
  if(cache_inode_close(data->current_entry,
                       data->pclient,
                       0,
                       &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_CLOSE4.status = nfs4_Errno(cache_status);
      return res_CLOSE4.status;
    }

  res_CLOSE4.status = NFS4_OK;

  if(isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
    {
      nfs_State_PrintAll();
      nfs4_owner_PrintAll();
    }

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
