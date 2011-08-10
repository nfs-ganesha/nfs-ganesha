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

/* forward references from cache_inode.h */
typedef struct cache_entry_t        cache_entry_t;
typedef struct cache_inode_client_t cache_inode_client_t;

/* Some habits concerning mutex management */
#ifndef P
#define P( a ) pthread_mutex_lock( &a )
#endif

#ifndef V
#define V( a ) pthread_mutex_unlock( &a )
#endif

typedef struct nfs_state_id_param__
{
  hash_parameter_t hash_param;
} nfs_state_id_parameter_t;

typedef struct nfs_open_owner_param__
{
  hash_parameter_t hash_param;
} nfs_open_owner_parameter_t;

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
  char oexcl_verifier[8];                                                /**< Verifier to use when opening a file as EXCLUSIVE4    */
  unsigned int share_access;                                             /**< The NFSv4 Share Access state                         */
  unsigned int share_deny;                                               /**< The NFSv4 Share Deny state                           */
  unsigned int lockheld;                                                 /**< How many locks did I open ?                          */
} state_share_t;

typedef struct state_lock_t
{
  uint64_t offset;                                  /**< The offset for the beginning of the lock             */
  uint64_t length;                                  /**< The length of the range for this lock                */
  nfs_lock_type4 lock_type;                         /**< The kind of lock to be used                          */
  void *popenstate;                                 /**< The related open-stateid                             */
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

typedef struct state_open_owner_name__
{
  clientid4 clientid;
  unsigned int owner_len;
  char owner_val[MAXNAMLEN];
} state_open_owner_name_t;

typedef enum state_lock_owner_type_t
{
  STATE_LOCK_OWNER_UNKNOWN,
#ifdef _USE_NLM
  STATE_LOCK_OWNER_NLM,
#endif
  STATE_LOCK_OWNER_NFSV4
} state_lock_owner_type_t;

#ifdef _USE_NLM
typedef struct state_nlm_client_t
{
  pthread_mutex_t         slc_mutex;
  struct glist_head       slc_lock_list;
  int                     slc_refcount;
  int                     slc_nlm_caller_name_len;
  char                    slc_nlm_caller_name[LM_MAXSTRLEN+1];
} state_nlm_client_t;

typedef struct state_nlm_owner_t
{
  state_nlm_client_t * slo_client;
  int32_t              slo_nlm_svid;
  int                  slo_nlm_oh_len;
  char                 slo_nlm_oh[MAX_NETOBJ_SZ];
} state_nlm_owner_t;
#endif

typedef struct state_open_owner__
{
  clientid4 clientid;
  unsigned int owner_len;
  char owner_val[NFS4_OPAQUE_LIMIT];
  unsigned int confirmed;
  unsigned int seqid;
  pthread_mutex_t lock;                       //TODO FSF: remove this and use clo_lock
  uint32_t counter;                           /** < Counter is used to build unique stateids */
  struct state_open_owner__ *related_owner;
} state_open_owner_t;

/* Undistinguished lock owner type */
typedef struct state_lock_owner_t
{
  state_lock_owner_type_t slo_type;
  struct glist_head       slo_lock_list;
  pthread_mutex_t         slo_mutex;
  int                     slo_refcount;
  union
  {
    state_open_owner_t    slo_open_owner;
#ifdef _USE_NLM
    state_nlm_owner_t     slo_nlm_owner;
#endif
  } clo_owner;
} state_lock_owner_t;

typedef struct state_t
{
  state_type_t state_type;
  union state_data__
  {
    state_share_t share;
    state_lock_t lock;
    state_deleg_t deleg;
    state_layout_t layout;
  } state_data;
  u_int32_t seqid;                                       /**< The NFSv4 Sequence id                      */
  char stateid_other[12];                                /**< "Other" part of state id, used as hash key */
  state_open_owner_t *powner;                      /**< Open Owner related to this state           */
  struct state_state__ *next;                      /**< Next entry in the state list               */
  struct state_state__ *prev;                      /**< Prev entry in the state list               */
  struct cache_entry__ *pentry;                          /**< Related pentry                             */
} state_t;

typedef union state_data__ state_data_t;

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

typedef struct state_lock_entry_t   state_lock_entry_t;
typedef struct state_cookie_entry_t state_cookie_entry_t;

/* The granted call back is responsible for acquiring a reference to
 * the lock entry if needed.
 */
typedef state_status_t (*granted_callback_s)(cache_entry_t        * pentry,
                                             state_lock_entry_t   * lock_entry,
                                             cache_inode_client_t * pclient,
                                             state_status_t       * pstatus);

#ifdef _USE_NLM
typedef struct state_nlm_block_data_t
{
  sockaddr_t                 sbd_nlm_hostaddr;
  netobj                     sbd_nlm_fh;
  char                       sbd_nlm_fh_buf[MAX_NETOBJ_SZ];
  uid_t                      sbd_nlm_caller_uid;
  gid_t                      sbd_nlm_caller_gid;
  unsigned int               sbd_nlm_caller_glen;
  gid_t                      sbd_nlm_caller_garray[NGRPS];
} state_nlm_block_data_t;
#endif

typedef struct state_block_data_t
{
  granted_callback_s           sbd_granted_callback;
  state_cookie_entry_t       * sbd_blocked_cookie;
  union
    {
#ifdef _USE_NLM
      state_nlm_block_data_t   sbd_nlm_block_data;
#endif
      void                   * sbd_v4_block_data;
    } cbd_block_data;
} state_block_data_t;

struct state_lock_entry_t
{
  struct glist_head             sle_list;
  struct glist_head             sle_owner_locks;
#ifdef _USE_NLM
  struct glist_head             sle_client_locks;
#endif
#ifdef _DEBUG_MEMLEAKS
  struct glist_head             sle_all_locks;
#endif
  int                           sle_ref_count;
  unsigned long long            sle_fileid;
  cache_entry_t               * sle_pentry;
  state_blocking_t              sle_blocked;
  state_block_data_t          * sle_block_data;
  state_lock_owner_t          * sle_owner;
  state_lock_desc_t             sle_lock;
  pthread_mutex_t               sle_mutex;
};

#ifdef _USE_NLM
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
  pthread_mutex_t     sce_mutex;
  int                 sce_refcount;
  cache_entry_t      *sce_pentry;
  state_lock_entry_t *sce_lock_entry;
};
#endif


#endif                          /*  _SAL_DATA_H */
