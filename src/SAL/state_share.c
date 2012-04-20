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
 * \file    state_share.c
 * \author  $Author: deniel $
 * \date    $Date$
 * \version $Revision$
 * \brief   This file contains functions used in share reservation management.
 *
 * state_share.c : This file contains functions used in share reservation management.
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
#include <assert.h>

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "stuff_alloc.h"
#ifdef _USE_NLM
#include "nlm_util.h"
#endif

/* Update the ref counter of share state of given file. */
static void state_share_update_counter(cache_entry_t * pentry,
                                       int old_access,
                                       int old_deny,
                                       int new_access,
                                       int new_deny);

/* Calculate the union of share access of given file. */
static unsigned int state_share_get_share_access(cache_entry_t * pentry);

/* Calculate the union of share deny of given file. */
static unsigned int state_share_get_share_deny(cache_entry_t * pentry);

/* Push share state down to FSAL. Only the union of share states should be
 * passed to this function.
 */
static state_status_t do_share_op(cache_entry_t        * pentry,
                                  fsal_op_context_t    * pcontext,
                                  state_owner_t        * powner,
                                  fsal_share_param_t   * pshare,
                                  cache_inode_client_t * pclient)
{
  fsal_status_t fsal_status;
  state_status_t status = STATE_SUCCESS;
  fsal_staticfsinfo_t * pstatic = pcontext->export_context->fe_static_fs_info;

  /* Quick exit if share reservation is not supported by FSAL */
  if(!pstatic->share_support)
    return STATE_SUCCESS;

  fsal_status = FSAL_share_op(cache_inode_fd(pentry),
                              &pentry->handle,
                              pcontext,
                              NULL,
                              *pshare);

  status = state_error_convert(fsal_status);

  LogFullDebug(COMPONENT_STATE,
               "FSAL_share_op returned %s",
               state_err_str(status));

  return status;
}

/* This is called when new share state is added. */
state_status_t state_share_add(cache_entry_t         * pentry,
                               fsal_op_context_t     * pcontext,
                               state_owner_t         * powner,
                               state_t               * pstate,  /* state that holds share bits to be added */
                               cache_inode_client_t  * pclient,
                               state_status_t        * pstatus)
{
  state_status_t status = STATE_SUCCESS;
  unsigned int            old_pentry_share_access = 0;
  unsigned int            old_pentry_share_deny = 0;
  unsigned int            new_pentry_share_access = 0;
  unsigned int            new_pentry_share_deny = 0;
  unsigned int            new_share_access = 0;
  unsigned int            new_share_deny = 0;
  fsal_share_param_t      share_param;

  P_w(&pentry->lock);

  /* Check if new share state has conflicts. */
  status = state_share_check_conflict_no_mutex(pentry,
                                               &(pstate->state_data),
                                               pstatus);
  if(status != STATE_SUCCESS)
    {
      V_w(&pentry->lock);
      LogEvent(COMPONENT_STATE, "Share conflicts detected during add");
      *pstatus = STATE_STATE_CONFLICT;
      return *pstatus;
    }

  /* Get the current union of share states of this file. */
  old_pentry_share_access = state_share_get_share_access(pentry);
  old_pentry_share_deny = state_share_get_share_deny(pentry);

  /* Share state to be added. */
  new_share_access = pstate->state_data.share.share_access;
  new_share_deny = pstate->state_data.share.share_deny;

  /* Update the ref counted share state of this file. */
  state_share_update_counter(pentry,
                             0,
                             0,
                             new_share_access,
                             new_share_deny);

  /* Get the updated union of share states of this file. */
  new_pentry_share_access = state_share_get_share_access(pentry);
  new_pentry_share_deny = state_share_get_share_deny(pentry);

  /* If this file's share bits are different from the supposed value, update
   * it.
   */
  if((new_pentry_share_access != old_pentry_share_access) ||
     (new_pentry_share_deny != old_pentry_share_deny))
    {
      /* Try to push to FSAL. */
      share_param.share_access = new_pentry_share_access;
      share_param.share_deny = new_pentry_share_deny;

      status = do_share_op(pentry, pcontext, powner, &share_param, pclient);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     new_share_access,
                                     new_share_deny,
                                     0,
                                     0);
          V_w(&pentry->lock);
          LogDebug(COMPONENT_STATE, "do_share_op failed");
          *pstatus = status;
          return *pstatus;
        }
    }

  LogFullDebug(COMPONENT_STATE, "pstate %p: added share_access %u, "
               "share_deny %u",
               pstate, new_share_access, new_share_deny);

  /* Update previously seen share state in the bitmap. */
  state_share_set_prev(pstate, &(pstate->state_data));

  V_w(&pentry->lock);

  return status;
}

/* This is called when a share state is removed. */
state_status_t state_share_remove(cache_entry_t         * pentry,
                                  fsal_op_context_t     * pcontext,
                                  state_owner_t         * powner,
                                  state_t               * pstate,  /* state that holds share bits to be removed */
                                  cache_inode_client_t  * pclient,
                                  state_status_t        * pstatus)
{
  state_status_t status = STATE_SUCCESS;
  unsigned int            old_pentry_share_access = 0;
  unsigned int            old_pentry_share_deny = 0;
  unsigned int            new_pentry_share_access = 0;
  unsigned int            new_pentry_share_deny = 0;
  unsigned int            removed_share_access = 0;
  unsigned int            removed_share_deny = 0;
  fsal_share_param_t      share_param;

  P_w(&pentry->lock);

  /* Get the current union of share states of this file. */
  old_pentry_share_access = state_share_get_share_access(pentry);
  old_pentry_share_deny = state_share_get_share_deny(pentry);

  /* Share state to be removed. */
  removed_share_access = pstate->state_data.share.share_access;
  removed_share_deny = pstate->state_data.share.share_deny;

  /* Update the ref counted share state of this file. */
  state_share_update_counter(pentry,
                             removed_share_access,
                             removed_share_deny,
                             0,
                             0);

  /* Get the updated union of share states of this file. */
  new_pentry_share_access = state_share_get_share_access(pentry);
  new_pentry_share_deny = state_share_get_share_deny(pentry);

  /* If this file's share bits are different from the supposed value, update
   * it.
   */
  if((new_pentry_share_access != old_pentry_share_access) ||
     (new_pentry_share_deny != old_pentry_share_deny))
    {
      /* Try to push to FSAL. */
      share_param.share_access = new_pentry_share_access;
      share_param.share_deny = new_pentry_share_deny;

      status = do_share_op(pentry, pcontext, powner, &share_param, pclient);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     0,
                                     0,
                                     removed_share_access,
                                     removed_share_deny);
          V_w(&pentry->lock);
          LogDebug(COMPONENT_STATE, "do_share_op failed");
          *pstatus = status;
          return *pstatus;
        }
    }

  LogFullDebug(COMPONENT_STATE, "pstate %p: removed share_access %u, "
               "share_deny %u",
               pstate,
               removed_share_access,
               removed_share_deny);

  V_w(&pentry->lock);

  return status;
}

/* This is called when share state is upgraded during open. */
state_status_t state_share_upgrade(cache_entry_t         * pentry,
                                   fsal_op_context_t     * pcontext,
                                   state_data_t          * pstate_data, /* new share bits */
                                   state_owner_t         * powner,
                                   state_t               * pstate,      /* state that holds current share bits */
                                   cache_inode_client_t  * pclient,
                                   state_status_t        * pstatus)
{
  state_status_t status = STATE_SUCCESS;
  unsigned int            old_pentry_share_access = 0;
  unsigned int            old_pentry_share_deny = 0;
  unsigned int            new_pentry_share_access = 0;
  unsigned int            new_pentry_share_deny = 0;
  unsigned int            old_share_access = 0;
  unsigned int            old_share_deny = 0;
  unsigned int            new_share_access = 0;
  unsigned int            new_share_deny = 0;
  fsal_share_param_t share_param;

  P_w(&pentry->lock);

  /* Check if new share state has conflicts. */
  status = state_share_check_conflict_no_mutex(pentry,
                                               pstate_data,
                                               pstatus);
  if(status != STATE_SUCCESS)
    {
      V_w(&pentry->lock);
      LogEvent(COMPONENT_STATE, "Share conflicts detected during upgrade");
      *pstatus = STATE_STATE_CONFLICT;
      return *pstatus;
    }

  /* Get the current union of share states of this file. */
  old_pentry_share_access = state_share_get_share_access(pentry);
  old_pentry_share_deny = state_share_get_share_deny(pentry);

  /* Old share state. */
  old_share_access = pstate->state_data.share.share_access;
  old_share_deny = pstate->state_data.share.share_deny;

  /* New share state. */
  new_share_access = pstate_data->share.share_access;
  new_share_deny = pstate_data->share.share_deny;

  /* Update the ref counted share state of this file. */
  state_share_update_counter(pentry,
                             old_share_access,
                             old_share_deny,
                             new_share_access,
                             new_share_deny);

  /* Get the updated union of share states of this file. */
  new_pentry_share_access = state_share_get_share_access(pentry);
  new_pentry_share_deny = state_share_get_share_deny(pentry);

  /* If this file's share bits are different from the supposed value, update
   * it.
   */
  if((new_pentry_share_access != old_pentry_share_access) ||
     (new_pentry_share_deny != old_pentry_share_deny))
    {
      /* Try to push to FSAL. */
      share_param.share_access = new_pentry_share_access;
      share_param.share_deny = new_pentry_share_deny;

      status = do_share_op(pentry, pcontext, powner, &share_param, pclient);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     new_share_access,
                                     new_share_deny,
                                     old_share_access,
                                     old_share_deny);
          V_w(&pentry->lock);
          LogDebug(COMPONENT_STATE, "do_share_op failed");
          *pstatus = status;
          return *pstatus;
        }
    }

  /* Update share state. */
  pstate->state_data.share.share_access = new_share_access;
  pstate->state_data.share.share_deny = new_share_deny;
  LogFullDebug(COMPONENT_STATE, "pstate %p: upgraded share_access %u, share_deny %u",
               pstate,
               pstate->state_data.share.share_access,
               pstate->state_data.share.share_deny);

  /* Update previously seen share state. */
  state_share_set_prev(pstate, pstate_data);

  V_w(&pentry->lock);

  return status;
}

/* This is called when share is downgraded via open_downgrade op. */
state_status_t state_share_downgrade(cache_entry_t         * pentry,
                                     fsal_op_context_t     * pcontext,
                                     state_data_t          * pstate_data, /* new share bits */
                                     state_owner_t         * powner,
                                     state_t               * pstate,      /* state that holds current share bits */
                                     cache_inode_client_t  * pclient,
                                     state_status_t        * pstatus)
{
  state_status_t status = STATE_SUCCESS;
  unsigned int            old_pentry_share_access = 0;
  unsigned int            old_pentry_share_deny = 0;
  unsigned int            new_pentry_share_access = 0;
  unsigned int            new_pentry_share_deny = 0;
  unsigned int            old_share_access = 0;
  unsigned int            old_share_deny = 0;
  unsigned int            new_share_access = 0;
  unsigned int            new_share_deny = 0;
  fsal_share_param_t      share_param;

  P_w(&pentry->lock);

  /* Get the current union of share states of this file. */
  old_pentry_share_access = state_share_get_share_access(pentry);
  old_pentry_share_deny = state_share_get_share_deny(pentry);

  /* Old share state. */
  old_share_access = pstate->state_data.share.share_access;
  old_share_deny = pstate->state_data.share.share_deny;

  /* New share state. */
  new_share_access = pstate_data->share.share_access;
  new_share_deny = pstate_data->share.share_deny;

  /* Update the ref counted share state of this file. */
  state_share_update_counter(pentry,
                             old_share_access,
                             old_share_deny,
                             new_share_access,
                             new_share_deny);

  /* Get the updated union of share states of this file. */
  new_pentry_share_access = state_share_get_share_access(pentry);
  new_pentry_share_deny = state_share_get_share_deny(pentry);

  /* If this file's share bits are different from the supposed value, update
   * it.
   */
  if((new_pentry_share_access != old_pentry_share_access) ||
     (new_pentry_share_deny != old_pentry_share_deny))
    {
      /* Try to push to FSAL. */
      share_param.share_access = new_pentry_share_access;
      share_param.share_deny = new_pentry_share_deny;

      status = do_share_op(pentry, pcontext, powner, &share_param, pclient);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     new_share_access,
                                     new_share_deny,
                                     old_share_access,
                                     old_share_deny);
          V_w(&pentry->lock);
          LogDebug(COMPONENT_STATE, "do_share_op failed");
          *pstatus = status;
          return *pstatus;
        }
    }

  /* Update share state. */
  pstate->state_data.share.share_access = new_share_access;
  pstate->state_data.share.share_deny   = new_share_deny;
  LogFullDebug(COMPONENT_STATE, "pstate %p: downgraded share_access %u, "
               "share_deny %u",
               pstate,
               pstate->state_data.share.share_access,
               pstate->state_data.share.share_deny);

  V_w(&pentry->lock);

  return status;
}

/* Update the bitmap of previously seen share access and deny bits for the
 * given state.
 */
state_status_t state_share_set_prev(state_t      * pstate,
                                    state_data_t * pstate_data)
{
  state_status_t status = STATE_SUCCESS;

  pstate->state_data.share.share_access_prev |=
    (1 << pstate_data->share.share_access);

  pstate->state_data.share.share_deny_prev |=
    (1 << pstate_data->share.share_deny);

  return status;
}

/* Check if the given state has seen the given share access and deny bits
 * before. This is needed when we check validity of open downgrade.
 */
state_status_t state_share_check_prev(state_t      * pstate,
                                    state_data_t * pstate_data)
{
  state_status_t status = STATE_SUCCESS;

  if((pstate->state_data.share.share_access_prev &
     (1 << pstate_data->share.share_access)) == 0)
    return STATE_STATE_ERROR;

  if((pstate->state_data.share.share_deny_prev &
     (1 << pstate_data->share.share_deny)) == 0)
    return STATE_STATE_ERROR;

  return status;
}

/* Check if the given share access and deny bits have conflict. */
state_status_t state_share_check_conflict_sw(cache_entry_t  * pentry,
                                             state_data_t   * pstate_data,
                                             state_status_t * pstatus,
                                             int use_mutex)
{
  state_status_t status = STATE_SUCCESS;
  char * cause = "";

  if(use_mutex)
    P_r(&pentry->lock);

  if((pstate_data->share.share_access & OPEN4_SHARE_ACCESS_READ) != 0 &&
     pentry->object.file.share_state.share_deny_read > 0)
    {
      cause = "access read denied by existing deny read";
      goto out_conflict;
    }

  if((pstate_data->share.share_access & OPEN4_SHARE_ACCESS_WRITE) != 0 &&
     pentry->object.file.share_state.share_deny_write > 0)
    {
      cause = "access write denied by existing deny write";
      goto out_conflict;
    }

  if((pstate_data->share.share_deny & OPEN4_SHARE_ACCESS_READ) != 0 &&
     pentry->object.file.share_state.share_access_read > 0)
    {
      cause = "deny read denied by existing access read";
      goto out_conflict;
    }

  if((pstate_data->share.share_deny & OPEN4_SHARE_ACCESS_WRITE) != 0 &&
     pentry->object.file.share_state.share_access_write > 0)
    {
      cause = "deny write denied by existing access write";
      goto out_conflict;
    }

  if(use_mutex)
    V_r(&pentry->lock);

  return status;

out_conflict:
  if(use_mutex)
    V_r(&pentry->lock);
  LogDebug(COMPONENT_STATE, "Share conflict detected: %s", cause);
  *pstatus = STATE_STATE_CONFLICT;
  return *pstatus;
}

state_status_t state_share_check_conflict_no_mutex(cache_entry_t  * pentry,
                                                   state_data_t   * pstate_data,
                                                   state_status_t * pstatus)
{
  return state_share_check_conflict_sw(pentry, pstate_data, pstatus, FALSE);
}

state_status_t state_share_check_conflict(cache_entry_t  * pentry,
                                          state_data_t   * pstate_data,
                                          state_status_t * pstatus)
{
  return state_share_check_conflict_sw(pentry, pstate_data, pstatus, TRUE);
}

/* Update the ref counter of share state. This function should be called with
 * cache_entry write lock.
 */
static void state_share_update_counter(cache_entry_t * pentry,
                                       int old_access,
                                       int old_deny,
                                       int new_access,
                                       int new_deny)
{
  int access_read_inc  = ((new_access & OPEN4_SHARE_ACCESS_READ) != 0) - ((old_access & OPEN4_SHARE_ACCESS_READ) != 0);
  int access_write_inc = ((new_access & OPEN4_SHARE_ACCESS_WRITE) != 0) - ((old_access & OPEN4_SHARE_ACCESS_WRITE) != 0);
  int deny_read_inc    = ((new_deny   & OPEN4_SHARE_ACCESS_READ) != 0) - ((old_deny   & OPEN4_SHARE_ACCESS_READ) != 0);
  int deny_write_inc   = ((new_deny   & OPEN4_SHARE_ACCESS_WRITE) != 0) - ((old_deny   & OPEN4_SHARE_ACCESS_WRITE) != 0);

  pentry->object.file.share_state.share_access_read  += access_read_inc;
  pentry->object.file.share_state.share_access_write += access_write_inc;
  pentry->object.file.share_state.share_deny_read    += deny_read_inc;
  pentry->object.file.share_state.share_deny_write   += deny_write_inc;

  LogFullDebug(COMPONENT_STATE, "pentry %p: share counter: "
               "access_read %u, access_write %u, "
               "deny_read %u, deny_write %u",
               pentry,
               pentry->object.file.share_state.share_access_read,
               pentry->object.file.share_state.share_access_write,
               pentry->object.file.share_state.share_deny_read,
               pentry->object.file.share_state.share_deny_write);
}

/* Utility function to calculate the union of share access of given file. */
static unsigned int state_share_get_share_access(cache_entry_t * pentry)
{
  unsigned int share_access = 0;

  if(pentry->object.file.share_state.share_access_read > 0)
    share_access |= OPEN4_SHARE_ACCESS_READ;

  if(pentry->object.file.share_state.share_access_write > 0)
    share_access |= OPEN4_SHARE_ACCESS_WRITE;

  LogFullDebug(COMPONENT_STATE, "pentry %p: union share access = %u",
               pentry, share_access);

  return share_access;
}

/* Utility function to calculate the union of share deny of given file. */
static unsigned int state_share_get_share_deny(cache_entry_t * pentry)
{
  unsigned int share_deny = 0;

  if(pentry->object.file.share_state.share_deny_read > 0)
    share_deny |= OPEN4_SHARE_DENY_READ;

  if(pentry->object.file.share_state.share_deny_write > 0)
    share_deny |= OPEN4_SHARE_DENY_WRITE;

  LogFullDebug(COMPONENT_STATE, "pentry %p: union share deny = %u",
               pentry, share_deny);

  return share_deny;
}
