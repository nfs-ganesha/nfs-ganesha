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
  state_t            * pnew_state = NULL;
  state_t            * piter_state = NULL;
  state_t            * piter_saved = NULL;
  state_owner_t      * powner = powner_input;
  char                 debug_str[OTHERSIZE * 2 + 1];
  bool_t               conflict_found = FALSE;

  /* Sanity Check */
  if(pstatus == NULL)
    return STATE_INVALID_ARGUMENT;

  if(pentry == NULL || pstate_data == NULL || pclient == NULL || pcontext == NULL
     || powner_input == NULL || ppstate == NULL)
    {
      *pstatus = STATE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* entry has to be a file */
  if(pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = STATE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Acquire lock to enter critical section on this entry */
  P_w(&pentry->lock);

  GetFromPool(pnew_state, &pclient->pool_state_v4, state_t);

  if(pnew_state == NULL)
    {
      LogDebug(COMPONENT_STATE,
               "Can't allocate a new file state from cache pool");
      *pstatus = STATE_MALLOC_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* If there already a state or not ? */
  if(pentry->object.file.pstate_head == NULL)
    {
      /* The file has no state for now, accept this new state */
      pnew_state->state_next = NULL;
      pnew_state->state_prev = NULL;

      /* Add the stateid.other, this will increment pentry->object.file.state_current_counter */
      if(!nfs4_BuildStateId_Other(pentry,
                                  pcontext, powner_input, pnew_state->stateid_other))
        {
          LogDebug(COMPONENT_STATE,
                   "Can't create a new state id for the pentry %p (A)", pentry);
          *pstatus = STATE_STATE_ERROR;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* Set the head state id */
      pentry->object.file.pstate_head = (void *)pnew_state;
    }
  else
    {
      /* Brwose the state's list */
      for(piter_state = pentry->object.file.pstate_head; piter_state != NULL;
          piter_saved = piter_state, piter_state = piter_state->state_next)
        {
          if(state_conflict(piter_state, state_type, pstate_data))
            {
              conflict_found = TRUE;
              break;
            }
        }

      /* An error is to be returned if a conflict is found */
      if(conflict_found == TRUE)
        {
          LogDebug(COMPONENT_STATE,
                   "new state conflicts with another state for pentry %p",
                   pentry);
          *pstatus = STATE_STATE_CONFLICT;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* If this point is reached, then the state is to be added to the state list and piter_saved is the tail of the list  */
      pnew_state->state_next = NULL;
      pnew_state->state_prev = piter_saved;
      piter_saved->state_next = pnew_state;

      /* Add the stateid.other, this will increment pentry->object.file.state_current_counter */
      if(!nfs4_BuildStateId_Other
         (pentry, pcontext, powner_input, pnew_state->stateid_other))
        {
          LogDebug(COMPONENT_STATE,
                   "Can't create a new state id for the pentry %p (E)", pentry);
          *pstatus = STATE_STATE_ERROR;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* Set the tail state id */
      pentry->object.file.pstate_tail = (void *)pnew_state;
    }                           /* else */

  /* Set the type and data for this state */
  pnew_state->state_type = state_type;
  memcpy(&(pnew_state->state_data), pstate_data, sizeof(state_data_t));
  pnew_state->state_seqid = 0; /* will be incremented to 1 later */
  pnew_state->state_pentry = pentry;
  pnew_state->state_powner = powner;

  if (isDebug(COMPONENT_STATE))
    sprint_mem(debug_str, (char *)pnew_state->stateid_other, OTHERSIZE);

  /* Add the state to the related hashtable */
  if(!nfs4_State_Set(pnew_state->stateid_other, pnew_state))
    {
      LogDebug(COMPONENT_STATE,
               "Can't create a new state id %s for the pentry %p (F)",
               debug_str, pentry);
      *pstatus = STATE_STATE_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* Copy the result */
  *ppstate = pnew_state;

  /* Regular exit */
  *pstatus = STATE_SUCCESS;

  LogFullDebug(COMPONENT_STATE,
               "Add State: %s", debug_str);

  V_w(&pentry->lock);

  return *pstatus;
}                               /* state_add */

/**
 *
 * state_del_by_key: deletes a state from the hash's state associated with a given stateid
 *
 * Deletes a state from the hash's state
 *
 * @param other    [IN]    stateid.other used as hash key
 * @param pstate   [OUT]   pointer to the new state
 * @param pclient  [INOUT] related cache inode client
 * @param pstatus  [OUT]   returned status
 *
 * @return the same as *pstatus
 *
 */
state_status_t state_del_by_key(char                   other[OTHERSIZE],
                                cache_inode_client_t * pclient,
                                state_status_t       * pstatus)
{
  state_t       * pstate = NULL;
  cache_entry_t * pentry = NULL;
  char            debug_str[OTHERSIZE * 2 + 1];

  if(pstatus == NULL)
    return STATE_INVALID_ARGUMENT;

  if(pstatus == NULL || pclient == NULL)
    {
      *pstatus = STATE_INVALID_ARGUMENT;
      return *pstatus;
    }

  if (isDebug(COMPONENT_STATE))
    sprint_mem(debug_str, other, OTHERSIZE);

  /* Does this state exists ? */
  if(!nfs4_State_Get_Pointer(other, &pstate))
    {
      *pstatus = STATE_NOT_FOUND;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

      LogDebug(COMPONENT_STATE, "Could not find state %s to delete", debug_str);

      return *pstatus;
    }

  /* The state exists, locks the related pentry before operating on it */
  pentry = pstate->state_pentry;

  P_w(&pentry->lock);

  /* Set the head counter */
  if(pstate == pentry->object.file.pstate_head)
    {
      /* This is the first state managed */
      if(pstate->state_next == NULL)
        {
          /* I am the only remaining state, set the head counter to 0 in the pentry */
          pentry->object.file.pstate_head = NULL;
        }
      else
        {
          /* The state that is next to me become the new head */
          pentry->object.file.pstate_head = (void *)pstate->state_next;
        }
    }

  /* redo the double chained list */
  if(pstate->state_next != NULL)
    pstate->state_next->state_prev = pstate->state_prev;

  if(pstate->state_prev != NULL)
    pstate->state_prev->state_next = pstate->state_next;

  if(!memcmp((char *)pstate->stateid_other, other, OTHERSIZE))
    {
      /* Remove the entry from the HashTable */
      if(!nfs4_State_Del(pstate->stateid_other))
        {
          *pstatus = STATE_STATE_ERROR;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

          LogDebug(COMPONENT_STATE, "Could not delete state %s", debug_str);

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* reset the pstate field to avoid later mistakes */
      memset((char *)pstate->stateid_other, 0, OTHERSIZE);
      pstate->state_type   = STATE_TYPE_NONE;
      pstate->state_next   = NULL;
      pstate->state_prev   = NULL;
      pstate->state_pentry = NULL;

      LogFullDebug(COMPONENT_STATE, "Deleted state %s", debug_str);

      ReleaseToPool(pstate, &pclient->pool_state_v4);
    }
  else
    {
      LogDebug(COMPONENT_STATE, "Something odd happened while deleting state %s", debug_str);
    }

  *pstatus = STATE_SUCCESS;

  V_w(&pentry->lock);

  return *pstatus;
}                               /* state_del_by_key */

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
  state_t       * ptest_state = NULL;
  cache_entry_t * pentry = NULL;
  char            debug_str[OTHERSIZE * 2 + 1];

  if(pstatus == NULL)
    return STATE_INVALID_ARGUMENT;

  if(pstate == NULL || pclient == NULL)
    {
      *pstatus = STATE_INVALID_ARGUMENT;
      return *pstatus;
    }

  if (isDebug(COMPONENT_STATE))
    sprint_mem(debug_str, (char *)pstate->stateid_other, OTHERSIZE);

  /* Does this state exists ? */
  if(!nfs4_State_Get_Pointer(pstate->stateid_other, &ptest_state))
    {
      *pstatus = STATE_NOT_FOUND;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

      LogDebug(COMPONENT_STATE, "Could not find state %s to delete", debug_str);

      return *pstatus;
    }

  /* The state exists, locks the related pentry before operating on it */
  pentry = pstate->state_pentry;

  P_w(&pentry->lock);

  /* Set the head counter */
  if(pstate == pentry->object.file.pstate_head)
    {
      /* This is the first state managed */
      if(pstate->state_next == NULL)
        {
          /* I am the only remaining state, set the head counter to 0 in the pentry */
          pentry->object.file.pstate_head = NULL;
        }
      else
        {
          /* The state that is next to me become the new head */
          pentry->object.file.pstate_head = (void *)pstate->state_next;
        }
    }

  /* redo the double chained list */
  if(pstate->state_next != NULL)
    pstate->state_next->state_prev = pstate->state_prev;

  if(pstate->state_prev != NULL)
    pstate->state_prev->state_next = pstate->state_next;

  /* Remove the entry from the HashTable */
  if(!nfs4_State_Del(pstate->stateid_other))
    {
      *pstatus = STATE_STATE_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

      LogDebug(COMPONENT_STATE, "Could not delete state %s", debug_str);

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* reset the pstate field to avoid later mistakes */
  memset((char *)pstate->stateid_other, 0, OTHERSIZE);
  pstate->state_type   = STATE_TYPE_NONE;
  pstate->state_next   = NULL;
  pstate->state_prev   = NULL;
  pstate->state_pentry = NULL;

  ReleaseToPool(pstate, &pclient->pool_state_v4);

  LogFullDebug(COMPONENT_STATE, "Deleted state %s", debug_str);

  *pstatus = STATE_SUCCESS;

  V_w(&pentry->lock);

  return *pstatus;
}                               /* state_del */

/**
 *
 * state_iterate: iterates on the states's loop
 *
 * Iterates on the states's loop
 *
 * @param other           [IN]    stateid.other used as hash key
 * @param ppstate         [OUT]   pointer to a pointer of state that will point to the result
 * @param previous_pstate [IN]    a pointer to be used to start search. Should be NULL at first call
 * @param pclient         [INOUT] related cache inode client
 * @param pcontext        [IN]    related FSAL operation context
 * @pstatus               [OUT]   status for the operation
 *
 * @return the same as *pstatus
 *
 */
state_status_t state_iterate(cache_entry_t         * pentry,
                             state_t              ** ppstate,
                             state_t               * previous_pstate,
                             cache_inode_client_t  * pclient,
                             fsal_op_context_t     * pcontext,
                             state_status_t        * pstatus)
{
  state_t  * piter_state = NULL;
  uint64_t   fileid_digest = 0;

  if(pstatus == NULL)
    return STATE_INVALID_ARGUMENT;

  if(pentry == NULL || ppstate == NULL || pclient == NULL || pcontext == NULL)
    {
      *pstatus = STATE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Here, we need to know the file id */
  if(FSAL_IS_ERROR(FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                     FSAL_DIGEST_FILEID4,
                                     &(pentry->object.file.handle),
                                     (caddr_t) & fileid_digest)))
    {
      LogDebug(COMPONENT_STATE,
               "Can't create a new state id for the pentry %p (F)", pentry);
      *pstatus = STATE_STATE_ERROR;

      return *pstatus;
    }

  P_r(&pentry->lock);

  /* if this is the first call, used the data stored in pentry to get the state's chain head */
  if(previous_pstate == NULL)
    {
      /* The file already have at least one state, browse all of them, starting with the first state */
      piter_state = pentry->object.file.pstate_head;
    }
  else
    {
      /* Sanity check: make sure that this state is related to this pentry */
      if(previous_pstate->state_pentry != pentry)
        {
          LogDebug(COMPONENT_STATE,
                   "Bad previous pstate: related to pentry %p, not to %p",
                   previous_pstate->state_pentry, pentry);

          *pstatus = STATE_STATE_ERROR;

          V_r(&pentry->lock);

          return *pstatus;
        }

      piter_state = previous_pstate->state_next;
    }

  *ppstate = piter_state;

  V_r(&pentry->lock);

  return *pstatus;
}                               /* state_iterate */
