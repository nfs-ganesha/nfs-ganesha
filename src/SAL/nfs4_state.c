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
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "fsal.h"
#include "sal_functions.h"
#include "stuff_alloc.h"
#include "cache_inode_lru.h"

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
      return FALSE;              /** layout conflict is managed by the FSAL */

    case STATE_TYPE_DELEG:
      /* Not yet implemented for now, answer TRUE to avoid weird behavior */
      return TRUE;
    }

  return TRUE;
}                               /* state_conflict */

/**
 *
 * state_add_impl: adds a new state to a file pentry
 *
 * Adds a new state to a file pentry.  This version of the function
 * does not take the state lock on the entry.  It exists to allow
 * callers to integrate state into a larger operation.
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
state_status_t state_add_impl(cache_entry_t         * pentry,
                              state_type_t            state_type,
                              state_data_t          * pstate_data,
                              state_owner_t         * powner_input,
                              cache_inode_client_t  * pclient,
                              fsal_op_context_t     * pcontext,
                              state_t              ** ppstate,
                              state_status_t        * pstatus)
{
  state_t              * pnew_state  = NULL;
  state_t              * piter_state = NULL;
  char                   debug_str[OTHERSIZE * 2 + 1];
  struct glist_head    * glist;
  cache_inode_status_t   cache_status;
  bool_t                 got_pinned = FALSE;

  if(glist_empty(&pentry->state_list))
    {
      cache_status = cache_inode_inc_pin_ref(pentry);

      if(cache_status != CACHE_INODE_SUCCESS)
        {
          *pstatus = cache_inode_status_to_state_status(cache_status);
          LogDebug(COMPONENT_STATE,
                   "Could not pin file");
          return *pstatus;
        }

      got_pinned = TRUE;
    }

  GetFromPool(pnew_state, &pclient->pool_state_v4, state_t);

  if(pnew_state == NULL)
    {
      LogDebug(COMPONENT_STATE,
               "Can't allocate a new file state from cache pool");

      /* stat */
      *pstatus = STATE_MALLOC_ERROR;

      if(got_pinned)
        cache_inode_dec_pin_ref(pentry);

      return *pstatus;
    }

  memset(pnew_state, 0, sizeof(*pnew_state));

  /* Browse the state's list */
  glist_for_each(glist, &pentry->state_list)
    {
      piter_state = glist_entry(glist, state_t, state_list);

      if(state_conflict(piter_state, state_type, pstate_data))
        {
          LogDebug(COMPONENT_STATE,
                   "new state conflicts with another state for pentry %p",
                   pentry);

          /* stat */
          ReleaseToPool(pnew_state, &pclient->pool_state_v4);

          *pstatus = STATE_STATE_CONFLICT;

          if(got_pinned)
            cache_inode_dec_pin_ref(pentry);

          return *pstatus;
        }
    }

  /* Add the stateid.other, this will increment state_id_counter */
  nfs4_BuildStateId_Other(pnew_state->stateid_other);

  /* Set the type and data for this state */
  memcpy(&(pnew_state->state_data), pstate_data, sizeof(state_data_t));
  pnew_state->state_type   = state_type;
  pnew_state->state_seqid  = 0; /* will be incremented to 1 later */
  pnew_state->state_pentry = pentry;
  pnew_state->state_powner = powner_input;

  if (isDebug(COMPONENT_STATE))
    sprint_mem(debug_str, (char *)pnew_state->stateid_other, OTHERSIZE);

  init_glist(&pnew_state->state_list);
  init_glist(&pnew_state->state_owner_list);

  /* Add the state to the related hashtable */
  if(!nfs4_State_Set(pnew_state->stateid_other, pnew_state))
    {
      LogDebug(COMPONENT_STATE,
               "Can't create a new state id %s for the pentry %p (F)",
               debug_str, pentry);

      ReleaseToPool(pnew_state, &pclient->pool_state_v4);

      /* Return STATE_MALLOC_ERROR since most likely the nfs4_State_Set failed
       * to allocate memory.
       */
      *pstatus = STATE_MALLOC_ERROR;

      if(got_pinned)
        cache_inode_dec_pin_ref(pentry);

      return *pstatus;
    }

  /* Add state to list for cache entry */
  glist_add_tail(&pentry->state_list, &pnew_state->state_list);

  P(powner_input->so_mutex);
  glist_add_tail(&powner_input->so_owner.so_nfs4_owner.so_state_list,
                 &pnew_state->state_owner_list);
  V(powner_input->so_mutex);

  /* Copy the result */
  *ppstate = pnew_state;

  LogFullDebug(COMPONENT_STATE,
               "Add State: %s", debug_str);

  /* Regular exit */
  *pstatus = STATE_SUCCESS;
  return *pstatus;
}                               /* state_add */


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
  /* Ensure that states are are associated only with the appropriate
     owners */

  if (((state_type == STATE_TYPE_SHARE) &&
       (powner_input->so_type != STATE_OPEN_OWNER_NFSV4)) ||
      ((state_type == STATE_TYPE_LOCK) &&
       (powner_input->so_type != STATE_LOCK_OWNER_NFSV4)) ||
      (((state_type == STATE_TYPE_DELEG) ||
        (state_type == STATE_TYPE_LAYOUT)) &&
       (powner_input->so_type != STATE_CLIENTID_OWNER_NFSV4)))
    {
      return (*pstatus = STATE_BAD_TYPE);
    }

  pthread_rwlock_wrlock(&pentry->state_lock);
  state_add_impl(pentry, state_type, pstate_data, powner_input,
                 pclient, pcontext, ppstate, pstatus);
  pthread_rwlock_unlock(&pentry->state_lock);

  return *pstatus;
}                               /* state_add */

state_status_t state_del_locked(state_t              * pstate,
                                cache_entry_t        * pentry,
                                cache_inode_client_t * pclient)
{
  char            debug_str[OTHERSIZE * 2 + 1];

  if (isDebug(COMPONENT_STATE))
    sprint_mem(debug_str, (char *)pstate->stateid_other, OTHERSIZE);

  LogFullDebug(COMPONENT_STATE, "Deleting state %s", debug_str);

  /* Remove the entry from the HashTable */
  if(!nfs4_State_Del(pstate->stateid_other))
    {
      LogDebug(COMPONENT_STATE, "Could not delete state %s", debug_str);

      return STATE_STATE_ERROR;
    }

  /* Remove from list of states owned by owner */

  /* Release the state owner reference */
  if(pstate->state_powner != NULL)
    {
      P(pstate->state_powner->so_mutex);
      glist_del(&pstate->state_owner_list);
      V(pstate->state_powner->so_mutex);
      dec_state_owner_ref(pstate->state_powner, pclient);
    }

  /* Remove from the list of states for a particular cache entry */
  glist_del(&pstate->state_list);

  /* Remove from the list of lock states for a particular open state */
  if(pstate->state_type == STATE_TYPE_LOCK)
    glist_del(&pstate->state_data.lock.state_sharelist);

  /* Remove from list of states for a particular export */
  P(pstate->state_pexport->exp_state_mutex);
  glist_del(&pstate->state_export_list);
  V(pstate->state_pexport->exp_state_mutex);

  ReleaseToPool(pstate, &pclient->pool_state_v4);

  LogFullDebug(COMPONENT_STATE, "Deleted state %s", debug_str);

  if(glist_empty(&pentry->state_list))
    cache_inode_dec_pin_ref(pentry);

  return STATE_SUCCESS;
}

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
  cache_entry_t *entry = pstate->state_pentry;

  pthread_rwlock_wrlock(&entry->state_lock);

  *pstatus = state_del_locked(pstate, pstate->state_pentry, pclient);

  pthread_rwlock_unlock(&entry->state_lock);

  return *pstatus;
}                               /* state_del */

void state_nfs4_state_wipe(cache_entry_t        * pentry,
                           cache_inode_client_t * pclient)
{
  struct glist_head * glist;
  state_t           * pstate = NULL;

  if(glist_empty(&pentry->state_list))
    return;

  /* Iterate through file's state to wipe out all stateids */
  glist_for_each(glist, &pentry->state_list)
    {
      pstate = glist_entry(glist, state_t, state_list);

      switch(pstate->state_type)
        {
          case STATE_TYPE_SHARE:
            break;

          case STATE_TYPE_LOCK:
            break;

          case STATE_TYPE_DELEG:
            break;

          case STATE_TYPE_LAYOUT:
            break;

          case STATE_TYPE_NONE:
            break;
        }
      state_del_locked(pstate, pentry, pclient);
    }

  cache_inode_dec_pin_ref(pstate->state_pentry);
}
