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
 * \brief   Management of the state abstraction layer. 
 *
 * sal_functions.h : Management of the state abstraction layer
 *
 *
 */

#ifndef _SAL_FUNCTIONS_H
#define _SAL_FUNCTIONS_H

#include "sal_data.h"
#include "cache_inode.h"
#include "nfs_exports.h"

/******************************************************************************
 *
 * Misc functions
 *
 ******************************************************************************/

const char *state_err_str(state_status_t err);

state_status_t state_error_convert(fsal_status_t fsal_status);

state_status_t cache_inode_status_to_state_status(cache_inode_status_t status);

nfsstat4 nfs4_Errno_state(state_status_t error);
nfsstat3 nfs3_Errno_state(state_status_t error);
nfsstat2 nfs2_Errno_state(state_status_t error);

const char * state_owner_type_to_str(state_owner_type_t type);
int different_owners(state_owner_t *powner1, state_owner_t *powner2);
int DisplayOwner(state_owner_t *powner, char *buf);
void Hash_inc_state_owner_ref(hash_buffer_t *buffval);
int Hash_dec_state_owner_ref(hash_buffer_t *buffval);
void inc_state_owner_ref_locked(state_owner_t *powner);
void inc_state_owner_ref(state_owner_t *powner);

void dec_state_owner_ref_locked(state_owner_t        * powner,
                                cache_inode_client_t * pclient);

void dec_state_owner_ref(state_owner_t        * powner,
                         cache_inode_client_t * pclient);

/******************************************************************************
 *
 * NLM State functions
 *
 ******************************************************************************/

#ifdef _USE_NLM
void inc_nsm_client_ref_locked(state_nsm_client_t * pclient);
void inc_nsm_client_ref(state_nsm_client_t * pclient);
void dec_nsm_client_ref_locked(state_nsm_client_t * pclient);
void dec_nsm_client_ref(state_nsm_client_t * pclient);
int display_nsm_client(state_nsm_client_t * pkey, char * str);
int display_nsm_client_val(hash_buffer_t * pbuff, char * str);
int display_nsm_client_key(hash_buffer_t * pbuff, char * str);

int compare_nsm_client(state_nsm_client_t * pclient1,
                       state_nsm_client_t * pclient2);

int compare_nsm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

unsigned long nsm_client_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t    * buffclef);

unsigned long nsm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t    * buffclef);

state_nsm_client_t *get_nsm_client(care_t       care,
                                   SVCXPRT    * xprt,
                                   const char * caller_name);
void nsm_client_PrintAll(void);

void inc_nlm_client_ref_locked(state_nlm_client_t * pclient);
void inc_nlm_client_ref(state_nlm_client_t * pclient);
void dec_nlm_client_ref_locked(state_nlm_client_t * pclient);
void dec_nlm_client_ref(state_nlm_client_t * pclient);
int display_nlm_client(state_nlm_client_t * pkey, char * str);
int display_nlm_client_val(hash_buffer_t * pbuff, char * str);
int display_nlm_client_key(hash_buffer_t * pbuff, char * str);

int compare_nlm_client(state_nlm_client_t * pclient1,
                       state_nlm_client_t * pclient2);

int compare_nlm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

unsigned long nlm_client_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t    * buffclef);

unsigned long nlm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t    * buffclef);

state_nlm_client_t *get_nlm_client(care_t               care,
                                   SVCXPRT            * xprt,
                                   state_nsm_client_t * pnsm_client,
                                   const char         * caller_name);
void nlm_client_PrintAll(void);

void remove_nlm_owner(cache_inode_client_t * pclient,
                      state_owner_t        * powner,
                      const char           * str);

int display_nlm_owner(state_owner_t * pkey, char * str);
int display_nlm_owner_val(hash_buffer_t * pbuff, char * str);
int display_nlm_owner_key(hash_buffer_t * pbuff, char * str);

int compare_nlm_owner(state_owner_t * powner1,
                      state_owner_t * powner2);

int compare_nlm_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

unsigned long nlm_owner_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t    * buffclef);

unsigned long nlm_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t    * buffclef);

void make_nlm_special_owner(state_nsm_client_t * pnsm_client,
                            state_nlm_client_t * pnlm_client,
                            state_owner_t      * pnlm_owner);

state_owner_t *get_nlm_owner(care_t               care,
                             state_nlm_client_t * pclient, 
                             netobj             * oh,
                             uint32_t             svid);
void nlm_owner_PrintAll(void);

int Init_nlm_hash(void);
#endif


/******************************************************************************
 *
 * NFSv4 Stateid functions
 *
 ******************************************************************************/

int nfs4_BuildStateId_Other(cache_entry_t     * pentry,
                            fsal_op_context_t * pcontext,
                            state_owner_t     * popen_owner,
                            char              * other);

#define STATEID_NO_SPECIAL 0
#define STATEID_SPECIAL_SEQID_0    1
#define STATEID_SPECIAL_ALL_0      2
#define STATEID_SPECIAL_ALL_1      4
#define STATEID_SPECIAL_CURRENT    8
#define STATEID_SPECIAL_ANY        0xFF
#define STATEID_SPECIAL_FOR_LOCK   (STATEID_SPECIAL_SEQID_0 | STATEID_SPECIAL_CURRENT)

int nfs4_Check_Stateid(stateid4        * pstate,
                       cache_entry_t   * pentry,
                       clientid4         clientid,
                       state_t        ** ppstate,
                       compound_data_t * data,
                       char              flags,
                       const char      * tag);

void update_stateid(state_t         * pstate,
                    stateid4        * presp,
                    compound_data_t * data,
                    const char      * tag);

int nfs4_Init_state_id(nfs_state_id_parameter_t param);
int nfs4_State_Set(char other[OTHERSIZE], state_t * pstate_data);
int nfs4_State_Get_Pointer(char other[OTHERSIZE], state_t * *pstate_data);
int nfs4_State_Del(char other[OTHERSIZE]);
void nfs_State_PrintAll(void);

int nfs4_is_lease_expired(cache_entry_t * pentry);

int display_state_id_val(hash_buffer_t * pbuff, char *str);
int display_state_id_key(hash_buffer_t * pbuff, char *str);

/******************************************************************************
 *
 * NFSv4 Owner functions
 *
 ******************************************************************************/

void remove_nfs4_owner(cache_inode_client_t * pclient,
                       state_owner_t        * powner,
                       const char           * str);

int display_nfs4_owner(state_owner_t *powner, char *str);
int display_nfs4_owner_val(hash_buffer_t * pbuff, char *str);
int display_nfs4_owner_key(hash_buffer_t * pbuff, char *str);

int compare_nfs4_owner(state_owner_t * powner1,
                       state_owner_t * powner2);

int compare_nfs4_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

unsigned long nfs4_owner_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t    * buffclef);

unsigned long nfs4_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t    * buffclef);

void convert_nfs4_open_owner(open_owner4             * pnfsowoner,
                             state_nfs4_owner_name_t * pname_owner);

void convert_nfs4_lock_owner(lock_owner4             * pnfsowoner,
                             state_nfs4_owner_name_t * pname_owner);

void nfs4_owner_PrintAll(void);

int nfs4_owner_Get_Pointer(state_nfs4_owner_name_t  * pname,
                           state_owner_t           ** powner);

state_owner_t *create_nfs4_owner(cache_inode_client_t    * pclient,
                                 state_nfs4_owner_name_t * pname,
                                 state_owner_type_t        type,
                                 state_owner_t           * related_owner,
                                 unsigned int              init_seqid);

int Init_nfs4_owner(nfs4_owner_parameter_t param);

void Process_nfs4_conflict(LOCK4denied          * denied,    /* NFS v4 LOck4denied structure to fill in */
                           state_owner_t        * holder,    /* owner that holds conflicting lock */
                           state_lock_desc_t    * conflict,  /* description of conflicting lock */
                           cache_inode_client_t * pclient);

void Release_nfs4_denied(LOCK4denied * denied);
void Copy_nfs4_denied(LOCK4denied * denied_dst, LOCK4denied * denied_src);

void Copy_nfs4_state_req(state_owner_t   * powner,
                         seqid4            seqid,
                         nfs_argop4      * args,
                         compound_data_t * data,
                         nfs_resop4      * resp,
                         const char      * tag);

bool_t Check_nfs4_seqid(state_owner_t   * powner,
                        seqid4            seqid,
                        nfs_argop4      * args,
                        compound_data_t * data,
                        nfs_resop4      * resp,
                        const char      * tag);

/******************************************************************************
 *
 * Lock functions
 *
 ******************************************************************************/

#ifdef _USE_NLM
state_status_t state_lock_init(state_status_t   * pstatus,
                               hash_parameter_t   cookie_param);
#else
state_status_t state_lock_init(state_status_t * pstatus);
#endif

void LogLock(log_components_t     component,
             log_levels_t         debug,
             const char         * reason, 
             cache_entry_t      * pentry,
             fsal_op_context_t  * pcontext,
             state_owner_t      * powner,
             state_lock_desc_t  * plock);

#ifdef _USE_BLOCKING_LOCKS
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
                                cache_inode_client_t  * pclient,
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

state_status_t state_test(cache_entry_t        * pentry,
                          fsal_op_context_t    * pcontext,
                          state_owner_t        * powner,
                          state_lock_desc_t    * plock,
                          state_owner_t       ** holder,   /* owner that holds conflicting lock */
                          state_lock_desc_t    * conflict, /* description of conflicting lock */
                          cache_inode_client_t * pclient,
                          state_status_t       * pstatus);

state_status_t state_lock(cache_entry_t         * pentry,
                          fsal_op_context_t     * pcontext,
                          state_owner_t         * powner,
                          state_t               * pstate,
                          state_blocking_t        blocking,
                          state_block_data_t    * block_data,
                          state_lock_desc_t     * plock,
                          state_owner_t        ** holder,   /* owner that holds conflicting lock */
                          state_lock_desc_t     * conflict, /* description of conflicting lock */
                          cache_inode_client_t  * pclient,
                          state_status_t        * pstatus);

state_status_t state_unlock(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            state_owner_t        * powner,
                            state_t              * pstate,
                            state_lock_desc_t    * plock,
                            cache_inode_client_t * pclient,
                            state_status_t       * pstatus);

#ifdef _USE_BLOCKING_LOCKS
state_status_t state_cancel(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            state_owner_t        * powner,
                            state_lock_desc_t    * plock,
                            cache_inode_client_t * pclient,
                            state_status_t       * pstatus);
#endif

#ifdef _USE_NLM
state_status_t state_nlm_notify(fsal_op_context_t    * pcontext,
                                state_nsm_client_t   * pnsmclient,
                                state_t              * pstate,
                                cache_inode_client_t * pclient,
                                state_status_t       * pstatus);
#endif

state_status_t state_owner_unlock_all(fsal_op_context_t    * pcontext,
                                      state_owner_t        * powner,
                                      state_t              * pstate,
                                      cache_inode_client_t * pclient,
                                      state_status_t       * pstatus);

int state_conflict(state_t      * pstate,
                   state_type_t   state_type,
                   state_data_t * pstate_data);

state_status_t state_add(cache_entry_t         * pentry,
                         state_type_t            state_type,
                         state_data_t          * pstate_data,
                         state_owner_t         * powner_input,
                         cache_inode_client_t  * pclient,
                         fsal_op_context_t     * pcontext,
                         state_t              ** ppstate,
                         state_status_t        * pstatus);

state_status_t state_set(state_t              * pstate,
                         cache_inode_client_t * pclient,
                         state_status_t       * pstatus);

state_status_t state_del(state_t              * pstate,
                         cache_inode_client_t * pclient,
                         state_status_t       * pstatus);

int display_lock_cookie_key(hash_buffer_t * pbuff, char *str);
int display_lock_cookie_val(hash_buffer_t * pbuff, char *str);
int compare_lock_cookie_key(hash_buffer_t * buff1, hash_buffer_t * buff2);
unsigned long lock_cookie_value_hash_func(hash_parameter_t * p_hparam,
                                          hash_buffer_t * buffclef);
unsigned long lock_cookie_rbt_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef);

#endif                          /*  _SAL_FUNCTIONS_H */
