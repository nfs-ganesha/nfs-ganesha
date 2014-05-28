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

#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "wait_queue.h"
#include "gsh_config.h"
#include "cache_inode.h"
#ifdef _USE_9P
#include "9p.h"
#endif
#ifdef _ERROR_INJECTION
#include "err_inject.h"
#endif

/* Arbitrary string buffer lengths */
#define XATTR_BUFFERSIZE 4096

char *host_name;

/*
 * Bind protocol family, pending a richer interface model.
 */
#define P_FAMILY AF_INET6

typedef struct nfs_request_data {
	SVCXPRT *xprt;
	struct svc_req req;
	struct nfs_request_lookahead lookahead;
	nfs_arg_t arg_nfs;
	nfs_res_t *res_nfs;
	const nfs_function_desc_t *funcdesc;
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

typedef int32_t(*rpc_call_func) (rpc_call_t *call, rpc_call_hook hook,
				 void *arg, uint32_t flags);

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
	UNKNOWN_REQUEST,
	NFS_CALL,
	NFS_REQUEST,
#ifdef _USE_9P
	_9P_REQUEST,
#endif				/* _USE_9P */
} request_type_t;

typedef struct request_data {
	struct glist_head req_q;	/* chaining of pending requests */
	request_type_t rtype;
	union request_content {
		rpc_call_t *call;
		nfs_request_data_t *nfs;
#ifdef _USE_9P
		struct _9p_request_data _9p;
#endif
	} r_u;
	struct timespec time_queued;	/*< The time at which a request was
					 *  added to the worker thread queue.
					 */
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
} netid_nc_table[] = {
	{
	"-", 1, _NC_ERR, 0}, {
	"tcp", 3, _NC_TCP, AF_INET}, {
	"tcp6", 4, _NC_TCP6, AF_INET6}, {
	"rdma", 4, _NC_RDMA, AF_INET}, {
	"rdma6", 5, _NC_RDMA6, AF_INET6}, {
	"sctp", 4, _NC_SCTP, AF_INET}, {
	"sctp6", 5, _NC_SCTP6, AF_INET6}, {
	"udp", 3, _NC_UDP, AF_INET}, {
	"udp6", 4, _NC_UDP6, AF_INET6},};

nc_type nfs_netid_to_nc(const char *netid);
void nfs_set_client_location(nfs_client_id_t *pclientid,
			     const clientaddr4 *addr4);

/* end TI-RPC */

typedef struct gsh_addr {
	nc_type nc;
	struct sockaddr_storage ss;
	uint32_t port;
} gsh_addr_t;

extern pool_t *request_pool;
extern pool_t *request_data_pool;
extern pool_t *dupreq_pool;	/* XXX hide */

/**
 * @brief Per-worker data.  Some of this will be destroyed.
 */

struct nfs_worker_data {
	unsigned int worker_index;	/*< Index for log messages */
	wait_q_entry_t wqe;	/*< Queue for coordinating with decoder */

	sockaddr_t hostaddr;	/*< Client address */
	struct fridgethr_context *ctx;	/*< Link back to thread context */
};

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
extern struct timespec ServerBootTime;
extern time_t ServerEpoch;

extern verifier4 NFS4_write_verifier;	/*< NFS V4 write verifier */
extern writeverf3 NFS3_write_verifier;	/*< NFS V3 write verifier */

extern nfs_worker_data_t *workers_data;
extern char *config_path;
extern char *pidfile_path;
extern ushort g_nodeid;

/*
 * function prototypes
 */
request_data_t *nfs_rpc_get_nfsreq(uint32_t flags);
void nfs_rpc_enqueue_req(request_data_t *req);

/*
 * Thread entry functions
 */
void *rpc_dispatcher_thread(void *UnusedArg);
void *admin_thread(void *UnusedArg);
void *reaper_thread(void *UnusedArg);
void *sigmgr_thread(void *UnusedArg);

#ifdef _USE_9P
void *_9p_dispatcher_thread(void *arg);
void _9p_tcp_process_request(struct _9p_request_data *req9p,
			     nfs_worker_data_t *worker_data);
int _9p_process_buffer(struct _9p_request_data *req9p,
		       nfs_worker_data_t *worker_data, char *replydata,
		       u32 *poutlen);

void DispatchWork9P(request_data_t *req);
#endif

#ifdef _USE_9P_RDMA
void *_9p_rdma_dispatcher_thread(void *arg);
void _9p_rdma_process_request(struct _9p_request_data *req9p,
			      nfs_worker_data_t *worker_data);
void _9p_rdma_cleanup_conn(msk_trans_t *trans);
#endif

void nfs_Init_svc(void);
int nfs_Init_worker_data(nfs_worker_data_t *pdata);
int nfs_Init_request_data(nfs_request_data_t *pdata);
void nfs_rpc_dispatch_threads(pthread_attr_t *attr_thr);
void nfs_rpc_dispatch_stop(void);
void Clean_RPC(void);

/* Config parsing routines */
extern config_file_t config_struct;
extern struct config_block nfs_core;
extern struct config_block nfs_ip_name;
#ifdef _HAVE_GSSAPI
extern struct config_block krb5_param;
#endif
extern struct config_block version4_param;

/* Admin thread control */

void nfs_Init_admin_thread(void);
void admin_replace_exports(void);
void admin_halt(void);

/* Tools */

int compare_state_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);

/* used in DBUS-api diagnostic functions (e.g., serialize sessionid) */
int b64_ntop(u_char const *src, size_t srclength, char *target,
	     size_t targsize);
int b64_pton(char const *src, u_char *target, size_t targsize);

unsigned int nfs_core_select_worker_queue(unsigned int avoid_index);

int nfs_Init_ip_name(void);

void nfs_rpc_destroy_chan(rpc_call_channel_t *chan);
int32_t nfs_rpc_dispatch_call(rpc_call_t *call, uint32_t flags);

int reaper_init(void);
int reaper_shutdown(void);

int worker_init(void);
int worker_shutdown(void);

#endif				/* !NFS_CORE_H */
