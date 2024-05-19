/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 * @defgroup idmapper ID Mapper
 *
 * The ID Mapper module provides mapping between numerical user and
 * group IDs and NFSv4 style owner and group strings.
 *
 * @{
 */

/**
 * @file idmapper_monitoring.h
 * @brief ID mapping monitoring functions
 */

#ifndef IDMAPPER_MONITORING_H
#define IDMAPPER_MONITORING_H

#include <stdbool.h>
#include <time.h>
#include "common_utils.h"

typedef enum idmapping_utility {
	IDMAPPING_PWUTILS = 0,
	IDMAPPING_NFSIDMAP,
	IDMAPPING_WINBIND,
	IDMAPPING_UTILITY_COUNT,
} idmapping_utility_t;

typedef enum idmapping_op {
	IDMAPPING_UID_TO_UIDGID = 0,
	IDMAPPING_UID_TO_GROUPLIST,
	IDMAPPING_USERNAME_TO_UIDGID,
	IDMAPPING_USERNAME_TO_GROUPLIST,
	IDMAPPING_PRINCIPAL_TO_UIDGID,
	IDMAPPING_PRINCIPAL_TO_GROUPLIST,
	IDMAPPING_MSPAC_TO_SID,
	IDMAPPING_SID_TO_UIDGID,
	IDMAPPING_GID_TO_GROUP,
	IDMAPPING_GROUPNAME_TO_GROUP,
	IDMAPPING_OP_COUNT,
} idmapping_op_t;

typedef enum idmapping_cache {
	IDMAPPING_UID_TO_USER_CACHE = 0,
	IDMAPPING_USERNAME_TO_USER_CACHE,
	IDMAPPING_GSSPRINC_TO_USER_CACHE,
	IDMAPPING_GID_TO_GROUP_CACHE,
	IDMAPPING_GROUPNAME_TO_GROUP_CACHE,
	IDMAPPING_UID_TO_GROUPLIST_CACHE,
	IDMAPPING_USERNAME_TO_GROUPLIST_CACHE,
	IDMAPPING_CACHE_COUNT,
} idmapping_cache_t;

typedef enum idmapping_cache_entity {
	IDMAPPING_CACHE_ENTITY_USER = 0,
	IDMAPPING_CACHE_ENTITY_GROUP,
	IDMAPPING_CACHE_ENTITY_USER_GROUPS,
	IDMAPPING_CACHE_ENTITY_NEGATIVE_USER,
	IDMAPPING_CACHE_ENTITY_NEGATIVE_GROUP,
	IDMAPPING_CACHE_ENTITY_COUNT,
} idmapping_cache_entity_t;

/**
 * @brief Registers all idmapping metrics
 */
void idmapper_monitoring__init(void);

/**
 * @brief Updates idmapping external latency metric
 */
void idmapper_monitoring__external_request(
	idmapping_op_t, idmapping_utility_t, bool is_success,
	const struct timespec *start, const struct timespec *end);

/**
 * @brief Updates idmapping cache usage metric
 */
void idmapper_monitoring__cache_usage(idmapping_cache_t, bool is_cache_hit);

/**
 * @brief Updates idmapping failure metric
 */
void idmapper_monitoring__failure(idmapping_op_t, idmapping_utility_t);

/**
 * @brief Updates idmapping metric to count user groups
 */
void idmapper_monitoring__user_groups(int num_groups);

/**
 * @brief Updates idmapping metric to record cached-duration of the
 * cache-evicted entries
 */
void idmapper_monitoring__evicted_cache_entity(
	idmapping_cache_entity_t, time_t cached_duration_in_sec);

#endif				/* IDMAPPER_MONITORING_H */
/** @} */
