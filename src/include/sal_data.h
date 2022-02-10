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
 * @brief Maximum number of operation in a compound request
 *
 * We cap the number of operation to this value for all V4 versions
 */
#define NFS4_MAX_OPERATIONS 100

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
 * @brief Default number of forechannel slots in a session
 *
 * This is also the maximum number of backchannel slots we'll use,
 * even if the client offers more.
 */
#define NFS41_NB_SLOTS_DEF 64

/**
 * @brief Members in the slot table
 */

typedef struct nfs41_session_slot__ {
	sequenceid4 sequence;	/*< Sequence number of this operation */
	pthread_mutex_t lock;	/*< Lock on the slot */
	struct COMPOUND4res_extended *cached_result;	/*< NFv41: pointer to
							   cached RPC result in
							   a session's slot */
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
enum {
	session_bc_up = 0x01,
	session_bc_fault = 0x02, /* not actually used anywhere */
};

/**
 * @brief minimum values for channel attributes
 */
#define NFS41_MIN_REQUEST_SIZE 256
#define NFS41_MIN_RESPONSE_SIZE 256
#define NFS41_MIN_OPERATIONS 2
#define NFS41_MAX_CONNECTIONS 16

/**
 * @brief Structure representing an NFSv4.1 session
 */

struct nfs41_session {
	char session_id[NFS4_SESSIONID_SIZE];	/*< Session ID */
	struct glist_head session_link;	/*< Link in the list of
					   sessions for this
					   clientid */

	channel_attrs4 fore_channel_attrs;	/*< Fore-channel attributes */

	channel_attrs4 back_channel_attrs;	/*< Back-channel attributes */
	struct rpc_call_channel cb_chan;	/*< Back channel */
	pthread_mutex_t cb_mutex;	/*< Protects the cb slot table,
					   when searching for a free slot */
	pthread_cond_t cb_cond;	/*< Condition variable on which we
				   wait if the slot table is full
				   and on which we signal when we
				   free an entry. */

	pthread_rwlock_t conn_lock;
	int num_conn;
	sockaddr_t connections[NFS41_MAX_CONNECTIONS];

	nfs_client_id_t *clientid_record;	/*< Client record
						   correspinding to ID */
	clientid4 clientid;	/*< Clientid owning this session */
	uint32_t cb_program;	/*< Callback program ID */
	uint32_t flags;		/*< Flags pertaining to this session */
	int32_t refcount;
	uint32_t nb_slots;	/**< Number of slots in this session */
	nfs41_session_slot_t *fc_slots;	/**< Forechannel slot table*/
	nfs41_cb_session_slot_t *bc_slots;	/**< Backchannel slot table */
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

#include "sal_shared.h"

/**
 * @brief Data for an NFS v4 share reservation/open
 */

struct state_share {
	struct glist_head share_lockstates;	/*< Lock states for this
						   open state
						   This field MUST be first */
	unsigned int share_access;	/*< The NFSv4 Share Access state */
	unsigned int share_deny;	/*< The NFSv4 Share Deny state */
	unsigned int share_access_prev;	/*< Previous share access state */
	unsigned int share_deny_prev;	/*< Previous share deny state   */
};

/**
 * @brief Data for an NLM share reservation/open
 */

struct state_nlm_share {
	/** Put this share state on a list per client. This field MUST be first
	 */
	struct glist_head share_perclient;
	/** The NLM Share Access state */
	unsigned int share_access;
	/** The NLM Share Deny state */
	unsigned int share_deny;
	/** Counts of each share access state */
	unsigned int share_access_counts[fsa_RW + 1];
	/** Counts of each share deny state   */
	unsigned int share_deny_counts[fsm_DRW + 1];
};

/**
 * @brief Data for a set of locks
 */

struct state_lock {
	struct glist_head state_locklist;	/*< List of locks owned by
						   this stateid
						   This field MUST be first */
	struct glist_head state_sharelist;	/*< List of states related
						   to a share */
	state_t *openstate;	/*< The related open-state */
};

/**
 * @brief Data for a 9p fid
 */

struct state_9p_fid {
	struct glist_head state_locklist;	/*< List of locks owned by
						   this fid
						   This field MUST be first */
	unsigned int share_access;	/*< The 9p Access state */
	unsigned int share_deny;	/*< Will always be 0 */
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
	enum deleg_state sd_state;
	struct cf_deleg_stats sd_clfile_stats;  /* client specific */
	uint32_t share_access;	/*< The NFSv4 Share Access state */
	uint32_t share_deny;	/*< The NFSv4 Share Deny state */
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
	struct state_nlm_share nlm_share;
	struct state_lock lock;
	struct state_deleg deleg;
	struct state_layout layout;
	struct state_9p_fid fid;
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
 *
 * The lists are protected by the st_lock
 */

struct state_t {
	struct glist_head state_list;	/**< List of states on a file */
	struct glist_head state_owner_list;  /**< List of states for an owner */
	struct glist_head state_export_list; /**< List of states on the same
						 export */
#ifdef DEBUG_SAL
	struct glist_head state_list_all;    /**< Global list of all stateids */
#endif
	pthread_mutex_t state_mutex; /**< Mutex protecting following pointers */
	struct gsh_export *state_export; /**< Export this entry belongs to */
	/* Don't re-order or move these next two.  They are used for hashing */
	state_owner_t *state_owner;	/**< State Owner related to state */
	struct fsal_obj_handle *state_obj; /**< owning object */
	struct fsal_export *state_exp;  /**< FSAL export */
	union state_data state_data;
	enum state_type state_type;
	u_int32_t state_seqid;		/**< The NFSv4 Sequence id */
	int32_t state_refcount;		/**< Refcount for state_t objects */
	char stateid_other[OTHERSIZE];	/**< "Other" part of state id,
					   used as hash key */
	struct state_refer state_refer;	/**< For NFSv4.1, track the
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

extern struct glist_head cached_open_owners;
extern pthread_mutex_t cached_open_owners_lock;

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
#ifdef _USE_NLM
	STATE_LOCK_OWNER_NLM,	/*< An NLM client */
#endif /* _USE_NLM */
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
	CARE_OWNER,             /*< Don't care about nsm state but do need an
				    owner. */
	CARE_ALWAYS,		/*< Always care about client's status */
	CARE_NO_MONITOR,	/*< Care, but will not actively monitor */
	CARE_MONITOR		/*< Will actively monitor client status */
} care_t;

extern hash_table_t *ht_nsm_client;
extern hash_table_t *ht_nlm_client;

/**
 * @brief NSM (rpc.statd) state for a given client.
 *
 * All nsm clients have a caller name.
 *
 * When nsm_use_caller_name is true, the caller name comes from the NLM
 * requests.
 *
 * When nsm_use_caller_name is false, the caller name is the string form of
 * the IP address. If the IP address is IPv4 encapsulated in IPv6, the name
 * will be the simple IPv4 version of the address.
 */

typedef struct state_nsm_client_t {
	pthread_mutex_t ssc_mutex;	/*< Mutex protecting this
					   structure */
	struct glist_head ssc_lock_list;	/*< All locks held by client */
	struct glist_head ssc_share_list;	/*< All share reservations */
	struct gsh_client *ssc_client;		/*< The client involved */
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
	sockaddr_t slc_server_addr; /*< local addr when request is
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
	sockaddr_t client_addr;	/*< Network address of client */
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
	struct glist_head so_state_list; /*< This is the head of a list of
					   states owned by this owner.
					   so_mutex MUST be held when
					   accessing this field. */
	struct glist_head so_perclient;  /*< open owner entry to be
					   linked to client */
	struct glist_head so_cache_entry; /*< Entry on cached_open_owners */
	time_t so_cache_expire; /* time cached OPEN owner will expire.  If
				   non-zero, so_cache_entry is in
				   cached_open_owners list.
				   cached_open_owners_lock MUST be held
				   when accessing this field.*/
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
	int32_t so_refcount;	/*< Reference count for lifecyce management */
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
	bool cid_allow_reclaim;	/*< Can still reclaim state? */
	nfs_client_cred_t cid_credential;	/*< Client credential */
	char *cid_recov_tag;	/*< Recovery tag */
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
			char cb_client_r_addr[SOCK_NAME_MAX];
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
	unsigned int cid_nb_session;	/*< Number of sessions stored */
	uint32_t cid_create_session_sequence;	/*< Sequence number for session
						   creation. */
	CREATE_SESSION4res cid_create_session_slot; /*< Cached response to
							  last CREATE_SESSION */
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
typedef state_status_t(*granted_callback_t) (struct fsal_obj_handle *obj,
					     state_lock_entry_t *lock_entry);

/**
 * @brief NLM specific Blocking lock data
 */

typedef struct state_nlm_block_data_t {
	netobj sbd_nlm_fh;	/*< Filehandle */
	char sbd_nlm_fh_buf[NFS3_FHSIZE];	/*< Statically allocated
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
	STATE_GRANT_FSAL_AVAILABLE,	/*< FSAL signalling lock availability */
	STATE_GRANT_POLL,		/*< Poll this lock */
} state_grant_type_t;

typedef enum state_block_type_t {
	/** Not blocking */
	STATE_BLOCK_NONE,
	/** Lock is blocked by an internal lock. */
	STATE_BLOCK_INTERNAL,
	/** Lock is blocked and FSAL supports async blocking locks. */
	STATE_BLOCK_ASYNC,
	/** Lock is blocked and SAL will poll the lock. */
	STATE_BLOCK_POLL,
} state_block_type_t;

/**
 * @brief Blocking lock data
 */
struct state_block_data_t {
	struct glist_head sbd_list;	/*< Lost of blocking locks */
	state_grant_type_t sbd_grant_type;	/*< Type of grant */
	state_block_type_t sbd_block_type;	/*< Type of block */
	granted_callback_t sbd_granted_callback; /*< Callback for grant */
	state_cookie_entry_t *sbd_blocked_cookie; /*< Blocking lock cookie */
	state_lock_entry_t *sbd_lock_entry;	/*< Details of lock */
	union {
		state_nlm_block_data_t sbd_nlm; /*< NLM block data */
		void *sbd_v4;			/*< NFSv4 block data */
	} sbd_prot;
};

struct state_lock_entry_t {
	struct glist_head sle_list;	/*< Locks on this file */
	struct glist_head sle_owner_locks; /*< Link on the owner lock list */
	struct glist_head sle_client_locks;	/*< Locks on this client */
	struct glist_head sle_state_locks;	/*< Locks on this state */
#ifdef DEBUG_SAL
	struct glist_head sle_all_locks; /*< Link on the global lock list */
#endif				/* DEBUG_SAL */
	struct glist_head sle_export_locks;	/*< Link on the export
						   lock list */
	struct gsh_export *sle_export;
	struct fsal_obj_handle *sle_obj;	/*< File being locked */
	state_block_data_t *sle_block_data;	/*< Blocking lock data */
	state_owner_t *sle_owner;	/* Lock owner */
	state_t *sle_state;	/*< Associated lock state */
	state_blocking_t sle_blocked;	/*< Blocking status */
	int32_t sle_ref_count;	/*< Reference count */
	fsal_lock_param_t sle_lock;	/*< Lock description */
	pthread_mutex_t sle_mutex;	/*< Mutex to protect the structure */
};

/**
 * @brief Stats for file-specific and client-file delegation heuristics
 */

struct file_deleg_stats {
	uint32_t fds_curr_delegations;    /* number of delegations on file */
	open_delegation_type4 fds_deleg_type; /* delegation type */
	uint32_t fds_delegation_count;  /* times file has been delegated */
	uint32_t fds_recall_count;      /* times file has been recalled */
	time_t fds_avg_hold;            /* avg amount of time deleg held */
	time_t fds_last_delegation;
	time_t fds_last_recall;
	uint32_t fds_num_opens;         /* total num of opens so far. */
	time_t fds_first_open;          /* time that we started recording
					   num_opens */
};

enum cbgetattr_state {
	CB_GETATTR_NONE = 0, /* initial state or reset to as
				and when finished processing
				response */
	CB_GETATTR_WIP, /* when req sent */
	CB_GETATTR_RSP_OK, /* successful response */
	CB_GETATTR_FAILED /* for any failure */
};

struct cbgetattr_rsp {
	enum cbgetattr_state state;
	uint64_t change;
	uint64_t filesize;
	bool modified;
};
typedef struct cbgetattr_rsp  cbgetattr_t;

/**
 * @brief Per-file state lists
 *
 * To be used by FSALs
 */
struct state_file {
	/** File owning state */
	struct fsal_obj_handle *obj;
	/** NFSv4 states on this file. Protected by st_lock */
	struct glist_head list_of_states;
	/** Layout recalls on this file. Protected by st_lock */
	struct glist_head layoutrecall_list;
	/** Pointers for lock list. Protected by st_lock */
	struct glist_head lock_list;
	/** Pointers for NLM share list. Protected by st_lock */
	struct glist_head nlm_share_list;
	/** true iff write delegated. Protected by st_lock */
	bool write_delegated;
	/** client holding write_deleg. Protected by st_lock */
	nfs_client_id_t *write_deleg_client;
	/** Delegation statistics. Protected by st_lock */
	struct file_deleg_stats fdeleg_stats;
	/* cbgetattr stats. Protected by st_lock */
	cbgetattr_t cbgetattr;
	uint32_t anon_ops;   /* number of anonymous operations
			      * happening at the moment which
			      * prevents delegations from being
			      * granted */
};

/**
 * @brief Per-directory state lists
 *
 * To be used by FSALs/SAL
 */
struct state_dir {
	/** If this is a junction, the export this node points to.
	 * Protected by jct_lock. */
	struct gsh_export *junction_export;
	/** gsh_refstr to stash copy of the export's pseudopath */
	struct gsh_refstr *jct_pseudopath;
	/** List of exports that have this object as their root.
	 * Protected by jct_lock */
	struct glist_head export_roots;
	/** There is one export root reference counted for each export
	    for which this entry is a root for. This field is used
	    with the atomic inc/dec/fetch routines. */
	int32_t exp_root_refcount;
};

/**
 *  @brief Associate lock state or export junction information with an object.
 *
 * LOCK ORDERING
 *
 * The state handle has two overlapping locks for different purposes.
 *
 * The st_lock is used to protect byte range locks, opens, and such for regular
 * files. It is a mutex since there is no parallelism benefit to it being a
 * rwlock.
 *
 * The jct_lock is used to protect export junction information for directories.
 * It is a rwlock since most of the time junctions are being looked at not
 * modified.
 *
 * Both of these locks are often used in conjunction with the export->lock, but
 * the rules of lock order are different.
 *
 * For st_lock, the export->lock MUST NOT be held, the export->lock is often
 * taken while already holding st_lock, so the order is:
 *
 * st_lock THEN export->lock
 *
 * For jct_lock, there is one place where the export->lock is already held,
 * so it makes sense for the order to be:
 *
 * export->lock THEN jct_lock.
 *
 */
struct state_hdl {
	union {
		/** Lock protecting state */
		pthread_mutex_t st_lock;
		/** Lock protecting export junctions */
		pthread_rwlock_t jct_lock;
	};
	bool no_cleanup;
	union {
		/** Details about the state held for a regular file. */
		struct state_file	file;
		/** Details about an export junction. */
		struct state_dir	dir;
	};
};

#define JCT_PSEUDOPATH(shdl) (shdl->dir.jct_pseudopath ? \
	shdl->dir.jct_pseudopath->gr_val : NULL)

/**
 * @brief Description of a layout segment
 */

typedef struct state_layout_segment {
	struct glist_head sls_state_segments;	/*< Link on the per-layout-state
						   segment list */
	state_t *sls_state;	/*< Associated layout state */
	struct pnfs_segment sls_segment;	/*< Segment descriptor */
	void *sls_fsal_data;	/*< FSAL data */
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
	struct fsal_obj_handle *obj;	/*< Related file */
	layouttype4 type;	/*< Type of layout being recalled */
	struct pnfs_segment segment;	/*< Segment to recall */
	struct glist_head state_list;	/*< List of states affected by this
					   layout */
	void *recall_cookie;	/*< Cookie returned to FSAL on return of last
				   segment satisfying the layout */
};

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
	struct fsal_obj_handle *sce_obj;	/*< Associated file */
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

#ifdef DEBUG_SAL
extern struct glist_head state_v4_all;
extern struct glist_head state_owners_all;
#endif

#endif				/* SAL_DATA_H */
/** @} */
