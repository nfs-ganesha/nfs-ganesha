/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2013
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup Client host management
 * @{
 */

/**
 * @file client_mgr.h
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Client manager
 */

#ifndef CLIENT_MGR_H
#define CLIENT_MGR_H

#include <pthread.h>
#include <sys/types.h>

#include "avltree.h"
#include "gsh_types.h"

struct gsh_client {
	struct avltree_node node_k;
	pthread_rwlock_t lock;
	int64_t refcnt;
	struct timespec last_update;
	char hostaddr_str[SOCK_NAME_MAX];
	sockaddr_t cl_addrbuf;
};

static inline int64_t inc_gsh_client_refcount(struct gsh_client *client)
{
	return atomic_inc_int64_t(&client->refcnt);
}

void client_pkginit(void);
#ifdef USE_DBUS
void dbus_client_init(void);
#endif
struct gsh_client *get_gsh_client(sockaddr_t *client_ipaddr, bool lookup_only);
void put_gsh_client(struct gsh_client *client);
int foreach_gsh_client(bool(*cb) (struct gsh_client *cl, void *state),
		       void *state);

#endif				/* !CLIENT_MGR_H */
/** @} */
