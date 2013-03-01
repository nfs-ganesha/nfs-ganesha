// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2010, 2011
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    pt_ganesha.h
// Description: Define common macros and data types used by PT FSAL layer
// Author:      FSI IPC team
// ----------------------------------------------------------------------------

#ifndef __PT_GANESHA_H__
#define __PT_GANESHA_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsi_ipc_ccl.h"

// Linux includes
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/param.h>

/* VFS operations structure */
struct vfs_fn_pointers {
  int (*init_fn)(int                        multi_threaded,
                 log_function_t             log_fn,
                 log_level_check_function_t log_level_check_fn,
                 int                        ipc_ccl_to_component_trc_level_map
                 [FSI_NUM_TRACE_LEVELS]);
  int (*check_handle_index_fn) (int handle_index);
  int (*find_handle_by_name_and_export_fn)(const char * filename,
                                           ccl_context_t * handle);
  int (*stat_fn)(ccl_context_t * handle,
                 const char        * filename,
                 fsi_stat_struct       * sbuf);
  int (*fstat_fn)(int                 handle_index,
                  fsi_stat_struct   * sbuf);
  int (*stat_by_handle_fn)(ccl_context_t           * context,
                           struct PersistentHandle * handle,
                           fsi_stat_struct         * sbuf);
  int (*rcv_msg_nowait_fn)(int     msg_id,
                           void  * p_msg_buf,
                           size_t  msg_size,
                           long    msg_type);
  int (*rcv_msg_wait_fn)(int     msg_id,
                         void  * p_msg_buf,
                         size_t  msg_size,
                         long    msg_type);
  int (*rcv_msg_wait_block_fn)(int     msg_id,
                               void  * p_msg_buf,
                               size_t  msg_size,
                               long    msg_type);
  int (*send_msg_fn)(int          msg_id,
                     const void * p_msg_buf,
                     size_t       msg_size);
  int (*chmod_fn)(ccl_context_t * handle,
                  const char        * path,
                  mode_t              mode);
  int (*chown_fn)(ccl_context_t * handle,
                  const char        * path,
                  uid_t               uid,
                  gid_t               gid);
  int (*ntimes_fn)(ccl_context_t * handle,
                   const char        * filename,
                   uint64_t            atime,
                   uint64_t            mtime);
  int (*mkdir_fn)(ccl_context_t  * handle,
                  const char        * path,
                  mode_t              mode);
  int (*rmdir_fn)(ccl_context_t  * handle,
                  const char        * path);
  int (*get_real_filename_fn)(ccl_context_t * handle,
                              const char    * path,
                              const char    * name,
                              char          * found_name,
                              const size_t    found_name_max_size);
  uint64_t (*disk_free_fn)(ccl_context_t * handle,
                           const char    * path,
                           uint64_t      * bsize,
                           uint64_t      * dfree,
                           uint64_t      * dsize);
  int (*unlink_fn)(ccl_context_t  * handle,
                   char               * path);
  int (*rename_fn)(ccl_context_t * handle,
                   const char        * old_name,
                   const char        * new_name);
  int (*opendir_fn)(ccl_context_t * handle,
                    const char        * filename,
                    const char        * mask,
                    uint32              attr);
  int (*closedir_fn)(ccl_context_t * handle,
                     struct fsi_struct_dir_t * dirp);
  int (*readdir_fn)(ccl_context_t * handle,
                    struct fsi_struct_dir_t * dirp,
                    fsi_stat_struct   * sbuf);
  void (*seekdir_fn)(ccl_context_t * handle,
                     struct fsi_struct_dir_t * dirp,
                     long                offset);
  long (*telldir_fn)(ccl_context_t * handle,
                     struct fsi_struct_dir_t * dirp);
  int (*chdir_fn)(ccl_context_t * handle,
                  const char    * path);
  int (*fsync_fn)(ccl_context_t * handle,
                  int handle_index);
  int (*ftruncate_fn)(ccl_context_t * handle,
                      int handle_index,
                      uint64_t           offset);
  ssize_t (*pread_fn)(ccl_context_t * handle,
                      void              * data,
                      size_t              n,
                      uint64_t            offset,
                      uint64_t            max_readahead_offset);
  ssize_t (*pwrite_fn)(ccl_context_t         * handle,
                       int                    handle_index,
                       const void           * data,
                       size_t                 n,
                       uint64_t               offset);
  int (*open_fn)(ccl_context_t   * handle,
                 char                  * path,
                 int                   flags,
                 mode_t                mode);
  int (*close_fn)(ccl_context_t * handle,
                  int             handle_index,
                  int             close_style);
  int (*get_any_io_responses_fn)(int     handle_index,
                                 int   * p_combined_rc,
                                 struct msg_t* p_msg);
  void (*ipc_stats_logger_fn)(ccl_context_t * handle);
  uint64_t (*update_stats_fn)(struct ipc_client_stats_t * stat, uint64_t value_fn);
  int (*sys_acl_get_entry_fn)(ccl_context_t * handle,
                              acl_t           theacl,
                              int             entry_id,
                              acl_entry_t   * entry_p);
  int (*sys_acl_get_tag_type_fn)(ccl_context_t * handle,
                                 acl_entry_t     entry_d,
                                 acl_tag_t     * tag_type_p);
  int (*sys_acl_get_permset_fn)(ccl_context_t * handle,
                                acl_entry_t     entry_d,
                                acl_permset_t * permset_p);
  void * (*sys_acl_get_qualifier_fn)(ccl_context_t * handle,
                                     acl_entry_t     entry_d);
  acl_t (*sys_acl_get_file_fn)(ccl_context_t * handle,
                               const char    * path_p,
                               acl_type_t      type);
  int (*sys_acl_clear_perms_fn)(ccl_context_t * handle,
                                acl_permset_t   permset);
  int (*sys_acl_add_perm_fn)(ccl_context_t * handle,
                             acl_permset_t   permset,
                             acl_perm_t      perm);
  acl_t (*sys_acl_init_fn)(ccl_context_t * handle,
                           int             count);
  int (*sys_acl_create_entry_fn)(ccl_context_t * handle,
                                 acl_t         * pacl,
                                 acl_entry_t   * pentry);
  int (*sys_acl_set_tag_type_fn)(ccl_context_t * handle,
                                 acl_entry_t     entry,
                                 acl_tag_t       tagtype);
  int (*sys_acl_set_qualifier_fn)(ccl_context_t * handle,
                                  acl_entry_t     entry,
                                  void              * qual);
  int (*sys_acl_set_permset_fn)(ccl_context_t * handle,
                                acl_entry_t     entry,
                                acl_permset_t   permset);
  int (*sys_acl_set_file_fn)(ccl_context_t * handle,
                             const char    * name,
                             acl_type_t      acltype,
                             acl_t           theacl);
  int (*sys_acl_delete_def_file_fn)(ccl_context_t * handle,
                                    const char    * path);
  int (*sys_acl_get_perm_fn)(ccl_context_t * handle,
                             acl_permset_t   permset,
                             acl_perm_t      perm);
  int (*sys_acl_free_acl_fn)(ccl_context_t * handle,
                             acl_t           posix_acl);
  int (*name_to_handle_fn)(ccl_context_t           * pvfs_handle,
                           char                    * path,
                           struct PersistentHandle * phandle);
  int (*handle_to_name_fn)(ccl_context_t           * pvfs_handle,
                           struct PersistentHandle * phandle,
                           char                    * path);
  int (*dynamic_fsinfo_fn)(ccl_context_t                      * pvfs_handle,
                           char                               * path,
                           struct ClientOpDynamicFsInfoRspMsg * pfs_info);
  int (*readlink_fn)(ccl_context_t * pvfs_handle,
                     const char    * path,
                     char          * link_content);
  int (*symlink_fn)(ccl_context_t * pvfs_handle,
                    const char    * path,
                    const char    * link_content);
  int (*update_handle_nfs_state_fn)(int              handle_index,
                                    enum e_nfs_state state,
                                    int              expected_state);
  int (*fsal_try_stat_by_index_fn)(ccl_context_t           * handle,
                                   int                       handle_index,
                                   char                    * fsal_name,
                                   fsi_stat_struct         * sbuf);
  int (*fsal_try_fastopen_by_index_fn)(ccl_context_t       * handle,
                                       int                   handle_index,
                                       char                * fsal_name);
  int (*find_oldest_handle_fn)();
  bool (*can_close_handle_fn)(int handle_index,
                              int timeout);
  int (*up_mutex_lock_fn)(pthread_mutex_t *mutex);
  int (*up_mutex_unlock_fn)(pthread_mutex_t *mutex);
  void (*log_fn)(const fsi_ipc_trace_level level,
                 const char              * func,
                 const char              * format,
                 ...);
  int (*implicit_close_for_nfs_fn)(int handle_index_to_close, 
                                   int close_style);
  int (*update_cache_stat_fn)(const char * filename,
                              uint64_t     newMode,
                              uint64_t     export_id); 
  char* (*get_version_fn)();
  int (*check_version_fn)(char *version);
};

#define CCL_INIT                           g_ccl_function_map.init_fn
#define CCL_CHECK_HANDLE_INDEX             g_ccl_function_map.check_handle_index_fn
#define CCL_FIND_HANDLE_BY_NAME_AND_EXPORT g_ccl_function_map.find_handle_by_name_and_export_fn
#define CCL_STAT                           g_ccl_function_map.stat_fn
#define CCL_FSTAT                          g_ccl_function_map.fstat_fn
#define CCL_STAT_BY_HANDLE                 g_ccl_function_map.stat_by_handle_fn
#define RCV_MSG_NOWAIT                     g_ccl_function_map.rcv_msg_nowait_fn
#define RCV_MSG_WAIT                       g_ccl_function_map.rcv_msg_wait_fn
#define RCV_MSG_WAIT_BLOCK                 g_ccl_function_map.rcv_msg_wait_block_fn
#define SEND_MSG                           g_ccl_function_map.send_msg_fn
#define CCL_CHMOD                          g_ccl_function_map.chmod_fn
#define CCL_CHOWN                          g_ccl_function_map.chown_fn
#define CCL_NTIMES                         g_ccl_function_map.ntimes_fn
#define CCL_MKDIR                          g_ccl_function_map.mkdir_fn
#define CCL_RMDIR                          g_ccl_function_map.rmdir_fn
#define CCL_GET_REAL_FILENAME              g_ccl_function_map.get_real_filename_fn
#define CCL_DISK_FREE                      g_ccl_function_map.disk_free_fn
#define CCL_UNLINK                         g_ccl_function_map.unlink_fn
#define CCL_RENAME                         g_ccl_function_map.rename_fn
#define CCL_OPENDIR                        g_ccl_function_map.opendir_fn
#define CCL_CLOSEDIR                       g_ccl_function_map.closedir_fn
#define CCL_READDIR                        g_ccl_function_map.readdir_fn
#define CCL_SEEKDIR                        g_ccl_function_map.seekdir_fn
#define CCL_TELLDIR                        g_ccl_function_map.telldir_fn
#define CCL_CHDIR                          g_ccl_function_map.chdir_fn
#define CCL_FSYNC                          g_ccl_function_map.fsync_fn
#define CCL_FTRUNCATE                      g_ccl_function_map.ftruncate_fn
#define CCL_PREAD                          g_ccl_function_map.pread_fn
#define CCL_PWRITE                         g_ccl_function_map.pwrite_fn
#define CCL_OPEN                           g_ccl_function_map.open_fn
#define CCL_CLOSE                          g_ccl_function_map.close_fn
#define GET_ANY_IO_RESPONSES               g_ccl_function_map.get_any_io_responses_fn
#define CCL_IPC_STATS_LOGGER               g_ccl_function_map.ipc_stats_logger_fn
#define UPDATE_STATS                       g_ccl_function_map.update_stats_fn
#define CCL_SYS_ACL_GET_ENTRY              g_ccl_function_map.sys_acl_get_entry_fn
#define CCL_SYS_ACL_GET_TAG_TYPE           g_ccl_function_map.sys_acl_get_tag_type_fn
#define CCL_SYS_ACL_GET_PERMSET            g_ccl_function_map.sys_acl_get_permset_fn
#define CCL_SYS_ACL_GET_QUALIFIER          g_ccl_function_map.sys_acl_get_qualifier_fn
#define CCL_SYS_ACL_GET_FILE               g_ccl_function_map.sys_acl_get_file_fn
#define CCL_SYS_ACL_CLEAR_PERMS            g_ccl_function_map.sys_acl_clear_perms_fn
#define CCL_SYS_ACL_ADD_PERM               g_ccl_function_map.sys_acl_add_perm_fn
#define CCL_SYS_ACL_INIT                   g_ccl_function_map.sys_acl_init_fn
#define CCL_SYS_ACL_CREATE_ENTRY           g_ccl_function_map.sys_acl_create_entry_fn
#define CCL_SYS_ACL_SET_TAG_TYPE           g_ccl_function_map.sys_acl_set_tag_type_fn
#define CCL_SYS_ACL_SET_QUALIFIER          g_ccl_function_map.sys_acl_set_qualifier_fn
#define CCL_SYS_ACL_SET_PERMSET            g_ccl_function_map.sys_acl_set_permset_fn
#define CCL_SYS_ACL_SET_FILE               g_ccl_function_map.sys_acl_set_file_fn
#define CCL_SYS_ACL_DELETE_DEF_FILE        g_ccl_function_map.sys_acl_delete_def_file_fn
#define CCL_SYS_ACL_GET_PERM               g_ccl_function_map.sys_acl_get_perm_fn
#define CCL_SYS_ACL_FREE_ACL               g_ccl_function_map.sys_acl_free_acl_fn
#define CCL_NAME_TO_HANDLE                 g_ccl_function_map.name_to_handle_fn
#define CCL_HANDLE_TO_NAME                 g_ccl_function_map.handle_to_name_fn
#define CCL_DYNAMIC_FSINFO                 g_ccl_function_map.dynamic_fsinfo_fn
#define CCL_READLINK                       g_ccl_function_map.readlink_fn
#define CCL_SYMLINK                        g_ccl_function_map.symlink_fn
#define CCL_UPDATE_HANDLE_NFS_STATE        g_ccl_function_map.update_handle_nfs_state_fn
#define CCL_FSAL_TRY_STAT_BY_INDEX         g_ccl_function_map.fsal_try_stat_by_index_fn
#define CCL_FSAL_TRY_FASTOPEN_BY_INDEX     g_ccl_function_map.fsal_try_fastopen_by_index_fn
#define CCL_FIND_OLDEST_HANDLE             g_ccl_function_map.find_oldest_handle_fn
#define CCL_CAN_CLOSE_HANDLE               g_ccl_function_map.can_close_handle_fn
#define CCL_UP_MUTEX_LOCK                  g_ccl_function_map.up_mutex_lock_fn
#define CCL_UP_MUTEX_UNLOCK                g_ccl_function_map.up_mutex_unlock_fn
#define CCL_UP_SELF                        g_ccl_function_map.up_self_fn
#define CCL_LOG                            g_ccl_function_map.log_fn
#define CCL_IMPLICIT_CLOSE_FOR_NFS         g_ccl_function_map.implicit_close_for_nfs_fn  
#define CCL_UPDATE_CACHE_STAT              g_ccl_function_map.update_cache_stat_fn	
#define CCL_GET_VERSION                    g_ccl_function_map.get_version_fn
#define CCL_CHECK_VERSION                  g_ccl_function_map.check_version_fn

// function map to be used througout FSAL layer
extern char   * g_shm_at_fsal;              // SHM Base Address
extern struct vfs_fn_pointers g_ccl_function_map;
extern void * g_ccl_lib_handle;
extern struct file_handles_struct_t * g_fsal_fsi_handles;

// FSAL handle structure analogs to CCL variables and structures
extern struct file_handles_struct_t * g_fsi_handles_fsal;  // FSI client
                                                           // handles
extern struct dir_handles_struct_t  * g_fsi_dir_handles_fsal; // FSI client Dir
                                                              // handles
extern struct acl_handles_struct_t  * g_fsi_acl_handles_fsal; // FSI client ACL
                                                              // handles

#define fsi_dirent                 dirent
#define FSI_MAX_HANDLE_CACHE_ENTRY 2500
#define WRITE_IO_BUFFER_SIZE              262144 // 256k
#define READ_IO_BUFFER_SIZE              1048576 // 1M
#define PTFSAL_USE_READSIZE_THRESHOLD     524288 // 512K.
#define PTFSAL_POLLING_THREAD_FREQUENCY_SEC 1    // how often polling thread
                                                 // is called
#define PTFSAL_POLLING_HANDLE_TIMEOUT_SEC 10   // Time interval between
                                               // polling for handle to
                                               // close in the
                                               // background
extern int             debug_flag;
extern struct          fsi_handle_cache_t  g_fsi_name_handle_cache;
extern pthread_rwlock_t g_fsi_cache_handle_rw_lock;
extern int             polling_thread_handle_timeout_sec;

void fsi_get_whole_path(const char * parentPath,
                        const char * name,
                        char       * path);
int  fsi_cache_name_and_handle(fsal_op_context_t * p_context,
                               char              * handle,
                               char              * name);
int  fsi_get_name_from_handle(fsal_op_context_t * p_context,
                              char              * handle,
                              char              * name,
                              int               * handle_index);
int  fsi_update_cache_name(char * oldname,
                           char * newname);
int  fsi_update_cache_stat(const char * p_filename,
                           uint64_t     newMode,
                           uint64_t     export_id);
void fsi_remove_cache_by_fullpath(char * path);

void fsi_remove_cache_by_handle(char * handle);

struct fsi_handle_cache_entry_t {
  char m_handle[FSI_PERSISTENT_HANDLE_N_BYTES];
  char m_name[PATH_MAX];
};

struct fsi_handle_cache_t {
  /* we need to set a constant to manage this and add LRU cleanup */
  struct fsi_handle_cache_entry_t m_entry[FSI_MAX_HANDLE_CACHE_ENTRY];
  int m_count;
};

int ptfsal_stat_by_handle(fsal_handle_t     * p_filehandle,
                          fsal_op_context_t * p_context,
                          fsi_stat_struct   * p_stat);

int ptfsal_stat_by_name(fsal_op_context_t * p_context,
                        fsal_path_t       * p_fsalpath,
                        fsi_stat_struct   * p_stat);

int ptfsal_stat_by_parent_name(fsal_op_context_t * p_context,
                               fsal_handle_t     * p_parentdir_handle,
                               char              * p_filename,
                               fsi_stat_struct   * p_stat);

int ptfsal_opendir(fsal_op_context_t * p_context,
                   const char        * filename,
                   const char        * mask,
                   uint32              attr);

int ptfsal_readdir(fsal_dir_t      * dir_desc,
                   fsi_stat_struct * sbuf,
                   char            * fsi_dname);

int ptfsal_closedir(fsal_dir_t * dir_desc);

int ptfsal_fsync (fsal_file_t * p_file_desciptor);

int ptfsal_open_by_handle(fsal_op_context_t * p_context,
                          fsal_handle_t     * p_object_handle,
                          int oflags, mode_t  mode);

int ptfsal_open(fsal_handle_t     * p_parent_directory_handle,
                fsal_name_t       * p_filename,
                fsal_op_context_t * p_context,
                mode_t              mode,
                fsal_handle_t     * p_object_handle);

int ptfsal_close_mount_root(fsal_export_context_t * p_export_context);

int ptfsal_ftruncate(fsal_op_context_t * p_context,
                     int                 handle_index,
                     uint64_t            offset);

int ptfsal_unlink(fsal_op_context_t * p_context,
                  fsal_handle_t     * p_parent_directory_handle,
                  char              * p_filename);

int ptfsal_rename(fsal_op_context_t * p_context,
                  fsal_handle_t     * p_old_parentdir_handle,
                  char              * p_old_name,
                  fsal_handle_t     * p_new_parentdir_handle,
                  char              * p_new_name);

int fsi_check_handle_index(int handle_index);

int ptfsal_chown(fsal_op_context_t * p_context,
                 const char        * path,
                 uid_t               uid,
                 gid_t               gid);

int ptfsal_chmod(fsal_op_context_t * p_context,
                 const char        * path,
                 mode_t              mode);

int ptfsal_ntimes(fsal_op_context_t * p_context,
                  const char        * filename,
                  uint64_t            atime,
                  uint64_t            mtime);

int ptfsal_mkdir(fsal_handle_t     * p_parent_directory_handle,
                 fsal_name_t       * p_dirname,
                 fsal_op_context_t * p_context,
                 mode_t              mode,
                 fsal_handle_t     * p_object_handle);

int ptfsal_rmdir(fsal_op_context_t * p_context,
                 fsal_handle_t     * p_parent_directory_handle,
                 char              * p_object_name);

int ptfsal_dynamic_fsinfo(fsal_handle_t        * p_filehandle,
                          fsal_op_context_t    * p_context,
                          fsal_dynamicfsinfo_t * p_dynamicinfo);

int ptfsal_readlink(fsal_handle_t     * p_linkhandle,
                    fsal_op_context_t * p_context,
                    char              * p_buf);

int ptfsal_symlink(fsal_handle_t     * p_parent_directory_handle,
                   fsal_name_t       * p_linkname,
                   fsal_path_t       * p_linkcontent,
                   fsal_op_context_t * p_context,
                   fsal_accessmode_t   accessmode,
                   fsal_handle_t     * p_link_handle);

int ptfsal_SetDefault_FS_specific_parameter(
    /* param TBD fsal_parameter_t * out_parameter */);

int ptfsal_name_to_handle(fsal_op_context_t * p_context,
                          fsal_path_t       * p_fsalpath,
                          fsal_handle_t     * p_handle);

int ptfsal_handle_to_name(fsal_handle_t     * p_filehandle,
                          fsal_op_context_t * p_context,
                          char              * path);

uint64_t ptfsal_read(fsal_file_t * p_file_descriptor,
                     char        * buf,
                     size_t        size,
                     off_t         offset,
                     int           in_handle);

uint64_t ptfsal_write(fsal_file_t * p_file_descriptor,
                      const char  * buf,
                      size_t        size,
                      off_t         offset,
                      int           in_handle);

void ptfsal_print_handle(char * handle);

mode_t fsal_type2unix(int fsal_type);

void ptfsal_set_fsi_handle_data(fsal_op_context_t * p_context,
                                ccl_context_t     * context);
void *ptfsal_closeHandle_listener_thread(void *args);

int ptfsal_implicit_close_for_nfs(int handle_index_to_close, int close_style);

void *ptfsal_polling_closeHandler_thread(void *args);

#define CACHE_MAX_NUM_CACHE_ENTRY(_CACHE_TABLE) (sizeof(_CACHE_TABLE)/sizeof(CACHE_DATA_TYPE_T))

// An enum representing what the purpose of this cache table is
// This is mostly for facilitating debugging in the log
typedef enum {
  CACHE_ID_192_FRONT_END_HANDLE_TO_NAME_CACHE = 1,
  CACHE_ID_2500_BACK_END_HANDLE_TO_NAME_CACHE = 2
} CACHE_ID_ENUMS;

typedef struct {
  int            keyLengthInBytes;      // Length (in bytes) of the key
  int            dataSizeInBytes;       // Data size (in bytes)
  int            maxNumOfCacheEntries;  // How big the cache table should be at max
  CACHE_ID_ENUMS cacheTableID;          // This is used to identify which cache this is.  Defined
                                        // in CACHE_ID_* enum

  // ----------------------------------------------------------------------------
  // Compare two key entry and indicates the order of those two entries
  // This is intended for the use of binary search and binary insertion routine
  // used in this cache utilities
  //
  // Return:  1 if string1 >  string2
  //          0 if string1 == string2
  //         -1 if string1 <  string2
  int    (*cacheKeyComprefn)(const void *cacheEntry1, const void *cacheEntry2);
} CACHE_TABLE_INIT_PARAM;

typedef struct {
  // NOTE: cache entries are not pre-declared array.  They only contains pointers
  //       to memory location where the real information is stored for the cached
  //       entries.
  void *key;  // Pointer to cached Key
  void *data; // Pointer Cached data
} CACHE_TABLE_ENTRY_T;

typedef struct {
  int numElementsOccupied;      // How many elements currently in the cache table
  int maxNumOfCacheEntries;     // How big this cache table can hold at max
  int keyLengthInBytes;         // Length of the key in the cache entry
  int dataSizeInBytes;          // Length of the data in the cache entry
  CACHE_ID_ENUMS cacheTableID;  // This is used to identify which cache this is.
                                // Defined in CACHE_ID_* enum
  // Function pointer to the comparison function
  int (*cacheKeyComprefn)(const void *key1, const void *key2);
} CACHE_TABLE_META_DATA_T;

typedef struct {
  CACHE_TABLE_META_DATA_T  cacheMetaData;
  CACHE_TABLE_ENTRY_T     *cacheEntries;
} CACHE_TABLE_T;

typedef struct {
  char m_name[PATH_MAX];  // This is for fsi_get_name_from_handle() lookup
                 // This should have the name length of PATH_MAX
  int  handle_index;      // We record handle index if there is one for this name
} CACHE_ENTRY_DATA_HANDLE_TO_NAME_T;
int fsi_cache_handle2name_keyCompare(const void *cacheEntry1, const void *cacheEntry2);

int fsi_cache_table_init(CACHE_TABLE_T *cacheTableToInit,
                         CACHE_TABLE_INIT_PARAM *cacheTableInitParam);

int fsi_cache_getInsertionPoint(CACHE_TABLE_T         *cacheTable,
                                CACHE_TABLE_ENTRY_T   *whatToInsert,
                                int                   *whereToInsert);
int fsi_cache_insertEntry(CACHE_TABLE_T *cacheTable, CACHE_TABLE_ENTRY_T *whatToInsert);
int fsi_cache_deleteEntry(CACHE_TABLE_T *cacheTable, CACHE_TABLE_ENTRY_T *whatToDelete);
int fsi_cache_getEntry(CACHE_TABLE_T *cacheTable, CACHE_TABLE_ENTRY_T *buffer);
void fsi_cache_handle2name_dumpTableKeys(fsi_ipc_trace_level logLevel,
                                         CACHE_TABLE_T *cacheTable,
                                         char *titleString);
void fsi_cache_32Bytes_rawDump(fsi_ipc_trace_level loglevel, void *data, int index);
extern CACHE_TABLE_T g_fsi_name_handle_cache_opened_files;
#endif // ifndef __PT_GANESHA_H__

