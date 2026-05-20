/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Google Inc., 2025
 * Author: Roy Babayov roybabayov@google.com
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
 * This file contains two types of metrics:
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
 * Naming convention:
 *   For new metrics, please use "<module>__<metric>", for example:
 *   "clients__lease_expire_count"
 *
 * See more:
 *  - https://prometheus.io/docs/concepts/data_model/
 *  - https://prometheus.io/docs/concepts/metric_types/
 */

#ifndef GANESHA_DYNAMIC_METRICS_H
#define GANESHA_DYNAMIC_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "config.h"
#include "gsh_types.h"
#include "monitoring.h"

#ifdef HAVE_PROCPS
#include <proc/readproc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t export_id_t;

#ifdef USE_MONITORING

/* Inits monitoring module and exposes a Prometheus-format HTTP endpoint. */
void dynamic_metrics__init(void);

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

void dynamic_metrics__observe_nfs_request(
	const char *operation, uint64_t op_count, const char *version,
	const char *status_label, export_id_t export_id, const char *path,
	const char *client_ip);

void dynamic_metrics__observe_nfs_request_histogram(
	const char *operation, nsecs_elapsed_t request_time,
	export_id_t export_id, const char *path);

void dynamic_metrics__observe_nfs_io(size_t bytes_requested,
				     size_t bytes_transferred, bool is_write,
				     export_id_t export_id, const char *path,
				     const char *client_ip);

/* MDCache hit rates. */
void dynamic_metrics__mdcache_cache_hit(const char *operation,
					export_id_t export_id);
void dynamic_metrics__mdcache_cache_miss(const char *operation,
					 export_id_t export_id);

void dynamic_metrics_export_info(const char *path, const uint64_t total_size,
				 const uint64_t avail_size,
				 const uint64_t total_files,
				 const uint64_t avail_files);

#ifdef HAVE_PROCPS
void dynamic_metrics__mem_info(proc_t proc_info);
#endif

#else /* USE_MONITORING */

#ifndef UNUSED
#define UNUSED_ATTR __attribute__((unused))
#define UNUSED(...) UNUSED_(__VA_ARGS__)
#define UNUSED_(arg) NOT_USED_##arg UNUSED_ATTR
#endif

static inline void dynamic_metrics__init(void)
{
}

static inline void dynamic_metrics__observe_nfs_request(
	const char *UNUSED(operation), uint64_t UNUSED(op_count),
	const char *UNUSED(version), const char *UNUSED(status_label),
	export_id_t UNUSED(export_id), const char *UNUSED(path),
	const char *UNUSED(client_ip))
{
}

static inline void dynamic_metrics__observe_nfs_request_histogram(
	const char *UNUSED(operation), nsecs_elapsed_t UNUSED(request_time),
	export_id_t UNUSED(export_id), const char *UNUSED(path))
{
}

static inline void dynamic_metrics__observe_nfs_io(
	size_t UNUSED(bytes_requested), size_t UNUSED(bytes_transferred),
	bool UNUSED(is_write), export_id_t UNUSED(export_id),
	const char *UNUSED(path), const char *UNUSED(client_ip))
{
}

static inline void dynamic_metrics__mdcache_cache_hit(
	const char *UNUSED(operation), export_id_t UNUSED(export_id))
{
}

static inline void dynamic_metrics__mdcache_cache_miss(
	const char *UNUSED(operation), export_id_t UNUSED(export_id))
{
}

static inline void dynamic_export_info(const char *UNSED(path),
				       const uint64_t UNUSED(total_size),
				       const uint64_t UNUSED(avail_size),
				       const uint64_t UNUSED(total_files),
				       const uint64_t UNUSED(avail_files))
{
}

#ifdef HAVE_PROCPS
static inline void dynamic_metrics__mem_info(proc_t *UNUSED(proc_info))
{
}
#endif

#endif /* USE_MONITORING */

#ifdef __cplusplus
}
#endif

#endif /* GANESHA_DYNAMIC_METRICS_H */
