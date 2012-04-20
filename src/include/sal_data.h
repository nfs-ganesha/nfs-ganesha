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
 * \file    sal_data.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:15 $
 * \version $Revision: 1.95 $
 * \brief   Management of the state abstraction layer.
 *
 * sal_data.h : Management of the state abstraction layer
 *
 *
 */

#ifndef _SAL_DATA_H
#define _SAL_DATA_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

#include "cache_inode.h"
#include "stuff_alloc.h"
#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#include "log.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_proto_functions.h"
#ifdef _USE_NLM
#include "nlm4.h"
#endif
#include "nlm_list.h"
#ifdef _USE_NFS4_1
#include "nfs41_session.h"
#endif                          /* _USE_NFS4_1 */
#ifdef _PNFS_MDS
#include "fsal_pnfs.h"
#endif /* _PNFS_MDS */

/* Indicate if state code must support blocking locks
 * NLM supports blocking locks
 * Eventually NFS v4.1 will support blocking locks
 */
#ifdef _USE_NLM
#define _USE_BLOCKING_LOCKS
#endif

#define STATE_LOCK_OFFSET_EOF 0xFFFFFFFFFFFFFFFFLL

/* Forward references to types */
typedef struct state_nfs4_owner_t   state_nfs4_owner_t;
typedef struct state_owner_t        state_owner_t;
typedef struct state_t              state_t;
typedef struct nfs_argop4_state     nfs_argop4_state;
typedef struct state_lock_entry_t   state_lock_entry_t;
typedef struct state_async_queue_t  state_async_queue_t;
#ifdef _USE_NLM
typedef struct state_nlm_client_t   state_nlm_client_t;
#endif
#ifdef _USE_BLOCKING_LOCKS
typedef struct state_cookie_entry_t state_cookie_entry_t;
typedef struct state_block_data_t   state_block_data_t;
#endif /* _USE_BLOCKING_LOCKS */
#ifdef _PNFS_MDS
typedef struct state_layout_segment_t state_layout_segment_t;
#endif /* _PNFS_MDS */

typedef struct nfs_state_id_param__
{
  hash_parameter_t hash_param;
} nfs_state_id_parameter_t;

typedef struct nfs4_owner_parameter_t
{
  hash_parameter_t hash_param;
} nfs4_owner_parameter_t;

typedef enum state_type_t
{
  STATE_TYPE_NONE    = 0,
  STATE_TYPE_SHARE   = 1,
  STATE_TYPE_DELEG   = 2,
  STATE_TYPE_LOCK    = 4,
  STATE_TYPE_LAYOUT  = 5
} state_type_t;

typedef struct state_share__
{
  char              share_oexcl_verifier[8]; /**< Verifier to use when opening a file as EXCLUSIVE4       */
  unsigned int      share_access;            /**< The NFSv4 Share Access state                            */
  unsigned int      share_deny;              /**< The NFSv4 Share Deny state                              */
  struct glist_head share_lockstates;        /**< The list of lock states associated with this open state */
  unsigned int      share_access_prev;       /**< The bitmap to keep track of previous share access state */
  unsigned int      share_deny_prev;         /**< The bitmap to keep track of previous share deny state   */
} state_share_t;

typedef struct state_lock_t
{
  state_t           * popenstate;      /**< The related open-stateid            */
  struct glist_head   state_locklist;  /**< List of locks owned by this stateid */
  struct glist_head   state_sharelist; /**< List of states related to a share          */
} state_lock_t;

typedef struct state_deleg__
{
  unsigned int nothing;
} state_deleg_t;

typedef struct state_layout__
{
#ifdef _PNFS_MDS
  layouttype4            state_layout_type;
  bool_t                 state_return_on_close;
  struct glist_head      state_segments;
#else /* !_PNFS_MDS */
  int nothing;
#endif /* !_PNFS_MDS */
} state_layout_t;

typedef union state_data_t
{
  state_share_t  share;
  state_lock_t   lock;
  state_deleg_t  deleg;
  state_layout_t layout;
} state_data_t;

/* The value 12 is fixed by RFC3530 */
#define OTHERSIZE 12

extern char all_zero[OTHERSIZE];
extern char all_one[OTHERSIZE];

struct state_t
{
  struct glist_head   state_list;                /**< List of states on a file                   */
  struct glist_head   state_owner_list;          /**< List of states for an owner                */
  struct glist_head   state_export_list;         /**< List of states on the same export          */
  exportlist_t      * state_pexport;             /**< Export this entry belongs to               */
  state_owner_t     * state_powner;              /**< State Owner related to this state          */
  cache_entry_t     * state_pentry;              /**< Related pentry                             */
  state_type_t        state_type;
  state_data_t        state_data;
  u_int32_t           state_seqid;               /**< The NFSv4 Sequence id                      */
  char                stateid_other[OTHERSIZE];  /**< "Other" part of state id, used as hash key */
};

typedef struct state_nfs4_owner_name_t
{
  clientid4    son_clientid;
  unsigned int son_owner_len;
  char         son_owner_val[MAXNAMLEN];
  bool_t       son_islock;
} state_nfs4_owner_name_t;

typedef enum state_owner_type_t
{
  STATE_LOCK_OWNER_UNKNOWN,
#ifdef _USE_NLM
  STATE_LOCK_OWNER_NLM,
#endif
  STATE_OPEN_OWNER_NFSV4,
  STATE_LOCK_OWNER_NFSV4,
  STATE_CLIENTID_OWNER_NFSV4
} state_owner_type_t;

#ifdef _USE_NLM
typedef enum care_t
{
  CARE_NOT,
  CARE_NO_MONITOR,
  CARE_MONITOR
} care_t;

typedef struct state_nsm_client_t
{
  pthread_mutex_t         ssc_mutex;
  struct glist_head       ssc_lock_list;
  sockaddr_t              ssc_client_addr;
  int                     ssc_refcount;
  bool_t                  ssc_monitored;
  int                     ssc_nlm_caller_name_len;
  char                  * ssc_nlm_caller_name;
} state_nsm_client_t;

struct state_nlm_client_t
{
  pthread_mutex_t         slc_mutex;
  state_nsm_client_t    * slc_nsm_client;
  xprt_type_t             slc_client_type;
  int                     slc_refcount;
  int                     slc_nlm_caller_name_len;
  char                    slc_nlm_caller_name[LM_MAXSTRLEN+1];
  CLIENT                * slc_callback_clnt;
};

typedef struct state_nlm_owner_t
{
  state_nlm_client_t * so_client;
  int32_t              so_nlm_svid;
} state_nlm_owner_t;
#endif

struct nfs_argop4_state
{
  nfs_opnum4 argop;
  union
  {
    CLOSE4args opclose;
    LOCK4args oplock;
    LOCKU4args oplocku;
    OPEN4args opopen;
    OPEN_CONFIRM4args opopen_confirm;
    OPEN_DOWNGRADE4args opopen_downgrade;
  } nfs_argop4_u;
};

struct state_nfs4_owner_t
{
  clientid4           so_clientid;
  unsigned int        so_confirmed;
  seqid4              so_seqid;
  uint32_t            so_counter;       /** < Counter is used to build unique stateids  */
  nfs_argop4_state    so_args;          /** < Saved args                                */
  cache_entry_t     * so_last_pentry;   /** < Last file operated on by this state owner */
  nfs_resop4          so_resp;          /** < Saved response                            */
  state_owner_t     * so_related_owner;
  struct glist_head   so_owner_list;    /** < Share and lock owners with the same clientid */
  struct glist_head   so_state_list;    /** < States owned by this owner */
  struct glist_head   so_perclient;     /** open owner entry to be linked to client */
};

/* Undistinguished lock owner type */
struct state_owner_t
{
  state_owner_type_t      so_type;
  struct glist_head       so_lock_list;
  pthread_mutex_t         so_mutex;
  int                     so_refcount;
  int                     so_owner_len;
  char                    so_owner_val[NFS4_OPAQUE_LIMIT]; /* big enough for all owners */
  cache_inode_client_t  * so_pclient;
  union
  {
    state_nfs4_owner_t    so_nfs4_owner;
#ifdef _USE_NLM
    state_nlm_owner_t     so_nlm_owner;
#endif
  } so_owner;
};

extern state_owner_t unknown_owner;

/*
 * Possible errors
 */
typedef enum state_status_t
{
  STATE_SUCCESS               = 0,
  STATE_MALLOC_ERROR          = 1,
  STATE_POOL_MUTEX_INIT_ERROR = 2,
  STATE_GET_NEW_LRU_ENTRY     = 3,
  STATE_UNAPPROPRIATED_KEY    = 4,
  STATE_INIT_ENTRY_FAILED     = 5,
  STATE_FSAL_ERROR            = 6,
  STATE_LRU_ERROR             = 7,
  STATE_HASH_SET_ERROR        = 8,
  STATE_NOT_A_DIRECTORY       = 9,
  STATE_INCONSISTENT_ENTRY    = 10,
  STATE_BAD_TYPE              = 11,
  STATE_ENTRY_EXISTS          = 12,
  STATE_DIR_NOT_EMPTY         = 13,
  STATE_NOT_FOUND             = 14,
  STATE_INVALID_ARGUMENT      = 15,
  STATE_INSERT_ERROR          = 16,
  STATE_HASH_TABLE_ERROR      = 17,
  STATE_FSAL_EACCESS          = 18,
  STATE_IS_A_DIRECTORY        = 19,
  STATE_FSAL_EPERM            = 20,
  STATE_NO_SPACE_LEFT         = 21,
  STATE_CACHE_CONTENT_ERROR   = 22,
  STATE_CACHE_CONTENT_EXISTS  = 23,
  STATE_CACHE_CONTENT_EMPTY   = 24,
  STATE_READ_ONLY_FS          = 25,
  STATE_IO_ERROR              = 26,
  STATE_FSAL_ESTALE           = 27,
  STATE_FSAL_ERR_SEC          = 28,
  STATE_STATE_CONFLICT        = 29,
  STATE_QUOTA_EXCEEDED        = 30,
  STATE_DEAD_ENTRY            = 31,
  STATE_ASYNC_POST_ERROR      = 32,
  STATE_NOT_SUPPORTED         = 33,
  STATE_STATE_ERROR           = 34,
  STATE_FSAL_DELAY            = 35,
  STATE_NAME_TOO_LONG         = 36,
  STATE_LOCK_CONFLICT         = 37,
  STATE_LOCK_BLOCKED          = 38,
  STATE_LOCK_DEADLOCK         = 39,
  STATE_BAD_COOKIE            = 40,
  STATE_FILE_BIG              = 41,
  STATE_GRACE_PERIOD          = 42,
  STATE_CACHE_INODE_ERR       = 43,
  STATE_SIGNAL_ERROR          = 44,
  STATE_KILLED                = 45,
} state_status_t;

typedef enum state_blocking_t
{
  STATE_NON_BLOCKING,
  STATE_NLM_BLOCKING,
  STATE_NFSV4_BLOCKING,
  STATE_GRANTING,
  STATE_CANCELED
} state_blocking_t;

/* The granted call back is responsible for acquiring a reference to
 * the lock entry if needed.
 *
 * NB: this is always defined to avoid conditional function prototype
 */
typedef state_status_t (*granted_callback_t)(cache_entry_t        * pentry,
                                             state_lock_entry_t   * lock_entry,
                                             cache_inode_client_t * pclient,
                                             state_status_t       * pstatus);

#ifdef _USE_BLOCKING_LOCKS
typedef bool_t (*block_data_to_fsal_context_t)(state_block_data_t * block_data,
                                               fsal_op_context_t  * fsal_context);

typedef struct state_nlm_block_data_t
{
  sockaddr_t                 sbd_nlm_hostaddr;
  netobj                     sbd_nlm_fh;
  char                       sbd_nlm_fh_buf[MAX_NETOBJ_SZ];
} state_nlm_block_data_t;

/* List of all locks blocked in FSAL */
struct glist_head state_blocked_locks;

/* List of all async blocking locks notified by FSAL but not processed */
struct glist_head state_notified_locks;

/* Mutex to protect above lists */
pthread_mutex_t blocked_locks_mutex;

typedef enum state_grant_type_t
{
  STATE_GRANT_NONE,
  STATE_GRANT_INTERNAL,
  STATE_GRANT_FSAL,
  STATE_GRANT_FSAL_AVAILABLE
} state_grant_type_t;

struct state_block_data_t
{
  struct glist_head              sbd_list;
  state_grant_type_t             sbd_grant_type;
  granted_callback_t             sbd_granted_callback;
  state_cookie_entry_t         * sbd_blocked_cookie;
  state_lock_entry_t           * sbd_lock_entry;
  block_data_to_fsal_context_t   sbd_block_data_to_fsal_context;
  struct user_credentials        sbd_credential;
  union
    {
#ifdef _USE_NLM
      state_nlm_block_data_t     sbd_nlm_block_data;
#endif
      void                     * sbd_v4_block_data;
    } sbd_block_data;
};
#else
typedef void state_block_data_t;
#endif

struct state_lock_entry_t
{
  struct glist_head      sle_list;
  struct glist_head      sle_owner_locks;
  struct glist_head      sle_locks;
#ifdef _DEBUG_MEMLEAKS
  struct glist_head      sle_all_locks;
#endif
  struct glist_head      sle_export_locks;
  exportlist_t         * sle_pexport;
  cache_entry_t        * sle_pentry;
  state_block_data_t   * sle_block_data;
  state_owner_t        * sle_owner;
  state_t              * sle_state;
  unsigned long long     sle_fileid;
  state_blocking_t       sle_blocked;
  int                    sle_ref_count;
  fsal_lock_param_t      sle_lock;
  pthread_mutex_t        sle_mutex;
};

#ifdef _PNFS_MDS
struct state_layout_segment_t
{
  struct glist_head      sls_state_segments;
  state_t              * sls_state;
  struct pnfs_segment    sls_segment;
  void                 * sls_fsal_data;
  pthread_mutex_t        sls_mutex;
};
#endif /* _PNFS_MDS */

#ifdef _USE_NLM
#define sle_client_locks sle_locks
#endif
#define sle_state_locks  sle_locks

#ifdef _USE_BLOCKING_LOCKS
/*
 * Management of lce_refcount:
 *
 *   state_add_grant_cookie creates a reference.
 *   state_find_grant       gets a reference
 *   state_complete_grant   always releases 1 reference
 *                                it releases a 2nd reference when the call instance
 *                                is the first to actually try and complete the grant
 *   state_release_grant    always releases 1 reference
 *                                it releases a 2nd reference when the call instance
 *                                is the first to actually try and release the grant
 *   state_cancel_grant     calls cancel_blocked_lock, which will release
 *                                the initial reference
 *   cancel_blocked_lock          releases 1 reference if cookie exists
 *                                called by state_cancel_grant
 *                                also called by unlock, cancel, sm_notify
 */
struct state_cookie_entry_t
{
  cache_entry_t      * sce_pentry;
  state_lock_entry_t * sce_lock_entry;
  void               * sce_pcookie;
  int                  sce_cookie_size;
};

/*
 * Structures for state async processing
 *
 */
extern cache_inode_client_t state_async_cache_inode_client;

typedef void (state_async_func_t) (state_async_queue_t * arg);

#ifdef _USE_NLM
typedef struct state_nlm_async_data_t
{
  state_nlm_client_t       * nlm_async_host;
  void                     * nlm_async_key;
  union
    {
      nfs_res_t              nlm_async_res;
      nlm4_testargs          nlm_async_grant;
    } nlm_async_args;
} state_nlm_async_data_t;
#endif

typedef struct state_async_block_data_t
{
  state_lock_entry_t * state_async_lock_entry;
} state_async_block_data_t;

struct state_async_queue_t
{
  struct glist_head              state_async_glist;
  state_async_func_t           * state_async_func;
  union
    {
#ifdef _USE_NLM
      state_nlm_async_data_t     state_nlm_async_data;
#endif
      void                     * state_no_data;
    } state_async_data;
};
#endif

typedef struct nfs_grace_start
{
  int		event;
  ushort	nodeid;
  void		*ipaddr;
} nfs_grace_start_t;

#endif                          /*  _SAL_DATA_H */
