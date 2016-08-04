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

#include "sal_data.h"
#include "gsh_config.h"

#ifdef _USE_9P
#include "9p.h"
#endif
#ifdef _ERROR_INJECTION
#include "err_inject.h"
#endif

/* Delegation client cache limits */
#define DELEG_SPACE_LIMIT_FILESZ 102400  /* just 100K, revisit? */
#define DELEG_SPACE_LIMIT_BLOCKS 200

#define XATTR_BUFFERSIZE 4096

char *host_name;

/*
 * Bind protocol family, pending a richer interface model.
 */
#define P_FAMILY AF_INET6

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
	struct timespec time_queued;	/*< The time at which a request was
					 *  added to the worker thread queue.
					 */
	request_type_t rtype;

	union request_content {
		rpc_call_t call;
		nfs_request_t req;
#ifdef _USE_9P
		struct _9p_request_data _9p;
#endif
	} r_u;
} request_data_t;

extern pool_t *request_pool;

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
extern struct timespec ServerBootTime;
extern time_t ServerEpoch;

extern verifier4 NFS4_write_verifier;	/*< NFS V4 write verifier */
extern writeverf3 NFS3_write_verifier;	/*< NFS V3 write verifier */

extern char *config_path;
extern char *pidfile_path;

/*
 * Thread entry functions
 */

#ifdef _USE_9P
void *_9p_dispatcher_thread(void *arg);
void _9p_tcp_process_request(struct _9p_request_data *req9p);
int _9p_process_buffer(struct _9p_request_data *req9p, char *replydata,
		       u32 *poutlen);

void DispatchWork9P(request_data_t *req);
#endif

#ifdef _USE_9P_RDMA
void *_9p_rdma_dispatcher_thread(void *arg);
void _9p_rdma_process_request(struct _9p_request_data *req9p);
void _9p_rdma_cleanup_conn(msk_trans_t *trans);
#endif

/* in nfs_rpc_dispatcher_thread.c */

void Clean_RPC(void);
void nfs_Init_svc(void);
void nfs_rpc_dispatch_threads(pthread_attr_t *attr_thr);
void nfs_rpc_dispatch_stop(void);

request_data_t *nfs_rpc_dequeue_req(nfs_worker_data_t *worker);
void nfs_rpc_enqueue_req(request_data_t *req);
uint32_t get_dequeue_count(void);
uint32_t get_enqueue_count(void);

/* in nfs_worker_thread.c */

void nfs_rpc_execute(request_data_t *req);
const nfs_function_desc_t *nfs_rpc_get_funcdesc(nfs_request_t *);

int worker_init(void);
int worker_shutdown(void);

/* Config parsing routines */
extern config_file_t config_struct;
extern struct config_block nfs_core;
extern struct config_block nfs_ip_name;
#ifdef _HAVE_GSSAPI
extern struct config_block krb5_param;
#endif
extern struct config_block version4_param;

/* in nfs_admin_thread.c */

void nfs_Init_admin_thread(void);
void *admin_thread(void *UnusedArg);
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

#endif				/* !NFS_CORE_H */
