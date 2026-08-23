// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup clntmmt Client management
 * @{
 */

/**
 * @file client_mgr.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Protocol client manager
 */

#include "config.h"

#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#ifdef RPC_VSOCK
#include <linux/vm_sockets.h>
#endif /* VSOCK */
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include <fnmatch.h>
#include "gsh_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "avltree.h"
#include "gsh_types.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "client_mgr.h"
#include "export_mgr.h"
#include "nfs_qos.h"
#include "server_stats_private.h"
#include "abstract_atomic.h"
#include "gsh_intrinsic.h"
#include "server_stats.h"
#include "sal_functions.h"
#include "nfs_ip_stats.h"
#include "netgroup_cache.h"
#include "ip_utils.h"
#include "server_stats_grpc.h"

/* Clients are stored in an AVL tree
 */

struct client_by_ip {
	struct avltree t;
	pthread_rwlock_t cip_lock;
	struct avltree_node **cache;
	uint32_t cache_sz;
};

static struct client_by_ip client_by_ip;

/**
 * @brief Compute cache slot for an entry
 *
 * This function computes a hash slot, taking an address modulo the
 * number of cache slots (which should be prime).
 *
 * @param wt [in] The table
 * @param ptr [in] Entry address
 *
 * @return The computed offset.
 */
static inline int eip_cache_offsetof(struct client_by_ip *eid, uint64_t k)
{
	return k % eid->cache_sz;
}

/**
 * @brief IP address comparator for AVL tree walk
 *
 * We tell the difference between IPv4 and IPv6 addresses
 * by size (4 vs. 16). IPv4 addresses are "lower", left, sorted
 * first.
 */

static int client_ip_cmpf(const struct avltree_node *lhs,
			  const struct avltree_node *rhs)
{
	struct gsh_client *lk, *rk;

	lk = avltree_container_of(lhs, struct gsh_client, node_k);
	rk = avltree_container_of(rhs, struct gsh_client, node_k);

	return sockaddr_cmp(COMPONENT_HASHTABLE_CACHE, &lk->cl_addrbuf,
			    &rk->cl_addrbuf, true);
}

/**
 * @brief Lookup the client manager struct for this client IP
 *
 * Lookup the client manager struct by client host IP address.
 * IPv4 and IPv6 addresses both handled.  Sets a reference on the
 * block.
 *
 * @param[in] client_ipaddr The sockaddr struct with the v4/v6 address
 * @param[in] lookup_only   If true, only look up, don't create
 *
 * @return pointer to ref locked stats block
 */

struct gsh_client *get_gsh_client(sockaddr_t *client_ipaddr, bool lookup_only)
{
	struct avltree_node *node = NULL;
	struct gsh_client *cl;
	struct server_stats *server_st;
	struct gsh_client v;
	void **cache_slot;
	uint64_t hash = hash_sockaddr(client_ipaddr, true);

	memcpy(&v.cl_addrbuf, client_ipaddr, sizeof(v.cl_addrbuf));
	PTHREAD_RWLOCK_rdlock(&client_by_ip.cip_lock);

	/* check cache */
	cache_slot = (void **)&(
		client_by_ip.cache[eip_cache_offsetof(&client_by_ip, hash)]);
	node = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
	if (node) {
		if (client_ip_cmpf(&v.node_k, node) == 0) {
			/* got it in 1 */
			LogDebug(COMPONENT_HASHTABLE_CACHE,
				 "client_mgr cache hit slot %d",
				 eip_cache_offsetof(&client_by_ip, hash));
			cl = avltree_container_of(node, struct gsh_client,
						  node_k);
			goto out;
		}
	}

	/* fall back to AVL */
	node = avltree_lookup(&v.node_k, &client_by_ip.t);
	if (node) {
		cl = avltree_container_of(node, struct gsh_client, node_k);
		/* update cache */
		atomic_store_voidptr(cache_slot, node);
		goto out;
	} else if (lookup_only) {
		PTHREAD_RWLOCK_unlock(&client_by_ip.cip_lock);
		return NULL;
	}
	PTHREAD_RWLOCK_unlock(&client_by_ip.cip_lock);

	server_st = gsh_calloc(1, sizeof(*server_st), MEM_COMP_CLIENT);
	server_st->st.comp = MEM_COMP_CLIENT;
	server_st->c_all.comp = MEM_COMP_CLIENT;

	cl = &server_st->client;
	cl->cl_addrbuf = *client_ipaddr;
	cl->refcnt = 0;

	if (!sprint_sockip(client_ipaddr, cl->hostaddr_str,
			   sizeof(cl->hostaddr_str))) {
		(void)strlcpy(cl->hostaddr_str, "<unknown>",
			      sizeof(cl->hostaddr_str));
	}

	LogDebug(COMPONENT_HASHTABLE,
		 "Inserting new gsh_client with IP(%s) hash(%lu)",
		 cl->hostaddr_str, hash);

	PTHREAD_RWLOCK_wrlock(&client_by_ip.cip_lock);
	node = avltree_insert(&cl->node_k, &client_by_ip.t);
	if (node) {
		/* somebody beat us to it */
		gsh_free(server_st, server_st->st.comp);
		cl = avltree_container_of(node, struct gsh_client, node_k);
	} else {
		PTHREAD_RWLOCK_init(&cl->client_lock, NULL);
		connection_manager__client_init(&cl->connection_manager);
		/* update cache */
		atomic_store_voidptr(cache_slot, &cl->node_k);
	}

out:
	/* we will hold a ref starting out... */
	inc_gsh_client_refcount(cl);
	PTHREAD_RWLOCK_unlock(&client_by_ip.cip_lock);
	return cl;
}

/**
 * @brief Release the client management struct
 *
 * We are done with it, let it go.
 */

void put_gsh_client(struct gsh_client *client)
{
	int64_t __attribute__((unused))
	new_refcnt = atomic_dec_int64_t(&client->refcnt);
	assert(new_refcnt >= 0);
}

/**
 * @brief Remove a client from the AVL and free its resources
 *
 * @param client_ipaddr [IN] sockaddr (key) to remove
 *
 * @retval 0 if removed
 * @retval ENOENT if not found
 * @retval EBUSY if in use
 */

int remove_gsh_client(sockaddr_t *client_ipaddr)
{
	struct avltree_node *node = NULL;
	struct avltree_node *cnode = NULL;
	struct gsh_client *cl = NULL;
	struct server_stats *server_st;
	struct gsh_client v;
	int removed = 0;
	void **cache_slot;
	uint64_t hash;

	/* Copy the search address into the key */
	memcpy(&v.cl_addrbuf, client_ipaddr, sizeof(v.cl_addrbuf));

	PTHREAD_RWLOCK_wrlock(&client_by_ip.cip_lock);
	node = avltree_lookup(&v.node_k, &client_by_ip.t);
	if (node) {
		cl = avltree_container_of(node, struct gsh_client, node_k);
		if (atomic_fetch_int64_t(&cl->refcnt) > 0) {
			removed = EBUSY;
			goto out;
		}
		/* Invalidate using the stored address's hash so AF_INET and
		 * IPv4-mapped AF_INET6 forms clear the same cache slot.
		 */
		hash = hash_sockaddr(&cl->cl_addrbuf, true);
		cache_slot = (void **)&(client_by_ip.cache[eip_cache_offsetof(
			&client_by_ip, hash)]);
		cnode = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
		if (node == cnode)
			atomic_store_voidptr(cache_slot, NULL);
		avltree_remove(node, &client_by_ip.t);
	} else {
		removed = ENOENT;
	}
out:
	PTHREAD_RWLOCK_unlock(&client_by_ip.cip_lock);
	if (removed == 0) {
		server_st = container_of(cl, struct server_stats, client);
#ifdef ENABLE_QOS
		qos_free_mem(cl, QOS_CLIENT);
#endif
		server_stats_free(&server_st->st);
		server_stats_allops_free(&server_st->c_all);
		connection_manager__client_fini(&cl->connection_manager);
		PTHREAD_RWLOCK_destroy(&cl->client_lock);
		gsh_free(server_st, server_st->st.comp);
	}
	return removed;
}

/**
 * @ Walk the tree and do the callback on each node
 *
 * @param cb    [IN] Callback function
 * @param state [IN] param block to pass
 */

int foreach_gsh_client(bool (*cb)(struct gsh_client *cl, void *state),
		       void *state)
{
	struct avltree_node *client_node;
	struct gsh_client *cl;
	int cnt = 0;

	PTHREAD_RWLOCK_rdlock(&client_by_ip.cip_lock);
	for (client_node = avltree_first(&client_by_ip.t); client_node != NULL;
	     client_node = avltree_next(client_node)) {
		cl = avltree_container_of(client_node, struct gsh_client,
					  node_k);
		if (!cb(cl, state))
			break;
		cnt++;
	}
	PTHREAD_RWLOCK_unlock(&client_by_ip.cip_lock);
	return cnt;
}

#ifdef USE_DBUS

/* DBUS helpers
 */

/* parse the ipaddr string in args
 */

bool arg_ipaddr(DBusMessageIter *args, sockaddr_t *sp, char **errormsg)
{
	char *client_addr;
	unsigned char cl_addrbuf[sizeof(struct in6_addr)];
	bool success = true;

	/* XXX AF_VSOCK addresses are not self-describing--and one might
	 * question whether inet addresses really are, either...so?
	 */

	if (args == NULL) {
		success = false;
		*errormsg = "message has no arguments";
	} else if (dbus_message_iter_get_arg_type(args) != DBUS_TYPE_STRING) {
		success = false;
		*errormsg = "arg not a string";
	} else {
		dbus_message_iter_get_basic(args, &client_addr);
		if (inet_pton(AF_INET, client_addr, cl_addrbuf) == 1) {
			sp->ss_family = AF_INET;
			memcpy(&((struct sockaddr_in *)sp)->sin_addr,
			       cl_addrbuf, sizeof(struct in_addr));
		} else if (inet_pton(AF_INET6, client_addr, cl_addrbuf) == 1) {
			sp->ss_family = AF_INET6;
			memcpy(&((struct sockaddr_in6 *)sp)->sin6_addr,
			       cl_addrbuf, sizeof(struct in6_addr));
		} else {
			success = false;
			*errormsg = "can't decode client address";
		}
	}
	return success;
}

/** @brief lookup gsh_client from input ip-address
 */

struct gsh_client *lookup_client(DBusMessageIter *args, char **errormsg)
{
	sockaddr_t sockaddr;
	struct gsh_client *client = NULL;
	bool success = true;

	success = arg_ipaddr(args, &sockaddr, errormsg);

	if (success) {
		client = get_gsh_client(&sockaddr, true);
		if (client == NULL)
			*errormsg = "Client IP address not found";
	}

	return client;
}

/* DBUS interface(s)
 */

/* org.ganesha.nfsd.clienttmgr interface
 */

/**
 * @brief Add a client into the client manager via DBUS
 *
 * DBUS interface method call
 *
 * @param args [IN] dbus argument stream from the message
 * @param reply [OUT] dbus reply stream for method to fill
 */

static bool gsh_client_addclient(DBusMessageIter *args, DBusMessage *reply,
				 DBusError *error)
{
	struct gsh_client *client;
	sockaddr_t sockaddr;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	success = arg_ipaddr(args, &sockaddr, &errormsg);
	if (success) {
		client = get_gsh_client(&sockaddr, false);
		if (client != NULL) {
			put_gsh_client(client);
		} else {
			success = false;
			errormsg = "No memory to insert client";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	return true;
}

static struct gsh_dbus_method cltmgr_add_client = {
	.name = "AddClient",
	.method = gsh_client_addclient,
	.args = { IPADDR_ARG, STATUS_REPLY, END_ARG_LIST }
};

static bool gsh_client_removeclient(DBusMessageIter *args, DBusMessage *reply,
				    DBusError *error)
{
	sockaddr_t sockaddr;
	bool success = false;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (arg_ipaddr(args, &sockaddr, &errormsg)) {
		switch (remove_gsh_client(&sockaddr)) {
		case 0:
			errormsg = "OK";
			success = true;
			break;
		case ENOENT:
			errormsg = "Client with that address not found";
			break;
		case EBUSY:
			errormsg = "Client with that address is in use (busy)";
			break;
		default:
			errormsg = "Unexpected error";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	return true;
}

static struct gsh_dbus_method cltmgr_remove_client = {
	.name = "RemoveClient",
	.method = gsh_client_removeclient,
	.args = { IPADDR_ARG, STATUS_REPLY, END_ARG_LIST }
};

struct showclients_state {
	DBusMessageIter client_iter;
};

void client_state_stats(DBusMessageIter *iter, struct gsh_client *cl_node)
{
	DBusMessageIter ss_iter;
	char *state_type;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &ss_iter);

	state_type = "Open";
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_STRING, &state_type);
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_UINT64,
				       &cl_node->state_stats[STATE_TYPE_SHARE]);

	state_type = "Lock";
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_STRING, &state_type);
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_UINT64,
				       &cl_node->state_stats[STATE_TYPE_LOCK]);

	state_type = "Delegation";
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_STRING, &state_type);
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_UINT64,
				       &cl_node->state_stats[STATE_TYPE_DELEG]);

	dbus_message_iter_close_container(iter, &ss_iter);
}

/**
 * @brief Check if a client is actively connected
 *
 * A client is considered connected if:
 * 1. It has active operations or NFSv4 state (refcnt > 0), OR
 * 2. It has had recent activity within the configured timeout period
 *
 * @param[in] cl_node  The client to check
 *
 * @return true if connected, false otherwise
 */
static inline bool client_is_connected(struct gsh_client *cl_node)
{
	int64_t refcnt;
	struct timespec now;
	time_t last_sec, elapsed;

	/* Check if client has active operations or NFSv4 state */
	refcnt = atomic_fetch_int64_t(&cl_node->refcnt);
	if (refcnt > 0)
		return true;

	/* Check recent activity */
	clock_gettime(CLOCK_REALTIME, &now);

	/* Atomic read of last_update timestamp */
	last_sec = (time_t)atomic_fetch_uint64_t(
		(uint64_t *)&cl_node->last_update.tv_sec);

	elapsed = now.tv_sec - last_sec;

	return (elapsed < nfs_param.core_param.client_activity_timeout_sec);
}

static bool client_to_dbus(struct gsh_client *cl_node, void *state)
{
	struct showclients_state *iter_state =
		(struct showclients_state *)state;
	struct server_stats *cl;
	char *ipaddr = alloca(SOCK_NAME_MAX);
	DBusMessageIter struct_iter;
	dbus_bool_t is_connected;

	cl = container_of(cl_node, struct server_stats, client);

	if (!sprint_sockip(&cl_node->cl_addrbuf, ipaddr, SOCK_NAME_MAX))
		(void)strlcpy(ipaddr, "<unknown>", SOCK_NAME_MAX);

	/* Check if client is actively connected */
	is_connected = client_is_connected(cl_node);

	dbus_message_iter_open_container(&iter_state->client_iter,
					 DBUS_TYPE_STRUCT, NULL, &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &ipaddr);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_BOOLEAN,
				       &is_connected);
	server_stats_summary(&struct_iter, &cl->st);
	client_state_stats(&struct_iter, cl_node);
	gsh_dbus_append_timestamp(&struct_iter, &cl_node->last_update);
	dbus_message_iter_close_container(&iter_state->client_iter,
					  &struct_iter);
	return true;
}

static bool gsh_client_showclients(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	DBusMessageIter iter;
	struct showclients_state iter_state;
	struct timespec timestamp;

	now(&timestamp);
	/* create a reply from the message */
	dbus_message_iter_init_append(reply, &iter);
	gsh_dbus_append_timestamp(&iter, &timestamp);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 CLIENT_CONTAINER,
					 &iter_state.client_iter);

	(void)foreach_gsh_client(client_to_dbus, (void *)&iter_state);

	dbus_message_iter_close_container(&iter, &iter_state.client_iter);
	return true;
}

static struct gsh_dbus_method cltmgr_show_clients = {
	.name = "ShowClients",
	.method = gsh_client_showclients,
	.args = { TIMESTAMP_REPLY, CLIENTS_REPLY, END_ARG_LIST }
};

static bool disconnect_nfsv41_client(DBusMessageIter *args, DBusMessage *reply,
				     DBusError *error)
{
	sockaddr_t sockaddr;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;
	int connections_destroyed = 0;

	dbus_message_iter_init_append(reply, &iter);
	success = arg_ipaddr(args, &sockaddr, &errormsg);

	struct gsh_client *const client = lookup_client(args, &errormsg);

	if (client == NULL) {
		success = false;
	} else {
		LogInfo(COMPONENT_NFS_V4,
			"Found gsh-client for input ip-address. Now disconnecting it");
		connections_destroyed = destroy_all_client_connections(client);
	}

	gsh_dbus_status_reply(&iter, success, errormsg);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32,
				       &connections_destroyed);
	return true;
}

static struct gsh_dbus_method cltmgr_disconnect_nfsv41_client = {
	.name = "DisconnectNfsv41Client",
	.method = disconnect_nfsv41_client,
	.args = { IPADDR_ARG, STATUS_REPLY, TOTAL_DESTROYED_CONNECTIONS_REPLY,
		  END_ARG_LIST }
};

/* Reset Client specific stats counters
 */
#endif /* USE_DBUS */

/* Not DBus-specific: the gRPC stats API calls this too. */
void reset_client_stats(void)
{
	struct avltree_node *client_node;
	struct gsh_client *cl;
	struct server_stats *clnt;

	PTHREAD_RWLOCK_rdlock(&client_by_ip.cip_lock);
	for (client_node = avltree_first(&client_by_ip.t); client_node != NULL;
	     client_node = avltree_next(client_node)) {
		cl = avltree_container_of(client_node, struct gsh_client,
					  node_k);
		clnt = container_of(cl, struct server_stats, client);
		reset_gsh_stats(&clnt->st);
		/* reset stats counter for allops structs */
		reset_gsh_allops_stats(&clnt->c_all);
	}
	PTHREAD_RWLOCK_unlock(&client_by_ip.cip_lock);
}

#ifdef USE_DBUS

/* Reset Client specific stats counters for allops
 */
#endif /* USE_DBUS */

/* Not DBus-specific: the gRPC stats API calls these too. */
void reset_clnt_allops_stats(void)
{
	struct avltree_node *client_node;
	struct gsh_client *cl;
	struct server_stats *clnt;

	PTHREAD_RWLOCK_rdlock(&client_by_ip.cip_lock);
	for (client_node = avltree_first(&client_by_ip.t); client_node != NULL;
	     client_node = avltree_next(client_node)) {
		cl = avltree_container_of(client_node, struct gsh_client,
					  node_k);
		clnt = container_of(cl, struct server_stats, client);
		reset_gsh_allops_stats(&clnt->c_all);
	}
	PTHREAD_RWLOCK_unlock(&client_by_ip.cip_lock);
}

#ifdef USE_DBUS

static struct gsh_dbus_method *cltmgr_client_methods[] = {
	&cltmgr_add_client, &cltmgr_remove_client, &cltmgr_show_clients,
	&cltmgr_disconnect_nfsv41_client, NULL
};

static struct gsh_dbus_interface cltmgr_client_table = {
	.name = "org.ganesha.nfsd.clientmgr",
	.props = NULL,
	.methods = cltmgr_client_methods,
	.signals = NULL
};

/* org.ganesha.nfsd.clientstats interface
 */

/**
 * DBUS method to get client IO ops statistics
 */
static bool gsh_client_io_ops(DBusMessageIter *args, DBusMessage *reply,
			      DBusError *error)
{
	char *errormsg = "OK";
	struct gsh_client *client = NULL;
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		errormsg = "Client IP address not found";
	}

	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_client_io_ops(&iter, client);

	if (client != NULL)
		put_gsh_client(client);

	return true;
}

static struct gsh_dbus_method cltmgr_client_io_ops = {
	.name = "GetClientIOops",
	.method = gsh_client_io_ops,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, CE_STATS_REPLY,
		  END_ARG_LIST }
};

/**
 * @brief State structure for iterating clients and appending to DBus
 */
struct all_clients_state {
	DBusMessageIter client_iter;
	int count;
};

/**
 * @brief Callback to append client I/O ops to DBus array
 *
 * @param cl_node[in] Client node
 * @param state[in] Iterator state containing DBus iterator
 * @return true to continue iteration
 */
static bool client_io_ops_to_dbus(struct gsh_client *cl_node, void *state)
{
	struct all_clients_state *iter_state =
		(struct all_clients_state *)state;
	char *ipaddr = alloca(SOCK_NAME_MAX);
	const char *ip_str;
	DBusMessageIter struct_iter;

	if (!sprint_sockip(&cl_node->cl_addrbuf, ipaddr, SOCK_NAME_MAX))
		(void)strlcpy(ipaddr, "<unknown>", SOCK_NAME_MAX);

	ip_str = ipaddr;
	dbus_message_iter_open_container(&iter_state->client_iter,
					 DBUS_TYPE_STRUCT, NULL, &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &ip_str);

	/* Use array-safe version that always writes complete structure */
	server_dbus_client_io_ops_for_array(&struct_iter, cl_node);

	dbus_message_iter_close_container(&iter_state->client_iter,
					  &struct_iter);
	return true;
}

/**
 * @brief DBUS method to get IO ops statistics for all clients
 *
 * Returns an array of structures, each containing:
 * - Client IP address (string)
 * - Timestamp (struct of 2 uint64)
 * - NFSv3/v4.0/v4.1/v4.2 I/O statistics
 *
 * @param args[in] DBus arguments (none expected)
 * @param reply[out] DBus reply message
 * @param error[out] DBus error
 * @return true on success
 */
static bool gsh_all_clients_io_ops(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	DBusMessageIter iter;
	struct all_clients_state iter_state;
	struct timespec timestamp;
	char *errormsg = "OK";
	bool success = true;

	dbus_message_iter_init_append(reply, &iter);

	gsh_dbus_status_reply(&iter, success, errormsg);
	now(&timestamp);
	gsh_dbus_append_timestamp(&iter, &timestamp);

	/* Open array container - type signature uses CE_STATS_TYPE macro */
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 "(s(tt)" CE_STATS_TYPE ")",
					 &iter_state.client_iter);

	(void)foreach_gsh_client(client_io_ops_to_dbus, (void *)&iter_state);

	dbus_message_iter_close_container(&iter, &iter_state.client_iter);

	return true;
}

static struct gsh_dbus_method cltmgr_all_clients_io_ops = {
	.name = "GetAllClientIOops",
	.method = gsh_all_clients_io_ops,
	.args = { STATUS_REPLY, TIMESTAMP_REPLY, CE_STATS_ARRAY_REPLY,
		  END_ARG_LIST }
};

/**
 * @brief Callback to append client all ops to DBus array
 *
 * @param cl_node[in] Client node
 * @param state[in] Iterator state containing DBus iterator
 * @return true to continue iteration
 */
static bool client_all_ops_to_dbus(struct gsh_client *cl_node, void *state)
{
	struct all_clients_state *iter_state =
		(struct all_clients_state *)state;
	char *ipaddr = alloca(SOCK_NAME_MAX);
	const char *ip_str;
	DBusMessageIter struct_iter;

	if (!sprint_sockip(&cl_node->cl_addrbuf, ipaddr, SOCK_NAME_MAX))
		(void)strlcpy(ipaddr, "<unknown>", SOCK_NAME_MAX);

	ip_str = ipaddr;
	dbus_message_iter_open_container(&iter_state->client_iter,
					 DBUS_TYPE_STRUCT, NULL, &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &ip_str);
	server_dbus_client_all_ops_for_array(&struct_iter, cl_node);
	dbus_message_iter_close_container(&iter_state->client_iter,
					  &struct_iter);
	return true;
}

/**
 * DBUS method to get all ops statistics for a client
 */
static bool gsh_client_all_ops(DBusMessageIter *args, DBusMessage *reply,
			       DBusError *error)
{
	char *errormsg = "OK";
	struct gsh_client *client = NULL;
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_CLNTALLSTATS) {
		errormsg = "Stat counting for all ops for a client is disabled";
		success = false;
	} else {
		client = lookup_client(args, &errormsg);
		if (client == NULL) {
			success = false;
			errormsg = "Client IP address not found";
		}
	}

	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success) {
		server_dbus_client_all_ops(&iter, client);
		put_gsh_client(client);
	}

	return true;
}

/**
 * @brief DBUS method to get all ops statistics for all clients
 *
 * Returns an array of structures, each containing:
 * - Client IP address (string)
 * - Timestamp (struct of 2 uint64)
 * - NFSv3/NLM/NFSv4/Compound operation statistics
 *
 * @param args[in] DBus arguments (none expected)
 * @param reply[out] DBus reply message
 * @param error[out] DBus error
 * @return true on success
 */
static bool gsh_all_clients_all_ops(DBusMessageIter *args, DBusMessage *reply,
				    DBusError *error)
{
	DBusMessageIter iter;
	struct all_clients_state iter_state;
	struct timespec timestamp;
	char *errormsg = "OK";
	bool success = true;

	dbus_message_iter_init_append(reply, &iter);

	if (!nfs_param.core_param.enable_CLNTALLSTATS) {
		errormsg = "Stat counting for all ops for clients is disabled";
		success = false;
		gsh_dbus_status_reply(&iter, success, errormsg);
		return true;
	}

	gsh_dbus_status_reply(&iter, success, errormsg);
	now(&timestamp);
	gsh_dbus_append_timestamp(&iter, &timestamp);

	/* Open array container - type signature uses CE_ALL_OPS_TYPE macro */
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 "(s(tt)" CE_ALL_OPS_TYPE ")",
					 &iter_state.client_iter);

	(void)foreach_gsh_client(client_all_ops_to_dbus, (void *)&iter_state);

	dbus_message_iter_close_container(&iter, &iter_state.client_iter);

	return true;
}

static struct gsh_dbus_method cltmgr_client_all_ops = {
	.name = "GetClientAllops",
	.method = gsh_client_all_ops,
	.args = { IPADDR_ARG,
		  STATUS_REPLY,
		  TIMESTAMP_REPLY,
#ifdef _USE_NFS3
		  { .name = "clnt_v3", .type = "b", .direction = "out" },
		  CLNT_V3NLM_OPS_REPLY,
#endif
#ifdef _USE_NLM
		  { .name = "clnt_nlm", .type = "b", .direction = "out" },
		  CLNT_V3NLM_OPS_REPLY,
#endif
		  { .name = "clnt_v4", .type = "b", .direction = "out" },
		  CLNT_V4_OPS_REPLY,
		  { .name = "clnt_cmp", .type = "b", .direction = "out" },
		  CLNT_CMP_OPS_REPLY,
		  END_ARG_LIST }
};

static struct gsh_dbus_method cltmgr_all_clients_all_ops = {
	.name = "GetAllClientAllops",
	.method = gsh_all_clients_all_ops,
	.args = { STATUS_REPLY, TIMESTAMP_REPLY, CE_ALL_OPS_ARRAY_REPLY,
		  END_ARG_LIST }
};

#ifdef _USE_NFS3
/**
 * DBUS method to report NFSv3 I/O statistics
 *
 */

static bool get_nfsv3_stats_io(DBusMessageIter *args, DBusMessage *reply,
			       DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = NULL;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_NFSSTATS)
		errormsg = "NFS stat counting disabled";
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st.nfsv3 == NULL) {
			success = false;
			errormsg = "Client does not have any NFSv3 activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v3_iostats(server_st->st.nfsv3, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_v3_io = {
	.name = "GetNFSv3IO",
	.method = get_nfsv3_stats_io,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, IOSTATS_REPLY,
		  END_ARG_LIST }
};
#endif

/**
 * DBUS method to report NFSv40 I/O statistics
 *
 */

static bool get_nfsv40_stats_io(DBusMessageIter *args, DBusMessage *reply,
				DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_NFSSTATS)
		errormsg = "NFS stat counting disabled";
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st.nfsv40 == NULL) {
			success = false;
			errormsg = "Client does not have any NFSv4.0 activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v40_iostats(server_st->st.nfsv40, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_v40_io = {
	.name = "GetNFSv40IO",
	.method = get_nfsv40_stats_io,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, IOSTATS_REPLY,
		  END_ARG_LIST }
};

/**
 * DBUS method to report NFSv41 I/O statistics
 *
 */

static bool get_nfsv41_stats_io(DBusMessageIter *args, DBusMessage *reply,
				DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_NFSSTATS)
		errormsg = "NFS stat counting disabled";
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st.nfsv41 == NULL) {
			success = false;
			errormsg = "Client does not have any NFSv4.1 activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v41_iostats(server_st->st.nfsv41, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_v41_io = {
	.name = "GetNFSv41IO",
	.method = get_nfsv41_stats_io,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, IOSTATS_REPLY,
		  END_ARG_LIST }
};

/**
 * DBUS method to report NFSv41 layout statistics
 *
 */

static bool get_nfsv41_stats_layouts(DBusMessageIter *args, DBusMessage *reply,
				     DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_NFSSTATS)
		errormsg = "NFS stat counting disabled";
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st.nfsv41 == NULL) {
			success = false;
			errormsg = "Client does not have any NFSv4.1 activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v41_layouts(server_st->st.nfsv41, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_v41_layouts = {
	.name = "GetNFSv41Layouts",
	.method = get_nfsv41_stats_layouts,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, LAYOUTS_REPLY,
		  END_ARG_LIST }
};

/**
 * DBUS method to report NFSv42 I/O statistics
 *
 */

static bool get_nfsv42_stats_io(DBusMessageIter *args, DBusMessage *reply,
				DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_NFSSTATS)
		errormsg = "NFS stat counting disabled";
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st.nfsv42 == NULL) {
			success = false;
			errormsg = "Client does not have any NFSv4.2 activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v42_iostats(server_st->st.nfsv42, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_v42_io = {
	.name = "GetNFSv42IO",
	.method = get_nfsv42_stats_io,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, IOSTATS_REPLY,
		  END_ARG_LIST }
};

/**
 * DBUS method to report NFSv42 layout statistics
 *
 */

static bool get_nfsv42_stats_layouts(DBusMessageIter *args, DBusMessage *reply,
				     DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_NFSSTATS)
		errormsg = "NFS stat counting disabled";
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st.nfsv42 == NULL) {
			success = false;
			errormsg = "Client does not have any NFSv4.2 activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_v42_layouts(server_st->st.nfsv42, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_v42_layouts = {
	.name = "GetNFSv42Layouts",
	.method = get_nfsv42_stats_layouts,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, LAYOUTS_REPLY,
		  END_ARG_LIST }
};

/**
 * DBUS method to report NFSv4 delegation statistics
 */
static bool get_stats_delegations(DBusMessageIter *args, DBusMessage *reply,
				  DBusError *error)
{
	char *errormsg = "OK";
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st.deleg == NULL) {
			success = false;
			errormsg =
				"Client does not have any Delegation activity";
		}
	}

	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_delegations(server_st->st.deleg, &iter);

	if (client != NULL)
		put_gsh_client(client);

	return true;
}

static struct gsh_dbus_method cltmgr_show_delegations = {
	.name = "GetDelegations",
	.method = get_stats_delegations,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, DELEG_REPLY,
		  END_ARG_LIST }
};

#ifdef _USE_9P
/**
 * DBUS method to report 9p I/O statistics
 *
 */

static bool get_9p_stats_io(DBusMessageIter *args, DBusMessage *reply,
			    DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st._9p == NULL) {
			success = false;
			errormsg = "Client does not have any 9p activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_9p_iostats(server_st->st._9p, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_9p_io = {
	.name = "Get9pIO",
	.method = get_9p_stats_io,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, IOSTATS_REPLY,
		  END_ARG_LIST }
};

/**
 * DBUS method to report 9p transport statistics
 *
 */

static bool get_9p_stats_trans(DBusMessageIter *args, DBusMessage *reply,
			       DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
		if (errormsg == NULL)
			errormsg = "Client IP address not found";
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st._9p == NULL) {
			success = false;
			errormsg = "Client does not have any 9p activity";
		}
	}
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_9p_transstats(server_st->st._9p, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_9p_trans = {
	.name = "Get9pTrans",
	.method = get_9p_stats_trans,
	.args = { IPADDR_ARG, STATUS_REPLY, TIMESTAMP_REPLY, TRANSPORT_REPLY,
		  END_ARG_LIST }
};

/**
 * DBUS method to report 9p protocol operation statistics
 *
 */

static bool get_9p_client_op_stats(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	u8 opcode;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	client = lookup_client(args, &errormsg);
	if (client == NULL) {
		success = false;
	} else {
		server_st = container_of(client, struct server_stats, client);
		if (server_st->st._9p == NULL) {
			success = false;
			errormsg = "Client does not have any 9p activity";
		}
	}
	dbus_message_iter_next(args);
	if (success)
		success = arg_9p_op(args, &opcode, &errormsg);
	gsh_dbus_status_reply(&iter, success, errormsg);
	if (success)
		server_dbus_9p_opstats(server_st->st._9p, opcode, &iter);

	if (client != NULL)
		put_gsh_client(client);
	return true;
}

static struct gsh_dbus_method cltmgr_show_9p_op_stats = {
	.name = "Get9pOpStats",
	.method = get_9p_client_op_stats,
	.args = { IPADDR_ARG, _9P_OP_ARG, STATUS_REPLY, TIMESTAMP_REPLY,
		  OP_STATS_REPLY, END_ARG_LIST }
};
#endif

static struct gsh_dbus_method *cltmgr_stats_methods[] = {
#ifdef _USE_NFS3
	&cltmgr_show_v3_io,
#endif
	&cltmgr_show_v40_io,
	&cltmgr_show_v41_io,
	&cltmgr_show_v41_layouts,
	&cltmgr_show_v42_io,
	&cltmgr_show_v42_layouts,
	&cltmgr_show_delegations,
	&cltmgr_client_io_ops,
	&cltmgr_all_clients_io_ops,
	&cltmgr_client_all_ops,
	&cltmgr_all_clients_all_ops,
#ifdef _USE_9P
	&cltmgr_show_9p_io,
	&cltmgr_show_9p_trans,
	&cltmgr_show_9p_op_stats,
#endif
	NULL
};

static struct gsh_dbus_interface cltmgr_stats_table = {
	.name = "org.ganesha.nfsd.clientstats",
	.props = NULL,
	.methods = cltmgr_stats_methods,
	.signals = NULL
};

/* DBUS list of interfaces on /org/ganesha/nfsd/ClientMgr
 */

static struct gsh_dbus_interface *cltmgr_interfaces[] = { &cltmgr_client_table,
							  &cltmgr_stats_table,
							  NULL };

void dbus_client_init(void)
{
	gsh_dbus_register_path("ClientMgr", cltmgr_interfaces);
}

#endif /* USE_DBUS */

typedef bool (*grpc_cltmgr_fill_iostats_t)(struct gsh_stats *st,
					   struct grpc_iostats *read_out,
					   struct grpc_iostats *write_out);

/**
 * @brief Shared implementation for per-version cltmgr I/O stats lookup
 *
 * @param ipaddr [IN] client IP address string
 * @param read_out [OUT] read statistics
 * @param write_out [OUT] write statistics
 * @param time_out [OUT] stats timestamp
 * @param success [OUT] API-level success status
 * @param errmsg [OUT] API-level status message
 * @param errmsg_len [IN] length of errmsg buffer
 * @param fill_stats [IN] version-specific stats extractor
 * @param no_activity_msg [IN] error message for missing version stats
 *
 * @return true; failures are reported via success and errmsg
 */
static bool grpc_cltmgr_get_version_io(const char *ipaddr,
				       struct grpc_iostats *read_out,
				       struct grpc_iostats *write_out,
				       struct timespec *time_out, bool *success,
				       char *errmsg, size_t errmsg_len,
				       grpc_cltmgr_fill_iostats_t fill_stats,
				       const char *no_activity_msg)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	sockaddr_t sockaddr;
	const char *errormsg = "OK";

	*success = true;

	/* Stats must be enabled before lookup. */
	if (!nfs_param.core_param.enable_NFSSTATS) {
		*success = false;
		errormsg = "NFS stat counting disabled";
		goto out;
	}

	if (ip_str_to_sockaddr(COMPONENT_GRPC, (char *)ipaddr, &sockaddr) != 0) {
		*success = false;
		errormsg = "can't decode client address";
		goto out;
	}

	/* Lookup-only prevents creating a new client for a stats query. */
	client = get_gsh_client(&sockaddr, true);
	if (client == NULL) {
		*success = false;
		errormsg = "Client IP address not found";
		goto out;
	}

	server_st = container_of(client, struct server_stats, client);

	/* A NULL per-version stats pointer means no activity for that
	 * version.
	 */
	if (!fill_stats(&server_st->st, read_out, write_out)) {
		*success = false;
		errormsg = no_activity_msg;
		goto out_put;
	}

	*time_out = nfs_stats_time;
	errormsg = "OK";

out_put:
	put_gsh_client(client);
	client = NULL;

out:
	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

#ifdef _USE_NFS3
/**
 * @brief gRPC entry point for cltmgr_show_v3_io parity
 */
bool grpc_cltmgr_get_v3_io(const char *ipaddr, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len)
{
	return grpc_cltmgr_get_version_io(
		ipaddr, read_out, write_out, time_out, success, errmsg,
		errmsg_len, server_grpc_fill_v3_iostats,
		"Client does not have any NFSv3 activity");
}
#else
/**
 * @brief gRPC entry point for cltmgr_show_v3_io when NFSv3 is disabled
 */
bool grpc_cltmgr_get_v3_io(const char *ipaddr, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len)
{
	*success = false;
	snprintf(errmsg, errmsg_len, "NFSv3 not supported");
	return true;
}
#endif

/**
 * @brief gRPC entry point for cltmgr_show_v40_io parity
 */
bool grpc_cltmgr_get_v40_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len)
{
	return grpc_cltmgr_get_version_io(
		ipaddr, read_out, write_out, time_out, success, errmsg,
		errmsg_len, server_grpc_fill_v40_iostats,
		"Client does not have any NFSv4.0 activity");
}

/**
 * @brief gRPC entry point for cltmgr_show_v41_io parity
 */
bool grpc_cltmgr_get_v41_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len)
{
	return grpc_cltmgr_get_version_io(
		ipaddr, read_out, write_out, time_out, success, errmsg,
		errmsg_len, server_grpc_fill_v41_iostats,
		"Client does not have any NFSv4.1 activity");
}

/**
 * @brief gRPC entry point for cltmgr_show_v42_io parity
 */
bool grpc_cltmgr_get_v42_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len)
{
	return grpc_cltmgr_get_version_io(
		ipaddr, read_out, write_out, time_out, success, errmsg,
		errmsg_len, server_grpc_fill_v42_iostats,
		"Client does not have any NFSv4.2 activity");
}

typedef bool (*grpc_cltmgr_fill_layouts_t)(struct gsh_stats *st,
					   struct grpc_layouts *layouts_out);

/**
 * @brief Shared implementation for per-version cltmgr layout stats lookup
 */
static bool grpc_cltmgr_get_version_layouts(
	const char *ipaddr, struct grpc_layouts *layouts_out,
	struct timespec *time_out, bool *success, char *errmsg,
	size_t errmsg_len, grpc_cltmgr_fill_layouts_t fill_layouts,
	const char *no_activity_msg)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	sockaddr_t sockaddr;
	const char *errormsg = "OK";

	*success = true;

	if (!nfs_param.core_param.enable_NFSSTATS) {
		*success = false;
		errormsg = "NFS stat counting disabled";
		goto out;
	}

	if (ip_str_to_sockaddr(COMPONENT_GRPC, ipaddr, &sockaddr) != 0) {
		*success = false;
		errormsg = "can't decode client address";
		goto out;
	}

	client = get_gsh_client(&sockaddr, true);
	if (client == NULL) {
		*success = false;
		errormsg = "Client IP address not found";
		goto out;
	}

	server_st = container_of(client, struct server_stats, client);
	if (!fill_layouts(&server_st->st, layouts_out)) {
		*success = false;
		errormsg = no_activity_msg;
		goto out_put;
	}

	*time_out = nfs_stats_time;
	errormsg = "OK";

out_put:
	put_gsh_client(client);
	client = NULL;

out:
	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

/**
 * @brief Shared client lookup for cltmgr stats that do not require NFSSTATS
 */
static struct gsh_client *grpc_cltmgr_lookup_client_simple(
	const char *ipaddr, bool *success, const char **errormsg)
{
	sockaddr_t sockaddr;
	struct gsh_client *client;

	*success = true;
	*errormsg = "OK";

	if (ip_str_to_sockaddr(COMPONENT_GRPC, ipaddr, &sockaddr) != 0) {
		*success = false;
		*errormsg = "can't decode client address";
		return NULL;
	}

	client = get_gsh_client(&sockaddr, true);
	if (client == NULL) {
		*success = false;
		*errormsg = "Client IP address not found";
	}
	return client;
}

/**
 * @brief gRPC entry point for cltmgr_show_v41_layouts parity
 */
bool grpc_cltmgr_get_v41_layouts(const char *ipaddr,
				 struct grpc_layouts *layouts_out,
				 struct timespec *time_out, bool *success,
				 char *errmsg, size_t errmsg_len)
{
	return grpc_cltmgr_get_version_layouts(
		ipaddr, layouts_out, time_out, success, errmsg, errmsg_len,
		server_grpc_fill_v41_layouts,
		"Client does not have any NFSv4.1 activity");
}

/**
 * @brief gRPC entry point for cltmgr_show_v42_layouts parity
 */
bool grpc_cltmgr_get_v42_layouts(const char *ipaddr,
				 struct grpc_layouts *layouts_out,
				 struct timespec *time_out, bool *success,
				 char *errmsg, size_t errmsg_len)
{
	return grpc_cltmgr_get_version_layouts(
		ipaddr, layouts_out, time_out, success, errmsg, errmsg_len,
		server_grpc_fill_v42_layouts,
		"Client does not have any NFSv4.2 activity");
}

/**
 * @brief gRPC entry point for cltmgr_show_delegations parity
 */
bool grpc_cltmgr_get_delegations(const char *ipaddr,
				 struct grpc_delegation_stats *deleg_out,
				 struct timespec *time_out, bool *success,
				 char *errmsg, size_t errmsg_len)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	const char *errormsg = "OK";

	client = grpc_cltmgr_lookup_client_simple(ipaddr, success, &errormsg);
	if (*success) {
		server_st = container_of(client, struct server_stats, client);
		if (!server_grpc_fill_delegations(&server_st->st, deleg_out)) {
			*success = false;
			errormsg =
				"Client does not have any Delegation activity";
		} else {
			*time_out = nfs_stats_time;
			errormsg = "OK";
		}
	}

	if (client != NULL)
		put_gsh_client(client);

	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

/**
 * @brief gRPC entry point for gsh_client_io_ops parity
 */
bool grpc_cltmgr_get_client_io_ops(const char *ipaddr,
				   struct grpc_client_io_ops *ops_out,
				   bool *success, char *errmsg,
				   size_t errmsg_len)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	const char *errormsg = "OK";

	client = grpc_cltmgr_lookup_client_simple(ipaddr, success, &errormsg);
	if (*success) {
		server_st = container_of(client, struct server_stats, client);
		server_grpc_fill_client_io_ops(&server_st->st,
					       &client->last_update, ops_out);
		errormsg = "OK";
	}

	if (client != NULL)
		put_gsh_client(client);

	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

/**
 * @brief gRPC entry point for gsh_client_all_ops parity
 */
bool grpc_cltmgr_get_client_allops(const char *ipaddr,
				   struct grpc_client_allops **allops_out,
				   bool *success, char *errmsg,
				   size_t errmsg_len)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	const char *errormsg = "OK";

	*allops_out = NULL;
	*success = true;

	if (!nfs_param.core_param.enable_CLNTALLSTATS) {
		*success = false;
		snprintf(errmsg, errmsg_len,
			 "Stat counting for all ops for a client is disabled");
		return true;
	}

	client = grpc_cltmgr_lookup_client_simple(ipaddr, success, &errormsg);
	if (*success) {
		server_st = container_of(client, struct server_stats, client);
		*allops_out =
			server_grpc_fill_client_allops(&server_st->st,
						       &server_st->c_all,
						       &client->last_update);
		errormsg = "OK";
	}

	if (client != NULL)
		put_gsh_client(client);

	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

#ifdef _USE_9P
/**
 * @brief Shared implementation for 9p cltmgr stats lookup
 */
static bool grpc_cltmgr_get_9p_stats(
	const char *ipaddr, bool *success, char *errmsg, size_t errmsg_len,
	bool (*fill_stats)(struct gsh_stats *st, void *out), void *out,
	struct timespec *time_out, const char *no_activity_msg)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	const char *errormsg = "OK";

	*success = true;

	client = grpc_cltmgr_lookup_client_simple(ipaddr, success, &errormsg);
	if (!*success)
		goto out;

	server_st = container_of(client, struct server_stats, client);
	if (!fill_stats(&server_st->st, out)) {
		*success = false;
		errormsg = no_activity_msg;
		goto out_put;
	}

	now(time_out);
	errormsg = "OK";

out_put:
	put_gsh_client(client);
	client = NULL;

out:
	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

static bool grpc_fill_9p_iostats_wrapper(struct gsh_stats *st, void *out)
{
	struct grpc_iostats *pair = (struct grpc_iostats *)out;

	return server_grpc_fill_9p_iostats(st, &pair[0], &pair[1]);
}

static bool grpc_fill_9p_transport_wrapper(struct gsh_stats *st, void *out)
{
	return server_grpc_fill_9p_transport(st, out);
}

/**
 * @brief gRPC entry point for cltmgr_show_9p_io parity
 */
bool grpc_cltmgr_get_9p_io(const char *ipaddr, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len)
{
	struct grpc_iostats pair[2];

	if (!grpc_cltmgr_get_9p_stats(ipaddr, success, errmsg, errmsg_len,
				      grpc_fill_9p_iostats_wrapper, pair,
				      time_out,
				      "Client does not have any 9p activity"))
		return true;

	if (*success) {
		*read_out = pair[0];
		*write_out = pair[1];
	}
	return true;
}

/**
 * @brief gRPC entry point for cltmgr_show_9p_trans parity
 */
bool grpc_cltmgr_get_9p_trans(const char *ipaddr,
			      struct grpc_transport_stats *trans_out,
			      struct timespec *time_out, bool *success,
			      char *errmsg, size_t errmsg_len)
{
	return grpc_cltmgr_get_9p_stats(ipaddr, success, errmsg, errmsg_len,
					grpc_fill_9p_transport_wrapper,
					trans_out, time_out,
					"Client does not have any 9p activity");
}

/**
 * @brief gRPC entry point for cltmgr_show_9p_op_stats parity
 */
bool grpc_cltmgr_get_9p_opstats(const char *ipaddr, const char *opname,
				struct grpc_op_stats *op_out,
				struct timespec *time_out, bool *success,
				char *errmsg, size_t errmsg_len)
{
	struct gsh_client *client = NULL;
	struct server_stats *server_st = NULL;
	uint8_t opcode;
	const char *errormsg = "OK";

	*success = true;

	client = grpc_cltmgr_lookup_client_simple(ipaddr, success, &errormsg);
	if (!*success)
		goto out;

	server_st = container_of(client, struct server_stats, client);
	if (server_st->st._9p == NULL) {
		*success = false;
		errormsg = "Client does not have any 9p activity";
		goto out_put;
	}

	if (!grpc_parse_9p_opname(opname, &opcode)) {
		*success = false;
		errormsg = "arg not a known 9P operation";
		goto out_put;
	}

	server_grpc_fill_9p_opstats(&server_st->st, opcode, op_out);
	now(time_out);
	errormsg = "OK";

out_put:
	put_gsh_client(client);
	client = NULL;

out:
	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}
#else
bool grpc_cltmgr_get_9p_io(const char *ipaddr, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len)
{
	*success = false;
	snprintf(errmsg, errmsg_len, "9P not supported");
	return true;
}

bool grpc_cltmgr_get_9p_trans(const char *ipaddr,
			      struct grpc_transport_stats *trans_out,
			      struct timespec *time_out, bool *success,
			      char *errmsg, size_t errmsg_len)
{
	*success = false;
	snprintf(errmsg, errmsg_len, "9P not supported");
	return true;
}

bool grpc_cltmgr_get_9p_opstats(const char *ipaddr, const char *opname,
				struct grpc_op_stats *op_out,
				struct timespec *time_out, bool *success,
				char *errmsg, size_t errmsg_len)
{
	*success = false;
	snprintf(errmsg, errmsg_len, "9P not supported");
	return true;
}
#endif

/**
 * @brief gRPC entry point for cltmgr_add_client / D-Bus AddClient
 *
 * Creates an entry in the client manager for the given IP address.
 * If the entry already exists the call succeeds silently (D-Bus parity).
 */
bool grpc_cltmgr_add_client(const char *ipaddr, bool *success, char *errmsg,
			    size_t errmsg_len)
{
	sockaddr_t sockaddr;
	struct gsh_client *client;
	const char *errormsg = "OK";

	*success = true;

	if (ip_str_to_sockaddr(COMPONENT_GRPC, ipaddr, &sockaddr) != 0) {
		*success = false;
		errormsg = "can't decode client address";
		goto out;
	}

	/* lookup_only=false creates the entry if missing */
	client = get_gsh_client(&sockaddr, false);
	if (client != NULL) {
		put_gsh_client(client);
	} else {
		*success = false;
		errormsg = "No memory to insert client";
	}

out:
	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

/**
 * @brief gRPC entry point for cltmgr_remove_client / D-Bus RemoveClient
 *
 * Removes the client entry for the given IP address. Fails if the
 * client is not found or is currently in use (busy).
 */
bool grpc_cltmgr_remove_client(const char *ipaddr, bool *success, char *errmsg,
			       size_t errmsg_len)
{
	sockaddr_t sockaddr;
	const char *errormsg = "OK";

	*success = false;

	if (ip_str_to_sockaddr(COMPONENT_GRPC, ipaddr, &sockaddr) != 0) {
		errormsg = "can't decode client address";
		goto out;
	}

	switch (remove_gsh_client(&sockaddr)) {
	case 0:
		errormsg = "OK";
		*success = true;
		break;
	case ENOENT:
		errormsg = "Client with that address not found";
		break;
	case EBUSY:
		errormsg = "Client with that address is in use (busy)";
		break;
	default:
		errormsg = "Unexpected error";
		break;
	}

out:
	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

/* Two-pass AVL walk helpers for ShowClients */

struct grpc_show_count_state {
	uint32_t count;
};

static bool grpc_show_count_cb(struct gsh_client *cl, void *state)
{
	struct grpc_show_count_state *st =
		(struct grpc_show_count_state *)state;

	(void)cl;
	st->count++;
	return true;
}

struct grpc_show_fill_state {
	struct grpc_client_info *clients;
	uint32_t index;
};

static bool grpc_show_fill_cb(struct gsh_client *cl_node, void *state)
{
	struct grpc_show_fill_state *st = (struct grpc_show_fill_state *)state;
	struct grpc_client_info *info = &st->clients[st->index++];
	struct server_stats *cl;

	cl = container_of(cl_node, struct server_stats, client);

	if (!sprint_sockip(&cl_node->cl_addrbuf, info->ipaddr,
			   sizeof(info->ipaddr)))
		(void)strlcpy(info->ipaddr, "<unknown>", sizeof(info->ipaddr));

	/* Check if client is actively connected */
	info->is_connected = client_is_connected(cl_node);

	server_grpc_fill_stats_summary(&cl->st, info->protocols,
				       &info->protocol_count, &info->total_ops);

	info->state_count = 3;
	snprintf(info->state_stats[0].state_type,
		 sizeof(info->state_stats[0].state_type), "Open");
	info->state_stats[0].count = cl_node->state_stats[STATE_TYPE_SHARE];

	snprintf(info->state_stats[1].state_type,
		 sizeof(info->state_stats[1].state_type), "Lock");
	info->state_stats[1].count = cl_node->state_stats[STATE_TYPE_LOCK];

	snprintf(info->state_stats[2].state_type,
		 sizeof(info->state_stats[2].state_type), "Delegation");
	info->state_stats[2].count = cl_node->state_stats[STATE_TYPE_DELEG];

	info->last_update = cl_node->last_update;
	return true;
}

/**
 * @brief Release a grpc_show_clients snapshot allocated by
 *        grpc_cltmgr_show_clients().
 */
void grpc_cltmgr_free_show_clients(struct grpc_show_clients *show)
{
	if (show == NULL)
		return;
	gsh_free(show->clients, MEM_COMP_MANAGE);
	gsh_free(show, MEM_COMP_MANAGE);
}

/**
 * @brief gRPC entry point for cltmgr_show_clients / D-Bus ShowClients
 *
 * Takes a two-pass snapshot of all known clients (count then fill) under
 * the AVL read lock to avoid TOCTOU races. The returned struct must be
 * freed via grpc_cltmgr_free_show_clients().
 */
bool grpc_cltmgr_show_clients(struct grpc_show_clients **out, bool *success,
			      char *errmsg, size_t errmsg_len)
{
	struct grpc_show_count_state count_st = { .count = 0 };
	struct grpc_show_fill_state fill_st;
	struct grpc_show_clients *show;

	*out = NULL;
	*success = true;

	show = gsh_calloc(1, sizeof(*show), MEM_COMP_MANAGE);
	now(&show->time);

	(void)foreach_gsh_client(grpc_show_count_cb, &count_st);
	show->client_count = count_st.count;

	if (show->client_count > 0) {
		show->clients = gsh_calloc(show->client_count,
					   sizeof(struct grpc_client_info),
					   MEM_COMP_MANAGE);
		fill_st.clients = show->clients;
		fill_st.index = 0;
		(void)foreach_gsh_client(grpc_show_fill_cb, &fill_st);
	}

	*out = show;
	snprintf(errmsg, errmsg_len, "OK");
	return true;
}

/**
 * @brief gRPC entry point for DisconnectNfsv41Client
 *
 * Parses the client IP once (the D-Bus path calls arg_ipaddr then
 * lookup_client which re-parses), then calls
 * destroy_all_client_connections() and returns the count.
 */
bool grpc_cltmgr_disconnect_nfsv41_client(const char *ipaddr,
					  int32_t *connections_destroyed,
					  bool *success, char *errmsg,
					  size_t errmsg_len)
{
	sockaddr_t sockaddr;
	struct gsh_client *client = NULL;
	const char *errormsg = "OK";

	*success = true;
	*connections_destroyed = 0;

	if (ip_str_to_sockaddr(COMPONENT_GRPC, ipaddr, &sockaddr) != 0) {
		*success = false;
		errormsg = "can't decode client address";
		goto out;
	}

	client = get_gsh_client(&sockaddr, true);
	if (client == NULL) {
		*success = false;
		errormsg = "Client IP address not found";
		goto out;
	}

	LogInfo(COMPONENT_NFS_V4,
		"Found gsh-client for input ip-address. Now disconnecting it");
	*connections_destroyed = destroy_all_client_connections(client);
	put_gsh_client(client);

out:
	snprintf(errmsg, errmsg_len, "%s", errormsg);
	return true;
}

/* Cleanup on shutdown */
void client_mgr_cleanup(void)
{
	PTHREAD_RWLOCK_destroy(&client_by_ip.cip_lock);
}

struct cleanup_list_element client_mgr_cleanup_element = {
	.clean = client_mgr_cleanup,
};

/**
 * @brief Initialize client manager
 */

void client_pkginit(void)
{
	PTHREAD_RWLOCK_init(&client_by_ip.cip_lock, NULL);
	avltree_init(&client_by_ip.t, client_ip_cmpf, 0);
	client_by_ip.cache_sz = 32767;
	client_by_ip.cache = gsh_calloc(client_by_ip.cache_sz,
					sizeof(struct avltree_node *),
					MEM_COMP_CLIENT);
	RegisterCleanup(&client_mgr_cleanup_element);
}

static char *client_types[] = { [PROTO_CLIENT] = "PROTO_CLIENT",
				[NETWORK_CLIENT] = "NETWORK_CLIENT",
				[NETGROUP_CLIENT] = "NETGROUP_CLIENT",
				[WILDCARDHOST_CLIENT] = "WILDCARDHOST_CLIENT",
				[GSSPRINCIPAL_CLIENT] = "GSSPRINCIPAL_CLIENT",
				[MATCH_ANY_CLIENT] = "MATCH_ANY_CLIENT",
				[BAD_CLIENT] = "BAD_CLIENT" };

int StrClient(struct display_buffer *dspbuf, struct base_client_entry *client)
{
	int b_left = display_start(dspbuf);

	if (client->type > BAD_CLIENT) {
		b_left = display_printf(dspbuf, "UNKNOWN_CLIENT_TYPE: 0x%08x",
					client->type);
	} else {
		b_left = display_printf(dspbuf, "%s: %s",
					client_types[client->type],
					client->str);
	}

	return b_left;
}

void LogClientListEntry(enum log_components component, log_levels_t level,
			const char *file, int line, const char *func,
			const char *tag, struct base_client_entry *entry)
{
	char buf[1024] = "\0";
	struct display_buffer dspbuf = { sizeof(buf), buf, buf };
	int b_left = display_start(&dspbuf);

	if (!isLevel(component, level))
		return;

	if (b_left > 0 && tag != NULL)
		b_left = display_cat(&dspbuf, tag);

	if (b_left > 0 && level >= NIV_DEBUG)
		b_left = display_printf(&dspbuf, "%p ", entry);

	if (b_left > 0)
		b_left = StrClient(&dspbuf, entry);

	DisplayLogComponentLevel(component, file, line, func, level, "%s", buf);
}

void LogClientList(enum log_components component, log_levels_t level,
		   const char *file, int line, const char *func,
		   const char *tag, struct glist_head *list)
{
	struct glist_head *glist;

	if (!isLevel(component, level))
		return;

	glist_for_each(glist, list) {
		struct base_client_entry *bce;

		bce = glist_entry(glist, struct base_client_entry, cle_list);

		LogClientListEntry(component, level, file, line, func, tag,
				   bce);
	}
}

void FreeClientList(struct glist_head *clients, client_free_func free_func)
{
	struct glist_head *glist;
	struct glist_head *glistn;

	glist_for_each_safe(glist, glistn, clients) {
		struct base_client_entry *client;

		client = glist_entry(glist, struct base_client_entry, cle_list);

		glist_del(&client->cle_list);
		cidr_free(client->cidr, client->mem_comp);
		gsh_free(client->str, client->mem_comp);
		free_func(client);
	}
}

void *base_client_allocator(mem_components_t mem_comp)
{
	return gsh_calloc(1, sizeof(struct base_client_entry), mem_comp);
}

struct base_client_entry *is_base_client_exact_match(
	enum log_components component, struct glist_head *client_list,
	const char *client_tok)
{
	struct base_client_entry *cli;
	struct glist_head *g;
	CIDR *cidr = NULL;
	CIDR cli_cidr;

	/* Try CIDR parse (NETWORK_CLIENT) */
	cidr = cidr_from_str(component, client_tok, MEM_COMP_TRANSIENT);
	if (cidr)
		normalize_v4_mapped_cidr(component, cidr);

	glist_for_each(g, client_list) {
		cli = glist_entry(g, struct base_client_entry, cle_list);

		switch (cli->type) {
		case NETWORK_CLIENT:
			cli_cidr = *cli->cidr;

			normalize_v4_mapped_cidr(component, &cli_cidr);
			if (cidr && cidr_equals(component, &cli_cidr, cidr))
				goto found;
			break;

		case MATCH_ANY_CLIENT:
			if (strcmp(client_tok, "*") == 0)
				goto found;
			break;

		case NETGROUP_CLIENT:
			if (client_tok[0] == '@' &&
			    strcmp(cli->str, client_tok + 1) == 0)
				goto found;
			break;

		case WILDCARDHOST_CLIENT:
			if (strcmp(cli->str, client_tok) == 0)
				goto found;
			break;

		default:
			break;
		}
	}

	cli = NULL;

found:

	cidr_free(cidr, MEM_COMP_TRANSIENT);

	return cli;
}

/**
 * @brief Expand the client name token into one or more client entries
 *
 * @param component     [IN]  component for logging
 * @param client_list   [IN]  the client list this gets linked to (in tail order)
 * @param client_tok    [IN]  the name string.  We modify it.
 * @param type_hint     [IN]  type hint from parser for client_tok
 * @param mem_comp      [IN]  memory component
 * @param cnode         [IN]  opaque pointer needed for config_proc_error()
 * @param err_type      [OUT] error handling ref
 * @param cle_allocator [IN]  function to allocate a list entry
 * @param cle_filler    [IN]  function to fill in a list entry
 * @param private_data  [IN]  data to be passed to cle_filler function
 *
 * @returns 0 on success, error count on failure
 */

int add_client(enum log_components component, struct glist_head *client_list,
	       const char *client_tok, enum term_type type_hint,
	       mem_components_t mem_comp, void *cnode,
	       struct config_error_type *err_type,
	       client_list_entry_allocator_t cle_allocator,
	       client_list_entry_filler_t cle_filler, void *private_data)
{
	int errcnt = 0;
	struct addrinfo *info;
	CIDR *cidr;
	int rc;
	struct base_client_entry *cli;

	/*
	 * Locking requirements:
	 * Caller must hold rwlock (read/write lock) before calling.
	 * No locking is performed internally.
	 */
	/* Check if the same Network client already exist */
	if (is_base_client_exact_match(component, client_list, client_tok)) {
		config_proc_error(cnode, err_type,
				  "Duplicate client entry found: %s",
				  client_tok);
		err_type->exists = true;
		errcnt++;
		goto exit;
	}

	if (cle_allocator == NULL)
		cle_allocator = base_client_allocator;

	cli = cle_allocator(mem_comp);

	cli->cidr = NULL;
	cli->mem_comp = mem_comp;
	glist_init(&cli->cle_list);
	switch (type_hint) {
	case TERM_V4_ANY:
		cli->type = MATCH_ANY_CLIENT;
		cli->str = gsh_strdup("*", mem_comp);
		break;
	case TERM_NETGROUP:
		if (strlen(client_tok) > MAXHOSTNAMELEN) {
			config_proc_error(cnode, err_type,
					  "netgroup (%s) name too long",
					  client_tok);
			err_type->invalid = true;
			errcnt++;
			goto out;
		}
		cli->str = gsh_strdup(client_tok + 1, mem_comp);
		cli->type = NETGROUP_CLIENT;
		break;
	case TERM_V4CIDR:
	case TERM_V6CIDR:
	case TERM_V4ADDR:
	case TERM_V6ADDR:
		cidr = cidr_from_str(component, client_tok, mem_comp);
		if (cidr == NULL) {
			switch (type_hint) {
			case TERM_V4CIDR:
				config_proc_error(
					cnode, err_type,
					"Expected a IPv4 CIDR address, got (%s)",
					client_tok);
				break;
			case TERM_V6CIDR:
				config_proc_error(
					cnode, err_type,
					"Expected a IPv6 CIDR address, got (%s)",
					client_tok);
				break;
			case TERM_V4ADDR:
				config_proc_error(
					cnode, err_type,
					"IPv4 addr (%s) not in presentation format",
					client_tok);
				break;
			case TERM_V6ADDR:
				config_proc_error(
					cnode, err_type,
					"IPv6 addr (%s) not in presentation format",
					client_tok);
				break;
			default:
				break;
			}
			err_type->invalid = true;
			errcnt++;
			goto out;
		}
		cli->cidr = cidr;
		cli->type = NETWORK_CLIENT;
		break;
	case TERM_REGEX:
		if (strlen(client_tok) > MAXHOSTNAMELEN) {
			config_proc_error(cnode, err_type,
					  "Wildcard client (%s) name too long",
					  client_tok);
			err_type->invalid = true;
			errcnt++;
			goto out;
		}
		cli->str = gsh_strdup(client_tok, mem_comp);
		cli->type = WILDCARDHOST_CLIENT;
		break;
	case TERM_TOKEN: /* only dns names now. */
		rc = gsh_getaddrinfo(client_tok, NULL, NULL, &info,
				     nfs_param.core_param.enable_AUTHSTATS);
		if (rc == 0) {
			struct addrinfo *ap, *ap_last = NULL;
			struct in_addr in_addr_last;
			struct in6_addr in6_addr_last;

			for (ap = info; ap != NULL; ap = ap->ai_next) {
				LogFullDebug(
					COMPONENT_EXPORT,
					"flags=%d family=%d socktype=%d protocol=%d addrlen=%d name=%s",
					ap->ai_flags, ap->ai_family,
					ap->ai_socktype, ap->ai_protocol,
					(int)ap->ai_addrlen, ap->ai_canonname);

				if (cli == NULL) {
					cli = cle_allocator(mem_comp);
					cli->mem_comp = mem_comp;
					glist_init(&cli->cle_list);
				}

				if (ap->ai_family == AF_INET &&
				    (ap->ai_socktype == SOCK_STREAM ||
				     ap->ai_socktype == SOCK_DGRAM)) {
					struct in_addr infoaddr =
						((struct sockaddr_in *)
							 ap->ai_addr)
							->sin_addr;
					if (ap_last != NULL &&
					    ap_last->ai_family ==
						    ap->ai_family &&
					    memcmp(&infoaddr, &in_addr_last,
						   sizeof(struct in_addr)) == 0)
						continue;
					cli->cidr = cidr_from_inaddr(&infoaddr,
								     mem_comp);
					cli->type = NETWORK_CLIENT;
					ap_last = ap;
					in_addr_last = infoaddr;

				} else if (ap->ai_family == AF_INET6 &&
					   (ap->ai_socktype == SOCK_STREAM ||
					    ap->ai_socktype == SOCK_DGRAM)) {
					struct in6_addr infoaddr =
						((struct sockaddr_in6 *)
							 ap->ai_addr)
							->sin6_addr;

					if (ap_last != NULL &&
					    ap_last->ai_family ==
						    ap->ai_family &&
					    !memcmp(&infoaddr, &in6_addr_last,
						    sizeof(struct in6_addr)))
						continue;
					/* IPv6 address */
					cli->cidr = cidr_from_in6addr(&infoaddr,
								      mem_comp);
					cli->type = NETWORK_CLIENT;
					ap_last = ap;
					in6_addr_last = infoaddr;
				} else {
					continue;
				}

				if (cle_filler != NULL)
					cle_filler(cli, private_data);
				else
					LogMidDebug_ClientListEntry(component,
								    "", cli);

				glist_add_tail(client_list, &cli->cle_list);
				cli = NULL; /* let go of it */
			}
			freeaddrinfo(info);
			goto out;
		} else {
			config_proc_error(cnode, err_type,
					  "Client (%s) not found because %s",
					  client_tok, gai_strerror(rc));
			err_type->bogus = true;
			errcnt++;
		}
		break;
	default:
		config_proc_error(cnode, err_type,
				  "Expected a client, got a %s for (%s)",
				  config_term_desc(type_hint), client_tok);
		err_type->bogus = true;
		errcnt++;
		goto out;
	}

	if (cli->type == NETWORK_CLIENT) {
		/* Standardize string form */
		cli->str = cidr_to_str(cli->cidr, mem_comp);
	}

	if (cle_filler != NULL)
		cle_filler(cli, private_data);
	else
		LogMidDebug_ClientListEntry(component, "", cli);

	glist_add_tail(client_list, &cli->cle_list);
	cli = NULL;
out:
	gsh_free(cli, mem_comp);

exit:
	return errcnt;
}

/**
 * @brief Expand the client name token into one or more client entries
 *
 * @param component     [IN]  component for logging
 * @param client_list   [IN]  the client list
 * @param client_tok    [IN]  the name string.  We modify it.
 *
 * @returns True on success, false if no entry found
 */
bool delete_base_client(enum log_components component,
			struct glist_head *client_list, const char *client_tok)
{
	bool deleted = false;
	struct base_client_entry *cli;

	/*
	 * Locking requirements:
	 * Caller must hold rwlock (read/write lock) before calling.
	 * No locking is performed internally.
	 */
	/* Check if the same client already exist */
	cli = is_base_client_exact_match(component, client_list, client_tok);

	if (cli == NULL) {
		LogCrit(component, "Matching client entry not found: %s",
			client_tok);
		goto out;
	}

	cidr_free(cli->cidr, cli->mem_comp);
	gsh_free(cli->str, cli->mem_comp);
	glist_del(&cli->cle_list);
	gsh_free(cli, cli->mem_comp);

	LogInfo(component, "Removed Base client: (%s)", client_tok);
	deleted = true;

out:
	return deleted;
}

/**
 * @brief Match a specific client in a client list
 *
 * @param[in]  hostaddr          Host to search for
 * @param[in]  clients           Client list to search
 * @param[in]  client_predicate  A callback predicate the client must match
 *
 * @return the client entry or NULL if failure.
 */
struct base_client_entry *
client_match(enum log_components component, const char *str,
	     sockaddr_t *clientaddr, struct glist_head *clients,
	     client_list_entry_predicate_t client_predicate)
{
	struct glist_head *glist;
	int rc;
	int ipvalid = -1; /* -1 need to print, 0 - invalid, 1 - ok */
	char hostname[NI_MAXHOST];
	char ipstring[SOCK_NAME_MAX];
	struct base_client_entry *client;
	sockaddr_t alt_hostaddr;
	sockaddr_t *hostaddr = NULL;

	hostaddr = convert_ipv6_to_ipv4(component, clientaddr, &alt_hostaddr);

	if (isMidDebug(component)) {
		char ipstring[SOCK_NAME_MAX];
		struct display_buffer dspbuf = { sizeof(ipstring), ipstring,
						 ipstring };

		display_sockip(&dspbuf, hostaddr);

		LogMidDebug(component, "Check for address %s%s", ipstring,
			    str ? str : "");
	}

	glist_for_each(glist, clients) {
		client = glist_entry(glist, struct base_client_entry, cle_list);
		LogMidDebug_ClientListEntry(component, "Match V4: ", client);
		if (client_predicate != NULL && !client_predicate(client)) {
			LogMidDebug_ClientListEntry(
				component,
				"Client does not match predicate: ", client);
			continue;
		}

		switch (client->type) {
		case NETWORK_CLIENT:
			if (cidr_contains_ip(client->cidr, hostaddr) == 0) {
				goto out;
			}
			break;

		case NETGROUP_CLIENT:
			/* Try to get the entry from th IP/name cache */
			rc = nfs_ip_name_get(hostaddr, hostname,
					     sizeof(hostname));

			if (rc == IP_NAME_NOT_FOUND) {
				/* IPaddr was not cached, add it to the cache */
				rc = nfs_ip_name_add(hostaddr, hostname,
						     sizeof(hostname));
			}

			if (rc != IP_NAME_SUCCESS)
				break; /* Fatal failure */

			/* At this point 'hostname' should contain the
			 * name that was found
			 */
			if (ng_innetgr(client->str, hostname)) {
				goto out;
			}
			break;

		case WILDCARDHOST_CLIENT:
			/* Now checking for IP wildcards */
			if (ipvalid < 0)
				ipvalid = sprint_sockip(hostaddr, ipstring,
							sizeof(ipstring));

			if (ipvalid && (fnmatch(client->str, ipstring,
						FNM_PATHNAME) == 0)) {
				goto out;
			}

			/* Try to get the entry from th IP/name cache */
			rc = nfs_ip_name_get(hostaddr, hostname,
					     sizeof(hostname));

			if (rc == IP_NAME_NOT_FOUND) {
				/* IPaddr was not cached, add it to the cache */

				/** @todo this change from 1.5 is not IPv6
				 * useful.  come back to this and use the
				 * string from client mgr inside op_context...
				 */
				rc = nfs_ip_name_add(hostaddr, hostname,
						     sizeof(hostname));
			}

			if (rc != IP_NAME_SUCCESS)
				break;

			/* At this point 'hostname' should contain the
			 * name that was found
			 */
			if (fnmatch(client->str, hostname, FNM_PATHNAME) == 0) {
				goto out;
			}
			break;

		case GSSPRINCIPAL_CLIENT:
			/** @todo BUGAZOMEU a completer lors de l'integration de RPCSEC_GSS */
			LogCrit(COMPONENT_EXPORT,
				"Unsupported type GSS_PRINCIPAL_CLIENT");
			break;

		case MATCH_ANY_CLIENT:
			goto out;

		case BAD_CLIENT:
		default:
			continue;
		}
	}

	client = NULL;

out:

	return client;
}

bool haproxy_match(SVCXPRT *xprt)
{
	struct base_client_entry *host = NULL;

	PTHREAD_RWLOCK_rdlock(&nfs_core_lock);

	if (glist_empty(&nfs_param.core_param.haproxy_hosts))
		goto out;

	/* Does the host match anyone on the host list? */
	host = client_match(COMPONENT_DISPATCH, " for HAProxy",
			    &xprt->xp_proxy.ss,
			    &nfs_param.core_param.haproxy_hosts, NULL);
out:

	PTHREAD_RWLOCK_unlock(&nfs_core_lock);

	return host != NULL;
}

/** @} */
