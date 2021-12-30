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
#include "gsh_types.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "client_mgr.h"
#include "export_mgr.h"
#include "server_stats.h"
#include <abstract_atomic.h>
#include "nfs_proto_functions.h"

#define NFS_V3_NB_COMMAND (NFSPROC3_COMMIT + 1)
#define NFS_V4_NB_COMMAND 2
#define MNT_V1_NB_COMMAND (MOUNTPROC3_EXPORT + 1)
#define MNT_V3_NB_COMMAND (MOUNTPROC3_EXPORT + 1)
#define NLM_V4_NB_OPERATION (NLMPROC4_FREE_ALL + 1)
#define RQUOTA_NB_COMMAND (RQUOTAPROC_SETACTIVEQUOTA + 1)
#define NFS_V40_NB_OPERATION (NFS4_OP_RELEASE_LOCKOWNER + 1)
#define NFS_V41_NB_OPERATION (NFS4_OP_RECLAIM_COMPLETE + 1)
#define NFS_V42_NB_OPERATION (NFS4_OP_WRITE_SAME + 1)
#define _9P_NB_COMMAND 33

#define NFS_pcp nfs_param.core_param
#define NFS_program NFS_pcp.program

#ifdef USE_DBUS

struct op_name {
	char *name;
};

#ifdef _USE_RQUOTA
static const struct op_name optqta[] = {
	[RQUOTAPROC_GETQUOTA] = {.name = "GETQUOTA", },
	[RQUOTAPROC_GETACTIVEQUOTA] = {.name = "GETACTIVEQUOTA", },
	[RQUOTAPROC_SETQUOTA] = {.name = "SETQUOTA", },
	[RQUOTAPROC_SETACTIVEQUOTA] = {.name = "SETACTIVEQUOTA", },
};
#endif

#ifdef _USE_NFS3
static const struct op_name optmnt[] = {
	[MOUNTPROC3_NULL] = {.name = "NULL", },
	[MOUNTPROC3_MNT] = {.name = "MNT", },
	[MOUNTPROC3_DUMP] = {.name = "DUMP", },
	[MOUNTPROC3_UMNT] = {.name = "UMNT", },
	[MOUNTPROC3_UMNTALL] = {.name = "UMNTALL", },
	[MOUNTPROC3_EXPORT] = {.name = "EXPORT", },
};
#endif

#ifdef _USE_NLM
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
#endif

#ifdef _USE_NFS3
static const struct op_name optabv3[] = {
	[NFSPROC3_NULL] = {.name = "NULL", },
	[NFSPROC3_GETATTR] = {.name = "GETATTR", },
	[NFSPROC3_SETATTR] = {.name = "SETATTR", },
	[NFSPROC3_LOOKUP] = {.name = "LOOKUP", },
	[NFSPROC3_ACCESS] = {.name = "ACCESS", },
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
	[NFSPROC3_FSINFO] = {.name = "FSINFO", },
	[NFSPROC3_PATHCONF] = {.name = "PATHCONF", },
	[NFSPROC3_COMMIT] = {.name = "COMMIT", },
};
#endif

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
	[NFS4_OP_ALLOCATE] = {.name = "ALLOCATE",},
	[NFS4_OP_COPY] = {.name = "COPY",},
	[NFS4_OP_COPY_NOTIFY] = {.name = "COPY_NOTIFY",},
	[NFS4_OP_DEALLOCATE] = {.name = "DEALLOCATE",},
	[NFS4_OP_IO_ADVISE] = {.name = "IO_ADVISE",},
	[NFS4_OP_LAYOUTERROR] = {.name = "LAYOUTERROR",},
	[NFS4_OP_OFFLOAD_CANCEL] = {.name = "OFFLOAD_CANCEL",},
	[NFS4_OP_OFFLOAD_STATUS] = {.name = "OFFLOAD_STATUS",},
	[NFS4_OP_READ_PLUS] = {.name = "READ_PLUS",},
	[NFS4_OP_SEEK] = {.name = "SEEK",},
	[NFS4_OP_WRITE_SAME] = {.name = "WRITE_SAME",},
	[NFS4_OP_CLONE] = {.name = "OP_CLONE",},
	/* NFSv4.3 */
	[NFS4_OP_GETXATTR] = {.name = "OP_GETXATTR",},
	[NFS4_OP_SETXATTR] = {.name = "OP_SETXATTR",},
	[NFS4_OP_LISTXATTR] = {.name = "OP_LISTXATTR",},
	[NFS4_OP_REMOVEXATTR] = {.name = "OP_REMOVEXATTR",},
};

#endif

/* Classify protocol ops for stats purposes
 */

enum proto_op_type {
	GENERAL_OP = 0,		/* default for array init */
	READ_OP,
	WRITE_OP,
	LAYOUT_OP
};

#ifdef _USE_NFS3
static const uint32_t nfsv3_optype[NFS_V3_NB_COMMAND] = {
	[NFSPROC3_READ] = READ_OP,
	[NFSPROC3_WRITE] = WRITE_OP,
};
#endif

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
	[NFS4_OP_WRITE_SAME] = WRITE_OP,
	[NFS4_OP_READ_PLUS] = READ_OP,
};

/* latency stats
 */
struct op_latency {
	uint64_t latency;
	uint64_t min;
	uint64_t max;
};

#ifdef _USE_NFS3
/* v3 ops
 */
struct nfsv3_ops {
	uint64_t op[NFSPROC3_COMMIT+1];
};
#endif

#ifdef _USE_RQUOTA
/* quota ops
 */
struct qta_ops {
	uint64_t op[RQUOTAPROC_SETACTIVEQUOTA+1];
};
#endif

#ifdef _USE_NLM
/* nlm ops
 */
struct nlm_ops {
	uint64_t op[NLMPROC4_FREE_ALL+1];
};
#endif

#ifdef _USE_NFS3
/* mount ops
 */
struct mnt_ops {
	uint64_t op[MOUNTPROC3_EXPORT+1];
};
#endif

/* v4 ops
 */
struct nfsv4_ops {
	uint64_t op[NFS4_OP_LAST_ONE];
};

/* basic op counter
 */
struct op_count {
	uint64_t total;		/* total of any kind */
	uint64_t errors;	/* ! NFS_OK */
	uint64_t dups;		/* detected dup requests */
};

struct proto_op {
	uint64_t total;		/* total of any kind */
	uint64_t errors;	/* ! NFS_OK */
	uint64_t dups;		/* detected dup requests */
	struct op_latency latency;	/* either executed ops latency */
	struct op_latency dup_latency;	/* or latency (runtime) to replay */
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

#ifdef _USE_NFS3
/* NFSv3 statistics counters
 */

struct nfsv3_stats {
	struct proto_op cmds;	/* non-I/O ops = cmds - (read+write) */
	struct xfer_op read;
	struct xfer_op write;
};

struct clnt_allops_v3_stats {
	struct op_count cmds[NFSPROC3_COMMIT + 1];	/* all NFSv3 ops */
};

/* Mount statistics counters
 */
struct mnt_stats {
	struct proto_op v1_ops;
	struct proto_op v3_ops;
};
#endif

#ifdef _USE_NLM
/* lock manager counters
 */

struct nlmv4_stats {
	struct proto_op ops;
};

struct clnt_allops_nlm_stats {
	struct op_count cmds[NLMPROC4_FREE_ALL + 1];	/* all NLMv4 ops */
};
#endif

#ifdef _USE_RQUOTA
/* Quota counters
 */

struct rquota_stats {
	struct proto_op ops;
	struct proto_op ext_ops;
};
#endif

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

struct clnt_allops_v4_stats {
	struct op_count cmds[NFS4_OP_LAST_ONE];	/* all ops for NFSv4.x */
};

struct transport_stats {
	uint64_t rx_bytes;
	uint64_t rx_pkt;
	uint64_t rx_err;
	uint64_t tx_bytes;
	uint64_t tx_pkt;
	uint64_t tx_err;
};

#ifdef _USE_9P
struct _9p_stats {
	struct proto_op cmds;	/* non-I/O ops */
	struct xfer_op read;
	struct xfer_op write;
	struct transport_stats trans;
	struct proto_op *opcodes[_9P_RWSTAT+1];
};
#endif

struct global_stats {
#ifdef _USE_NFS3
	struct nfsv3_stats nfsv3;
	struct mnt_stats mnt;
#endif
#ifdef _USE_NLM
	struct nlmv4_stats nlm4;
#endif
#ifdef _USE_RQUOTA
	struct rquota_stats rquota;
#endif
	struct nfsv40_stats nfsv40;
	struct nfsv41_stats nfsv41;
	struct nfsv41_stats nfsv42; /* Uses v41 stats */
#ifdef _USE_NFS3
	struct nfsv3_ops v3;
#endif
	struct nfsv4_ops v4;
#ifdef _USE_NLM
	struct nlm_ops lm;
#endif
#ifdef _USE_NFS3
	struct mnt_ops mn;
#endif
#ifdef _USE_RQUOTA
	struct qta_ops qt;
#endif
};

struct deleg_stats {
	uint32_t curr_deleg_grants; /* current num of delegations owned by
				       this client */
	uint32_t tot_recalls;       /* total num of times client was asked to
				       recall */
	uint32_t failed_recalls;    /* times client failed to process recall */
	uint32_t num_revokes;	    /* Num revokes for the client */
};

static struct global_stats global_st;

/* include the top level server_stats struct definition
 */
#include "server_stats_private.h"

#ifdef _USE_NFS3
/* NFSv3 Detailed stats holder */
struct proto_op v3_full_stats[NFSPROC3_COMMIT+1];
#endif

/* NFSv4 Detailed stats holder */
struct proto_op v4_full_stats[NFS_V42_NB_OPERATION+1];

/**
 * @brief Get stats struct helpers
 *
 * These functions dereference the protocol specific struct
 * silently calloc the struct on first use.
 *
 * @param stats [IN] the stats structure to dereference in
 * @param lock  [IN] the lock in the stats owning struct
 *
 * @return pointer to proto struct
 *
 * @TODO make them inlines for release
 */

#ifdef _USE_NFS3
static struct nfsv3_stats *get_v3(struct gsh_stats *stats,
				  pthread_rwlock_t *lock)
{
	if (unlikely(stats->nfsv3 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nfsv3 == NULL)
			stats->nfsv3 =
			    gsh_calloc(1, sizeof(struct nfsv3_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nfsv3;
}

static struct clnt_allops_v3_stats *get_v3_all(struct gsh_clnt_allops_stats *st,
				  pthread_rwlock_t *lock)
{
	if (unlikely(st->nfsv3 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (st->nfsv3 == NULL)
			st->nfsv3 =
			    gsh_calloc(1, sizeof(struct clnt_allops_v3_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return st->nfsv3;
}

static struct mnt_stats *get_mnt(struct gsh_stats *stats,
				 pthread_rwlock_t *lock)
{
	if (unlikely(stats->mnt == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->mnt == NULL)
			stats->mnt = gsh_calloc(1, sizeof(struct mnt_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->mnt;
}
#endif

#ifdef _USE_NLM
static struct nlmv4_stats *get_nlm4(struct gsh_stats *stats,
				    pthread_rwlock_t *lock)
{
	if (unlikely(stats->nlm4 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nlm4 == NULL)
			stats->nlm4 = gsh_calloc(1, sizeof(struct nlmv4_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nlm4;
}

static struct clnt_allops_nlm_stats *get_nlm4_all(
		struct gsh_clnt_allops_stats *stats,
		pthread_rwlock_t *lock)
{
	if (unlikely(stats->nlm4 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nlm4 == NULL)
			stats->nlm4 = gsh_calloc(1,
				sizeof(struct clnt_allops_nlm_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nlm4;
}
#endif

#ifdef _USE_RQUOTA
static struct rquota_stats *get_rquota(struct gsh_stats *stats,
				       pthread_rwlock_t *lock)
{
	if (unlikely(stats->rquota == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->rquota == NULL)
			stats->rquota =
			    gsh_calloc(1, sizeof(struct rquota_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->rquota;
}
#endif

static struct nfsv40_stats *get_v40(struct gsh_stats *stats,
				    pthread_rwlock_t *lock)
{
	if (unlikely(stats->nfsv40 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nfsv40 == NULL)
			stats->nfsv40 =
			    gsh_calloc(1, sizeof(struct nfsv40_stats));
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
			    gsh_calloc(1, sizeof(struct nfsv41_stats));
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
			    gsh_calloc(1, sizeof(struct nfsv41_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nfsv42;
}

static struct clnt_allops_v4_stats *get_v4_all(
				struct gsh_clnt_allops_stats *stats,
				pthread_rwlock_t *lock)
{
	if (unlikely(stats->nfsv4 == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->nfsv4 == NULL)
			stats->nfsv4 =
			    gsh_calloc(1, sizeof(struct clnt_allops_v4_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
	return stats->nfsv4;
}

#ifdef _USE_9P
static struct _9p_stats *get_9p(struct gsh_stats *stats, pthread_rwlock_t *lock)
{
	if (unlikely(stats->_9p == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->_9p == NULL)
			stats->_9p = gsh_calloc(1, sizeof(struct _9p_stats));
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
 * @param dup          [IN] detected this was a dup request
 */
void record_latency(struct proto_op *op, nsecs_elapsed_t request_time, bool dup)
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
		if (op_ctx->nfs_vers == NFS_V4) {
			if (op_ctx->nfs_minorvers == 0) {
				struct nfsv40_stats *sp = get_v40(gsh_st, lock);

				iop = is_write ? &sp->write : &sp->read;
			} else if (op_ctx->nfs_minorvers == 1) {
				struct nfsv41_stats *sp = get_v41(gsh_st, lock);

				iop = is_write ? &sp->write : &sp->read;
			} else if (op_ctx->nfs_minorvers == 2) {
				struct nfsv41_stats *sp = get_v42(gsh_st, lock);

				iop = is_write ? &sp->write : &sp->read;
			}
			/* the frightening thought is someday minor == 3 */
#ifdef _USE_NFS3
		} else if (op_ctx->nfs_vers == NFS_V3) {
			struct nfsv3_stats *sp = get_v3(gsh_st, lock);

			iop = is_write ? &sp->write : &sp->read;
#endif
		} else {
			return;
		}
#ifdef _USE_9P
	} else if (op_ctx->req_type == _9P_REQUEST) {
		struct _9p_stats *sp = get_9p(gsh_st, lock);

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
 * @param success      [IN] protocol error code == OK
 * @param dup          [IN] true if op was detected duplicate
 */

static void record_op(struct proto_op *op, nsecs_elapsed_t request_time,
		      bool success, bool dup)
{
	/* count the op */
	(void)atomic_inc_uint64_t(&op->total);
	/* also count it as an error if protocol not happy */
	if (!success)
		(void)atomic_inc_uint64_t(&op->errors);
	if (unlikely(dup))
		(void)atomic_inc_uint64_t(&op->dups);
	record_latency(op, request_time, dup);
}

/**
 * @brief count the protocol operation only
 *
 * Use atomic ops to avoid locks. We don't lock for the max
 * and min because if there is a collision, over the long haul,
 * the error is near zero...
 *
 * @param op           [IN] pointer to specific protocol struct
 * @param success      [IN] protocol error code == OK
 * @param dup          [IN] true if op was detected duplicate
 */

static void record_op_only(struct proto_op *op, bool success, bool dup)
{
	/* count the op */
	(void)atomic_inc_uint64_t(&op->total);
	/* also count it as an error if protocol not happy */
	if (!success)
		(void)atomic_inc_uint64_t(&op->errors);
	if (unlikely(dup))
		(void)atomic_inc_uint64_t(&op->dups);
}

static void record_clnt_ops(struct op_count *op, bool success, bool dup)
{
	/* count the op */
	(void)atomic_inc_uint64_t(&op->total);
	/* also count it as an error if protocol not happy */
	if (!success)
		(void)atomic_inc_uint64_t(&op->errors);
	if (unlikely(dup))
		(void)atomic_inc_uint64_t(&op->dups);
}

#ifdef USE_DBUS
/**
 *  @brief reset the counts for protocol operation
 *  Use atomic ops to avoid locks.
 *  @param op           [IN] pointer to specific protocol struct
 */

static void reset_op(struct proto_op *op)
{
	(void)atomic_store_uint64_t(&op->total, 0);
	(void)atomic_store_uint64_t(&op->errors, 0);
	(void)atomic_store_uint64_t(&op->dups, 0);
	/* reset latency related counters */
	(void)atomic_store_uint64_t(&op->latency.latency, 0);
	(void)atomic_store_uint64_t(&op->latency.min, 0);
	(void)atomic_store_uint64_t(&op->latency.max, 0);
	(void)atomic_store_uint64_t(&op->dup_latency.latency, 0);
	(void)atomic_store_uint64_t(&op->dup_latency.min, 0);
	(void)atomic_store_uint64_t(&op->dup_latency.max, 0);
}

/**
 *  @brief reset the counts for op_count struct
 *  Use atomic ops to avoid locks.
 *  @param op           [IN] pointer to specific op_count struct
 */

static void reset_op_count(struct op_count *op)
{
	(void)atomic_store_uint64_t(&op->total, 0);
	(void)atomic_store_uint64_t(&op->errors, 0);
	(void)atomic_store_uint64_t(&op->dups, 0);
}

/**
 *  @brief reset the counts for xfer protocol operation
 *  Use atomic ops to avoid locks.
 *  @param xfer           [IN] pointer to specific xfer protocol struct
 */

static void reset_xfer_op(struct xfer_op *xfer)
{
	reset_op(&xfer->cmd);
	(void)atomic_store_uint64_t(&xfer->requested, 0);
	(void)atomic_store_uint64_t(&xfer->transferred, 0);
}

/**
 * @brief reset the counts related to layout
 * Use atomic ops to avoid locks.
 * @param lo           [IN] pointer to specific layout struct
 */

static void reset_layout_op(struct layout_op *lo)
{
	(void)atomic_store_uint64_t(&lo->total, 0);
	(void)atomic_store_uint64_t(&lo->errors, 0);
	(void)atomic_store_uint64_t(&lo->delays, 0);
}

#ifdef _USE_NFS3
/**
 * @brief reset the counts nfsv3_stats
 * Use atomic ops to avoid locks.
 * @param nfsv3           [IN] pointer to nfsv3_stats struct
 */

static void reset_nfsv3_stats(struct nfsv3_stats *nfsv3)
{
	/* Reset stats counter for nfsv3 protocol */
	reset_op(&nfsv3->cmds);
	reset_xfer_op(&nfsv3->read);
	reset_xfer_op(&nfsv3->write);
}
#endif

/**
 * @brief reset the counts nfsv40_stats
 * Use atomic ops to avoid locks.
 * @param nfsv40           [IN] pointer to nfsv40_stats struct
 */

static void reset_nfsv40_stats(struct nfsv40_stats *nfsv40)
{
	/* Reset stats counter for nfsv4 protocol */
	reset_op(&nfsv40->compounds);
	(void)atomic_store_uint64_t(&nfsv40->ops_per_compound, 0);
	reset_xfer_op(&nfsv40->read);
	reset_xfer_op(&nfsv40->write);
}


/**
 * @brief reset the counts nfsv41_stats
 * Use atomic ops to avoid locks.
 * @param nfsv41           [IN] pointer to nfsv41_stats struct
 */

static void reset_nfsv41_stats(struct nfsv41_stats *nfsv41)
{
	/* Reset stats counter for nfsv41 protocol */
	reset_op(&nfsv41->compounds);
	(void)atomic_store_uint64_t(&nfsv41->ops_per_compound, 0);
	reset_xfer_op(&nfsv41->read);
	reset_xfer_op(&nfsv41->write);
	reset_layout_op(&nfsv41->getdevinfo);
	reset_layout_op(&nfsv41->layout_get);
	reset_layout_op(&nfsv41->layout_commit);
	reset_layout_op(&nfsv41->layout_return);
	reset_layout_op(&nfsv41->recall);
}


#ifdef _USE_NFS3
/**
 * @brief reset the counts mnt_stats
 * Use atomic ops to avoid locks.
 * @param mnt           [IN] pointer to mnt_stats struct
 */

static void reset_mnt_stats(struct mnt_stats *mnt)
{
	/* Reset stats counter for mount protocol */
	reset_op(&mnt->v1_ops);
	reset_op(&mnt->v3_ops);
}
#endif

#ifdef _USE_RQUOTA
/**
 * @brief reset the counts rquota_stats
 * Use atomic ops to avoid locks.
 * @param rquota           [IN] pointer to rquota_stats struct
 */

static void reset_rquota_stats(struct rquota_stats *rquota)
{
	/* Reset stats counter for quota */
	reset_op(&rquota->ops);
	reset_op(&rquota->ext_ops);
}
#endif

#ifdef _USE_NLM
/**
 * @brief reset the counts nlmv4_stats
 * Use atomic ops to avoid locks.
 * @param nlmv4           [IN] pointer to nlmv4_stats struct
 */

static void reset_nlmv4_stats(struct nlmv4_stats *nlmv4)
{
	/* Reset stats counter for nlmv4 */
	reset_op(&nlmv4->ops);
}
#endif

/**
 * @brief reset the counts nlmv4_stats
 * Use atomic ops to avoid locks.
 * @param nlmv4           [IN] pointer to nlmv4_stats struct
 */

static void reset_deleg_stats(struct deleg_stats *deleg)
{
	/* Reset stats counter for deleg */
	(void)atomic_store_uint32_t(&deleg->curr_deleg_grants, 0);
	(void)atomic_store_uint32_t(&deleg->tot_recalls, 0);
	(void)atomic_store_uint32_t(&deleg->failed_recalls, 0);
	(void)atomic_store_uint32_t(&deleg->num_revokes, 0);
}

#ifdef _USE_9P
static void reset__9P_stats(struct _9p_stats *_9p)
{
	u8 opc;

	reset_op(&_9p->cmds);
	reset_xfer_op(&_9p->read);
	reset_xfer_op(&_9p->write);
	(void)atomic_store_uint64_t(&_9p->trans.rx_bytes, 0);
	(void)atomic_store_uint64_t(&_9p->trans.rx_pkt, 0);
	(void)atomic_store_uint64_t(&_9p->trans.rx_err, 0);
	(void)atomic_store_uint64_t(&_9p->trans.tx_bytes, 0);
	(void)atomic_store_uint64_t(&_9p->trans.tx_pkt, 0);
	(void)atomic_store_uint64_t(&_9p->trans.tx_err, 0);
	for (opc = 0; opc <= _9P_RWSTAT; opc++) {
		if (_9p->opcodes[opc] != NULL)
			reset_op(_9p->opcodes[opc]);
	}
}
#endif
#endif		/* USE_DBUS */

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
			    int status, bool is_export)
{
	if (minorversion == 0) {
		struct nfsv40_stats *sp = get_v40(gsh_st, lock);

		/* record stuff */
		switch (nfsv40_optype[proto_op]) {
		case READ_OP:
			if (is_export)
				record_latency(&sp->read.cmd, request_time,
					       false);
			break;
		case WRITE_OP:
			if (is_export)
				record_latency(&sp->write.cmd, request_time,
					       false);
			break;
		default:
			if (is_export)
				record_op(&sp->compounds, request_time,
					status == NFS4_OK, false);
			else
				record_op_only(&sp->compounds,
					status == NFS4_OK, false);
		}
	} else if (minorversion == 1) {
		struct nfsv41_stats *sp = get_v41(gsh_st, lock);

		/* record stuff */
		switch (nfsv41_optype[proto_op]) {
		case READ_OP:
			if (is_export)
				record_latency(&sp->read.cmd, request_time,
					       false);
			break;
		case WRITE_OP:
			if (is_export)
				record_latency(&sp->write.cmd, request_time,
					       false);
			break;
		case LAYOUT_OP:
			record_layout(sp, proto_op, status);
			break;
		default:
			if (is_export)
				record_op(&sp->compounds, request_time,
					  status == NFS4_OK, false);
			else
				record_op_only(&sp->compounds,
					  status == NFS4_OK, false);
		}
	} else if (minorversion == 2) {
		struct nfsv41_stats *sp = get_v42(gsh_st, lock);

		/* record stuff */
		switch (nfsv42_optype[proto_op]) {
		case READ_OP:
			if (is_export)
				record_latency(&sp->read.cmd, request_time,
					       false);
			break;
		case WRITE_OP:
			if (is_export)
				record_latency(&sp->write.cmd, request_time,
					       false);
			break;
		case LAYOUT_OP:
			record_layout(sp, proto_op, status);
			break;
		default:
			if (is_export)
				record_op(&sp->compounds, request_time,
					status == NFS4_OK, false);
			else
				record_op_only(&sp->compounds,
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
			    bool success)
{
	if (minorversion == 0) {

		struct nfsv40_stats *sp = get_v40(gsh_st, lock);

		/* record stuff */
		record_op(&sp->compounds, request_time, success, false);
		(void)atomic_add_uint64_t(&sp->ops_per_compound, num_ops);
	} else if (minorversion == 1) {
		struct nfsv41_stats *sp = get_v41(gsh_st, lock);

		/* record stuff */
		record_op(&sp->compounds, request_time, success, false);
		(void)atomic_add_uint64_t(&sp->ops_per_compound, num_ops);
	} else if (minorversion == 2) {
		struct nfsv41_stats *sp = get_v42(gsh_st, lock);

		/* record stuff */
		record_op(&sp->compounds, request_time, success, false);
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
 * @param dup          [IN] detected this was a dup request
 */

static void record_clnt_stats(struct gsh_stats *gsh_st, pthread_rwlock_t *lock,
			 nfs_request_t *reqdata, bool success, bool dup)
{
	struct svc_req *req = &reqdata->svc;
	uint32_t proto_op = req->rq_msg.cb_proc;
	uint32_t program_op = req->rq_msg.cb_prog;

	if (program_op == NFS_program[P_NFS]) {
		if (proto_op == 0)
			return;	/* we don't count NULL ops */
#ifdef _USE_NFS3
		if (req->rq_msg.cb_vers == NFS_V3) {
			struct nfsv3_stats *sp = get_v3(gsh_st, lock);

			/* record stuff */
			switch (nfsv3_optype[proto_op]) {
			/* op count for READ & WRITE is already done */
			case READ_OP:
			case WRITE_OP:
				break;
			default:
				record_op_only(&sp->cmds, success, dup);
			}
		}
		/* We don't do V4 here and V2 is toast */
#endif
#ifdef _USE_NFS3
	} else if (program_op == NFS_program[P_MNT]) {
		struct mnt_stats *sp = get_mnt(gsh_st, lock);

		if (req->rq_msg.cb_vers == MOUNT_V1)
			record_op_only(&sp->v1_ops, success, dup);
		else
			record_op_only(&sp->v3_ops, success, dup);
#endif
#ifdef _USE_NLM
	} else if (program_op == NFS_program[P_NLM]) {
		struct nlmv4_stats *sp = get_nlm4(gsh_st, lock);

		record_op_only(&sp->ops, success, dup);
#endif
#ifdef _USE_RQUOTA
	} else if (program_op == NFS_program[P_RQUOTA]) {
		struct rquota_stats *sp = get_rquota(gsh_st, lock);

		if (req->rq_msg.cb_vers == RQUOTAVERS)
			record_op_only(&sp->ops, success, dup);
		else
			record_op_only(&sp->ext_ops, success, dup);
#endif
	}
}

static void record_clnt_all_stats(struct gsh_clnt_allops_stats *gsh_st,
		pthread_rwlock_t *lock, uint32_t prog_op, uint32_t proto_op,
		uint32_t vers, bool success, bool dup)
{
	if (prog_op == NFS_program[P_NFS]) {
		if (proto_op == 0)
			return;	/* we don't count NULL ops */
		if (vers == NFS_V4) {
			struct clnt_allops_v4_stats *sp =
						get_v4_all(gsh_st, lock);

			/* record stuff */
			record_clnt_ops(&(sp->cmds[proto_op]), success, dup);
#ifdef _USE_NFS3
		} else if (vers == NFS_V3) {
			struct clnt_allops_v3_stats *sp =
						get_v3_all(gsh_st, lock);

			/* record stuff */
			record_clnt_ops(&(sp->cmds[proto_op]), success, dup);
#endif
		}
#ifdef _USE_NLM
	} else if (prog_op == NFS_program[P_NLM]) {
		struct clnt_allops_nlm_stats *sp = get_nlm4_all(gsh_st, lock);

		record_clnt_ops(&(sp->cmds[proto_op]), success, dup);
#endif
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
 * @param dup          [IN] detected this was a dup request
 */

static void record_stats(struct gsh_stats *gsh_st, pthread_rwlock_t *lock,
			 nfs_request_t *reqdata, nsecs_elapsed_t request_time,
			 bool success, bool dup, bool global)
{
	struct svc_req *req = &reqdata->svc;
	uint32_t proto_op = req->rq_msg.cb_proc;
	uint32_t program_op = req->rq_msg.cb_prog;

	if (program_op == NFS_program[P_NFS]) {
		if (proto_op == 0)
			return;	/* we don't count NULL ops */
#ifdef _USE_NFS3
		if (req->rq_msg.cb_vers == NFS_V3) {
			struct nfsv3_stats *sp = get_v3(gsh_st, lock);

			/* record stuff */
			if (global)
				record_op(&global_st.nfsv3.cmds, request_time,
					  success, dup);
			switch (nfsv3_optype[proto_op]) {
			case READ_OP:
				record_latency(&sp->read.cmd, request_time,
					       dup);
				break;
			case WRITE_OP:
				record_latency(&sp->write.cmd, request_time,
					       dup);
				break;
			default:
				record_op(&sp->cmds, request_time,
					  success, dup);
			}
		}
#endif
		/* We don't do V4 here and V2 is toast */
#ifdef _USE_NFS3
	} else if (program_op == NFS_program[P_MNT]) {
		struct mnt_stats *sp = get_mnt(gsh_st, lock);

		if (global && req->rq_msg.cb_vers == MOUNT_V1)
			record_op(&global_st.mnt.v1_ops, request_time,
				  success, dup);
		else if (global)
			record_op(&global_st.mnt.v3_ops, request_time,
				  success, dup);

		/* record stuff */
		if (req->rq_msg.cb_vers == MOUNT_V1)
			record_op(&sp->v1_ops, request_time, success, dup);
		else
			record_op(&sp->v3_ops, request_time, success, dup);
#endif
#ifdef _USE_NLM
	} else if (program_op == NFS_program[P_NLM]) {
		struct nlmv4_stats *sp = get_nlm4(gsh_st, lock);

		if (global)
			record_op(&global_st.nlm4.ops, request_time,
				  success, dup);
		/* record stuff */
		record_op(&sp->ops, request_time, success, dup);
#endif
#ifdef _USE_RQUOTA
	} else if (program_op == NFS_program[P_RQUOTA]) {
		struct rquota_stats *sp = get_rquota(gsh_st, lock);

		if (global)
			record_op(&global_st.rquota.ops, request_time,
				  success, dup);
		/* record stuff */
		if (req->rq_msg.cb_vers == RQUOTAVERS)
			record_op(&sp->ops, request_time, success, dup);
		else
			record_op(&sp->ext_ops, request_time, success, dup);
#endif
	}
}

#ifdef _USE_9P
/**
 * @brief Record transport stats
 *
 */
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
/**
 * @brief record 9P tcp transport stats
 *
 * Called from 9P functions doing send/recv
 */
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

/**
 * @bried record 9p operation stats
 *
 * Called from 9P interpreter at operation completion
 */
void server_stats_9p_done(u8 opc, struct _9p_request_data *req9p)
{
	struct gsh_client *client;
	struct gsh_export *export;
	struct _9p_stats *sp;

	client = req9p->pconn->client;
	if (client) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		sp = get_9p(&server_st->st, &client->lock);
		if (sp->opcodes[opc] == NULL)
			sp->opcodes[opc] =
				gsh_calloc(1, sizeof(struct proto_op));
		record_op(sp->opcodes[opc], 0, true, false);
	}

	if (op_ctx->ctx_export) {
		struct export_stats *exp_st;

		export = op_ctx->ctx_export;
		exp_st = container_of(export, struct export_stats, export);
		sp = get_9p(&exp_st->st, &export->lock);
		if (sp->opcodes[opc] == NULL)
			sp->opcodes[opc] =
				gsh_calloc(1, sizeof(struct proto_op));
		record_op(sp->opcodes[opc], 0, true, false);
	}
}
#endif

#ifdef _USE_NFS3
static void record_v3_full_stats(struct svc_req *req,
			       nsecs_elapsed_t request_time,
			       bool success, bool dup);
#endif

static void record_v4_full_stats(uint32_t proc,
			       nsecs_elapsed_t request_time,
			       bool success);

/**
 * @brief record NFS op finished
 *
 * Called from nfs_rpc_process_request at operation/command completion
 */

void server_stats_nfs_done(nfs_request_t *reqdata, int rc, bool dup)
{
	struct gsh_client *client = op_ctx->client;
	struct timespec current_time;
	nsecs_elapsed_t time_diff;
	struct svc_req *req = &reqdata->svc;
	uint32_t proto_op = req->rq_msg.cb_proc;
	uint32_t program_op = req->rq_msg.cb_prog;

	if (!nfs_param.core_param.enable_NFSSTATS)
		return;
#ifdef _USE_NFS3
	if (program_op == NFS_PROGRAM && op_ctx->nfs_vers == NFS_V3)
		global_st.v3.op[proto_op]++;
#endif
#ifdef _USE_NLM
	else if (program_op == NFS_program[P_NLM])
		global_st.lm.op[proto_op]++;
#endif
#ifdef _USE_NFS3
	else if (program_op == NFS_program[P_MNT])
		global_st.mn.op[proto_op]++;
#endif
#ifdef _USE_RQUOTA
	else if (program_op == NFS_program[P_RQUOTA])
		global_st.qt.op[proto_op]++;
#endif

	if (nfs_param.core_param.enable_FASTSTATS)
		return;

	now(&current_time);
	time_diff = timespec_diff(&op_ctx->start_time, &current_time);

#ifdef _USE_NFS3
	if (nfs_param.core_param.enable_FULLV3STATS)
		record_v3_full_stats(req, time_diff,
			    rc == NFS_REQ_OK, dup);
#endif
	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		record_clnt_stats(&server_st->st, &client->lock, reqdata,
			     rc == NFS_REQ_OK, dup);
		if (nfs_param.core_param.enable_CLNTALLSTATS)
			record_clnt_all_stats(&server_st->c_all, &client->lock,
					program_op, proto_op, NFS_V3,
					rc == NFS_REQ_OK, dup);
		timespec_update(&client->last_update, &current_time);
	}
	if (!dup && op_ctx->ctx_export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->ctx_export, struct export_stats,
			    export);
		record_stats(&exp_st->st, &op_ctx->ctx_export->lock, reqdata,
			     time_diff,
			     rc == NFS_REQ_OK, dup, true);
		timespec_update(&op_ctx->ctx_export->last_update,
				&current_time);
	}
}

/**
 * @brief record NFS V4 compound finished
 *
 * Called from nfs4_compound at compound loop completion
 */

void server_stats_nfsv4_op_done(int proto_op,
				struct timespec *start_time, int status)
{
	struct gsh_client *client = op_ctx->client;
	struct timespec current_time;
	nsecs_elapsed_t time_diff;

	if (!nfs_param.core_param.enable_NFSSTATS)
		return;
	if (op_ctx->nfs_vers == NFS_V4)
		global_st.v4.op[proto_op]++;

	if (nfs_param.core_param.enable_FASTSTATS)
		return;

	now(&current_time);
	time_diff = timespec_diff(start_time, &current_time);

	if (nfs_param.core_param.enable_FULLV4STATS)
		record_v4_full_stats(proto_op, time_diff,
			    status == NFS4_OK);

	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		record_nfsv4_op(&server_st->st, &client->lock, proto_op,
				op_ctx->nfs_minorvers, time_diff,
				status, false);
		if (nfs_param.core_param.enable_CLNTALLSTATS)
			record_clnt_all_stats(&server_st->c_all, &client->lock,
				NFS_program[P_NFS], proto_op, NFS_V4,
				status == NFS_REQ_OK, false);
		timespec_update(&client->last_update, &current_time);
	}

	if (op_ctx->nfs_minorvers == 0)
		record_op(&global_st.nfsv40.compounds, time_diff,
			  status == NFS4_OK, false);
	else if (op_ctx->nfs_minorvers == 1)
		record_op(&global_st.nfsv41.compounds, time_diff,
			  status == NFS4_OK, false);
	else if (op_ctx->nfs_minorvers == 2)
		record_op(&global_st.nfsv42.compounds, time_diff,
			  status == NFS4_OK, false);

	if (op_ctx->ctx_export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->ctx_export, struct export_stats,
			    export);
		record_nfsv4_op(&exp_st->st, &op_ctx->ctx_export->lock,
				proto_op, op_ctx->nfs_minorvers,
				time_diff, status, true);
		timespec_update(&op_ctx->ctx_export->last_update,
				&current_time);
	}
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
	nsecs_elapsed_t time_diff;

	if (!nfs_param.core_param.enable_NFSSTATS)
		return;
	now(&current_time);
	time_diff = timespec_diff(&op_ctx->start_time, &current_time);

	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		record_compound(&server_st->st, &client->lock,
				op_ctx->nfs_minorvers,
				num_ops, time_diff,
				status == NFS4_OK);
		timespec_update(&client->last_update, &current_time);
	}
	if (op_ctx->ctx_export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->ctx_export, struct export_stats,
			    export);
		record_compound(&exp_st->st, &op_ctx->ctx_export->lock,
				op_ctx->nfs_minorvers, num_ops,
				time_diff,
				status == NFS4_OK);
		timespec_update(&op_ctx->ctx_export->last_update,
				&current_time);
	}
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
	if (!nfs_param.core_param.enable_NFSSTATS)
		return;
	if (op_ctx->client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(op_ctx->client, struct server_stats,
					 client);
		record_io_stats(&server_st->st, &op_ctx->client->lock,
				requested, transferred, success,
				is_write);
	}
	if (op_ctx->ctx_export != NULL) {
		struct export_stats *exp_st;

		exp_st =
		    container_of(op_ctx->ctx_export, struct export_stats,
			    export);
		record_io_stats(&exp_st->st, &op_ctx->ctx_export->lock,
				requested, transferred, success, is_write);
	}
}

/**
 * @brief record Delegation stats
 *
 * Called from a bunch of places.
 */
void check_deleg_struct(struct gsh_stats *stats, pthread_rwlock_t *lock)
{
	if (unlikely(stats->deleg == NULL)) {
		PTHREAD_RWLOCK_wrlock(lock);
		if (stats->deleg == NULL)
			stats->deleg = gsh_calloc(1,
					sizeof(struct deleg_stats));
		PTHREAD_RWLOCK_unlock(lock);
	}
}
void inc_grants(struct gsh_client *client)
{
	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		check_deleg_struct(&server_st->st, &client->lock);
		server_st->st.deleg->curr_deleg_grants++;
	}
}
void dec_grants(struct gsh_client *client)
{
	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		check_deleg_struct(&server_st->st, &client->lock);
		server_st->st.deleg->curr_deleg_grants++;
	}
}
void inc_revokes(struct gsh_client *client)
{
	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		check_deleg_struct(&server_st->st, &client->lock);
		server_st->st.deleg->num_revokes++;
	}
}
void inc_recalls(struct gsh_client *client)
{
	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		check_deleg_struct(&server_st->st, &client->lock);
		server_st->st.deleg->tot_recalls++;
	}
}
void inc_failed_recalls(struct gsh_client *client)
{
	if (client != NULL) {
		struct server_stats *server_st;

		server_st = container_of(client, struct server_stats, client);
		check_deleg_struct(&server_st->st, &client->lock);
		server_st->st.deleg->failed_recalls++;
	}
}

#ifdef USE_DBUS

void dbus_message_iter_append_protocol_info(DBusMessageIter *niter,
					    char **protocol,
					    dbus_bool_t *enabled)
{
	DBusMessageIter p_iter;

	dbus_message_iter_open_container(niter, DBUS_TYPE_STRUCT, NULL,
					 &p_iter);
	dbus_message_iter_append_basic(&p_iter, DBUS_TYPE_STRING,
				       protocol);
	dbus_message_iter_append_basic(&p_iter, DBUS_TYPE_BOOLEAN,
				       enabled);
	dbus_message_iter_close_container(niter, &p_iter);
}

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
	dbus_bool_t stats_available;
	DBusMessageIter st_iter;
	char *protocol;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &st_iter);

#ifdef _USE_NFS3
	stats_available = st->nfsv3 != 0;
	protocol = "NFSv3";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);

	stats_available = st->mnt != 0;
	protocol = "MNT";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);
#endif
#ifdef _USE_NLM
	stats_available = st->nlm4 != 0;
	protocol = "NMLv4";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);
#endif
#ifdef _USE_RQUOTA
	stats_available = st->rquota != 0;
	protocol = "RQUOTA";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);

#endif
	stats_available = st->nfsv40 != 0;
	protocol = "NFSv40";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);
	stats_available = st->nfsv41 != 0;
	protocol = "NFSv41";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);

	stats_available = st->nfsv42 != 0;
	protocol = "NFSv42";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);
#ifdef _USE_9P
	stats_available = st->_9p != 0;
	protocol = "9P";
	dbus_message_iter_append_protocol_info(&st_iter, &protocol,
					       &stats_available);
#endif
	dbus_message_iter_close_container(iter, &st_iter);
}

#ifdef _USE_9P
/** @brief Report protocol operation statistics
 *
 * struct proto_op {
 *         uint64_t total;
 *         uint64_t errors;
 *         ...
 * }
 *
 * @param op    [IN] pointer to proto op sub-structure of interest
 * @param iter  [IN] interator in reply stream to fill
 */
static void server_dbus_op_stats(struct proto_op *op, DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;
	uint64_t zero = 0;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       op == NULL ? &zero : &op->total);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       op == NULL ? &zero : &op->errors);
	dbus_message_iter_close_container(iter, &struct_iter);
}
#endif

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
	uint64_t zero = 0;

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
				       &zero);
	dbus_message_iter_close_container(iter, &struct_iter);
}

/**
 * @brief Report Client/Export statistics for xfer_op
 *
 * struct iostats {
 *       uint64_t total_ops;
 *       uint64_t errors;
 *       double latency;
 *       uint64_t bytes_transferred;
 * }
 *
 * @param iop   [IN] pointer to xfer op sub-structure of interest
 * @param iter  [IN] interator in reply stream to fill
 * @param for_export  [IN] boolean indicating whether it is for an export
 **/

static void server_dbus_cexop_stats(struct xfer_op *iop, DBusMessageIter *iter,
				    bool for_export)
{
	DBusMessageIter struct_iter;
	double res = 0.0;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->cmd.total);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->cmd.errors);
	if (for_export) {
		if (iop->cmd.total)
			res = (double) (iop->cmd.latency.latency * 0.000001) /
					(iop->cmd.total);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
	}
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &iop->transferred);
	dbus_message_iter_close_container(iter, &struct_iter);
}

/**
 * @brief Report Client/Export statistics for proto_op
 *
 * struct iostats {
 *       uint64_t total_ops;
 *       uint64_t errors;
 *       double latency;
 * }
 *
 * @param iop   [IN] pointer to proto_op sub-structure of interest
 * @param iter  [IN] interator in reply stream to fill
 * @param for_export  [IN] boolean indicating whether it is for an export
 **/
static void server_dbus_ceop_stats(struct proto_op *op, DBusMessageIter *iter,
				   bool for_export)
{
	DBusMessageIter struct_iter;
	double res = 0.0;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &op->total);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &op->errors);
	if (for_export) {
		if (op->total)
			res = (double) (op->latency.latency / op->total) *
			       0.000001;
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
	}
	dbus_message_iter_close_container(iter, &struct_iter);
}

/**
 * @brief Report Client/Export statistics for layout operations
 *
 * struct layout {
 *       uint64_t total;
 *       uint64_t errors;
 *       uint64_t delays;
 * }
 *
 * @param sp   [IN] pointer to nfsv41_stats sub-structure of interest
 * @param iter  [IN] interator in reply stream to fill
 * @param for_export  [IN] boolean indicating whether it is for an export
 **/
static void server_dbus_celo_stats(struct nfsv41_stats *sp,
				   DBusMessageIter *iter,
				   bool for_export)
{
	DBusMessageIter struct_iter;
	uint64_t total, errors, delays;

	total = sp->getdevinfo.total + sp->layout_get.total +
		sp->layout_commit.total + sp->layout_return.total;
	errors = sp->getdevinfo.errors + sp->layout_get.errors +
		sp->layout_commit.errors + sp->layout_return.errors;
	delays = sp->getdevinfo.delays + sp->layout_get.delays +
		sp->layout_commit.delays + sp->layout_return.delays;
	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64, &total);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64, &errors);
	if (for_export)
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					       &delays);
	dbus_message_iter_close_container(iter, &struct_iter);
}

#ifdef _USE_9P
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
#endif

void server_dbus_client_io_ops(DBusMessageIter *iter,
				struct gsh_client *client)
{
	struct server_stats *svr = NULL;
	struct gsh_stats *st;
	dbus_bool_t stats_available;

	svr = container_of(client, struct server_stats, client);
	st = &svr->st;

	gsh_dbus_append_timestamp(iter, &client->last_update);

#ifdef _USE_NFS3
	stats_available = st->nfsv3 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv3) {
		server_dbus_cexop_stats(&st->nfsv3->read, iter, false);
		server_dbus_cexop_stats(&st->nfsv3->write, iter, false);
		server_dbus_ceop_stats(&st->nfsv3->cmds, iter, false);
	}
#endif

	stats_available = st->nfsv40 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv40) {
		server_dbus_cexop_stats(&st->nfsv40->read, iter, false);
		server_dbus_cexop_stats(&st->nfsv40->write, iter, false);
		server_dbus_ceop_stats(&st->nfsv40->compounds, iter, false);
	}

	stats_available = st->nfsv41 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv41) {
		server_dbus_cexop_stats(&st->nfsv41->read, iter, false);
		server_dbus_cexop_stats(&st->nfsv41->write, iter, false);
		server_dbus_ceop_stats(&st->nfsv41->compounds, iter, false);
		server_dbus_celo_stats(st->nfsv41, iter, false);
	}

	stats_available = st->nfsv42 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv42) {
		server_dbus_cexop_stats(&st->nfsv42->read, iter, false);
		server_dbus_cexop_stats(&st->nfsv42->write, iter, false);
		server_dbus_ceop_stats(&st->nfsv42->compounds, iter, false);
		server_dbus_celo_stats(st->nfsv42, iter, false);
	}
}

void server_dbus_client_all_ops(DBusMessageIter *iter,
				struct gsh_client *client)
{
	struct server_stats *svr = NULL;
	struct gsh_clnt_allops_stats *c_all;
	dbus_bool_t stats_available;
	int i;
	DBusMessageIter array_iter;
	struct gsh_stats *st;
	uint64_t tot_cmp = 0, err_cmp = 0, ops_in_cmp = 0;

	svr = container_of(client, struct server_stats, client);
	c_all = &svr->c_all;
	st = &svr->st;

	gsh_dbus_append_timestamp(iter, &client->last_update);

#ifdef _USE_NFS3
	/* Stats of NFSv3 ops */
	stats_available = c_all->nfsv3 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (c_all->nfsv3) {
		dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT,
						 NULL, &array_iter);
		for (i = 0; i < NFSPROC3_COMMIT + 1; i++) {
			if (c_all->nfsv3->cmds[i].total) {
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_STRING, &optabv3[i].name);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nfsv3->cmds[i].total);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nfsv3->cmds[i].errors);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nfsv3->cmds[i].dups);
			}
		}
		dbus_message_iter_close_container(iter, &array_iter);
	}
#endif

#ifdef _USE_NLM
	/* Stats of NLMv4 ops */
	stats_available = c_all->nlm4 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (c_all->nlm4) {
		dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT,
						 NULL, &array_iter);
		for (i = 0; i < NLMPROC4_FREE_ALL + 1; i++) {
			if (c_all->nlm4->cmds[i].total) {
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_STRING, &optnlm[i].name);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nlm4->cmds[i].total);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nlm4->cmds[i].errors);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nlm4->cmds[i].dups);
			}
		}
		dbus_message_iter_close_container(iter, &array_iter);
	}
#endif

	/* Stats of NFSv4 ops */
	stats_available = c_all->nfsv4 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (c_all->nfsv4) {
		dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT,
						 NULL, &array_iter);
		for (i = 0; i < NFS4_OP_LAST_ONE; i++) {
			if (c_all->nfsv4->cmds[i].total) {
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_STRING, &optabv4[i].name);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nfsv4->cmds[i].total);
				dbus_message_iter_append_basic(&array_iter,
					DBUS_TYPE_UINT64,
					&c_all->nfsv4->cmds[i].errors);
			}
		}
		dbus_message_iter_close_container(iter, &array_iter);
	}
	/* Gather info abt compound ops */
	if (st->nfsv40) {
		tot_cmp += st->nfsv40->compounds.total;
		err_cmp += st->nfsv40->compounds.errors;
		ops_in_cmp += st->nfsv40->ops_per_compound;
	}
	if (st->nfsv41) {
		tot_cmp += st->nfsv41->compounds.total;
		err_cmp += st->nfsv41->compounds.errors;
		ops_in_cmp += st->nfsv41->ops_per_compound;
	}
	if (st->nfsv42) {
		tot_cmp += st->nfsv42->compounds.total;
		err_cmp += st->nfsv42->compounds.errors;
		ops_in_cmp += st->nfsv42->ops_per_compound;
	}
	stats_available = tot_cmp != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (stats_available) {
		dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT,
						 NULL, &array_iter);
		dbus_message_iter_append_basic(&array_iter,
			DBUS_TYPE_UINT64, &tot_cmp);
		dbus_message_iter_append_basic(&array_iter,
			DBUS_TYPE_UINT64, &err_cmp);
		dbus_message_iter_append_basic(&array_iter,
			DBUS_TYPE_UINT64, &ops_in_cmp);
		dbus_message_iter_close_container(iter, &array_iter);
	}
}

void server_dbus_export_details(DBusMessageIter *iter,
				struct gsh_export *g_export)
{
	struct export_stats *exp_st = NULL;
	struct gsh_stats *st;
	dbus_bool_t stats_available;

	exp_st = container_of(g_export, struct export_stats, export);
	st = &exp_st->st;

	gsh_dbus_append_timestamp(iter, &g_export->last_update);

#ifdef _USE_NFS3
	stats_available = st->nfsv3 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv3) {
		server_dbus_cexop_stats(&st->nfsv3->read, iter, true);
		server_dbus_cexop_stats(&st->nfsv3->write, iter, true);
		server_dbus_ceop_stats(&st->nfsv3->cmds, iter, true);
	}
#endif

	stats_available = st->nfsv40 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv40) {
		server_dbus_cexop_stats(&st->nfsv40->read, iter, true);
		server_dbus_cexop_stats(&st->nfsv40->write, iter, true);
		server_dbus_ceop_stats(&st->nfsv40->compounds, iter, true);
	}

	stats_available = st->nfsv41 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv41) {
		server_dbus_cexop_stats(&st->nfsv41->read, iter, true);
		server_dbus_cexop_stats(&st->nfsv41->write, iter, true);
		server_dbus_ceop_stats(&st->nfsv41->compounds, iter, true);
		server_dbus_celo_stats(st->nfsv41, iter, true);
	}

	stats_available = st->nfsv42 != 0;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
				       &stats_available);
	if (st->nfsv42) {
		server_dbus_cexop_stats(&st->nfsv42->read, iter, true);
		server_dbus_cexop_stats(&st->nfsv42->write, iter, true);
		server_dbus_ceop_stats(&st->nfsv42->compounds, iter, true);
		server_dbus_celo_stats(st->nfsv42, iter, true);
	}
}

void server_dbus_total(struct export_stats *export_st, DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;
	uint64_t total = 0;
	char *version;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);

#ifdef _USE_NFS3
	version = "NFSv3";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	if (export_st->st.nfsv3 == NULL)
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&total);
	else
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				&export_st->st.nfsv3->cmds.total);
#endif
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

#ifdef _USE_NFS3
	version = "NFSv3";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.nfsv3.cmds.total);
#endif
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
#ifdef _USE_NLM
	version = "NLM4";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.nlm4.ops.total);
#endif
#ifdef _USE_NFS3
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
#endif
#ifdef _USE_RQUOTA
	version = "RQUOTA";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					&global_st.rquota.ops.total);
#endif
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

#ifdef _USE_NFS3
	version = "NFSv3:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i <= NFSPROC3_COMMIT; i++) {
		if (global_st.v3.op[i] > 0) {
			op = optabv3[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.v3.op[i]);
		}
	}
#endif
	version = "\nNFSv4:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i < NFS4_OP_LAST_ONE; i++) {
		if (global_st.v4.op[i] > 0) {
			op = optabv4[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.v4.op[i]);
		}
	}
#ifdef _USE_NLM
	version = "\nNLM:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i <= NLMPROC4_FREE_ALL; i++) {
		if (global_st.lm.op[i] > 0) {
			op = optnlm[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.lm.op[i]);
		}
	}
#endif
#ifdef _USE_NFS3
	version = "\nMNT:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i <= MOUNTPROC3_EXPORT; i++) {
		if (global_st.mn.op[i] > 0) {
			op = optmnt[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.mn.op[i]);
		}
	}
#endif
#ifdef _USE_RQUOTA
	version = "\nQUOTA:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &version);
	for (i = 0; i <= RQUOTAPROC_SETACTIVEQUOTA; i++) {
		if (global_st.qt.op[i] > 0) {
			op = optqta[i].name;
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_STRING, &op);
			dbus_message_iter_append_basic(&struct_iter,
					DBUS_TYPE_UINT64, &global_st.qt.op[i]);
		}
	}
#endif
	dbus_message_iter_close_container(iter, &struct_iter);
}

#ifdef _USE_NFS3
void server_dbus_v3_iostats(struct nfsv3_stats *v3p, DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_iostats(&v3p->read, iter);
	server_dbus_iostats(&v3p->write, iter);
}
#endif

void server_dbus_v40_iostats(struct nfsv40_stats *v40p, DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_iostats(&v40p->read, iter);
	server_dbus_iostats(&v40p->write, iter);
}

void server_dbus_v41_iostats(struct nfsv41_stats *v41p, DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_iostats(&v41p->read, iter);
	server_dbus_iostats(&v41p->write, iter);
}

void server_dbus_v42_iostats(struct nfsv41_stats *v42p, DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_iostats(&v42p->read, iter);
	server_dbus_iostats(&v42p->write, iter);
}

void server_nfsmon_export_iostats(struct export_stats *export_st,
				  struct xfer_op *opread,
				  struct xfer_op *opwrite)
{
	struct gsh_stats gsh_st = export_st->st;

#ifdef _USE_NFS3
	if (gsh_st.nfsv3 != NULL) {
		(void)atomic_add_uint64_t(&opread->cmd.total,
					  gsh_st.nfsv3->read.cmd.total);
		(void)atomic_add_uint64_t(&opread->requested,
					  gsh_st.nfsv3->read.requested);
		(void)atomic_add_uint64_t(&opread->transferred,
					  gsh_st.nfsv3->read.transferred);
		(void)atomic_add_uint64_t(&opwrite->cmd.total,
					  gsh_st.nfsv3->write.cmd.total);
		(void)atomic_add_uint64_t(&opwrite->requested,
					  gsh_st.nfsv3->write.requested);
		(void)atomic_add_uint64_t(&opwrite->transferred,
					  gsh_st.nfsv3->write.transferred);
	}
#endif

	if (gsh_st.nfsv40 != NULL) {
		(void)atomic_add_uint64_t(&opread->cmd.total,
					  gsh_st.nfsv40->read.cmd.total);
		(void)atomic_add_uint64_t(&opread->requested,
					  gsh_st.nfsv40->read.requested);
		(void)atomic_add_uint64_t(&opread->transferred,
					  gsh_st.nfsv40->read.transferred);
		(void)atomic_add_uint64_t(&opwrite->cmd.total,
					  gsh_st.nfsv40->write.cmd.total);
		(void)atomic_add_uint64_t(&opwrite->requested,
					  gsh_st.nfsv40->write.requested);
		(void)atomic_add_uint64_t(&opwrite->transferred,
					  gsh_st.nfsv40->write.transferred);
	}

	if (gsh_st.nfsv41 != NULL) {
		(void)atomic_add_uint64_t(&opread->cmd.total,
					  gsh_st.nfsv41->read.cmd.total);
		(void)atomic_add_uint64_t(&opread->requested,
					  gsh_st.nfsv41->read.requested);
		(void)atomic_add_uint64_t(&opread->transferred,
					  gsh_st.nfsv41->read.transferred);
		(void)atomic_add_uint64_t(&opwrite->cmd.total,
					  gsh_st.nfsv41->write.cmd.total);
		(void)atomic_add_uint64_t(&opwrite->requested,
					  gsh_st.nfsv41->write.requested);
		(void)atomic_add_uint64_t(&opwrite->transferred,
					  gsh_st.nfsv41->write.transferred);
	}

	if (gsh_st.nfsv42 != NULL) {
		(void)atomic_add_uint64_t(&opread->cmd.total,
					  gsh_st.nfsv42->read.cmd.total);
		(void)atomic_add_uint64_t(&opread->requested,
					  gsh_st.nfsv42->read.requested);
		(void)atomic_add_uint64_t(&opread->transferred,
					  gsh_st.nfsv42->read.transferred);
		(void)atomic_add_uint64_t(&opwrite->cmd.total,
					  gsh_st.nfsv42->write.cmd.total);
		(void)atomic_add_uint64_t(&opwrite->requested,
					  gsh_st.nfsv42->write.requested);
		(void)atomic_add_uint64_t(&opwrite->transferred,
					  gsh_st.nfsv42->write.transferred);
	}
}

void server_ret_nfsmon_iostats(struct xfer_op *op_read,
			       struct xfer_op *op_write,
			       struct xfer_op *op_preread,
			       struct xfer_op *op_prewrite)
{
	(void)atomic_sub_uint64_t(&op_read->cmd.total,
				  op_preread->cmd.total);
	(void)atomic_sub_uint64_t(&op_read->requested,
				  op_preread->requested);
	(void)atomic_sub_uint64_t(&op_read->transferred,
				  op_preread->transferred);

	(void)atomic_sub_uint64_t(&op_write->cmd.total,
				  op_prewrite->cmd.total);
	(void)atomic_sub_uint64_t(&op_write->requested,
				  op_prewrite->requested);
	(void)atomic_sub_uint64_t(&op_write->transferred,
				  op_prewrite->transferred);
}

void server_dbus_nfsmon_iostats(struct export_stats *export_st,
				DBusMessageIter *iter)
{
	struct xfer_op *op_preread = NULL;
	struct xfer_op *op_prewrite = NULL;
	struct xfer_op *op_read = NULL;
	struct xfer_op *op_write = NULL;

	op_preread = (struct xfer_op *)gsh_calloc(1, sizeof(struct xfer_op));
	op_prewrite = (struct xfer_op *)gsh_calloc(1, sizeof(struct xfer_op));
	op_read = (struct xfer_op *)gsh_calloc(1, sizeof(struct xfer_op));
	op_write = (struct xfer_op *)gsh_calloc(1, sizeof(struct xfer_op));

	server_nfsmon_export_iostats(export_st, op_preread, op_prewrite);
	sleep(1);
	server_nfsmon_export_iostats(export_st, op_read, op_write);

	server_ret_nfsmon_iostats(op_read, op_write, op_preread, op_prewrite);

	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_iostats(op_read, iter);
	server_dbus_iostats(op_write, iter);

	gsh_free(op_preread);
	gsh_free(op_prewrite);
	gsh_free(op_read);
	gsh_free(op_write);
}

void server_dbus_fill_io(DBusMessageIter *array_iter, uint16_t *export_id,
			 const char *protocolversion, struct xfer_op *read,
			 struct xfer_op *write)
{
	DBusMessageIter struct_iter;

	LogFullDebug(COMPONENT_DBUS, " Found %s I/O stats for export ID %d",
		     protocolversion, *export_id);

	/* create a structure container iterator for the export statistics */
	dbus_message_iter_open_container(array_iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);

	/* append export statistics */
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT16,
				       export_id);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
				       &(protocolversion));
	server_dbus_iostats(read, &struct_iter);
	server_dbus_iostats(write, &struct_iter);

	/* close the structure container */
	dbus_message_iter_close_container(array_iter, &struct_iter);

}

/**
 * @brief Return all IO stats of an export
 *
 * @reply DBUS_TYPE_ARRAY, "qs(tttttt)(tttttt)"
 *	export id
 *	string containing the protocol version
 *	read statistics structure
 *		(requested, transferred, total, errors, latency, queue wait)
 *	write statistics structure
 *		(requested, transferred, total, errors, latency, queue wait)
 */

void server_dbus_all_iostats(struct export_stats *export_statistics,
			     DBusMessageIter *array_iter)
{
#ifdef _USE_NFS3
	if (export_statistics->st.nfsv3 != NULL) {
		server_dbus_fill_io(array_iter,
				    &(export_statistics->export.export_id),
				    "NFSv3",
				    &(export_statistics->st.nfsv3->read),
				    &(export_statistics->st.nfsv3->write));
	}
#endif

	if (export_statistics->st.nfsv40 != NULL) {
		server_dbus_fill_io(array_iter,
				    &(export_statistics->export.export_id),
				    "NFSv40",
				    &(export_statistics->st.nfsv40->read),
				    &(export_statistics->st.nfsv40->write));
	}

	if (export_statistics->st.nfsv41 != NULL) {
		server_dbus_fill_io(array_iter,
				    &(export_statistics->export.export_id),
				    "NFSv41",
				    &(export_statistics->st.nfsv41->read),
				    &(export_statistics->st.nfsv41->write));
	}

	if (export_statistics->st.nfsv42 != NULL) {
		server_dbus_fill_io(array_iter,
				    &(export_statistics->export.export_id),
				    "NFSv42",
				    &(export_statistics->st.nfsv42->read),
				    &(export_statistics->st.nfsv42->write));
	}
}

void reset_gsh_stats(struct gsh_stats *st)
{
#ifdef _USE_NFS3
	if (st->nfsv3)
		reset_nfsv3_stats(st->nfsv3);
#endif
	if (st->nfsv40)
		reset_nfsv40_stats(st->nfsv40);
	if (st->nfsv41)
		reset_nfsv41_stats(st->nfsv41);
	if (st->nfsv42)
		reset_nfsv41_stats(st->nfsv42); /* Uses v41 stats */
#ifdef _USE_NFS3
	if (st->mnt)
		reset_mnt_stats(st->mnt);
#endif
#ifdef _USE_RQUOTA
	if (st->rquota)
		reset_rquota_stats(st->rquota);
#endif
#ifdef _USE_NLM
	if (st->nlm4)
		reset_nlmv4_stats(st->nlm4);
#endif
	if (st->deleg)
		reset_deleg_stats(st->deleg);
#ifdef _USE_9P
	if (st->_9p)
		reset__9P_stats(st->_9p);
#endif
}

void reset_gsh_allops_stats(struct gsh_clnt_allops_stats *st)
{
	int i;

#ifdef _USE_NFS3
	if (st->nfsv3) {
		for (i = 0; i < NFSPROC3_COMMIT + 1 ; i++) {
			reset_op_count(&(st->nfsv3->cmds[i]));
		}
	}
#endif
	if (st->nfsv4) {
		for (i = 0; i < NFS4_OP_LAST_ONE ; i++) {
			reset_op_count(&(st->nfsv4->cmds[i]));
		}
	}
#ifdef _USE_NLM
	if (st->nlm4) {
		for (i = 0; i < NLMPROC4_FREE_ALL + 1 ; i++) {
			reset_op_count(&(st->nlm4->cmds[i]));
		}
	}
#endif
}

void reset_global_stats(void)
{
	int i;
#ifdef _USE_NFS3
	/* Reset all ops counters of nfsv3 */
	for (i = 0; i < NFSPROC3_COMMIT; i++) {
		(void)atomic_store_uint64_t(&global_st.v3.op[i], 0);
	}
#endif
	/* Reset all ops counters of nfsv4 */
	for (i = 0; i < NFS4_OP_LAST_ONE; i++) {
		(void)atomic_store_uint64_t(&global_st.v4.op[i], 0);
	}
#ifdef _USE_NLM
	/* Reset all ops counters of lock manager */
	for (i = 0; i < NLM4_FAILED; i++) {
		(void)atomic_store_uint64_t(&global_st.lm.op[i], 0);
	}
#endif
#ifdef _USE_NFS3
	/* Reset all ops counters of mountd */
	for (i = 0; i < MOUNTPROC3_EXPORT; i++) {
		(void)atomic_store_uint64_t(&global_st.mn.op[i], 0);
	}
#endif
#ifdef _USE_RQUOTA
	/* Reset all ops counters of rquotad */
	for (i = 0; i < RQUOTAPROC_SETACTIVEQUOTA; i++) {
		(void)atomic_store_uint64_t(&global_st.qt.op[i], 0);
	}
#endif
#ifdef _USE_NFS3
	reset_nfsv3_stats(&global_st.nfsv3);
#endif
	reset_nfsv40_stats(&global_st.nfsv40);
	reset_nfsv41_stats(&global_st.nfsv41);
	reset_nfsv41_stats(&global_st.nfsv42);  /* Uses v41 stats */
#ifdef _USE_NFS3
	reset_mnt_stats(&global_st.mnt);
#endif
#ifdef _USE_RQUOTA
	reset_rquota_stats(&global_st.rquota);
#endif
#ifdef _USE_NLM
	reset_nlmv4_stats(&global_st.nlm4);
#endif
}

void server_dbus_total_ops(struct export_stats *export_st,
			   DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_total(export_st, iter);
}

void server_dbus_fast_ops(DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	global_dbus_fast(iter);
}

void global_dbus_total_ops(DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	global_dbus_total(iter);
}

void reset_server_stats(void)
{
	reset_global_stats();
	reset_export_stats();
	reset_client_stats();
#ifdef _USE_NFS3
	reset_v3_full_stats();
#endif
	reset_v4_full_stats();
}

#ifdef _USE_9P
void server_dbus_9p_iostats(struct _9p_stats *_9pp, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	gsh_dbus_append_timestamp(iter, &timestamp);
	server_dbus_iostats(&_9pp->read, iter);
	server_dbus_iostats(&_9pp->write, iter);
}

void server_dbus_9p_transstats(struct _9p_stats *_9pp, DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	gsh_dbus_append_timestamp(iter, &timestamp);
	server_dbus_transportstats(&_9pp->trans, iter);
}

void server_dbus_9p_opstats(struct _9p_stats *_9pp, u8 opcode,
			    DBusMessageIter *iter)
{
	struct timespec timestamp;

	now(&timestamp);
	gsh_dbus_append_timestamp(iter, &timestamp);
	server_dbus_op_stats(_9pp->opcodes[opcode], iter);
}
#endif


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
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_layouts(&v41p->getdevinfo, iter);
	server_dbus_layouts(&v41p->layout_get, iter);
	server_dbus_layouts(&v41p->layout_commit, iter);
	server_dbus_layouts(&v41p->layout_return, iter);
	server_dbus_layouts(&v41p->recall, iter);
}

void server_dbus_v42_layouts(struct nfsv41_stats *v42p, DBusMessageIter *iter)
{
	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	server_dbus_layouts(&v42p->getdevinfo, iter);
	server_dbus_layouts(&v42p->layout_get, iter);
	server_dbus_layouts(&v42p->layout_commit, iter);
	server_dbus_layouts(&v42p->layout_return, iter);
	server_dbus_layouts(&v42p->recall, iter);
}

/**
 * @brief Report delegation statistics as a struct
 *
 * @param iop   [IN] pointer to xfer op sub-structure of interest
 * @param iter  [IN] interator in reply stream to fill
 */
void server_dbus_delegations(struct deleg_stats *ds, DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;

	gsh_dbus_append_timestamp(iter, &nfs_stats_time);
	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &ds->curr_deleg_grants);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &ds->tot_recalls);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &ds->failed_recalls);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &ds->num_revokes);
	dbus_message_iter_close_container(iter, &struct_iter);
}


#ifdef _USE_NFS3
/**
 * @brief NFSv3 Detailed stats reporting
 */
void server_dbus_v3_full_stats(DBusMessageIter *iter)
{
	DBusMessageIter array_iter, op_iter;
	int op;
	double res = 0.0;
	uint64_t op_counter = 0;
	char *message;

	gsh_dbus_append_timestamp(iter, &v3_full_stats_time);
	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					 "(stttddd)", &array_iter);
	for (op = 1; op < NFSPROC3_COMMIT+1; op++) {
		if (v3_full_stats[op].total) {
			dbus_message_iter_open_container(&array_iter,
							 DBUS_TYPE_STRUCT,
							 NULL, &op_iter);
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_STRING, &optabv3[op].name);
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &v3_full_stats[op].total);
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &v3_full_stats[op].errors);
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &v3_full_stats[op].dups);
			res = (double) v3_full_stats[op].latency.latency /
					v3_full_stats[op].total * 0.000001;
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
			res = (double) v3_full_stats[op].latency.min * 0.000001;
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
			res = (double) v3_full_stats[op].latency.max * 0.000001;
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
			dbus_message_iter_close_container(&array_iter,
							  &op_iter);
			op_counter += v3_full_stats[op].total;
		}
	}
	if (op_counter == 0) {
		message = "None";
		/* insert dummy stats to avoid dbus crash */
		dbus_message_iter_open_container(&array_iter,
						 DBUS_TYPE_STRUCT,
						 NULL, &op_iter);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_STRING, &message);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &op_counter);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &op_counter);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &op_counter);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_close_container(&array_iter,
						  &op_iter);
	} else {
		message = "OK";
	}
	dbus_message_iter_close_container(iter, &array_iter);
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &message);
}
#endif

/**
 * @brief NFSv4 Detailed stats reporting
 */
void server_dbus_v4_full_stats(DBusMessageIter *iter)
{
	DBusMessageIter array_iter, op_iter;
	int op;
	double res = 0.0;
	uint64_t op_counter = 0;
	char *message;

	gsh_dbus_append_timestamp(iter, &v4_full_stats_time);
	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					 "(sttddd)", &array_iter);
	for (op = 1; op < NFS_V42_NB_OPERATION+1; op++) {
		if (v4_full_stats[op].total) {
			dbus_message_iter_open_container(&array_iter,
							 DBUS_TYPE_STRUCT,
							 NULL, &op_iter);
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_STRING, &optabv4[op].name);
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &v4_full_stats[op].total);
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &v4_full_stats[op].errors);
			res = (double) v4_full_stats[op].latency.latency /
					v4_full_stats[op].total * 0.000001;
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
			res = (double) v4_full_stats[op].latency.min * 0.000001;
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
			res = (double) v4_full_stats[op].latency.max * 0.000001;
			dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
			dbus_message_iter_close_container(&array_iter,
							  &op_iter);
			op_counter += v4_full_stats[op].total;
		}
	}
	if (op_counter == 0) {
		message = "None";
		dbus_message_iter_open_container(&array_iter,
						 DBUS_TYPE_STRUCT,
						 NULL, &op_iter);
		/* insert dummy stats to avoid dbus crash */
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_STRING, &message);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &op_counter);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_UINT64, &op_counter);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_append_basic(&op_iter,
				DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_close_container(&array_iter,
						  &op_iter);
	} else {
		message = "OK";
	}
	dbus_message_iter_close_container(iter, &array_iter);
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &message);
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
#ifdef _USE_NFS3
	if (statsp->nfsv3 != NULL) {
		gsh_free(statsp->nfsv3);
		statsp->nfsv3 = NULL;
	}
	if (statsp->mnt != NULL) {
		gsh_free(statsp->mnt);
		statsp->mnt = NULL;
	}
#endif
#ifdef _USE_NLM
	if (statsp->nlm4 != NULL) {
		gsh_free(statsp->nlm4);
		statsp->nlm4 = NULL;
	}
#endif
#ifdef _USE_RQUOTA
	if (statsp->rquota != NULL) {
		gsh_free(statsp->rquota);
		statsp->rquota = NULL;
	}
#endif
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
#ifdef _USE_9P
	if (statsp->_9p != NULL) {
		u8 opc;

		for (opc = 0; opc <= _9P_RWSTAT; opc++) {
			if (statsp->_9p->opcodes[opc] != NULL)
				gsh_free(statsp->_9p->opcodes[opc]);
		}
		gsh_free(statsp->_9p);
		statsp->_9p = NULL;
	}
#endif
}

/**
 * @brief Free statistics storage used for all ops
 *
 * The struct itself is not freed because it is a member
 * of either the client manager struct.
 *
 * @param statsp [IN] pointer to stats to be cleaned
 */

void server_stats_allops_free(struct gsh_clnt_allops_stats *statsp)
{
#ifdef _USE_NFS3
	if (statsp->nfsv3 != NULL) {
		gsh_free(statsp->nfsv3);
		statsp->nfsv3 = NULL;
	}
#endif
	if (statsp->nfsv4 != NULL) {
		gsh_free(statsp->nfsv4);
		statsp->nfsv4 = NULL;
	}
#ifdef _USE_NLM
	if (statsp->nlm4 != NULL) {
		gsh_free(statsp->nlm4);
		statsp->nlm4 = NULL;
	}
#endif
}

#ifdef _USE_NFS3
static void record_v3_full_stats(struct svc_req *req,
			       nsecs_elapsed_t request_time,
			       bool success, bool dup)
{
	uint32_t prog = req->rq_msg.cb_prog;
	uint32_t vers = req->rq_msg.cb_vers;
	uint32_t proc = req->rq_msg.cb_proc;

	if (prog == NFS_program[P_NFS] && vers == NFS_V3) {
		if (proc > NFSPROC3_COMMIT) {
			LogCrit(COMPONENT_DBUS,
				"req->rq_proc is more than COMMIT: %d\n",
				proc);
			return;
		}
		record_op(&v3_full_stats[proc], request_time, success, dup);
	}
}

void reset_v3_full_stats(void)
{
	int op;

	for (op = 1; op < NFSPROC3_COMMIT+1; op++) {
		v3_full_stats[op].total = 0;
		v3_full_stats[op].errors = 0;
		v3_full_stats[op].dups = 0;
		v3_full_stats[op].latency.latency = 0;
		v3_full_stats[op].latency.min = 0;
		v3_full_stats[op].latency.max = 0;
	}
}
#endif

static void record_v4_full_stats(uint32_t proc,
			       nsecs_elapsed_t request_time,
			       bool success)
{
	if (proc > NFS_V42_NB_OPERATION) {
		LogCrit(COMPONENT_DBUS,
			"proc is more than NFS4_OP_WRITE_SAME: %d\n",
			proc);
		return;
	}
	record_op(&v4_full_stats[proc], request_time, success, false);
}

void reset_v4_full_stats(void)
{
	int op;

	for (op = 1; op < NFS_V42_NB_OPERATION+1; op++) {
		v4_full_stats[op].total = 0;
		v4_full_stats[op].errors = 0;
		v4_full_stats[op].dups = 0;
		v4_full_stats[op].latency.latency = 0;
		v4_full_stats[op].latency.min = 0;
		v4_full_stats[op].latency.max = 0;
	}
}

/** @} */
