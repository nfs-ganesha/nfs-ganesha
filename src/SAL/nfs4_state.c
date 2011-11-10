/* vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    nfs4_state.c
 * \author  $Author: deniel $
 * \date    $Date$
 * \version $Revision$
 * \brief   This file contains functions used in state management.
 *
 * nfs4_state.c : This file contains functions used in state management.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "sal_functions.h"
#include "stuff_alloc.h"
#include "nfs_core.h"

/**
 *
 * state_conflict : checks for a conflict between an existing state and a candidate state.
 *
 * Checks for a conflict between an existing state and a candidate state.
 *
 * @param pstate      [IN] existing state
 * @param state_type  [IN] type of candidate state
 * @param pstate_data [IN] data for the candidate state
 *
 * @return TRUE if there is a conflict, FALSE if no conflict has been found
 *
 */
int state_conflict(state_t      * pstate,
                   state_type_t   state_type,
                   state_data_t * pstate_data)
{
  if(pstate == NULL || pstate_data == NULL)
    return TRUE;

  switch (state_type)
    {
    case STATE_TYPE_NONE:
      return FALSE;               /* STATE_NONE conflicts with nobody */

    case STATE_TYPE_SHARE:
      if(pstate->state_type == STATE_TYPE_SHARE)
        {
          if((pstate->state_data.share.share_access & pstate_data->share.share_deny) ||
             (pstate->state_data.share.share_deny & pstate_data->share.share_access))
            return TRUE;
        }
      return FALSE;

    case STATE_TYPE_LOCK:
      return FALSE;              /* lock conflict is managed in the NFS request */

    case STATE_TYPE_LAYOUT:
      return FALSE;              /** @todo No conflict management on layout for now */

    case STATE_TYPE_DELEG:
      /* Not yet implemented for now, answer TRUE to avoid weird behavior */
      return TRUE;
    }

  return TRUE;
}                               /* state_conflict */

/**
 *
 * state_add: adds a new state to a file pentry
 *
 * Adds a new state to a file pentry
 *
 * @param pentry        [INOUT] cache entry to operate on
 * @param state_type    [IN]    state to be defined
 * @param pstate_data   [IN]    data related to this state
 * @param powner_input  [IN]    related open_owner
 * @param pclient       [INOUT] cache inode client to be used
 * @param pcontext      [IN]    FSAL credentials
 * @param ppstate       [OUT]   pointer to a pointer to the new state
 * @param pstatus       [OUT]   returned status
 *
 * @return the same as *pstatus
 *
 */
state_status_t state_add(cache_entry_t         * pentry,
                         state_type_t            state_type,
                         state_data_t          * pstate_data,
                         state_owner_t         * powner_input,
                         cache_inode_client_t  * pclient,
                         fsal_op_context_t     * pcontext,
                         state_t              ** ppstate,
                         state_status_t        * pstatus)
{
  state_t           * pnew_state  = NULL;
  state_t           * piter_state = NULL;
  char                debug_str[OTHERSIZE * 2 + 1];
  struct glist_head * glist;

  /* Acquire lock to enter critical section on this entry */
  P_w(&pentry->lock);

  GetFromPool(pnew_state, &pclient->pool_state_v4, state_t);

  if(pnew_state == NULL)
    {
      LogDebug(COMPONENT_STATE,
               "Can't allocate a new file state from cache pool");

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

      V_w(&pentry->lock);

      *pstatus = STATE_MALLOC_ERROR;
      return *pstatus;
    }

  memset(pnew_state, 0, sizeof(*pnew_state));

  /* Brwose the state's list */
  glist_for_each(glist, &pentry->object.file.state_list)
    {
      piter_state = glist_entry(glist, state_t, state_list);

      if(state_conflict(piter_state, state_type, pstate_data))
        {
          LogDebug(COMPONENT_STATE,
                   "new state conflicts with another state for pentry %p",
                   pentry);

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

          ReleaseToPool(pnew_state, &pclient->pool_state_v4);

          V_w(&pentry->lock);

          *pstatus = STATE_STATE_CONFLICT;
          return *pstatus;
        }
    }

  /* Add the stateid.other, this will increment pentry->object.file.state_current_counter */
  if(!nfs4_BuildStateId_Other(pentry,
                              pcontext,
                              powner_input,
                              pnew_state->stateid_other))
    {
      LogDebug(COMPONENT_STATE,
               "Can't create a new state id for the pentry %p (E)", pentry);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

      ReleaseToPool(pnew_state, &pclient->pool_state_v4);

      V_w(&pentry->lock);

      *pstatus = STATE_STATE_ERROR;
      return *pstatus;
    }

  /* Set the type and data for this state */
  memcpy(&(pnew_state->state_data), pstate_data, sizeof(state_data_t));
  pnew_state->state_type   = state_type;
  pnew_state->state_seqid  = 0; /* will be incremented to 1 later */
  pnew_state->state_pentry = pentry;
  pnew_state->state_powner = powner_input;

  if (isDebug(COMPONENT_STATE))
    sprint_mem(debug_str, (char *)pnew_state->stateid_other, OTHERSIZE);

  /* Add the state to the related hashtable */
  if(!nfs4_State_Set(pnew_state->stateid_other, pnew_state))
    {
      LogDebug(COMPONENT_STATE,
               "Can't create a new state id %s for the pentry %p (F)",
               debug_str, pentry);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

      ReleaseToPool(pnew_state, &pclient->pool_state_v4);

      V_w(&pentry->lock);

      /* Return STATE_MALLOC_ERROR since most likely the nfs4_State_Set failed
       * to allocate memory.
       */
      *pstatus = STATE_MALLOC_ERROR;
      return *pstatus;
    }

  /* Add state to list for cache entry */
  glist_add_tail(&pentry->object.file.state_list, &pnew_state->state_list);

  /* Copy the result */
  *ppstate = pnew_state;

  LogFullDebug(COMPONENT_STATE,
               "Add State: %s", debug_str);

  V_w(&pentry->lock);

  /* Regular exit */
  *pstatus = STATE_SUCCESS;
  return *pstatus;
}                               /* state_add */

/**
 *
 * state_del: deletes a state from the hash's state
 *
 * Deletes a state from the hash's state
 *
 * @param pstate   [OUT]   pointer to the new state
 * @param pclient  [INOUT] related cache inode client
 * @param pstatus  [OUT]   returned status
 *
 * @return the same as *pstatus
 *
 */
state_status_t state_del(state_t              * pstate,
                         cache_inode_client_t * pclient,
                         state_status_t       * pstatus)
{
  cache_entry_t * pentry = NULL;
  char            debug_str[OTHERSIZE * 2 + 1];

  if (isDebug(COMPONENT_STATE))
    sprint_mem(debug_str, (char *)pstate->stateid_other, OTHERSIZE);

  LogFullDebug(COMPONENT_STATE, "Deleting state %s", debug_str);

  /* Locks the related pentry before operating on it */
  pentry = pstate->state_pentry;

  /* Remove the entry from the HashTable */
  if(!nfs4_State_Del(pstate->stateid_other))
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

      LogDebug(COMPONENT_STATE, "Could not delete state %s", debug_str);

      *pstatus = STATE_STATE_ERROR;
      return *pstatus;
    }

  P_w(&pentry->lock);

  /* Release the state owner reference */
  if(pstate->state_powner != NULL)
    dec_state_owner_ref(pstate->state_powner, pclient);

  /* Remove from the list of states for a particular cache entry */
  glist_del(&pstate->state_list);

  /* Remove from the list of lock states for a particular open state */
  if(pstate->state_type == STATE_TYPE_LOCK)
    glist_del(&pstate->state_data.lock.state_sharelist);

  ReleaseToPool(pstate, &pclient->pool_state_v4);

  LogFullDebug(COMPONENT_STATE, "Deleted state %s", debug_str);

  *pstatus = STATE_SUCCESS;

  V_w(&pentry->lock);

  return *pstatus;
}                               /* state_del */
