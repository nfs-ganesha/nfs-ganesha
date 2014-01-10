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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
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

#include "ganesha_rpc.h"
#include "fsal.h"
#include "cache_inode.h"
#include "nfs_stat.h"
#include "external_tools.h"

#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "nfs_tcb.h"
#include "wait_queue.h"
#include "err_HashTable.h"

#include "fsal_up.h"
#include "timers.h"

#ifdef _USE_9P
#include "9p.h"
#endif

#ifdef _ERROR_INJECTION
#include "err_inject.h"
#endif

#ifdef _USE_GPFS
/* HACK for ease of testing, remove this - jw */
#define SONAS
#endif

/* Maximum thread count */
#define NB_MAX_WORKER_THREAD 4096
#define NB_MAX_FLUSHER_THREAD 100

/* NFS daemon behavior default values */
#define NB_WORKER_THREAD_DEFAULT  16
#define NB_FLUSHER_THREAD_DEFAULT 16
#define NB_REQUEST_BEFORE_QUEUE_AVG  1000
#define NB_MAX_PENDING_REQUEST 30
#define PRIME_ID_MAPPER 17      /* has to be a prime number */

#define DRC_TCP_NPART 1
#define DRC_TCP_SIZE 1024
#define DRC_TCP_CACHESZ 127 /* make prime */
#define DRC_TCP_HIWAT 64 /* 1/2(size) */
#define DRC_TCP_RECYCLE_NPART 7
#define DRC_TCP_RECYCLE_EXPIRE_S 600 /* 10m */
#define DRC_TCP_CHECKSUM true

#define DRC_UDP_NPART 7
#define DRC_UDP_SIZE 32768
#define DRC_UDP_CACHESZ  599 /* make prime */
#define DRC_UDP_HIWAT 16384 /* 1/2(size) */
#define DRC_UDP_CHECKSUM true

#define PRIME_CACHE_INODE 37    /* has to be a prime number */

#define PRIME_IP_NAME            17
#define IP_NAME_EXPIRATION       36000
#define PRIME_IP_STATS            17
#define PRIME_CLIENT_ID            17
#define PRIME_STATE_ID            17

#define DEFAULT_NFS_PRINCIPAL     "nfs" /* GSSAPI will expand this to nfs/host@DOMAIN */
#define DEFAULT_NFS_KEYTAB        ""    /* let GSSAPI use keytab specified in /etc/krb5.conf */
#define DEFAULT_NFS_CCACHE_DIR    "/var/run/ganesha"

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
#define RQCRED_SIZE           400        /* this size is excessive */
#define NFS_DEFAULT_SEND_BUFFER_SIZE 1048576
#define NFS_DEFAULT_RECV_BUFFER_SIZE 1048576

/* Default 'Raw Dev' values */
#define GANESHA_RAW_DEV_MAJOR 168
#define GANESHA_RAW_DEV_MINOR 168

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

typedef char path_str_t[MAXPATHLEN] ;

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
  unsigned int nb_before_gc;
} nfs_worker_parameter_t;

typedef struct nfs_rpc_dupreq_param__
{
  hash_parameter_t hash_param;
} nfs_rpc_dupreq_parameter_t;

typedef enum protos
{
  P_NFS,
  P_MNT,
#ifdef _USE_NLM
  P_NLM,
#endif
#ifdef _USE_RQUOTA
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
  long core_dump_size;
  int nb_max_fd;
  unsigned int drop_io_errors;
  unsigned int drop_inval_errors;
  unsigned int drop_delay_errors;
  unsigned int use_nfs_commit;
  time_t expiration_dupreq;
  unsigned int dispatch_max_reqs;
  unsigned int dispatch_max_reqs_xprt;
  unsigned int stats_update_delay;
  struct {
    bool disabled;
    struct {
      uint32_t npart;
      uint32_t size;
      uint32_t cachesz;
      uint32_t hiwat;
      uint32_t recycle_npart;
      uint32_t recycle_expire_s;
      bool checksum;
    } tcp;
    struct {
      uint32_t npart;
      uint32_t size;
      uint32_t cachesz;
      uint32_t hiwat;
      bool checksum;
    } udp;
  } drc;
  unsigned int long_processing_threshold;
  unsigned int long_processing_threshold_msec;
  unsigned int dump_stats_per_client;
  char stats_file_path[MAXPATHLEN];
  char stats_per_client_directory[MAXPATHLEN];
  int decoder_fridge_expiration_delay ;
  unsigned int core_options;
  unsigned int max_send_buffer_size; /* Size of RPC send buffer */
  unsigned int max_recv_buffer_size; /* Size of RPC recv buffer */
#ifdef _USE_NLM
  bool_t nsm_use_caller_name;
#endif
  bool_t clustered;
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
  hash_parameter_t cid_confirmed_hash_param;
  hash_parameter_t cid_unconfirmed_hash_param;
  hash_parameter_t cr_hash_param;
} nfs_client_id_parameter_t;

typedef struct nfs_state_id_param__
{
  hash_parameter_t hash_param;
} nfs_state_id_parameter_t;

typedef struct nfs4_owner_parameter_t
{
  hash_parameter_t hash_param;
} nfs4_owner_parameter_t;

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

typedef struct nfs4_pseudo_param__
{
  hash_parameter_t hash_param;
} nfs4_pseudo_parameter_t;

typedef char entry_name_array_item_t[FSAL_MAX_NAME_LEN+1];

typedef struct nfs_version4_parameter__
{
  unsigned int lease_lifetime;
  unsigned int fh_expire;
  unsigned int returns_err_fh_expired;
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
#ifdef _HAVE_GSSAPI
  nfs_krb5_parameter_t krb5_param;
#endif  
  nfs_version4_parameter_t nfsv4_param;
  nfs_client_id_parameter_t client_id_param;
  nfs_state_id_parameter_t state_id_param;
#ifdef _USE_NFS4_1
  nfs_session_id_parameter_t session_id_param;
#endif                          /* _USE_NFS4_1 */
  nfs4_pseudo_parameter_t nfs4_pseudofs_param;
  nfs4_owner_parameter_t nfs4_owner_param;
#ifdef _USE_NLM
  hash_parameter_t nsm_client_hash_param;
  hash_parameter_t nlm_client_hash_param;
  hash_parameter_t nlm_owner_hash_param;
#endif
#ifdef _USE_9P
  hash_parameter_t _9p_owner_hash_param;
#endif 
  fsal_parameter_t fsal_param;
  external_tools_parameter_t extern_param;

  /* list of exports declared in config file */
  struct glist_head *pexportlist;
} nfs_parameter_t;

typedef struct nfs_dupreq_stat
{
  hash_stat_t htstat;
} nfs_dupreq_stat_t;

typedef struct nfs_request_data
{
  SVCXPRT *xprt;
  struct svc_req req;
  struct rpc_msg msg;
  struct nfs_request_lookahead lookahead;
  const nfs_function_desc_t *funcdesc;
  char cred_area[2 * MAX_AUTH_BYTES + RQCRED_SIZE];
  nfs_res_t *res_nfs;
  nfs_arg_t arg_nfs;
  struct timeval time_queued; /* The time at which a request was added
                               * to the worker thread queue. */
} nfs_request_data_t;

enum rpc_chan_type {
    RPC_CHAN_V40,
    RPC_CHAN_V41
};

typedef struct rpc_call_channel
{
    enum rpc_chan_type type;
    pthread_mutex_t mtx;
    uint32_t states;
    union {
        struct {
            nfs_client_id_t *pclientid;
        } v40;
    } nvu;
    time_t last_called;
    CLIENT *clnt;
    AUTH *auth;
    struct rpc_gss_sec gss_sec;
} rpc_call_channel_t;

typedef struct __nfs4_compound {
    union {
        int type;
        struct {
            CB_COMPOUND4args args;
            CB_COMPOUND4res res;
        } v4;
    } v_u;
} nfs4_compound_t;

/* RPC callback processing */
typedef enum rpc_call_hook
{
    RPC_CALL_COMPLETE,
    RPC_CALL_ABORT,
} rpc_call_hook;

typedef struct _rpc_call rpc_call_t;

typedef int32_t (*rpc_call_func)(rpc_call_t* call, rpc_call_hook hook,
                                 void* arg, uint32_t flags);

extern gss_OID_desc krb5oid;

struct _rpc_call
{
    rpc_call_channel_t *chan;
    rpc_call_func call_hook;
    nfs4_compound_t cbt;
    struct wait_entry we;
    enum clnt_stat stat;
    uint32_t states;
    uint32_t flags;
    void *u_data[2];
};

typedef enum request_type
{
  NFS_CALL,
  NFS_REQUEST,
#ifdef _USE_9P
  _9P_REQUEST
#endif
} request_type_t;

typedef struct request_data
{
    struct glist_head req_q; /* chaining of pending requests */
    request_type_t rtype;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    union request_content {
        rpc_call_t *call;
        nfs_request_data_t *nfs;
#ifdef _USE_9P
        _9p_request_data_t _9p;
#endif
    } r_u ;
} request_data_t ;

/* XXXX this is automatically redundant, but in fact upstream TI-RPC is
 * not up-to-date with RFC 5665, will fix (Matt)
 *
 * (c) 2012, Linux Box Corp
*/
enum rfc_5665_nc_type
{
    _NC_ERR,
    _NC_TCP,
    _NC_TCP6,
    _NC_RDMA,
    _NC_RDMA6,
    _NC_SCTP,
    _NC_SCTP6,
    _NC_UDP,
    _NC_UDP6,
};
typedef enum rfc_5665_nc_type nc_type;

static const struct __netid_nc_table
{
    const char *netid;
    int netid_len;
    const nc_type nc;
    int af;
} 
    netid_nc_table[]  = 
{
    { "-",      1,  _NC_ERR,    0         },
    { "tcp",    3,  _NC_TCP,    AF_INET   },
    { "tcp6",   4,  _NC_TCP6,   AF_INET6  },
    { "rdma",   4,  _NC_RDMA,   AF_INET   },
    { "rdma6",  5,  _NC_RDMA6,  AF_INET6  },
    { "sctp",   4,  _NC_SCTP,   AF_INET   },
    { "sctp6",  5,  _NC_SCTP6,  AF_INET6  },
    { "udp",    3,  _NC_UDP,    AF_INET   },
    { "udp6",   4,  _NC_UDP6,   AF_INET6  },
};

nc_type nfs_netid_to_nc(const char *netid);
void nfs_set_client_location(nfs_client_id_t *pclientid, const clientaddr4 *addr4);

/* end TI-RPC */

typedef struct gsh_addr
{
    nc_type nc;
    struct sockaddr_storage ss;
    uint32_t port;
} gsh_addr_t;

typedef enum idmap_type__
{ UIDMAP_TYPE = 1,
  GIDMAP_TYPE = 2
} idmap_type_t;

extern pool_t *request_pool;
extern pool_t *request_data_pool;
extern pool_t *dupreq_pool;
extern pool_t *ip_stats_pool;
extern pool_t *nfs_res_pool;

/* XXX in 2.0 these are in nfs_dupreq.h */
static inline nfs_res_t *
alloc_nfs_res(void)
{
    nfs_res_t *res =
        pool_alloc(nfs_res_pool, NULL); /* XXX can pool/ctor zero mem? */
    memset(res, 0, sizeof(nfs_res_t));
    return (res);
}

static inline void
free_nfs_res(nfs_res_t *res)
{
    pool_free(nfs_res_pool, res);
}

struct nfs_worker_data__
{
  unsigned int worker_index;
  hash_table_t *ht_ip_stats;

  wait_q_entry_t wqe;

  pthread_mutex_t request_pool_mutex;
  nfs_tcb_t wcb; /* Worker control block */
  export_perms_t export_perms; /* current rpc functions export perms */
  struct user_cred user_credentials; /* current user credentials */

  nfs_worker_stat_t stats;
  unsigned int passcounter;
  sockaddr_t hostaddr;
  char       hostaddr_str[SOCK_NAME_MAX];
  sigset_t sigmask; /* masked signals */
  fsal_op_context_t thread_fsal_context;
  /* Description of current or most recent function processed and start time (or 0) */
  const nfs_function_desc_t *funcdesc;
#ifdef _USE_STAT_EXPORTER
  msectimer_t timer_start;
#endif
};

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

/**
 * group together all of NFS-Ganesha's statistics
 */
typedef struct ganesha_stats__ {
    nfs_worker_stat_t       global_worker_stat;
    hash_stat_t             cache_inode_hstat;
    hash_stat_t             uid_map;
    hash_stat_t             gid_map;
    hash_stat_t             ip_name_map;
    hash_stat_t             uid_reverse;
    hash_stat_t             gid_reverse;
    fsal_statistics_t       global_fsal;
    unsigned long long     total_fsal_calls;
} ganesha_stats_t;

extern nfs_parameter_t nfs_param;

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
extern time_t ServerBootTime;
extern time_t ServerEpoch;

extern verifier4 NFS4_write_verifier;  /* NFS V4 write verifier */
extern writeverf3 NFS3_write_verifier; /* NFS V3 write verifier */

extern nfs_worker_data_t *workers_data;
extern char config_path[MAXPATHLEN];
extern char pidfile_path[MAXPATHLEN] ;
extern int g_nodeid;

typedef enum process_status
{
  PROCESS_DISPATCHED,
  PROCESS_LOST_CONN,
  PROCESS_DONE
} process_status_t;

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
 * Object pools
 */

extern pool_t *nfs_clientid_pool;

/*
 *functions prototypes
 */
pause_rc pause_workers(pause_reason_t reason);
pause_rc wake_workers(awaken_reason_t reason);
pause_rc wait_for_workers_to_awaken();
void DispatchWorkNFS(request_data_t *pnfsreq, unsigned int worker_index);
void *worker_thread(void *IndexArg);
request_data_t *nfs_rpc_get_nfsreq(uint32_t flags);
process_status_t process_rpc_request(SVCXPRT *xprt);

process_status_t dispatch_rpc_subrequest(nfs_worker_data_t *mydata,
                                         request_data_t *onfsreq);
int stats_snmp(void);
/*
 * Thread entry functions
 */
void *rpc_dispatcher_thread(void *UnusedArg);
void *admin_thread(void *UnusedArg);
void *stats_thread(void *UnusedArg);
#ifdef _USE_STAT_EXPORTER
void *long_processing_thread(void *UnusedArg);
void *stat_exporter_thread(void *UnusedArg);
#endif
void *file_content_gc_thread(void *UnusedArg);
void *nfs_file_content_flush_thread(void *flush_data_arg);
void *reaper_thread(void *UnusedArg);
#ifdef SONAS
void *recovery_thread(void *UnusedArg);
#endif
void *rpc_tcp_socket_manager_thread(void *Arg);
void *sigmgr_thread( void * UnusedArg );
void *state_async_thread(void *UnusedArg);

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
void nfs_Init_admin_data(void);
int nfs_Init_worker_data(nfs_worker_data_t * pdata);
int nfs_Init_request_data(nfs_request_data_t * pdata);
void nfs_rpc_dispatch_threads(pthread_attr_t *attr_thr);

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
#endif                          /* _USE_NFS4_1 */

int nfs_export_create_root_entry(struct glist_head * pexportlist);

/* Add a list of clients to the client list of either an exports entry or
 * another service that has a client list (like snmp or statistics exporter) */
int nfs_AddClientsToClientList(exportlist_client_t * clients,
                               int                   new_clients_number,
                               char               ** new_clients_name,
                               int                   option,
                               char                * var_name);

int parseAccessParam(char                * var_name,
                     char                * var_value,
                     exportlist_client_t * clients,
                     int                   access_option,
                     const char          * label);

/* Config reparsing routines */
void admin_replace_exports();
int CleanUpExportContext(fsal_export_context_t * p_export_context);
exportlist_t *GetExportEntry(char *exportPath);

/* Tools */
unsigned int get_rpc_xid(struct svc_req *reqp);
void Print_param_worker_in_log(nfs_worker_parameter_t * pparam);
void Print_param_in_log();

void nfs_reset_stats(void);

const char * auth_stat2str(enum auth_stat);

uint64_t name_rbt_hash_func(hash_parameter_t * p_hparam,
                            hash_buffer_t    * buffclef);
uint64_t id_rbt_hash_func(hash_parameter_t * p_hparam,
                          hash_buffer_t    * buffclef);

uint32_t id_value_hash_func(hash_parameter_t * p_hparam,
                            hash_buffer_t    * buffclef);
uint32_t name_value_hash_func(hash_parameter_t * p_hparam,
                              hash_buffer_t    * buffclef);

int idmap_populate(char *path, idmap_type_t maptype);

void idmapper_init();

int display_idmapper_name(struct display_buffer * dspbuf, hash_buffer_t * pbuff);
int display_idmapper_id(struct display_buffer * dspbuf, hash_buffer_t * pbuff);

int compare_name(hash_buffer_t * buff1, hash_buffer_t * buff2);
int compare_id(hash_buffer_t * buff1, hash_buffer_t * buff2);
int compare_state_id(hash_buffer_t * buff1, hash_buffer_t * buff2);

int idmap_add(hash_table_t * ht, char *key, uint32_t val);
int uidmap_add(char *key, uid_t val, int propagate);
int gidmap_add(char *key, gid_t val, int propagate);

int namemap_add(hash_table_t * ht, uint32_t key, char *val);
int unamemap_add(uid_t key, char *val, int propagate);
int gnamemap_add(gid_t key, char *val);
int uidgidmap_add(uid_t key, gid_t value);

int idmap_get(hash_table_t * ht, char *key, uint32_t *pval);
int uidmap_get(char *key, uid_t *pval);
int gidmap_get(char *key, gid_t *pval);

int namemap_get(hash_table_t * ht, uint32_t key, char *pval, size_t size);
int unamemap_get(uid_t key, char *val, size_t size);
int gnamemap_get(gid_t key, char *val, size_t size);
int uidgidmap_get(uid_t key, gid_t *pval);

int idmap_remove(hash_table_t * ht, char *key);
int uidmap_remove(char *key);
int gidmap_remove(char *key);

int namemap_remove(hash_table_t * ht, uint32_t key);
int unamemap_remove(uid_t key);
int gnamemap_remove(gid_t key);
int uidgidmap_remove(uid_t key);

int uidgidmap_clear();
int idmap_clear();
int namemap_clear();

void idmap_get_stats(idmap_type_t maptype, hash_stat_t * phstat,
                     hash_stat_t * phstat_reverse);

#define WORKER_INDEX_ANY INT_MAX
unsigned int nfs_core_select_worker_queue(unsigned int avoid_index) ;

int nfs_Init_ip_name(nfs_ip_name_parameter_t param);
hash_table_t *nfs_Init_ip_stats(nfs_ip_stats_parameter_t param);
int nfs_Init_dupreq(nfs_rpc_dupreq_parameter_t param);
int Init_nfs4_pseudo(nfs4_pseudo_parameter_t param);
uint32_t nfs4_pseudo_value_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t    * buffclef);
uint64_t nfs4_pseudo_rbt_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t    * buffclef);
int compare_nfs4_pseudo_key(hash_buffer_t *buff1,
                            hash_buffer_t *buff2);
int display_pseudo_val(struct display_buffer * dspbuf, hash_buffer_t * pbuff);
int display_pseudo_key(struct display_buffer * dspbuf, hash_buffer_t * pbuff);

extern const nfs_function_desc_t *INVALID_FUNCDESC;

#ifdef _USE_FSAL_UP
void *fsal_up_process_thread( void * UnUsedArg );
void nfs_Init_FSAL_UP();
#endif /* _USE_FSAL_UP */

void stats_collect (ganesha_stats_t *ganesha_stats);
void nfs_rpc_destroy_chan(rpc_call_channel_t *chan);
int32_t nfs_rpc_dispatch_call(rpc_call_t *call, uint32_t flags);

/**
 *
 * nfs_Is_XattrD_Name: returns true is the string given as input is the name of an xattr object.
 *
 * Returns true is the string given as input is the name of an xattr object and returns the name of the object
 *
 * @param strname    [IN]  the name that is to be tested
 * @param objectname [OUT] if strname is related to a xattr, points to the portion of strname
                           that is the related object name.
 *
 * @return TRUE if strname is a xattr, FALSE otherwise
 *
 */
static inline bool_t nfs_XattrD_Name(char *strname, char **objectname)
{
  if(strname == NULL)
    return FALSE;

  if(!strncmp(strname, XATTRD_NAME, XATTRD_NAME_LEN))
    {
      *objectname = strname + XATTRD_NAME_LEN;
      return TRUE;
    }

  return FALSE;
}

#endif                          /* _NFS_CORE_H */
