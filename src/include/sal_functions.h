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
#include "fsal.h"

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

nfsstat4 nfs4_Errno_state(state_status_t error);
nfsstat3 nfs3_Errno_state(state_status_t error);

const char *state_owner_type_to_str(state_owner_type_t type);
bool different_owners(state_owner_t *owner1, state_owner_t *owner2);
int display_owner(struct display_buffer *dspbuf, state_owner_t *owner);
void inc_state_owner_ref(state_owner_t *owner);
bool hold_state_owner(state_owner_t *owner);
void dec_state_owner_ref(state_owner_t *owner);
void free_state_owner(state_owner_t *owner);

#define LogStateOwner(note, owner) \
	do { \
		if (isFullDebug(COMPONENT_STATE)) { \
			char str[LOG_BUFF_LEN]; \
			struct display_buffer dspbuf = { \
						sizeof(str), str, str}; \
			display_owner(&dspbuf, owner); \
			LogFullDebug(COMPONENT_STATE, "%s%s", note, str); \
		} \
	} while (0)

state_owner_t *get_state_owner(care_t care, state_owner_t *pkey,
			       state_owner_init_t init_owner, bool_t *isnew);

void state_wipe_file(struct fsal_obj_handle *obj);

#ifdef DEBUG_SAL
void dump_all_owners(void);
#endif

void state_release_export(struct gsh_export *exp);

bool state_unlock_err_ok(state_status_t status);

/**
 * @brief Initialize a state handle
 *
 * @param[in,out] ostate	State handle to initialize
 * @param[in] type	Type of handle
 * @param[in] obj	Object owning handle
 */
static inline void state_hdl_init(struct state_hdl *ostate,
				  object_file_type_t type,
				  struct fsal_obj_handle *obj)
{
	memset(ostate, 0, sizeof(*ostate));
	PTHREAD_RWLOCK_init(&ostate->state_lock, NULL);
	switch (type) {
	case REGULAR_FILE:
		glist_init(&ostate->file.list_of_states);
		glist_init(&ostate->file.layoutrecall_list);
		glist_init(&ostate->file.lock_list);
		glist_init(&ostate->file.nlm_share_list);
		ostate->file.obj = obj;
		break;
	case DIRECTORY:
		glist_init(&ostate->dir.export_roots);
		break;
	default:
		break;
	}
}

/**
 * @brief Clean up a state handle
 *
 * @param[in] state_hdl	State handle to clean up
 */
static inline void state_hdl_cleanup(struct state_hdl *state_hdl)
{
	PTHREAD_RWLOCK_destroy(&state_hdl->state_lock);
}

/*****************************************************************************
 *
 * 9P State functions
 *
 *****************************************************************************/

#ifdef _USE_9P
int compare_9p_owner(state_owner_t *owner1, state_owner_t *owner2);
int compare_9p_owner_key(struct gsh_buffdesc *buff1,
			 struct gsh_buffdesc *buff2);

int display_9p_owner(struct display_buffer *dspbuf, state_owner_t *owner);
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
 * NLM Owner functions
 *
 ******************************************************************************/

void free_nsm_client(state_nsm_client_t *client);

/* These refcount functions must not be called holding the ssc_mutex */
void inc_nsm_client_ref(state_nsm_client_t *client);
void dec_nsm_client_ref(state_nsm_client_t *client);

int display_nsm_client(struct display_buffer *dspbuf, state_nsm_client_t *key);
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

int display_nlm_owner(struct display_buffer *dspbuf, state_owner_t *owner);
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
 * NLM State functions
 *
 ******************************************************************************/

int display_nlm_state(struct display_buffer *dspbuf, state_t *key);
int compare_nlm_state(state_t *state1, state_t *state2);
int Init_nlm_state_hash(void);
void dec_nlm_state_ref(state_t *state);

int get_nlm_state(enum state_type state_type,
		  struct fsal_obj_handle *state_obj,
		  state_owner_t *state_owner,
		  care_t care,
		  uint32_t nsm_state,
		  state_t **pstate);

/******************************************************************************
 *
 * NFS4 Client ID functions
 *
 ******************************************************************************/

nfsstat4 clientid_error_to_nfsstat(clientid_status_t err);

static inline
nfsstat4 clientid_error_to_nfsstat_no_expire(clientid_status_t err)
{
	nfsstat4 rc = clientid_error_to_nfsstat(err);

	if (rc == NFS4ERR_EXPIRED)
		rc = NFS4ERR_STALE_CLIENTID;

	return rc;
}

const char *clientid_error_to_str(clientid_status_t err);

int nfs_Init_client_id(void);

clientid_status_t nfs_client_id_get_unconfirmed(clientid4 clientid,
						nfs_client_id_t **pclient_rec);

clientid_status_t nfs_client_id_get_confirmed(clientid4 clientid,
					      nfs_client_id_t **client_rec);

nfs_client_id_t *create_client_id(clientid4 clientid,
				  nfs_client_record_t *client_record,
				  nfs_client_cred_t *credential,
				  uint32_t minorversion);

clientid_status_t nfs_client_id_insert(nfs_client_id_t *clientid);

int remove_confirmed_client_id(nfs_client_id_t *clientid);
int remove_unconfirmed_client_id(nfs_client_id_t *clientid);

clientid_status_t nfs_client_id_confirm(nfs_client_id_t *clientid,
					log_components_t component);

bool clientid_has_state(nfs_client_id_t *clientid);

bool nfs_client_id_expire(nfs_client_id_t *clientid, bool make_stale);

#define DISPLAY_CLIENTID_SIZE 36
int display_clientid(struct display_buffer *dspbuf, clientid4 clientid);
clientid4 new_clientid(void);
void new_clientid_verifier(char *verf);

int display_client_id_key(struct gsh_buffdesc *buff, char *str);
int display_client_id_val(struct gsh_buffdesc *buff, char *str);

int compare_client_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);

uint64_t client_id_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key);

uint32_t client_id_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key);

int display_client_id_rec(struct display_buffer *dspbuf,
			  nfs_client_id_t *clientid);

#define CLIENTNAME_BUFSIZE (NFS4_OPAQUE_LIMIT * 2 + 1)
int display_clientid_name(struct display_buffer *dspbuf,
			  nfs_client_id_t *clientid);

void free_client_id(nfs_client_id_t *clientid);

void
nfs41_foreach_client_callback(bool(*cb) (nfs_client_id_t *cl, void *state),
			      void *state);

bool client_id_has_state(nfs_client_id_t *clientid);

int32_t inc_client_id_ref(nfs_client_id_t *clientid);
int32_t dec_client_id_ref(nfs_client_id_t *clientid);

int display_client_record(struct display_buffer *dspbuf,
			  nfs_client_record_t *record);

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

int display_session_id(struct display_buffer *dspbuf, char *session_id);
int display_session(struct display_buffer *dspbuf, nfs41_session_t *session);

int32_t _inc_session_ref(nfs41_session_t *session, const char *func, int line);
#define inc_session_ref(s)  _inc_session_ref(s, __func__, __LINE__)
int32_t _dec_session_ref(nfs41_session_t *session, const char *func, int line);
#define dec_session_ref(s)  _dec_session_ref(s, __func__, __LINE__)

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

bool check_session_conn(nfs41_session_t *session,
			compound_data_t *data,
			bool can_associate);

/******************************************************************************
 *
 * NFSv4 Stateid functions
 *
 ******************************************************************************/

#define DISPLAY_STATEID_OTHER_SIZE (DISPLAY_CLIENTID_SIZE + 72)

int display_stateid_other(struct display_buffer *dspbuf, char *other);
int display_stateid(struct display_buffer *dspbuf, state_t *state);

/* 17 is 7 for " seqid=" and 10 for MAX_UINT32 digits */
#define DISPLAY_STATEID4_SIZE (DISPLAY_STATEID_OTHER_SIZE + 17)

int display_stateid4(struct display_buffer *dspbuf, stateid4 *stateid);
void nfs4_BuildStateId_Other(nfs_client_id_t *clientid, char *other);

#define STATEID_NO_SPECIAL 0	/*< No special stateids */
#define STATEID_SPECIAL_ALL_0 2	/*< Allow anonymous */
#define STATEID_SPECIAL_ALL_1 4	/*< Allow read-bypass */
#define STATEID_SPECIAL_CURRENT 8	/*< Allow current */

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

nfsstat4 nfs4_Check_Stateid(stateid4 *stateid, struct fsal_obj_handle *fsal_obj,
			    state_t **state, compound_data_t *data, int flags,
			    seqid4 owner_seqid, bool check_seqid,
			    const char *tag);

void update_stateid(state_t *state, stateid4 *stateid, compound_data_t *data,
		    const char *tag);

int nfs4_Init_state_id(void);

/**
 * @brief Take a reference on state_t
 *
 * @param[in] state The state_t to ref
 */
static inline void inc_state_t_ref(struct state_t *state)
{
	int32_t refcount = atomic_inc_int32_t(&state->state_refcount);

	LogFullDebug(COMPONENT_STATE,
		     "State %p refcount now %"PRIi32,
		     state, refcount);
}

void dec_nfs4_state_ref(struct state_t *state);

/**
 * @brief Relinquish a reference on any State
 *
 * @param[in] state The NLM State to release
 */
static inline void dec_state_t_ref(struct state_t *state)
{
#ifdef _USE_NLM
	if (state->state_type == STATE_TYPE_NLM_LOCK ||
	    state->state_type == STATE_TYPE_NLM_SHARE)
		dec_nlm_state_ref(state);
	else
#endif /* _USE_NLM */
		dec_nfs4_state_ref(state);
}

state_status_t nfs4_State_Set(state_t *state_data);
struct state_t *nfs4_State_Get_Pointer(char *other);
bool nfs4_State_Del(state_t *state);
void nfs_State_PrintAll(void);

struct state_t *nfs4_State_Get_Obj(struct fsal_obj_handle *obj,
				     state_owner_t *owner);

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

void uncache_nfs4_owner(struct state_nfs4_owner_t *nfs4_owner);
void free_nfs4_owner(state_owner_t *owner);
int display_nfs4_owner(struct display_buffer *dspbuf, state_owner_t *owner);
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
				 care_t care, bool_t confirm);

int Init_nfs4_owner(void);

nfsstat4 Process_nfs4_conflict(LOCK4denied *denied,
			       state_owner_t *holder,
			       fsal_lock_param_t *conflict,
			       compound_data_t *data);

void Release_nfs4_denied(LOCK4denied *denied);
void Copy_nfs4_denied(LOCK4denied *denied_dst, LOCK4denied *denied_src);

void Copy_nfs4_state_req(state_owner_t *owner, seqid4 seqid, nfs_argop4 *args,
			 struct fsal_obj_handle *obj, nfs_resop4 *resp,
			 const char *tag);

bool Check_nfs4_seqid(state_owner_t *owner, seqid4 seqid, nfs_argop4 *args,
		      struct fsal_obj_handle *obj, nfs_resop4 *resp,
		      const char *tag);

/**
 * @brief Determine if an NFS v4 owner has state associated with it
 *
 * Note that this function is racy and is only suitable for calling
 * from places that should not have other activity pending against
 * the owner. Currently it is only called from setclientid which should
 * be fine.
 *
 * @param[in] owner The owner of interest
 *
 * @retval true if the owner has state
 */
static inline bool owner_has_state(state_owner_t *owner)
{
	bool live_state;
	struct state_nfs4_owner_t *nfs4_owner = &owner->so_owner.so_nfs4_owner;

	/* If the owner is on the cached owners list, there can't be
	 * active state.
	 */
	if (atomic_fetch_time_t(&nfs4_owner->so_cache_expire) != 0)
		return false;

	PTHREAD_MUTEX_lock(&owner->so_mutex);

	live_state = !glist_empty(&nfs4_owner->so_state_list);

	PTHREAD_MUTEX_unlock(&owner->so_mutex);

	return live_state;
}

/******************************************************************************
 *
 * Lock functions
 *
 ******************************************************************************/

static inline int display_lock_cookie(struct display_buffer *dspbuf,
				      struct gsh_buffdesc *buff)
{
	return display_opaque_value(dspbuf, buff->addr, buff->len);
}

int display_lock_cookie_key(struct gsh_buffdesc *buff, char *str);
int display_lock_cookie_val(struct gsh_buffdesc *buff, char *str);
int compare_lock_cookie_key(struct gsh_buffdesc *buff1,
			    struct gsh_buffdesc *buff2);

uint32_t lock_cookie_value_hash_func(hash_parameter_t *hparam,
				     struct gsh_buffdesc *key);

uint64_t lock_cookie_rbt_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key);

state_status_t state_lock_init(void);

void log_lock(log_components_t component,
	      log_levels_t debug,
	      const char *reason,
	      struct fsal_obj_handle *obj,
	      state_owner_t *owner,
	      fsal_lock_param_t *lock,
	      char *file,
	      int line,
	      char *function);

#define LogLock(component, debug, reason, obj, owner, lock) \
	log_lock(component, debug, reason, obj, owner, lock, \
		 (char *) __FILE__, __LINE__, (char *) __func__)

void dump_all_locks(const char *label);

state_status_t state_add_grant_cookie(struct fsal_obj_handle *obj,
				      void *cookie, int cookie_size,
				      state_lock_entry_t *lock_entry,
				      state_cookie_entry_t **cookie_entry);

state_status_t state_find_grant(void *cookie, int cookie_size,
				state_cookie_entry_t **cookie_entry);

void state_complete_grant(state_cookie_entry_t *cookie_entry);

state_status_t state_cancel_grant(state_cookie_entry_t *cookie_entry);

state_status_t state_release_grant(state_cookie_entry_t *cookie_entry);
state_status_t state_test(struct fsal_obj_handle *obj,
			  state_t *state,
			  state_owner_t *owner,
			  fsal_lock_param_t *lock,
			  /* owner that holds conflicting lock */
			  state_owner_t **holder,
			  /* description of conflicting lock */
			  fsal_lock_param_t *conflict);

state_status_t state_lock(struct fsal_obj_handle *obj,
			  state_owner_t *owner,
			  state_t *state, state_blocking_t blocking,
			  state_block_data_t *block_data,
			  fsal_lock_param_t *lock,
			  /* owner that holds conflicting lock */
			  state_owner_t **holder,
			  /* description of conflicting lock */
			  fsal_lock_param_t *conflict);

state_status_t state_unlock(struct fsal_obj_handle *obj,
			    state_t *state,
			    state_owner_t *owner,
			    bool state_applies,
			    int32_t nsm_state,
			    fsal_lock_param_t *lock);

state_status_t state_cancel(struct fsal_obj_handle *obj,
			    state_owner_t *owner, fsal_lock_param_t *lock);

state_status_t state_nlm_notify(state_nsm_client_t *nsmclient,
				bool state_applies,
				int32_t state);

void state_nfs4_owner_unlock_all(state_owner_t *owner);

void state_export_unlock_all(void);

bool state_lock_wipe(struct state_hdl *hstate);

void cancel_all_nlm_blocked(void);

/******************************************************************************
 *
 * NFSv4 State Management functions
 *
 ******************************************************************************/

#define state_add_impl(o, t, d, i, s, r) \
	_state_add_impl(o, t, d, i, s, r, __func__, __LINE__)
state_status_t _state_add_impl(struct fsal_obj_handle *obj,
			       enum state_type state_type,
			       union state_data *state_data,
			       state_owner_t *owner_input, state_t **state,
			       struct state_refer *refer,
			       const char *func, int line);

#define state_add(o, t, d, i, s, r) \
	_state_add(o, t, d, i, s, r, __func__, __LINE__)
state_status_t _state_add(struct fsal_obj_handle *obj,
			  enum state_type state_type,
			  union state_data *state_data,
			  state_owner_t *owner_input,
			  state_t **state, struct state_refer *refer,
			  const char *func, int line);

state_status_t state_set(state_t *state);

#define state_del_locked(s) _state_del_locked(s, __func__, __LINE__)
void _state_del_locked(state_t *state, const char *func, int line);

void state_del(state_t *state);

/**
 * @brief Get a reference to the obj owning a state
 *
 * @note state_mutex MUST be held
 *
 * @param[in] state	State to get from
 * @return obj handle on success
 */
static inline struct fsal_obj_handle *get_state_obj_ref_locked(state_t *state)
{
	if (state->state_obj) {
		state->state_obj->obj_ops->get_ref(state->state_obj);
	}

	return state->state_obj;
}

/**
 * @brief Get a reference to the obj owning a state
 *
 * Takes state_mutex, so it should not be held.
 *
 * @param[in] state	State to get from
 * @return obj handle on success
 */
static inline struct fsal_obj_handle *get_state_obj_ref(state_t *state)
{
	struct fsal_obj_handle *obj;

	PTHREAD_MUTEX_lock(&state->state_mutex);
	obj = get_state_obj_ref_locked(state);
	PTHREAD_MUTEX_unlock(&state->state_mutex);

	return obj;
}

static inline struct gsh_export *get_state_export_ref(state_t *state)
{
	struct gsh_export *export = NULL;

	PTHREAD_MUTEX_lock(&state->state_mutex);

	if (state->state_export != NULL &&
	    export_ready(state->state_export)) {
		get_gsh_export_ref(state->state_export);
		export = state->state_export;
	}

	PTHREAD_MUTEX_unlock(&state->state_mutex);

	return export;
}

static inline bool state_same_export(state_t *state, struct gsh_export *export)
{
	bool same = false;

	PTHREAD_MUTEX_lock(&state->state_mutex);

	if (state->state_export != NULL)
		same = state->state_export == export;

	PTHREAD_MUTEX_unlock(&state->state_mutex);

	return same;
}

static inline uint16_t state_export_id(state_t *state)
{
	uint16_t export_id = UINT16_MAX;

	PTHREAD_MUTEX_lock(&state->state_mutex);

	if (state->state_export != NULL)
		export_id = state->state_export->export_id;

	PTHREAD_MUTEX_unlock(&state->state_mutex);

	return export_id;
}

static inline state_owner_t *get_state_owner_ref(state_t *state)
{
	state_owner_t *owner = NULL;

	PTHREAD_MUTEX_lock(&state->state_mutex);

	if (state->state_owner != NULL) {
		owner = state->state_owner;
		inc_state_owner_ref(owner);
	}

	PTHREAD_MUTEX_unlock(&state->state_mutex);

	return owner;
}

static inline bool state_owner_confirmed(state_t *state)
{
	bool confirmed = false;

	PTHREAD_MUTEX_lock(&state->state_mutex);

	if (state->state_owner != NULL) {
		confirmed =
			state->state_owner->so_owner.so_nfs4_owner.so_confirmed;
	}

	PTHREAD_MUTEX_unlock(&state->state_mutex);

	return confirmed;
}

static inline bool state_same_owner(state_t *state, state_owner_t *owner)
{
	bool same = false;

	PTHREAD_MUTEX_lock(&state->state_mutex);

	if (state->state_owner != NULL)
		same = state->state_owner == owner;

	PTHREAD_MUTEX_unlock(&state->state_mutex);

	return same;
}

bool get_state_obj_export_owner_refs(state_t *state,
				     struct fsal_obj_handle **obj,
				       struct gsh_export **export,
				       state_owner_t **owner);

void state_nfs4_state_wipe(struct state_hdl *ostate);

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

state_status_t acquire_lease_lock(struct state_hdl *ostate,
				  state_owner_t *owner,
				  state_t *state);
state_status_t release_lease_lock(struct fsal_obj_handle *obj, state_t *state);

bool init_deleg_heuristics(struct fsal_obj_handle *obj);
bool deleg_supported(struct fsal_obj_handle *obj,
		     struct fsal_export *fsal_export,
		     struct export_perms *export_perms, uint32_t share_access);
bool can_we_grant_deleg(struct state_hdl *ostate, state_t *open_state);
bool should_we_grant_deleg(struct state_hdl *ostate, nfs_client_id_t *client,
			   state_t *open_state, OPEN4args *args,
			   OPEN4resok *resok, state_owner_t *owner,
			   bool *prerecall);
void init_new_deleg_state(union state_data *deleg_state,
			  open_delegation_type4 sd_type,
			  nfs_client_id_t *clientid);

void deleg_heuristics_recall(struct fsal_obj_handle *obj,
			     state_owner_t *owner,
			     struct state_t *deleg);
void get_deleg_perm(nfsace4 *permissions, open_delegation_type4 type);
void update_delegation_stats(struct state_hdl *ostate,
			     state_owner_t *owner);
state_status_t delegrecall_impl(struct fsal_obj_handle *obj);
nfsstat4 deleg_revoke(struct fsal_obj_handle *obj, struct state_t *deleg_state);
void state_deleg_revoke(struct fsal_obj_handle *obj, state_t *state);
bool state_deleg_conflict(struct fsal_obj_handle *obj, bool write);

/******************************************************************************
 *
 * Layout functions
 *
 ******************************************************************************/

state_status_t state_add_segment(state_t *state, struct pnfs_segment *segment,
				 void *fsal_data, bool return_on_close);

state_status_t state_delete_segment(state_layout_segment_t *segment);
state_status_t state_lookup_layout_state(struct fsal_obj_handle *obj,
					 state_owner_t *owner,
					 layouttype4 type, state_t **state);
void revoke_owner_layouts(state_owner_t *client_owner);


/******************************************************************************
 *
 * Share functions
 *
 ******************************************************************************/

#define OPEN4_SHARE_ACCESS_NONE 0

enum share_bypass_modes {
	SHARE_BYPASS_NONE,
	SHARE_BYPASS_READ,
	SHARE_BYPASS_V3_WRITE
};

state_status_t state_nlm_share(struct fsal_obj_handle *obj,
			       int share_access,
			       int share_deny,
			       state_owner_t *owner,
			       state_t *state,
			       bool reclaim,
			       bool unshare);

void state_share_wipe(struct state_hdl *ostate);
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

void grant_blocked_lock_upcall(struct fsal_obj_handle *obj, void *owner,
			       fsal_lock_param_t *lock);

void available_blocked_lock_upcall(struct fsal_obj_handle *obj, void *owner,
				   fsal_lock_param_t *lock);

void process_blocked_lock_upcall(state_block_data_t *block_data);

void blocked_lock_polling(struct fridgethr_context *ctx);

/******************************************************************************
 *
 * NFSv4 Recovery functions
 *
 ******************************************************************************/

/* Grace period handling */
extern int32_t reclaim_completes; /* atomic */
void nfs_start_grace(nfs_grace_start_t *gsp);
void nfs_end_grace(void);
bool nfs_in_grace(void);
void nfs_maybe_start_grace(void);
bool nfs_grace_is_member(void);
void nfs_try_lift_grace(void);
void nfs_wait_for_grace_enforcement(void);
void nfs_notify_grace_waiters(void);

/* v4 Client stable-storage database management */
void nfs4_add_clid(nfs_client_id_t *);
void nfs4_rm_clid(nfs_client_id_t *);
void nfs4_chk_clid(nfs_client_id_t *);

/* Delegation revocation tracking */
bool nfs4_check_deleg_reclaim(nfs_client_id_t *, nfs_fh4 *);
void nfs4_record_revoke(nfs_client_id_t *, nfs_fh4 *);

/* Recovery backend management */
int nfs4_recovery_init(void);
void nfs4_recovery_shutdown(void);

/**
 * @brief Check to see if an object is a junction
 *
 * @param[in] obj	Object to check
 * @return true if junction, false otherwise
 */
static inline bool obj_is_junction(struct fsal_obj_handle *obj)
{
	bool res = false;

	if (obj->type != DIRECTORY)
		return false;

	PTHREAD_RWLOCK_rdlock(&obj->state_hdl->state_lock);

	if ((obj->state_hdl->dir.junction_export != NULL ||
	     atomic_fetch_int32_t(&obj->state_hdl->dir.exp_root_refcount) != 0))
		res = true;
	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

	return res;
}

typedef clid_entry_t * (*add_clid_entry_hook)(char *);
typedef rdel_fh_t * (*add_rfh_entry_hook)(clid_entry_t *, char *);

struct nfs4_recovery_backend {
	int (*recovery_init)(void);
	void (*recovery_shutdown)(void);
	void (*recovery_read_clids)(nfs_grace_start_t *gsp,
				    add_clid_entry_hook add_clid,
				    add_rfh_entry_hook add_rfh);
	void (*add_clid)(nfs_client_id_t *);
	void (*rm_clid)(nfs_client_id_t *);
	void (*add_revoke_fh)(nfs_client_id_t *, nfs_fh4 *);
	void (*end_grace)(void);
	void (*maybe_start_grace)(void);
	bool (*try_lift_grace)(void);
	void (*set_enforcing)(void);
	bool (*grace_enforcing)(void);
	bool (*is_member)(void);
};

void fs_backend_init(struct nfs4_recovery_backend **);
void fs_ng_backend_init(struct nfs4_recovery_backend **);
#ifdef USE_RADOS_RECOV
int rados_kv_set_param_from_conf(config_file_t, struct config_error_type *);
void rados_kv_backend_init(struct nfs4_recovery_backend **);
void rados_ng_backend_init(struct nfs4_recovery_backend **);
void rados_cluster_backend_init(struct nfs4_recovery_backend **backend);
#endif

#endif				/* SAL_FUNCTIONS_H */

/** @} */
