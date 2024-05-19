/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Google Inc., 2021
 * Author: Bjorn Leffler leffler@google.com
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * @brief Monitoring library for NFS Ganesha.
 *
 * Monitoring must fail gracefully.
 * Monitoring problems should not affect serving.
 *
 * This file contains two types of metrics:
 * 1. Static metrics - metric definition is known at init time
 * 2. Dynamic metrics - metrics that create new labels during running time, for
 *    example, metrics that have a Client IP Address label.
 *
 * Static metrics (1) are preferable, since the Dynamic metrics (2) affect
 * performance.
 * The Dynamic metrics can be disabled by unsetting Enable_Dynamic_Metrics.
 *
 * We avoid using float/double values since updating them *atomically* also
 * affects performance.
 *
 * Usage:
 *  - Create a static metric during init time:
 *      my_handle = monitoring__register_counter(...);
 *  - Update the metric during running time:
 *      monitoring__counter_inc(my_handle, 1);
 *
 * Naming convention:
 *   For new metrics, please use "<module>__<metric>", for example:
 *   "clients__lease_expire_count"
 *
 * See more:
 *  - https://prometheus.io/docs/concepts/data_model/
 *  - https://prometheus.io/docs/concepts/metric_types/
 */

#ifndef GANESHA_MONITORING_H
#define GANESHA_MONITORING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "config.h"
#include "gsh_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t export_id_t;

/* Metric value units. */
#define METRIC_UNIT_NONE (NULL)
#define METRIC_UNIT_MINUTE ("minute")
#define METRIC_UNIT_SECOND ("sec")
#define METRIC_UNIT_MILLISECOND ("ms")
#define METRIC_UNIT_MICROSECOND ("us")
#define METRIC_UNIT_NANOSECOND ("ns")

#define METRIC_METADATA(DESCRIPTION, UNIT) \
	((metric_metadata_t) { .description = (DESCRIPTION), .unit = (UNIT) })

/* Metric help description */
typedef struct metric_metadata {
	const char *description; /* Helper message */
	const char *unit;        /* Units like: second, byte */
} metric_metadata_t;

/* Label is a dimension in the metric family, for example "operation=GETATTR" */
typedef struct metric_label {
	const char *key;
	const char *value;
} metric_label_t;

#define METRIC_LABEL(KEY, VALUE) \
	((metric_label_t) { .key = (KEY), .value = (VALUE) })

/* Buckets of (a,b,c) mean boundaries of: (-INF,a) [a,b) [b,c) [c, INF) */
typedef struct histogram_buckets {
	const int64_t *buckets;
	uint16_t count;
} histogram_buckets_t;

/* C wrapper for prometheus::Counter<int64_t> pointer */
typedef struct counter_metric_handle {
	void *metric;
} counter_metric_handle_t;

/* C wrapper for prometheus::Gauge<int64_t> pointer */
typedef struct gauge_metric_handle {
	void *metric;
} gauge_metric_handle_t;

/* C wrapper for prometheus::Histogram<int64_t> pointer */
typedef struct histogram_metric_handle {
	void *metric;
} histogram_metric_handle_t;


#ifdef USE_MONITORING

/* Registers and initializes a new static counter metric. */
counter_metric_handle_t monitoring__register_counter(
	const char *name,
	metric_metadata_t metadata,
	const metric_label_t *labels,
	uint16_t num_labels);

/* Registers and initializes a new static gauge metric. */
gauge_metric_handle_t monitoring__register_gauge(
	const char *name,
	metric_metadata_t metadata,
	const metric_label_t *labels,
	uint16_t num_labels);

/* Registers and initializes a new static histogram metric. */
histogram_metric_handle_t monitoring__register_histogram(
	const char *name,
	metric_metadata_t metadata,
	const metric_label_t *labels,
	uint16_t num_labels,
	histogram_buckets_t buckets);

/* Increments counter metric by value. */
void monitoring__counter_inc(counter_metric_handle_t, int64_t val);

/* Increments gauge metric by value. */
void monitoring__gauge_inc(gauge_metric_handle_t, int64_t val);

/* Decrements gauge metric by value. */
void monitoring__gauge_dec(gauge_metric_handle_t, int64_t val);

/* Sets gauge metric value. */
void monitoring__gauge_set(gauge_metric_handle_t, int64_t val);

/* Observes a histogram metric value */
void monitoring__histogram_observe(histogram_metric_handle_t, int64_t val);

/* Returns default exp2 histogram buckets. */
histogram_buckets_t monitoring__buckets_exp2(void);

/* Returns compact exp2 histogram buckets (fewer compared to default). */
histogram_buckets_t monitoring__buckets_exp2_compact(void);

/* Allow FSALs to register a human readable label used for per-export metrics.
 * The default label (if the FSAL doesn't set one) is "exportid=<fsid_major>".
 */
void monitoring_register_export_label(export_id_t export_id, const char *label);

/* Init monitoring export at TCP port <port>. */
void monitoring_init(const uint16_t port);

/*
 * The following two functions generate the following metrics,
 * exported both as total and per export.
 *
 * - Total request count.
 * - Total request count by success / failure status.
 * - Total bytes sent.
 * - Total bytes received.
 * - Request size in bytes as histogram.
 * - Response size in bytes as histogram.
 * - Latency in ms as histogram.
 */


void monitoring__dynamic_observe_nfs_request(
			      const char *operation,
			      nsecs_elapsed_t request_time,
			      const char *version,
			      const char *status_label,
			      export_id_t export_id,
			      const char *client_ip);

void monitoring__dynamic_observe_nfs_io(
			size_t bytes_requested,
			size_t bytes_transferred,
			bool success,
			bool is_write,
			export_id_t export_id,
			const char *client_ip);

/* MDCache hit rates. */
void monitoring__dynamic_mdcache_cache_hit(
				const char *operation,
				export_id_t export_id);
void monitoring__dynamic_mdcache_cache_miss(
				const char *operation,
				export_id_t export_id);

/* In flight RPC stats. */
void monitoring_rpc_received(void);
void monitoring_rpc_completed(void);
void monitoring_rpcs_in_flight(const uint64_t value);


#else  /* USE_MONITORING */


/** The empty implementations below enable using monitoring functions
 * conveniently without wrapping with USE_MONITORING condition each time. */

/* Wraps an expression to avoid *-unused warnings */
#define UNUSED_EXPR(x) ((void)(x))

#define monitoring__register_counter(			\
	name, metadata, labels, num_labels)		\
	({						\
		UNUSED_EXPR(name);			\
		UNUSED_EXPR(metadata);			\
		UNUSED_EXPR(labels);			\
		UNUSED_EXPR(num_labels);		\
		(counter_metric_handle_t) { 0 };	\
	})
#define monitoring__register_gauge(			\
	name, metadata, labels, num_labels)		\
	({						\
		UNUSED_EXPR(name);			\
		UNUSED_EXPR(metadata);			\
		UNUSED_EXPR(labels);			\
		UNUSED_EXPR(num_labels);		\
		(gauge_metric_handle_t) { 0 };		\
	})
#define monitoring__register_histogram(			\
	name, metadata, labels, num_labels, buckets)	\
	({						\
		UNUSED_EXPR(name);			\
		UNUSED_EXPR(metadata);			\
		UNUSED_EXPR(labels);			\
		UNUSED_EXPR(num_labels);		\
		UNUSED_EXPR(buckets);			\
		(histogram_metric_handle_t) { 0 };	\
	})
#define monitoring__counter_inc(metric, value)		\
	({ UNUSED_EXPR(metric); UNUSED_EXPR(value); })
#define monitoring__gauge_inc(metric, value)		\
	({ UNUSED_EXPR(metric); UNUSED_EXPR(value); })
#define monitoring__gauge_dec(metric, value)		\
	({ UNUSED_EXPR(metric); UNUSED_EXPR(value); })
#define monitoring__gauge_set(metric, value)		\
	({ UNUSED_EXPR(metric); UNUSED_EXPR(value); })
#define monitoring__histogram_observe(metric, value)	\
	({ UNUSED_EXPR(metric); UNUSED_EXPR(value); })
#define monitoring__buckets_exp2()			\
	((histogram_buckets_t) { 0 })
#define monitoring__buckets_exp2_compact()		\
	((histogram_buckets_t) { 0 })
#define monitoring__init(port) ({ UNUSED_EXPR(port); })
#define monitoring_register_export_label(export_id, label) \
	({ UNUSED_EXPR(export_id); UNUSED_EXPR(label); })
#define monitoring__dynamic_observe_nfs_request(	\
	operation, request_time, version, status_label, export_id, client_ip) \
	({						\
		UNUSED_EXPR(operation);			\
		UNUSED_EXPR(request_time);		\
		UNUSED_EXPR(version);			\
		UNUSED_EXPR(status_label);		\
		UNUSED_EXPR(export_id);			\
		UNUSED_EXPR(client_ip);			\
	})
#define monitoring__dynamic_observe_nfs_io(		\
	bytes_requested, bytes_transferred, success,	\
	is_write, export_id, client_ip)			\
	({						\
		UNUSED_EXPR(bytes_requested);		\
		UNUSED_EXPR(bytes_transferred);		\
		UNUSED_EXPR(success);			\
		UNUSED_EXPR(is_write);			\
		UNUSED_EXPR(export_id);			\
		UNUSED_EXPR(client_ip);			\
	})
#define monitoring__dynamic_mdcache_cache_hit(		\
	operation, export_id)				\
	({ UNUSED_EXPR(operation); UNUSED_EXPR(export_id); })
#define monitoring__dynamic_mdcache_cache_miss(		\
	operation, export_id)				\
	({ UNUSED_EXPR(operation); UNUSED_EXPR(export_id); })

#endif /* USE_MONITORING */

#ifdef __cplusplus
}
#endif

#endif   /* GANESHA_MONITORING_H */
