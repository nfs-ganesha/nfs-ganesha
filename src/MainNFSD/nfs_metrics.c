// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2024 Google LLC
 * Contributor : Yoni Couriel  yonic@google.com
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
 * @file nfs_metrics.c
 * @brief NFS metrics functions
 */

#include "nfs_metrics.h"
#include "common_utils.h"
#include "nfs_convert.h"

#define FOREACH_NFS_STAT4(X) \
	X(NFS4_OK)	\
	X(NFS4ERR_PERM)	\
	X(NFS4ERR_NOENT)	\
	X(NFS4ERR_IO)	\
	X(NFS4ERR_NXIO)	\
	X(NFS4ERR_ACCESS)	\
	X(NFS4ERR_EXIST)	\
	X(NFS4ERR_XDEV)	\
	X(NFS4ERR_NOTDIR)	\
	X(NFS4ERR_ISDIR)	\
	X(NFS4ERR_INVAL)	\
	X(NFS4ERR_FBIG)	\
	X(NFS4ERR_NOSPC)	\
	X(NFS4ERR_ROFS)	\
	X(NFS4ERR_MLINK)	\
	X(NFS4ERR_NAMETOOLONG)	\
	X(NFS4ERR_NOTEMPTY)	\
	X(NFS4ERR_DQUOT)	\
	X(NFS4ERR_STALE)	\
	X(NFS4ERR_BADHANDLE)	\
	X(NFS4ERR_BAD_COOKIE)	\
	X(NFS4ERR_NOTSUPP)	\
	X(NFS4ERR_TOOSMALL)	\
	X(NFS4ERR_SERVERFAULT)	\
	X(NFS4ERR_BADTYPE)	\
	X(NFS4ERR_DELAY)	\
	X(NFS4ERR_SAME)	\
	X(NFS4ERR_DENIED)	\
	X(NFS4ERR_EXPIRED)	\
	X(NFS4ERR_LOCKED)	\
	X(NFS4ERR_GRACE)	\
	X(NFS4ERR_FHEXPIRED)	\
	X(NFS4ERR_SHARE_DENIED)	\
	X(NFS4ERR_WRONGSEC)	\
	X(NFS4ERR_CLID_INUSE)	\
	X(NFS4ERR_RESOURCE)	\
	X(NFS4ERR_MOVED)	\
	X(NFS4ERR_NOFILEHANDLE)	\
	X(NFS4ERR_MINOR_VERS_MISMATCH)	\
	X(NFS4ERR_STALE_CLIENTID)	\
	X(NFS4ERR_STALE_STATEID)	\
	X(NFS4ERR_OLD_STATEID)	\
	X(NFS4ERR_BAD_STATEID)	\
	X(NFS4ERR_BAD_SEQID)	\
	X(NFS4ERR_NOT_SAME)	\
	X(NFS4ERR_LOCK_RANGE)	\
	X(NFS4ERR_SYMLINK)	\
	X(NFS4ERR_RESTOREFH)	\
	X(NFS4ERR_LEASE_MOVED)	\
	X(NFS4ERR_ATTRNOTSUPP)	\
	X(NFS4ERR_NO_GRACE)	\
	X(NFS4ERR_RECLAIM_BAD)	\
	X(NFS4ERR_RECLAIM_CONFLICT)	\
	X(NFS4ERR_BADXDR)	\
	X(NFS4ERR_LOCKS_HELD)	\
	X(NFS4ERR_OPENMODE)	\
	X(NFS4ERR_BADOWNER)	\
	X(NFS4ERR_BADCHAR)	\
	X(NFS4ERR_BADNAME)	\
	X(NFS4ERR_BAD_RANGE)	\
	X(NFS4ERR_LOCK_NOTSUPP)	\
	X(NFS4ERR_OP_ILLEGAL)	\
	X(NFS4ERR_DEADLOCK)	\
	X(NFS4ERR_FILE_OPEN)	\
	X(NFS4ERR_ADMIN_REVOKED)	\
	X(NFS4ERR_CB_PATH_DOWN)	\
	X(NFS4ERR_BADIOMODE)	\
	X(NFS4ERR_BADLAYOUT)	\
	X(NFS4ERR_BAD_SESSION_DIGEST)	\
	X(NFS4ERR_BADSESSION)	\
	X(NFS4ERR_BADSLOT)	\
	X(NFS4ERR_COMPLETE_ALREADY)	\
	X(NFS4ERR_CONN_NOT_BOUND_TO_SESSION)	\
	X(NFS4ERR_DELEG_ALREADY_WANTED)	\
	X(NFS4ERR_BACK_CHAN_BUSY)	\
	X(NFS4ERR_LAYOUTTRYLATER)	\
	X(NFS4ERR_LAYOUTUNAVAILABLE)	\
	X(NFS4ERR_NOMATCHING_LAYOUT)	\
	X(NFS4ERR_RECALLCONFLICT)	\
	X(NFS4ERR_UNKNOWN_LAYOUTTYPE)	\
	X(NFS4ERR_SEQ_MISORDERED)	\
	X(NFS4ERR_SEQUENCE_POS)	\
	X(NFS4ERR_REQ_TOO_BIG)	\
	X(NFS4ERR_REP_TOO_BIG)	\
	X(NFS4ERR_REP_TOO_BIG_TO_CACHE)	\
	X(NFS4ERR_RETRY_UNCACHED_REP)	\
	X(NFS4ERR_UNSAFE_COMPOUND)	\
	X(NFS4ERR_TOO_MANY_OPS)	\
	X(NFS4ERR_OP_NOT_IN_SESSION)	\
	X(NFS4ERR_HASH_ALG_UNSUPP)	\
	X(NFS4ERR_CLIENTID_BUSY)	\
	X(NFS4ERR_PNFS_IO_HOLE)	\
	X(NFS4ERR_SEQ_FALSE_RETRY)	\
	X(NFS4ERR_BAD_HIGH_SLOT)	\
	X(NFS4ERR_DEADSESSION)	\
	X(NFS4ERR_ENCR_ALG_UNSUPP)	\
	X(NFS4ERR_PNFS_NO_LAYOUT)	\
	X(NFS4ERR_NOT_ONLY_OP)	\
	X(NFS4ERR_WRONG_CRED)	\
	X(NFS4ERR_WRONG_TYPE)	\
	X(NFS4ERR_DIRDELEG_UNAVAIL)	\
	X(NFS4ERR_REJECT_DELEG)	\
	X(NFS4ERR_RETURNCONFLICT)	\
	X(NFS4ERR_DELEG_REVOKED)	\
	X(NFS4ERR_PARTNER_NOTSUPP)	\
	X(NFS4ERR_PARTNER_NO_AUTH)	\
	X(NFS4ERR_UNION_NOTSUPP)	\
	X(NFS4ERR_OFFLOAD_DENIED)	\
	X(NFS4ERR_WRONG_LFS)	\
	X(NFS4ERR_BADLABEL)	\
	X(NFS4ERR_OFFLOAD_NO_REQS)	\
	X(NFS4ERR_NOXATTR)	\
	X(NFS4ERR_XATTR2BIG)	\
	X(NFS4ERR_REPLAY)

enum nfsstat4_index {
	NFSSTAT4_INDEX_UNKNOWN_STATUS = 0,
	#define DEFINE_INDEX(name) CONCAT(name, __INDEX),
	FOREACH_NFS_STAT4(DEFINE_INDEX)
	NFSSTAT4_INDEX_LAST,
};

static counter_metric_handle_t rpcs_received_total;
static counter_metric_handle_t rpcs_completed_total;
static gauge_metric_handle_t rpcs_inflight;

/* NFSv4 Operation Metrics */
static histogram_metric_handle_t nfsv4_op_latency[
	NFS4_OP_LAST_ONE][NFSSTAT4_INDEX_LAST];
static counter_metric_handle_t nfsv4_op_count[
	NFS4_OP_LAST_ONE][NFSSTAT4_INDEX_LAST];

/* Compound procedure latency metric */
static histogram_metric_handle_t compound_latency_metric[NFSSTAT4_INDEX_LAST];
/* NFS operations per Compound procedure metric */
static histogram_metric_handle_t compound_ops_count_metric;


static enum nfsstat4_index nfsstat4_to_index(nfsstat4 stat)
{
	switch (stat) {
	#define DEFINE_CASE(name) case name: return CONCAT(name, __INDEX);
	FOREACH_NFS_STAT4(DEFINE_CASE)
	default:
		return NFSSTAT4_INDEX_UNKNOWN_STATUS;
	}
}

static const nfsstat4 index_to_nfsstat4[] = {
	[NFSSTAT4_INDEX_UNKNOWN_STATUS] = (nfsstat4)(-1),
	#define DEFINE_INDEX_TO_STAT(name) [CONCAT(name, __INDEX)] = name,
	FOREACH_NFS_STAT4(DEFINE_INDEX_TO_STAT)
};

static void register_nfsv4_operation_metrics(
	nfs_opnum4 opcode, enum nfsstat4_index statcode_index)
{
	const metric_label_t labels[] = {
		METRIC_LABEL("op", nfsop4_to_str(opcode)),
		METRIC_LABEL("status",
			nfsstat4_to_str(index_to_nfsstat4[statcode_index]))};

	nfsv4_op_latency[opcode][statcode_index] =
		monitoring__register_histogram(
			"nfsv4__op_latency",
			METRIC_METADATA("NFSv4 Operations Latency",
				METRIC_UNIT_MILLISECOND),
			labels,
			ARRAY_SIZE(labels),
			monitoring__buckets_exp2());

	nfsv4_op_count[opcode][statcode_index] =
		monitoring__register_counter(
			"nfsv4__op_count",
			METRIC_METADATA("NFSv4 Operations Counter",
				METRIC_UNIT_NONE),
			labels,
			ARRAY_SIZE(labels));
}

static void register_nfsv4_operations_metrics(void)
{
	for (nfs_opnum4 opcode = 0; opcode < NFS4_OP_LAST_ONE; opcode++) {
		for (enum nfsstat4_index statcode_index = 0;
			statcode_index < NFSSTAT4_INDEX_LAST;
			statcode_index++) {
			register_nfsv4_operation_metrics(
				opcode, statcode_index);
		}
	}
}

static void register_compound_operation_metrics(void)
{
	const metric_label_t empty_labels[] = {};

	compound_ops_count_metric = monitoring__register_histogram(
		"compound__ops_count",
		METRIC_METADATA(
			"Number of Operations in a Compound",
			METRIC_UNIT_NONE
		),
		empty_labels,
		ARRAY_SIZE(empty_labels),
		monitoring__buckets_exp2());

	for (enum nfsstat4_index statcode_index = 0;
		statcode_index < NFSSTAT4_INDEX_LAST; statcode_index++) {
		const metric_label_t labels[] = {
			METRIC_LABEL("status",
				nfsstat4_to_str(
					index_to_nfsstat4[statcode_index]))
		};
		compound_latency_metric[statcode_index] =
			monitoring__register_histogram(
				"compound__latency",
				METRIC_METADATA(
					"Compound Latency Histogram",
					METRIC_UNIT_MILLISECOND
				),
				labels,
				ARRAY_SIZE(labels),
				monitoring__buckets_exp2());
	}
}

void nfs_metrics__nfs4_op_completed(
	nfs_opnum4 opcode, nfsstat4 statcode, nsecs_elapsed_t latency)
{
	monitoring__histogram_observe(
		nfsv4_op_latency[opcode][nfsstat4_to_index(statcode)],
		latency / NS_PER_MSEC);
	monitoring__counter_inc(
		nfsv4_op_count[opcode][nfsstat4_to_index(statcode)], 1);
}

void nfs_metrics__nfs4_compound_completed(
	nfsstat4 statcode, nsecs_elapsed_t latency, int num_ops)
{
	monitoring__histogram_observe(
		compound_latency_metric[nfsstat4_to_index(statcode)],
		latency / NS_PER_MSEC);
	monitoring__histogram_observe(
		compound_ops_count_metric, num_ops);
}

static void register_rpcs_metrics(void)
{
	const metric_label_t labels[] = {};

	rpcs_received_total = monitoring__register_counter(
		"rpcs_received_total",
		METRIC_METADATA("Number of NFS requests received",
			METRIC_UNIT_NONE),
		labels,
		ARRAY_SIZE(labels));
	rpcs_completed_total = monitoring__register_counter(
		"rpcs_completed_total",
		METRIC_METADATA("Number of NFS requests completed",
			METRIC_UNIT_NONE),
		labels,
		ARRAY_SIZE(labels));
	rpcs_inflight = monitoring__register_gauge(
		"rpcs_in_flight",
		METRIC_METADATA(
			"Number of NFS requests received or in flight.",
			METRIC_UNIT_NONE),
		labels,
		ARRAY_SIZE(labels));
}

void nfs_metrics__rpc_received(void)
{
	monitoring__counter_inc(rpcs_received_total, 1);
}

void nfs_metrics__rpc_completed(void)
{
	monitoring__counter_inc(rpcs_completed_total, 1);
}

void nfs_metrics__rpcs_in_flight(int64_t value)
{
	monitoring__gauge_set(rpcs_inflight, value);
}

void nfs_metrics__nfs3_request(uint32_t proc,
				nsecs_elapsed_t request_time,
				nfsstat3 nfs_status,
				export_id_t export_id,
				const char *client_ip)
{
	const char *const version = "nfs3";
	const char *const operation = nfsproc3_to_str(proc);
	const char *const statusLabel = nfsstat3_to_str(nfs_status);

	monitoring__dynamic_observe_nfs_request(
		operation, request_time, version, statusLabel, export_id,
		client_ip);
}

void nfs_metrics__nfs4_request(uint32_t op,
				nsecs_elapsed_t request_time,
				nfsstat4 status,
				export_id_t export_id,
				const char *client_ip)
{
	const char *const version = "nfs4";
	const char *const operation = nfsop4_to_str(op);
	const char *const statusLabel = nfsstat4_to_str(status);

	monitoring__dynamic_observe_nfs_request(
		operation, request_time, version, statusLabel, export_id,
		client_ip);
}

void nfs_metrics__init(void)
{
	register_rpcs_metrics();
	register_nfsv4_operations_metrics();
	register_compound_operation_metrics();
}
