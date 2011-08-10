/*
 *
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
 * \file    sal_functions.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:15 $
 * \version $Revision: 1.95 $
 * \brief   Management of the cached inode layer. 
 *
 * sal_functions.h : Management of the state abstraction layer
 *
 *
 */

#ifndef _SAL_FUNCTIONS_H
#define _SAL_FUNCTIONS_H

#include "sal_data.h"
#include "cache_inode.h"


const char *state_err_str(state_status_t err);

state_status_t state_error_convert(fsal_status_t fsal_status);

#ifdef _USE_NLM
state_status_t state_lock_init(state_status_t   * pstatus,
                               hash_parameter_t   cookie_param);
#else
state_status_t state_lock_init(state_status_t * pstatus);
#endif

#ifdef _USE_NLM
/**
 *
 * state_add_grant_cookie: Add a grant cookie to a blocked lock that is
 *                               pending grant.
 *
 * This will attach the cookie to the lock so it can be found later.
 * It will also acquire the lock from the FSAL (which may not be possible).
 *
 * Returns:
 *
 * CACHE_INODE_SUCCESS       - Everything ok
 * CACHE_INODE_LOCK_CONFLICT - FSAL was unable to acquire lock, would have to block
 * CACHE_INODE_LOCK_BLOCKED  - FSAL is handling a block on the lock (TODO FSF: not implemented yet...)
 * other errors are possible from FSAL...
 */
state_status_t state_add_grant_cookie(cache_entry_t         * pentry,
                                      fsal_op_context_t     * pcontext,
                                      void                  * pcookie,
                                      int                     cookie_size,
                                      state_lock_entry_t    * lock_entry,
                                      state_cookie_entry_t ** ppcookie_entry,
                                      cache_inode_client_t  * pclient,
                                      state_status_t        * pstatus);

state_status_t state_find_grant(void                  * pcookie,
                                int                     cookie_size,
                                state_cookie_entry_t ** ppcookie_entry,
                                state_status_t        * pstatus);

void state_complete_grant(fsal_op_context_t    * pcontext,
                          state_cookie_entry_t * cookie_entry,
                          cache_inode_client_t * pclient);

/**
 *
 * state_cancel_grant: Cancel a blocked lock grant
 *
 * This function is to be called from the granted_callback_t function.
 */
state_status_t state_cancel_grant(fsal_op_context_t    * pcontext,
                                  state_cookie_entry_t * cookie_entry,
                                  cache_inode_client_t * pclient,
                                  state_status_t       * pstatus);

state_status_t state_release_grant(fsal_op_context_t    * pcontext,
                                   state_cookie_entry_t * cookie_entry,
                                   cache_inode_client_t * pclient,
                                   state_status_t       * pstatus);
#endif

/* Call this to release the lock owner reference resulting from a conflicting
 * lock holder being returned.
 */
 
void release_lock_owner_s(state_lock_owner_t *powner);

state_status_t state_test(cache_entry_t        * pentry,
                          fsal_op_context_t    * pcontext,
                          state_lock_owner_t   * powner,
                          state_lock_desc_t    * plock,
                          state_lock_owner_t  ** holder,   /* owner that holds conflicting lock */
                          state_lock_desc_t    * conflict, /* description of conflicting lock */
                          cache_inode_client_t * pclient,
                          state_status_t       * pstatus);

state_status_t state_lock(cache_entry_t         * pentry,
                          fsal_op_context_t     * pcontext,
                          state_lock_owner_t    * powner,
                          state_blocking_t        blocking,
                          state_block_data_t    * block_data,
                          state_lock_desc_t     * plock,
                          state_lock_owner_t   ** holder,   /* owner that holds conflicting lock */
                          state_lock_desc_t     * conflict, /* description of conflicting lock */
                          cache_inode_client_t  * pclient,
                          state_status_t        * pstatus);

state_status_t state_unlock(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            state_lock_owner_t   * powner,
                            state_lock_desc_t    * plock,
                            cache_inode_client_t * pclient,
                            state_status_t       * pstatus);

state_status_t state_cancel(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            state_lock_owner_t   * powner,
                            state_lock_desc_t    * plock,
                            cache_inode_client_t * pclient,
                            state_status_t       * pstatus);

#ifdef _USE_NLM
state_status_t state_nlm_notify(fsal_op_context_t    * pcontext,
                                state_nlm_client_t   * pnlmclient,
                                cache_inode_client_t * pclient,
                                state_status_t       * pstatus);
#endif

state_status_t state_owner_unlock_all(fsal_op_context_t    * pcontext,
                                      state_lock_owner_t   * powner,
                                      cache_inode_client_t * pclient,
                                      state_status_t       * pstatus);

int state_state_conflict(state_t      * pstate,
                         state_type_t   state_type,
                         state_data_t * pstate_data);

state_status_t state_add_state(cache_entry_t         * pentry,
                               state_type_t            state_type,
                               state_data_t          * pstate_data,
                               state_open_owner_t    * powner_input,
                               cache_inode_client_t  * pclient,
                               fsal_op_context_t     * pcontext,
                               state_t              ** ppstate,
                               state_status_t        * pstatus);

state_status_t state_get_state(char                    other[12],
                               state_t              ** ppstate,
                               cache_inode_client_t  * pclient,
                               state_status_t        * pstatus);

state_status_t state_set_state(state_t              * pstate,
                               cache_inode_client_t * pclient,
                               state_status_t       * pstatus);

state_status_t state_update_state(state_t              * pstate,
                                  cache_inode_client_t * pclient,
                                  state_status_t       * pstatus);

state_status_t state_del_state(state_t              * pstate,
                               cache_inode_client_t * pclient,
                               state_status_t       * pstatus);

state_status_t state_find_state_by_owner(cache_entry_t         * pentry,
                                         open_owner4           * powner,
                                         state_t              ** ppstate,
                                         state_t               * previous_pstate,
                                         cache_inode_client_t  * pclient,
                                         fsal_op_context_t     * pcontext,
                                         state_status_t        * pstatus);

state_status_t state_state_iterate(cache_entry_t        * pentry,
                                   state_t              * *ppstate,
                                   state_t              * previous_pstate,
                                   cache_inode_client_t * pclient,
                                   fsal_op_context_t    * pcontext,
                                   state_status_t       * pstatus);

state_status_t state_del_state_by_key(char                   other[12],
                                      cache_inode_client_t * pclient,
                                      state_status_t       * pstatus);

#endif                          /*  _SAL_FUNCTIONS_H */
