/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "avltree.h"
#include "ganesha_types.h"
#include "ganesha_dbus.h"
#include "client_mgr.h"
#include "export_mgr.h"
#include "server_stats.h"
#include <abstract_atomic.h>

#define NFS_V3_NB_COMMAND 22
#define NFS_V4_NB_COMMAND 2
#define MNT_V1_NB_COMMAND 6
#define MNT_V3_NB_COMMAND 6
#define NLM_V4_NB_OPERATION 5
#define RQUOTA_NB_COMMAND 5
#define NFS_V40_NB_OPERATION 39
#define NFS_V41_NB_OPERATION 58
#define _9P_NB_COMMAND 33

/* Classify protocol ops for stats purposes
 */

typedef enum {
	GENERAL_OP = 0, /* default for array init */
	READ_OP,
	WRITE_OP,
	LAYOUT_OP
} proto_op_type;

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
};


/* latency stats
 */
struct op_latency {
	uint64_t latency;
	uint64_t min;
	uint64_t max;
};

/* basic op counter
 */

struct proto_op {
	uint64_t total;			/* total of any kind */
	uint64_t errors;		/* ! NFS_OK */
	uint64_t dups;			/* detected dup requests */
	struct op_latency latency;	/* either executed ops latency */
	struct op_latency dup_latency;	/* or latency (runtime) to replay */
	struct op_latency queue_latency;/* queue wait time */
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
	struct proto_op gets;
	struct proto_op returns;
	uint64_t recalls;
	uint64_t no_match;
};

/* NFSv3 statistics counters
 */

struct nfsv3_stats {
	struct proto_op cmds;  /* non-I/O ops = cmds - (read+write) */
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
	uint64_t ops_per_compound; /* avg = total / ops_per */
	struct xfer_op read;
	struct xfer_op write;
};

struct nfsv41_stats {
	struct proto_op compounds;
	uint64_t ops_per_compound; /* for size averaging */
	struct xfer_op read;
	struct xfer_op write;
	struct layout_op layouts;
};

struct _9p_stats {
	struct proto_op cmds;  /* non-I/O ops */
	struct xfer_op read;
	struct xfer_op write;
};

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
				  pthread_mutex_t *lock)
{
	if( unlikely(stats->nfsv3 == NULL)) {
		pthread_mutex_lock(lock);
		if(stats->nfsv3 == NULL)
			stats->nfsv3 = gsh_calloc(sizeof(struct nfsv3_stats), 1);
		pthread_mutex_unlock(lock);
	}
	return stats->nfsv3;
}

static struct mnt_stats *get_mnt(struct gsh_stats *stats,
				  pthread_mutex_t *lock)
{
	if( unlikely(stats->mnt == NULL)) {
		pthread_mutex_lock(lock);
		if(stats->mnt == NULL)
			stats->mnt = gsh_calloc(sizeof(struct mnt_stats), 1);
		pthread_mutex_unlock(lock);
	}
	return stats->mnt;
}

static struct nlmv4_stats *get_nlm4(struct gsh_stats *stats,
				  pthread_mutex_t *lock)
{
	if( unlikely(stats->nlm4 == NULL)) {
		pthread_mutex_lock(lock);
		if(stats->nlm4 == NULL)
			stats->nlm4 = gsh_calloc(sizeof(struct nlmv4_stats), 1);
		pthread_mutex_unlock(lock);
	}
	return stats->nlm4;
}

static struct rquota_stats *get_rquota(struct gsh_stats *stats,
				  pthread_mutex_t *lock)
{
	if( unlikely(stats->rquota == NULL)) {
		pthread_mutex_lock(lock);
		if(stats->rquota == NULL)
			stats->rquota = gsh_calloc(sizeof(struct rquota_stats), 1);
		pthread_mutex_unlock(lock);
	}
	return stats->rquota;
}

static struct nfsv40_stats *get_v40(struct gsh_stats *stats,
				  pthread_mutex_t *lock)
{
	if( unlikely(stats->nfsv40 == NULL)) {
		pthread_mutex_lock(lock);
		if(stats->nfsv40 == NULL)
			stats->nfsv40 = gsh_calloc(sizeof(struct nfsv40_stats), 1);
		pthread_mutex_unlock(lock);
	}
	return stats->nfsv40;
}

static struct nfsv41_stats *get_v41(struct gsh_stats *stats,
				  pthread_mutex_t *lock)
{
	if( unlikely(stats->nfsv41 == NULL)) {
		pthread_mutex_lock(lock);
		if(stats->nfsv41 == NULL)
			stats->nfsv41 = gsh_calloc(sizeof(struct nfsv41_stats), 1);
		pthread_mutex_unlock(lock);
	}
	return stats->nfsv41;
}

/* Functions for recording statistics
 */

/**
 * @brief Record latency stats
 *
 * @brief op           [IN] protocol op stats struct
 * @brief request_time [IN] time consumed by request
 * @param qwait_time   [IN] time sitting on queue
 * @param dup          [IN] detected this was a dup request
 */

void record_latency(struct proto_op *op,
		    nsecs_elapsed_t request_time,
		    nsecs_elapsed_t qwait_time,
		    bool dup)
{

	/* dup latency is counted separately */
	if(likely( !dup)) {
		(void)atomic_add_uint64_t(&op->latency.latency, request_time);
		if(op->latency.min == 0L || op->latency.min > request_time)
			(void)atomic_store_uint64_t(&op->latency.min, request_time);
		if(op->latency.max == 0L || op->latency.max < request_time)
			(void)atomic_store_uint64_t(&op->latency.max, request_time);
	} else {
		(void)atomic_add_uint64_t(&op->dup_latency.latency, request_time);
		if(op->dup_latency.min == 0L || op->dup_latency.min > request_time)
			(void)atomic_store_uint64_t(&op->dup_latency.min, request_time);
		if(op->dup_latency.max == 0L || op->dup_latency.max < request_time)
			(void)atomic_store_uint64_t(&op->dup_latency.max, request_time);
	}
	/* record how long it was laying around waiting ... */
	(void)atomic_add_uint64_t(&op->queue_latency.latency, qwait_time);
	if(op->queue_latency.min == 0L || op->queue_latency.min > qwait_time)
		(void)atomic_store_uint64_t(&op->queue_latency.min, qwait_time);
	if(op->queue_latency.max == 0L || op->queue_latency.max < qwait_time)
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

static void record_io(struct xfer_op *iop,
		      size_t requested,
		      size_t transferred,
		      bool success)
{
	(void)atomic_inc_uint64_t(&iop->cmd.total);
	if(success) {
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

static void record_io_stats(struct gsh_stats *gsh_st,
			    pthread_mutex_t *lock,
			    struct req_op_context *req_ctx,
			    size_t requested,
			    size_t transferred,
			    bool success,
			    bool is_write)
{
	struct xfer_op *iop = NULL;

	if(req_ctx->req_type == NFS_REQUEST) {
		if(req_ctx->nfs_vers == NFS_V3) {
			struct nfsv3_stats *sp = get_v3(gsh_st, lock);

			if(sp == NULL)
				return;
			iop = is_write ? &sp->write : &sp->read;
		} else if(req_ctx->nfs_vers == NFS_V4) {
			struct nfsv40_stats *sp = get_v40(gsh_st, lock);

			if(sp == NULL)
				return;
			iop = is_write ? &sp->write : &sp->read;
		} else {
			return;
		}
	} else if (req_ctx->req_type == _9P_REQUEST) {
		/* do its counters sometime */ ;
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

static void record_op(struct proto_op *op,
		      nsecs_elapsed_t request_time,
		      nsecs_elapsed_t qwait_time,
		      bool success,
		      bool dup)
{
	/* count the op */
	(void)atomic_inc_uint64_t(&op->total);
	/* also count it as an error if protocol not happy */
	if( !success)
		(void)atomic_inc_uint64_t(&op->errors);
	if(unlikely(dup))
		(void)atomic_inc_uint64_t(&op->dups);
	record_latency(op, request_time, qwait_time, dup);
}

/**
 * @brief Record NFS V4 compound stats
 */

static void record_nfsv4_op(struct gsh_stats *gsh_st,
			    pthread_mutex_t *lock,
			    int proto_op,
			    int minorversion,
			    nsecs_elapsed_t request_time,
			    nsecs_elapsed_t qwait_time,
			    bool success)
{
	if(minorversion == 0) {
		struct nfsv40_stats *sp = get_v40(gsh_st, lock);

		if(sp == NULL)
			return;
		/* record stuff */
		switch(nfsv40_optype[proto_op]) {
		case READ_OP:
			record_latency(&sp->read.cmd, request_time,
				       qwait_time,
				       false);
			break;
		case WRITE_OP:
			record_latency(&sp->write.cmd, request_time,
				       qwait_time,
				       false);
			break;
		default:
			record_op(&sp->compounds, request_time,
				  qwait_time,
				  success, false);
		}
	} else { /* assume minor == 1 this low in stack */
		struct nfsv41_stats *sp = get_v41(gsh_st, lock);

		if(sp == NULL)
			return;
		/* record stuff */
		switch(nfsv41_optype[proto_op]) {
		case READ_OP:
			record_latency(&sp->read.cmd, request_time,
				       qwait_time,
				       false);
			break;
		case WRITE_OP:
			record_latency(&sp->write.cmd, request_time,
				       qwait_time,
				       false);
			break;
		default:
			record_op(&sp->compounds,
				  request_time,
				  qwait_time,
				  success, false);
		}
	}

}

/**
 * @brief Record NFS V4 compound stats
 */

static void record_compound(struct gsh_stats *gsh_st,
			    pthread_mutex_t *lock,
			    int minorversion,
			    uint64_t num_ops,
			    nsecs_elapsed_t request_time,
			    nsecs_elapsed_t qwait_time,
			    bool success)
{
	if(minorversion == 0) {

		struct nfsv40_stats *sp = get_v40(gsh_st, lock);

		if(sp == NULL)
			return;
		/* record stuff */
		record_op(&sp->compounds, request_time,
			  qwait_time,
			  success, false);
		(void)atomic_add_uint64_t(&sp->ops_per_compound,
					  num_ops);
	} else { /* assume minor == 1 this low in stack */
		struct nfsv41_stats *sp = get_v41(gsh_st, lock);

		if(sp == NULL)
			return;
		/* record stuff */
		record_op(&sp->compounds,
			  request_time,
			  qwait_time,
			  success, false);
		(void)atomic_add_uint64_t(&sp->ops_per_compound,
					  num_ops);
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

static void record_stats(struct gsh_stats *gsh_st,
			 pthread_mutex_t *lock,
			 request_data_t *reqdata,
			 bool success,
			 nsecs_elapsed_t request_time,
			 nsecs_elapsed_t qwait_time,
			 bool dup)
{
	struct svc_req *req = &reqdata->r_u.nfs->req;
	uint32_t proto_op = req->rq_proc;

	if(req->rq_prog == nfs_param.core_param.program[P_NFS]) {
		if(proto_op == 0)
			return;  /* we don't count NULL ops */
		if(req->rq_vers == NFS_V3) {
			struct nfsv3_stats *sp = get_v3(gsh_st, lock);

			if(sp == NULL)
				return;
			/* record stuff */
			switch(nfsv3_optype[proto_op]) {
			case READ_OP:
				record_latency(&sp->read.cmd, request_time,
					  qwait_time,
					  dup);
				break;
			case WRITE_OP:
				record_latency(&sp->write.cmd, request_time,
					  qwait_time,
					  dup);
				break;
			default:
				record_op(&sp->cmds, request_time,
					  qwait_time,
					  success, dup);
			}
		} else {
			/* We don't do V4 here and V2 is toast */
			return;
		}
	} else if(req->rq_prog == nfs_param.core_param.program[P_MNT]) {
		struct mnt_stats *sp = get_mnt(gsh_st, lock);

		if(sp == NULL)
			return;
		/* record stuff */
		if(req->rq_vers == MOUNT_V1)
			record_op(&sp->v1_ops,
				  request_time,
				  qwait_time,
				  success, dup);
		else
			record_op(&sp->v3_ops,
				  request_time, 
				  qwait_time,
				  success, dup);
	} else if(req->rq_prog == nfs_param.core_param.program[P_NLM]) {
		struct nlmv4_stats *sp = get_nlm4(gsh_st, lock);

		if(sp == NULL)
			return;
		/* record stuff */
		record_op(&sp->ops,
			  request_time,
			  qwait_time,
			  success, dup);
	} else if(req->rq_prog == nfs_param.core_param.program[P_RQUOTA]) {
		struct rquota_stats *sp = get_rquota(gsh_st, lock);

		if(sp == NULL)
			return;
		/* record stuff */
		if(req->rq_vers == RQUOTAVERS)
			record_op(&sp->ops,
				  request_time,
				  qwait_time,
				  success, dup);
		else
			record_op(&sp->ext_ops,
				  request_time,
				  qwait_time,
				  success, dup);
	}
}

/**
 * @brief record NFS op finished
 *
 * Called from nfs_rpc_execute at operation/command completion
 */

void server_stats_nfs_done(struct req_op_context *req_ctx,
			   int export_id,
			   request_data_t *reqdata,
			   int rc,
			   bool dup)
{
	struct gsh_client *client = req_ctx->client;
	struct server_stats *server_st;
	struct timespec current_time;
	nsecs_elapsed_t stop_time;

	now(&current_time);
	stop_time = timespec_diff(&ServerBootTime,
				  &current_time);
	server_st = container_of(client, struct server_stats, client);
	record_stats(&server_st->st,
		     &client->lock,
		     reqdata,
		     stop_time - req_ctx->start_time,
		     req_ctx->queue_wait,
		     rc == NFS_REQ_OK,
		     dup);
	(void)atomic_store_uint64_t(&client->last_update, stop_time);
	if( !dup && export_id >= 0) {
		struct gsh_export *exp;
		struct export_stats *exp_st;

		exp = get_gsh_export(export_id, false);
		if(exp == NULL)
			goto out;
		exp_st = container_of(exp, struct export_stats, export);
		record_stats(&exp_st->st,
			     &exp->lock,
			     reqdata,
			     stop_time - req_ctx->start_time,
			     req_ctx->queue_wait,
			     rc == NFS_REQ_OK,
			     dup);
		(void)atomic_store_uint64_t(&exp->last_update, stop_time);
		put_gsh_export(exp);
	}
out:
	return;
}

/**
 * @brief record NFS V4 compound finished
 *
 * Called from nfs4_compound at compound loop completion
 */

void server_stats_nfsv4_op_done(struct req_op_context *req_ctx,
				int export_id,
				int proto_op,
				int minorversion,
				nsecs_elapsed_t start_time,
				bool success)
{
	struct gsh_client *client = req_ctx->client;
	struct server_stats *server_st;
	struct timespec current_time;
	nsecs_elapsed_t stop_time;

	now(&current_time);
	stop_time = timespec_diff(&ServerBootTime,
				  &current_time);
	server_st = container_of(client, struct server_stats, client);
	record_nfsv4_op(&server_st->st,
			&client->lock,
			proto_op,
			minorversion,
			stop_time - start_time,
			req_ctx->queue_wait,
			success);
	(void)atomic_store_uint64_t(&client->last_update, stop_time);
	if(export_id >= 0) {
		struct gsh_export *exp;
		struct export_stats *exp_st;

		exp = get_gsh_export(export_id, false);
		if(exp == NULL)
			goto out;
		exp_st = container_of(exp, struct export_stats, export);
		record_nfsv4_op(&exp_st->st,
				&exp->lock,
				proto_op,
				minorversion,
				stop_time - start_time,
				req_ctx->queue_wait,
				success);
		(void)atomic_store_uint64_t(&exp->last_update, stop_time);
		put_gsh_export(exp);
	}
out:
	return;
}

/**
 * @brief record NFS V4 compound finished
 *
 * Called from nfs4_compound at compound loop completion
 */

void server_stats_compound_done(struct req_op_context *req_ctx,
				int export_id,
				int minorversion,
				int num_ops,
				bool success)
{
	struct gsh_client *client = req_ctx->client;
	struct server_stats *server_st;
	struct timespec current_time;
	nsecs_elapsed_t stop_time;

	now(&current_time);
	stop_time = timespec_diff(&ServerBootTime,
				  &current_time);
	server_st = container_of(client, struct server_stats, client);
	record_compound(&server_st->st,
			&client->lock,
			minorversion,
			num_ops,
			stop_time - req_ctx->start_time,
			req_ctx->queue_wait,
			success);
	(void)atomic_store_uint64_t(&client->last_update, stop_time);
	if(export_id >= 0) {
		struct gsh_export *exp;
		struct export_stats *exp_st;

		exp = get_gsh_export(export_id, false);
		if(exp == NULL)
			goto out;
		exp_st = container_of(exp, struct export_stats, export);
		record_compound(&exp_st->st,
				&exp->lock,
				minorversion,
				num_ops,
				stop_time - req_ctx->start_time,
				req_ctx->queue_wait,
				success);
		(void)atomic_store_uint64_t(&exp->last_update, stop_time);
		put_gsh_export(exp);
	}
out:
	return;
}

/**
 * @brief Record I/O stats for protocol read/write
 *
 * Called from protocol operation/command handlers to record
 * transfers
 */

void server_stats_io_done(struct req_op_context *req_ctx,
			  int export_id,
			  size_t requested,
			  size_t transferred,
			  bool success,
			  bool is_write)
{
	struct server_stats *server_st;

	server_st = container_of(req_ctx->client, struct server_stats, client);
	record_io_stats(&server_st->st,
			&req_ctx->client->lock,
			req_ctx,
			requested, transferred,
			success, is_write);
	if(export_id >= 0) {
		struct gsh_export *exp;
		struct export_stats *exp_st;

		exp = get_gsh_export(export_id, false);
		if(exp == NULL)
			goto out;
		exp_st = container_of(exp, struct export_stats, export);
		record_io_stats(&exp_st->st,
				&exp->lock,
				req_ctx,
				requested, transferred,
				success, is_write);
		put_gsh_export(exp);
	}
out:
	return;
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
 *	bool _9p;
 *      ...
 * }
 *
 * @param name        [IN] name of export or IP address as string
 * @param stats       [IN] pointer to server stats struct
 * @param last_update [IN] elapsed timestamp of last activity
 * @param iter        [IN] iterator to stuff struct into
 */

void server_stats_summary(DBusMessageIter *iter,
			  struct gsh_stats *st)
{
	int stats_available;

	stats_available = !!(st->nfsv3);
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = !!(st->mnt);
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = !!(st->nlm4);
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = !!(st->rquota);
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = !!(st->nfsv40);
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = !!(st->nfsv41);
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
				       &stats_available);
	stats_available = !!(st->_9p);
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
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

static void server_dbus_iostats(struct xfer_op *iop,
			 DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;
	
		dbus_message_iter_open_container(iter,
						 DBUS_TYPE_STRUCT,
						 NULL,
						 &struct_iter);
		dbus_message_iter_append_basic(&struct_iter,
					       DBUS_TYPE_UINT64,
					       &iop->requested);
		dbus_message_iter_append_basic(&struct_iter,
					       DBUS_TYPE_UINT64,
					       &iop->transferred);
		dbus_message_iter_append_basic(&struct_iter,
					       DBUS_TYPE_UINT64,
					       &iop->cmd.total);
		dbus_message_iter_append_basic(&struct_iter,
					       DBUS_TYPE_UINT64,
					       &iop->cmd.errors);
		dbus_message_iter_append_basic(&struct_iter,
					       DBUS_TYPE_UINT64,
					       &iop->cmd.latency.latency);
		dbus_message_iter_append_basic(&struct_iter,
					       DBUS_TYPE_UINT64,
					       &iop->cmd.queue_latency.latency);
		dbus_message_iter_close_container(iter,
						  &struct_iter);
}

void server_dbus_v3_iostats (struct nfsv3_stats *v3p,
			     DBusMessageIter *iter,
			     bool success,
			     char *errormsg)
{
	struct timespec timestamp;

	dbus_status_reply(iter, success, errormsg);
	if(success) {
		now(&timestamp);
		dbus_append_timestamp(iter, &timestamp);
		server_dbus_iostats(&v3p->read, iter);
		server_dbus_iostats(&v3p->write, iter);
	}
}

void server_dbus_v40_iostats (struct nfsv40_stats *v40p,
			      DBusMessageIter *iter,
			      bool success,
			      char *errormsg)
{
	struct timespec timestamp;

	dbus_status_reply(iter, success, errormsg);
	if(success) {
		now(&timestamp);
		dbus_append_timestamp(iter, &timestamp);
		server_dbus_iostats(&v40p->read, iter);
		server_dbus_iostats(&v40p->write, iter);
	}
}

void server_dbus_v41_iostats (struct nfsv41_stats *v41p,
			      DBusMessageIter *iter,
			      bool success,
			      char *errormsg)
{
	struct timespec timestamp;

	dbus_status_reply(iter, success, errormsg);
	if(success) {
		now(&timestamp);
		dbus_append_timestamp(iter, &timestamp);
		server_dbus_iostats(&v41p->read, iter);
		server_dbus_iostats(&v41p->write, iter);
	}
}

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
	if(statsp->nfsv3 != NULL) {
		gsh_free(statsp->nfsv3);
		statsp->nfsv3 = NULL;
	}
	if(statsp->mnt != NULL) {
		gsh_free(statsp->mnt);
		statsp->mnt = NULL;
	}
	if(statsp->nlm4 != NULL) {
		gsh_free(statsp->nlm4);
		statsp->nlm4 = NULL;
	}
	if(statsp->rquota != NULL) {
		gsh_free(statsp->rquota);
		statsp->rquota = NULL;
	}
	if(statsp->nfsv40 != NULL) {
		gsh_free(statsp->nfsv40);
		statsp->nfsv40 = NULL;
	}
	if(statsp->nfsv41 != NULL) {
		gsh_free(statsp->nfsv41);
		statsp->nfsv41 = NULL;
	}
	if(statsp->_9p != NULL) {
		gsh_free(statsp->_9p);
		statsp->_9p = NULL;
	}
}


/** @} */
