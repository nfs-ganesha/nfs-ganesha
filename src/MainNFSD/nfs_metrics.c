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

gauge_metric_handle_t ganesha_uptime_info;
static gauge_metric_handle_t ganesha_build_info;

/* NFSv4 Operation Metrics */
static histogram_metric_handle_t nfsv4_op_latency[NFS4_OP_LAST_ONE]
						 [NFSSTAT4_INDEX_LAST];
static counter_metric_handle_t nfsv4_op_count[NFS4_OP_LAST_ONE]
					     [NFSSTAT4_INDEX_LAST];

/* Compound procedure latency metric */
static histogram_metric_handle_t compound_latency_metric[NFSSTAT4_INDEX_LAST];
/* NFS operations per Compound procedure metric */
static histogram_metric_handle_t compound_ops_count_metric;
static counter_metric_handle_t dropped_gss_requests_count;

enum nfsstat4_index nfsstat4_to_index(nfsstat4 stat)
{
	switch (stat) {
#define DEFINE_CASE(name) \
	case name:        \
		return CONCAT(name, __INDEX);
		FOREACH_NFS_STAT4(DEFINE_CASE)
	default:
		return NFSSTAT4_INDEX_UNKNOWN_STATUS;
	}
}

const nfsstat4 index_to_nfsstat4[] = {
	[NFSSTAT4_INDEX_UNKNOWN_STATUS] = (nfsstat4)(-1),
#define DEFINE_INDEX_TO_STAT(name) [CONCAT(name, __INDEX)] = name,
	FOREACH_NFS_STAT4(DEFINE_INDEX_TO_STAT)
};

void register_ganesha_info_metrics(const char *server_scope)
{
	const metric_label_t build_labels[] = {
		METRIC_LABEL("GANESHA_VERSION", _GIT_DESCRIBE),
		METRIC_LABEL("BUILT_TIME", __DATE__ " " __TIME__),
		METRIC_LABEL("SERVER_SCOPE", server_scope)
	};
	const metric_label_t labels[] = {};

	ganesha_build_info = monitoring__register_gauge(
		"ganesha_build_info",
		METRIC_METADATA("Current ganesha build info", METRIC_UNIT_NONE),
		build_labels, ARRAY_SIZE(build_labels));

	monitoring__gauge_set(ganesha_build_info, 1);
	ganesha_uptime_info = monitoring__register_gauge(
		"ganesha_uptime_seconds",
		METRIC_METADATA("No.of minutes ganesha has been running",
				METRIC_UNIT_SECOND),
		labels, ARRAY_SIZE(labels));
}
static void register_nfsv4_operation_metrics(nfs_opnum4 opcode,
					     enum nfsstat4_index statcode_index)
{
	const metric_label_t labels[] = {
		METRIC_LABEL("op", nfsop4_to_str(opcode)),
		METRIC_LABEL("status",
			     nfsstat4_to_str(index_to_nfsstat4[statcode_index]))
	};

	nfsv4_op_latency[opcode][statcode_index] =
		monitoring__register_histogram(
			"nfsv4__op_latency",
			METRIC_METADATA("NFSv4 Operations Latency",
					METRIC_UNIT_MILLISECOND),
			labels, ARRAY_SIZE(labels), monitoring__buckets_exp2());

	nfsv4_op_count[opcode][statcode_index] = monitoring__register_counter(
		"nfsv4__op_count",
		METRIC_METADATA("NFSv4 Operations Counter", METRIC_UNIT_NONE),
		labels, ARRAY_SIZE(labels));
}

static void register_nfsv4_operations_metrics(void)
{
	for (nfs_opnum4 opcode = 0; opcode < NFS4_OP_LAST_ONE; opcode++) {
		for (enum nfsstat4_index statcode_index = 0;
		     statcode_index < NFSSTAT4_INDEX_LAST; statcode_index++) {
			register_nfsv4_operation_metrics(opcode,
							 statcode_index);
		}
	}
}

static void register_dropped_gss_requests_count_metric(void)
{
	const metric_label_t labels[] = {};

	dropped_gss_requests_count = monitoring__register_counter(
		"nfsv4__dropped_gss_requests_count",
		METRIC_METADATA("Number of dropped rpcsec_gss requests",
				METRIC_UNIT_NONE),
		labels, ARRAY_SIZE(labels));
}

static void register_compound_operation_metrics(void)
{
	const metric_label_t empty_labels[] = {};

	compound_ops_count_metric = monitoring__register_histogram(
		"compound__ops_count",
		METRIC_METADATA("Number of Operations in a Compound",
				METRIC_UNIT_NONE),
		empty_labels, ARRAY_SIZE(empty_labels),
		monitoring__buckets_exp2());

	for (enum nfsstat4_index statcode_index = 0;
	     statcode_index < NFSSTAT4_INDEX_LAST; statcode_index++) {
		const metric_label_t labels[] = { METRIC_LABEL(
			"status",
			nfsstat4_to_str(index_to_nfsstat4[statcode_index])) };
		compound_latency_metric[statcode_index] =
			monitoring__register_histogram(
				"compound__latency",
				METRIC_METADATA("Compound Latency Histogram",
						METRIC_UNIT_MILLISECOND),
				labels, ARRAY_SIZE(labels),
				monitoring__buckets_exp2());
	}
}

void nfs_metrics__nfs4_op_completed(nfs_opnum4 opcode, nfsstat4 statcode,
				    nsecs_elapsed_t latency)
{
	monitoring__histogram_observe(
		nfsv4_op_latency[opcode][nfsstat4_to_index(statcode)],
		latency / NS_PER_MSEC);
	monitoring__counter_inc(
		nfsv4_op_count[opcode][nfsstat4_to_index(statcode)], 1);
}

void nfs_metrics__gss_request_dropped(void)
{
	monitoring__counter_inc(dropped_gss_requests_count, 1);
}

void nfs_metrics__nfs4_compound_completed(nfsstat4 statcode,
					  nsecs_elapsed_t latency, int num_ops)
{
	monitoring__histogram_observe(
		compound_latency_metric[nfsstat4_to_index(statcode)],
		latency / NS_PER_MSEC);
	monitoring__histogram_observe(compound_ops_count_metric, num_ops);
}

#ifdef _USE_NFS3
void nfs_metrics__nfs3_request(uint32_t proc, nsecs_elapsed_t request_time,
			       enum nfs_req_result result, nfsstat3 nfs_status,
			       export_id_t export_id, const char *path,
			       const char *client_ip)
{
	const char *const version = "nfs3";
	const char *const operation = nfsproc3_to_str(proc);
	const char *const status_label =
		result == NFS_REQ_OK ? nfsstat3_to_str(nfs_status)
				     : nfs_req_result_to_str(result);
	dynamic_metrics__observe_nfs_request(operation, 0, version,
					     status_label, export_id, path,
					     client_ip);
}
#endif

void nfs_metrics__nfs4_request(uint32_t op, uint64_t op_count,
			       nsecs_elapsed_t request_time, nfsstat4 status,
			       export_id_t export_id, const char *path,
			       const char *client_ip)
{
	const char *const version = "nfs4";
	const char *const operation = nfsop4_to_str(op);
	const char *const status_label = nfsstat4_to_str(status);

	dynamic_metrics__observe_nfs_request(operation, op_count, version,
					     status_label, export_id, path,
					     client_ip);
}

void nfs_metrics__init(void)
{
	register_nfsv4_operations_metrics();
	register_dropped_gss_requests_count_metric();
	register_compound_operation_metrics();
}
