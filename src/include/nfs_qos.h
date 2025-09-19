/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
 * Author: Deeraj Patil <deeraj.patil@ibm.com>
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
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */
#if ENABLE_QOS

#include <time.h>

#ifdef USE_MONITORING
#include "nfs_metrics.h"
#endif
#define IS_QOS_IO (1 << 0)
#define IS_QOS_IO_READ_BYPASS (1 << 1)
#define IS_QOS_COMPOUND_IO (1 << 3)
#define IS_QOS_IOPS_ACCOUNTED (1 << 4)

#define NON_RATELIMITING_IO 0
#define RATELIMITING_IO 1

/* QOS configuration values for bandwidth and iops */
#define QOS_MIN_BW (1024UL * 32) /* 32 KiBps */
#define QOS_MAX_BW (100UL * 1024 * 1024 * 1024) /* 100GiBps */
#define QOS_DEFAULT_EXPORT_BW (2UL * 1024 * 1024 * 1024) /* 2GiBps */
#define QOS_DEFAULT_CLIENT_BW (2UL * 1024 * 1024 * 1024) /* 2GiBps */

/* A single compound op for read/write will have 4 ops internally.
 * Hence all IOPS are in multiple of 4 */
/* Max Values are derived w.r.t internal OPS timimg calculation supported */
#define QOS_MIN_IOPS (4 * 2) /* 2 compound OPS*/
#define QOS_MAX_IOPS (4 * 4 * 1024 * 100UL) /* 409600 actual read/writes PS */
#define QOS_DEFAULT_EXPORT_IOPS (4 * 1024 * 2UL) /* 4op per MB * GB*2=2iGBps*/
#define QOS_DEFAULT_CLIENT_IOPS (4 * 1024 * 2UL) /* 4op per MB * GB*2=2GiBps*/

#define QOS_MIN_TOKENS (QOS_MIN_BW * 3600) /* i.e 1MB * 3600Sec i.e 3600MB/Hr*/
#define QOS_MAX_TOKENS (UINT64_MAX)
#define QOS_DEFAULT_TOKENS (QOS_MIN_BW * 3600 * 24) /* Min BW * Per day limit */

#define QOS_MIN_TOKENS_REFRESH_TIME (3600) /* PerHr */
#define QOS_MAX_REFRESH_TIME (UINT64_MAX)
#define QOS_DEF_TOKEN_REFRESH_TIME (3600 * 24) /* Per 24 hours */

/* qos_type_supported should be used in config comparisons */
enum qos_type_supported {
	QOS_NOT_ENABLED = 0,
	QOS_PER_EXPORT_ENABLED = 1,
	QOS_PER_CLIENT_ENABLED = 2,
	QOS_PEREXPORT_PERCLIENT_ENABLED = 3
};

/* TASK/IO is suspended by QOS or not */
typedef enum { QOS_TASK_NOT_SUSPENDED, QOS_TASK_SUSPENDED } qos_status_t;

/* qos_class_type should be used while IO processing */
typedef enum { QOS_EXPORT, QOS_CLIENT, QOS_PEPC } qos_class_type_t;

/* Currently only NFS4 read IO and write IO are tapped */
typedef enum { QOS_READ, QOS_WRITE } qos_op_type_t;

struct qos_op_cb_arg {
	/* caller_data is mainly the write_data and read_data ptr*/
	void *caller_data;
	/* Distingiush the IO for ratelimiting IO or token exhausted IO */
	uint32_t ratecontrol;
};

/* Structure used for handling the BW and token exhausted clients */
typedef struct timer_entry {
	/*  list of such timer_entry */
	struct glist_head timer_list;
	/* Callback function to call on timer expiry */
	void (*callback)(void *);
	/* Call back arg : struct qos_op_cb_arg  */
	void *args;
	/*  Expiry time of this IO */
	uint64_t expiry;
	/* Size of the IO, required for BW calculation */
	uint64_t size;
} timer_entry_t;

/* Structure used on token exhausted by client */
typedef struct qos_client_entry {
	/* Client lists used in case of clients exhausted list
	 * i.e entry to hold the list of such clients */
	struct glist_head token_ex_cl;
	/* This client's IO waitlist, which holds timer_entry_t */
	struct glist_head io_waitlist_qos;
	/* current entry/io_waitlist belong to which gsh_client*/
	struct gsh_client *gsh_client;
	SVCXPRT *rq_xprt;
	compound_data_t *data;
	uint32_t num_ios_waiting;
	/*  bit used to disable/enable the xprt recv */
	bool epoll_disabled;
} qos_client_entry_t;

/* Actual struture which holds the accounting.
 * In case of combined, only write accounting structure
 * will be used */
typedef struct qos_bucket {
	/* BW conrtol, io wait queue */
	struct glist_head io_waitlist_qos_bc;
	/* OPS control, io wait queue */
	struct glist_head io_waitlist_qos_iops;

	uint64_t max_bw_allowed;
	uint64_t bw_ldct;
	uint64_t data_consumed;

	uint64_t max_iops_allowed;
	uint64_t iops_ldct;
	uint64_t iops_consumed;

	uint64_t max_available_tokens;
	uint64_t token_ldct;
	uint64_t tokens_consumed;
	uint64_t tokens_renew_time; /*   In useconds */

#ifdef USE_MONITORING
	struct gauge_metric_handle bw_metric_handler;
	struct gauge_metric_handle iops_metric_handler;
	struct gauge_metric_handle tokens_metric_handler;
#endif
	/* IO lock */
	pthread_mutex_t lock;
	uint32_t num_ios_waiting;
} qos_bucket_t;

/* This is a configuration stucture, not used while processing IO */
typedef struct qos_block_config {
	bool enable_qos;

	bool enable_tokens;
	bool enable_bw_control;
	bool enable_iops_control;
	bool enable_ds_control;

	bool combined_rw_bw_control;
	bool combined_rw_token_control;
	bool combined_rw_iops_control;
	enum qos_type_supported qos_type;

	uint64_t max_export_combined_bw;
	uint64_t max_client_combined_bw;
	uint64_t max_export_write_bw;
	uint64_t max_export_read_bw;
	uint64_t max_client_write_bw;
	uint64_t max_client_read_bw;

	uint64_t max_export_combined_iops;
	uint64_t max_client_combined_iops;
	uint64_t max_export_write_iops;
	uint64_t max_export_read_iops;
	uint64_t max_client_write_iops;
	uint64_t max_client_read_iops;

	uint64_t max_export_read_tokens;
	uint64_t max_export_write_tokens;
	uint64_t max_client_read_tokens;
	uint64_t max_client_write_tokens;
	uint64_t export_read_tokens_renew_time;
	uint64_t export_write_tokens_renew_time;
	uint64_t client_read_tokens_renew_time;
	uint64_t client_write_tokens_renew_time;
} qos_block_config_t;

extern qos_block_config_t qos_block_config;
extern struct config_block qos_core;
extern struct qos_block_config *g_qos_config;

typedef struct Qos_Class {
	union {
		struct gsh_export *gsh_export;
		struct gsh_client *gsh_client;
	};
	/*  List of clients attached with this share
	 *  i.e list if struct Qos_Class
	 *  In case of per_export or per_client this is not usable
	 *  only in case of per_export_per_client this is applicable
	 */
	struct glist_head clients;
	/* Entry is used to store the IO's after token exausted
	 * holds struct qos_client_entry */
	struct glist_head client_entries;
	struct qos_bucket rbucket;
	struct qos_bucket wbucket;

	/* lock used for adding/removing clients etc */
	pthread_mutex_t lock;
	/*  Used for waiting IO accounting in case of token exhaust */
	uint32_t num_ios_waiting;

	/* Type indicates structure is associated with EXPORT or CLIENT */
	qos_class_type_t type;
	/* Below are default config values applied */
	bool bw_enabled;
	bool ds_enabled;
	bool iops_enabled;
	bool token_enabled;
	bool combined_rw_bw_control;
	bool combined_rw_iops_control;
	bool combined_rw_token_control;
#ifdef USE_MONITORING
	bool enabled_bw_metric;
	bool enabled_iops_metric;
	bool enabled_tokens_metric;
#endif
} qos_class_t;

void qos_perexport_insert(struct gsh_export *export,
			  struct qos_block_config *qos_block);
void qos_free_mem(void *gsh_ptr, qos_class_type_t class_type);
void qos_drain_bw_ios(qos_class_t *qos_class);
void qos_drain_iops_ios(qos_class_t *qos_class);
qos_class_t *pepc_get_client_from_list(struct glist_head *head,
				       struct gsh_client *gsh_client);
void copy_gsh_qos_conf(struct gsh_export *dest, struct gsh_export *src);
void nfs4_qos_write_cb(void *args);
void nfs4_qos_read_cb(void *args);
void nfs4_qos_compound_cb(void *args);
qos_status_t qos_process(uint64_t size, void *caller_data,
			 compound_data_t *data, qos_op_type_t op_type,
			 bool is_ds);
qos_status_t qos_process_iops(compound_data_t *data);
void qos_init(void);
void shutdown_qos(void);
#endif
