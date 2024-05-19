/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 * @file connection_manager_metrics.h
 * @author Yoni Couriel <yonic@google.com>
 * @brief Metrics for the Connection Manager module
 */

#include "connection_manager.h"
#include "monitoring.h"

typedef struct connection_manager__metrics_t {
	gauge_metric_handle_t clients[CONNECTION_MANAGER__CLIENT_STATE__LAST];
	histogram_metric_handle_t connection_started_latencies
		[CONNECTION_MANAGER__CONNECTION_STARTED__LAST];
	histogram_metric_handle_t
		drain_local_client_latencies[CONNECTION_MANAGER__DRAIN__LAST];
} connection_manager__metrics_t;

void connection_manager_metrics__init(void);

void connection_manager_metrics__client_state_inc(
	enum connection_manager__client_state_t);

void connection_manager_metrics__client_state_dec(
	enum connection_manager__client_state_t);

void connection_manager_metrics__connection_started_done(
	enum connection_manager__connection_started_t,
	const struct timespec *start_time);

void connection_manager_metrics__drain_local_client_done(
	enum connection_manager__drain_t, const struct timespec *start_time);
