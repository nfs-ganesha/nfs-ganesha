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
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"
#ifdef _USE_NLM
#include "nlm4.h"
#endif
#include "nlm_list.h"
#ifdef _USE_NFS4_1
#include "nfs41_session.h"
#endif                          /* _USE_NFS4_1 */

/* Indicate if state code must support blocking locks
 * NLM supports blocking locks
 * Eventually NFS v4.1 will support blocking locks
 */
#ifdef _USE_NLM
#define _USE_BLOCKING_LOCKS
#endif

/* Some habits concerning mutex management */
#ifndef P
#define P( a ) pthread_mutex_lock( &a )
#endif

#ifndef V
#define V( a ) pthread_mutex_unlock( &a )
#endif

#define STATE_LOCK_OFFSET_EOF 0xFFFFFFFFFFFFFFFFLL

/* Forward references to types */
typedef struct state_nfs4_owner_t   state_nfs4_owner_t;
typedef struct state_owner_t        state_owner_t;
typedef struct state_t              state_t;
typedef struct nfs_argop4_state     nfs_argop4_state;
typedef struct state_lock_entry_t   state_lock_entry_t;
#ifdef _USE_BLOCKING_LOCKS
typedef struct state_cookie_entry_t state_cookie_entry_t;
#endif

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
#ifdef _USE_PNFS
  int nothing; /** @todo Add fsal_layout structure here */
#else
  int nothing;
#endif
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
  struct glist_head state_list;                /**< List of states on a file                   */
  state_type_t      state_type;
  state_data_t      state_data;
  u_int32_t         state_seqid;               /**< The NFSv4 Sequence id                      */
  char              stateid_other[OTHERSIZE];  /**< "Other" part of state id, used as hash key */
  state_owner_t   * state_powner;              /**< State Owner related to this state          */
  cache_entry_t   * state_pentry;              /**< Related pentry                             */
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
  STATE_LOCK_OWNER_NFSV4
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

typedef struct state_nlm_client_t
{
  pthread_mutex_t         slc_mutex;
  state_nsm_client_t    * slc_nsm_client;
  xprt_type_t             slc_client_type;
  int                     slc_refcount;
  int                     slc_nlm_caller_name_len;
  char                    slc_nlm_caller_name[LM_MAXSTRLEN+1];
  CLIENT                * slc_callback_clnt;
} state_nlm_client_t;

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
} state_status_t;

typedef enum state_blocking_t
{
  STATE_NON_BLOCKING,
  STATE_NLM_BLOCKING,
  STATE_NFSV4_BLOCKING,
  STATE_GRANTING,
  STATE_CANCELED
} state_blocking_t;

typedef enum state_lock_type_t
{
  STATE_LOCK_R,   /* not exclusive */
  STATE_LOCK_W,   /* exclusive */
  STATE_NO_LOCK   /* used when test lock returns success */
} state_lock_type_t;

typedef struct state_lock_desc_t
{
  state_lock_type_t sld_type;
  uint64_t          sld_offset;
  uint64_t          sld_length;
} state_lock_desc_t;

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
typedef struct state_nlm_block_data_t
{
  sockaddr_t                 sbd_nlm_hostaddr;
  netobj                     sbd_nlm_fh;
  char                       sbd_nlm_fh_buf[MAX_NETOBJ_SZ];
  struct user_credentials    sbd_credential;
} state_nlm_block_data_t;

typedef struct state_block_data_t
{
  granted_callback_t           sbd_granted_callback;
  state_cookie_entry_t       * sbd_blocked_cookie;
  union
    {
#ifdef _USE_NLM
      state_nlm_block_data_t   sbd_nlm_block_data;
#endif
      void                   * sbd_v4_block_data;
    } sbd_block_data;
} state_block_data_t;
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
  int                    sle_ref_count;
  unsigned long long     sle_fileid;
  cache_entry_t        * sle_pentry;
  state_blocking_t       sle_blocked;
  state_block_data_t   * sle_block_data;
  state_owner_t        * sle_owner;
  state_t              * sle_state;
  state_lock_desc_t      sle_lock;
  pthread_mutex_t        sle_mutex;
};

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
#endif

#endif                          /*  _SAL_DATA_H */
