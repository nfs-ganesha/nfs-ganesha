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
 * \file    cache_inode_state.c
 * \author  $Author: deniel $
 * \date    $Date$
 * \version $Revision$
 * \brief   This file contains functions used in state management.
 *
 * cache_inode_state.c : This file contains functions used in state management.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"
#include "nfs_core.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

/**
 *
 * cache_inode_state_conflict : checks for a conflict between an existing state and a candidate state.
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
int cache_inode_state_conflict(cache_inode_state_t * pstate,
                               cache_inode_state_type_t state_type,
                               cache_inode_state_data_t * pstate_data)
{
  int rc = FALSE;

  if(pstate == NULL || pstate_data == NULL)
    return TRUE;

  switch (state_type)
    {
    case CACHE_INODE_STATE_NONE:
      rc = FALSE;               /* STATE_NONE conflicts with nobody */
      break;

    case CACHE_INODE_STATE_SHARE:
      if(pstate->state_type == CACHE_INODE_STATE_SHARE)
        {
          if((pstate->state_data.share.share_access & pstate_data->share.share_deny) ||
             (pstate->state_data.share.share_deny & pstate_data->share.share_access))
            rc = TRUE;
        }

    case CACHE_INODE_STATE_LOCK:
      rc = FALSE;
      break;                    /* lock conflict is managed in the NFS request */

    case CACHE_INODE_STATE_LAYOUT:
      rc = FALSE;  /** @todo No conflict management on layout for now */
      break;

    case CACHE_INODE_STATE_DELEG:
    default:
      /* Not yet implemented for now, answer TRUE to 
       * avoid weird behavior */
      rc = TRUE;
      break;
    }

  return rc;
}                               /* cache_inode_state_conflict */

/**
 *
 * cache_inode_add_state: adds a new state to a file pentry 
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
cache_inode_status_t cache_inode_add_state(cache_entry_t * pentry,
                                           cache_inode_state_type_t state_type,
                                           cache_inode_state_data_t * pstate_data,
                                           cache_inode_open_owner_t * powner_input,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_inode_state_t * *ppstate,
                                           cache_inode_status_t * pstatus)
{
  cache_inode_state_t *phead_state = NULL;
  cache_inode_state_t *pnew_state = NULL;
  cache_inode_state_t *piter_state = NULL;
  cache_inode_state_t *piter_saved = NULL;
  cache_inode_open_owner_t *powner = powner_input;
  char debug_str[25];
  bool_t conflict_found = FALSE;
  unsigned int i = 0;

  /* Sanity Check */
  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pentry == NULL || pstate_data == NULL || pclient == NULL || pcontext == NULL
     || powner_input == NULL || ppstate == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* entry has to be a file */
  if(pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Acquire lock to enter critical section on this entry */
  P_w(&pentry->lock);

  GET_PREALLOC(pnew_state,
               pclient->pool_state_v4,
               pclient->nb_pre_state_v4, cache_inode_state_t, next);

  if(pnew_state == NULL)
    {
      LogDebug(COMPONENT_CACHE_INODE,
                        "Can't allocate a new file state from cache pool");
      *pstatus = CACHE_INODE_MALLOC_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* If there already a state or not ? */
  if(pentry->object.file.pstate_head == NULL)
    {
      /* The file has no state for now, accept this new state */
      pnew_state->next = NULL;
      pnew_state->prev = NULL;

      /* Add the stateid.other, this will increment pentry->object.file.state_current_counter */
      if(!nfs4_BuildStateId_Other(pentry,
                                  pcontext, powner_input, pnew_state->stateid_other))
        {
          LogDebug(COMPONENT_CACHE_INODE,
                       "Can't create a new state id for the pentry %p (A)", pentry);
          *pstatus = CACHE_INODE_STATE_ERROR;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* Set the type and data for this state */
      pnew_state->state_type = state_type;
      memcpy((char *)&(pnew_state->state_data), (char *)pstate_data,
             sizeof(cache_inode_state_data_t));
      pnew_state->seqid = 0;
      pnew_state->pentry = pentry;
      pnew_state->powner = powner;

      /* Set the head state id */
      pentry->object.file.pstate_head = (void *)pnew_state;
    }
  else
    {
      /* Brwose the state's list */
      for(piter_state = pentry->object.file.pstate_head; piter_state != NULL;
          piter_saved = piter_state, piter_state = piter_state->next)
        {
          if(cache_inode_state_conflict(piter_state, state_type, pstate_data))
            {
              conflict_found = TRUE;
              break;
            }
        }

      /* An error is to be returned if a conflict is found */
      if(conflict_found == TRUE)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                            "new state conflicts with another state for pentry %p",
                            pentry);
          *pstatus = CACHE_INODE_STATE_CONFLICT;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* If this point is reached, then the state is to be added to the state list and piter_saved is the tail of the list  */
      pnew_state->next = NULL;
      pnew_state->prev = piter_saved;
      piter_saved->next = pnew_state;

      /* Add the stateid.other, this will increment pentry->object.file.state_current_counter */
      if(!nfs4_BuildStateId_Other
         (pentry, pcontext, powner_input, pnew_state->stateid_other))
        {
          LogDebug(COMPONENT_CACHE_INODE,
                            "Can't create a new state id for the pentry %p (E)", pentry);
          *pstatus = CACHE_INODE_STATE_ERROR;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* Set the type and data for this state */
      pnew_state->state_type = state_type;
      memcpy((char *)&(pnew_state->state_data), (char *)pstate_data,
             sizeof(cache_inode_state_data_t));
      pnew_state->seqid = 0;
      pnew_state->pentry = pentry;
      pnew_state->powner = powner;

      /* Set the head state id */
      pentry->object.file.pstate_tail = (void *)pnew_state;
    }                           /* else */

  /* Add the state to the related hashtable */
  if(!nfs4_State_Set(pnew_state->stateid_other, pnew_state))
    {
      LogDebug(COMPONENT_CACHE_INODE,
                        "Can't create a new state id for the pentry %p (F)", pentry);
      *pstatus = CACHE_INODE_STATE_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1;

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* Copy the result */
  *ppstate = pnew_state;

  /* Regular exit */
  *pstatus = CACHE_INODE_SUCCESS;

  if (isFullDebug(COMPONENT_STATES)) {
    sprint_mem(debug_str, (char *)pnew_state->stateid_other, 12);
    LogFullDebug(COMPONENT_STATES,"cache_inode_add_state : %s", debug_str);
  }
  V_w(&pentry->lock);

  return *pstatus;
}                               /* cache_inode_add_state */

/**
 *
 * cache_inode_get_state: gets a state from the hash's state
 *
 * Gets a state from the hash's state
 *
 * @param other     [IN]    stateid.other used as hash key
 * @param ppstate   [OUT]   pointer to the pointer to the new state
 * @param pclient   [INOUT] related cache inode client
 * @param pstatus   [OUT]   returned status 
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_get_state(char other[12],
                                           cache_inode_state_t * *ppstate,
                                           cache_inode_client_t * pclient,
                                           cache_inode_status_t * pstatus)
{
  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(ppstate == NULL || pclient == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  if(!nfs4_State_Get_Pointer(other, ppstate))
    {
      *pstatus = CACHE_INODE_NOT_FOUND;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET_STATE] += 1;

      return *pstatus;
    }

  /* Sanity check, mostly for debug */
  if(memcmp(other, (*ppstate)->stateid_other, 12))
    LogFullDebug(COMPONENT_STATES, "-------------> Warning !!!! Stateid(other) differs !!!!!!\n");

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}                               /* cache_inode_get_state */

/**
 *
 * cache_inode_update_state: update a state from the hash's state
 *
 * Updates a state from the hash's state
 *
 * @param pstate   [OUT]   pointer to the new state
 * @param pclient  [INOUT] related cache inode client
 * @param pstatus  [OUT]   returned status 
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_update_state(cache_inode_state_t * pstate,
                                              cache_inode_client_t * pclient,
                                              cache_inode_status_t * pstatus)
{
  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pstate == NULL || pclient == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  if(!nfs4_State_Update(pstate->stateid_other, pstate))
    {
      *pstatus = CACHE_INODE_STATE_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_UPDATE_STATE] += 1;

      return *pstatus;
    }

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}                               /* cache_inode_set_state */

/**
 *
 * cache_inode_del_state_by_key: deletes a state from the hash's state associated with a given stateid
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
cache_inode_status_t cache_inode_del_state_by_key(char other[12],
                                                  cache_inode_client_t * pclient,
                                                  cache_inode_status_t * pstatus)
{
  cache_inode_state_t *pstate = NULL;
  cache_entry_t *pentry = NULL;

  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pstatus == NULL || pclient == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Does this state exists ? */
  if(!nfs4_State_Get_Pointer(other, &pstate))
    {
      *pstatus = CACHE_INODE_NOT_FOUND;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

      return *pstatus;
    }

  /* The state exists, locks the related pentry before operating on it */
  pentry = pstate->pentry;

  P_w(&pentry->lock);

  /* Set the head counter */
  if(pstate == pentry->object.file.pstate_head)
    {
      /* This is the first state managed */
      if(pstate->next == NULL)
        {
          /* I am the only remaining state, set the head counter to 0 in the pentry */
          pentry->object.file.pstate_head = NULL;
        }
      else
        {
          /* The state that is next to me become the new head */
          pentry->object.file.pstate_head = (void *)pstate->next;
        }
    }

  /* redo the double chained list */
  if(pstate->next != NULL)
    pstate->next->prev = pstate->prev;

  if(pstate->prev != NULL)
    pstate->prev->next = pstate->next;

  if(!memcmp((char *)pstate->stateid_other, other, 12))
    {
      /* Remove the entry from the HashTable */
      if(!nfs4_State_Del(pstate->stateid_other))
        {
          *pstatus = CACHE_INODE_STATE_ERROR;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

          V_w(&pentry->lock);

          return *pstatus;
        }

      /* reset the pstate field to avoid later mistakes */
      memset((char *)pstate->stateid_other, 0, 12);
      pstate->state_type = CACHE_INODE_STATE_NONE;
      pstate->next = NULL;
      pstate->prev = NULL;
      pstate->pentry = NULL;

      RELEASE_PREALLOC(pstate, pclient->pool_state_v4, next);
    }

  *pstatus = CACHE_INODE_SUCCESS;

  V_w(&pentry->lock);

  return *pstatus;
}                               /* cache_inode_del_state_by_key */

/**
 *
 * cache_inode_del_state: deletes a state from the hash's state
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
cache_inode_status_t cache_inode_del_state(cache_inode_state_t * pstate,
                                           cache_inode_client_t * pclient,
                                           cache_inode_status_t * pstatus)
{
  cache_inode_state_t *ptest_state = NULL;
  cache_entry_t *pentry = NULL;
  char str[25];
  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pstate == NULL || pclient == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  unsigned int i = 0;

  if (isFullDebug(COMPONENT_STATES)) {
    sprint_mem(str, (char *)pstate->stateid_other, 12);
    LogFullDebug(COMPONENT_STATES,"cache_inode_del_state : %s", str);
  }

  /* Does this state exists ? */
  if(!nfs4_State_Get_Pointer(pstate->stateid_other, &ptest_state))
    {
      *pstatus = CACHE_INODE_NOT_FOUND;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

      return *pstatus;
    }

  /* The state exists, locks the related pentry before operating on it */
  pentry = pstate->pentry;

  P_w(&pentry->lock);

  /* Set the head counter */
  if(pstate == pentry->object.file.pstate_head)
    {
      /* This is the first state managed */
      if(pstate->next == NULL)
        {
          /* I am the only remaining state, set the head counter to 0 in the pentry */
          pentry->object.file.pstate_head = NULL;
        }
      else
        {
          /* The state that is next to me become the new head */
          pentry->object.file.pstate_head = (void *)pstate->next;
        }
    }

  /* redo the double chained list */
  if(pstate->next != NULL)
    pstate->next->prev = pstate->prev;

  if(pstate->prev != NULL)
    pstate->prev->next = pstate->next;

  /* Remove the entry from the HashTable */
  if(!nfs4_State_Del(pstate->stateid_other))
    {
      *pstatus = CACHE_INODE_STATE_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1;

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* reset the pstate field to avoid later mistakes */
  memset((char *)pstate->stateid_other, 0, 12);
  pstate->state_type = CACHE_INODE_STATE_NONE;
  pstate->next = NULL;
  pstate->prev = NULL;
  pstate->pentry = NULL;

  RELEASE_PREALLOC(pstate, pclient->pool_state_v4, next);

  *pstatus = CACHE_INODE_SUCCESS;

  V_w(&pentry->lock);

  return *pstatus;
}                               /* cache_inode_del_state */

/**
 *
 * cache_inode_state_iterate: iterates on the states's loop
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
cache_inode_status_t cache_inode_state_iterate(cache_entry_t * pentry,
                                               cache_inode_state_t * *ppstate,
                                               cache_inode_state_t * previous_pstate,
                                               cache_inode_client_t * pclient,
                                               fsal_op_context_t * pcontext,
                                               cache_inode_status_t * pstatus)
{
  cache_inode_state_t *piter_state = NULL;
  cache_inode_state_t *phead_state = NULL;
  uint64_t fileid_digest = 0;

  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pentry == NULL || ppstate == NULL || pclient == NULL || pcontext == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Here, we need to know the file id */
  if(FSAL_IS_ERROR(FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                     FSAL_DIGEST_FILEID3,
                                     &(pentry->object.file.handle),
                                     (caddr_t) & fileid_digest)))
    {
      LogDebug(COMPONENT_CACHE_INODE,
                        "Can't create a new state id for the pentry %p (F)", pentry);
      *pstatus = CACHE_INODE_STATE_ERROR;

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
      if(previous_pstate->pentry != pentry)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                            "Bad previous pstate: related to pentry %p, not to %p",
                            previous_pstate->pentry, pentry);

          *pstatus = CACHE_INODE_STATE_ERROR;

          V_r(&pentry->lock);

          return *pstatus;
        }

      piter_state = previous_pstate->next;
    }

  *ppstate = piter_state;

  V_r(&pentry->lock);

  return *pstatus;
}                               /* cache_inode_state_iterate */
