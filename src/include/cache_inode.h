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
 * \file    cache_inode.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:15 $
 * \version $Revision: 1.95 $
 * \brief   Management of the cached inode layer. 
 *
 * cache_inode.h : Management of the cached inode layer
 *
 *
 */

#ifndef _CACHE_INODE_H
#define _CACHE_INODE_H

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
#include "avltree.h"
#include "fsal.h"
#ifdef _USE_MFSL
#include "mfsl.h"
#endif
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

/* forward references */
typedef struct cache_entry_t        cache_entry_t;
typedef struct cache_inode_client_t cache_inode_client_t;

/* Some habits concerning mutex management */
#ifndef P
#define P( a ) pthread_mutex_lock( &a )
#endif

#ifndef V
#define V( a ) pthread_mutex_unlock( &a )
#endif

#define FILEHANDLE_MAX_LEN_V2 32
#define FILEHANDLE_MAX_LEN_V3 64
#define FILEHANDLE_MAX_LEN_V4 128
/* Take care before changing the nest define, it has very strong impact on the memory use */
/* #define CHILDREN_ARRAY_SIZE 16 */
/* #define CHILDREN_ARRAY_SIZE 64 */
#define CHILDREN_ARRAY_SIZE 16
#define NB_CHUNCK_READDIR 4     /* Should be equal to FSAL_READDIR_SIZE divided by CHILDREN_ARRAY_SIZE */

#define CACHE_INODE_UNSTABLE_BUFFERSIZE 100*1024*1024
#define DIR_ENTRY_NAMLEN 1024

#define CACHE_INODE_TIME( __pentry ) (__pentry->internal_md.read_time > __pentry->internal_md.mod_time)?__pentry->internal_md.read_time:__pentry->internal_md.mod_time

#define CACHE_INODE_KEEP_CONTENT( __policy ) ((__policy==CACHE_INODE_POLICY_FULL_WRITE_THROUGH)||(__policy==CACHE_INODE_POLICY_FULL_WRITE_BACK ) )?1:0 
              

#define CONF_LABEL_CACHE_INODE_GCPOL  "CacheInode_GC_Policy"
#define CONF_LABEL_CACHE_INODE_CLIENT "CacheInode_Client"
#define CONF_LABEL_CACHE_INODE_HASH   "CacheInode_Hash"

#define CACHE_INODE_DUMP_LEN 1024

extern char *cache_inode_function_names[];
#define CACHE_INODE_ACCESS               0
#define CACHE_INODE_GETATTR              1
#define CACHE_INODE_MKDIR                2
#define CACHE_INODE_REMOVE               3
#define CACHE_INODE_STATFS               4
#define CACHE_INODE_LINK                 5
#define CACHE_INODE_READDIR              6
#define CACHE_INODE_RENAME               7
#define CACHE_INODE_SYMLINK              8
#define CACHE_INODE_CREATE               9
#define CACHE_INODE_LOOKUP              10
#define CACHE_INODE_LOOKUPP             11
#define CACHE_INODE_READLINK            12
#define CACHE_INODE_TRUNCATE            13
#define CACHE_INODE_GET                 14
#define CACHE_INODE_RELEASE             15
#define CACHE_INODE_SETATTR             16
#define CACHE_INODE_NEW_ENTRY           17
#define CACHE_INODE_READ_DATA           18
#define CACHE_INODE_WRITE_DATA          19
#define CACHE_INODE_ADD_DATA_CACHE      20
#define CACHE_INODE_RELEASE_DATA_CACHE  21
#define CACHE_INODE_RENEW_ENTRY         22
#define CACHE_INODE_COMMIT              23
#define CACHE_INODE_ADD_STATE           24
#define CACHE_INODE_DEL_STATE           25
#define CACHE_INODE_GET_STATE           26
#define CACHE_INODE_SET_STATE           27

#define CACHE_INODE_NB_COMMAND      28

typedef enum cache_inode_policy__
{
  CACHE_INODE_POLICY_FULL_WRITE_THROUGH       = 0, /**< Caches everything (attrs, directory content and symlink content  */
  CACHE_INODE_POLICY_FULL_WRITE_BACK          = 1, /**< Caches everything, but with an asynchronous logic behind         */
  CACHE_INODE_POLICY_ATTRS_ONLY_WRITE_THROUGH = 2, /**< Caches only attributes, no directory content nor symlink content */
  CACHE_INODE_POLICY_NO_CACHE                 = 3, /**< Nothing is cached at all                                         */
  CACHE_INODE_JOKER_POLICY                    = 4  /**< This policy is used when policy is unknown or doesn't matter     */
} cache_inode_policy_t ;

typedef enum cache_inode_expire_type__
{ 
  CACHE_INODE_EXPIRE = 0,
  CACHE_INODE_EXPIRE_NEVER = 1,
  CACHE_INODE_EXPIRE_IMMEDIATE = 2
} cache_inode_expire_type_t;

typedef struct cache_inode_stat__
{
  unsigned int nb_gc_lru_active;        /**< Number of active entries in Garbagge collecting list */
  unsigned int nb_gc_lru_total;         /**< Total mumber of entries in Garbagge collecting list  */

  struct func_inode_stats__
  {
    unsigned int nb_call[CACHE_INODE_NB_COMMAND];                         /**< total number of calls per functions     */
    unsigned int nb_success[CACHE_INODE_NB_COMMAND];                      /**< succesfull calls per function           */
    unsigned int nb_err_retryable[CACHE_INODE_NB_COMMAND];                /**< failed/retryable calls per function     */
    unsigned int nb_err_unrecover[CACHE_INODE_NB_COMMAND];                /**< failed/unrecoverable calls per function */
  } func_stats;
  unsigned int nb_call_total;                                       /**< Total number of calls */
} cache_inode_stat_t;

typedef struct cache_inode_parameter__
{
  hash_parameter_t hparam;                      /**< Parameter used for hashtable initialization */
#ifdef _USE_NLM
  hash_parameter_t cookie_param;                /**< Parameters used for cookie hash table initialization */
#endif
} cache_inode_parameter_t;

typedef struct cache_inode_client_parameter__
{
  LRU_parameter_t lru_param;                           /**< LRU list handle (used for gc)                    */
  fsal_attrib_mask_t attrmask;                         /**< FSAL attributes to be used in FSAL               */
  unsigned int nb_prealloc_entry;                      /**< number of preallocated pentries                  */
  unsigned int nb_pre_parent;                          /**< number of preallocated parent link               */
  unsigned int nb_pre_state_v4;                        /**< number of preallocated State_v4                  */
  unsigned int nb_pre_lock;                            /**< number of preallocated file lock                 */
  cache_inode_expire_type_t expire_type_attr;          /**< Cache inode expiration type for attributes       */
  cache_inode_expire_type_t expire_type_link;          /**< Cache inode expiration type for symbolic links   */
  cache_inode_expire_type_t expire_type_dirent;        /**< Cache inode expiration type for directory entries*/
  time_t grace_period_attr;                            /**< Cached attributes grace period                   */
  time_t grace_period_link;                            /**< Cached link grace period                         */
  time_t grace_period_dirent;                          /**< Cached dirent grace period                       */
  unsigned int getattr_dir_invalidation;               /**< Use getattr as cookie for directory invalidation */
  unsigned int use_test_access;                        /**< Is FSAL_test_access to be used ?                 */
  unsigned int max_fd_per_thread;                      /**< Max fd open per client                           */
  time_t retention;                                    /**< Fd retention duration                            */
  unsigned int use_cache;                              /** Do we cache fd or not ?                           */
  unsigned int use_fsal_hash ;                         /** Do we rely on FSAL to hash handle or not ?        */
} cache_inode_client_parameter_t;

typedef struct cache_inode_opened_file__
{
#ifdef _USE_MFSL
  mfsl_file_t mfsl_fd ;
#else
  fsal_file_t fd;
#endif 
  unsigned int fileno;
  fsal_openflags_t openflags;
  time_t last_op;
} cache_inode_opened_file_t;

typedef enum cache_inode_file_type__
{ UNASSIGNED = 1,
  REGULAR_FILE = 2,
  CHARACTER_FILE = 3,
  BLOCK_FILE = 4,
  SYMBOLIC_LINK = 5,
  SOCKET_FILE = 6,
  FIFO_FILE = 7,
  DIRECTORY = 8,
  FS_JUNCTION = 9,
  RECYCLED = 10
} cache_inode_file_type_t;

typedef enum cache_inode_lock_how__
{
  NO_LOCK = 0,
  RD_LOCK  = 1,
  WT_LOCK  = 2
} cache_inode_lock_how_t ;

typedef enum cache_inode_endofdir__
{ TO_BE_CONTINUED = 1,
  END_OF_DIR = 2,
  UNASSIGNED_EOD = 3
} cache_inode_endofdir_t;

typedef enum cache_inode_entry_valid_state__
{ VALID = 1,
  INVALID = 2,
  STALE = 3
} cache_inode_entry_valid_state_t;

typedef enum cache_inode_op__
{ CACHE_INODE_OP_GET = 1,
  CACHE_INODE_OP_SET = 2
} cache_inode_op_t;

typedef enum cache_inode_io_direction__
{ CACHE_INODE_READ = 1,
  CACHE_INODE_WRITE = 2
} cache_inode_io_direction_t;

typedef enum cache_inode_flag__
{ CACHE_INODE_YES = 1,
  CACHE_INODE_NO = 2,
  CACHE_INODE_RENEW_NEEDED = 3
} cache_inode_flag_t;

typedef enum cache_inode_dirent_op__
{ CACHE_INODE_DIRENT_OP_LOOKUP = 1,
  CACHE_INODE_DIRENT_OP_REMOVE = 2,
  CACHE_INODE_DIRENT_OP_RENAME = 3
} cache_inode_dirent_op_t;

typedef enum cache_inode_avl_which__
{ CACHE_INODE_AVL_NAMES = 1,
  CACHE_INODE_AVL_COOKIES = 2,
  CACHE_INODE_AVL_BOTH = 3
} cache_inode_avl_which_t;

typedef struct cache_inode_internal_md__
{
  cache_inode_file_type_t type;                            /**< The type of the entry                                */
  cache_inode_entry_valid_state_t valid_state;             /**< Is this entry valid or invalid ?                     */
  time_t read_time;                                        /**< Epoch time of the last read operation on the entry   */
  time_t mod_time;                                         /**< Epoch time of the last change operation on the entry */
  time_t refresh_time;                                     /**< Epoch time of the last update operation on the entry */
  time_t alloc_time;                                       /**< Epoch time of the allocation for this entry          */
} cache_inode_internal_md_t;

struct cache_inode_symlink__
{
  fsal_handle_t handle;                                   /**< The FSAL Handle     */
  fsal_attrib_list_t attributes;                          /**< The FSAL Attributes */
  fsal_path_t content;                                    /**< Content of the link */
};

typedef struct cache_inode_unstable_data__
{
  caddr_t buffer;
  uint64_t offset;
  uint32_t length;
} cache_inode_unstable_data_t;

struct cache_inode_dir_entry__
{
    struct avltree_node node_n; /* avl keyed on name */
    struct avltree_node node_c; /* avl keyed on cookie */
    cache_entry_t *pentry;
    fsal_name_t name;
    uint64_t cookie;
    uint64_t fsal_cookie;
};

struct cache_entry_t
{
  cache_inode_policy_t  policy ;                                     /**< The current cache policy for this entry               */

  union cache_inode_fsobj__
  {
    struct cache_inode_file__
    {
      fsal_handle_t handle;                                          /**< The FSAL Handle                                      */
#ifdef _USE_PNFS_SPNFS_LIKE
      fsal_pnfs_file_t pnfs_file ;                                   /**< Specific FS information for pNFS management          */
#endif
      cache_inode_opened_file_t open_fd;                             /**< Cached fsal_file_t for optimized access              */
#ifdef _USE_PROXY
      fsal_name_t *pname;                                            /**< Pointer to filename, for PROXY only                  */
      cache_entry_t *pentry_parent_open;                             /**< Parent associated with pname, for PROXY only         */
#endif                          /* _USE_PROXY */
      fsal_attrib_list_t attributes;                                 /**< The FSAL Attributes                                  */
      void *pentry_content;                                          /**< Entry in file content cache (NULL if not cached)     */
      struct glist_head state_list;                                  /**< Pointers for state list                              */
      struct glist_head lock_list;                                   /**< Pointers for lock list                               */
      pthread_mutex_t lock_list_mutex;                               /**< Mutex to protect lock list                           */
      cache_inode_unstable_data_t unstable_data;                     /**< Unstable data, for use with WRITE/COMMIT             */
    } file;                                   /**< file related filed     */

    struct cache_inode_symlink__ *symlink;     /**< symlink related field  */

    struct cache_inode_dir__
    {
      fsal_handle_t handle;                     /**< The FSAL Handle                                         */
      fsal_attrib_list_t attributes;            /**< The FSAL Attributes                                     */
      unsigned int nbactive;                    /**< Number of known active children                         */
      cache_inode_flag_t has_been_readdir;      /**< True if a full readdir was performed on the directory   */
      char *referral;                           /**< NULL is not a referral, is not this a 'referral string' */
      struct avltree dentries;                  /**< Children */
      struct avltree cookies;                   /**< sparse offset avl */
    } dir;                                /**< DIR related field                               */

    struct cache_inode_special_object__
    {
      fsal_handle_t handle;                     /**< The FSAL Handle                                         */
      fsal_attrib_list_t attributes;            /**< The FSAL Attributes                                     */
      /* Note that special data is in the rawdev field of FSAL attributes */
    } special_obj;

  } object;                                     /**< Type specific field (discriminated by internal_md.type)   */

  rw_lock_t lock;                             /**< a reader-writter lock used to protect the data     */
  cache_inode_internal_md_t internal_md;      /**< My metadata (from this cache's point of view)      */
  LRU_entry_t *gc_lru_entry;                  /**< related LRU entry in the LRU list used for GC      */
  LRU_list_t *gc_lru;                         /**< related LRU list for GC                            */

 /* List of parent cache entries of directory entries related by
  * hard links */       
  struct cache_inode_parent_entry__
  {
    cache_entry_t *parent;                           /**< Parent entry */
    uint64_t cookie;                                 /**< Key in sparse avl */
    struct cache_inode_parent_entry__ *next_parent;  /**< Next parent */
  } *parent_list;
#ifdef _USE_MFSL
  mfsl_object_t mobject;
#endif
};

typedef struct cache_inode_dir_entry__ cache_inode_dir_entry_t;
typedef struct cache_inode_file__ cache_inode_file_t;
typedef struct cache_inode_symlink__ cache_inode_symlink_t;
typedef union cache_inode_fsobj__ cache_inode_fsobj_t;
typedef struct cache_inode_parent_entry__ cache_inode_parent_entry_t;

typedef struct cache_inode_fsal_data__
{
  fsal_handle_t handle;                         /**< FSAL handle           */
  uint64_t cookie;                              /**< Cache inode cookie    */
} cache_inode_fsal_data_t;

#define SMALL_CLIENT_INDEX 0x20000000
#define NLM_THREAD_INDEX   0x40000000

struct cache_inode_client_t
{
  LRU_list_t *lru_gc;                                              /**< Pointer to the worker's LRU used for Garbagge collection */
  struct prealloc_pool pool_entry;                                 /**< Worker's preallocad cache entries pool                   */
  struct prealloc_pool pool_entry_symlink;                         /**< Symlink data for cache entries of type symlink           */
  struct prealloc_pool pool_dir_entry;                             /**< Worker's preallocated cache dir entry pool            */
  struct prealloc_pool pool_parent;                                /**< Pool of pointers to the parent entries                   */
  struct prealloc_pool pool_key;                                   /**< Pool for building hash's keys                            */
  struct prealloc_pool pool_state_v4;                              /**< Pool for NFSv4 files's states                            */
  struct prealloc_pool pool_state_owner;                           /**< Pool for NFSv4 files's open owner                        */
  struct prealloc_pool pool_nfs4_owner_name;                       /**< Pool for NFSv4 files's open_owner                        */
#ifdef _USE_NFS4_1
  struct prealloc_pool pool_session;                               /**< Pool for NFSv4.1 session                                 */
#endif                          /* _USE_NFS4_1 */
  unsigned int nb_prealloc;                                        /**< Size of the preallocated pool                            */
  unsigned int nb_pre_parent;                                      /**< Number of preallocated parent list entries               */
  unsigned int nb_pre_state_v4;                                    /**< Number of preallocated NFSv4 File States                 */
  fsal_attrib_mask_t attrmask;                                     /**< Mask of the supported attributes for the underlying FSAL */
  cache_inode_stat_t stat;                                         /**< Cache inode statistics for this client                   */
  cache_inode_expire_type_t expire_type_attr;                      /**< Cache inode expiration type for attributes               */
  cache_inode_expire_type_t expire_type_link;                      /**< Cache inode expiration type for symbolic links           */
  cache_inode_expire_type_t expire_type_dirent;                    /**< Cache inode expiration type for directory entries        */
  time_t grace_period_attr;                                        /**< Cached attributes grace period                           */
  time_t grace_period_link;                                        /**< Cached link grace period                                 */
  time_t grace_period_dirent;                                      /**< Cached directory entries grace period                    */
  unsigned int use_test_access;                                    /**< Is FSAL_test_access to be used instead of FSAL_access    */
  unsigned int getattr_dir_invalidation;                           /**< Use getattr as cookie for directory invalidation         */
  unsigned int call_since_last_gc;                                 /**< Number of call to cache_inode since the last gc run      */
  time_t time_of_last_gc;                                          /**< Epoch time for the last gc run for this thread           */
  time_t time_of_last_gc_fd;                                       /**< Epoch time for the last file descriptor gc               */
  caddr_t pcontent_client;                                         /**< Pointer to cache content client                          */
  void *pworker;                                                   /**< Pointer to the information on the worker I belong to     */
  unsigned int max_fd_per_thread;                                  /**< Max fd open per client                                   */
  time_t retention;                                                /**< Fd retention duration                                    */
  unsigned int use_cache;                                          /** Do we cache fd or not ?                                   */
  int fd_gc_needed;                                                /**< Should we perform fd gc ?                                */
#ifdef _USE_MFSL
  mfsl_context_t mfsl_context;                                     /**< Context to be used for MFSL module                       */
#endif
};

typedef struct cache_inode_gc_policy__
{
  signed int file_expiration_delay;           /**< maximum lifetime for a non directory entry             */
  signed int directory_expiration_delay;      /**< maximum lifetime for a directory entry                 */
  unsigned int hwmark_nb_entries;             /**< high water mark for cache_inode gc (number of entries) */
  unsigned int lwmark_nb_entries;             /**< low water mark for cache_inode gc (number of entries)  */
  unsigned int run_interval;                  /**< garbagge collection run-time interval                  */
  unsigned int nb_call_before_gc;             /**< Number of call to be made before thinking about gc run */
} cache_inode_gc_policy_t;

typedef struct cache_inode_param_gc__
{
  cache_inode_client_t *pclient;
  hash_table_t *ht;
  unsigned int nb_to_be_purged;
} cache_inode_param_gc_t;

typedef union cache_inode_create_arg__
{
  fsal_path_t link_content;
  fsal_dev_t dev_spec;
  bool_t use_pnfs;
} cache_inode_create_arg_t;

#define DIR_START     0

/*
 * Prototypes for the functions 
 */

/* Misc function */

/*
 * Possible errors 
 */
typedef enum cache_inode_status_t
{
  CACHE_INODE_SUCCESS               = 0,
  CACHE_INODE_MALLOC_ERROR          = 1,
  CACHE_INODE_POOL_MUTEX_INIT_ERROR = 2,
  CACHE_INODE_GET_NEW_LRU_ENTRY     = 3,
  CACHE_INODE_UNAPPROPRIATED_KEY    = 4,
  CACHE_INODE_INIT_ENTRY_FAILED     = 5,
  CACHE_INODE_FSAL_ERROR            = 6,
  CACHE_INODE_LRU_ERROR             = 7,
  CACHE_INODE_HASH_SET_ERROR        = 8,
  CACHE_INODE_NOT_A_DIRECTORY       = 9,
  CACHE_INODE_INCONSISTENT_ENTRY    = 10,
  CACHE_INODE_BAD_TYPE              = 11,
  CACHE_INODE_ENTRY_EXISTS          = 12,
  CACHE_INODE_DIR_NOT_EMPTY         = 13,
  CACHE_INODE_NOT_FOUND             = 14,
  CACHE_INODE_INVALID_ARGUMENT      = 15,
  CACHE_INODE_INSERT_ERROR          = 16,
  CACHE_INODE_HASH_TABLE_ERROR      = 17,
  CACHE_INODE_FSAL_EACCESS          = 18,
  CACHE_INODE_IS_A_DIRECTORY        = 19,
  CACHE_INODE_FSAL_EPERM            = 20,
  CACHE_INODE_NO_SPACE_LEFT         = 21,
  CACHE_INODE_CACHE_CONTENT_ERROR   = 22,
  CACHE_INODE_CACHE_CONTENT_EXISTS  = 23,
  CACHE_INODE_CACHE_CONTENT_EMPTY   = 24,
  CACHE_INODE_READ_ONLY_FS          = 25,
  CACHE_INODE_IO_ERROR              = 26,
  CACHE_INODE_FSAL_ESTALE           = 27,
  CACHE_INODE_FSAL_ERR_SEC          = 28,
  CACHE_INODE_STATE_CONFLICT        = 29,
  CACHE_INODE_QUOTA_EXCEEDED        = 30,
  CACHE_INODE_DEAD_ENTRY            = 31,
  CACHE_INODE_ASYNC_POST_ERROR      = 32,
  CACHE_INODE_NOT_SUPPORTED         = 33,
  CACHE_INODE_STATE_ERROR           = 34,
  CACHE_INODE_FSAL_DELAY            = 35,
  CACHE_INODE_NAME_TOO_LONG         = 36,
  CACHE_INODE_BAD_COOKIE            = 40,
  CACHE_INODE_FILE_BIG              = 41,
} cache_inode_status_t;

const char *cache_inode_err_str(cache_inode_status_t err);

#define inc_func_call(pclient, x)                       \
    pclient->stat.func_stats.nb_call[x] += 1
#define inc_func_success(pclient, x)                    \
    pclient->stat.func_stats.nb_success[x] += 1
#define inc_func_err_retryable(pclient, x)              \
    pclient->stat.func_stats.nb_err_retryable[x] += 1
#define inc_func_err_unrecover(pclient, x)              \
    pclient->stat.func_stats.nb_err_unrecover[x] += 1

cache_inode_status_t cache_inode_clean_entry(cache_entry_t * pentry);

int cache_inode_compare_key_fsal(hash_buffer_t * buff1, hash_buffer_t * buff2);

int cache_inode_fsaldata_2_key(hash_buffer_t * pkey, cache_inode_fsal_data_t * pfsdata,
                               cache_inode_client_t * pclient);

void cache_inode_release_fsaldata_key(hash_buffer_t * pkey,
                                      cache_inode_client_t * pclient);

void cache_inode_release_symlink(cache_entry_t * pentry,
                                 struct prealloc_pool *pool);

hash_table_t *cache_inode_init(cache_inode_parameter_t param,
                               cache_inode_status_t * pstatus);

int cache_inode_client_init(cache_inode_client_t * pclient,
                            cache_inode_client_parameter_t param,
                            int thread_index, void *pworker_data);

cache_entry_t *cache_inode_get(cache_inode_fsal_data_t * pfsdata,
			       cache_inode_policy_t policy,
                               fsal_attrib_list_t * pattr,
                               hash_table_t * ht,
                               cache_inode_client_t * pclient,
                               fsal_op_context_t * pcontext,
                               cache_inode_status_t * pstatus);

cache_entry_t *cache_inode_get_located(cache_inode_fsal_data_t * pfsdata,
                                       cache_entry_t * plocation,
				       cache_inode_policy_t policy,
                                       fsal_attrib_list_t * pattr,
                                       hash_table_t * ht,
                                       cache_inode_client_t * pclient,
                                       fsal_op_context_t * pcontext,
                                       cache_inode_status_t * pstatus) ;

cache_inode_status_t cache_inode_access_sw(cache_entry_t * pentry,
                                           fsal_accessflags_t access_type,
                                           hash_table_t * ht,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_inode_status_t * pstatus, int use_mutex);

cache_inode_status_t cache_inode_access_no_mutex(cache_entry_t * pentry,
                                                 fsal_accessflags_t access_type,
                                                 hash_table_t * ht,
                                                 cache_inode_client_t * pclient,
                                                 fsal_op_context_t * pcontext,
                                                 cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_access(cache_entry_t * pentry,
                                        fsal_accessflags_t access_type,
                                        hash_table_t * ht,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus);

/* functio not found in sources ??!!?? */
#ifdef _USE_SWIG________
cache_inode_status_t cache_inode_close(cache_entry_t * pentry,
                                       fsal_attrib_list_t * pattr,
                                       hash_table_t * ht,
                                       cache_inode_client_t * pclient,
                                       fsal_op_context_t * pcontext,
                                       cache_inode_status_t * pstatus);
#endif

#ifdef _USE_MFSL
mfsl_file_t * cache_inode_fd(cache_entry_t * pentry);
#else
fsal_file_t * cache_inode_fd(cache_entry_t * pentry);
#endif

cache_inode_status_t cache_inode_open(cache_entry_t * pentry,
                                      cache_inode_client_t * pclient,
                                      fsal_openflags_t openflags,
                                      fsal_op_context_t * pcontext,
                                      cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_open_by_name(cache_entry_t * pentry,
                                              fsal_name_t * pname,
                                              cache_entry_t * pentry_file,
                                              cache_inode_client_t * pclient,
                                              fsal_openflags_t openflags,
                                              fsal_op_context_t * pcontext,
                                              cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_close(cache_entry_t * pentry,
                                       cache_inode_client_t * pclient,
                                       cache_inode_status_t * pstatus);

cache_entry_t *cache_inode_create(cache_entry_t * pentry_parent,
                                  fsal_name_t * pname,
                                  cache_inode_file_type_t type,
                                  cache_inode_policy_t policy,
                                  fsal_accessmode_t mode,
                                  cache_inode_create_arg_t * pcreate_arg,
                                  fsal_attrib_list_t * pattr,
                                  hash_table_t * ht,
                                  cache_inode_client_t * pclient,
                                  fsal_op_context_t * pcontext,
                                  cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_getattr(cache_entry_t * pentry,
                                         fsal_attrib_list_t * pattr,
                                         hash_table_t * ht,
                                         cache_inode_client_t * pclient,
                                         fsal_op_context_t * pcontext,
                                         cache_inode_status_t * pstatus);

cache_entry_t *cache_inode_lookup_sw( cache_entry_t * pentry_parent,
                                      fsal_name_t * pname,
                                      cache_inode_policy_t policy,
                                      fsal_attrib_list_t * pattr,
                                      hash_table_t * ht,
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t * pcontext,
                                      cache_inode_status_t * pstatus, int use_mutex);

cache_entry_t *cache_inode_lookup_no_mutex(cache_entry_t * pentry_parent,
                                           fsal_name_t * pname,
                                           cache_inode_policy_t policy,
                                           fsal_attrib_list_t * pattr,
                                           hash_table_t * ht,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_inode_status_t * pstatus);

cache_entry_t *cache_inode_lookup( cache_entry_t * pentry_parent,
                                   fsal_name_t * pname,
                  		   cache_inode_policy_t policy,
                                   fsal_attrib_list_t * pattr,
                                   hash_table_t * ht,
                                   cache_inode_client_t * pclient,
                                   fsal_op_context_t * pcontext,
                                   cache_inode_status_t * pstatus);

cache_entry_t *cache_inode_lookupp_sw(cache_entry_t * pentry,
                                      hash_table_t * ht,
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t * pcontext,
                                      cache_inode_status_t * pstatus, int use_mutex);

cache_entry_t *cache_inode_lookupp_no_mutex(cache_entry_t * pentry,
                                            hash_table_t * ht,
                                            cache_inode_client_t * pclient,
                                            fsal_op_context_t * pcontext,
                                            cache_inode_status_t * pstatus);

cache_entry_t *cache_inode_lookupp(cache_entry_t * pentry,
                                   hash_table_t * ht,
                                   cache_inode_client_t * pclient,
                                   fsal_op_context_t * pcontext,
                                   cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_readlink(cache_entry_t * pentry,
                                          fsal_path_t * plink_content,
                                          hash_table_t * ht,
                                          cache_inode_client_t * pclient,
                                          fsal_op_context_t * pcontext,
                                          cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_link( cache_entry_t * pentry_src, 
                                       cache_entry_t * pentry_dir_dest,
                                       fsal_name_t * plink_name, 
                                       cache_inode_policy_t policy,
                                       fsal_attrib_list_t * pattr,        /* the directory attributes */
                                       hash_table_t * ht,
                                       cache_inode_client_t * pclient,
                                       fsal_op_context_t * pcontext,
                                       cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_remove_sw(cache_entry_t * pentry,             /**< Parent entry */
                                           fsal_name_t * pnode_name,
                                           fsal_attrib_list_t * pattr,
                                           hash_table_t * ht,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_inode_status_t * pstatus, int use_mutex);

cache_inode_status_t cache_inode_remove_no_mutex(cache_entry_t * pentry,
                                                 fsal_name_t * pnode_name,
                                                 fsal_attrib_list_t * pattr,
                                                 hash_table_t * ht,
                                                 cache_inode_client_t * pclient,
                                                 fsal_op_context_t * pcontext,
                                                 cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_remove(cache_entry_t * pentry,
                                        fsal_name_t * pnode_name,
                                        fsal_attrib_list_t * pattr,
                                        hash_table_t * ht,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_clean_internal(cache_entry_t * to_remove_entry,
                                                hash_table_t * ht,
                                                cache_inode_client_t * pclient);

cache_entry_t *cache_inode_operate_cached_dirent(cache_entry_t * pentry_parent,
                                                 fsal_name_t * pname,
                                                 fsal_name_t * newname,
                                                 cache_inode_client_t * pclient,
                                                 cache_inode_dirent_op_t dirent_op,
                                                 cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_remove_cached_dirent(cache_entry_t * pentry_parent,
                                                      fsal_name_t * pname,
                                                      hash_table_t * ht,
                                                      cache_inode_client_t * pclient,
                                                      cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_rename_cached_dirent(cache_entry_t * pentry_parent,
                                                      fsal_name_t * oldname,
                                                      fsal_name_t * newname,
                                                      hash_table_t * ht,
                                                      cache_inode_client_t * pclient,
                                                      cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_rename(cache_entry_t * pentry,
                                        fsal_name_t * poldname,
                                        cache_entry_t * pentry_dirdest,
                                        fsal_name_t * pnewname,
                                        fsal_attrib_list_t * pattr_src,
                                        fsal_attrib_list_t * pattr_dst,
                                        hash_table_t * ht,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_setattr(cache_entry_t * pentry, fsal_attrib_list_t * pattr,    /* INOUT */
                                         hash_table_t * ht,
                                         cache_inode_client_t * pclient,
                                         fsal_op_context_t * pcontext,
                                         cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_truncate_sw(cache_entry_t * pentry,
                                             fsal_size_t length,
                                             fsal_attrib_list_t * pattr,
                                             hash_table_t * ht,
                                             cache_inode_client_t * pclient,
                                             fsal_op_context_t * pcontext,
                                             cache_inode_status_t * pstatus,
                                             int use_mutex);

cache_inode_status_t cache_inode_truncate_no_mutex(cache_entry_t * pentry,
                                                   fsal_size_t length,
                                                   fsal_attrib_list_t * pattr,
                                                   hash_table_t * ht,
                                                   cache_inode_client_t * pclient,
                                                   fsal_op_context_t * pcontext,
                                                   cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_truncate(cache_entry_t * pentry,
                                          fsal_size_t length,
                                          fsal_attrib_list_t * pattr,
                                          hash_table_t * ht,
                                          cache_inode_client_t * pclient,
                                          fsal_op_context_t * pcontext,
                                          cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_error_convert(fsal_status_t fsal_status);

cache_entry_t *cache_inode_new_entry(cache_inode_fsal_data_t * pfsdata,
                                     fsal_attrib_list_t * pfsal_attr,
                                     cache_inode_file_type_t type,
                                     cache_inode_policy_t policy,
                                     cache_inode_create_arg_t * pcreate_arg,
                                     cache_entry_t * pentry_dir_prev,
                                     hash_table_t * ht,
                                     cache_inode_client_t * pclient,
                                     fsal_op_context_t * pcontext,
                                     unsigned int create_flag,
                                     cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_add_data_cache(cache_entry_t * pentry,
                                                hash_table_t * ht,
                                                cache_inode_client_t * pclient,
                                                fsal_op_context_t * pcontext,
                                                cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_release_data_cache(cache_entry_t * pentry,
                                                    hash_table_t * ht,
                                                    cache_inode_client_t * pclient,
                                                    fsal_op_context_t * pcontext,
                                                    cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_rdwr(cache_entry_t * pentry,
                                      cache_inode_io_direction_t read_or_write,
                                      fsal_seek_t * seek_descriptor,
                                      fsal_size_t buffer_size,
                                      fsal_size_t * pread_size,
                                      fsal_attrib_list_t * pfsal_attr,
                                      caddr_t buffer,
                                      fsal_boolean_t * p_fsal_eof,
                                      hash_table_t * ht,
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t * pcontext,
                                      uint64_t stable, cache_inode_status_t * pstatus);

#define cache_inode_read( a, b, c, d, e, f, g, h, i, j, k ) cache_inode_rdwr( a, CACHE_INODE_READ, b, c, d, e, f, g, h, i, j, k )
#define cache_inode_write( a, b, c, d, e, f, g, h, i, j, k ) cache_inode_rdwr( a, CACHE_INODE_WRITE, b, c, d, e, f, g, h, i, j. k )

cache_inode_status_t cache_inode_commit(cache_entry_t * pentry,
                                        uint64_t offset,
                                        fsal_size_t count,
                                        fsal_attrib_list_t * pfsal_attr,
                                        hash_table_t * ht,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t * pcontext,
                                        uint64_t typeofcommit,
                                        cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_readdir_populate(cache_entry_t * pentry_dir,
                                                  cache_inode_policy_t policy,
                                                  hash_table_t * ht,
                                                  cache_inode_client_t * pclient,
                                                  fsal_op_context_t * pcontext,
                                                  cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_readdir( cache_entry_t * pentry,
                                          cache_inode_policy_t policy,
                                          uint64_t cookie,
                                          unsigned int nbwanted,
                                          unsigned int *pnbfound,
                                          uint64_t *pend_cookie,
                                          cache_inode_endofdir_t *peod_met,
                                          cache_inode_dir_entry_t **dirent_array,
                                          hash_table_t *ht,
                                          int *unlock,
                                          cache_inode_client_t *pclient,
                                          fsal_op_context_t *pcontext,
                                          cache_inode_status_t *pstatus);

cache_inode_status_t cache_inode_cookieverf(cache_entry_t * pentry,
                                            fsal_op_context_t * pcontext,
                                            uint64_t * pverf,
                                            cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_renew_entry(cache_entry_t * pentry,
                                             fsal_attrib_list_t * pattr,
                                             hash_table_t * ht,
                                             cache_inode_client_t * pclient,
                                             fsal_op_context_t * pcontext,
                                             cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_add_cached_dirent(cache_entry_t * pdir,
                                                   fsal_name_t * pname,
                                                   cache_entry_t * pentry_added,
                                                   hash_table_t * ht,
						   cache_inode_dir_entry_t **pnew_dir_entry,
                                                   cache_inode_client_t * pclient,
                                                   fsal_op_context_t * pcontext,
                                                   cache_inode_status_t * pstatus);

void cache_inode_release_dirent(  cache_inode_dir_entry_t **dirent_array,
                                  unsigned int howmuch,
                                  cache_inode_client_t *pclient ) ;

cache_entry_t *cache_inode_make_root(cache_inode_fsal_data_t * pfsdata,
                                     cache_inode_policy_t policy,
                                     hash_table_t * ht,
                                     cache_inode_client_t * pclient,
                                     fsal_op_context_t * pcontext,
                                     cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_valid(cache_entry_t * pentry,
                                       cache_inode_op_t op,
                                       cache_inode_client_t * pclient);

cache_inode_status_t cache_inode_invalidate_all_cached_dirent(cache_entry_t *
                                                              pentry_parent,
                                                              hash_table_t * ht,
                                                              cache_inode_client_t *
                                                              pclient,
                                                              cache_inode_status_t *
                                                              pstatus);

void cache_inode_set_attributes(cache_entry_t * pentry, fsal_attrib_list_t * pattr);

void cache_inode_get_attributes(cache_entry_t * pentry, fsal_attrib_list_t * pattr);

cache_inode_file_type_t cache_inode_fsal_type_convert(fsal_nodetype_t type);

int cache_inode_type_are_rename_compatible(cache_entry_t * pentry_src,
                                           cache_entry_t * pentry2);

void cache_inode_mutex_destroy(cache_entry_t * pentry);

void cache_inode_print_dir(cache_entry_t * cache_entry_root);

fsal_handle_t *cache_inode_get_fsal_handle(cache_entry_t * pentry,
                                           cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_statfs(cache_entry_t * pentry,
                                        fsal_dynamicfsinfo_t * dynamicinfo,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_is_dir_empty(cache_entry_t * pentry);
cache_inode_status_t cache_inode_is_dir_empty_WithLock(cache_entry_t * pentry);

cache_inode_status_t cache_inode_add_avl(cache_entry_t * pentry,
					 fsal_name_t * pname,
					 fsal_name_t * newname);

void cache_inode_release_dirents(cache_entry_t *pentry,
				 cache_inode_client_t *pclient,
				 cache_inode_avl_which_t which);

void cache_inode_invalidate_related_dirent(cache_entry_t * pentry,
					   uint64_t cookie,
					   cache_inode_client_t * pclient);

cache_inode_status_t cache_inode_gc(hash_table_t * ht,
                                    cache_inode_client_t * pclient,
                                    cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_gc_fd(cache_inode_client_t * pclient,
                                       cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_kill_entry( cache_entry_t * pentry,
                                             cache_inode_lock_how_t lock_how, 
                                             hash_table_t * ht,
                                             cache_inode_client_t * pclient,
                                             cache_inode_status_t * pstatus);

cache_inode_status_t cache_inode_invalidate( fsal_handle_t        * pfsal_handle,
                                             fsal_attrib_list_t   * pattr,
                                             hash_table_t         * ht,
                                             cache_inode_client_t * pclient,
                                             cache_inode_status_t * pstatus) ;

void cache_inode_invalidate_related_dirent( cache_entry_t * pentry,
                                            uint64_t cookie,
                                            cache_inode_client_t * pclient );

void cache_inode_invalidate_related_dirents(  cache_entry_t        * pentry,
                                              cache_inode_client_t * pclient);

int cache_inode_gc_suppress_directory(cache_entry_t * pentry,
                                      cache_inode_param_gc_t * pgcparam);
int cache_inode_gc_suppress_file(cache_entry_t * pentry,
                                 cache_inode_param_gc_t * pgcparam);

cache_inode_gc_policy_t cache_inode_get_gc_policy(void);
void cache_inode_set_gc_policy(cache_inode_gc_policy_t policy);

/* Parsing functions */
cache_inode_status_t cache_inode_read_conf_hash_parameter(config_file_t in_config,
                                                          cache_inode_parameter_t *
                                                          pparam);

cache_inode_status_t cache_inode_read_conf_client_parameter(config_file_t in_config,
                                                            cache_inode_client_parameter_t
                                                            * pparam);

cache_inode_status_t cache_inode_read_conf_gc_policy(config_file_t in_config,
                                                     cache_inode_gc_policy_t * ppolicy);

void cache_inode_print_conf_hash_parameter(FILE * output, cache_inode_parameter_t param);

void cache_inode_print_conf_client_parameter(FILE * output,
                                             cache_inode_client_parameter_t param);

void cache_inode_print_conf_gc_policy(FILE * output, cache_inode_gc_policy_t gcpolicy);

cache_inode_status_t cache_inode_dump_content(char *path, cache_entry_t * pentry);

cache_inode_status_t cache_inode_reload_content(char *path, cache_entry_t * pentry);

void cache_inode_expire_to_str(cache_inode_expire_type_t type, time_t value, char *out);

inline unsigned int cache_inode_file_holds_state( cache_entry_t * pentry );

inline int cache_inode_set_time_current( fsal_time_t * ptime ) ;

/* Hash functions for hashtables and RBT */
unsigned long cache_inode_fsal_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef);
unsigned long cache_inode_fsal_rbt_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef);
unsigned int cache_inode_fsal_rbt_both( hash_parameter_t * p_hparam,
				        hash_buffer_t    * buffclef, 
				        uint32_t * phashval, uint32_t * prbtval ) ;
int display_key(hash_buffer_t * pbuff, char *str);
int display_not_implemented(hash_buffer_t * pbuff, char *str);
int display_value(hash_buffer_t * pbuff, char *str);

#endif                          /*  _CACHE_INODE_H */
