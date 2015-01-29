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
 * @file sal_data.h
 * @brief Data structures for state management.
 */

#ifndef SAL_DATA_H
#define SAL_DATA_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>		/* For having MAXNAMLEN */

#include "abstract_atomic.h"
#include "abstract_mem.h"
#include "hashtable.h"
#include "fsal_pnfs.h"
#include "config_parsing.h"

#ifdef _USE_9P
/* define u32 and related types independent of SAL and 9P */
#include "9p_types.h"
#endif				/* _USE_9P */

/**
** Forward declarations to avoid circular dependency conflicts
*/

#include "gsh_status.h"

typedef struct nfs_client_id_t		nfs_client_id_t;
typedef struct nfs41_session		nfs41_session_t;

/**
** Consolidated circular dependencies
*/

#include "nfs_proto_data.h"

/**
 ** Forward references to types
 */
typedef struct nfs_argop4_state		nfs_argop4_state;
typedef struct nfs_client_record_t	nfs_client_record_t;
typedef struct state_async_queue_t	state_async_queue_t;
typedef struct state_block_data_t	state_block_data_t;
typedef struct state_cookie_entry_t	state_cookie_entry_t;
typedef struct state_lock_entry_t	state_lock_entry_t;
typedef struct state_nfs4_owner_t	state_nfs4_owner_t;
typedef struct state_nlm_client_t	state_nlm_client_t;
typedef struct state_owner_t		state_owner_t;
typedef struct state_t			state_t;

/**
 * @brief Number of errors before giving up on recovery
 *
 * We set a maximum because the recovery routines need to terminate at
 * some point.
 */
#define STATE_ERR_MAX 100

/**
 * @brief Indicate that lock extends to the entire range of the file
 *
 * This is true no matter what the beginning of the lock range is.
 */
#define STATE_LOCK_OFFSET_EOF 0xFFFFFFFFFFFFFFFFLL

extern struct fridgethr *state_async_fridge;

/*****************************************************************************
 *
 * NFSv4.1 Session data
 *
 *****************************************************************************/

extern pool_t *nfs41_session_pool;

/**
 * @param Session ID hash
 */

extern hash_table_t *ht_session_id;

/**
 * @brief Number of forechannel slots in a session
 *
 * This is also the maximum number of backchannel slots we'll use,
 * even if the client offers more.
 */
#define NFS41_NB_SLOTS 3

/**
 * @brief Members in the slot table
 */

typedef struct nfs41_session_slot__ {
	sequenceid4 sequence;	/*< Sequence number of this operation */
	pthread_mutex_t lock;	/*< Lock on the slot */
	struct COMPOUND4res_extended cached_result;	/*< NFv41: pointer to
							   cached RPC result in
							   a session's slot */
	unsigned int cache_used;	/*< If we cached the result */
} nfs41_session_slot_t;

/**
 * @brief Bookkeeping for callback slots on the client
 */

typedef struct nfs41_cb_session_slot {
	sequenceid4 sequence;	/*< Sequence number of last call */
	bool in_use;		/*< Set if a call if in progress on this slot */
} nfs41_cb_session_slot_t;

/**
 * Flag indicating that the backchannel is up
 */
static const uint32_t session_bc_up = 0x01;
/**
 * Flag indicating that there is an irrecoverable fault with the
 * backchannel
 */
static const uint32_t session_bc_fault = 0x02;

/**
 * @brief Structure representing an NFSv4.1 session
 */

struct nfs41_session {
	char session_id[NFS4_SESSIONID_SIZE];	/*< Session ID */
	int32_t refcount;
	clientid4 clientid;	/*< Clientid owning this session */
	nfs_client_id_t *clientid_record;	/*< Client record
						   correspinding to ID */
	struct glist_head session_link;	/*< Link in the list of
					   sessions for this
					   clientid */
	uint32_t flags;		/*< Flags pertaining to this session */
	SVCXPRT *xprt;		/*< Referenced pointer to transport */

	channel_attrs4 fore_channel_attrs;	/*< Fore-channel attributes */
	nfs41_session_slot_t slots[NFS41_NB_SLOTS];	/*< Slot table */

	channel_attrs4 back_channel_attrs;	/*< Back-channel attributes */
	nfs41_cb_session_slot_t cb_slots[NFS41_NB_SLOTS];	/*< Callback
								   Slot table */
	uint32_t cb_program;	/*< Callback program ID */
	struct rpc_call_channel cb_chan;	/*< Back channel */
	pthread_mutex_t cb_mutex;	/*< Protects the cb slot table,
					   when searching for a free slot */
	pthread_cond_t cb_cond;	/*< Condition variable on which we
				   wait if the slot table is full
				   and on which we signal when we
				   free an entry. */
};

/**
 * @brief Track the call that creates a state, for NFSv4.1.
 */

struct state_refer {
	sessionid4 session;	/*< The session of the creating call. */
	sequenceid4 sequence;	/*< The sequence ID of the creating
				   call. */
	slotid4 slot;		/*< The slot ID of the creating call. */
};

/******************************************************************************
 *
 * NFSv4 Recovery data
 *
 *****************************************************************************/

/**
 * @brief Grace period control structure
 *
 * This could be expanded to implement grace instances, where a new
 * grace period is started for every failover.  for now keep it
 * simple, just a global used by all clients.
 */
typedef struct grace {
	pthread_mutex_t g_mutex;	/*< Mutex */
	time_t g_start;		/*< Start of grace period */
	time_t g_duration;	/*< Duration of grace period */
	struct glist_head g_clid_list;	/*< Clients */
} grace_t;

/**
 * @brief Revoked filehandle list
 */
typedef struct rdel_fh {
	struct glist_head rdfh_list;
	char *rdfh_handle_str;
} rdel_fh_t;

/**
 * @brief A client entry
 */
typedef struct clid_entry {
	struct glist_head cl_list;	/*< Link in the list */
	struct glist_head cl_rfh_list;
	char cl_name[PATH_MAX];	/*< Client name */
} clid_entry_t;

extern char v4_old_dir[PATH_MAX];
extern char v4_recov_dir[PATH_MAX];

/******************************************************************************
 *
 * NFSv4 State data
 *
 *****************************************************************************/

extern hash_table_t *ht_state_id;

/**
 * @brief Type of state
 */

enum state_type {
	STATE_TYPE_NONE = 0,
	STATE_TYPE_SHARE = 1,
	STATE_TYPE_DELEG = 2,
	STATE_TYPE_LOCK = 4,
	STATE_TYPE_LAYOUT = 5
};

/**
 * @brief Data for a share reservation/open
 */

struct state_share {
	struct glist_head share_lockstates;	/*< Lock states for this
						   open state */
	unsigned int share_access;	/*< The NFSv4 Share Access state */
	unsigned int share_deny;	/*< The NFSv4 Share Deny state */
	unsigned int share_access_prev;	/*< Previous share access state */
	unsigned int share_deny_prev;	/*< Previous share deny state   */
};

/**
 * @brief Data for a set of locks
 */

struct state_lock {
	struct glist_head state_locklist;	/*< List of locks owned by
						   this stateid */
	struct glist_head state_sharelist;	/*< List of states related
						   to a share */
	state_t *openstate;	/*< The related open-state */
};

/**
 * @brief Stats for client-file delegation heuristics
 */

/* @brief Per client, per file stats */
struct cf_deleg_stats {
	time_t cfd_rs_time;                   /* time when the client responsed
						 NFS4_OK for a recall. */
	time_t cfd_r_time;               /* time of the recall attempt */
};

/**
 * @brief States of a delegation
 *
 * Different states a delegation can be in during its lifetime
 */
enum deleg_state {
	DELEG_GRANTED =  1,	/* Granted               */
	DELEG_RECALL_WIP,	/* Recall in progress    */
};

/**
 * @brief Data for a delegation
 */

struct state_deleg {
	open_delegation_type4 sd_type;
	time_t sd_grant_time;               /* time of successful delegation */
	enum deleg_state sd_state;
	uint16_t dd_export_id;
	struct cf_deleg_stats sd_clfile_stats;  /* client specific */
};

/**
 * @brief Data for a set of layouts of a given type
 */

struct state_layout {
	struct glist_head state_segments;	/*< List of segments */
	layouttype4 state_layout_type;	/*< The type of layout this state
					   represents */
	uint32_t granting;	/*< Number of LAYOUTGETs in progress */
	bool state_return_on_close;	/*< Whether this layout should be
					   returned on last close. */
};

/**
 * @brief Type specific state data
 */

union state_data {
	struct state_share share;
	struct state_lock lock;
	struct state_deleg deleg;
	struct state_layout layout;
	uint32_t io_advise;
};

/**
 * @brief The number of bytes in the stateid.other
 *
 * The value 12 is fixed by RFC 3530/RFC 5661.
 */
#define OTHERSIZE 12

extern char all_zero[OTHERSIZE];
extern char all_ones[OTHERSIZE];

/**
 * @brief Structure representing a single NFSv4 state
 *
 * Each state is identified by stateid and represents some resource or
 * set of resources.
 */

struct state_t {
	struct glist_head state_list;	/*< List of states on a file */
	struct glist_head state_owner_list;  /*< List of states for an owner */
	struct glist_head state_export_list; /*< List of states on the same
						 export */
#ifdef DEBUG_SAL
	struct glist_head state_list_all;    /*< Global list of all stateids */
#endif
	struct gsh_export *state_export; /*< Export this entry belongs to */
	state_owner_t *state_owner;	/*< State Owner related to this state */
	cache_entry_t *state_entry;	/*< Related entry */
	union state_data state_data;
	enum state_type state_type;
	u_int32_t state_seqid;		/*< The NFSv4 Sequence id */
	int32_t state_refcount;		/*< Refcount for state_t objects */
	char stateid_other[OTHERSIZE];	/*< "Other" part of state id,
					   used as hash key */
	struct state_refer state_refer;	/*< For NFSv4.1, track the
					   call that created a
					   state. */
};

/* Macros to compare and copy state_t to a struct stateid4 */
#define SAME_STATEID(id4, state) \
	((id4)->seqid == (state)->state_seqid && \
	memcmp((id4)->other, (state)->stateid_other, OTHERSIZE) == 0)

#define COPY_STATEID(id4, state) \
	do { \
		(id4)->seqid = (state)->state_seqid; \
		(void)memcpy((id4)->other, (state)->stateid_other, OTHERSIZE); \
	} while (0)

/*****************************************************************************
 *
 * NFS Owner data
 *
 *****************************************************************************/

typedef void (state_owner_init_t) (state_owner_t *powner);

extern hash_table_t *ht_nlm_owner;
#ifdef _USE_9P
extern hash_table_t *ht_9p_owner;
#endif
extern hash_table_t *ht_nfs4_owner;

/**
 * @brief A structure identifying the owner of an NFSv4 open or lock state
 *
 * This serves as the key to the NFSv4 owner table, and is generated
 * from either an open or lock owner.
 */

typedef struct state_nfs4_owner_name_t {
	unsigned int son_owner_len;
	char *son_owner_val;
} state_nfs4_owner_name_t;

/**
 * @brief The type of entity responsible for a state
 *
 * For NLM and 9P, one kind of owner owns every kind of state.  For
 * NFSv4.1 open owners and lock owners are in disjoint spaces, and all
 * non-open and non-lock states are associated with a given client.
 */

typedef enum state_owner_type_t {
	STATE_LOCK_OWNER_UNKNOWN,	/*< Unknown */
	STATE_LOCK_OWNER_NLM,	/*< An NLM client */
#ifdef _USE_9P
	STATE_LOCK_OWNER_9P,	/*< A 9P client */
#endif
	STATE_OPEN_OWNER_NFSV4,	/*< An NFSv4 owner of an open */
	STATE_LOCK_OWNER_NFSV4,	/*< An NFSv4 owner of a set of locks */
	STATE_CLIENTID_OWNER_NFSV4	/*< An NFSv4 client, owns all
					   states but opens and locks. */
} state_owner_type_t;

/**
 * @brief Specifies interest in a client for NLN/NSM.
 */

typedef enum care_t {
	CARE_NOT,		/*< Do not care about client's status */
	CARE_ALWAYS,		/*< Always care about client's status */
	CARE_NO_MONITOR,	/*< Care, but will not actively monitor */
	CARE_MONITOR		/*< Will actively monitor client status */
} care_t;

extern hash_table_t *ht_nsm_client;
extern hash_table_t *ht_nlm_client;

/**
 * @brief NSM (rpc.statd) state for a given client.
 */

typedef struct state_nsm_client_t {
	pthread_mutex_t ssc_mutex;	/*< Mutex protecting this
					   structure */
	struct glist_head ssc_lock_list;	/*< All locks held by client */
	struct glist_head ssc_share_list;	/*< All share reservations */
	sockaddr_t ssc_client_addr;	/*< Network address of client */
	int32_t ssc_refcount;	/*< Reference count to protect
				   structure */
	int32_t ssc_monitored;	/*< If this client is actively
				   monitored */
	int32_t ssc_nlm_caller_name_len;	/*< Length of identifier */
	char *ssc_nlm_caller_name;	/*< Client identifier */
} state_nsm_client_t;

/**
 * @brief Represent a single client accessing us through NLM
 */

struct state_nlm_client_t {
	state_nsm_client_t *slc_nsm_client;	/*< Related NSM client */
	xprt_type_t slc_client_type;	/*< The transport type to this
					   client */
	int32_t slc_refcount;	/*< Reference count for disposal */
	struct sockaddr_storage slc_server_addr; /*< local addr when request is
						     made */
	int32_t slc_nlm_caller_name_len;	/*< Length of client name */
	char *slc_nlm_caller_name;	/*< Client name */
	CLIENT *slc_callback_clnt;	/*< Callback for blocking locks */
	AUTH *slc_callback_auth;	/*< Authentication for callback */
};

/**
 * @brief Owner of an NLM lock or share.
 */

typedef struct state_nlm_owner_t {
	state_nlm_client_t *so_client;	/*< Structure for this client */
	int32_t so_nlm_svid;	/*< Owner within client */
	struct glist_head so_nlm_shares;	/*< Share reservations */
} state_nlm_owner_t;

/**
 * @brief 9P lock owner
 */

#ifdef _USE_9P
typedef struct state_9p_owner_t {
	u32 proc_id;		/*< PID on the client */
	struct sockaddr_storage client_addr;	/*< Network address of client */
} state_9p_owner_t;
#endif				/* _USE_9P */

/**
 * @brief Share and lock operations for NFSv4.0
 *
 * This structure saves the arguments to the most recent operation on
 * an open or lock owner associated with an NFSv4.0 client.  This is
 * part of the At-Most Once semantics and not used under NFSv4.1.
 */

struct nfs_argop4_state {
	nfs_opnum4 argop;	/*< Operation being saved */
	union {
		CLOSE4args opclose;	/*< CLOSE */
		LOCK4args oplock;	/*< LOCK */
		LOCKU4args oplocku;	/*< LOCKU */
		OPEN4args opopen;	/*< OPEN */
		OPEN_CONFIRM4args opopen_confirm;	/*< OPEN_CONFIRM */
		OPEN_DOWNGRADE4args opopen_downgrade;	/*< OPEN_DOWNGRADE */
	} nfs_argop4_u;
};

/**
 * @brief A structure supporting all NFSv4 owners.
 */

struct state_nfs4_owner_t {
	clientid4 so_clientid;	/*< Owning clientid */
	nfs_client_id_t *so_clientrec;	/*< Owning client id record */
	bool so_confirmed;	/*< Confirmation (NFSv4.0 only) */
	seqid4 so_seqid;	/*< Seqid for serialization of operations on
				   owner (NFSv4.0 only) */
	nfs_argop4_state so_args;	/*< Saved args */
	void *so_last_entry;	/*< Last file operated on by this state owner
				 *  we don't keep a reference so this is a
				 *  void * to prevent it's dereferencing because
				 *  the pointer might become invalid if cache
				 *  inode flushes the entry out. But it sufices
				 *  for the purposes of detecting replayed
				 *  operations.
				 */
	nfs_resop4 so_resp;	/*< Saved response */
	state_owner_t *so_related_owner;	/*< For lock-owners, the
						   open-owner under which
						   the lock owner was
						   created */
	struct glist_head so_state_list; /*< States owned by this owner */
	struct glist_head so_perclient;  /*< open owner entry to be
					   linked to client */
};

/**
 * @brief General state owner
 *
 * This structure encodes the owner of any state, protocol specific
 * information is contained within the union.
 */

struct state_owner_t {
	state_owner_type_t so_type;	/*< Owner type */
	struct glist_head so_lock_list;	/*< Locks for this owner */
#ifdef DEBUG_SAL
	struct glist_head so_all_owners; /**< Global list of all state owners */
#endif				/* _DEBUG_MEMLEAKS */
	pthread_mutex_t so_mutex;	/*< Mutex on this owner */
	int so_refcount;	/*< Reference count for lifecyce management */
	int so_owner_len;	/*< Length of owner name */
	char *so_owner_val;	/*< Owner name */
	union {
		state_nfs4_owner_t so_nfs4_owner; /*< All NFSv4 state owners */
		state_nlm_owner_t so_nlm_owner;	/*< NLM lock and share
						   owners */
#ifdef _USE_9P
		state_9p_owner_t so_9p_owner;	/*< 9P lock owners */
#endif
	} so_owner;
};

/* Test if the lock owner type is 9P */
#ifdef _USE_9P
#define LOCK_OWNER_9P(owner) ((owner)->so_type == STATE_LOCK_OWNER_9P)
#else
#define LOCK_OWNER_9P(owner) (0)
#endif

extern state_owner_t unknown_owner;

/******************************************************************************
 *
 * NFSv4 Clientid data
 *
 *****************************************************************************/

/**
 * @brief State of a clientid record.
 */

typedef enum nfs_clientid_confirm_state__ {
	UNCONFIRMED_CLIENT_ID,
	CONFIRMED_CLIENT_ID,
	EXPIRED_CLIENT_ID,
	STALE_CLIENT_ID
} nfs_clientid_confirm_state_t;

/**
 * @brief Errors from the clientid functions
 */

typedef enum clientid_status {
	CLIENT_ID_SUCCESS = 0,	/*< Success */
	CLIENT_ID_INSERT_MALLOC_ERROR,	/*< Unable to allocate memory */
	CLIENT_ID_INVALID_ARGUMENT,	/*< Invalid argument */
	CLIENT_ID_EXPIRED,	/*< requested client id expired */
	CLIENT_ID_STALE		/*< requested client id stale */
} clientid_status_t;

/**
 * @brief Record associated with a clientid
 *
 * This record holds Ganesha's state on an NFSv4.0 client.
 */

struct nfs_client_id_t {
	clientid4 cid_clientid;	/*< The clientid */
	verifier4 cid_verifier;	/*< Known verifier */
	verifier4 cid_incoming_verifier; /*< Most recently supplied verifier */
	time_t cid_last_renew;	/*< Time of last renewal */
	nfs_clientid_confirm_state_t cid_confirmed; /*< Confirm/expire state */
	nfs_client_cred_t cid_credential;	/*< Client credential */
	sockaddr_t cid_client_addr;	/*< Network address of
					   client. @note This only really
					   makes sense for NFSv4.0, and
					   even then it's dubious.
					   NFSv4.1 explicitly allows
					   multiple addresses per
					   session and multiple sessions
					   per client. */
	int cid_allow_reclaim;	/*< Whether this client can still
				   reclaim state */
	char *cid_recov_dir;	/*< Recovery directory */
	nfs_client_record_t *cid_client_record;	/*< Record for managing
						   confirmation and
						   replacement */
	struct glist_head cid_openowners;	/*< All open owners */
	struct glist_head cid_lockowners;	/*< All lock owners */
	pthread_mutex_t cid_mutex;	/*< Mutex for this client */
	union {
		struct {
			/** Callback channel */
			struct rpc_call_channel cb_chan;
			/** Decoded address */
			gsh_addr_t cb_addr;
			/** Callback identifier */
			uint32_t cb_callback_ident;
			/** Universal address */
			char cb_client_r_addr[SOCK_NAME_MAX + 1];
			/** Callback program */
			uint32_t cb_program;
			bool cb_chan_down;    /* Callback channel state */
		} v40;		/*< v4.0 callback information */
		struct {
			bool cid_reclaim_complete; /*< reclaim complete
						       indication */
			/** All sessions */
			struct glist_head cb_session_list;
		} v41;		/*< v4.1 callback information */
	} cid_cb;		/*< Version specific callback information */
	time_t first_path_down_resp_time;  /* Time when the server first sent
					       NFS4ERR_CB_PATH_DOWN */
	char cid_server_owner[MAXNAMLEN + 1];	/*< Server owner.
						 * @note Why is this
						 * stored per-client? */
	char cid_server_scope[MAXNAMLEN + 1];	/*< Server scope */
	unsigned int cid_nb_session;	/*< Number of sessions stored */
	nfs41_session_slot_t cid_create_session_slot; /*< Cached response to
							  last CREATE_SESSION */
	unsigned cid_create_session_sequence;	/*< Sequence number for session
						   creation. */
	state_owner_t cid_owner;	/*< Owner for per-client state */
	int32_t cid_refcount;	/*< Reference count for lifecycle */
	int cid_lease_reservations;	/*< Counted lease reservations, to spare
					   this clientid from the reaper */
	uint32_t cid_minorversion;
	uint32_t cid_stateid_counter;

	uint32_t curr_deleg_grants; /* current num of delegations owned by
				       this client */
	uint32_t num_revokes;       /* Num revokes for the client */
	struct gsh_client *gsh_client; /* for client specific statistics. */
};

/**
 * @brief Client owner record for verification and replacement
 *
 * @note The cr_mutex should never be acquired while holding a
 * cid_mutex.
 */

struct nfs_client_record_t {
	int32_t cr_refcount;	/*< Reference count for lifecycle */
	pthread_mutex_t cr_mutex;	/*< Mutex protecting this structure */
	nfs_client_id_t *cr_confirmed_rec; /*< The confirmed record associated
					       with this owner (if there is
					       one.) */
	nfs_client_id_t *cr_unconfirmed_rec;	/*< The unconfirmed record
						   associated with this
						   client name, if there is
						   one. */
	uint32_t cr_server_addr; /*< Server IP address the client connected to
				  */
	uint32_t cr_pnfs_flags;  /*< pNFS flags.  RFC 5661 allows us
				     to treat identical co_owners with
				     different pNFS flags as
				     disjoint. Linux client stupidity
				     forces us to do so. */
	int cr_client_val_len;  /*< Length of owner */
	char cr_client_val[];   /*< Suplied co_owner */
};

extern pool_t *client_id_pool;

extern hash_table_t *ht_client_record;
extern hash_table_t *ht_confirmed_client_id;
extern hash_table_t *ht_unconfirmed_client_id;

/******************************************************************************
 *
 * Lock Data
 *
 *****************************************************************************/

/**
 * @brief Blocking lock type and state
 */

typedef enum state_blocking_t {
	STATE_NON_BLOCKING,
	STATE_NLM_BLOCKING,
	STATE_NFSV4_BLOCKING,
	STATE_GRANTING,
	STATE_CANCELED
} state_blocking_t;

/**
 * @brief Grant callback
 *
 * The granted call back is responsible for acquiring a reference to
 * the lock entry if needed.
 */
typedef state_status_t(*granted_callback_t) (cache_entry_t *entry,
					     state_lock_entry_t *lock_entry);

/**
 * @brief NLM specific Blocking lock data
 */

typedef struct state_nlm_block_data_t {
	netobj sbd_nlm_fh;	/*< Filehandle */
	char sbd_nlm_fh_buf[MAX_NETOBJ_SZ];	/*< Statically allocated
						   FH buffer */
} state_nlm_block_data_t;

extern struct glist_head state_blocked_locks;

/**
 * @brief Grant types
 */
typedef enum state_grant_type_t {
	STATE_GRANT_NONE,	/*< No grant */
	STATE_GRANT_INTERNAL,	/*< Grant generated by SAL */
	STATE_GRANT_FSAL,	/*< FSAL granting lock */
	STATE_GRANT_FSAL_AVAILABLE	/*< FSAL signalling lock availability */
} state_grant_type_t;

/**
 * @brief Blocking lock data
 */
struct state_block_data_t {
	struct glist_head sbd_list;	/*< Lost of blocking locks */
	state_grant_type_t sbd_grant_type;	/*< Type of grant */
	granted_callback_t sbd_granted_callback; /*< Callback for grant */
	state_cookie_entry_t *sbd_blocked_cookie; /*< Blocking lock cookie */
	state_lock_entry_t *sbd_lock_entry;	/*< Details of lock */
	union {
		state_nlm_block_data_t sbd_nlm; /*< NLM block data */
		void *sbd_v4;			/*< NFSv4 block data */
	} sbd_prot;
};

typedef enum lock_type_t {
	POSIX_LOCK,		/*< Byte-range lock */
	LEASE_LOCK		/*< Delegation */
} lock_type_t;

struct state_lock_entry_t {
	struct glist_head sle_list;	/*< Locks on this file */
	struct glist_head sle_owner_locks; /*< Link on the owner lock list */
	struct glist_head sle_locks;	/*< Locks on this state/client */
#ifdef DEBUG_SAL
	struct glist_head sle_all_locks; /*< Link on the global lock list */
#endif				/* DEBUG_SAL */
	struct glist_head sle_export_locks;	/*< Link on the export
						   lock list */
	struct gsh_export *sle_export;
	cache_entry_t *sle_entry;	/*< File being locked */
	state_block_data_t *sle_block_data;	/*< Blocking lock data */
	state_owner_t *sle_owner;	/* Lock owner */
	state_t *sle_state;	/*< Associated lock state */
	state_blocking_t sle_blocked;	/*< Blocking status */
	int sle_ref_count;	/*< Reference count */
	fsal_lock_param_t sle_lock;	/*< Lock description */
	pthread_mutex_t sle_mutex;	/*< Mutex to protect the structure */
	lock_type_t sle_type;	/*< Type of lock */
};

/**
 * @brief Description of a layout segment
 */

typedef struct state_layout_segment {
	struct glist_head sls_state_segments;	/*< Link on the per-layout-state
						   segment list */
	state_t *sls_state;	/*< Associated layout state */
	struct pnfs_segment sls_segment;	/*< Segment descriptor */
	void *sls_fsal_data;	/*< FSAL data */
	pthread_mutex_t sls_mutex;	/*< Mutex */
} state_layout_segment_t;

/**
 * @brief An entry in a list of states affected by a recall
 *
 * We make a non-intrusive list so that a state can be affected by any
 * number of recalls.
 */

struct recall_state_list {
	struct glist_head link;	/*< Link to the next layout state in the
				   queue */
	state_t *state;		/*< State on which to recall */
};

/**
 * @brief Processing for a LAYOUTRECALL from the FSAL.
 */

struct state_layout_recall_file {
	struct glist_head entry_link;	/*< List of recalls on a file */
	cache_entry_t *entry;	/*< Related cache entry */
	layouttype4 type;	/*< Type of layout being recalled */
	struct pnfs_segment segment;	/*< Segment to recall */
	struct glist_head state_list;	/*< List of states affected by this
					   layout */
	void *recall_cookie;	/*< Cookie returned to FSAL on return of last
				   segment satisfying the layout */
};

#define sle_client_locks sle_locks
#define sle_state_locks  sle_locks

/**
 * @brief Blocking lock cookie entry
 *
 * Management of lce_refcount
 * ==========================
 *
 * * state_add_grant_cookie
 *
 *   creates a reference.
 *
 * * state_find_grant
 *
 *   gets a reference
 *
 * * state_complete_grant
 *
 *   always releases 1 reference it releases a 2nd reference when the
 *   call instance is the first to actually try and complete the grant
 *
 * * state_release_grant
 *
 *   always releases 1 reference it releases a 2nd reference when the
 *   call instance is the first to actually try and release the grant
 *
 * * state_cancel_grant
 *
 *   calls cancel_blocked_lock, which will release the initial
 *   reference
 *
 * * cancel_blocked_lock
 *
 *   releases 1 reference if cookie exists called by
 *   state_cancel_grant also called by unlock, cancel, sm_notify
 */

struct state_cookie_entry_t {
	cache_entry_t *sce_entry;	/*< Associated file */
	state_lock_entry_t *sce_lock_entry;	/*< Associated lock */
	void *sce_cookie;	/*< Cookie data */
	int sce_cookie_size;	/*< Length of cookie */
};

/*
 * Structures for state async processing
 *
 */

/**
 * @brief Asynchronous state function
 */
typedef void (state_async_func_t) (state_async_queue_t *arg);

/**
 * @brief Data for asynchronous NLM calls
 */
typedef struct state_nlm_async_data_t {
	state_nlm_client_t *nlm_async_host;	/*< The client */
	void *nlm_async_key;	/*< Identifying key */
	union {
		nfs_res_t nlm_async_res;	/*< Asynchronous response */
		nlm4_testargs nlm_async_grant;	/*< Arguments for grant */
	} nlm_async_args;
} state_nlm_async_data_t;

/**
 * @brief Asynchronous blocking lock data
 */

typedef struct state_async_block_data_t {
	state_lock_entry_t *state_async_lock_entry;	/*< Associated lock */
} state_async_block_data_t;

/**
 * @brief Queue of asynchronous events
 */

struct state_async_queue_t {
	struct glist_head state_async_glist;	/*< List of events */
	state_async_func_t *state_async_func;	/*< Processing function */
	union {
		state_nlm_async_data_t state_nlm_async_data;	/*< Data for
								   operation */
		void *state_no_data;	/*< Dummy pointer */
	} state_async_data;
};

/**
 * @brief Start of grace period
 *
 * @note This looks specific to SONAS and ought not to be in the top
 *       level SAL files.  (The SAL needs more A to support this kind
 *       of thing.)
 */

#define EVENT_JUST_GRACE     0	/* Just start grace period */
#define EVENT_CLEAR_BLOCKED  1	/* Start grace period, and clear blocked locks
				 */
#define EVENT_RELEASE_IP     2	/* Start grace period, clear blocked locks,
				   and release all locks */
#define EVENT_UPDATE_CLIENTS 3	/* Start grace period, clear blocked locks,
				   release all locks, and update clients list.
				 */
#define EVENT_TAKE_NODEID    4	/* Start grace period, clear blocked locks,
				   release all locks, and update clients using
				   node id. */
#define EVENT_TAKE_IP        5	/* Start grace period, clear blocked locks,
				   release all locks, and update clients using
				   IP address. */

typedef struct nfs_grace_start {
	int event;		/*< Reason for grace period, see EVENT_nnn */
	int nodeid;		/*< Node from which we are taking over */
	char *ipaddr;		/*< IP of failed node */
} nfs_grace_start_t;

/* Memory pools */

extern pool_t *state_owner_pool;	/*< Pool for NFSv4 files's open owner */
extern pool_t *state_v4_pool;	/*< Pool for NFSv4 files's states */

/**
 * @brief NLM share reservation
 */

typedef struct state_nlm_share {
	struct glist_head sns_share_per_file;	/*< Shares on this file */
	struct glist_head sns_share_per_owner;	/*< Shares for this owner */
	struct glist_head sns_share_per_client;	/*< Shares for this client */
	struct glist_head sns_share_per_export;	/*< Shares for this export */
	state_owner_t *sns_owner;	/*< State owner */
	cache_entry_t *sns_entry;	/*< File */
	struct gsh_export *sns_export;	/*< Export */
	int sns_access;		/*< Access mode */
	int sns_deny;		/*< Deny mode */
} state_nlm_share_t;

#ifdef DEBUG_SAL
extern struct glist_head state_v4_all;
extern struct glist_head state_owners_all;
#endif

#endif				/* SAL_DATA_H */
/** @} */
