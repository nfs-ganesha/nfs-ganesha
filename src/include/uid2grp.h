/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file idmapper.c
 * @brief Id mapping functions
 */

#ifndef UID2GRP_H
#define UID2GRP_H
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include "gsh_rpc.h"
#include "gsh_types.h"

/**
 * @brief Shared between idmapper.c and uid2grp_cache.c.  If you
 * aren't in idmapper.c, leave these symbols alone.
 *
 * @{
 */
typedef struct group_data {
	uid_t uid;
	struct gsh_buffdesc uname;
	gid_t gid;
	time_t epoch;
	int nbgroups;
	unsigned int refcount;
	pthread_mutex_t lock;
	gid_t *groups;
} group_data_t;

extern pthread_rwlock_t uid2grp_user_lock;
extern sem_t uid2grp_sem;

void uid2grp_cache_init(void);

void uid2grp_add_user(struct group_data *);
bool uid2grp_lookup_by_uname(const struct gsh_buffdesc *, uid_t *,
			     struct group_data **);
bool uid2grp_lookup_by_uid(const uid_t, struct group_data **);

void uid2grp_remove_by_uname(const struct gsh_buffdesc *);
void uid2grp_remove_by_uid(const uid_t);

void uid2grp_clear_cache(void);

bool uid2grp(uid_t uid, struct group_data **);
bool name2grp(const struct gsh_buffdesc *name, struct group_data **gdata);
void uid2grp_unref(struct group_data *gdata);
void uid2grp_hold_group_data(struct group_data *);
void uid2grp_release_group_data(struct group_data *);

#endif				/* UID2GRP_H */
/** @} */
