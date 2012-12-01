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
 * @file    nfs_core.h
 * @brief   Prototypes for the different threads in the nfs core
 */

#ifndef NFS_CORE_H
#define NFS_CORE_H

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

#include "cache_inode.h"
#include "fsal_up.h"

#ifdef _USE_9P
#include "9p.h"
#endif

#ifdef _ERROR_INJECTION
#include "err_inject.h"
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
#define NB_REQUEST_BEFORE_GC 50
#define PRIME_DUPREQ 17 /* has to be a prime number */
#define PRIME_ID_MAPPER 17 /* has to be a prime number */

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

#define PRIME_IP_NAME 17
#define IP_NAME_EXPIRATION 36000

#define PRIME_IP_STATS 17

#define PRIME_CLIENT_ID 17

#define PRIME_STATE_ID 17

/* GSSAPI will expand this to nfs/host@DOMAIN */
#define DEFAULT_NFS_PRINCIPAL "nfs"
/* let GSSAPI use keytab specified in /etc/krb5.conf */
#define DEFAULT_NFS_KEYTAB ""
#define DEFAULT_NFS_CCACHE_DIR "/var/run/ganesha"

/* Config labels */
#define CONF_LABEL_NFS_CORE "NFS_Core_Param"
#define CONF_LABEL_NFS_WORKER "NFS_Worker_Param"
#define CONF_LABEL_NFS_DUPREQ "NFS_DupReq_Hash"
#define CONF_LABEL_NFS_IP_NAME "NFS_IP_Name"
#define CONF_LABEL_NFS_KRB5 "NFS_KRB5"
#define CONF_LABEL_PNFS "pNFS"
#define CONF_LABEL_NFS_VERSION4 "NFSv4"
#define CONF_LABEL_CLIENT_ID "NFSv4_ClientId_Cache"
#define CONF_LABEL_STATE_ID "NFSv4_StateId_Cache"
#define CONF_LABEL_SESSION_ID "NFSv4_Session_Cache"
#define CONF_LABEL_UID_MAPPER "UidMapper_Cache"
#define CONF_LABEL_GID_MAPPER "GidMapper_Cache"
#define CONF_LABEL_UID_MAPPER_TABLE "Users"
#define CONF_LABEL_GID_MAPPER_TABLE "Groups"
#define CONF_LABEL_IP_NAME_HOSTS "Hosts"
#define CONF_LABEL_NFSV4_REFERRALS  "NFSv4_Referrals"

/*
 * Worker and sidpatcher stack size. This needs to be a multiple of
 * the page size on some systems. This was previously set to a value
 * of 2116488. No idea where this number came from, so I'm setting
 * this to a slightly larger *round* number of 2129920 = 65 * 32768.
 */
#define THREAD_STACK_SIZE  (65 * 32768)

/* NFS/RPC specific values */
#define NFS_PORT 2049
#define RQUOTA_PORT 875
#define RQCRED_SIZE 400 /* this size is excessive */
#define NFS_DEFAULT_SEND_BUFFER_SIZE 32768
#define NFS_DEFAULT_RECV_BUFFER_SIZE 32768

/* Default 'Raw Dev' values */
#define GANESHA_RAW_DEV_MAJOR 168
#define GANESHA_RAW_DEV_MINOR 168

/* Other #define */
#define TMP_STR_LEN 256
#define AUTH_STR_LEN 30
#define PWENT_MAX_LEN 81 /* MUST be a multiple of 9 */

/* Id Mapper cache error */
#define ID_MAPPER_SUCCESS 0
#define ID_MAPPER_INSERT_MALLOC_ERROR 1
#define ID_MAPPER_NOT_FOUND 2
#define ID_MAPPER_INVALID_ARGUMENT 3
#define ID_MAPPER_FAIL 4

/* Hard and soft limit for nfsv4 quotas */
#define NFS_V4_MAX_QUOTA_SOFT 4294967296LL /*  4 GB */
#define NFS_V4_MAX_QUOTA_HARD 17179869184LL /* 16 GB */
#define NFS_V4_MAX_QUOTA      34359738368LL /* 32 GB */

/* protocol flags */
#define CORE_OPTION_NFSV3 0x00000004 /*< NFSv3 operations are supported */
#define CORE_OPTION_NFSV4 0x00000008 /*< NFSv4 operations are supported */
#define CORE_OPTION_ALL_VERS 0x0000000E

/* Things related to xattr ghost directory */
#define XATTRD_NAME ".xattr.d."
#define XATTRD_NAME_LEN 9 /* MUST be equal to strlen( XATTRD_NAME ) */
#define XATTR_BUFFERSIZE 4096

typedef char path_str_t[MAXPATHLEN] ;

/* The default attribute mask for NFSv2/NFSv3 */
#define FSAL_ATTR_MASK_V2_V3 (FSAL_ATTRS_MANDATORY | FSAL_ATTR_MODE     | \
			      FSAL_ATTR_FILEID     | FSAL_ATTR_FSID     | \
			      FSAL_ATTR_NUMLINKS   | FSAL_ATTR_OWNER    | \
			      FSAL_ATTR_GROUP      | FSAL_ATTR_SIZE     | \
			      FSAL_ATTR_ATIME      | FSAL_ATTR_MTIME    | \
			      FSAL_ATTR_CTIME      | FSAL_ATTR_SPACEUSED| \
			      FSAL_ATTR_RAWDEV)

/* The default attribute mask for NFSv4 */
#define FSAL_ATTR_MASK_V4 (FSAL_ATTRS_MANDATORY | FSAL_ATTR_MODE     | \
			   FSAL_ATTR_FILEID     | FSAL_ATTR_FSID     | \
			   FSAL_ATTR_NUMLINKS   | FSAL_ATTR_OWNER    | \
			   FSAL_ATTR_GROUP      | FSAL_ATTR_SIZE     | \
			   FSAL_ATTR_ATIME      | FSAL_ATTR_MTIME    | \
			   FSAL_ATTR_CTIME      | FSAL_ATTR_SPACEUSED| \
			   FSAL_ATTR_RAWDEV     | FSAL_ATTR_ACL)

typedef struct nfs_worker_param__ {
	unsigned int nb_before_gc;
} nfs_worker_parameter_t;

/**
 * @TODO troublesome decl.  Max cleaned up some of this...
 */
typedef struct nfs_cache_layer_parameter__ {
	cache_inode_parameter_t cache_param;
	cache_inode_gc_policy_t gcpol;
} nfs_cache_layers_parameter_t;

typedef enum protos {
	P_NFS,
	P_MNT,
	P_NLM,
	P_RQUOTA,
	P_COUNT
} protos;

typedef struct nfs_core_param__ {
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
	unsigned int dispatch_max_reqs;
	unsigned int dispatch_max_reqs_xprt;
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
	unsigned int stats_update_delay;
	unsigned int long_processing_threshold;
	unsigned int dump_stats_per_client;
	int decoder_fridge_expiration_delay;
	char stats_file_path[MAXPATHLEN];
	char stats_per_client_directory[MAXPATHLEN];
	char fsal_shared_library[MAXPATHLEN];
	unsigned int core_options;
	unsigned int max_send_buffer_size; /*< Size of RPC send buffer */
	unsigned int max_recv_buffer_size; /*< Size of RPC recv buffer */
	bool nsm_use_caller_name;
	bool clustered;
	bool enable_FSAL_upcalls;
	bool enable_NLM;
	bool enable_RQUOTA;
} nfs_core_parameter_t;

typedef struct nfs_ip_name_param__ {
	hash_parameter_t hash_param;
	unsigned int expiration_time;
	char mapfile[MAXPATHLEN];
} nfs_ip_name_parameter_t;

typedef struct nfs_ip_stats_param__ {
	hash_parameter_t hash_param;
} nfs_ip_stats_parameter_t;

typedef struct nfs_client_id_param__ {
	hash_parameter_t cid_confirmed_hash_param;
	hash_parameter_t cid_unconfirmed_hash_param;
	hash_parameter_t cr_hash_param;
} nfs_client_id_parameter_t;

typedef struct nfs_state_id_param__ {
	hash_parameter_t hash_param;
} nfs_state_id_parameter_t;

typedef struct nfs4_owner_parameter_t {
	hash_parameter_t hash_param;
} nfs4_owner_parameter_t;

typedef struct nfs_idmap_cache_param__ {
	hash_parameter_t hash_param;
	char mapfile[MAXPATHLEN];
} nfs_idmap_cache_parameter_t;

typedef struct nfs_session_id_param__ {
	hash_parameter_t hash_param;
} nfs_session_id_parameter_t;

typedef struct nfs_version4_parameter__ {
	unsigned int lease_lifetime;
	unsigned int fh_expire;
	unsigned int returns_err_fh_expired;
	unsigned int return_bad_stateid;
	char domainname[NFS4_MAX_DOMAIN_LEN];
	char idmapconf[MAXPATHLEN];
	bool use_getpwnam;
} nfs_version4_parameter_t;

typedef struct nfs_param__ {
	nfs_core_parameter_t core_param;
	nfs_worker_parameter_t worker_param;
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
#endif /* _HAVE_GSSAPI */
	nfs_version4_parameter_t nfsv4_param;
	nfs_client_id_parameter_t client_id_param;
	nfs_state_id_parameter_t state_id_param;
	nfs_session_id_parameter_t session_id_param;
	nfs4_owner_parameter_t nfs4_owner_param;
	hash_parameter_t nsm_client_hash_param;
	hash_parameter_t nlm_client_hash_param;
	hash_parameter_t nlm_owner_hash_param;
#ifdef _USE_9P
	hash_parameter_t _9p_owner_hash_param;
#endif /* _USE_9P */
	nfs_cache_layers_parameter_t cache_layers_param;
	external_tools_parameter_t extern_param;

	/* list of exports declared in config file */
	exportlist_t *pexportlist;
} nfs_parameter_t;

typedef struct nfs_dupreq_stat {
	hash_stat_t htstat;
} nfs_dupreq_stat_t;

typedef struct nfs_request_data {
	SVCXPRT *xprt;
	struct svc_req req;
	struct nfs_request_lookahead lookahead;
	nfs_arg_t arg_nfs;
	nfs_res_t *res_nfs;
	const nfs_function_desc_t *funcdesc;
	struct timeval time_queued; /*< The time at which a request was added
				     *  to the worker thread queue. */
} nfs_request_data_t;

enum rpc_chan_type {
	RPC_CHAN_V40,
	RPC_CHAN_V41
};

typedef struct rpc_call_channel {
	enum rpc_chan_type type;
	pthread_mutex_t mtx;
	uint32_t states;
	union {
		nfs_client_id_t *clientid;
		nfs41_session_t *session;
	} source;
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
typedef enum rpc_call_hook {
	RPC_CALL_COMPLETE,
	RPC_CALL_ABORT,
} rpc_call_hook;

typedef struct _rpc_call rpc_call_t;

typedef int32_t (*rpc_call_func)(rpc_call_t* call, rpc_call_hook hook,
				 void* arg, uint32_t flags);

extern gss_OID_desc krb5oid;

struct _rpc_call {
	rpc_call_channel_t *chan;
	rpc_call_func call_hook;
	nfs4_compound_t cbt;
	struct wait_entry we;
	enum clnt_stat stat;
	uint32_t states;
	uint32_t flags;
	void *u_data[2];
	void *completion_arg;
};

typedef enum request_type {
	NFS_CALL,
	NFS_REQUEST,
#ifdef _USE_9P
	_9P_REQUEST,
#endif /* _USE_9P */
} request_type_t;

typedef struct request_data {
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
} request_data_t;

/**
 * @todo Matt: this is automatically redundant, but in fact upstream
 * TI-RPC is not up-to-date with RFC 5665, will fix (Matt)
 *
 * @copyright 2012, Linux Box Corp
*/
enum rfc_5665_nc_type {
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

static const struct __netid_nc_table {
	const char *netid;
	int netid_len;
	const nc_type nc;
	int af;
} netid_nc_table[]  = {
	{"-",      1,  _NC_ERR,    0},
	{"tcp",    3,  _NC_TCP,    AF_INET},
	{"tcp6",   4,  _NC_TCP6,   AF_INET6},
	{"rdma",   4,  _NC_RDMA,   AF_INET},
	{"rdma6",  5,  _NC_RDMA6,  AF_INET6},
	{"sctp",   4,  _NC_SCTP,   AF_INET},
	{"sctp6",  5,  _NC_SCTP6,  AF_INET6},
	{"udp",    3,  _NC_UDP,    AF_INET},
	{"udp6",   4,  _NC_UDP6,   AF_INET6},
};

nc_type nfs_netid_to_nc(const char *netid);
void nfs_set_client_location(nfs_client_id_t *pclientid,
			     const clientaddr4 *addr4);

/* end TI-RPC */

typedef struct gsh_addr {
	nc_type nc;
	struct sockaddr_storage ss;
	uint32_t port;
} gsh_addr_t;

typedef enum idmap_type__ {
	UIDMAP_TYPE = 1,
	GIDMAP_TYPE = 2
} idmap_type_t;

extern pool_t *request_pool;
extern pool_t *request_data_pool;
extern pool_t *dupreq_pool; /* XXX hide */
extern pool_t *ip_stats_pool;

struct nfs_worker_data__ {
	unsigned int worker_index;
	hash_table_t *ht_ip_stats;

	wait_q_entry_t wqe;
	pthread_mutex_t request_pool_mutex;
	nfs_tcb_t wcb; /* Worker control block */

	nfs_worker_stat_t stats;
	sockaddr_t hostaddr;
	sigset_t sigmask; /* masked signals */
	unsigned int current_xid;

	/* Most recent execution start time (or 0) */
	struct timeval timer_start;
};

/* flush thread data */
typedef struct nfs_flush_thread_data__ {
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
	nfs_worker_stat_t global_worker_stat;
	hash_stat_t cache_inode_hstat;
	hash_stat_t uid_map;
	hash_stat_t gid_map;
	hash_stat_t ip_name_map;
	hash_stat_t uid_reverse;
	hash_stat_t gid_reverse;
	hash_stat_t drc_udp;
	hash_stat_t drc_tcp;
	unsigned int min_pending_request;
	unsigned int max_pending_request;
	unsigned int total_pending_request;
	unsigned int average_pending_request;
	unsigned int len_pending_request;
	unsigned int avg_latency;
	unsigned long long total_fsal_calls;
} ganesha_stats_t;

extern nfs_parameter_t nfs_param;

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
extern time_t ServerBootTime;
extern time_t ServerEpoch;

extern verifier4 NFS4_write_verifier;  /*< NFS V4 write verifier */
extern writeverf3 NFS3_write_verifier; /*< NFS V3 write verifier */

extern nfs_worker_data_t *workers_data;
extern char config_path[MAXPATHLEN];
extern char pidfile_path[MAXPATHLEN];
extern ushort g_nodeid;

typedef enum process_status {
	PROCESS_DISPATCHED,
	PROCESS_LOST_CONN,
	PROCESS_DONE
} process_status_t;

#if 0 /* XXXX */
typedef enum worker_available_rc {
	WORKER_AVAILABLE,
	WORKER_BUSY,
	WORKER_PAUSED,
	WORKER_GC,
	WORKER_ALL_PAUSED,
	WORKER_EXIT
} worker_available_rc;
#endif

extern pool_t *nfs_clientid_pool;

/*
 * function prototypes
 */
pause_rc pause_workers(pause_reason_t reason);
pause_rc wake_workers(awaken_reason_t reason);
pause_rc wait_for_workers_to_awaken(void);
void *worker_thread(void *IndexArg);
request_data_t *nfs_rpc_get_nfsreq(uint32_t flags);
void nfs_rpc_enqueue_req(request_data_t *req);
int stats_snmp(void);

/*
 * Thread entry functions
 */
void *rpc_dispatcher_thread(void *UnusedArg);
void *admin_thread(void *UnusedArg);
void *stats_thread(void *UnusedArg);
void *long_processing_thread(void *UnusedArg);
void *stat_exporter_thread(void *UnusedArg);
void *reaper_thread(void *UnusedArg);
void *rpc_tcp_socket_manager_thread(void *Arg);
void *sigmgr_thread(void *UnusedArg);
void *state_async_thread(void *UnusedArg);

#ifdef _USE_9P
void * _9p_dispatcher_thread(void *arg);
void _9p_tcp_process_request(_9p_request_data_t *preq9p,
			     nfs_worker_data_t *pworker_data);
int _9p_process_buffer(_9p_request_data_t *preq9p,
		       nfs_worker_data_t *pworker_data,
		       char *replydata, u32 *poutlen);
#endif

#ifdef _USE_9P_RDMA
void * _9p_rdma_dispatcher_thread(void *arg);
void _9p_rdma_process_request( _9p_request_data_t * preq9p, nfs_worker_data_t * pworker_data ) ;
void _9p_rdma_cleanup_conn(msk_trans_t *trans) ;
#endif

void nfs_operate_on_sigusr1(void);
void nfs_operate_on_sigterm(void);
void nfs_operate_on_sighup(void);

int nfs_init_get_n_event_chan(void);;

void nfs_Init_svc(void);
void nfs_Init_admin_data(void);
int nfs_Init_worker_data(nfs_worker_data_t *pdata);
int nfs_Init_request_data(nfs_request_data_t *pdata);
int nfs_Init_gc_counter(void);
void nfs_rpc_dispatch_threads(pthread_attr_t *attr_thr);

/* Config parsing routines */
extern config_file_t config_struct;

int get_stat_exporter_conf(config_file_t in_config,
			   external_tools_parameter_t *out_parameter);
int nfs_read_core_conf(config_file_t in_config, nfs_core_parameter_t *pparam);
int nfs_read_worker_conf(config_file_t in_config,
			 nfs_worker_parameter_t *pparam);
int nfs_read_ip_name_conf(config_file_t in_config,
			  nfs_ip_name_parameter_t *pparam);
int nfs_read_version4_conf(config_file_t in_config,
			   nfs_version4_parameter_t *pparam);
int nfs_read_client_id_conf(config_file_t in_config,
			    nfs_client_id_parameter_t *pparam);
#ifdef _HAVE_GSSAPI
int nfs_read_krb5_conf(config_file_t in_config, nfs_krb5_parameter_t *pparam);
#endif
int nfs_read_uidmap_conf(config_file_t in_config,
			 nfs_idmap_cache_parameter_t *pparam);
int nfs_read_gidmap_conf(config_file_t in_config,
			 nfs_idmap_cache_parameter_t *pparam);
int nfs_read_state_id_conf(config_file_t in_config,
			   nfs_state_id_parameter_t *pparam);
int nfs_read_session_id_conf(config_file_t in_config,
			     nfs_session_id_parameter_t *pparam);

bool nfs_export_create_root_entry(exportlist_t *pexportlist);

/* Add a list of clients to the client array of either an exports
 * entry or another service that has a client array (like snmp or
 * statistics exporter) */
int nfs_AddClientsToClientArray(exportlist_client_t *clients,
				int new_clients_number,
				char **new_clients_name,
				int option);

int parseAccessParam(char *var_name, char *var_value,
		     exportlist_t *p_entry, int access_option);

/* Checks an access list for a specific client */
bool export_client_match(sockaddr_t *hostaddr,
			 exportlist_client_t *clients,
			 exportlist_client_entry_t * pclient_found,
			 unsigned int export_option);
bool export_client_matchv6(struct in6_addr *paddrv6,
			   exportlist_client_t *clients,
			   exportlist_client_entry_t *pclient_found,
			   unsigned int export_option);

/* Config reparsing routines */
void admin_replace_exports(void);
exportlist_t *RemoveExportEntry(exportlist_t *exportEntry);
exportlist_t *GetExportEntry(char *exportPath);

/* Tools */
unsigned int get_rpc_xid(struct svc_req *reqp);
void Print_param_worker_in_log(nfs_worker_parameter_t *pparam);
void Print_param_in_log();

void nfs_reset_stats(void);

void auth_stat2str(enum auth_stat, char *str);

uint64_t idmapper_rbt_hash_func(hash_parameter_t *p_hparam,
				struct gsh_buffdesc *buffclef);
uint64_t namemapper_rbt_hash_func(hash_parameter_t *p_hparam,
				  struct gsh_buffdesc *buffclef);

uint32_t namemapper_value_hash_func(hash_parameter_t *p_hparam,
				    struct gsh_buffdesc *buffclef);
uint32_t idmapper_value_hash_func(hash_parameter_t *p_hparam,
				  struct gsh_buffdesc *buffclef);

int idmap_populate(char *path, idmap_type_t maptype);

int idmap_gid_init(nfs_idmap_cache_parameter_t param);
int idmap_gname_init(nfs_idmap_cache_parameter_t param);

int idmap_uid_init(nfs_idmap_cache_parameter_t param);
int idmap_uname_init(nfs_idmap_cache_parameter_t param);
int uidgidmap_init(nfs_idmap_cache_parameter_t param);

int display_idmapper_val(struct gsh_buffdesc *pbuff, char *str);
int display_idmapper_key(struct gsh_buffdesc *pbuff, char *str);

int compare_idmapper(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);
int compare_namemapper(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);
int compare_state_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);

uint32_t idmap_compute_hash_value(char *name);
int idmap_add(hash_table_t *ht, char *key, uint32_t val);
int uidmap_add(char *key, uid_t val);
int gidmap_add(char *key, gid_t val);

int namemap_add(hash_table_t *ht, uint32_t key, char *val);
int unamemap_add(uid_t key, char *val);
int gnamemap_add(gid_t key, char *val);
int uidgidmap_add(uid_t key, gid_t value);

int idmap_get(hash_table_t *ht, char *key, uint32_t *pval);
int uidmap_get(char *key, uid_t *pval);
int gidmap_get(char *key, gid_t *pval);

int namemap_get(hash_table_t *ht, uint32_t key, char *pval);
int unamemap_get(uid_t key, char *val);
int gnamemap_get(gid_t key, char *val);
int uidgidmap_get(uid_t key, gid_t *pval);

int idmap_remove(hash_table_t *ht, char *key);
int uidmap_remove(char *key);
int gidmap_remove(char *key);

int namemap_remove(hash_table_t *ht, uint32_t key);
int unamemap_remove(uid_t key);
int gnamemap_remove(gid_t key);
int uidgidmap_remove(uid_t key);

int uidgidmap_clear(void);
int idmap_clear(void);
int namemap_clear(void);

void idmap_get_stats(idmap_type_t maptype,
		     hash_stat_t *phstat,
		     hash_stat_t *phstat_reverse);

/* used in DBUS-api diagnostic functions (e.g., serialize sessionid) */
int b64_ntop(u_char const *src, size_t srclength, char *target,
	     size_t targsize);
int b64_pton(char const *src, u_char *target, size_t targsize);

unsigned int nfs_core_select_worker_queue(unsigned int avoid_index) ;

int nfs_Init_ip_name(nfs_ip_name_parameter_t param);
hash_table_t *nfs_Init_ip_stats(nfs_ip_stats_parameter_t param);

extern const nfs_function_desc_t *INVALID_FUNCDESC;
void stats_collect (ganesha_stats_t *ganesha_stats);
void nfs_rpc_destroy_chan(rpc_call_channel_t *chan);
int32_t nfs_rpc_dispatch_call(rpc_call_t *call, uint32_t flags);
#endif /* !NFS_CORE_H */
