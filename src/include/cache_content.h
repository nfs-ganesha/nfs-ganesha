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
 * \file    cache_content.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:41:01 $
 * \version $Revision: 1.29 $
 * \brief   Management of the cached content layer. 
 *
 * cache_content.h : Management of the cached content layer
 *
 *
 */

#ifndef _CACHE_CONTENT_H
#define _CACHE_CONTENT_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

typedef int cache_content_status_t;

typedef struct cache_content_gc_policy__
{
  time_t lifetime;
  time_t emergency_grace_delay;
  unsigned int run_interval;
  unsigned int nb_call_before_gc;
  unsigned int hwmark_df;
  unsigned int lwmark_df;
} cache_content_gc_policy_t;

#define CONF_LABEL_CACHE_CONTENT_GCPOL  "FileContent_GC_Policy"
#define CONF_LABEL_CACHE_CONTENT_CLIENT "FileContent_Client"
#define CONF_LABEL_CACHE_CONTENT_PARAM  "FileContent_Param"

#define CACHE_CONTENT_NEW_ENTRY     0
#define CACHE_CONTENT_RELEASE_ENTRY 1
#define CACHE_CONTENT_READ_ENTRY    2
#define CACHE_CONTENT_WRITE_ENTRY   3
#define CACHE_CONTENT_TRUNCATE      4
#define CACHE_CONTENT_FLUSH         5
#define CACHE_CONTENT_REFRESH       6

#define CACHE_CONTENT_NB_COMMAND    7

#define CACHE_CONTENT_FLAGS_READ       0x0000001
#define CACHE_CONTENT_FLAGS_WRITE      0x0000002
#define CACHE_CONTENT_FLAGS_READ_WRITE 0x0000003

typedef struct cache_content_opened_file__
{
  int local_fd;
  time_t last_op;
} cache_content_opened_file_t;

typedef enum cache_content_sync_state__
{ JUST_CREATED = 1,
  SYNC_OK = 2,
  FLUSH_NEEDED = 3,
  REFRESH_NEEDED = 4
} cache_content_sync_state_t;

typedef enum cache_content_entry_valid_state__
{ STATE_OK = 1,
  TO_BE_GARBAGGED = 2
} cache_content_entry_valid_state_t;

typedef enum cache_content_io_direction__
{ CACHE_CONTENT_READ = 1,
  CACHE_CONTENT_WRITE = 2
} cache_content_io_direction_t;

typedef enum cache_content_flush_behaviour__
{ CACHE_CONTENT_FLUSH_AND_DELETE = 1,
  CACHE_CONTENT_FLUSH_SYNC_ONLY = 2
} cache_content_flush_behaviour_t;
typedef struct cache_content_client_parameter__
{
  unsigned int nb_prealloc_entry;             /**< number of preallocated pentries */
  char cache_dir[MAXPATHLEN];                 /**< Path to the directory where data are cached */
  unsigned int flush_force_fsal;              /**< Should the flush force the write to FSAL    */
  unsigned int max_fd_per_thread;             /**< Max fd open per client */
  time_t retention;                           /**< Fd retention duration */
  unsigned int use_cache;                     /** Do we cache fd or not ? */
} cache_content_client_parameter_t;

#define CACHE_CONTENT_SPEC_DATA_SIZE 400
typedef char cache_content_spec_data_t[CACHE_CONTENT_SPEC_DATA_SIZE];

typedef struct cache_content_internal_md__
{
  time_t read_time;                                       /**< Epoch time of the last read operation on the entry   */
  time_t mod_time;                                        /**< Epoch time of the last change operation on the entry */
  time_t refresh_time;                                    /**< Epoch time of the last update operation on the entry */
  time_t alloc_time;                                      /**< Epoch time of the allocation for this entry          */
  time_t last_flush_time;                                 /**< Epoch time of the last flush                         */
  time_t last_refresh_time;                               /**< Epoch time of the last refresh                       */
  cache_content_entry_valid_state_t valid_state;          /**< Is this entry valid or invalid ?                     */
  cache_content_spec_data_t *pspecdata;                   /**< Pointer to the entry's specific data                 */

} cache_content_internal_md_t;

typedef struct cache_content_local_entry__
{
  char cache_path_data[MAXPATHLEN];                                /**< Path of the cached content                  */
  char cache_path_index[MAXPATHLEN];                               /**< Path to the index file (for crash recovery) */
  cache_content_opened_file_t opened_file;                         /**< Opened file descriptor related to the entry */
  cache_content_sync_state_t sync_state;                           /**< Is this entry synchronized ?                */
} cache_content_local_entry_t;

typedef struct cache_content_entry__
{
  cache_content_internal_md_t internal_md;              /**< Metadata for this data cache entry                   */
  cache_content_local_entry_t local_fs_entry;           /**< Handle to the data cached in local fs                */
  cache_entry_t *pentry_inode;                          /**< The related cache inode entry                        */
} cache_content_entry_t;

typedef struct cache_content_stat__
{
  unsigned int nb_gc_lru_active;  /**< Number of active entries in Garbagge collecting list */
  unsigned int nb_gc_lru_total;   /**< Total mumber of entries in Garbagge collecting list  */

  struct func_inode_stats
  {
    unsigned int nb_call[CACHE_CONTENT_NB_COMMAND];             /**< total number of calls per functions     */
    unsigned int nb_success[CACHE_CONTENT_NB_COMMAND];          /**< succesfull calls per function           */
    unsigned int nb_err_retryable[CACHE_CONTENT_NB_COMMAND];    /**< failed/retryable calls per function     */
    unsigned int nb_err_unrecover[CACHE_CONTENT_NB_COMMAND];    /**< failed/unrecoverable calls per function */
  } func_stats;

  unsigned int nb_call_total;                                   /**< Total number of calls */
} cache_content_stat_t;

typedef struct cache_content_client__
{
  struct prealloc_pool content_pool;                /**< Worker's preallocad cache entries pool                   */
  unsigned int nb_prealloc;                         /**< Size of the preallocated pool                            */
  cache_content_stat_t stat;                        /**< File content statistics for this client                  */
  char cache_dir[MAXPATHLEN];                       /**< Path to the directory where data are cached              */
  unsigned int flush_force_fsal;                    /**< Should the flush force the write to FSAL                 */
  unsigned int max_fd_per_thread;                   /**< Max fd open per client                                   */
  time_t retention;                                 /**< Fd retention duration                                    */
  unsigned int use_cache;                           /**< Do we cache fd or not ?                                  */
  int fd_gc_needed;                                 /**< Should we perform fd gc ?                                */
} cache_content_client_t;

typedef enum cache_content_op__
{ CACHE_CONTENT_OP_GET = 1,
  CACHE_CONTENT_OP_SET = 2,
  CACHE_CONTENT_OP_FLUSH = 3
} cache_content_op_t;

typedef struct cache_content_dirinfo__
{
  DIR *level0_dir;
  DIR *level1_dir;
  DIR *level2_dir;

  unsigned int level1_cnt;

  char level0_path[MAXPATHLEN];
  char level1_name[MAXNAMLEN];
  char level2_name[MAXNAMLEN];

  struct dirent *cookie0;
  struct dirent *cookie1;
  struct dirent *cookie2;

  unsigned int level0_opened;
  unsigned int level1_opened;
  unsigned int level2_opened;
} cache_content_dirinfo_t;

#define CACHE_CONTENT_DIR_INITIALIZER   NULL

/*
 * Possible errors 
 */
#define CACHE_CONTENT_SUCCESS               0
#define CACHE_CONTENT_INVALID_ARGUMENT      1
#define CACHE_CONTENT_UNAPPROPRIATED_KEY    2
#define CACHE_CONTENT_BAD_CACHE_INODE_ENTRY 3
#define CACHE_CONTENT_ENTRY_EXISTS          4
#define CACHE_CONTENT_FSAL_ERROR            5
#define CACHE_CONTENT_LOCAL_CACHE_ERROR     6
#define CACHE_CONTENT_MALLOC_ERROR          7
#define CACHE_CONTENT_LRU_ERROR             8
#define CACHE_CONTENT_NOT_FOUND             9
#define CACHE_CONTENT_LOCAL_CACHE_NOT_FOUND 10
#define CACHE_CONTENT_TOO_LARGE_FOR_CACHE   11

typedef enum cache_content_nametype__
{ CACHE_CONTENT_UNASSIGNED = 1,
  CACHE_CONTENT_DATA_FILE = 2,
  CACHE_CONTENT_INDEX_FILE = 3,
  CACHE_CONTENT_DIR = 4
} cache_content_nametype_t;

typedef enum cache_content_create_behaviour__
{ ADD_ENTRY = 1,
  RECOVER_ENTRY = 2,
  RENEW_ENTRY
} cache_content_add_behaviour_t;

typedef enum cache_content_refresh_how__
{ KEEP_LOCAL = 1,
  FORCE_FROM_FSAL = 2,
  DEFAULT_REFRESH
} cache_content_refresh_how_t;

typedef struct cache_content_flush_thread_data__
{
  unsigned int thread_pos;
  unsigned int thread_number;
} cache_content_flush_thread_data_t;

int cache_content_client_init(cache_content_client_t * pclient,
                              cache_content_client_parameter_t param,
                              char *name);

cache_content_status_t cache_content_create_name(char *path,
                                                 cache_content_nametype_t type,
                                                 fsal_op_context_t * pcontext,
                                                 cache_entry_t * pentry_inode,
                                                 cache_content_client_t * pclient);

int cache_content_init(cache_content_client_parameter_t param,
                       cache_content_status_t * pstatus);

int cache_content_init_dir(cache_content_client_parameter_t param,
                           unsigned short export_id);

cache_content_entry_t *cache_content_new_entry(cache_entry_t * pentry_inode,
                                               cache_content_spec_data_t * pspecdata,
                                               cache_content_client_t * pclient,
                                               cache_content_add_behaviour_t how,
                                               fsal_op_context_t * pcontext,
                                               cache_content_status_t * pstatus);

cache_content_status_t cache_content_release_entry(cache_content_entry_t * pentry,
                                                   cache_content_client_t * pclient,
                                                   cache_content_status_t * pstatus);

cache_content_status_t cache_content_open(cache_content_entry_t * pentry,
                                          cache_content_client_t * pclient,
                                          cache_content_status_t * pstatus);

cache_content_status_t cache_content_close(cache_content_entry_t * pentry,
                                           cache_content_client_t * pclient,
                                           cache_content_status_t * pstatus);

cache_content_status_t cache_content_rdwr(cache_content_entry_t * pentry,
                                          cache_content_io_direction_t read_or_write,
                                          fsal_seek_t * seek_descriptor,
                                          fsal_size_t * pio_size_in,
                                          fsal_size_t * pio_size_out,
                                          caddr_t buffer,
                                          fsal_boolean_t * p_fsal_eof,
                                          struct stat *pbuffstat,
                                          cache_content_client_t * pclient,
                                          fsal_op_context_t * pcontext,
                                          cache_content_status_t * pstatus);

#define cache_content_read( a, b, c, d, e, f, g, h, i ) cache_content_rdwr( a, CACHE_CONTENT_READ, b, c, d, e, f, g, h, i )
#define cache_content_write( a, b, c, d, e, f, g, h, i ) cache_content_rdwr( a, CACHE_CONTENT_WRITE, b, c, d, e, f, g, h, i )

cache_content_status_t cache_content_truncate(cache_content_entry_t * pentry,
                                              fsal_size_t length,
                                              cache_content_client_t * pclient,
                                              cache_content_status_t * pstatus);

cache_content_status_t cache_content_flush(cache_content_entry_t * pentry,
                                           cache_content_flush_behaviour_t flushhow,
                                           cache_content_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_content_status_t * pstatus);

cache_content_status_t cache_content_refresh(cache_content_entry_t * pentry,
                                             cache_content_client_t * pclient,
                                             fsal_op_context_t * pcontext,
                                             cache_content_refresh_how_t how,
                                             cache_content_status_t * pstatus);

cache_content_status_t cache_content_sync_all(cache_content_client_t * pclient,
                                              fsal_op_context_t * pcontext,
                                              cache_content_status_t * pstatus);

cache_content_status_t cache_content_read_conf_client_parameter(config_file_t in_config,
                                                                cache_content_client_parameter_t
                                                                * pparam);

void cache_content_print_conf_client_parameter(FILE * output,
                                               cache_content_client_parameter_t param);

off_t cache_content_fsal_seek_convert(fsal_seek_t seek, cache_content_status_t * pstatus);
size_t cache_content_fsal_size_convert(fsal_size_t size,
                                       cache_content_status_t * pstatus);

cache_content_status_t cache_content_crash_recover(unsigned short exportid,
                                                   unsigned int index,
                                                   unsigned int mod,
                                                   cache_content_client_t * pclient_data,
                                                   cache_inode_client_t * pclient_inode,
                                                   hash_table_t * ht,
                                                   fsal_op_context_t * pcontext,
                                                   cache_content_status_t * pstatus);

int cache_content_get_export_id(char *dirname);
u_int64_t cache_content_get_inum(char *filename);
int cache_content_get_datapath(char *basepath, u_int64_t inum, char *datapath);
off_t cache_content_recover_size(char *basepath, u_int64_t inum);

cache_inode_status_t cache_content_error_convert(cache_content_status_t status);

cache_content_status_t cache_content_valid(cache_content_entry_t * pentry,
                                           cache_content_op_t op,
                                           cache_content_client_t * pclient);

cache_content_status_t cache_content_read_conf_gc_policy(config_file_t in_config,
                                                         cache_content_gc_policy_t *
                                                         ppolicy);

void cache_content_print_conf_gc_policy(FILE * output,
                                        cache_content_gc_policy_t gcpolicy);

void cache_content_set_gc_policy(cache_content_gc_policy_t policy);

cache_content_gc_policy_t cache_content_get_gc_policy(void);

cache_content_status_t cache_content_gc(cache_content_client_t * pclient,
                                        cache_content_status_t * pstatus);

cache_content_status_t cache_content_emergency_flush(char *cachedir,
                                                     cache_content_flush_behaviour_t
                                                     flushhow,
                                                     unsigned int lw_mark_trigger_flag,
                                                     time_t grace_period,
                                                     unsigned int index, unsigned int mod,
                                                     unsigned int *p_nb_flushed,
                                                     unsigned int *p_nb_too_young,
                                                     unsigned int *p_nb_errors,
                                                     unsigned int *p_nb_orphans,
                                                     fsal_op_context_t * pcontext,
                                                     cache_content_status_t * pstatus);

cache_content_status_t cache_content_check_threshold(char *datacache_path,
                                                     unsigned int threshold_min,
                                                     unsigned int threshold_max,
                                                     int *p_bool_overflow,
                                                     unsigned long *p_blocks_to_lwm);

int cache_content_local_cache_opendir(char *cache_dir,
                                      cache_content_dirinfo_t * pdirectory);

int cache_content_local_cache_dir_iter(cache_content_dirinfo_t * directory,
                                       struct dirent *dir_entry,
                                       unsigned int index, unsigned int mod);

void cache_content_local_cache_closedir(cache_content_dirinfo_t * directory);

int cache_content_invalidate_flushed(LRU_entry_t * plru_entry, void *addparam);
cache_content_status_t cache_content_test_cached(cache_entry_t * pentry_inode,
                                                 cache_content_client_t * pclient,
                                                 fsal_op_context_t * pcontext,
                                                 cache_content_status_t * pstatus);
off_t cache_content_get_cached_size(cache_content_entry_t * pentry);

#endif                          /* _CACHE_CONTENT_H */
