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
 * @file connection_manager_metrics.c
 * @author Yoni Couriel <yonic@google.com>
 * @brief Metrics for the Connection Manager module
 */

#include "connection_manager_metrics.h"

static connection_manager__metrics_t metrics = {0};

static const char *
stringify_client_state(enum connection_manager__client_state_t client_state)
{
	switch (client_state) {
	case CONNECTION_MANAGER__CLIENT_STATE__DRAINED:
		return "DRAINED";
	case CONNECTION_MANAGER__CLIENT_STATE__ACTIVATING:
		return "ACTIVATING";
	case CONNECTION_MANAGER__CLIENT_STATE__ACTIVE:
		return "ACTIVE";
	case CONNECTION_MANAGER__CLIENT_STATE__DRAINING:
		return "DRAINING";
	default:
		LogFatal(COMPONENT_XPRT, "Unknown client state: %d",
			client_state);
	}
}

static const char *stringify_connection_started_result(
	enum connection_manager__connection_started_t result)
{
	switch (result) {
	case CONNECTION_MANAGER__CONNECTION_STARTED__ALLOW:
		return "ALLOW";
	case CONNECTION_MANAGER__CONNECTION_STARTED__DROP:
		return "DROP";
	default:
		LogFatal(COMPONENT_XPRT,
			"Unknown connection stated result: %d", result);
	}
}

static const char *
stringify_drain_result(enum connection_manager__drain_t result)
{
	switch (result) {
	case CONNECTION_MANAGER__DRAIN__SUCCESS:
		return "SUCCESS";
	case CONNECTION_MANAGER__DRAIN__SUCCESS_NO_CONNECTIONS:
		return "SUCCESS_NO_CONNECTIONS";
	case CONNECTION_MANAGER__DRAIN__FAILED:
		return "FAILED";
	case CONNECTION_MANAGER__DRAIN__FAILED_TIMEOUT:
		return "FAILED_TIMEOUT";
	default:
		LogFatal(COMPONENT_XPRT, "Unknown drain result: %d", result);
	}
}

static void register_clients_metrics(void)
{
	for (uint32_t state = 0; state < ARRAY_SIZE(metrics.clients); state++) {
		const metric_label_t labels[] = {METRIC_LABEL(
			"state", stringify_client_state(
				(enum connection_manager__client_state_t)state))
		};
		metrics.clients[state] = monitoring__register_gauge(
			"connection_manager__clients",
			METRIC_METADATA(
				"Connection Manager Clients per State",
				METRIC_UNIT_NONE),
			labels, ARRAY_SIZE(labels));
	}
}

static void register_connection_started_latencies_metrics(void)
{
	for (uint32_t result = 0;
		result < ARRAY_SIZE(metrics.connection_started_latencies);
		result++) {
		const metric_label_t labels[] = {METRIC_LABEL(
			"result", stringify_connection_started_result(
			(enum connection_manager__connection_started_t)result))
		};
		metrics.connection_started_latencies[result] =
			monitoring__register_histogram(
			"connection_manager__connection_started_latencies",
			METRIC_METADATA(
				"Connection Manager Connection Started "
				"Latencies per Result",
				METRIC_UNIT_MILLISECOND),
			labels,
			ARRAY_SIZE(labels),
			monitoring__buckets_exp2());
	}
}

static void register_drain_local_client_latencies_metrics(void)
{
	for (uint32_t result = 0;
		result < ARRAY_SIZE(metrics.drain_local_client_latencies);
		result++) {
		const metric_label_t labels[] = {METRIC_LABEL(
			"result", stringify_drain_result(
				(enum connection_manager__drain_t)result))};
		metrics.drain_local_client_latencies[result] =
			monitoring__register_histogram(
			"connection_manager__drain_local_client_latencies",
			METRIC_METADATA(
				"Connection Manager Drain Local Client "
				"Latencies per Result",
				METRIC_UNIT_MILLISECOND),
			labels,
			ARRAY_SIZE(labels),
			monitoring__buckets_exp2());
	}
}

void connection_manager_metrics__init(void)
{
	register_clients_metrics();
	register_connection_started_latencies_metrics();
	register_drain_local_client_latencies_metrics();
}

static inline int64_t get_latency_ms(const struct timespec *start_time)
{
	struct timespec current_time;

	now(&current_time);
	return timespec_diff(start_time, &current_time) / NS_PER_MSEC;
}

void connection_manager_metrics__client_state_inc(
	enum connection_manager__client_state_t state)
{
	monitoring__gauge_inc(metrics.clients[state], 1);
}

void connection_manager_metrics__client_state_dec(
	enum connection_manager__client_state_t state)
{
	monitoring__gauge_dec(metrics.clients[state], 1);
}

void connection_manager_metrics__connection_started_done(
	enum connection_manager__connection_started_t result,
	const struct timespec *start_time)
{
	monitoring__histogram_observe(
		metrics.connection_started_latencies[result],
		get_latency_ms(start_time));
}

void connection_manager_metrics__drain_local_client_done(
	enum connection_manager__drain_t result,
	const struct timespec *start_time)
{
	monitoring__histogram_observe(
		metrics.drain_local_client_latencies[result],
		get_latency_ms(start_time));
}
