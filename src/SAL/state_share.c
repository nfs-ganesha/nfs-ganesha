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

#include "fsal.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#ifdef _USE_NLM
#include "nlm_util.h"
#endif
#include "cache_inode_lru.h"

/* Update the ref counter of share state of given file. */
static void state_share_update_counter(cache_entry_t * pentry,
                                       int old_access,
                                       int old_deny,
                                       int new_access,
                                       int new_deny,
                                       bool_t v4);

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
                                  fsal_share_param_t   * pshare)
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

/* This is called when new share state is added. The state lock MUST
   be held. */
state_status_t state_share_add(cache_entry_t         * pentry,
                               fsal_op_context_t     * pcontext,
                               state_owner_t         * powner,
                               state_t               * pstate,  /* state that holds share bits to be added */
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

  /* Check if new share state has conflicts. */
  status = state_share_check_conflict(pentry,
                                      pstate->state_data.share.share_access,
                                      pstate->state_data.share.share_deny,
                                      pstatus);
  if(status != STATE_SUCCESS)
    {
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
                             OPEN4_SHARE_ACCESS_NONE,
                             OPEN4_SHARE_DENY_NONE,
                             new_share_access,
                             new_share_deny,
                             TRUE);

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

      status = do_share_op(pentry, pcontext, powner, &share_param);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     new_share_access,
                                     new_share_deny,
                                     OPEN4_SHARE_ACCESS_NONE,
                                     OPEN4_SHARE_DENY_NONE,
                                     TRUE);
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

  return status;
}

/* This is called when a share state is removed.  The state lock MUST
   be held. */
state_status_t state_share_remove(cache_entry_t         * pentry,
                                  fsal_op_context_t     * pcontext,
                                  state_owner_t         * powner,
                                  state_t               * pstate,  /* state that holds share bits to be removed */
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
                             OPEN4_SHARE_ACCESS_NONE,
                             OPEN4_SHARE_DENY_NONE,
                             TRUE);

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

      status = do_share_op(pentry, pcontext, powner, &share_param);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     OPEN4_SHARE_ACCESS_NONE,
                                     OPEN4_SHARE_DENY_NONE,
                                     removed_share_access,
                                     removed_share_deny,
                                     TRUE);
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

  return status;
}

/* This is called when share state is upgraded during open.  The
   state ock MUST be held. */
state_status_t state_share_upgrade(cache_entry_t         * pentry,
                                   fsal_op_context_t     * pcontext,
                                   state_data_t          * pstate_data, /* new share bits */
                                   state_owner_t         * powner,
                                   state_t               * pstate,      /* state that holds current share bits */
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

  /* Check if new share state has conflicts. */
  status = state_share_check_conflict(pentry,
                                      pstate_data->share.share_access,
                                      pstate_data->share.share_deny,
                                      pstatus);
  if(status != STATE_SUCCESS)
    {
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
                             new_share_deny,
                             TRUE);

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

      status = do_share_op(pentry, pcontext, powner, &share_param);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     new_share_access,
                                     new_share_deny,
                                     old_share_access,
                                     old_share_deny,
                                     TRUE);
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

  return status;
}

/* This is called when share is downgraded via open_downgrade op.
   The state lock MUST be held. */
state_status_t state_share_downgrade(cache_entry_t         * pentry,
                                     fsal_op_context_t     * pcontext,
                                     state_data_t          * pstate_data, /* new share bits */
                                     state_owner_t         * powner,
                                     state_t               * pstate,      /* state that holds current share bits */
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
                             new_share_deny,
                             TRUE);

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

      status = do_share_op(pentry, pcontext, powner, &share_param);
      if(status != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     new_share_access,
                                     new_share_deny,
                                     old_share_access,
                                     old_share_deny,
                                     TRUE);
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

/* Check if the given share access and deny bits have conflict.  The
   state lock MUST be held. */
state_status_t state_share_check_conflict(cache_entry_t  * pentry,
                                          int              share_acccess,
                                          int              share_deny,
                                          state_status_t * pstatus)
{
  char * cause = "";

  if((share_acccess & OPEN4_SHARE_ACCESS_READ) != 0 &&
     pentry->object.file.share_state.share_deny_read > 0)
    {
      cause = "access read denied by existing deny read";
      goto out_conflict;
    }

  if((share_acccess & OPEN4_SHARE_ACCESS_WRITE) != 0 &&
     pentry->object.file.share_state.share_deny_write > 0)
    {
      cause = "access write denied by existing deny write";
      goto out_conflict;
    }

  if((share_deny & OPEN4_SHARE_DENY_READ) != 0 &&
     pentry->object.file.share_state.share_access_read > 0)
    {
      cause = "deny read denied by existing access read";
      goto out_conflict;
    }

  if((share_deny & OPEN4_SHARE_DENY_WRITE) != 0 &&
     pentry->object.file.share_state.share_access_write > 0)
    {
      cause = "deny write denied by existing access write";
      goto out_conflict;
    }

  *pstatus = STATE_SUCCESS;
  return *pstatus;

out_conflict:

  LogDebug(COMPONENT_STATE, "Share conflict detected: %s", cause);
  *pstatus = STATE_STATE_CONFLICT;
  return *pstatus;
}

/* Update the ref counter of share state. This function should be called with
 * the state lock held
 */
static void state_share_update_counter(cache_entry_t * pentry,
                                       int old_access,
                                       int old_deny,
                                       int new_access,
                                       int new_deny,
                                       bool_t v4)
{
  int access_read_inc  = ((new_access & OPEN4_SHARE_ACCESS_READ) != 0) - ((old_access & OPEN4_SHARE_ACCESS_READ) != 0);
  int access_write_inc = ((new_access & OPEN4_SHARE_ACCESS_WRITE) != 0) - ((old_access & OPEN4_SHARE_ACCESS_WRITE) != 0);
  int deny_read_inc    = ((new_deny   & OPEN4_SHARE_ACCESS_READ) != 0) - ((old_deny   & OPEN4_SHARE_ACCESS_READ) != 0);
  int deny_write_inc   = ((new_deny   & OPEN4_SHARE_ACCESS_WRITE) != 0) - ((old_deny   & OPEN4_SHARE_ACCESS_WRITE) != 0);

  pentry->object.file.share_state.share_access_read  += access_read_inc;
  pentry->object.file.share_state.share_access_write += access_write_inc;
  pentry->object.file.share_state.share_deny_read    += deny_read_inc;
  pentry->object.file.share_state.share_deny_write   += deny_write_inc;
  if(v4)
    pentry->object.file.share_state.share_deny_write_v4 += deny_write_inc;

  LogFullDebug(COMPONENT_STATE, "pentry %p: share counter: "
               "access_read %u, access_write %u, "
               "deny_read %u, deny_write %u, deny_write_v4 %u",
               pentry,
               pentry->object.file.share_state.share_access_read,
               pentry->object.file.share_state.share_access_write,
               pentry->object.file.share_state.share_deny_read,
               pentry->object.file.share_state.share_deny_write,
               pentry->object.file.share_state.share_deny_write_v4);
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

state_status_t state_share_anonymous_io_start(cache_entry_t  * pentry,
                                              int              share_access,
                                              state_status_t * pstatus)
{
  pthread_rwlock_wrlock(&pentry->state_lock);

  if(state_share_check_conflict(pentry,
                                share_access,
                                0,
                                pstatus) == STATE_SUCCESS)
    {
      /* Temporarily bump the access counters, v4 mode doesn't matter
       * since there is no deny mode associated with anonymous I/O.
       */
      state_share_update_counter(pentry,
                                 OPEN4_SHARE_ACCESS_NONE,
                                 OPEN4_SHARE_DENY_NONE,
                                 share_access,
                                 OPEN4_SHARE_DENY_NONE,
                                 FALSE);
    }

  pthread_rwlock_unlock(&pentry->state_lock);

  return *pstatus;
}

void state_share_anonymous_io_done(cache_entry_t  * pentry,
                                   int              share_access)
{
  pthread_rwlock_wrlock(&pentry->state_lock);

  /* Undo the temporary bump to the access counters, v4 mode doesn't
   * matter since there is no deny mode associated with anonymous I/O.
   */
  state_share_update_counter(pentry,
                             share_access,
                             OPEN4_SHARE_DENY_NONE,
                             OPEN4_SHARE_ACCESS_NONE,
                             OPEN4_SHARE_DENY_NONE,
                             FALSE);

  pthread_rwlock_unlock(&pentry->state_lock);
}

#ifdef _USE_NLM
state_status_t state_nlm_share(cache_entry_t        * pentry,
                               fsal_op_context_t    * pcontext,
                               exportlist_t         * pexport,
                               int                    share_access,
                               int                    share_deny,
                               state_owner_t        * powner,
                               state_status_t       * pstatus)
{
  unsigned int           old_pentry_share_access;
  unsigned int           old_pentry_share_deny;
  unsigned int           new_pentry_share_access;
  unsigned int           new_pentry_share_deny;
  fsal_share_param_t     share_param;
  state_nlm_share_t    * nlm_share;
  cache_inode_status_t   cache_status;

  cache_status = cache_inode_inc_pin_ref(pentry);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogDebug(COMPONENT_STATE,
               "Could not pin file");
      return *pstatus;
    }

  if(cache_inode_open(pentry,
                      FSAL_O_RDWR,
                      pcontext,
                      0,
                      &cache_status) != CACHE_INODE_SUCCESS)
    {
      cache_inode_dec_pin_ref(pentry, TRUE);

      *pstatus = cache_inode_status_to_state_status(cache_status);

      LogFullDebug(COMPONENT_STATE,
                   "Could not open file");

      return *pstatus;
    }

  pthread_rwlock_wrlock(&pentry->state_lock);

  /* Check if new share state has conflicts. */
  if(state_share_check_conflict(pentry,
                                share_access,
                                share_deny,
                                pstatus) != STATE_SUCCESS)
    {
      pthread_rwlock_unlock(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, TRUE);

      LogEvent(COMPONENT_STATE, "Share conflicts detected during add");

      return *pstatus;
    }

  /* Create a new NLM Share object */
  nlm_share = gsh_calloc(1, sizeof(state_nlm_share_t));

  if(nlm_share == NULL)
    {
      pthread_rwlock_unlock(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, TRUE);

      LogEvent(COMPONENT_STATE, "Can not allocate memory for share");

      *pstatus = STATE_MALLOC_ERROR;

      return *pstatus;
    }

  nlm_share->sns_powner  = powner;
  nlm_share->sns_pentry  = pentry;
  nlm_share->sns_access  = share_access;
  nlm_share->sns_deny    = share_deny;
  nlm_share->sns_pexport = pexport;

  /* Add share to list for NLM Owner */
  inc_state_owner_ref(powner);

  P(powner->so_mutex);

  glist_add_tail(&powner->so_owner.so_nlm_owner.so_nlm_shares, &nlm_share->sns_share_per_owner);

  V(powner->so_mutex);

  /* Add share to list for NSM Client */
  inc_nsm_client_ref(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client);

  P(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

  glist_add_tail(&powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_share_list,
                 &nlm_share->sns_share_per_client);

  V(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

  /* Add share to list for file, if list was empty take a pin ref to keep this
   * file pinned in the inode cache.
   */
  if(glist_empty(&pentry->object.file.nlm_share_list))
    cache_inode_inc_pin_ref(pentry);

  glist_add_tail(&pentry->object.file.nlm_share_list, &nlm_share->sns_share_per_file);

  /* Get the current union of share states of this file. */
  old_pentry_share_access = state_share_get_share_access(pentry);
  old_pentry_share_deny   = state_share_get_share_deny(pentry);

  /* Update the ref counted share state of this file. */
  state_share_update_counter(pentry,
                             OPEN4_SHARE_ACCESS_NONE,
                             OPEN4_SHARE_DENY_NONE,
                             share_access,
                             share_deny,
                             TRUE);

  /* Get the updated union of share states of this file. */
  new_pentry_share_access = state_share_get_share_access(pentry);
  new_pentry_share_deny   = state_share_get_share_deny(pentry);

  /* If this file's share bits are different from the supposed value, update
   * it.
   */
  if((new_pentry_share_access != old_pentry_share_access) ||
     (new_pentry_share_deny   != old_pentry_share_deny))
    {
      /* Try to push to FSAL. */
      share_param.share_access = new_pentry_share_access;
      share_param.share_deny   = new_pentry_share_deny;

      *pstatus = do_share_op(pentry, pcontext, powner, &share_param);

      if(*pstatus != STATE_SUCCESS)
        {
          /* Revert the ref counted share state of this file. */
          state_share_update_counter(pentry,
                                     share_access,
                                     share_deny,
                                     OPEN4_SHARE_ACCESS_NONE,
                                     OPEN4_SHARE_DENY_NONE,
                                     TRUE);

          /* Remove the share from the list for the file. If the list is now
           * empty also remove the extra pin ref.
           */
          glist_del(&nlm_share->sns_share_per_file);

          if(glist_empty(&pentry->object.file.nlm_share_list))
            cache_inode_dec_pin_ref(pentry, TRUE);

          /* Remove the share from the NSM Client list */
          P(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

          glist_del(&nlm_share->sns_share_per_client);

          V(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

          dec_nsm_client_ref(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client);

          /* Remove the share from the NLM Owner list */
          P(powner->so_mutex);

          glist_del(&nlm_share->sns_share_per_owner);

          V(powner->so_mutex);

          dec_state_owner_ref(powner);

          /* Free the NLM Share and exit */
          gsh_free(nlm_share);

          pthread_rwlock_unlock(&pentry->state_lock);

          cache_inode_dec_pin_ref(pentry, TRUE);

          LogDebug(COMPONENT_STATE, "do_share_op failed");

          return *pstatus;
        }
    }

  LogFullDebug(COMPONENT_STATE, "added share_access %u, "
               "share_deny %u",
               share_access, share_deny);

  pthread_rwlock_unlock(&pentry->state_lock);

  cache_inode_dec_pin_ref(pentry, TRUE);

  return *pstatus;
}

state_status_t state_nlm_unshare(cache_entry_t        * pentry,
                                 fsal_op_context_t    * pcontext,
                                 int                    share_access,
                                 int                    share_deny,
                                 state_owner_t        * powner,
                                 state_status_t       * pstatus)
{
  struct glist_head      *glist, *glistn;
  unsigned int           old_pentry_share_access;
  unsigned int           old_pentry_share_deny;
  unsigned int           new_pentry_share_access;
  unsigned int           new_pentry_share_deny;
  unsigned int           removed_share_access;
  unsigned int           removed_share_deny;
  fsal_share_param_t     share_param;
  state_nlm_share_t    * nlm_share;
  cache_inode_status_t   cache_status;

  cache_status = cache_inode_inc_pin_ref(pentry);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogDebug(COMPONENT_STATE,
               "Could not pin file");
      return *pstatus;
    }

  pthread_rwlock_wrlock(&pentry->state_lock);

  glist_for_each_safe(glist, glistn, &pentry->object.file.nlm_share_list)
    {
      nlm_share = glist_entry(glist, state_nlm_share_t, sns_share_per_file);

      if(different_owners(powner, nlm_share->sns_powner))
        continue;

      /* share_access == OPEN4_SHARE_ACCESS_NONE indicates that any share
       * should be matched for unshare.
       */
      if(share_access != OPEN4_SHARE_ACCESS_NONE &&
         (nlm_share->sns_access != share_access ||
          nlm_share->sns_deny   != share_deny))
        continue;

      /* Get the current union of share states of this file. */
      old_pentry_share_access = state_share_get_share_access(pentry);
      old_pentry_share_deny   = state_share_get_share_deny(pentry);

      /* Share state to be removed. */
      removed_share_access = nlm_share->sns_access;
      removed_share_deny   = nlm_share->sns_deny;

      /* Update the ref counted share state of this file. */
      state_share_update_counter(pentry,
                                 removed_share_access,
                                 removed_share_deny,
                                 OPEN4_SHARE_ACCESS_NONE,
                                 OPEN4_SHARE_DENY_NONE,
                                 TRUE);

      /* Get the updated union of share states of this file. */
      new_pentry_share_access = state_share_get_share_access(pentry);
      new_pentry_share_deny   = state_share_get_share_deny(pentry);

      /* If this file's share bits are different from the supposed value, update
       * it.
       */
      if((new_pentry_share_access != old_pentry_share_access) ||
         (new_pentry_share_deny   != old_pentry_share_deny))
        {
          /* Try to push to FSAL. */
          share_param.share_access = new_pentry_share_access;
          share_param.share_deny   = new_pentry_share_deny;

          *pstatus = do_share_op(pentry, pcontext, powner, &share_param);

          if(*pstatus != STATE_SUCCESS)
            {
              /* Revert the ref counted share state of this file. */
              state_share_update_counter(pentry,
                                         OPEN4_SHARE_ACCESS_NONE,
                                         OPEN4_SHARE_DENY_NONE,
                                         removed_share_access,
                                         removed_share_deny,
                                         TRUE);

              pthread_rwlock_unlock(&pentry->state_lock);

              cache_inode_dec_pin_ref(pentry, TRUE);

              LogDebug(COMPONENT_STATE, "do_share_op failed");

              return *pstatus;
            }
        }

      LogFullDebug(COMPONENT_STATE,
                   "removed share_access %u, share_deny %u",
                   removed_share_access,
                   removed_share_deny);

      /* Remove the share from the list for the file. If the list is now
       * empty also remove the extra pin ref.
       */
      glist_del(&nlm_share->sns_share_per_file);

      if(glist_empty(&pentry->object.file.nlm_share_list))
        cache_inode_dec_pin_ref(pentry, TRUE);

      /* Remove the share from the NSM Client list */
      P(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

      glist_del(&nlm_share->sns_share_per_client);

      V(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

      dec_nsm_client_ref(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client);

      /* Remove the share from the NLM Owner list */
      P(powner->so_mutex);

      glist_del(&nlm_share->sns_share_per_owner);

      V(powner->so_mutex);

      dec_state_owner_ref(powner);

      /* Free the NLM Share (and continue to look for more) */
      gsh_free(nlm_share);
    }

  pthread_rwlock_unlock(&pentry->state_lock);

  cache_inode_dec_pin_ref(pentry, TRUE);

  return *pstatus;
}

void state_share_wipe(cache_entry_t * pentry)
{
  state_nlm_share_t * nlm_share;
  struct glist_head * glist;
  struct glist_head * glistn;
  state_owner_t     * powner;

  glist_for_each_safe(glist, glistn, &pentry->object.file.nlm_share_list)
    {
      nlm_share = glist_entry(glist, state_nlm_share_t, sns_share_per_file);

      powner = nlm_share->sns_powner;

      /* Remove the share from the list for the file. If the list is now
       * empty also remove the extra pin ref.
       */
      glist_del(&nlm_share->sns_share_per_file);

      if(glist_empty(&pentry->object.file.nlm_share_list))
        cache_inode_dec_pin_ref(pentry, FALSE);

      /* Remove the share from the NSM Client list */
      P(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

      glist_del(&nlm_share->sns_share_per_client);

      V(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

      dec_nsm_client_ref(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client);

      /* Remove the share from the NLM Owner list */
      P(powner->so_mutex);

      glist_del(&nlm_share->sns_share_per_owner);

      V(powner->so_mutex);

      dec_state_owner_ref(powner);

      /* Free the NLM Share (and continue to look for more) */
      gsh_free(nlm_share);
    }
}

#endif /* _USE_NLM */
