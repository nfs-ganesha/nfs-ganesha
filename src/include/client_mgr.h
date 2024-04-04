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
#include "gsh_rpc.h"
#include "cidr.h"
#include "sal_shared.h"
#include "connection_manager.h"

struct gsh_client {
	struct avltree_node node_k;
	pthread_rwlock_t client_lock;
	int64_t refcnt;
	struct timespec last_update;
	char hostaddr_str[SOCK_NAME_MAX];
	sockaddr_t cl_addrbuf;
	uint64_t state_stats[STATE_TYPE_MAX]; /* state stats for this client */
	connection_manager__client_t connection_manager;
};

static inline int64_t inc_gsh_client_refcount(struct gsh_client *client)
{
	return atomic_inc_int64_t(&client->refcnt);
}

static inline int64_t inc_gsh_client_state_stats(struct gsh_client *client,
						 enum state_type state_type)
{
	return atomic_inc_uint64_t(&client->state_stats[state_type]);
}

static inline int64_t dec_gsh_client_state_stats(struct gsh_client *client,
						 enum state_type state_type)
{
	return atomic_dec_uint64_t(&client->state_stats[state_type]);
}

void client_pkginit(void);
#ifdef USE_DBUS
void dbus_client_init(void);
#endif
struct gsh_client *get_gsh_client(sockaddr_t *client_ipaddr, bool lookup_only);
void put_gsh_client(struct gsh_client *client);
int foreach_gsh_client(bool(*cb) (struct gsh_client *cl, void *state),
		       void *state);

enum exportlist_client_type {
	PROTO_CLIENT = 0,
	NETWORK_CLIENT = 1,
	NETGROUP_CLIENT = 2,
	WILDCARDHOST_CLIENT = 3,
	GSSPRINCIPAL_CLIENT = 4,
	MATCH_ANY_CLIENT = 5,
	BAD_CLIENT = 6
};

struct base_client_entry {
	struct glist_head cle_list;
	enum exportlist_client_type type;
	union {
		struct {
			CIDR *cidr;
		} network;
		struct {
			char *netgroupname;
		} netgroup;
		struct {
			char *wildcard;
		} wildcard;
		struct {
			char *princname;
		} gssprinc;
	} client;
};

int StrClient(struct display_buffer *dspbuf, struct base_client_entry *client);

void LogClientListEntry(enum log_components component,
			log_levels_t level,
			int line,
			const char *func,
			const char *tag,
			struct base_client_entry *entry);

#define LogMidDebug_ClientListEntry(component, tag, cli) \
	LogClientListEntry(component, NIV_MID_DEBUG, \
			   __LINE__, (char *) __func__, tag, cli)

typedef void (client_free_func) (struct base_client_entry *client);

void FreeClientList(struct glist_head *clients, client_free_func free_func);

struct base_client_entry *client_match(enum log_components component,
				       const char *str,
				       sockaddr_t *hostaddr,
				       struct glist_head *clients);

typedef void * (client_list_entry_allocator_t) (void);

typedef void (client_list_entry_filler_t) (struct base_client_entry *client,
					   void *private_data);

int add_client(enum log_components component,
	       struct glist_head *client_list,
	       const char *client_tok,
	       enum term_type type_hint,
	       void *cnode,
	       struct config_error_type *err_type,
	       client_list_entry_allocator_t cle_allocator,
	       client_list_entry_filler_t cle_filler,
	       void *private_data);

bool haproxy_match(SVCXPRT *xprt);

#endif				/* !CLIENT_MGR_H */
/** @} */
