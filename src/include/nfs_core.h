/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
#include "gsh_wait_queue.h"

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

extern char *nfs_host_name;

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

typedef struct _rpc_call rpc_call_t;

typedef void (*rpc_call_func) (rpc_call_t *call);

#ifdef _HAVE_GSSAPI
extern gss_OID_desc krb5oid;
#endif /* _HAVE_GSSAPI */

struct _rpc_call {
	struct clnt_req call_req;
	rpc_call_channel_t *chan;
	rpc_call_func call_hook;
	void *call_arg;
	void *call_user_data[2];
	nfs4_compound_t cbt;
	uint32_t states;
	uint32_t flags;
};

/* in nfs_init.c */

struct _nfs_health {
	uint64_t enqueued_reqs;
	uint64_t dequeued_reqs;
};

extern struct _nfs_health nfs_health_;
bool nfs_health(void);

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
extern struct timespec nfs_ServerBootTime;
extern time_t nfs_ServerEpoch;

extern verifier4 NFS4_write_verifier;	/*< NFS V4 write verifier */
extern writeverf3 NFS3_write_verifier;	/*< NFS V3 write verifier */

extern char *nfs_config_path;
extern char *nfs_pidfile_path;

/*
 * Thread entry functions
 */

#ifdef _USE_9P
void *_9p_dispatcher_thread(void *arg);
void _9p_tcp_process_request(struct _9p_request_data *req9p);
int _9p_process_buffer(struct _9p_request_data *req9p, char *replydata,
		       u32 *poutlen);

int _9p_worker_init(void);
int _9p_worker_shutdown(void);
void DispatchWork9P(struct _9p_request_data *req);
#endif

#ifdef _USE_9P_RDMA
void *_9p_rdma_dispatcher_thread(void *arg);
void _9p_rdma_process_request(struct _9p_request_data *req9p);
void _9p_rdma_cleanup_conn(msk_trans_t *trans);
#endif

/* in nfs_rpc_dispatcher_thread.c */

void Clean_RPC(void);
void nfs_Init_svc(void);
void nfs_rpc_dispatch_stop(void);

/* Config parsing routines */
extern config_file_t nfs_config_struct;
extern struct config_block nfs_core;
extern struct config_block nfs_ip_name;
#ifdef _HAVE_GSSAPI
extern struct config_block krb5_param;
#endif
extern struct config_block version4_param;

/* in nfs_admin_thread.c */

extern bool admin_shutdown;
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

int reaper_init(void);
void reaper_wake(void);
int reaper_shutdown(void);

#endif				/* !NFS_CORE_H */
