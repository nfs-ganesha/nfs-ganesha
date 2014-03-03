/*
 *
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
 * \file    sal_functions.h
 * \brief   Management of the state abstraction layer.
 *
 * Management of the state abstraction layer
 *
 *
 */

#ifndef _SAL_FUNCTIONS_H
#define _SAL_FUNCTIONS_H

#include "sal_data.h"
#include "nfs_exports.h"
#include "nfs_core.h"

extern pool_t *nfs41_session_pool;

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

int display_owner(struct display_buffer * dspbuf,
                  state_owner_t         * powner);

void inc_state_owner_ref(state_owner_t * powner);
void dec_state_owner_ref(state_owner_t * powner);

state_owner_t *get_state_owner(care_t               care,
                               state_owner_t      * pkey,
                               state_owner_init_t   init_owner,
                               bool_t             * isnew);

int DisplayOpaqueValue(char * value, int len, char * str);

void state_wipe_file(cache_entry_t * pentry);

#ifdef _DEBUG_MEMLEAKS
void dump_all_owners(void);
#endif

/******************************************************************************
 *
 * 9P State functions
 *
 ******************************************************************************/

#ifdef _USE_9P
int compare_9p_owner(state_owner_t * powner1,
                     state_owner_t * powner2);
int compare_9p_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2) ;

int display_9p_owner(struct display_buffer * dspbuf,
                     state_owner_t         * pkey);

int display_9p_owner_key(struct display_buffer * dspbuf,
                         hash_buffer_t         * pbuff);

int display_9p_owner_val(struct display_buffer * dspbuf,
                         hash_buffer_t         * pbuff);

uint32_t _9p_owner_value_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t * buffclef) ;
uint64_t _9p_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                 hash_buffer_t * buffclef) ;

state_owner_t * get_9p_owner( struct sockaddr_storage * pclient_addr,
                              uint32_t    proc_id) ;

int Init_9p_hash(void) ;
#endif

/******************************************************************************
 *
 * NLM State functions
 *
 ******************************************************************************/

#ifdef _USE_NLM
/* These refcount functions must not be called holding the ssc_mutex */
void inc_nsm_client_ref(state_nsm_client_t * pclient);
void dec_nsm_client_ref(state_nsm_client_t * pclient);

int display_nsm_client(struct display_buffer * dspbuf,
                       state_nsm_client_t    * pkey);

int display_nsm_client_val(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int display_nsm_client_key(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int compare_nsm_client(state_nsm_client_t * pclient1,
                       state_nsm_client_t * pclient2);

int compare_nsm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint32_t nsm_client_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef);

uint64_t nsm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef);

state_nsm_client_t *get_nsm_client(care_t    care,
                                   SVCXPRT * xprt,
                                   char    * caller_name);
void inc_nlm_client_ref(state_nlm_client_t * pclient);
void dec_nlm_client_ref(state_nlm_client_t * pclient);

int display_nlm_client(struct display_buffer * dspbuf,
                       state_nlm_client_t    * pkey);

int display_nlm_client_val(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int display_nlm_client_key(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int compare_nlm_client(state_nlm_client_t * pclient1,
                       state_nlm_client_t * pclient2);

int compare_nlm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint32_t nlm_client_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef);

uint64_t nlm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef);

state_nlm_client_t *get_nlm_client(care_t               care,
                                   SVCXPRT            * xprt,
                                   state_nsm_client_t * pnsm_client,
                                   char               * caller_name);

void free_nlm_owner(state_owner_t * powner);

int display_nlm_owner(struct display_buffer * dspbuf,
                      state_owner_t         * pkey);

int display_nlm_owner_val(struct display_buffer * dspbuf,
                          hash_buffer_t         * pbuff);

int display_nlm_owner_key(struct display_buffer * dspbuf,
                          hash_buffer_t         * pbuff);

int compare_nlm_owner(state_owner_t * powner1,
                      state_owner_t * powner2);

int compare_nlm_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint32_t nlm_owner_value_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t    * buffclef);

uint64_t nlm_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                 hash_buffer_t    * buffclef);

state_owner_t *get_nlm_owner(care_t               care,
                             state_nlm_client_t * pclient,
                             netobj             * oh,
                             uint32_t             svid);

int Init_nlm_hash(void);
#endif

/******************************************************************************
 *
 * NFS4 Client ID functions
 *
 ******************************************************************************/

nfsstat4 clientid_error_to_nfsstat(nfs_clientid_error_t err);

const char * clientid_error_to_str(nfs_clientid_error_t err);

state_status_t get_clientid_owner(clientid4        clientid,
                                  state_owner_t ** clientid_owner);

int nfs_Init_client_id(nfs_client_id_parameter_t * param);

int nfs_client_id_get_unconfirmed(clientid4          clientid,
                                  nfs_client_id_t ** p_pclientid);

int nfs_client_id_get_confirmed(clientid4          clientid,
                                nfs_client_id_t ** p_pclientid);

nfs_client_id_t * create_client_id(clientid4              clientid,
                                   nfs_client_record_t  * pclient_record,
                                   sockaddr_t           * pclient_addr,
                                   nfs_client_cred_t    * pcredential);

int nfs_client_id_insert(nfs_client_id_t * pclientid);

int remove_unconfirmed_client_id(nfs_client_id_t * pclientid);

int nfs_client_id_confirm(nfs_client_id_t * pclientid,
                          log_components_t  component);

int nfs_client_id_expire(nfs_client_id_t * pclientid, int release);

clientid4 new_clientid(void);
void new_clientid_verifier(char * pverf);

int display_client_id_key(struct display_buffer * dspbuf,
                          hash_buffer_t         * pbuff);

int display_client_id_val(struct display_buffer * dspbuf,
                          hash_buffer_t         * pbuff);

int compare_client_id(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint64_t client_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                 hash_buffer_t    * buffclef);

uint32_t client_id_value_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t    * buffclef);

int display_client_id_rec(struct display_buffer * dspbuf,
                          nfs_client_id_t       * pclientid);

int display_clientid_name(struct display_buffer * dspbuf,
                          nfs_client_id_t       * pclientid);

void free_client_id(nfs_client_id_t *pclientid);

void inc_client_id_ref(nfs_client_id_t * pclientid);
void dec_client_id_ref(nfs_client_id_t * pclientid);

int display_client_record(struct display_buffer * dspbuf,
                          nfs_client_record_t   * precord);

void inc_client_record_ref(nfs_client_record_t *precord);
void dec_client_record_ref(nfs_client_record_t *precord);

int display_client_record_key(struct display_buffer * dspbuf,
                              hash_buffer_t         * pbuff);

int display_client_record_val(struct display_buffer * dspbuf,
                              hash_buffer_t         * pbuff);

int compare_client_record(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint64_t client_record_rbt_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t    * buffclef);

uint32_t client_record_value_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t    * buffclef);

nfs_client_record_t *get_client_record(char * value, int len);

/******************************************************************************
 *
 * NFS4.1 Session ID functions
 *
 ******************************************************************************/

#ifdef _USE_NFS4_1
int display_session_id_key(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int display_session_id_val(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int compare_session_id(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint32_t session_id_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef);

uint64_t session_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef);

int nfs41_Init_session_id(nfs_session_id_parameter_t param);

int nfs41_Session_Set(char              sessionid[NFS4_SESSIONID_SIZE],
                      nfs41_session_t * psession_data);

int nfs41_Session_Get_Pointer(char               sessionid[NFS4_SESSIONID_SIZE],
                              nfs41_session_t ** psession_data);

int nfs41_Session_Del(char sessionid[NFS4_SESSIONID_SIZE]);
void nfs41_Build_sessionid(clientid4 * pclientid, char * sessionid);
void nfs41_Session_PrintAll(void);
int display_session(struct display_buffer * dspbuf, nfs41_session_t * psession);
int display_session_id(struct display_buffer * dspbuf, char * session_id);
#endif

/******************************************************************************
 *
 * NFSv4 Stateid functions
 *
 ******************************************************************************/

void nfs4_BuildStateId_Other(nfs_client_id_t * clientid, char * other);

#define STATEID_NO_SPECIAL 0
#define STATEID_SPECIAL_ALL_0      2
#define STATEID_SPECIAL_ALL_1      4
#define STATEID_SPECIAL_CURRENT    8
/* The following flag tells nfs4_Check_Stateid this is a close call
 * and to ignore stateid that have valid clientid portion, but the
 * counter portion doesn't reference a currently open file.
 */
#define STATEID_SPECIAL_CLOSE_40   0x40
#define STATEID_SPECIAL_CLOSE_41   0x80
#define STATEID_SPECIAL_ANY        0x3F
#define STATEID_SPECIAL_FOR_LOCK   (STATEID_SPECIAL_CURRENT)
#define STATEID_SPECIAL_FOR_CLOSE_40 (STATEID_SPECIAL_CLOSE_40)
#define STATEID_SPECIAL_FOR_CLOSE_41 (STATEID_SPECIAL_CLOSE_41 | \
                                      STATEID_SPECIAL_CURRENT)

int nfs4_Check_Stateid(stateid4        * pstate,
                       cache_entry_t   * pentry,
                       state_t        ** ppstate,
                       compound_data_t * data,
                       char              flags,
                       seqid4            owner_seqid,
                       unsigned char     version4,
                       const char      * tag);

void update_stateid(state_t         * pstate,
                    stateid4        * presp,
                    compound_data_t * data,
                    const char      * tag);

int nfs4_check_special_stateid(cache_entry_t *pentry,
                               const char    *tag,
                               int            access);

int nfs4_Init_state_id(nfs_state_id_parameter_t param);
int nfs4_State_Set(char other[OTHERSIZE], state_t * pstate_data);
int nfs4_State_Get_Pointer(char other[OTHERSIZE], state_t * *pstate_data);
int nfs4_State_Del(char other[OTHERSIZE]);
void nfs_State_PrintAll(void);

int display_state_id_val(struct display_buffer * dspbuf, hash_buffer_t * pbuff);
int display_state_id_key(struct display_buffer * dspbuf, hash_buffer_t * pbuff);

uint32_t state_id_value_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef);

uint64_t state_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                hash_buffer_t    * buffclef);

/******************************************************************************
 *
 * NFSv4 Lease functions
 *
 ******************************************************************************/

int  reserve_lease(nfs_client_id_t * pclientid);
void update_lease(nfs_client_id_t * pclientid);
int  valid_lease(nfs_client_id_t * pclientid);

/******************************************************************************
 *
 * NFSv4 Owner functions
 *
 ******************************************************************************/

void free_nfs4_owner(state_owner_t * powner);

int display_nfs4_owner(struct display_buffer * dspbuf,
                       state_owner_t         * powner);

int display_nfs4_owner_val(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int display_nfs4_owner_key(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff);

int compare_nfs4_owner(state_owner_t * powner1,
                       state_owner_t * powner2);

int compare_nfs4_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint32_t nfs4_owner_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef);

uint64_t nfs4_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef);

static inline void convert_nfs4_open_owner(open_owner4             * pnfsowner,
                                           state_nfs4_owner_name_t * pname_owner)
{
  pname_owner->son_owner_len = pnfsowner->owner.owner_len;
  pname_owner->son_owner_val = pnfsowner->owner.owner_val;
}

static inline void convert_nfs4_lock_owner(lock_owner4             * pnfsowner,
                                           state_nfs4_owner_name_t * pname_owner)
{
  pname_owner->son_owner_len = pnfsowner->owner.owner_len;
  pname_owner->son_owner_val = pnfsowner->owner.owner_val;
}

void nfs4_owner_PrintAll(void);

state_owner_t *create_nfs4_owner(state_nfs4_owner_name_t * pname,
                                 nfs_client_id_t         * pclientid,
                                 state_owner_type_t        type,
                                 state_owner_t           * related_owner,
                                 unsigned int              init_seqid,
                                 bool_t                  * pisnew,
                                 care_t                    care);

int Init_nfs4_owner(nfs4_owner_parameter_t param);

void Process_nfs4_conflict(LOCK4denied          * denied,    /* NFS v4 LOck4denied structure to fill in */
                           state_owner_t        * holder,    /* owner that holds conflicting lock */
                           fsal_lock_param_t    * conflict); /* description of conflicting lock */

void Release_nfs4_denied(LOCK4denied * denied);
void Copy_nfs4_denied(LOCK4denied * denied_dst, LOCK4denied * denied_src);

void Copy_nfs4_state_req(state_owner_t   * powner,
                         seqid4            seqid,
                         nfs_argop4      * args,
                         cache_entry_t   * entry,
                         nfs_resop4      * resp,
                         const char      * tag);

bool_t Check_nfs4_seqid(state_owner_t   * powner,
                        seqid4            seqid,
                        nfs_argop4      * args,
                        cache_entry_t   * entry,
                        nfs_resop4      * resp,
                        const char      * tag);

/******************************************************************************
 *
 * Lock functions
 *
 ******************************************************************************/

#ifdef _USE_BLOCKING_LOCKS
void free_block_data(state_block_data_t * block_data);

int display_lock_cookie_key(struct display_buffer * dspbuf,
                            hash_buffer_t         * pbuff);

int display_lock_cookie_val(struct display_buffer * dspbuf,
                            hash_buffer_t         * pbuff);

int compare_lock_cookie_key(hash_buffer_t * buff1, hash_buffer_t * buff2);

uint32_t lock_cookie_value_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t    * buffclef);

uint64_t lock_cookie_rbt_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t    * buffclef);
#endif

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
             fsal_lock_param_t  * plock);

void dump_all_locks(const char * label);

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
                                      state_status_t        * pstatus);

state_status_t state_find_grant(void                  * pcookie,
                                int                     cookie_size,
                                state_cookie_entry_t ** ppcookie_entry,
                                state_status_t        * pstatus);

void state_complete_grant(fsal_op_context_t    * pcontext,
                          state_cookie_entry_t * cookie_entry);

/**
 *
 * state_cancel_grant: Cancel a blocked lock grant
 *
 * This function is to be called from the granted_callback_t function.
 */
state_status_t state_cancel_grant(fsal_op_context_t    * pcontext,
                                  state_cookie_entry_t * cookie_entry,
                                  state_status_t       * pstatus);

state_status_t state_release_grant(fsal_op_context_t    * pcontext,
                                   state_cookie_entry_t * cookie_entry,
                                   state_status_t       * pstatus);
#endif

state_status_t state_test(cache_entry_t        * pentry,
                          fsal_op_context_t    * pcontext,
                          exportlist_t         * pexport,
                          state_owner_t        * powner,
                          fsal_lock_param_t    * plock,
                          state_owner_t       ** holder,   /* owner that holds conflicting lock */
                          fsal_lock_param_t    * conflict, /* description of conflicting lock */
                          state_status_t       * pstatus);

state_status_t state_lock(cache_entry_t         * pentry,
                          fsal_op_context_t     * pcontext,
                          exportlist_t          * pexport,
                          state_owner_t         * powner,
                          state_t               * pstate,
                          state_blocking_t        blocking,
                          state_block_data_t    * block_data,
                          fsal_lock_param_t     * plock,
                          bool_t                  is_reclaim,
                          state_owner_t        ** holder,   /* owner that holds conflicting lock */
                          fsal_lock_param_t     * conflict, /* description of conflicting lock */
                          state_status_t        * pstatus);

state_status_t state_unlock(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            exportlist_t         * pexport,
                            state_owner_t        * powner,
                            state_t              * pstate,
                            fsal_lock_param_t    * plock,
                            state_status_t       * pstatus);

#ifdef _USE_BLOCKING_LOCKS
state_status_t state_cancel(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            exportlist_t         * pexport,
                            state_owner_t        * powner,
                            fsal_lock_param_t    * plock,
                            state_status_t       * pstatus);
#endif

#ifdef _USE_NLM
state_status_t state_nlm_notify(state_nsm_client_t   * pnsmclient,
                                state_t              * pstate,
                                state_status_t       * pstatus,
                                bool_t                 release_all);
#endif

state_status_t state_owner_unlock_all(fsal_op_context_t    * pcontext,
                                      state_owner_t        * powner,
                                      state_t              * pstate,
                                      state_status_t       * pstatus);

void state_lock_wipe(cache_entry_t * pentry);

void cancel_all_nlm_blocked();

/******************************************************************************
 *
 * NFSv4 State Management functions
 *
 ******************************************************************************/

int state_conflict(state_t      * pstate,
                   state_type_t   state_type,
                   state_data_t * pstate_data);

state_status_t state_add_impl(cache_entry_t         * pentry,
                              state_type_t            state_type,
                              state_data_t          * pstate_data,
                              state_owner_t         * powner_input,
                              fsal_op_context_t     * pcontext,
                              state_t              ** ppstate,
                              state_status_t        * pstatus);

state_status_t state_add(cache_entry_t         * pentry,
                         state_type_t            state_type,
                         state_data_t          * pstate_data,
                         state_owner_t         * powner_input,
                         fsal_op_context_t     * pcontext,
                         state_t              ** ppstate,
                         state_status_t        * pstatus);

state_status_t state_set(state_t              * pstate,
                         state_status_t       * pstatus);

state_status_t state_del_locked(state_t              * pstate,
                                cache_entry_t        * pentry);

state_status_t state_del(state_t              * pstate,
                         state_status_t       * pstatus);

int display_lock_cookie_key(struct display_buffer * dspbuf,
                            hash_buffer_t         * pbuff);

int display_lock_cookie_val(struct display_buffer * dspbuf,
                            hash_buffer_t         * pbuff);

int compare_lock_cookie_key(hash_buffer_t * buff1, hash_buffer_t * buff2);
uint32_t lock_cookie_value_hash_func(hash_parameter_t * p_hparam,
                                          hash_buffer_t * buffclef);
uint64_t lock_cookie_rbt_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t * buffclef);

#ifdef _PNFS_MDS
state_status_t state_add_segment(state_t             * pstate,
                                 struct pnfs_segment * segment,
                                 void                * fsal_data,
                                 bool_t                return_on_close);

state_status_t state_delete_segment(state_layout_segment_t *segment);
state_status_t state_lookup_layout_state(cache_entry_t * pentry,
                                         state_owner_t * powner,
                                         layouttype4     type,
                                         state_t      ** pstate);
#endif /*  _PNFS_MDS */

void state_nfs4_state_wipe(cache_entry_t        * pentry);

void release_lockstate(state_owner_t * plock_owner);
void release_openstate(state_owner_t * popen_owner);

#ifdef _DEBUG_MEMLEAKS
void dump_all_states(void);
#endif

/******************************************************************************
 *
 * Share functions
 *
 ******************************************************************************/

#define OPEN4_SHARE_ACCESS_NONE 0

state_status_t state_share_add(cache_entry_t         * pentry,
                               fsal_op_context_t     * pcontext,
                               state_owner_t         * powner,
                               state_t               * pstate,  /* state that holds share bits to be added */
                               state_status_t        * pstatus);

state_status_t state_share_remove(cache_entry_t         * pentry,
                                  fsal_op_context_t     * pcontext,
                                  state_owner_t         * powner,
                                  state_t               * pstate,  /* state that holds share bits to be removed */
                                  state_status_t        * pstatus);

state_status_t state_share_upgrade(cache_entry_t         * pentry,
                                   fsal_op_context_t     * pcontext,
                                   state_data_t          * pstate_data, /* new share bits */
                                   state_owner_t         * powner,
                                   state_t               * pstate,      /* state that holds current share bits */
                                   state_status_t        * pstatus);

state_status_t state_share_downgrade(cache_entry_t         * pentry,
                                     fsal_op_context_t     * pcontext,
                                     state_data_t          * pstate_data, /* new share bits */
                                     state_owner_t         * powner,
                                     state_t               * pstate,      /* state that holds current share bits */
                                     state_status_t        * pstatus);

state_status_t state_share_set_prev(state_t      * pstate,
                                    state_data_t * pstate_data);

state_status_t state_share_check_prev(state_t      * pstate,
                                    state_data_t * pstate_data);

state_status_t state_share_check_conflict(cache_entry_t  * pentry,
                                          int              share_acccess,
                                          int              share_deny,
                                          state_status_t * pstatus);

state_status_t state_share_anonymous_io_start(cache_entry_t  * pentry,
                                              int              share_access,
                                              state_status_t * pstatus);

void state_share_anonymous_io_done(cache_entry_t  * pentry,
                                   int              share_access);

#ifdef _USE_NLM
state_status_t state_nlm_share(cache_entry_t        * pentry,
                               fsal_op_context_t    * pcontext,
                               exportlist_t         * pexport,
                               int                    share_access,
                               int                    share_deny,
                               state_owner_t        * powner,
                               state_status_t       * pstatus);

state_status_t state_nlm_unshare(cache_entry_t        * pentry,
                                 fsal_op_context_t    * pcontext,
                                 int                    share_access,
                                 int                    share_deny,
                                 state_owner_t        * powner,
                                 state_status_t       * pstatus);

void state_share_wipe(cache_entry_t * pentry);
#endif

/******************************************************************************
 *
 * Async functions
 *
 ******************************************************************************/

#ifdef _USE_BLOCKING_LOCKS

/* Schedule Async Work */
state_status_t state_async_schedule(state_async_queue_t *arg);

/* Signal Async Work */
void signal_async_work();

state_status_t state_async_init();
void state_async_thread_start();

void grant_blocked_lock_upcall(cache_entry_t        * pentry,
                               void                 * powner,
                               fsal_lock_param_t    * plock);

void available_blocked_lock_upcall(cache_entry_t        * pentry,
                                   void                 * powner,
                                   fsal_lock_param_t    * plock);

void process_blocked_lock_upcall(state_block_data_t   * block_data);
#endif

/******************************************************************************
 *
 * NFSv4 Recovery functions
 *
 ******************************************************************************/

void nfs4_init_grace();
void nfs4_start_grace(nfs_grace_start_array_t *gsap);
int nfs_in_grace();

void
nfs4_create_clid_name(nfs_client_record_t * cl_recp,
                      nfs_client_id_t     * pclientid,
                      compound_data_t     * data);

void nfs4_add_clid(nfs_client_id_t *);
void nfs4_rm_clid(const char *, char*, int);
void nfs4_chk_clid(nfs_client_id_t *);
void nfs4_load_recov_clids(int nodeid);
void nfs4_clean_old_recov_dir(char *);
void nfs4_create_recov_dir();

#endif                          /*  _SAL_FUNCTIONS_H */
