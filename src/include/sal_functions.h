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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file  sal_functions.h
 * @brief Routines in the state abstraction layer
 * @note  not called by other header files.
 */

#ifndef SAL_FUNCTIONS_H
#define SAL_FUNCTIONS_H

#include <stdint.h>
#include "sal_data.h"

/**
 * @brief Divisions in state and clientid tables.
 */
#define PRIME_STATE 17

/*****************************************************************************
 *
 * Misc functions
 *
 *****************************************************************************/

const char *state_err_str(state_status_t err);

state_status_t state_error_convert(fsal_status_t fsal_status);

state_status_t cache_inode_status_to_state_status(cache_inode_status_t status);

nfsstat4 nfs4_Errno_state(state_status_t error);
nfsstat3 nfs3_Errno_state(state_status_t error);

const char *state_owner_type_to_str(state_owner_type_t type);
bool different_owners(state_owner_t *owner1, state_owner_t *owner2);
int DisplayOwner(state_owner_t *owner, char *buf);
void inc_state_owner_ref(state_owner_t *owner);
void dec_state_owner_ref(state_owner_t *owner);

state_owner_t *get_state_owner(care_t care, state_owner_t *pkey,
			       state_owner_init_t init_owner, bool_t *isnew);

int DisplayOpaqueValue(char *value, int len, char *str);

void state_wipe_file(cache_entry_t *entry);

#ifdef DEBUG_SAL
void dump_all_owners(void);
#endif

void state_release_export(struct gsh_export *exp);

bool state_unlock_err_ok(state_status_t status);

/*****************************************************************************
 *
 * 9P State functions
 *
 *****************************************************************************/

#ifdef _USE_9P
int compare_9p_owner(state_owner_t *owner1, state_owner_t *owner2);
int compare_9p_owner_key(struct gsh_buffdesc *buff1,
			 struct gsh_buffdesc *buff2);

int display_9p_owner(state_owner_t *key, char *str);
int display_9p_owner_key(struct gsh_buffdesc *buff, char *str);
int display_9p_owner_val(struct gsh_buffdesc *buff, char *str);

uint32_t _9p_owner_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key);
uint64_t _9p_owner_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key);

state_owner_t *get_9p_owner(struct sockaddr_storage *client_addr,
			    uint32_t proc_id);

int Init_9p_hash(void);
#endif

/******************************************************************************
 *
 * NLM State functions
 *
 ******************************************************************************/

void free_nsm_client(state_nsm_client_t *client);

/* These refcount functions must not be called holding the ssc_mutex */
void inc_nsm_client_ref(state_nsm_client_t *client);
void dec_nsm_client_ref(state_nsm_client_t *client);

int display_nsm_client(state_nsm_client_t *key, char *str);
int display_nsm_client_val(struct gsh_buffdesc *buff, char *str);
int display_nsm_client_key(struct gsh_buffdesc *buff, char *str);

int compare_nsm_client(state_nsm_client_t *client1,
		       state_nsm_client_t *client2);

int compare_nsm_client_key(struct gsh_buffdesc *buff1,
			   struct gsh_buffdesc *buff2);

uint32_t nsm_client_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key);

uint64_t nsm_client_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key);

state_nsm_client_t *get_nsm_client(care_t care, SVCXPRT *xprt,
				   char *caller_name);
void inc_nlm_client_ref(state_nlm_client_t *client);
void dec_nlm_client_ref(state_nlm_client_t *client);
int display_nlm_client(state_nlm_client_t *key, char *str);
int display_nlm_client_val(struct gsh_buffdesc *buff, char *str);
int display_nlm_client_key(struct gsh_buffdesc *buff, char *str);

int compare_nlm_client(state_nlm_client_t *client1,
		       state_nlm_client_t *client2);

int compare_nlm_client_key(struct gsh_buffdesc *buff1,
			   struct gsh_buffdesc *buff2);

uint32_t nlm_client_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key);

uint64_t nlm_client_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key);

state_nlm_client_t *get_nlm_client(care_t care, SVCXPRT *xprt,
				   state_nsm_client_t *nsm_client,
				   char *caller_name);

void free_nlm_owner(state_owner_t *powner);

int display_nlm_owner(state_owner_t *key, char *str);
int display_nlm_owner_val(struct gsh_buffdesc *buff, char *str);
int display_nlm_owner_key(struct gsh_buffdesc *buff, char *str);

int compare_nlm_owner(state_owner_t *owner1, state_owner_t *owner2);

int compare_nlm_owner_key(struct gsh_buffdesc *buff1,
			  struct gsh_buffdesc *buff2);

uint32_t nlm_owner_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key);

uint64_t nlm_owner_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key);

state_owner_t *get_nlm_owner(care_t care, state_nlm_client_t *client,
			     netobj *oh, uint32_t svid);

int Init_nlm_hash(void);

/******************************************************************************
 *
 * NFS4 Client ID functions
 *
 ******************************************************************************/

nfsstat4 clientid_error_to_nfsstat(clientid_status_t err);

const char *clientid_error_to_str(clientid_status_t err);

int nfs_Init_client_id(void);

clientid_status_t nfs_client_id_get_unconfirmed(clientid4 clientid,
						nfs_client_id_t **pclient_rec);

clientid_status_t nfs_client_id_get_confirmed(clientid4 clientid,
					      nfs_client_id_t **client_rec);

nfs_client_id_t *create_client_id(clientid4 clientid,
				  nfs_client_record_t *client_record,
				  sockaddr_t *client_addr,
				  nfs_client_cred_t *credential,
				  struct gsh_client *gsh_client,
				  uint32_t minorversion);

clientid_status_t nfs_client_id_insert(nfs_client_id_t *clientid);

int remove_confirmed_client_id(nfs_client_id_t *clientid);
int remove_unconfirmed_client_id(nfs_client_id_t *clientid);

clientid_status_t nfs_client_id_confirm(nfs_client_id_t *clientid,
					log_components_t component);

bool nfs_client_id_expire(nfs_client_id_t *clientid, bool make_stale);

clientid4 new_clientid(void);
void new_clientid_verifier(char *verf);

int display_client_id_key(struct gsh_buffdesc *buff, char *str);
int display_client_id_val(struct gsh_buffdesc *buff, char *str);

int compare_client_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);

uint64_t client_id_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key);

uint32_t client_id_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key);

int display_client_id_rec(nfs_client_id_t *clientid, char *str);
int display_clientid_name(nfs_client_id_t *clientid, char *str);

void free_client_id(nfs_client_id_t *clientid);

void
nfs41_foreach_client_callback(bool(*cb) (nfs_client_id_t *cl, void *state),
			      void *state);

bool client_id_has_nfs41_sessions(nfs_client_id_t *clientid);

bool client_id_has_state(nfs_client_id_t *clientid);

int32_t inc_client_id_ref(nfs_client_id_t *clientid);
int32_t dec_client_id_ref(nfs_client_id_t *clientid);

int32_t inc_session_ref(nfs41_session_t *session);
int32_t dec_session_ref(nfs41_session_t *session);

int display_client_record(nfs_client_record_t *record, char *str);

void free_client_record(nfs_client_record_t *record);

int32_t inc_client_record_ref(nfs_client_record_t *record);
int32_t dec_client_record_ref(nfs_client_record_t *record);

int display_client_record_key(struct gsh_buffdesc *buff, char *str);
int display_client_record_val(struct gsh_buffdesc *buff, char *str);

int compare_client_record(struct gsh_buffdesc *buff1,
			  struct gsh_buffdesc *buff2);

uint64_t client_record_rbt_hash_func(hash_parameter_t *hparam,
				     struct gsh_buffdesc *key);

uint32_t client_record_value_hash_func(hash_parameter_t *hparam,
				       struct gsh_buffdesc *key);

nfs_client_record_t *get_client_record(const char *const value,
				       const size_t len,
				       const uint32_t pnfs_flags,
				       const uint32_t server_addr);

/******************************************************************************
 *
 * NFS4.1 Session ID functions
 *
 ******************************************************************************/

int display_session_id_key(struct gsh_buffdesc *buff, char *str);
int display_session_id_val(struct gsh_buffdesc *buff, char *str);
int compare_session_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);

uint32_t session_id_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key);

uint64_t session_id_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key);

int nfs41_Init_session_id(void);

int nfs41_Session_Set(nfs41_session_t *session_data);

int nfs41_Session_Get_Pointer(char sessionid[NFS4_SESSIONID_SIZE],
			      nfs41_session_t **session_data);

int nfs41_Session_Del(char sessionid[NFS4_SESSIONID_SIZE]);
void nfs41_Build_sessionid(clientid4 *clientid, char *sessionid);
void nfs41_Session_PrintAll(void);
int display_session(nfs41_session_t *session, char *str);
int display_session_id(char *session_id, char *str);

/******************************************************************************
 *
 * NFSv4 Stateid functions
 *
 ******************************************************************************/

int display_stateid_other(char *other, char *str);
void nfs4_BuildStateId_Other(nfs_client_id_t *clientid, char *other);

#define STATEID_NO_SPECIAL 0	/*< No special stateids */
#define STATEID_SPECIAL_ALL_0 2	/*< Allow anonymous */
#define STATEID_SPECIAL_ALL_1 4	/*< Allow read-bypass */
#define STATEID_SPECIAL_CURRENT 8	/*< Allow current */
#define STATEID_SPECIAL_FREE 0x100	/*< Check for FREE_STATEID */
#define STATEID_SPECIAL_FOR_FREE (STATEID_SPECIAL_CURRENT | \
				  STATEID_SPECIAL_FREE)

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

nfsstat4 nfs4_Check_Stateid(stateid4 *stateid, cache_entry_t *entry,
			    state_t **state, compound_data_t *data, int flags,
			    seqid4 owner_seqid, bool check_seqid,
			    const char *tag);

void update_stateid(state_t *state, stateid4 *stateid, compound_data_t *data,
		    const char *tag);

int nfs4_Init_state_id(void);
void inc_state_t_ref(struct state_t *state);
void dec_state_t_ref(struct state_t *state);
int nfs4_State_Set(state_t *state_data);
struct state_t *nfs4_State_Get_Pointer(char *other);
bool nfs4_State_Del(char *other);
void nfs_State_PrintAll(void);

int display_state_id_val(struct gsh_buffdesc *buff, char *str);
int display_state_id_key(struct gsh_buffdesc *buff, char *str);

uint32_t state_id_value_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key);

uint64_t state_id_rbt_hash_func(hash_parameter_t *hparam,
				struct gsh_buffdesc *key);

/******************************************************************************
 *
 * NFSv4 Lease functions
 *
 ******************************************************************************/

int reserve_lease(nfs_client_id_t *clientid);
void update_lease(nfs_client_id_t *clientid);
bool valid_lease(nfs_client_id_t *clientid);

/******************************************************************************
 *
 * NFSv4 Owner functions
 *
 ******************************************************************************/

void free_nfs4_owner(state_owner_t *owner);
int display_nfs4_owner(state_owner_t *owner, char *str);
int display_nfs4_owner_val(struct gsh_buffdesc *buff, char *str);
int display_nfs4_owner_key(struct gsh_buffdesc *buff, char *str);

int compare_nfs4_owner(state_owner_t *owner1, state_owner_t *owner2);

int compare_nfs4_owner_key(struct gsh_buffdesc *buff1,
			   struct gsh_buffdesc *buff2);

uint32_t nfs4_owner_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key);

uint64_t nfs4_owner_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key);

/**
 * @brief Convert an open_owner to an owner name
 *
 * @param[in]  nfsowner   Open owner as specified in NFS
 * @param[out] name_owner Name used as key in owner table
 */
static inline void convert_nfs4_open_owner(open_owner4 *nfsowner,
					   state_nfs4_owner_name_t *name_owner)
{
	name_owner->son_owner_len = nfsowner->owner.owner_len;
	name_owner->son_owner_val = nfsowner->owner.owner_val;
}

/**
 * @brief Convert a lock_owner to an owner name
 *
 * @param[in]  nfsowner   Open owner as specified in NFS
 * @param[out] name_owner Name used as key in owner table
 */
static inline void convert_nfs4_lock_owner(lock_owner4 *nfsowner,
					   state_nfs4_owner_name_t *name_owner)
{
	name_owner->son_owner_len = nfsowner->owner.owner_len;
	name_owner->son_owner_val = nfsowner->owner.owner_val;
}

void nfs4_owner_PrintAll(void);

state_owner_t *create_nfs4_owner(state_nfs4_owner_name_t *name,
				 nfs_client_id_t *clientid,
				 state_owner_type_t type,
				 state_owner_t *related_owner,
				 unsigned int init_seqid, bool_t *pisnew,
				 care_t care);

int Init_nfs4_owner(void);

void Process_nfs4_conflict(/* NFS v4 Lock4denied structure to fill in */
			   LOCK4denied * denied,
			   /* owner that holds conflicting lock */
			   state_owner_t *holder,
			   /* description of conflicting lock */
			   fsal_lock_param_t *conflict);

void Release_nfs4_denied(LOCK4denied *denied);
void Copy_nfs4_denied(LOCK4denied *denied_dst, LOCK4denied *denied_src);

void Copy_nfs4_state_req(state_owner_t *owner, seqid4 seqid, nfs_argop4 *args,
			 cache_entry_t *entry, nfs_resop4 *resp,
			 const char *tag);

bool Check_nfs4_seqid(state_owner_t *owner, seqid4 seqid, nfs_argop4 *args,
		      cache_entry_t *entry, nfs_resop4 *resp,
		      const char *tag);

/******************************************************************************
 *
 * Lock functions
 *
 ******************************************************************************/

int display_lock_cookie_key(struct gsh_buffdesc *buff, char *str);
int display_lock_cookie_val(struct gsh_buffdesc *buff, char *str);
int compare_lock_cookie_key(struct gsh_buffdesc *buff1,
			    struct gsh_buffdesc *buff2);

uint32_t lock_cookie_value_hash_func(hash_parameter_t *hparam,
				     struct gsh_buffdesc *key);

uint64_t lock_cookie_rbt_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key);
state_status_t state_lock_init(void);

void LogLock(log_components_t component, log_levels_t debug, const char *reason,
	     cache_entry_t *entry, state_owner_t *owner,
	     fsal_lock_param_t *lock);

void dump_all_locks(const char *label);

state_status_t state_add_grant_cookie(cache_entry_t *entry,
				      void *cookie, int cookie_size,
				      state_lock_entry_t *lock_entry,
				      state_cookie_entry_t **cookie_entry);

state_status_t state_find_grant(void *cookie, int cookie_size,
				state_cookie_entry_t **cookie_entry);

void state_complete_grant(state_cookie_entry_t *cookie_entry);

state_status_t state_cancel_grant(state_cookie_entry_t *cookie_entry);

state_status_t state_release_grant(state_cookie_entry_t *cookie_entry);
state_status_t state_test(cache_entry_t *entry,
			  state_owner_t *owner,
			  fsal_lock_param_t *lock,
			  /* owner that holds conflicting lock */
			  state_owner_t **holder,
			  /* description of conflicting lock */
			  fsal_lock_param_t *conflict);

state_status_t state_lock(cache_entry_t *entry,
			  state_owner_t *owner,
			  state_t *state, state_blocking_t blocking,
			  state_block_data_t *block_data,
			  fsal_lock_param_t *lock,
			  /* owner that holds conflicting lock */
			  state_owner_t **holder,
			  /* description of conflicting lock */
			  fsal_lock_param_t *conflict);
state_status_t do_lock_op(cache_entry_t *entry,
			  fsal_lock_op_t lock_op,
			  state_owner_t *owner,
			  fsal_lock_param_t *lock,
			  state_owner_t **holder,
			  fsal_lock_param_t *conflict,
			  bool_t overlap,
			  enum fsal_sle_type sle_type);
state_status_t state_unlock(cache_entry_t *entry,
			    state_owner_t *owner, state_t *state,
			    fsal_lock_param_t *lock);

state_status_t state_cancel(cache_entry_t *entry,
			    state_owner_t *owner, fsal_lock_param_t *lock);

state_status_t state_nlm_notify(state_nsm_client_t *nsmclient,
				bool from_client,
				state_t *state);

void state_nfs4_owner_unlock_all(state_owner_t *owner);

void state_export_unlock_all(void);

void state_lock_wipe(cache_entry_t *entry);

void cancel_all_nlm_blocked();

/******************************************************************************
 *
 * NFSv4 State Management functions
 *
 ******************************************************************************/

state_status_t state_add_impl(cache_entry_t *entry, enum state_type state_type,
			      union state_data *state_data,
			      state_owner_t *owner_input, state_t **state,
			      struct state_refer *refer);

state_status_t state_add(cache_entry_t *entry, enum state_type state_type,
			 union state_data *state_data,
			 state_owner_t *owner_input,
			 state_t **state, struct state_refer *refer);

state_status_t state_set(state_t *state);

void state_del_locked(state_t *state);

void state_del(state_t *state);

static inline cache_entry_t *get_state_entry_ref(state_t *state)
{
	cache_entry_t *entry = NULL;

	pthread_mutex_lock(&state->state_mutex);

	if (state->state_entry != NULL &&
	    cache_inode_lru_ref(state->state_entry,
				LRU_FLAG_NONE) == CACHE_INODE_SUCCESS)
		entry = state->state_entry;

	pthread_mutex_unlock(&state->state_mutex);

	return entry;
}

static inline struct gsh_export *get_state_export_ref(state_t *state)
{
	struct gsh_export *export = NULL;

	pthread_mutex_lock(&state->state_mutex);

	if (state->state_export != NULL &&
	    !get_gsh_export_ref(state->state_export, false))
		export = state->state_export;

	pthread_mutex_unlock(&state->state_mutex);

	return export;
}

static inline bool state_same_export(state_t *state, struct gsh_export *export)
{
	bool same = false;

	pthread_mutex_lock(&state->state_mutex);

	if (state->state_export != NULL)
		same = state->state_export == export;

	pthread_mutex_unlock(&state->state_mutex);

	return same;
}

static inline uint16_t state_export_id(state_t *state)
{
	uint16_t export_id = UINT16_MAX;

	pthread_mutex_lock(&state->state_mutex);

	if (state->state_export != NULL)
		export_id = state->state_export->export_id;

	pthread_mutex_unlock(&state->state_mutex);

	return export_id;
}

static inline state_owner_t *get_state_owner_ref(state_t *state)
{
	state_owner_t *owner = NULL;

	pthread_mutex_lock(&state->state_mutex);

	if (state->state_owner != NULL) {
		owner = state->state_owner;
		inc_state_owner_ref(owner);
	}

	pthread_mutex_unlock(&state->state_mutex);

	return owner;
}

static inline bool state_owner_confirmed(state_t *state)
{
	bool confirmed = false;

	pthread_mutex_lock(&state->state_mutex);

	if (state->state_owner != NULL) {
		confirmed =
			state->state_owner->so_owner.so_nfs4_owner.so_confirmed;
	}

	pthread_mutex_unlock(&state->state_mutex);

	return confirmed;
}

static inline bool state_same_owner(state_t *state, state_owner_t *owner)
{
	bool same = false;

	pthread_mutex_lock(&state->state_mutex);

	if (state->state_owner != NULL)
		same = state->state_owner == owner;

	pthread_mutex_unlock(&state->state_mutex);

	return same;
}

bool get_state_entry_export_owner_refs(state_t *state,
				       cache_entry_t **entry,
				       struct gsh_export **export,
				       state_owner_t **owner);

int display_lock_cookie_key(struct gsh_buffdesc *buff, char *str);
int display_lock_cookie_val(struct gsh_buffdesc *buff, char *str);
int compare_lock_cookie_key(struct gsh_buffdesc *buff1,
			    struct gsh_buffdesc *buff2);
uint32_t lock_cookie_value_hash_func(hash_parameter_t *hparam,
				     struct gsh_buffdesc *key);
uint64_t lock_cookie_rbt_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key);

state_status_t state_add_segment(state_t *state, struct pnfs_segment *segment,
				 void *fsal_data, bool return_on_close);

state_status_t state_delete_segment(state_layout_segment_t *segment);
state_status_t state_lookup_layout_state(cache_entry_t *entry,
					 state_owner_t *owner,
					 layouttype4 type, state_t **state);
void state_nfs4_state_wipe(cache_entry_t *entry);

enum nfsstat4 release_lock_owner(state_owner_t *owner);
void release_openstate(state_owner_t *owner);
void state_export_release_nfs4_state(void);
void revoke_owner_delegs(state_owner_t *client_owner);

#ifdef DEBUG_SAL
void dump_all_states(void);
#endif

/******************************************************************************
 *
 * Delegations functions
 *
 ******************************************************************************/

state_status_t acquire_lease_lock(cache_entry_t *entry,
				  state_owner_t *owner,
				  state_t *state);
state_status_t release_lease_lock(cache_entry_t *entry, state_t *state);

bool init_deleg_heuristics(cache_entry_t *entry);
bool deleg_supported(cache_entry_t *entry, struct fsal_export *fsal_export,
		     struct export_perms *export_perms, uint32_t share_access);
bool can_we_grant_deleg(cache_entry_t *entry, state_t *open_state);
bool should_we_grant_deleg(cache_entry_t *entry, nfs_client_id_t *client,
			   state_t *open_state, OPEN4args *args,
			   state_owner_t *owner, bool *prerecall);
void init_new_deleg_state(union state_data *deleg_state,
			  open_delegation_type4 sd_type,
			  nfs_client_id_t *clientid);

void deleg_heuristics_recall(cache_entry_t *entry,
			     state_owner_t *owner,
			     struct state_t *deleg);
void get_deleg_perm(cache_entry_t *entry, nfsace4 *permissions,
		    open_delegation_type4 type);
void update_delegation_stats(cache_entry_t *entry,
			     state_owner_t *owner,
			     struct state_t *deleg);
state_status_t delegrecall_impl(cache_entry_t *entry);
state_status_t deleg_revoke(cache_entry_t *entry, struct state_t *deleg_state);
void state_deleg_revoke(cache_entry_t *entry, state_t *state);
bool state_deleg_conflict(cache_entry_t *entry, bool write);

/******************************************************************************
 *
 * Share functions
 *
 ******************************************************************************/

#define OPEN4_SHARE_ACCESS_NONE 0

state_status_t state_share_add(cache_entry_t *entry,
			       state_owner_t *owner,
			       /* state that holds share bits to be added */
			       state_t *state, bool reclaim);

state_status_t state_share_remove(cache_entry_t *entry,
				  state_owner_t *owner,
				  /* state that holds share bits to be removed
				   */
				  state_t *state);

state_status_t state_share_upgrade(cache_entry_t *entry,
				   /* new share bits */
				   union state_data *state_data,
				   state_owner_t *owner,
				   /* state that holds current share bits */
				   state_t *state, bool reclaim);

state_status_t state_share_downgrade(cache_entry_t *entry,
				     /* new share bits */
				     union state_data *state_data,
				     state_owner_t *owner,
				     /* state that holds current share bits */
				     state_t *state);

state_status_t state_share_set_prev(state_t *state,
				    union state_data *state_data);

state_status_t state_share_check_prev(state_t *state,
				      union state_data *state_data);

enum share_bypass_modes {
	SHARE_BYPASS_NONE,
	SHARE_BYPASS_READ,
	SHARE_BYPASS_V3_WRITE
};

state_status_t state_share_check_conflict(cache_entry_t *entry,
					  int share_acccess,
					  int share_deny,
					  enum share_bypass_modes bypass);
bool state_open_deleg_conflict(cache_entry_t *entry, const state_t *open_state);

state_status_t state_share_anonymous_io_start(cache_entry_t *entry,
					      int share_access,
					      enum share_bypass_modes bypass);

void state_share_anonymous_io_done(cache_entry_t *entry, int share_access);

state_status_t state_nlm_share(cache_entry_t *,
			       int, int, state_owner_t *, bool);

state_status_t state_nlm_unshare(cache_entry_t *entry,
				 int share_access,
				 int share_deny,
				 state_owner_t *owner);

void state_share_wipe(cache_entry_t *entry);
void state_export_unshare_all(void);

/******************************************************************************
 *
 * Async functions
 *
 ******************************************************************************/

/* Schedule Async Work */
state_status_t state_async_schedule(state_async_queue_t *arg);

/* Schedule lock notifications */
state_status_t state_block_schedule(state_block_data_t *block);

/* Signal Async Work */
void signal_async_work(void);

state_status_t state_async_init(void);
state_status_t state_async_shutdown(void);

void grant_blocked_lock_upcall(cache_entry_t *entry, void *owner,
			       fsal_lock_param_t *lock);

void available_blocked_lock_upcall(cache_entry_t *entry, void *owner,
				   fsal_lock_param_t *lock);

void process_blocked_lock_upcall(state_block_data_t *block_data);

/******************************************************************************
 *
 * NFSv4 Recovery functions
 *
 ******************************************************************************/

void nfs4_init_grace(void);
void nfs4_start_grace(nfs_grace_start_t *gsp);
int nfs_in_grace(void);
void nfs4_create_clid_name(nfs_client_record_t *, nfs_client_id_t *,
			   struct svc_req *);
void nfs4_add_clid(nfs_client_id_t *);
void nfs4_rm_clid(const char *, char *, int);
void nfs4_chk_clid(nfs_client_id_t *);
void nfs4_load_recov_clids(nfs_grace_start_t *gsp);
void nfs4_clean_old_recov_dir(char *);
void nfs4_create_recov_dir(void);
void nfs4_record_revoke(nfs_client_id_t *, nfs_fh4 *);
bool nfs4_check_deleg_reclaim(nfs_client_id_t *, nfs_fh4 *);


#endif				/* SAL_FUNCTIONS_H */

/** @} */
