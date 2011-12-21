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
 * \file    nfs_core.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/22 12:02:39 $
 * \version $Revision: 1.43 $
 * \brief   Prototypes for the different threads in the nfs core.
 *
 * nfs_core.h : Prototypes for the different threads in the nfs core.
 *
 */

#ifndef _NFS_CORE_H
#define _NFS_CORE_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <sys/time.h>

#include "rpc.h"
#include "LRU_List.h"
#include "fsal.h"
#ifdef _USE_MFSL
#include "mfsl.h"
#endif
#include "cache_inode.h"
#include "sal_data.h"
#include "cache_content.h"
#include "nfs_stat.h"
#include "external_tools.h"

#include "stuff_alloc.h"

#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "err_LRU_List.h"
#include "err_HashTable.h"

#include "cache_inode.h"
#include "fsal_up.h"

#ifdef _USE_NFS4_1
#include "nfs41_session.h"
#ifdef _USE_PNFS
#include "pnfs.h"
#endif                          /* _USE_PNFS */
#endif

#ifdef _USE_9P
#include "9p.h"
#endif

#ifdef _ERROR_INJECTION
#include "err_inject.h"
#endif

#ifndef NFS4_MAX_DOMAIN_LEN
#define NFS4_MAX_DOMAIN_LEN 512
#endif

/* Maximum thread count */
#define NB_MAX_WORKER_THREAD 4096
#define NB_MAX_FLUSHER_THREAD 100

/* NFS daemon behavior default values */
#define NB_WORKER_THREAD_DEFAULT  16
#define NB_FLUSHER_THREAD_DEFAULT 16
#define NB_REQUEST_BEFORE_QUEUE_AVG  1000
#define NB_MAX_CONCURRENT_GC 3
#define NB_MAX_PENDING_REQUEST 30
#define NB_PREALLOC_LRU_WORKER 100
#define NB_REQUEST_BEFORE_GC 50
#define PRIME_DUPREQ 17         /* has to be a prime number */
#define PRIME_ID_MAPPER 17      /* has to be a prime number */
#define DUPREQ_EXPIRATION 180
#define NB_PREALLOC_HASH_DUPREQ 100
#define NB_PREALLOC_LRU_DUPREQ 100
#define NB_PREALLOC_GC_DUPREQ 100
#define NB_PREALLOC_ID_MAPPER 200

#define PRIME_CACHE_INODE 29    /* has to be a prime number */
#define NB_PREALLOC_HASH_CACHE_INODE 1000
#define NB_PREALLOC_LRU_CACHE_INODE 1000

#define PRIME_IP_NAME            17
#define NB_PREALLOC_HASH_IP_NAME 10
#define IP_NAME_EXPIRATION       36000

#define PRIME_IP_STATS            17
#define NB_PREALLOC_HASH_IP_STATS 10

#define PRIME_CLIENT_ID            17
#define NB_PREALLOC_HASH_CLIENT_ID 10

#define PRIME_STATE_ID            17
#define NB_PREALLOC_HASH_STATE_ID 10

#define DEFAULT_NFS_PRINCIPAL     "nfs" /* GSSAPI will expand this to nfs/host@DOMAIN */
#define DEFAULT_NFS_KEYTAB        ""    /* let GSSAPI use keytab specified in /etc/krb5.conf */

/* Config labels */
#define CONF_LABEL_NFS_CORE         "NFS_Core_Param"
#define CONF_LABEL_NFS_WORKER       "NFS_Worker_Param"
#define CONF_LABEL_NFS_DUPREQ       "NFS_DupReq_Hash"
#define CONF_LABEL_NFS_IP_NAME      "NFS_IP_Name"
#define CONF_LABEL_NFS_KRB5         "NFS_KRB5"
#define CONF_LABEL_PNFS             "pNFS"
#define CONF_LABEL_NFS_VERSION4     "NFSv4"
#define CONF_LABEL_CLIENT_ID        "NFSv4_ClientId_Cache"
#define CONF_LABEL_STATE_ID         "NFSv4_StateId_Cache"
#define CONF_LABEL_SESSION_ID       "NFSv4_Session_Cache"
#define CONF_LABEL_UID_MAPPER       "UidMapper_Cache"
#define CONF_LABEL_GID_MAPPER       "GidMapper_Cache"
#define CONF_LABEL_UID_MAPPER_TABLE "Users"
#define CONF_LABEL_GID_MAPPER_TABLE "Groups"
#define CONF_LABEL_IP_NAME_HOSTS    "Hosts"
#define CONF_LABEL_NFSV4_REFERRALS  "NFSv4_Referrals"

/* Worker and sidpatcher stack size */
#define THREAD_STACK_SIZE  2116488

/* NFS/RPC specific values */
#define NFS_PORT             2049
#define RQUOTA_PORT           875
#define	RQCRED_SIZE	     400        /* this size is excessive */
#define NFS_DEFAULT_SEND_BUFFER_SIZE 32768
#define NFS_DEFAULT_RECV_BUFFER_SIZE 32768

/* Default 'Raw Dev' values */
#define GANESHA_RAW_DEV_MAJOR 168
#define GANESHA_RAW_DEV_MINOR 168

/* Other #define */
#define TMP_STR_LEN 256
#define AUTH_STR_LEN 30
#define  PWENT_MAX_LEN 81       /* MUST be a multiple of 9 */

/* IP/name cache error */
#define CLIENT_ID_SUCCESS             0
#define CLIENT_ID_INSERT_MALLOC_ERROR 1
#define CLIENT_ID_NOT_FOUND           2
#define CLIENT_ID_INVALID_ARGUMENT    3

/* Id Mapper cache error */
#define ID_MAPPER_SUCCESS             0
#define ID_MAPPER_INSERT_MALLOC_ERROR 1
#define ID_MAPPER_NOT_FOUND           2
#define ID_MAPPER_INVALID_ARGUMENT    3
#define ID_MAPPER_FAIL                4

/* Hard and soft limit for nfsv4 quotas */
#define NFS_V4_MAX_QUOTA_SOFT 4294967296LL      /*  4 GB */
#define NFS_V4_MAX_QUOTA_HARD 17179869184LL     /* 16 GB */
#define NFS_V4_MAX_QUOTA      34359738368LL     /* 32 GB */

/* protocol flags */
#define CORE_OPTION_NFSV2           0x00000002        /* NFSv2 operations are supported      */
#define CORE_OPTION_NFSV3           0x00000004        /* NFSv3 operations are supported      */
#define CORE_OPTION_NFSV4           0x00000008        /* NFSv4 operations are supported      */
#define CORE_OPTION_ALL_VERS        0x0000000E

/* Things related to xattr ghost directory */
#define XATTRD_NAME ".xattr.d."
#define XATTRD_NAME_LEN 9       /* MUST be equal to strlen( XATTRD_NAME ) */
#define XATTR_BUFFERSIZE 4096

/* Flags for how the stable flag should be interpretted */
#define FSAL_UNSAFE_WRITE_TO_FS_BUFFER 0
#define FSAL_SAFE_WRITE_TO_FS 1
#define FSAL_UNSAFE_WRITE_TO_GANESHA_BUFFER 2 

typedef enum nfs_clientid_confirm_state__
{ CONFIRMED_CLIENT_ID = 1,
  UNCONFIRMED_CLIENT_ID = 2,
  REBOOTED_CLIENT_ID = 3,
  CB_RECONFIGURED_CLIENT_ID = 4
} nfs_clientid_confirm_state_t;

typedef char path_str_t[MAXPATHLEN] ;

#define CLIENT_ID_MAX_LEN             72        /* MUST be a multiple of 9 */

#ifndef P
#define P( sem ) pthread_mutex_lock( &sem )
#endif

#ifndef V
#define V( sem ) pthread_mutex_unlock( &sem )
#endif

/* The default attribute mask for NFSv2/NFSv3 */
#define FSAL_ATTR_MASK_V2_V3   ( FSAL_ATTRS_MANDATORY | FSAL_ATTR_MODE     | FSAL_ATTR_FILEID | \
                                 FSAL_ATTR_FSID       | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER  | \
                                 FSAL_ATTR_GROUP      | FSAL_ATTR_SIZE     | FSAL_ATTR_ATIME  | \
                                 FSAL_ATTR_MTIME      | FSAL_ATTR_CTIME    | FSAL_ATTR_SPACEUSED | \
                                 FSAL_ATTR_RAWDEV )

/* The default attribute mask for NFSv4 */
#define FSAL_ATTR_MASK_V4   ( FSAL_ATTRS_MANDATORY | FSAL_ATTR_MODE     | FSAL_ATTR_FILEID | \
                                 FSAL_ATTR_FSID       | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER  | \
                                 FSAL_ATTR_GROUP      | FSAL_ATTR_SIZE     | FSAL_ATTR_ATIME  | \
                                 FSAL_ATTR_MTIME      | FSAL_ATTR_CTIME    | FSAL_ATTR_SPACEUSED | \
                                 FSAL_ATTR_RAWDEV | FSAL_ATTR_ACL )

typedef struct nfs_worker_param__
{
  LRU_parameter_t lru_param;
  LRU_parameter_t lru_dupreq;
  unsigned int nb_pending_prealloc;
  unsigned int nb_dupreq_prealloc;
  unsigned int nb_client_id_prealloc;
  unsigned int nb_ip_stats_prealloc;
  unsigned int nb_before_gc;
  unsigned int nb_dupreq_before_gc;
} nfs_worker_parameter_t;

typedef struct nfs_rpc_dupreq_param__
{
  hash_parameter_t hash_param;
} nfs_rpc_dupreq_parameter_t;

typedef struct nfs_cache_layer_parameter__
{
  cache_inode_parameter_t cache_param;
  cache_inode_client_parameter_t cache_inode_client_param;
  cache_content_client_parameter_t cache_content_client_param;
  cache_inode_gc_policy_t gcpol;
  cache_content_gc_policy_t dcgcpol;
} nfs_cache_layers_parameter_t;

typedef enum protos
{
  P_NFS,
  P_MNT,
#ifdef _USE_NLM
  P_NLM,
#endif
#ifdef _USE_QUOTA
  P_RQUOTA,
#endif
  P_COUNT
} protos;

typedef struct nfs_core_param__
{
  unsigned short port[P_COUNT];
  struct sockaddr_in bind_addr; // IPv4 only for now...
  unsigned int program[P_COUNT];
  unsigned int nb_worker;
  unsigned int nb_call_before_queue_avg;
  unsigned int nb_max_concurrent_gc;
  long core_dump_size;
  int nb_max_fd;
  unsigned int drop_io_errors;
  unsigned int drop_inval_errors;
  unsigned int drop_delay_errors;
  unsigned int use_nfs_commit;
  time_t expiration_dupreq;
  unsigned int stats_update_delay;
  unsigned int long_processing_threshold;
  unsigned int dump_stats_per_client;
  char stats_file_path[MAXPATHLEN];
  char stats_per_client_directory[MAXPATHLEN];
  char fsal_shared_library[MAXPATHLEN];
  int tcp_fridge_expiration_delay ;
  unsigned int core_options;
  unsigned int max_send_buffer_size; /* Size of RPC send buffer */
  unsigned int max_recv_buffer_size; /* Size of RPC recv buffer */
#ifdef _USE_NLM
  bool_t nsm_use_caller_name;
#endif
} nfs_core_parameter_t;

typedef struct nfs_ip_name_param__
{
  hash_parameter_t hash_param;
  unsigned int expiration_time;
  char mapfile[MAXPATHLEN];
} nfs_ip_name_parameter_t;

typedef struct nfs_ip_stats_param__
{
  hash_parameter_t hash_param;
} nfs_ip_stats_parameter_t;

typedef struct nfs_client_id_param__
{
  hash_parameter_t hash_param;
  hash_parameter_t hash_param_reverse;
} nfs_client_id_parameter_t;

typedef struct nfs_idmap_cache_param__
{
  hash_parameter_t hash_param;
  char mapfile[MAXPATHLEN];
} nfs_idmap_cache_parameter_t;

#ifdef _USE_NFS4_1
typedef struct nfs_session_id_param__
{
  hash_parameter_t hash_param;
} nfs_session_id_parameter_t;
#endif

typedef struct nfs_fsal_up_param__
{
  struct prealloc_pool event_pool;
  unsigned int nb_event_data_prealloc;
  hash_table_t *ht; /* cache inode hashtable */
} nfs_fsal_up_parameter_t;

typedef char entry_name_array_item_t[FSAL_MAX_NAME_LEN];

typedef struct nfs_version4_parameter__
{
  unsigned int lease_lifetime;
  unsigned int fh_expire;
  unsigned int returns_err_fh_expired;
  unsigned int use_open_confirm;
  unsigned int return_bad_stateid;
  char domainname[NFS4_MAX_DOMAIN_LEN];
  char idmapconf[MAXPATHLEN];
} nfs_version4_parameter_t;

typedef struct nfs_param__
{
  nfs_core_parameter_t core_param;
  nfs_worker_parameter_t worker_param;
  nfs_rpc_dupreq_parameter_t dupreq_param;
  nfs_ip_name_parameter_t ip_name_param;
  nfs_idmap_cache_parameter_t uidmap_cache_param;
  nfs_idmap_cache_parameter_t gidmap_cache_param;
  nfs_idmap_cache_parameter_t unamemap_cache_param;
  nfs_idmap_cache_parameter_t gnamemap_cache_param;
  nfs_idmap_cache_parameter_t uidgidmap_cache_param;
  nfs_ip_stats_parameter_t ip_stats_param;
#ifdef _USE_9P
  _9p_parameter_t _9p_param ;
#endif
#ifdef _USE_FSAL_UP
  nfs_fsal_up_parameter_t fsal_up_param;
#endif
#ifdef _HAVE_GSSAPI
  nfs_krb5_parameter_t krb5_param;
#endif  
  nfs_version4_parameter_t nfsv4_param;
  nfs_client_id_parameter_t client_id_param;
  nfs_state_id_parameter_t state_id_param;
#ifdef _USE_NFS4_1
  nfs_session_id_parameter_t session_id_param;
#ifdef _USE_PNFS
  pnfs_parameter_t pnfs_param;
#endif                          /* _USE_PNFS */
#endif                          /* _USE_NFS4_1 */
  nfs4_owner_parameter_t nfs4_owner_param;
#ifdef _USE_NLM
  hash_parameter_t nsm_client_hash_param;
  hash_parameter_t nlm_client_hash_param;
  hash_parameter_t nlm_owner_hash_param;
#endif
  nfs_cache_layers_parameter_t cache_layers_param;
#ifdef _USE_SHARED_FSAL
  unsigned int nb_loaded_fsal ;
  unsigned int loaded_fsal[NB_AVAILABLE_FSAL];
  fsal_parameter_t fsal_param[NB_AVAILABLE_FSAL];
#else
  fsal_parameter_t fsal_param;
#endif
  external_tools_parameter_t extern_param;

  /* list of exports declared in config file */
  exportlist_t *pexportlist;
#ifndef _NO_BUDDY_SYSTEM
  /* buddy parameter for workers and dispatcher */
  buddy_parameter_t buddy_param_worker;
  buddy_parameter_t buddy_param_admin;
  buddy_parameter_t buddy_param_tcp_mgr;
  buddy_parameter_t buddy_param_fsal_up;
#endif

#ifdef _USE_MFSL
  mfsl_parameter_t mfsl_param;
#endif

} nfs_parameter_t;

typedef struct nfs_worker_stat__
{
  unsigned int nb_total_req;
  unsigned int nb_udp_req;
  unsigned int nb_tcp_req;
  nfs_request_stat_t stat_req;

  /* the last time stat have been retrieved from buddy and FSAL layers */
  time_t last_stat_update;
  fsal_statistics_t fsal_stats;
#ifndef _NO_BUDDY_SYSTEM
  buddy_stats_t buddy_stats;
#endif

} nfs_worker_stat_t;

typedef struct nfs_dupreq_stat__
{
  hash_stat_t htstat;
} nfs_dupreq_stat_t;

typedef struct nfs_request_data__
{
  SVCXPRT *xprt;
  SVCXPRT *xprt_copy;
  struct svc_req req;
  struct rpc_msg msg;
  char cred_area[2 * MAX_AUTH_BYTES + RQCRED_SIZE];
  nfs_res_t res_nfs;
  nfs_arg_t arg_nfs;
} nfs_request_data_t;

typedef enum request_type__
{
  NFS_REQUEST,
  _9P_REQUEST
} request_type_t ;

typedef struct request_data__
{
  request_type_t rtype ;
  union request_content__
   {
      nfs_request_data_t nfs ;
#ifdef _USE_9P
      _9p_request_data_t _9p ;
#endif
   } rcontent ;
} request_data_t ;

typedef struct nfs_client_id__
{
  char client_name[NFS4_MAX_DOMAIN_LEN];
  clientid4 clientid;
  uint32_t cb_program;
  char client_r_addr[SOCK_NAME_MAX];
  char client_r_netid[MAXNAMLEN];
  verifier4 verifier;
  verifier4 incoming_verifier;
  time_t last_renew;
  nfs_clientid_confirm_state_t confirmed;
  nfs_client_cred_t credential;
#ifdef _USE_NFS4_1
  char server_owner[MAXNAMLEN];
  char server_scope[MAXNAMLEN];
  unsigned int nb_session;
  nfs41_session_slot_t create_session_slot;
  unsigned create_session_sequence;
#endif
} nfs_client_id_t;

typedef enum idmap_type__
{ UIDMAP_TYPE = 1,
  GIDMAP_TYPE = 2
} idmap_type_t;

typedef enum pause_state
{
  STATE_STARTUP,
  STATE_AWAKEN,
  STATE_AWAKE,
  STATE_PAUSE,
  STATE_PAUSED,
  STATE_EXIT
} pause_state_t;
  
typedef struct nfs_thread_control_block__
{
  pthread_cond_t tcb_condvar;
  pthread_mutex_t tcb_mutex;
  int tcb_ready;
  pause_state_t tcb_state;
  char tcb_name[256];
  struct glist_head tcb_list;
} nfs_tcb_t;

typedef struct nfs_worker_data__
{
  unsigned int worker_index;
  LRU_list_t *pending_request;
  LRU_list_t *duplicate_request;
  struct prealloc_pool request_pool;
  struct prealloc_pool dupreq_pool;
  struct prealloc_pool ip_stats_pool;
  struct prealloc_pool clientid_pool;
  cache_inode_client_t cache_inode_client;
  cache_content_client_t cache_content_client;
  hash_table_t *ht;
  hash_table_t *ht_ip_stats;
  pthread_mutex_t request_pool_mutex;
  nfs_tcb_t wcb; /* Worker control block */

  nfs_worker_stat_t stats;
  unsigned int passcounter;
  sockaddr_t hostaddr;
  unsigned int gc_in_progress;
  unsigned int current_xid;
#ifdef _USE_SHARED_FSAL
  fsal_op_context_t thread_fsal_context[NB_AVAILABLE_FSAL];
#else
  fsal_op_context_t thread_fsal_context;
#endif

  /* Description of current or most recent function processed and start time (or 0) */
  const nfs_function_desc_t *pfuncdesc;
  struct timeval timer_start;
} nfs_worker_data_t;

/* flush thread data */
typedef struct nfs_flush_thread_data__
{
  unsigned int thread_index;

  /* stats */
  unsigned int nb_flushed;
  unsigned int nb_too_young;
  unsigned int nb_errors;
  unsigned int nb_orphans;

} nfs_flush_thread_data_t;

typedef struct fridge_entry__
{
  pthread_t thrid ;
  pthread_mutex_t condmutex ;
  pthread_cond_t condvar ;
  unsigned int frozen ;
  void * arg ;
  struct fridge_entry__ * pprev ;
  struct fridge_entry__ * pnext ;
} fridge_entry_t  ;

extern nfs_parameter_t nfs_param;
extern time_t ServerBootTime;
extern nfs_worker_data_t *workers_data;
extern char config_path[MAXPATHLEN];

typedef enum process_status
{
  PROCESS_DISPATCHED,
  PROCESS_LOST_CONN,
  PROCESS_DONE
} process_status_t;

typedef enum pause_reason
{
  PAUSE_RELOAD_EXPORTS,
  PAUSE_SHUTDOWN,
} pause_reason_t;

typedef enum awaken_reason
{
  AWAKEN_STARTUP,
  AWAKEN_RELOAD_EXPORTS,
} awaken_reason_t;

typedef enum pause_rc
{
  PAUSE_OK,
  PAUSE_PAUSE, /* Calling thread should pause - most callers can ignore this return code */
  PAUSE_EXIT,  /* Calling thread should exit */
} pause_rc;

extern const char *pause_rc_str[];

typedef enum worker_available_rc
{
  WORKER_AVAILABLE,
  WORKER_BUSY,
  WORKER_PAUSED,
  WORKER_GC,
  WORKER_ALL_PAUSED,
  WORKER_EXIT
} worker_available_rc;

/* 
 *functions prototypes
 */
enum auth_stat AuthenticateRequest(nfs_request_data_t *pnfsreq,
                                   bool_t *dispatch);
worker_available_rc worker_available(unsigned long index, unsigned int avg_number_pending);
pause_rc pause_workers(pause_reason_t reason);
pause_rc wake_workers(awaken_reason_t reason);
pause_rc wait_for_workers_to_awaken();
void DispatchWorkNFS(request_data_t *pnfsreq, unsigned int worker_index);
void *worker_thread(void *IndexArg);
process_status_t process_rpc_request(SVCXPRT *xprt);
void *rpc_dispatcher_thread(void *arg);
void *admin_thread(void *arg);
void *stats_thread(void *IndexArg);
void *long_processing_thread(void *arg);
void *stat_exporter_thread(void *IndexArg);
int stats_snmp(nfs_worker_data_t * workers_data_local);
void *file_content_gc_thread(void *IndexArg);
void *nfs_file_content_flush_thread(void *flush_data_arg);

#ifdef _USE_UPCALL_SIMULATOR
void * upcall_simulator_thread( void * UnusedArg ) ;
#endif

#ifdef _USE_9P
void * _9p_dispatcher_thread(void *arg);
void DispatchWork9P(request_data_t *pnfsreq, unsigned int worker_index);
void _9p_process_request( _9p_request_data_t * preq9p, nfs_worker_data_t * pworker_data ) ;
#endif

void nfs_operate_on_sigusr1() ;
void nfs_operate_on_sigterm() ;
void nfs_operate_on_sighup() ;

void nfs_Init_svc(void);
void nfs_Init_admin_data(hash_table_t *ht);
int nfs_Init_worker_data(nfs_worker_data_t * pdata);
int nfs_Init_request_data(nfs_request_data_t * pdata);
int nfs_Init_gc_counter(void);
void constructor_nfs_request_data_t(void *ptr);
void constructor_request_data_t(void *ptr);

/* Config parsing routines */
int get_stat_exporter_conf(config_file_t in_config, external_tools_parameter_t * out_parameter);
int nfs_read_core_conf(config_file_t in_config, nfs_core_parameter_t * pparam);
int nfs_read_worker_conf(config_file_t in_config, nfs_worker_parameter_t * pparam);
int nfs_read_dupreq_hash_conf(config_file_t in_config,
                              nfs_rpc_dupreq_parameter_t * pparam);
int nfs_read_ip_name_conf(config_file_t in_config, nfs_ip_name_parameter_t * pparam);
int nfs_read_version4_conf(config_file_t in_config, nfs_version4_parameter_t * pparam);
int nfs_read_client_id_conf(config_file_t in_config, nfs_client_id_parameter_t * pparam);
#ifdef _HAVE_GSSAPI
int nfs_read_krb5_conf(config_file_t in_config, nfs_krb5_parameter_t * pparam);
#endif
int nfs_read_uidmap_conf(config_file_t in_config, nfs_idmap_cache_parameter_t * pparam);
int nfs_read_gidmap_conf(config_file_t in_config, nfs_idmap_cache_parameter_t * pparam);
int nfs_read_state_id_conf(config_file_t in_config, nfs_state_id_parameter_t * pparam);
#ifdef _USE_NFS4_1
int nfs_read_session_id_conf(config_file_t in_config,
                             nfs_session_id_parameter_t * pparam);
#ifdef _USE_PNFS
int nfs_read_pnfs_conf(config_file_t in_config, pnfs_parameter_t * pparam);
#endif                          /* _USE_PNFS */
#endif                          /* _USE_NFS4_1 */

int nfs_export_create_root_entry(exportlist_t * pexportlist, hash_table_t * ht);

/* Add a list of clients to the client array of either an exports entry or
 * another service that has a client array (like snmp or statistics exporter) */
int nfs_AddClientsToClientArray(exportlist_client_t *clients, int new_clients_number,
    char **new_clients_name, int option);

int parseAccessParam(char *var_name, char *var_value,
                     exportlist_t *p_entry, int access_option);

/* Checks an access list for a specific client */
int export_client_match(sockaddr_t *hostaddr,
                        char *ipstring,
                        exportlist_client_t *clients,
                        exportlist_client_entry_t * pclient_found,
                        unsigned int export_option);
int export_client_matchv6(struct in6_addr *paddrv6,
                          exportlist_client_t *clients,
                          exportlist_client_entry_t * pclient_found,
                          unsigned int export_option);

/* Config reparsing routines */
void admin_replace_exports();
int CleanUpExportContext(fsal_export_context_t * p_export_context);
exportlist_t *RemoveExportEntry(exportlist_t * exportEntry);

/* Tools */
unsigned int get_rpc_xid(struct svc_req *reqp);
void Print_param_worker_in_log(nfs_worker_parameter_t * pparam);
void Print_param_in_log();

void nfs_reset_stats(void);

int display_req_key(hash_buffer_t * pbuff, char *str);
int display_req_val(hash_buffer_t * pbuff, char *str);
int compare_req(hash_buffer_t * buff1, hash_buffer_t * buff2);

int print_entry_dupreq(LRU_data_t data, char *str);
int clean_entry_dupreq(LRU_entry_t * pentry, void *addparam);

int clean_pending_request(LRU_entry_t * pentry, void *addparam);
int print_pending_request(LRU_data_t data, char *str);

void auth_stat2str(enum auth_stat, char *str);

int nfs_Init_client_id(nfs_client_id_parameter_t param);
int nfs_Init_client_id_reverse(nfs_client_id_parameter_t param);

int nfs_client_id_remove(clientid4 clientid, struct prealloc_pool *clientid_pool);

int nfs_client_id_get(clientid4 clientid, nfs_client_id_t * client_id_res);

int nfs_client_id_get_reverse(char *key, nfs_client_id_t * client_id_res);

int nfs_client_id_Get_Pointer(clientid4 clientid, nfs_client_id_t ** ppclient_id_res);

int nfs_client_id_add(clientid4 clientid,
                      nfs_client_id_t client_record,
                      struct prealloc_pool *clientid_pool);

int nfs_client_id_set(clientid4 clientid,
                      nfs_client_id_t client_record,
                      struct prealloc_pool *clientid_pool);

int nfs_client_id_compute(char *name, clientid4 * pclientid);
int nfs_client_id_basic_compute(char *name, clientid4 * pclientid);

int display_client_id(hash_buffer_t * pbuff, char *str);
int display_client_id_reverse(hash_buffer_t * pbuff, char *str);
int display_client_id_val(hash_buffer_t * pbuff, char *str);

int compare_client_id(hash_buffer_t * buff1, hash_buffer_t * buff2);
int compare_client_id_reverse(hash_buffer_t * buff1, hash_buffer_t * buff2);

unsigned long client_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef);
unsigned long client_id_rbt_hash_func_reverse(hash_parameter_t * p_hparam,
                                              hash_buffer_t * buffclef);

unsigned long state_id_value_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef);
unsigned long state_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef);
unsigned int state_id_hash_both( hash_parameter_t * p_hparam,
				 hash_buffer_t    * buffclef, 
				 uint32_t * phashval, uint32_t * prbtval );

unsigned long client_id_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef);
unsigned long client_id_value_hash_func_reverse(hash_parameter_t * p_hparam,
                                                hash_buffer_t * buffclef);
unsigned int client_id_value_both_reverse( hash_parameter_t * p_hparam,
				           hash_buffer_t    * buffclef, 
				           uint32_t * phashval, uint32_t * prbtval ) ;

unsigned long idmapper_rbt_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef);
unsigned long int namemapper_rbt_hash_func(hash_parameter_t * p_hparam,
                                           hash_buffer_t * buffclef);

unsigned long int namemapper_value_hash_func(hash_parameter_t * p_hparam,
                                             hash_buffer_t * buffclef);
unsigned long idmapper_value_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef);

int idmap_populate(char *path, idmap_type_t maptype);

int idmap_gid_init(nfs_idmap_cache_parameter_t param);
int idmap_gname_init(nfs_idmap_cache_parameter_t param);

int idmap_uid_init(nfs_idmap_cache_parameter_t param);
int idmap_uname_init(nfs_idmap_cache_parameter_t param);
int uidgidmap_init(nfs_idmap_cache_parameter_t param);

int display_idmapper_val(hash_buffer_t * pbuff, char *str);
int display_idmapper_key(hash_buffer_t * pbuff, char *str);

int compare_idmapper(hash_buffer_t * buff1, hash_buffer_t * buff2);
int compare_namemapper(hash_buffer_t * buff1, hash_buffer_t * buff2);
int compare_state_id(hash_buffer_t * buff1, hash_buffer_t * buff2);

int idmap_compute_hash_value(char *name, uint32_t * phashval);
int idmap_add(hash_table_t * ht, char *key, unsigned int val);
int uidmap_add(char *key, unsigned int val);
int gidmap_add(char *key, unsigned int val);

int namemap_add(hash_table_t * ht, unsigned int key, char *val);
int unamemap_add(unsigned int key, char *val);
int gnamemap_add(unsigned int key, char *val);
int uidgidmap_add(unsigned int key, unsigned int value);

int idmap_get(hash_table_t * ht, char *key, unsigned long *pval);
int uidmap_get(char *key, unsigned long *pval);
int gidmap_get(char *key, unsigned long *pval);

int namemap_get(hash_table_t * ht, unsigned int key, char *pval);
int unamemap_get(unsigned int key, char *val);
int gnamemap_get(unsigned int key, char *val);
int uidgidmap_get(unsigned int key, unsigned int *pval);

int idmap_remove(hash_table_t * ht, char *key);
int uidmap_remove(char *key);
int gidmap_remove(char *key);

int namemap_remove(hash_table_t * ht, unsigned int key);
int unamemap_remove(unsigned int key);
int gnamemap_remove(unsigned int key);
int uidgidmap_remove(unsigned int key);

int uidgidmap_clear();
int idmap_clear();
int namemap_clear();

void idmap_get_stats(idmap_type_t maptype, hash_stat_t * phstat,
                     hash_stat_t * phstat_reverse);

int fridgethr_get( pthread_t * pthrid, void *(*thrfunc)(void*), void * thrarg ) ;
fridge_entry_t * fridgethr_freeze( ) ;
int fridgethr_init() ;

#ifdef _USE_NFS4_1
int display_session_id_key(hash_buffer_t * pbuff, char *str);
int display_session_id_val(hash_buffer_t * pbuff, char *str);
int compare_session_id(hash_buffer_t * buff1, hash_buffer_t * buff2);
unsigned long session_id_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef);
unsigned long session_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef);
int nfs41_Init_session_id(nfs_session_id_parameter_t param);
int nfs41_Session_Set(char sessionid[NFS4_SESSIONID_SIZE],
                      nfs41_session_t * psession_data);
int nfs41_Session_Get(char sessionid[NFS4_SESSIONID_SIZE],
                      nfs41_session_t * psession_data);
int nfs41_Session_Get_Pointer(char sessionid[NFS4_SESSIONID_SIZE],
                              nfs41_session_t * *psession_data);
int nfs41_Session_Update(char sessionid[NFS4_SESSIONID_SIZE],
                         nfs41_session_t * psession_data);
int nfs41_Session_Del(char sessionid[NFS4_SESSIONID_SIZE]);
int nfs41_Build_sessionid(clientid4 * pclientid, char sessionid[NFS4_SESSIONID_SIZE]);
void nfs41_Session_PrintAll(void);
#endif

int nfs_Init_ip_name(nfs_ip_name_parameter_t param);
hash_table_t *nfs_Init_ip_stats(nfs_ip_stats_parameter_t param);
int nfs_Init_dupreq(nfs_rpc_dupreq_parameter_t param);

extern const nfs_function_desc_t *INVALID_FUNCDESC;
const nfs_function_desc_t *nfs_rpc_get_funcdesc(nfs_request_data_t * preqnfs);
int nfs_rpc_get_args(nfs_request_data_t * preqnfs, const nfs_function_desc_t *pfuncdesc);

void create_fsal_up_threads();
void nfs_Init_FSAL_UP();

#endif                          /* _NFS_CORE_H */
