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

#include "cache_inode.h"
#include "abstract_atomic.h"
#include "abstract_mem.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#include "log.h"
#include "config_parsing.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_proto_functions.h"
#include "nlm4.h"
#ifdef _USE_9P
#include "9p.h"
#endif /* _USE_9P*/
#include "nlm_list.h"
#include "fsal_pnfs.h"

/**
 * @brief Indicate that lock extends to the entire range of the file
 *
 * This is true no matter what the beginning of the lock range is.
 */
#define STATE_LOCK_OFFSET_EOF 0xFFFFFFFFFFFFFFFFLL

/* Forward references to types */
typedef struct state_nfs4_owner_t state_nfs4_owner_t;
typedef struct state_owner_t state_owner_t;
typedef struct state_t state_t;
typedef struct nfs_argop4_state nfs_argop4_state;
typedef struct state_lock_entry_t state_lock_entry_t;
typedef struct state_async_queue_t state_async_queue_t;
typedef struct nfs_client_record_t nfs_client_record_t;
typedef struct state_nlm_client_t state_nlm_client_t;
typedef struct state_nlm_share_t state_nlm_share_t;
typedef struct state_cookie_entry_t state_cookie_entry_t;
typedef struct state_block_data_t state_block_data_t;
typedef struct state_layout_segment state_layout_segment_t;

/*****************************************************************************
 *
 * NFSv4.1 Session data
 *
 *****************************************************************************/

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
	sequenceid4 sequence; /*< Sequence number of this operation */
	pthread_mutex_t lock; /*< Lock on the slot */
	COMPOUND4res_extended cached_result; /*< The cached result */
	unsigned int cache_used; /*< If we cached the result */
} nfs41_session_slot_t;

/**
 * @brief Bookkeeping for callback slots on the client
 */

typedef struct nfs41_cb_session_slot {
	sequenceid4 sequence; /*< Sequence number of last call */
	bool in_use; /*< Set if a call if in progress on this slot */
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

struct nfs41_session__ {
	char session_id[NFS4_SESSIONID_SIZE]; /*< Session ID */
	int32_t refcount;
	clientid4 clientid; /*< Clientid owning this session */
	nfs_client_id_t *clientid_record; /*< Client record
					      correspinding to ID */
	struct glist_head session_link; /*< Link in the list of
					    sessions for this
					    clientid */
	uint32_t flags; /*< Flags pertaining to this session */
	SVCXPRT *xprt; /*< Referenced pointer to transport */

	channel_attrs4 fore_channel_attrs; /*< Fore-channel attributes */
	nfs41_session_slot_t slots[NFS41_NB_SLOTS]; /*< Slot table */

	channel_attrs4 back_channel_attrs; /*< Back-channel attributes */
	nfs41_cb_session_slot_t cb_slots[NFS41_NB_SLOTS]; /*< Callback
							      Slot table */
	uint32_t cb_program; /*< Callback program ID */
	struct rpc_call_channel cb_chan; /*< Back channel */
	pthread_mutex_t cb_mutex; /*< Protects the cb slot table,
				      when searching for a free slot */
	pthread_cond_t cb_cond; /*< Condition variable on which we
				    wait if the slot table is full
				    and on which we signal when we
				    free an entry. */
};

/**
 * @brief Track the call that creates a state, for NFSv4.1.
 */

struct state_refer {
	sessionid4 session; /*< The session of the creating call. */
	sequenceid4 sequence; /*< The sequence ID of the creating
				  call. */
	slotid4 slot; /*< The slot ID of the creating call. */
};

/******************************************************************************
 *
 * NFSv4 State data
 *
 *****************************************************************************/

/**
 * @brief Type of state
 */

typedef enum state_type_t {
	STATE_TYPE_NONE    = 0,
	STATE_TYPE_SHARE   = 1,
	STATE_TYPE_DELEG   = 2,
	STATE_TYPE_LOCK    = 4,
	STATE_TYPE_LAYOUT  = 5
} state_type_t;

/**
 * @brief Data for a share reservation/open
 */

typedef struct state_share__ {
	unsigned int share_access; /*< The NFSv4 Share Access state */
	unsigned int share_deny; /*< The NFSv4 Share Deny state */
	struct glist_head share_lockstates; /*< Lock states for this
					        open state */
	unsigned int share_access_prev; /*< Previous share access state */
	unsigned int share_deny_prev; /*< Previous share deny state   */
} state_share_t;

/**
 * @brief Data for a set of locks
 */

typedef struct state_lock_t {
	state_t *openstate; /*< The related open-state */
	struct glist_head state_locklist; /*< List of locks owned by
					      this stateid */
	struct glist_head state_sharelist; /*< List of states related
					       to a share */
} state_lock_t;

/**
 * @brief Data for a delegation
 *
 * @todo We should at least track whether this is a read or write
 * delegation.
 */

typedef struct state_deleg__ {
	unsigned int nothing;
} state_deleg_t;

/**
 * @brief Data for a set of layouts of a given type
 */

typedef struct state_layout__ {
	layouttype4 state_layout_type; /*< The type of layout this state
					   represents */
	bool state_return_on_close; /*< Whether this layout should be
				        returned on last close. */
	uint32_t granting; /*< Number of LAYOUTGETs in progress */
	struct glist_head state_segments; /*< List of segments */
} state_layout_t;

/**
 * @brief Type specific state data
 */

typedef union state_data_t {
	state_share_t share;
	state_lock_t lock;
	state_deleg_t deleg;
	state_layout_t layout;
} state_data_t;

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
	struct glist_head state_list; /*< List of states on a file */
	struct glist_head state_owner_list; /*< List of states for an owner */
	struct glist_head state_export_list; /*< List of states on the same
					         export */
#ifdef DEBUG_SAL
	struct glist_head state_list_all; /*< Global list of all stateids */
#endif
	exportlist_t *state_export; /*< Export this entry belongs to */
	state_owner_t *state_owner; /*< State Owner related to this state */
	cache_entry_t *state_entry; /*< Related entry */
	state_type_t state_type;
	state_data_t state_data;
	u_int32_t state_seqid; /*< The NFSv4 Sequence id */
	char stateid_other[OTHERSIZE];  /*< "Other" part of state id,
					    used as hash key */
	struct state_refer state_refer; /*< For NFSv4.1, track the
					    call that created a
					    state. */
};

/*****************************************************************************
 *
 * NFS Owner data
 *
 *****************************************************************************/

typedef void (state_owner_init_t) (state_owner_t * powner);

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
	char * son_owner_val;
} state_nfs4_owner_name_t;

/**
 * @brief The type of entity responsible for a state
 *
 * For NLM and 9P, one kind of owner owns every kind of state.  For
 * NFSv4.1 open owners and lock owners are in disjoint spaces, and all
 * non-open and non-lock states are associated with a given client.
 */

typedef enum state_owner_type_t {
	STATE_LOCK_OWNER_UNKNOWN, /*< Unknown */
	STATE_LOCK_OWNER_NLM, /*< An NLM client */
#ifdef _USE_9P
	STATE_LOCK_OWNER_9P, /*< A 9P client */
#endif /* _USE_9P */
	STATE_OPEN_OWNER_NFSV4, /*< An NFSv4 owner of an open */
	STATE_LOCK_OWNER_NFSV4, /*< An NFSv4 owner of a set of locks */
	STATE_CLIENTID_OWNER_NFSV4 /*< An NFSv4 client, owns all
				       states but opens and locks. */
} state_owner_type_t;

/**
 * @brief Specifies interest in a client for NLN/NSM.
 */

typedef enum care_t {
	CARE_NOT, /*< Do not care about client's status */
	CARE_ALWAYS, /*< Always care about client's status */
	CARE_NO_MONITOR, /*< Care, but will not actively monitor */
	CARE_MONITOR /*< Will actively monitor client status */
} care_t;

/**
 * @brief NSM (rpc.statd) state for a given client.
 */

typedef struct state_nsm_client_t {
	pthread_mutex_t ssc_mutex; /*< Mutex protecting this
				       structure */
	struct glist_head ssc_lock_list; /*< All locks held by client */
	struct glist_head ssc_share_list; /*< All share reservations */
	sockaddr_t ssc_client_addr; /*< Network address of client */
	int32_t ssc_refcount; /*< Reference count to protect
				  structure */
	int32_t ssc_monitored; /*< If this client is actively
				   monitored */
	int32_t ssc_nlm_caller_name_len; /*< Length of identifier */
	char *ssc_nlm_caller_name; /*< Client identifier */
} state_nsm_client_t;

/**
 * @brief Represent a single client accessing us through NLM
 */

struct state_nlm_client_t {
	state_nsm_client_t *slc_nsm_client; /*< Related NSM client */
	xprt_type_t slc_client_type; /*< The transport type to this
				         client */
	int32_t slc_refcount; /*< Reference count for disposal */
	int32_t slc_nlm_caller_name_len; /*< Length of client name */
	char *slc_nlm_caller_name; /*< Client name */
	CLIENT *slc_callback_clnt; /*< Callback for blocking locks */
	AUTH *slc_callback_auth; /*< Authentication for callback */
};

/**
 * @brief Owner of an NLM lock or share.
 */

typedef struct state_nlm_owner_t {
	state_nlm_client_t *so_client; /*< Structure for this client */
	int32_t so_nlm_svid; /*< Owner within client */
	struct glist_head so_nlm_shares; /*< Share reservations */
} state_nlm_owner_t;

/**
 * @brief 9P lock owner
 */

#ifdef _USE_9P
typedef struct state_9p_owner_t {
	u32 proc_id; /*< PID on the client */
	struct sockaddr_storage client_addr; /*< Network address of client */
} state_9p_owner_t;
#endif /* _USE_9P */

/**
 * @brief Share and lock operations for NFSv4.0
 *
 * This structure saves the arguments to the most recent operation on
 * an open or lock owner associated with an NFSv4.0 client.  This is
 * part of the At-Most Once semantics and not used under NFSv4.1.
 */

struct nfs_argop4_state {
	nfs_opnum4 argop; /*< Operation being saved */
	union {
		CLOSE4args opclose; /*< CLOSE */
		LOCK4args oplock; /*< LOCK */
		LOCKU4args oplocku; /*< LOCKU */
		OPEN4args opopen; /*< OPEN */
		OPEN_CONFIRM4args opopen_confirm; /*< OPEN_CONFIRM */
		OPEN_DOWNGRADE4args opopen_downgrade; /*< OPEN_DOWNGRADE */
	} nfs_argop4_u;
};

/**
 * @brief A structure supporting all NFSv4 owners.
 */

struct state_nfs4_owner_t {
	clientid4 so_clientid; /*< Owning clientid */
	nfs_client_id_t *so_clientrec; /*< Owning client id record */
	bool so_confirmed; /*< Confirmation (NFSv4.0 only) */
	seqid4 so_seqid; /*< Seqid for serialization of operations on
			   owner (NFSv4.0 only) */
	nfs_argop4_state so_args; /*< Saved args */
	cache_entry_t *so_last_entry; /*< Last file operated on by
					  this state owner */
	nfs_resop4 so_resp; /*< Saved response */
	state_owner_t *so_related_owner; /*< For lock-owners, the
					     open-owner under which
					     the lock owner was
					     created */
	struct glist_head so_state_list; /*< States owned by this owner */
	struct glist_head so_perclient; /*< open owner entry to be
					    linked to client */
};

/**
 * @brief General state owner
 *
 * This structure encodes the owner of any state, protocol specific
 * information is contained within the union.
 */

struct state_owner_t {
	state_owner_type_t so_type; /*< Owner type */
	struct glist_head so_lock_list; /*< Locks for this owner */
#ifdef DEBUG_SAL
	struct glist_head so_all_owners; /**< Global list of all state owners */
#endif /* _DEBUG_MEMLEAKS */
	pthread_mutex_t so_mutex; /*< Mutex on this owner */
	int so_refcount; /*< Reference count for lifecyce management */
	int so_owner_len; /*< Length of owner name */
	char * so_owner_val; /*< Owner name */
	union {
		state_nfs4_owner_t so_nfs4_owner; /*< All NFSv4 state owners */
		state_nlm_owner_t so_nlm_owner; /*< NLM lock and share
						    owners */
#ifdef _USE_9P
		state_9p_owner_t so_9p_owner; /*< 9P lock owners */
#endif
	} so_owner;
};

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
	EXPIRED_CLIENT_ID
} nfs_clientid_confirm_state_t;

/**
 * @brief Errors from the clientid functions
 */

typedef enum clientid_status {
	CLIENT_ID_SUCCESS = 0, /*< Success */
	CLIENT_ID_INSERT_MALLOC_ERROR, /*< Unable to allocate memory */
	CLIENT_ID_INVALID_ARGUMENT, /*< Invalid argument */
	CLIENT_ID_EXPIRED, /*< requested client id expired */
	CLIENT_ID_STALE /*< requested client id stale */
} clientid_status_t;

/**
 * @brief Record associated with a clientid
 *
 * This record holds Ganesha's state on an NFSv4.0 client.
 */

struct nfs_client_id_t {
	clientid4 cid_clientid; /*< The clientid */
	verifier4 cid_verifier; /*< Known verifier */
	verifier4 cid_incoming_verifier; /*< Most recently supplied verifier */
	time_t cid_last_renew; /*< Time of last renewal */
	nfs_clientid_confirm_state_t cid_confirmed; /*< Confirm/expire state */
	nfs_client_cred_t cid_credential; /*< Client credential */
	sockaddr_t cid_client_addr; /*< Network address of
				        client. @note This only really
				        makes sense for NFSv4.0, and
				        even then it's dubious.
				        NFSv4.1 explicitly allows
				        multiple addresses per
				        session and multiple sessions
				        per client. */
	int cid_allow_reclaim; /*< Whether this client can still
				   reclaim state */
	char *cid_recov_dir; /*< Recovery directory */
	nfs_client_record_t *cid_client_record; /*< Record for managing
						    confirmation and
						    replacement */
	struct glist_head cid_openowners; /*< All open owners */
	struct glist_head cid_lockowners; /*< All lock owners */
	pthread_mutex_t cid_mutex; /*< Mutex for this client */
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
		} v40; /*< v4.0 callback information */
		struct {
			/** All sessions */
			struct glist_head cb_session_list;
		} v41; /*< v4.1 callback information */
	} cid_cb; /*< Version specific callback information */
	char cid_server_owner[MAXNAMLEN + 1]; /*< Server owner.
					       * @note Why is this
					       * stored per-client? */
	char cid_server_scope[MAXNAMLEN + 1]; /*< Server scope */
	unsigned int cid_nb_session; /*< Number of sessions stored */
	nfs41_session_slot_t cid_create_session_slot; /*< Cached response to
							  last CREATE_SESSION*/
	unsigned cid_create_session_sequence; /*< Sequence number for session
						  creation. */
	state_owner_t cid_owner; /*< Owner for per-client state */
	int32_t cid_refcount; /*< Reference count for lifecycle */
	int cid_lease_reservations; /*< Counted lease reservations, to spare
				        this clientid from the reaper */
	uint32_t cid_minorversion;
	uint32_t cid_stateid_counter;
};

/**
 * @brief Client owner record for verification and replacement
 *
 * @note The cr_mutex should never be acquired while holding a
 * cid_mutex.
 */

struct nfs_client_record_t {
	int32_t cr_refcount; /*< Reference count for lifecycle */
	pthread_mutex_t cr_mutex; /*< Mutex protecting this structure */
	nfs_client_id_t *cr_confirmed_rec; /*< The confirmed record associated
					       with this owner (if there is
					       one.) */
	nfs_client_id_t *cr_unconfirmed_rec; /*< The unconfirmed record
					         associated with this
					         client name, if there is
						 one. */
	uint32_t cr_pnfs_flags; /*< pNFS flags.  RFC 5661 allows us
				    to treat identical co_owners with
				    different pNFS flags as
				    disjoint. Linux client stupidity
				    forces us to do so. */
	int cr_client_val_len; /*< Length of owner */
	char cr_client_val[]; /*< Suplied co_owner */
};

extern hash_table_t *ht_confirmed_client_id;
extern hash_table_t *ht_unconfirmed_client_id;


/**
 * @brief Possible Errors from SAL Code
 *
 * @note A lot of these errors don't make sense in the context of the
 *       SAL and ought to be pruned.
 */

typedef enum state_status_t {
	STATE_SUCCESS = 0,
	STATE_MALLOC_ERROR = 1,
	STATE_POOL_MUTEX_INIT_ERROR = 2,
	STATE_GET_NEW_LRU_ENTRY = 3,
	STATE_UNAPPROPRIATED_KEY = 4,
	STATE_INIT_ENTRY_FAILED = 5,
	STATE_FSAL_ERROR = 6,
	STATE_LRU_ERROR = 7,
	STATE_HASH_SET_ERROR = 8,
	STATE_NOT_A_DIRECTORY = 9,
	STATE_INCONSISTENT_ENTRY = 10,
	STATE_BAD_TYPE = 11,
	STATE_ENTRY_EXISTS = 12,
	STATE_DIR_NOT_EMPTY = 13,
	STATE_NOT_FOUND = 14,
	STATE_INVALID_ARGUMENT = 15,
	STATE_INSERT_ERROR = 16,
	STATE_HASH_TABLE_ERROR = 17,
	STATE_FSAL_EACCESS = 18,
	STATE_IS_A_DIRECTORY = 19,
	STATE_FSAL_EPERM = 20,
	STATE_NO_SPACE_LEFT = 21,
	STATE_READ_ONLY_FS = 25,
	STATE_IO_ERROR = 26,
	STATE_FSAL_ESTALE = 27,
	STATE_FSAL_ERR_SEC = 28,
	STATE_STATE_CONFLICT = 29,
	STATE_QUOTA_EXCEEDED = 30,
	STATE_DEAD_ENTRY = 31,
	STATE_ASYNC_POST_ERROR = 32,
	STATE_NOT_SUPPORTED = 33,
	STATE_STATE_ERROR = 34,
	STATE_FSAL_DELAY = 35,
	STATE_NAME_TOO_LONG = 36,
	STATE_LOCK_CONFLICT = 37,
	STATE_LOCK_BLOCKED = 38,
	STATE_LOCK_DEADLOCK = 39,
	STATE_BAD_COOKIE = 40,
	STATE_FILE_BIG = 41,
	STATE_GRACE_PERIOD = 42,
	STATE_CACHE_INODE_ERR = 43,
	STATE_SIGNAL_ERROR = 44,
	STATE_KILLED = 45,
	STATE_FILE_OPEN = 46,
        STATE_MLINK                 = 47,
        STATE_SERVERFAULT           = 48,
        STATE_TOOSMALL              = 49,
        STATE_XDEV                  = 50,
} state_status_t;

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
typedef state_status_t (*granted_callback_t)(cache_entry_t *entry,
					     struct req_op_context *req_ctx,
					     state_lock_entry_t *lock_entry);

/**
 * @brief Get export from blocking lock data
 */

typedef bool (*block_data_to_fsal_context_t)(state_block_data_t *block_data,
					     exportlist_t **export);
/**
 * @brief NLM specific Blocking lock data
 */

typedef struct state_nlm_block_data_t {
	sockaddr_t sbd_nlm_hostaddr; /*< Host waiting for blocked lock */
	netobj sbd_nlm_fh; /*< Filehandle */
	char sbd_nlm_fh_buf[MAX_NETOBJ_SZ]; /*< Statically allocated
					        FH buffer */
} state_nlm_block_data_t;

extern struct glist_head state_blocked_locks;

/**
 * @brief Grant types
 */
typedef enum state_grant_type_t {
	STATE_GRANT_NONE, /*< No grant */
	STATE_GRANT_INTERNAL, /*< Grant generated by SAL */
	STATE_GRANT_FSAL, /*< FSAL granting lock */
	STATE_GRANT_FSAL_AVAILABLE /*< FSAL signalling lock availability */
} state_grant_type_t;

/**
 * @brief Blocking lock data
 */
struct state_block_data_t {
	struct glist_head sbd_list; /*< Lost of blocking locks */
	state_grant_type_t sbd_grant_type; /*< Type of grant */
	granted_callback_t sbd_granted_callback; /*< Callback for grant */
	state_cookie_entry_t *sbd_blocked_cookie; /*< Blocking lock cookie */
	state_lock_entry_t *sbd_lock_entry; /*< Details of lock */
	/** Callback to get FSAL data */
	block_data_to_fsal_context_t sbd_block_data_to_fsal_context;
	union {
		state_nlm_block_data_t sbd_nlm_block_data; /*< NLM block
							       data */
		void *sbd_v4_block_data; /*< NFSv4 block data */
	} sbd_block_data;
};

typedef enum lock_type_t {
	POSIX_LOCK, /*< Byte-range lock */
	LEASE_LOCK /*< Delegation */
} lock_type_t;

struct state_lock_entry_t {
	struct glist_head sle_list; /*< Ranges on this lock */
	struct glist_head sle_owner_locks; /*< Link on the owner lock list */
	struct glist_head sle_locks; /*< Locks on this state/client */
#ifdef DEBUG_SAL
	struct glist_head sle_all_locks; /*< Link on the global lock list */
#endif /* DEBUG_SAL */
	struct glist_head sle_export_locks; /*< Link on the export
					      lock list */
	exportlist_t *sle_export;
	cache_entry_t *sle_entry; /*< File being locked */
	state_block_data_t *sle_block_data; /*< Blocking lock data */
	state_owner_t *sle_owner; /* Lock owner */
	state_t *sle_state; /*< Associated lock state */
	state_blocking_t sle_blocked; /*< Blocking status */
	int sle_ref_count; /*< Reference count */
	fsal_lock_param_t sle_lock; /*< Lock description */
	pthread_mutex_t sle_mutex; /*< Mutex to protect the structure */
	lock_type_t sle_type; /*< Type of lock */
};

/**
 * @brief Description of a layout segment
 */

struct state_layout_segment {
	struct glist_head sls_state_segments; /*< Link on the per-layout-state
						  segment list */
	state_t *sls_state; /*< Associated layout state */
	struct pnfs_segment sls_segment; /*< Segment descriptor */
	void *sls_fsal_data; /*< FSAL data */
	pthread_mutex_t sls_mutex; /*< Mutex */
};

/**
 * @brief An entry in a list of states affected by a recall
 *
 * We make a non-intrusive list so that a state can be affected by any
 * number of recalls.
 */

struct recall_state_list {
	struct glist_head link; /*< Link to the next layout state in the
                                    queue */
	state_t *state; /*< State on which to recall */
};

/**
 * @brief Processing for a LAYOUTRECALL from the FSAL.
 */

struct state_layout_recall_file {
	struct glist_head entry_link; /*< List of recalls on a file */
	cache_entry_t *entry; /*< Related cache entry */
	layouttype4 type; /*< Type of layout being recalled */
	struct pnfs_segment segment; /*< Segment to recall */
	struct glist_head state_list; /*< List of states affected by this
					layout */
	void *recall_cookie; /*< Cookie returned to FSAL on return of last
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
	cache_entry_t *sce_entry; /*< Associated file */
	state_lock_entry_t *sce_lock_entry; /*< Associated lock */
	void *sce_cookie; /*< Cookie data */
	int sce_cookie_size; /*< Length of cookie */
};

/*
 * Structures for state async processing
 *
 */

/**
 * @brief Asynchronous state function
 */
typedef void (state_async_func_t) (state_async_queue_t *arg,
				   struct req_op_context *req_ctx);

/**
 * @brief Data for asynchronous NLM calls
 */
typedef struct state_nlm_async_data_t {
	state_nlm_client_t *nlm_async_host; /*< The client */
	void *nlm_async_key; /*< Identifying key */
	union {
		nfs_res_t nlm_async_res; /*< Asynchronous response */
		nlm4_testargs nlm_async_grant; /*< Arguments for grant */
	} nlm_async_args;
} state_nlm_async_data_t;

/**
 * @brief Asynchronous blocking lock data
 */

typedef struct state_async_block_data_t {
	state_lock_entry_t *state_async_lock_entry; /*< Associated lock */
} state_async_block_data_t;

/**
 * @brief Queue of asynchronous events
 */

struct state_async_queue_t {
	struct glist_head state_async_glist; /*< List of events */
	state_async_func_t *state_async_func; /*< Processing function */
	union {
		state_nlm_async_data_t state_nlm_async_data; /*< Data for
							         operation*/
		void *state_no_data; /*< Dummy pointer */
	} state_async_data;
};

/**
 * @brief Start of grace period
 *
 * @note This looks specific to SONAS and ought not to be in the top
 *       level SAL files.  (The SAL needs more A to support this kind
 *       of thing.)
 */

#define EVENT_JUST_GRACE     0
#define EVENT_TAKE_NODEID    1
#define EVENT_UPDATE_CLIENTS 2
#define EVENT_RELEASE_IP     3
#define EVENT_TAKE_IP        4

typedef struct nfs_grace_start {
	int event;    /*< Reason for grace period, see EVENT_nnn */
	int nodeid;   /*< Node from which we are taking over */
	char *ipaddr; /*< IP of failed node */
} nfs_grace_start_t;

/* Memory pools */

extern pool_t *state_owner_pool; /*< Pool for NFSv4 files's open owner */
extern pool_t *state_v4_pool; /*< Pool for NFSv4 files's states */

/**
 * @brief NLM share reservation
 */

struct state_nlm_share_t {
	struct glist_head sns_share_per_file; /*< Shares on this file */
	struct glist_head sns_share_per_owner; /*< Shares for this owner */
	struct glist_head sns_share_per_client; /*< Shares for this client */
	state_owner_t *sns_owner; /*< State owner */
	cache_entry_t *sns_entry; /*< File */
	exportlist_t *sns_export; /*< Export */
	int sns_access; /*< Access mode */
	int sns_deny; /*< Deny mode */
};

#ifdef DEBUG_SAL
extern struct glist_head state_v4_all;
extern struct glist_head state_owners_all;
#endif

#endif /* SAL_DATA_H */
/** @} */
