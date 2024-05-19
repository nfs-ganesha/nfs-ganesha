// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2024 Google LLC
 * Contributor : Dipit Grover  dipit@google.com
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
 * @addtogroup idmapper
 * @{
 */

/**
 * @file idmapper_monitoring.c
 * @brief ID mapping monitoring functions
 */

#include <stddef.h>
#include "idmapper_monitoring.h"
#include "monitoring.h"

typedef enum idmapping_status {
	IDMAPPING_STATUS_SUCCESS = 0,
	IDMAPPING_STATUS_FAILURE,
	IDMAPPING_STATUS_COUNT,
} idmapping_status_t;

/* ID Mapping metrics */
static histogram_metric_handle_t idmapping_user_groups_total;
static histogram_metric_handle_t idmapping_external_request_latency[
	IDMAPPING_OP_COUNT][IDMAPPING_UTILITY_COUNT][IDMAPPING_STATUS_COUNT];
static counter_metric_handle_t idmapping_cache_uses_total[
	IDMAPPING_CACHE_COUNT][IDMAPPING_STATUS_COUNT];
static counter_metric_handle_t idmapping_failures_total[
	IDMAPPING_OP_COUNT][IDMAPPING_UTILITY_COUNT];

/* Distribution of cached-duration of the cache-evicted entries */
static histogram_metric_handle_t
	evicted_entries_cached_duration[IDMAPPING_CACHE_ENTITY_COUNT];

/* 8 buckets in increasing powers of 2 */
static const int64_t groups_buckets[] = { 0, 1, 2, 4, 8, 16, 32, 64, 128, 256,
	512, 1024 };

/* Get status string */
static const char *get_status_name(idmapping_status_t status)
{
	switch (status) {
	case IDMAPPING_STATUS_SUCCESS:
		return "success";
	case IDMAPPING_STATUS_FAILURE:
		return "failure";
	default:
		LogFatal(COMPONENT_IDMAPPER,
			"Unsupported idmapping operation status");
	}
}

/* Get idmapping_op_t string */
static const char *get_op_name(idmapping_op_t op)
{
	switch (op) {
	case IDMAPPING_UID_TO_UIDGID:
		return "uid_to_uidgid";
	case IDMAPPING_UID_TO_GROUPLIST:
		return "uid_to_grouplist";
	case IDMAPPING_USERNAME_TO_UIDGID:
		return "username_to_uidgid";
	case IDMAPPING_USERNAME_TO_GROUPLIST:
		return "username_to_grouplist";
	case IDMAPPING_PRINCIPAL_TO_UIDGID:
		return "principal_to_uidgid";
	case IDMAPPING_PRINCIPAL_TO_GROUPLIST:
		return "principal_to_grouplist";
	case IDMAPPING_MSPAC_TO_SID:
		return "mspac_to_sid";
	case IDMAPPING_SID_TO_UIDGID:
		return "sid_to_uidgid";
	case IDMAPPING_GID_TO_GROUP:
		return "gid_to_group";
	case IDMAPPING_GROUPNAME_TO_GROUP:
		return "groupname_to_group";
	default:
		LogFatal(COMPONENT_IDMAPPER, "Unsupported idmapping op");
	}
}

/* Get idmapping_utility_t string */
static const char *get_utility_name(idmapping_utility_t utility)
{
	switch (utility) {
	case IDMAPPING_PWUTILS:
		return "pwutils";
	case IDMAPPING_NFSIDMAP:
		return "nfsidmap";
	case IDMAPPING_WINBIND:
		return "winbind";
	default:
		LogFatal(COMPONENT_IDMAPPER, "Unsupported idmapping utility");
	}
}

/* Get idmapping_cache_t string */
static const char *get_cache_name(idmapping_cache_t idmapping_cache)
{
	switch (idmapping_cache) {
	case IDMAPPING_UID_TO_USER_CACHE:
		return "uid_to_user";
	case IDMAPPING_USERNAME_TO_USER_CACHE:
		return "username_to_user";
	case IDMAPPING_GSSPRINC_TO_USER_CACHE:
		return "gssprinc_to_user";
	case IDMAPPING_GID_TO_GROUP_CACHE:
		return "gid_to_group";
	case IDMAPPING_GROUPNAME_TO_GROUP_CACHE:
		return "groupname_to_group";
	case IDMAPPING_UID_TO_GROUPLIST_CACHE:
		return "uid_to_grouplist";
	case IDMAPPING_USERNAME_TO_GROUPLIST_CACHE:
		return "username_to_grouplist";
	default:
		LogFatal(COMPONENT_IDMAPPER, "Unsupported idmapping cache");
	}
}

/* Get idmapping_cache_entity_t string */
static const char *get_cache_entity_name(idmapping_cache_entity_t cache_entity)
{
	switch (cache_entity) {
	case IDMAPPING_CACHE_ENTITY_USER:
		return "USER";
	case IDMAPPING_CACHE_ENTITY_GROUP:
		return "GROUP";
	case IDMAPPING_CACHE_ENTITY_USER_GROUPS:
		return "USER_GROUPS";
	case IDMAPPING_CACHE_ENTITY_NEGATIVE_USER:
		return "NEGATIVE_USER";
	case IDMAPPING_CACHE_ENTITY_NEGATIVE_GROUP:
		return "NEGATIVE_GROUP";
	default:
		LogFatal(COMPONENT_IDMAPPER,
			"Unsupported idmapping cache entity: %d", cache_entity);
	}
}

static void register_user_groups_metric(void)
{
	const histogram_buckets_t histogram_buckets = (histogram_buckets_t){
		.buckets = groups_buckets,
		.count = ARRAY_SIZE(groups_buckets)
	};
	const metric_label_t empty_labels[] = {};

	idmapping_user_groups_total =
		monitoring__register_histogram(
			"idmapping__user_groups_total",
			METRIC_METADATA(
				"Total groups per user",
				METRIC_UNIT_NONE),
			empty_labels,
			ARRAY_SIZE(empty_labels),
			histogram_buckets);
}

static void register_external_request_latency_metric(
	idmapping_op_t op,
	idmapping_utility_t utility,
	idmapping_status_t status)
{
	const metric_label_t labels[] = {
		METRIC_LABEL("op", get_op_name(op)),
		METRIC_LABEL("utility", get_utility_name(utility)),
		METRIC_LABEL("status", get_status_name(status))
	};
	idmapping_external_request_latency[op][utility][status] =
		monitoring__register_histogram(
			"idmapping__external_request_latency",
			METRIC_METADATA(
				"Idmapping external request latency",
				METRIC_UNIT_MILLISECOND),
			labels,
			ARRAY_SIZE(labels),
			monitoring__buckets_exp2_compact());
}

static void register_external_request_latency_metrics(void)
{
	for (int i = 0; i < IDMAPPING_OP_COUNT; i++) {
		for (int j = 0; j < IDMAPPING_UTILITY_COUNT; j++) {
			for (int k = 0; k < IDMAPPING_STATUS_COUNT; k++) {
				register_external_request_latency_metric(
					i, j, k);
			}
		}
	}
}

static void register_cache_uses_total_metrics(void)
{
	for (int i = 0; i < IDMAPPING_CACHE_COUNT; i++) {
		for (int j = 0; j < IDMAPPING_STATUS_COUNT; j++) {
			const metric_label_t labels[] = {
				METRIC_LABEL("cache", get_cache_name(i)),
				METRIC_LABEL("hit", get_status_name(j))
			};
			idmapping_cache_uses_total[i][j] =
				monitoring__register_counter(
					"idmapping__cache_uses_total",
					METRIC_METADATA(
						"Total idmapping-cache uses",
						METRIC_UNIT_NONE),
					labels,
					ARRAY_SIZE(labels));
		}
	}
}

static void register_failure_total_metrics(void)
{
	for (int i = 0; i < IDMAPPING_OP_COUNT; i++) {
		for (int j = 0; j < IDMAPPING_UTILITY_COUNT; j++) {
			const metric_label_t labels[] = {
				METRIC_LABEL("op", get_op_name(i)),
				METRIC_LABEL("utility", get_utility_name(j))
			};
			idmapping_failures_total[i][j] =
				monitoring__register_counter(
					"idmapping__failures_total",
					METRIC_METADATA(
						"Total idmapping failures",
						METRIC_UNIT_NONE),
					labels,
					ARRAY_SIZE(labels));
		}
	}
}

static void register_evicted_entries_cache_duration_metrics(void)
{
	for (int i = 0; i < IDMAPPING_CACHE_ENTITY_COUNT; i++) {
		const metric_label_t labels[] = {
			METRIC_LABEL("cache_entity", get_cache_entity_name(i))
		};
		evicted_entries_cached_duration[i] =
			monitoring__register_histogram(
				"idmapping__evicted_entries_cached_duration",
				METRIC_METADATA(
					"Distribution of the time duration "
					"that evicted entries were stored in "
					"the cache",
					METRIC_UNIT_MINUTE),
				labels,
				ARRAY_SIZE(labels),
				monitoring__buckets_exp2_compact());
	}
}

void idmapper_monitoring__init(void)
{
	register_user_groups_metric();
	register_external_request_latency_metrics();
	register_cache_uses_total_metrics();
	register_failure_total_metrics();
	register_evicted_entries_cache_duration_metrics();
}

void idmapper_monitoring__cache_usage(idmapping_cache_t idmapping_cache,
	bool is_cache_hit)
{
	const idmapping_status_t idmapping_status = is_cache_hit ?
		IDMAPPING_STATUS_SUCCESS : IDMAPPING_STATUS_FAILURE;
	monitoring__counter_inc(
		idmapping_cache_uses_total[idmapping_cache][idmapping_status],
		1);
}

void idmapper_monitoring__external_request(
	idmapping_op_t idmapping_op, idmapping_utility_t idmapping_utility,
	bool is_success, const struct timespec *start,
	const struct timespec *end)
{
	const nsecs_elapsed_t resp_time_ns = timespec_diff(start, end);
	const idmapping_status_t idmapping_status = is_success ?
		IDMAPPING_STATUS_SUCCESS : IDMAPPING_STATUS_FAILURE;
	const int64_t resp_time_ms = resp_time_ns / NS_PER_MSEC;

	monitoring__histogram_observe(
		idmapping_external_request_latency[idmapping_op][
			idmapping_utility][idmapping_status],
		resp_time_ms);
}

void idmapper_monitoring__evicted_cache_entity(
	idmapping_cache_entity_t idmapping_cache_entity,
	time_t cached_duration_in_sec)
{
	const time_t cached_duration_in_min = cached_duration_in_sec / 60;

	monitoring__histogram_observe(
		evicted_entries_cached_duration[idmapping_cache_entity],
		cached_duration_in_min);
}

void idmapper_monitoring__failure(
	idmapping_op_t idmapping_op, idmapping_utility_t idmapping_utility)
{
	monitoring__counter_inc(
		idmapping_failures_total[idmapping_op][idmapping_utility], 1);
}

void idmapper_monitoring__user_groups(int num_groups)
{
	monitoring__histogram_observe(
		idmapping_user_groups_total, num_groups);
}

/** @} */
