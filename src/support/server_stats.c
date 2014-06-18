/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2013
 * Author: Jim Lieb jlieb@panasas.com
 *
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
 * -------------
 */

/**
 * @defgroup Server statistics management
 * @{
 */

/**
 * @file server_stats.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief FSAL module manager
 */

#include "config.h"

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/param.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "avltree.h"
#include "ganesha_types.h"
#ifdef USE_DBUS
#include "ganesha_dbus.h"
#endif
#include "client_mgr.h"
#include "export_mgr.h"
#include "server_stats.h"
#include <abstract_atomic.h>

#define NFS_V3_NB_COMMAND (NFSPROC3_COMMIT + 1)
#define NFS_V4_NB_COMMAND 2
#define MNT_V1_NB_COMMAND (MOUNTPROC3_EXPORT + 1)
#define MNT_V3_NB_COMMAND (MOUNTPROC3_EXPORT + 1)
#define NLM_V4_NB_OPERATION (NLMPROC4_FREE_ALL + 1)
#define RQUOTA_NB_COMMAND (RQUOTAPROC_SETACTIVEQUOTA + 1)
#define NFS_V40_NB_OPERATION (NFS4_OP_RELEASE_LOCKOWNER + 1)
#define NFS_V41_NB_OPERATION (NFS4_OP_RECLAIM_COMPLETE + 1)
#define NFS_V42_NB_OPERATION (NFS4_OP_IO_ADVISE + 1)
#define _9P_NB_COMMAND 33

struct op_name {
	char *name;
};

static const struct op_name optqta[] = {
	[RQUOTAPROC_GETQUOTA] = {.name = "GETQUOTA", },
	[RQUOTAPROC_GETACTIVEQUOTA] = {.name = "GETACTIVEQUOTA", },
	[RQUOTAPROC_SETQUOTA] = {.name = "SETQUOTA", },
	[RQUOTAPROC_SETACTIVEQUOTA] = {.name = "SETACTIVEQUOTA", },
};

static const struct op_name optmnt[] = {
	[MOUNTPROC3_NULL] = {.name = "NULL", },
	[MOUNTPROC3_MNT] = {.name = "MNT", },
	[MOUNTPROC3_DUMP] = {.name = "DUMP", },
	[MOUNTPROC3_UMNT] = {.name = "UMNT", },
	[MOUNTPROC3_UMNTALL] = {.name = "UMNTALL", },
	[MOUNTPROC3_EXPORT] = {.name = "EXPORT", },
};

static const struct op_name optnlm[] = {
	[NLMPROC4_NULL] = {.name = "NULL", },
	[NLMPROC4_TEST] = {.name = "TEST", },
	[NLMPROC4_LOCK] = {.name = "LOCK", },
	[NLMPROC4_CANCEL] = {.name = "CANCEL", },
	[NLMPROC4_UNLOCK] = {.name = "UNLOCK", },
	[NLMPROC4_GRANTED] = {.name = "GRANTED", },
	[NLMPROC4_TEST_MSG] = {.name = "TEST_MSG", },
	[NLMPROC4_LOCK_MSG] = {.name = "LOCK_MSG", },
	[NLMPROC4_CANCEL_MSG] = {.name = "CANCEL_MSG", },
	[NLMPROC4_UNLOCK_MSG] = {.name = "UNLOCK_MSG", },
	[NLMPROC4_GRANTED_MSG] = {.name = "GRANTED_MSG", },
	[NLMPROC4_TEST_RES] = {.name = "TEST_RES ", },
	[NLMPROC4_LOCK_RES] = {.name = "LOCK_RES", },
	[NLMPROC4_CANCEL_RES] = {.name = "CANCEL_RES", },
	[NLMPROC4_UNLOCK_RES] = {.name = "UNLOCK_RES", },
	[NLMPROC4_GRANTED_RES] = {.name = "GRANTED_RES", },
	[NLMPROC4_SM_NOTIFY] = {.name = "SM_NOTIFY", },
	[NLMPROC4_SHARE] = {.name = "SHARE", },
	[NLMPROC4_UNSHARE] = {.name = "UNSHARE", },
	[NLMPROC4_NM_LOCK] = {.name = "NM_LOCK", },
	[NLMPROC4_FREE_ALL] = {.name = "FREE_ALL", },
};

static const struct op_name optabv3[] = {
	[NFSPROC3_NULL] = {.name = "NULL", },
	[NFSPROC3_GETATTR] = {.name = "GETATTR", },
	[NFSPROC3_SETATTR] = {.name = "SETATTR", },
	[NFSPROC3_LOOKUP] = {.name = "LOOKUP", },
	[NFSPROC3_ACCESS] = {.name = "ACCESS" , } ,
	[NFSPROC3_READLINK] = {.name = "READLINK", },
	[NFSPROC3_READ] = {.name = "READ", },
	[NFSPROC3_WRITE] = {.name = "WRITE", },
	[NFSPROC3_CREATE] = {.name = "CREATE", },
	[NFSPROC3_MKDIR] = {.name = "MKDIR", },
	[NFSPROC3_SYMLINK] = {.name = "SYMLINK", },
	[NFSPROC3_MKNOD] = {.name = "MKNOD", },
	[NFSPROC3_REMOVE] = {.name = "REMOVE", },
	[NFSPROC3_RMDIR] = {.name = "RMDIR", },
	[NFSPROC3_RENAME] = {.name = "RENAME", },
	[NFSPROC3_LINK] = {.name = "LINK", },
	[NFSPROC3_READDIR] = {.name = "READDIR", },
	[NFSPROC3_READDIRPLUS] = {.name = "READDIRPLUS", },
	[NFSPROC3_FSSTAT] = {.name = "FSSTAT", },
	[NFSPROC3_FSINFO] = {.name = "FSINFO	", },
	[NFSPROC3_PATHCONF] = {.name = "PATHCONF", },
	[NFSPROC3_COMMIT] = {.name = "COMMIT", },
};

static const struct op_name optabv4[] = {
	[0] = {.name = "ILLEGAL", },
	[1] = {.name = "ILLEGAL",},
	[2] = {.name = "ILLEGAL",},
	[NFS4_OP_ACCESS] = {.name = "ACCESS",},
	[NFS4_OP_CLOSE] = {.name = "CLOSE",},
	[NFS4_OP_COMMIT] = {.name = "COMMIT",},
	[NFS4_OP_CREATE] = {.name = "CREATE",},
	[NFS4_OP_DELEGPURGE] = {.name = "DELEGPURGE",},
	[NFS4_OP_DELEGRETURN] = {.name = "DELEGRETURN",},
	[NFS4_OP_GETATTR] = {.name = "GETATTR",},
	[NFS4_OP_GETFH] = {.name = "GETFH",},
	[NFS4_OP_LINK] = {.name = "LINK",},
	[NFS4_OP_LOCK] = {.name = "LOCK",},
	[NFS4_OP_LOCKT] = {.name = "LOCKT",},
	[NFS4_OP_LOCKU] = {.name = "LOCKU",},
	[NFS4_OP_LOOKUP] = {.name = "LOOKUP",},
	[NFS4_OP_LOOKUPP] = {.name = "LOOKUPP",},
	[NFS4_OP_NVERIFY] = {.name = "NVERIFY",},
	[NFS4_OP_OPEN] = {.name = "OPEN",},
	[NFS4_OP_OPENATTR] = {.name = "OPENATTR",},
	[NFS4_OP_OPEN_CONFIRM] = {.name = "OPEN_CONFIRM",},
	[NFS4_OP_OPEN_DOWNGRADE] = {.name = "OPEN_DOWNGRADE",},
	[NFS4_OP_PUTFH] = {.name = "PUTFH",},
	[NFS4_OP_PUTPUBFH] = {.name = "PUTPUBFH",},
	[NFS4_OP_PUTROOTFH] = {.name = "PUTROOTFH",},
	[NFS4_OP_READ] = {.name = "READ",},
	[NFS4_OP_READDIR] = {.name = "READDIR",},
	[NFS4_OP_READLINK] = {.name = "READLINK",},
	[NFS4_OP_REMOVE] = {.name = "REMOVE",},
	[NFS4_OP_RENAME] = {.name = "RENAME",},
	[NFS4_OP_RENEW] = {.name = "RENEW",},
	[NFS4_OP_RESTOREFH] = {.name = "RESTOREFH",},
	[NFS4_OP_SAVEFH] = {.name = "SAVEFH",},
	[NFS4_OP_SECINFO] = {.name = "SECINFO",},
	[NFS4_OP_SETATTR] = {.name = "SETATTR",},
	[NFS4_OP_SETCLIENTID] = {.name = "SETCLIENTID",},
	[NFS4_OP_SETCLIENTID_CONFIRM] = {.name = "SETCLIENTID_CONFIRM",},
	[NFS4_OP_VERIFY] = {.name = "VERIFY",},
	[NFS4_OP_WRITE] = {.name = "WRITE",},
	[NFS4_OP_RELEASE_LOCKOWNER] = {.name = "RELEASE_LOCKOWNER",},
	[NFS4_OP_BACKCHANNEL_CTL] = {.name = "BACKCHANNEL_CTL",},
	[NFS4_OP_BIND_CONN_TO_SESSION] = {.name = "BIND_CONN_TO_SESSION",},
	[NFS4_OP_EXCHANGE_ID] = {.name = "EXCHANGE_ID",},
	[NFS4_OP_CREATE_SESSION] = {.name = "CREATE_SESSION",},
	[NFS4_OP_DESTROY_SESSION] = {.name = "DESTROY_SESSION",},
	[NFS4_OP_FREE_STATEID] = {.name = "FREE_STATEID",},
	[NFS4_OP_GET_DIR_DELEGATION] = {.name = "GET_DIR_DELEGATION",},
	[NFS4_OP_GETDEVICEINFO] = {.name = "GETDEVICEINFO",},
	[NFS4_OP_GETDEVICELIST] = {.name = "GETDEVICELIST",},
	[NFS4_OP_LAYOUTCOMMIT] = {.name = "LAYOUTCOMMIT",},
	[NFS4_OP_LAYOUTGET] = {.name = "LAYOUTGET",},
	[NFS4_OP_LAYOUTRETURN] = {.name = "LAYOUTRETURN",},
	[NFS4_OP_SECINFO_NO_NAME] = {.name = "SECINFO_NO_NAME",},
	[NFS4_OP_SEQUENCE] = {.name = "SEQUENCE",},
	[NFS4_OP_SET_SSV] = {.name = "SET_SSV",},
	[NFS4_OP_TEST_STATEID] = {.name = "TEST_STATEID",},
	[NFS4_OP_WANT_DELEGATION] = {.name = "WANT_DELEGATION",},
	[NFS4_OP_DESTROY_CLIENTID] = {.name = "DESTROY_CLIENTID",},
	[NFS4_OP_RECLAIM_COMPLETE] = {.name = "RECLAIM_COMPLETE",},
	/* NFSv4.2 */
	[NFS4_OP_COPY] = {.name = "COPY",},
	[NFS4_OP_OFFLOAD_ABORT] = {.name = "OFFLOAD_ABORT",},
	[NFS4_OP_COPY_NOTIFY] = {.name = "COPY_NOTIFY",},
	[NFS4_OP_OFFLOAD_REVOKE] = {.name = "OFFLOAD_REVOKE",},
	[NFS4_OP_OFFLOAD_STATUS] = {.name = "OFFLOAD_STATUS",},
	[NFS4_OP_WRITE_PLUS] = {.name = "WRITE_PLUS",},
	[NFS4_OP_READ_PLUS] = {.name = "READ_PLUS",},
	[NFS4_OP_SEEK] = {.name = "SEEK",},
	[NFS4_OP_IO_ADVISE] = {.name = "IO_ADVISE",},
};

/* Classify protocol ops for stats purposes
 */

enum proto_op_type {
	GENERAL_OP = 0,		/* default for array init */
	READ_OP,
	WRITE_OP,
	LAYOUT_OP
};

static const uint32_t nfsv3_optype[NFS_V3_NB_COMMAND] = {
	[NFSPROC3_READ] = READ_OP,
	[NFSPROC3_WRITE] = WRITE_OP,
};

static const uint32_t nfsv40_optype[NFS_V40_NB_OPERATION] = {
	[NFS4_OP_READ] = READ_OP,
	[NFS4_OP_WRITE] = WRITE_OP,
};

static const uint32_t nfsv41_optype[NFS_V41_NB_OPERATION] = {
	[NFS4_OP_READ] = READ_OP,
	[NFS4_OP_WRITE] = WRITE_OP,
	[NFS4_OP_GETDEVICEINFO] = LAYOUT_OP,
	[NFS4_OP_GETDEVICELIST] = LAYOUT_OP,
	[NFS4_OP_LAYOUTCOMMIT] = LAYOUT_OP,
	[NFS4_OP_LAYOUTGET] = LAYOUT_OP,
	[NFS4_OP_LAYOUTRETURN] = LAYOUT_OP,
};

static const uint32_t nfsv42_optype[NFS_V42_NB_OPERATION] = {
	[NFS4_OP_READ] = READ_OP,
	[NFS4_OP_WRITE] = WRITE_OP,
	[NFS4_OP_GETDEVICEINFO] = LAYOUT_OP,
	[NFS4_OP_GETDEVICELIST] = LAYOUT_OP,
	[NFS4_OP_LAYOUTCOMMIT] = LAYOUT_OP,
	[NFS4_OP_LAYOUTGET] = LAYOUT_OP,
	[NFS4_OP_LAYOUTRETURN] = LAYOUT_OP,
	[NFS4_OP_WRITE_PLUS] = WRITE_OP,
	[NFS4_OP_READ_PLUS] = READ_OP,
};

/* latency stats
 */
struct op_latency {
	uint64_t latency;
	uint64_t min;
	uint64_t max;
};

/* v3 ops
 */
struct nfsv3_ops {
	uint64_t op[NFSPROC3_COMMIT+1];
};

/* quota ops
 */
struct qta_ops {
	uint64_t op[RQUOTAPROC_SETACTIVEQUOTA+1];
};

/* nlm ops
 */
struct nlm_ops {
	uint64_t op[NLMPROC4_FREE_ALL+1];
};

/* mount ops
 */
struct mnt_ops {
	uint64_t op[MOUNTPROC3_EXPORT+1];
};

/* v4 ops
 */
struct nfsv4_ops {
	uint64_t op[NFS4_OP_IO_ADVISE+1];
};

/* basic op counter
 */

struct proto_op {
	uint64_t total;		/* total of any kind */
	uint64_t errors;	/* ! NFS_OK */
	uint64_t dups;		/* detected dup requests */
	struct op_latency latency;	/* either executed ops latency */
	struct op_latency dup_latency;	/* or latency (runtime) to replay */
	struct op_latency queue_latency;	/* queue wait time */
};

/* basic I/O transfer counter
 */
struct xfer_op {
	struct proto_op cmd;
	uint64_t requested;
	uint64_t transferred;
};

/* pNFS Layout counters
 */

struct layout_op {
	uint64_t total;		/* total ops */
	uint64_t errors;	/* ! NFS4_OK && !NFS4ERR_DELAY */
	uint64_t delays;	/* NFS4ERR_DELAY */
};

/* NFSv3 statistics counters
 */

struct nfsv3_stats {
	struct proto_op cmds;	/* non-I/O ops = cmds - (read+write) */
	struct xfer_op read;
	struct xfer_op write;
};

/* Mount statistics counters
 */
struct mnt_stats {
	struct proto_op v1_ops;
	struct proto_op v3_ops;
};

/* lock manager counters
 */

struct nlmv4_stats {
	struct proto_op ops;
};

/* Quota counters
 */

struct rquota_stats {
	struct proto_op ops;
	struct proto_op ext_ops;
};

/* NFSv4 statistics counters
 */

struct nfsv40_stats {
	struct proto_op compounds;
	uint64_t ops_per_compound;	/* avg = total / ops_per */
	struct xfer_op read;
	struct xfer_op write;
};

struct nfsv41_stats {
	struct proto_op compounds;
	uint64_t ops_per_compound;	/* for size averaging */
	struct xfer_op read;
	struct xfer_op write;
	struct layout_op getdevinfo;
	struct layout_op layout_get;
	struct layout_op layout_commit;
	struct layout_op layout_return;
	struct layout_op recall;
};

struct _9p_stats {
	struct proto_op cmds;	/* non-I/O ops */
	struct xfer_op read;
	struct xfer_op write;
	struct transport_stats {
		uint64_t rx_bytes;
		uint64_t rx_pkt;
		uint64_t rx_err;
		uint64_t tx_bytes;
		uint64_t tx_pkt;
		uint64_t tx_err;
	} trans;
};

struct global_stats {
	struct nfsv3_stats nfsv3;
	struct mnt_stats mnt;
	struct nlmv4_stats nlm4;
	struct rquota_stats rquota;
	struct nfsv40_stats nfsv40;
	struct nfsv41_stats nfsv41;
	struct nfsv41_stats nfsv42; /* Uses v41 stats */
	struct nfsv3_ops v3;
	struct nfsv4_ops v4;
	struct nlm_ops lm;
	struct mnt_ops mn;
	struct qta_ops qt;
};

static struct global_stats global_st;

struct cache_stats cache_st;
struct cache_stats *cache_stp = &cache_st;

/* include the top level server_stats struct definition
 */
#include "server_stats_private.h"

/**
 * @brief Get stats struct helpers
 *
 * These functions dereference the protocol specific struct
 * silently calloc the struct on first use.
 *
 * @param stats [IN] the stats structure to dereference in
 * @param lock  [IN] the lock in the stats owning struct
 *
 * @return pointer to proto struct, NULL on OOM
 *
 * @TODO make them inlines for release
 */

static struct nfsv3_stats *get_v3(struct gsh_stats *stats,
				  pthread_rwlock_t *lock)
{
	if (unlikely(stats->nfsv3 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nfsv3 == NULL)
			stats->nfsv3 =
			    gsh_calloc(sizeof(struct nfsv3_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nfsv3;
}

static struct mnt_stats *get_mnt(struct gsh_stats *stats,
				 pthread_rwlock_t *lock)
{
	if (unlikely(stats->mnt == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->mnt == NULL)
			stats->mnt = gsh_calloc(sizeof(struct mnt_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->mnt;
}

static struct nlmv4_stats *get_nlm4(struct gsh_stats *stats,
				    pthread_rwlock_t *lock)
{
	if (unlikely(stats->nlm4 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nlm4 == NULL)
			stats->nlm4 = gsh_calloc(sizeof(struct nlmv4_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nlm4;
}

static struct rquota_stats *get_rquota(struct gsh_stats *stats,
				       pthread_rwlock_t *lock)
{
	if (unlikely(stats->rquota == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->rquota == NULL)
			stats->rquota =
			    gsh_calloc(sizeof(struct rquota_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->rquota;
}

static struct nfsv40_stats *get_v40(struct gsh_stats *stats,
				    pthread_rwlock_t *lock)
{
	if (unlikely(stats->nfsv40 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nfsv40 == NULL)
			stats->nfsv40 =
			    gsh_calloc(sizeof(struct nfsv40_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nfsv40;
}

static struct nfsv41_stats *get_v41(struct gsh_stats *stats,
				    pthread_rwlock_t *lock)
{
	if (unlikely(stats->nfsv41 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nfsv41 == NULL)
			stats->nfsv41 =
			    gsh_calloc(sizeof(struct nfsv41_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nfsv41;
}

static struct nfsv41_stats *get_v42(struct gsh_stats *stats,
				    pthread_rwlock_t *lock)
{
	if (unlikely(stats->nfsv42 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nfsv42 == NULL)
			stats->nfsv42 =
			    gsh_calloc(sizeof(struct nfsv41_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nfsv42;
}

#ifdef _USE_9P
static struct _9p_stats *get_9p(struct gsh_stats *stats, pthread_rwlock_t *lock)
{
	if (unlikely(stats->_9p == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->_9p == NULL)
			stats->_9p = gsh_calloc(sizeof(struct _9p_stats), 1);
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->_9p;
}
#endif

/* Functions for recording statistics
 */

/**
 * @brief Record latency stats
 *
 * @param op           [IN] protocol op stats struct
 * @param request_time [IN] time consumed by request
 * @param qwait_time   [IN] time sitting on queue
 * @param dup          [IN] detected this was a dup request
 */
void record_latency(struct proto_op *op, nsecs_elapsed_t request_time,
		    nsecs_elapsed_t qwait_time, bool dup)
{

	/* dup latency is counted separately */
	if (likely(!dup)) {
		(void)atomic_add_uint64_t(&op->latency.latency, request_time);
		if (op->latency.min == 0L || op->latency.min > request_time)
			(void)atomic_store_uint64_t(&op->latency.min,
						    request_time);
		if (op->latency.max == 0L || op->latency.max < request_time)
			(void)atomic_store_uint64_t(&op->latency.max,
						    request_time);
	} else {
		(void)atomic_add_uint64_t(&op->dup_latency.latency,
					  request_time);
		if (op->dup_latency.min == 0L
		    || op->dup_latency.min > request_time)
			(void)atomic_store_uint64_t(&op->dup_latency.min,
						    request_time);
		if (op->dup_latency.max == 0L
		    || op->dup_latency.max < request_time)
			(void)atomic_store_uint64_t(&op->dup_latency.max,
						    request_time);
	}
	/* record how long it was laying around waiting ... */
	(void)atomic_add_uint64_t(&op->queue_latency.latency, qwait_time);
	if (op->queue_latency.min == 0L || op->queue_latency.min > qwait_time)
		(void)atomic_store_uint64_t(&op->queue_latency.min, qwait_time);
	if (op->queue_latency.max == 0L || op->queue_latency.max < qwait_time)
		(void)atomic_store_uint64_t(&op->queue_latency.max, qwait_time);
}

/**
 * @brief count the i/o stats
 *
 * We do the transfer counts here.  Latency is done later at
 * operation/compound completion.
 *
 * @param iop          [IN] transfer stats struct
 * @param requested    [IN] bytes requested
 * @param transferred  [IN] bytes actually transferred
 * @param success      [IN] the op returned OK (or error)
 */

static void record_io(struct xfer_op *iop, size_t requested, size_t transferred,
		      bool success)
{
	(void)atomic_inc_uint64_t(&iop->cmd.total);
	if (success) {
		(void)atomic_add_uint64_t(&iop->requested, requested);
		(void)atomic_add_uint64_t(&iop->transferred, transferred);
	} else {
		(void)atomic_inc_uint64_t(&iop->cmd.errors);
	}
	/* somehow we must record latency */
}

/**
 * @brief record i/o stats by protocol
 */

static void record_io_stats(struct gsh_stats *gsh_st, pthread_rwlock_t *lock,
			    size_t requested,
			    size_t transferred, bool success, bool is_write)
{
	struct xfer_op *iop = NULL;

	if (op_ctx->req_type == NFS_REQUEST) {
		if (op_ctx->nfs_vers == NFS_V3) {
			struct nfsv3_stats *sp = get_v3(gsh_st, lock);

			if (sp == NULL)
				return;
			iop = is_write ? &sp->write : &sp->read;
		} else if (op_ctx->nfs_vers == NFS_V4) {
			if (op_ctx->nfs_minorvers == 0) {
				struct nfsv40_stats *sp = get_v40(gsh_st, lock);

				if (sp == NULL)
					return;
				iop = is_write ? &sp->write : &sp->read;
			} else if (op_ctx->nfs_minorvers == 1) {
				struct nfsv41_stats *sp = get_v41(gsh_st, lock);

				if (sp == NULL)
					return;
				iop = is_write ? &sp->write : &sp->read;
			} else if (op_ctx->nfs_minorvers == 2) {
				struct nfsv41_stats *sp = get_v42(gsh_st, lock);

				if (sp == NULL)
					return;
				iop = is_write ? &sp->write : &sp->read;
			}
			/* the frightening thought is someday minor == 3 */
		} else {
			return;
		}
#ifdef _USE_9P
	} else if (op_ctx->req_type == _9P_REQUEST) {
		struct _9p_stats *sp = get_9p(gsh_st, lock);

		if (sp == NULL)
			return;
		iop = is_write ? &sp->write : &sp->read;
#endif
	} else {
		return;
	}
	record_io(iop, requested, transferred, success);
}

/**
 * @brief count the protocol operation
 *
 * Use atomic ops to avoid locks. We don't lock for the max
 * and min because if there is a collision, over the long haul,
 * the error is near zero...
 *
 * @param op           [IN] pointer to specific protocol struct
 * @param request_time [IN] wallclock time (nsecs) for this op
 * @param qwait_time   [IN] wallclock time (nsecs) waiting for service
 * @param success      [IN] protocol error code == OK
 * @param dup          [IN] true if op was detected duplicate
 */

static void record_op(struct proto_op *op, nsecs_elapsed_t request_time,
		      nsecs_elapsed_t qwait_time, bool success, bool dup)
{
	/* count the op */
	(void)atomic_inc_uint64_t(&op->total);
	/* also count it as an error if protocol not happy */
	if (!success)
		(void)atomic_inc_uint64_t(&op->errors);
	if (unlikely(dup))
		(void)atomic_inc_uint64_t(&op->dups);
	record_latency(op, request_time, qwait_time, dup);
}

/**
 * @brief record V4.1 layout op stats
 *
 * @param sp       [IN] stats block to update
 * @param proto_op [IN] protocol op
 * @param status   [IN] operation status
 */

static void record_layout(struct nfsv41_stats *sp, int proto_op, int status)
{
	struct layout_op *lp;

	if (proto_op == NFS4_OP_GETDEVICEINFO)
		lp = &sp->getdevinfo;
	else if (proto_op == NFS4_OP_GETDEVICELIST)
		lp = &sp->getdevinfo;
	else if (proto_op == NFS4_OP_LAYOUTGET)
		lp = &sp->layout_get;
	else if (proto_op == NFS4_OP_LAYOUTCOMMIT)
		lp = &sp->layout_commit;
	else if (proto_op == NFS4_OP_LAYOUTRETURN)
		lp = &sp->layout_return;
	else
		return;
	(void)atomic_inc_uint64_t(&lp->total);
	if (status == NFS4ERR_DELAY)
		(void)atomic_inc_uint64_t(&lp->delays);
	else if (status != NFS4_OK)
		(void)atomic_inc_uint64_t(&lp->errors);
}

/**
 * @brief Record NFS V4 compound stats
 */

static void record_nfsv4_op(struct gsh_stats *gsh_st, pthread_rwlock_t *lock,
			    int proto_op, int minorversion,
			    nsecs_elapsed_t request_time,
			    nsecs_elapsed_t qwait_time, int status)
{
	if (minorversion == 0) {
		struct nfsv40_stats *sp = get_v40(gsh_st, lock);

		if (sp == NULL)
			return;
		/* record stuff */
		switch (nfsv40_optype[proto_op]) {
		case READ_OP:
			record_latency(&sp->read.cmd, request_time, qwait_time,
				       false);
			break;
		case WRITE_OP:
			record_latency(&sp->write.cmd, request_time, qwait_time,
				       false);
			break;
		default:
			record_op(&sp->compounds, request_time, qwait_time,
				  status == NFS4_OK, false);
		}
	} else if (minorversion == 1) {
		struct nfsv41_stats *sp = get_v41(gsh_st, lock);

		if (sp == NULL)
			return;
		/* record stuff */
		switch (nfsv41_optype[proto_op]) {
		case READ_OP:
			record_latency(&sp->read.cmd, request_time, qwait_time,
				       false);
			break;
		case WRITE_OP:
			record_latency(&sp->write.cmd, request_time, qwait_time,
				       false);
			break;
		case LAYOUT_OP:
			record_layout(sp, proto_op, status);
			break;
		default:
			record_op(&sp->compounds, request_time, qwait_time,
				  status == NFS4_OK, false);
		}
	} else if (minorversion == 2) {
		struct nfsv41_stats *sp = get_v42(gsh_st, lock);

		if (sp == NULL)
			return;
		/* record stuff */
		switch (nfsv42_optype[proto_op]) {
		case READ_OP:
			record_latency(&sp->read.cmd, request_time, qwait_time,
				       false);
			break;
		case WRITE_OP:
			record_latency(&sp->write.cmd, request_time, qwait_time,
				       false);
			break;
		case LAYOUT_OP:
			record_layout(sp, proto_op, status);
			break;
		default:
			record_op(&sp->compounds, request_time, qwait_time,
				  status == NFS4_OK, false);
		}
	}

}

/**
 * @brief Record NFS V4 compound stats
 */

static void record_compound(struct gsh_stats *gsh_st, pthread_rwlock_t *lock,
			    int minorversion, uint64_t num_ops,
			    nsecs_elapsed_t request_time,
			    nsecs_elapsed_t qwait_time, bool success)
{
	if (minorversion == 0) {

		struct nfsv40_stats *sp = get_v40(gsh_st, lock);

		if (sp == NULL)
			return;
		/* record stuff */
		record_op(&sp->compounds, request_time, qwait_time, success,
			  false);
		(void)atomic_add_uint64_t(&sp->ops_per_compound, num_ops);
	} else if (minorversion == 1) {
		struct nfsv41_stats *sp = get_v41(gsh_st, lock);

		if (sp == NULL)
			return;
		/* record stuff */
		record_op(&sp->compounds, request_time, qwait_time, success,
			  false);
		(void)atomic_add_uint64_t(&sp->ops_per_compound, num_ops);
	} else if (minorversion == 2) {
		struct nfsv41_stats *sp = get_v42(gsh_st, lock);

		if (sp == NULL)
			return;
		/* record stuff */
		record_op(&sp->compounds, request_time, qwait_time, success,
			  false);
		(void)atomic_add_uint64_t(&sp->ops_per_compound, num_ops);
	}

}

/**
 * @brief Record request statistics (V3 era protos only)
 *
 * Decode the protocol and find the proto specific stats struct.
 * Once we found the stats block, do the update(s).
 *
 * @param gsh_st       [IN] stats struct from client or export
 * @param lock         [IN] lock on client|export for malloc
 * @param reqdata      [IN] info about the proto request
 * @param success      [IN] the op returned OK (or error)
 * @param request_time [IN] time consumed by request
 * @param qwait_time   [IN] time sitting on queue
 * @param dup          [IN] detected this was a dup request
 */

static void record_stats(struct gsh_stats *gsh_st, pthread_rwlock_t *lock,
			 request_data_t *reqdata, bool success,
			 nsecs_elapsed_t request_time,
			 nsecs_elapsed_t qwait_time, bool dup, bool global)
{
	struct svc_req *req = &reqdata->r_u.nfs->req;
	uint32_t proto_op = req->rq_proc;

	if (req->rq_prog == nfs_param.core_param.program[P_NFS]) {
		if (proto_op == 0)
			return;	/* we don't count NULL ops */
		if (req->rq_vers == NFS_V3) {
			struct nfsv3_stats *sp = get_v3(gsh_st, lock);

			if (sp == NULL)
				return;
			/* record stuff */
			if (global)
				record_op(&global_st.nfsv3.cmds, request_time,
					  qwait_time, success, dup);
			switch (nfsv3_optype[proto_op]) {
			case READ_OP:
				record_latency(&sp->read.cmd, request_time,
					       qwait_time, dup);
				break;
			case WRITE_OP:
				record_latency(&sp->write.cmd, request_time,
					       qwait_time, dup);
				break;
			default:
				record_op(&sp->cmds, request_time, qwait_time,
					  success, dup);
			}
		} else {
			/* We don't do V4 here and V2 is toast */
			return;
		}
	} else if (req->rq_prog == nfs_param.core_param.program[P_MNT]) {
		struct mnt_stats *sp = get_mnt(gsh_st, lock);

		if (global && req->rq_vers == MOUNT_V1)
			record_op(&global_st.mnt.v1_ops, request_time,
				  qwait_time, success, dup);
		else if (global)
			record_op(&global_st.mnt.v3_ops, request_time,
				  qwait_time, success, dup);

		if (sp == NULL)
			return;
		/* record stuff */
		if (req->rq_vers == MOUNT_V1)
			record_op(&sp->v1_ops, request_time, qwait_time,
				  success, dup);
		else
			record_op(&sp->v3_ops, request_time, qwait_time,
				  success, dup);
	} else if (req->rq_prog == nfs_param.core_param.program[P_NLM]) {
		struct nlmv4_stats *sp = get_nlm4(gsh_st, lock);

		if (global)
			record_op(&global_st.nlm4.ops, request_time,
				  qwait_time, success, dup);
		if (sp == NULL)
			return;
		/* record stuff */
		record_op(&sp->ops, request_time, qwait_time, success, dup);
	} else if (req->rq_prog == nfs_param.core_param.program[P_RQUOTA]) {
		struct rquota_stats *sp = get_rquota(gsh_st, lock);

		if (global)
			record_op(&global_st.rquota.ops, request_time,
				  qwait_time, success, dup);
		if (sp == NULL)
			return;
		/* record stuff */
		if (req->rq_vers == RQUOTAVERS)
			record_op(&sp->ops, request_time, qwait_time, success,
				  dup);
		else
			record_op(&sp->ext_ops, request_time, qwait_time,
				  success, dup);
	}
}

/**
 * @brief Record transport stats
 *
 */
#ifdef _USE_9P
static void record_transport_stats(struct transport_stats *t_st,
				   uint64_t rx_bytes, uint64_t rx_pkt,
				   uint64_t rx_err, uint64_t tx_bytes,
				   uint64_t tx_pkt, uint64_t tx_err)
{
	if (rx_bytes)
		atomic_add_uint64_t(&t_st->rx_bytes, rx_bytes);
	if (rx_pkt)
		atomic_add_uint64_t(&t_st->rx_pkt, rx_pkt);
	if (rx_err)
		atomic_add_uint64_t(&t_st->rx_err, rx_err);
	if (tx_bytes)
		atomic_add_uint64_t(&t_st->tx_bytes, tx_bytes);
	if (tx_pkt)
		atomic_add_uint64_t(&t_st->tx_pkt, tx_pkt);
	if (tx_err)
		atomic_add_uint64_t(&t_st->tx_err, tx_err);
}
#endif
/**
 * @brief record 9P tcp transport stats
 *
 * Called from 9P functions doing send/recv
 */
#ifdef _USE_9P
void server_stats_transport_done(struct gsh_client *client,
				uint64_t rx_bytes, uint64_t rx_pkt,
				uint64_t rx_err, uint64_t tx_bytes,
				uint64_t tx_pkt, uint64_t tx_err)
{
	struct server_stats *server_st =
		container_of(client, struct server_stats, client);
	struct _9p_stats *sp = get_9p(&server_st->st, &client->lock);

	if (sp != NULL)
		record_transport_stats(&sp->trans, rx_bytes, rx_pkt, rx_err,
				       tx_bytes, tx_pkt, tx_err);
}
#endif

/**
 * @brief record NFS op finished
 *
 * Called from nfs_rpc_execute at operation/command completion
 */

void server_stats_nfs_done(request_data_t *reqdata, int rc, bool dup)
{
	struct gsh_client *client = op_ctx->client;
	struct timespec current_time;
	nsecs_elapsed_t stop_time;
	struct svc_req *req = &reqdata->r_u.nfs->req;
	uint32_t proto_op = req->rq_proc;

	if (req->rq_prog == NFS_PROGRAM && op_ctx->nfs_vers == NFS_V3)
			global_st.v3.op[proto_op]++;
	else if (req->rq_prog == nfs_param.core_param.program[P_NLM])
		global_st.lm.op[proto_op]++;
	else if (req->rq_prog == nfs_param.core_param.program[P_MNT])
		global_st.mn.op[proto_op]++;
	else if (req->rq_prog == nfs_param.core_param.program[P_RQUOTA])
		global_st.qt.op[proto_op]++;

	if (nfs_param.core_param.enable_FASTSTATS)
		return;

	now(&current_time);
	stop_time = timespec_diff(&ServerBootTime, &current_time);
	if (client != NULL) {
		struct server_stats *server_st;
		server_st = container_of(client, struct server_stats, client);
		record_stats(&server_st->st, &client->lock, reqdata,
			     stop_time - op_ctx->start_time,
			     op_ctx->queue_wait,
			     rc == NFS_REQ_OK, dup, true);
		(void)atomic_store_uint64_t(&client->last_update, stop_time);
	}
	if (!dup && op_ctx->export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->export, struct export_stats, export);
		record_stats(&exp_st->st, &op_ctx->export->lock, reqdata,
			     stop_time - op_ctx->start_time,
			     op_ctx->queue_wait, rc == NFS_REQ_OK, dup, false);
		(void)atomic_store_uint64_t(&op_ctx->export->last_update,
					    stop_time);
	}
	return;
}

/**
 * @brief record NFS V4 compound finished
 *
 * Called from nfs4_compound at compound loop completion
 */

void server_stats_nfsv4_op_done(int proto_op,
				nsecs_elapsed_t start_time, int status)
{
	struct gsh_client *client = op_ctx->client;
	struct timespec current_time;
	nsecs_elapsed_t stop_time;

	if (op_ctx->nfs_vers == NFS_V4)
		global_st.v4.op[proto_op]++;

	if (nfs_param.core_param.enable_FASTSTATS)
		return;

	now(&current_time);
	stop_time = timespec_diff(&ServerBootTime, &current_time);

	if (client != NULL) {
		struct server_stats *server_st;
		server_st = container_of(client, struct server_stats, client);
		record_nfsv4_op(&server_st->st, &client->lock, proto_op,
				op_ctx->nfs_minorvers, stop_time - start_time,
				op_ctx->queue_wait, status);
		(void)atomic_store_uint64_t(&client->last_update, stop_time);
	}

	if (op_ctx->nfs_minorvers == 0)
		record_op(&global_st.nfsv40.compounds, stop_time - start_time,
			  op_ctx->queue_wait, status == NFS4_OK, false);
	else if (op_ctx->nfs_minorvers == 1)
		record_op(&global_st.nfsv41.compounds, stop_time - start_time,
			  op_ctx->queue_wait, status == NFS4_OK, false);
	else if (op_ctx->nfs_minorvers == 2)
		record_op(&global_st.nfsv42.compounds, stop_time - start_time,
			  op_ctx->queue_wait, status == NFS4_OK, false);

	if (op_ctx->export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->export, struct export_stats, export);
		record_nfsv4_op(&exp_st->st, &op_ctx->export->lock, proto_op,
				op_ctx->nfs_minorvers, stop_time - start_time,
				op_ctx->queue_wait, status);
		(void)atomic_store_uint64_t(&op_ctx->export->last_update,
					    stop_time);
	}
	return;
}

/**
 * @brief record NFS V4 compound finished
 *
 * Called from nfs4_compound at compound loop completion
 */

void server_stats_compound_done(int num_ops, int status)
{
	struct gsh_client *client = op_ctx->client;
	struct timespec current_time;
	nsecs_elapsed_t stop_time;

	now(&current_time);
	stop_time = timespec_diff(&ServerBootTime, &current_time);
	if (client != NULL) {
		struct server_stats *server_st;
		server_st = container_of(client, struct server_stats, client);
		record_compound(&server_st->st, &client->lock,
				op_ctx->nfs_minorvers,
				num_ops, stop_time - op_ctx->start_time,
				op_ctx->queue_wait, status == NFS4_OK);
		(void)atomic_store_uint64_t(&client->last_update, stop_time);
	}
	if (op_ctx->export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->export, struct export_stats, export);
		record_compound(&exp_st->st, &op_ctx->export->lock,
				op_ctx->nfs_minorvers, num_ops,
				stop_time - op_ctx->start_time,
				op_ctx->queue_wait, status == NFS4_OK);
		(void)atomic_store_uint64_t(&op_ctx->export->last_update,
					    stop_time);
	}
	return;
}

/**
 * @brief Record I/O stats for protocol read/write
 *
 * Called from protocol operation/command handlers to record
 * transfers
 */

void server_stats_io_done(size_t requested,
			  size_t transferred, bool success, bool is_write)
{
	if (op_ctx->client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(op_ctx->client, struct server_stats,
					 client);
		record_io_stats(&server_st->st, &op_ctx->client->lock,
				requested, transferred, success,
				is_write);
	}
	if (op_ctx->export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->export, struct export_stats, export);
		record_io_stats(&exp_st->st, &op_ctx->export->lock,
				requested, transferred, success, is_write);
	}
	return;
}

#ifdef USE_DBUS

/* Functions for marshalling statistics to DBUS
 */

/**
 * @brief Report Stats availability as members of a struct
 *
 * struct available_stats {
 *      ...
 *	bool nfsv3;
 *	bool mnt;
 *	bool nlm4;
 *	bool rquota;
 *	bool nfsv40;
 *	bool nfsv41;
 *	bool nfsv42;
 *	bool _9p;
 *      ...
 * }
 *
 * @param name        [IN] name of export or IP address as string
 * @param stats       [IN] pointer to server stats struct
 * @param last_update [IN] elapsed timestamp of last activity
 * @param iter        [IN] iterator to stuff struct into
 */

void server_stats_summary(DBusMessageIter *iter, struct gsh_stats *st)
{
	int stats_available;

	stats_available = st->nfsv3 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = st->mnt != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = st->nlm4 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = st->rquota != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = st->nfsv40 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = st->nfsv41 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = st->nfsv42 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = st->_9p != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
}

/**
 * @brief Report I/O statistics as a struct
 *
 * struct iostats {
 *       uint64_t bytes_requested;
 *       uint64_t bytes_transferred;
 *       uint64_t total_ops;
 *       uint64_t errors;
 *       uint64_t latency;
 *       uint64_t queue_wait;
 * }
 *
 * @param iop   [IN] pointer to xfer op sub-structure of interest
 * @param iter  [IN] interator in reply stream to fill
 */

static void server_dbus_iostats(struct xfer_op *iop, DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->requested);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->transferred);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->cmd.total);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->cmd.errors);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->cmd.latency.latency);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->cmd.queue_latency.latency);
	dbus_message_iter_close_container(iter, &struct_iter);
}

static void server_dbus_transportstats(struct transport_stats *tstats,
				       DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &tstats->rx_bytes);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &tstats->rx_pkt);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &tstats->rx_err);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &tstats->tx_bytes);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &tstats->tx_pkt);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &tstats->tx_err);
	dbus_message_iter_close_container(iter, &struct_iter);
}

void server_dbus_total(struct export_stats *export_st, DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;
	uint64_t total = 0;
	char *version;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);

	version = "NFSv3";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	if (export_st->st.nfsv3 == NULL)
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&total);
	else
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&export_st->st.nfsv3->cmds.total);
	version = "NFSv40";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	if (export_st->st.nfsv40 == NULL)
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&total);
	else
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&export_st->st.nfsv40->compounds.total);
	version = "NFSv41";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	if (export_st->st.nfsv41 == NULL)
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&total);
	else
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&export_st->st.nfsv41->compounds.total);
	version = "NFSv42";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	if (export_st->st.nfsv42 == NULL)
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&total);
	else
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&export_st->st.nfsv42->compounds.total);
	dbus_message_iter_close_container(iter, &struct_iter);
}

void global_dbus_total(DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;
	char *version;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);

	version = "NFSv3";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.nfsv3.cmds.total);
	version = "NFSv40";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.nfsv40.compounds.total);
	version = "NFSv41";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.nfsv41.compounds.total);
	version = "NFSv42";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.nfsv42.compounds.total);
	version = "NLM4";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.nlm4.ops.total);
	version = "MNTv1";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.mnt.v1_ops.total);
	version = "MNTv3";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.mnt.v3_ops.total);
	version = "RQUOTA";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.rquota.ops.total);
	dbus_message_iter_close_container(iter, &struct_iter);
}

void global_dbus_fast(DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;
	char *version;
	char *op;
	int i;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);

	version = "NFSv3:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i < NFSPROC3_COMMIT; i++) {
		if (global_st.v3.op[i] > 0) {
			op = optabv3[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.v3.op[i]);
		}
	}
	version = "\nNFSv4:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i < NFS4_OP_IO_ADVISE; i++) {
		if (global_st.v4.op[i] > 0) {
			op = optabv4[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.v4.op[i]);
		}
	}
	version = "\nNLM:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i < NLM4_FAILED; i++) {
		if (global_st.lm.op[i] > 0) {
			op = optnlm[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.lm.op[i]);
		}
	}
	version = "\nMNT:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i < MOUNTPROC3_EXPORT; i++) {
		if (global_st.mn.op[i] > 0) {
			op = optmnt[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.mn.op[i]);
		}
	}
	version = "\nQUOTA:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i < RQUOTAPROC_SETACTIVEQUOTA; i++) {
		if (global_st.qt.op[i] > 0) {
			op = optqta[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.qt.op[i]);
		}
	}
	dbus_message_iter_close_container(iter, &struct_iter);
}

void server_dbus_v3_iostats(struct nfsv3_stats *v3p, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_iostats(&v3p->read, iter);
	server_dbus_iostats(&v3p->write, iter);
}

void server_dbus_v40_iostats(struct nfsv40_stats *v40p, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_iostats(&v40p->read, iter);
	server_dbus_iostats(&v40p->write, iter);
}

void server_dbus_v41_iostats(struct nfsv41_stats *v41p, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_iostats(&v41p->read, iter);
	server_dbus_iostats(&v41p->write, iter);
}

void server_dbus_v42_iostats(struct nfsv41_stats *v42p, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_iostats(&v42p->read, iter);
	server_dbus_iostats(&v42p->write, iter);
}

void server_dbus_total_ops(struct export_stats *export_st,
			   DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_total(export_st, iter);
}

void server_dbus_fast_ops(DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	global_dbus_fast(iter);
}

void global_dbus_total_ops(DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	global_dbus_total(iter);
}

void cache_inode_dbus_show(DBusMessageIter *iter)
{
	struct timespec timestamp;
	DBusMessageIter struct_iter;
	char *type;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	type = "cache_req";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_req);
	type = "cache_hit";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_hit);
	type = "cache_miss";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_miss);
	type = "cache_conf";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_conf);
	type = "cache_added";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_added);
	type = "cache_mapping";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&cache_st.inode_mapping);

	dbus_message_iter_close_container(iter, &struct_iter);
}

void server_dbus_9p_iostats(struct _9p_stats *_9pp, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_iostats(&_9pp->read, iter);
	server_dbus_iostats(&_9pp->write, iter);
}

void server_dbus_9p_transstats(struct _9p_stats *_9pp, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_transportstats(&_9pp->trans, iter);
}

/**
 * @brief Report layout statistics as a struct
 *
 * struct layout {
 *       uint64_t total_layouts;
 *       uint64_t errors;
 *       uint64_t delays;
 * }
 *
 * @param iop   [IN] pointer to xfer op sub-structure of interest
 * @param iter  [IN] interator in reply stream to fill
 */

static void server_dbus_layouts(struct layout_op *lop, DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &lop->total);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &lop->errors);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &lop->delays);
	dbus_message_iter_close_container(iter, &struct_iter);
}

void server_dbus_v41_layouts(struct nfsv41_stats *v41p, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_layouts(&v41p->getdevinfo, iter);
	server_dbus_layouts(&v41p->layout_get, iter);
	server_dbus_layouts(&v41p->layout_commit, iter);
	server_dbus_layouts(&v41p->layout_return, iter);
	server_dbus_layouts(&v41p->recall, iter);
}

void server_dbus_v42_layouts(struct nfsv41_stats *v42p, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	server_dbus_layouts(&v42p->getdevinfo, iter);
	server_dbus_layouts(&v42p->layout_get, iter);
	server_dbus_layouts(&v42p->layout_commit, iter);
	server_dbus_layouts(&v42p->layout_return, iter);
	server_dbus_layouts(&v42p->recall, iter);
}

#endif				/* USE_DBUS */

/**
 * @brief Free statistics storage
 *
 * The struct itself is not freed because it is a member
 * of either the client manager struct or the export struct.
 *
 * @param statsp [IN] pointer to stats to be cleaned
 */

void server_stats_free(struct gsh_stats *statsp)
{
	if (statsp->nfsv3 != NULL) {
		gsh_free(statsp->nfsv3);
		statsp->nfsv3 = NULL;
	}
	if (statsp->mnt != NULL) {
		gsh_free(statsp->mnt);
		statsp->mnt = NULL;
	}
	if (statsp->nlm4 != NULL) {
		gsh_free(statsp->nlm4);
		statsp->nlm4 = NULL;
	}
	if (statsp->rquota != NULL) {
		gsh_free(statsp->rquota);
		statsp->rquota = NULL;
	}
	if (statsp->nfsv40 != NULL) {
		gsh_free(statsp->nfsv40);
		statsp->nfsv40 = NULL;
	}
	if (statsp->nfsv41 != NULL) {
		gsh_free(statsp->nfsv41);
		statsp->nfsv41 = NULL;
	}
	if (statsp->nfsv42 != NULL) {
		gsh_free(statsp->nfsv42);
		statsp->nfsv42 = NULL;
	}
	if (statsp->_9p != NULL) {
		gsh_free(statsp->_9p);
		statsp->_9p = NULL;
	}
}

/** @} */
