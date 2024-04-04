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
#include "server_stats_private.h"
#include "abstract_atomic.h"
#include "gsh_intrinsic.h"
#include "server_stats.h"
#include "sal_functions.h"
#include "nfs_ip_stats.h"
#include "netgroup_cache.h"

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

	return sockaddr_cmpf(&lk->cl_addrbuf, &rk->cl_addrbuf, true);
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
	cache_slot = (void **)
	    &(client_by_ip.cache[eip_cache_offsetof(&client_by_ip, hash)]);
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

	server_st = gsh_calloc(1, sizeof(*server_st));

	cl = &server_st->client;
	cl->cl_addrbuf = *client_ipaddr;
	cl->refcnt = 0;

	if (!sprint_sockip(client_ipaddr, cl->hostaddr_str,
			   sizeof(cl->hostaddr_str))) {
		(void) strlcpy(cl->hostaddr_str, "<unknown>",
			       sizeof(cl->hostaddr_str));
	}

	PTHREAD_RWLOCK_wrlock(&client_by_ip.cip_lock);
	node = avltree_insert(&cl->node_k, &client_by_ip.t);
	if (node) {
		gsh_free(server_st);	/* somebody beat us to it */
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
	int64_t new_refcnt;

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
	uint64_t hash = hash_sockaddr(client_ipaddr, true);

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
		cache_slot = (void **)
		    &(client_by_ip.cache[eip_cache_offsetof(
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
		server_stats_free(&server_st->st);
		server_stats_allops_free(&server_st->c_all);
		connection_manager__client_fini(&cl->connection_manager);
		PTHREAD_RWLOCK_destroy(&cl->client_lock);
		gsh_free(server_st);
	}
	return removed;
}

/**
 * @ Walk the tree and do the callback on each node
 *
 * @param cb    [IN] Callback function
 * @param state [IN] param block to pass
 */

int foreach_gsh_client(bool(*cb) (struct gsh_client *cl, void *state),
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

static bool arg_ipaddr(DBusMessageIter *args, sockaddr_t *sp, char **errormsg)
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

static bool gsh_client_addclient(DBusMessageIter *args,
				 DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 END_ARG_LIST}
};

static bool gsh_client_removeclient(DBusMessageIter *args,
				    DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 END_ARG_LIST}
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
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_STRING,
				       &state_type);
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_UINT64,
				       &cl_node->state_stats[STATE_TYPE_SHARE]);

	state_type = "Lock";
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_STRING,
				       &state_type);
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_UINT64,
				       &cl_node->state_stats[STATE_TYPE_LOCK]);

	state_type = "Delegation";
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_STRING,
				       &state_type);
	dbus_message_iter_append_basic(&ss_iter, DBUS_TYPE_UINT64,
				       &cl_node->state_stats[STATE_TYPE_DELEG]);

	dbus_message_iter_close_container(iter, &ss_iter);
}

static bool client_to_dbus(struct gsh_client *cl_node, void *state)
{
	struct showclients_state *iter_state =
	    (struct showclients_state *)state;
	struct server_stats *cl;
	char *ipaddr = alloca(SOCK_NAME_MAX);
	DBusMessageIter struct_iter;

	cl = container_of(cl_node, struct server_stats, client);

	if (!sprint_sockip(&cl_node->cl_addrbuf, ipaddr, SOCK_NAME_MAX))
		(void) strlcpy(ipaddr, "<unknown>", SOCK_NAME_MAX);

	dbus_message_iter_open_container(&iter_state->client_iter,
					 DBUS_TYPE_STRUCT, NULL, &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &ipaddr);
	server_stats_summary(&struct_iter, &cl->st);
	client_state_stats(&struct_iter, cl_node);
	gsh_dbus_append_timestamp(&struct_iter, &cl_node->last_update);
	dbus_message_iter_close_container(&iter_state->client_iter,
					  &struct_iter);
	return true;
}

static bool gsh_client_showclients(DBusMessageIter *args,
				   DBusMessage *reply,
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
	.args = {TIMESTAMP_REPLY,
		 CLIENTS_REPLY,
		 END_ARG_LIST}
};

/* Reset Client specific stats counters
 */
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

/* Reset Client specific stats counters for allops
 */
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

static struct gsh_dbus_method *cltmgr_client_methods[] = {
	&cltmgr_add_client,
	&cltmgr_remove_client,
	&cltmgr_show_clients,
	NULL
};

static struct gsh_dbus_interface cltmgr_client_table = {
	.name = "org.ganesha.nfsd.clientmgr",
	.props = NULL,
	.methods = cltmgr_client_methods,
	.signals = NULL
};

/* org.ganesha.nfsd.clientstats interface
 */

/* DBUS client manager stats helpers
 */

static struct gsh_client *lookup_client(DBusMessageIter *args, char **errormsg)
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

/**
 * DBUS method to get client IO ops statistics
 */
static bool gsh_client_io_ops(DBusMessageIter *args,
				  DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 CE_STATS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to get all ops statistics for a client
 */
static bool gsh_client_all_ops(DBusMessageIter *args,
				  DBusMessage *reply,
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

static struct gsh_dbus_method cltmgr_client_all_ops = {
	.name = "GetClientAllops",
	.method = gsh_client_all_ops,
	.args = {
			IPADDR_ARG,
			STATUS_REPLY,
			TIMESTAMP_REPLY,
#ifdef _USE_NFS3
			{
				.name = "clnt_v3",
				.type = "b",
				.direction = "out"
			},
			CLNT_V3NLM_OPS_REPLY,
#endif
#ifdef _USE_NLM
			{
				.name = "clnt_nlm",
				.type = "b",
				.direction = "out"
			},
			CLNT_V3NLM_OPS_REPLY,
#endif
			{
				.name = "clnt_v4",
				.type = "b",
				.direction = "out"
			},
			CLNT_V4_OPS_REPLY,
			{
				.name = "clnt_cmp",
				.type = "b",
				.direction = "out"
			},
			CLNT_CMP_OPS_REPLY,
			END_ARG_LIST}
};

#ifdef _USE_NFS3
/**
 * DBUS method to report NFSv3 I/O statistics
 *
 */

static bool get_nfsv3_stats_io(DBusMessageIter *args,
			       DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};
#endif

/**
 * DBUS method to report NFSv40 I/O statistics
 *
 */

static bool get_nfsv40_stats_io(DBusMessageIter *args,
				DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report NFSv41 I/O statistics
 *
 */

static bool get_nfsv41_stats_io(DBusMessageIter *args,
				DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};


/**
 * DBUS method to report NFSv41 layout statistics
 *
 */

static bool get_nfsv41_stats_layouts(DBusMessageIter *args,
				     DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 LAYOUTS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report NFSv42 I/O statistics
 *
 */

static bool get_nfsv42_stats_io(DBusMessageIter *args,
				DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};


/**
 * DBUS method to report NFSv42 layout statistics
 *
 */

static bool get_nfsv42_stats_layouts(DBusMessageIter *args,
				     DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 LAYOUTS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report NFSv4 delegation statistics
 */
static bool get_stats_delegations(DBusMessageIter *args,
				  DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 DELEG_REPLY,
		 END_ARG_LIST}
};

#ifdef _USE_9P
/**
 * DBUS method to report 9p I/O statistics
 *
 */

static bool get_9p_stats_io(DBusMessageIter *args,
			    DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 IOSTATS_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report 9p transport statistics
 *
 */

static bool get_9p_stats_trans(DBusMessageIter *args,
			       DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 TRANSPORT_REPLY,
		 END_ARG_LIST}
};

/**
 * DBUS method to report 9p protocol operation statistics
 *
 */

static bool get_9p_client_op_stats(DBusMessageIter *args,
				   DBusMessage *reply,
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
	.args = {IPADDR_ARG,
		 _9P_OP_ARG,
		 STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 OP_STATS_REPLY,
		 END_ARG_LIST}
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
	&cltmgr_client_all_ops,
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

static struct gsh_dbus_interface *cltmgr_interfaces[] = {
	&cltmgr_client_table,
	&cltmgr_stats_table,
	NULL
};

void dbus_client_init(void)
{
	gsh_dbus_register_path("ClientMgr", cltmgr_interfaces);
}

#endif				/* USE_DBUS */

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
	client_by_ip.cache =
	    gsh_calloc(client_by_ip.cache_sz, sizeof(struct avltree_node *));
	RegisterCleanup(&client_mgr_cleanup_element);
}

static char *client_types[] = {
	[PROTO_CLIENT] = "PROTO_CLIENT",
	[NETWORK_CLIENT] = "NETWORK_CLIENT",
	[NETGROUP_CLIENT] = "NETGROUP_CLIENT",
	[WILDCARDHOST_CLIENT] = "WILDCARDHOST_CLIENT",
	[GSSPRINCIPAL_CLIENT] = "GSSPRINCIPAL_CLIENT",
	[MATCH_ANY_CLIENT] = "MATCH_ANY_CLIENT",
	[BAD_CLIENT] = "BAD_CLIENT"
	 };

int StrClient(struct display_buffer *dspbuf, struct base_client_entry *client)
{
	char *paddr = NULL;
	char *free_paddr = NULL;
	int b_left = display_start(dspbuf);

	switch (client->type) {
	case NETWORK_CLIENT:
		free_paddr = cidr_to_str(client->client.network.cidr,
					 CIDR_NOFLAGS);
		paddr = free_paddr;
		break;

	case NETGROUP_CLIENT:
		paddr = client->client.netgroup.netgroupname;
		break;

	case WILDCARDHOST_CLIENT:
		paddr = client->client.wildcard.wildcard;
		break;

	case GSSPRINCIPAL_CLIENT:
		paddr = client->client.gssprinc.princname;
		break;

	case MATCH_ANY_CLIENT:
		paddr = "*";
		break;

	case PROTO_CLIENT:
	case BAD_CLIENT:
		paddr = "<unknown>";
		break;

	default:
		break;
	}

	if (client->type > BAD_CLIENT) {
		b_left = display_printf(dspbuf, "UNKNOWN_CLIENT_TYPE: 0x%08x",
					client->type);
	} else {
		b_left = display_printf(dspbuf, "%s: %s",
					client_types[client->type], paddr);
	}

	gsh_free(free_paddr);

	return b_left;
}

void LogClientListEntry(enum log_components component,
			log_levels_t level,
			int line,
			const char *func,
			const char *tag,
			struct base_client_entry *entry)
{
	char buf[1024] = "\0";
	struct display_buffer dspbuf = {sizeof(buf), buf, buf};
	int b_left = display_start(&dspbuf);

	if (!isLevel(component, level))
		return;

	if (b_left > 0 && tag != NULL)
		b_left = display_cat(&dspbuf, tag);

	if (b_left > 0 && level >= NIV_DEBUG)
		b_left = display_printf(&dspbuf, "%p ", entry);

	if (b_left > 0)
		b_left = StrClient(&dspbuf, entry);

	DisplayLogComponentLevel(component,
				 (char *) __FILE__, line, func, level,
				 "%s", buf);

}

void FreeClientList(struct glist_head *clients, client_free_func free_func)
{
	struct glist_head *glist;
	struct glist_head *glistn;

	glist_for_each_safe(glist, glistn, clients) {
		struct base_client_entry *client;

		client = glist_entry(glist,
				     struct base_client_entry,
				     cle_list);

		glist_del(&client->cle_list);
		switch (client->type) {
		case NETWORK_CLIENT:
			if (client->client.network.cidr != NULL)
				cidr_free(client->client.network.cidr);
			break;
		case NETGROUP_CLIENT:
			gsh_free(client->client.netgroup.netgroupname);
			break;
		case WILDCARDHOST_CLIENT:
			gsh_free(client->client.wildcard.wildcard);
			break;
		case GSSPRINCIPAL_CLIENT:
			gsh_free(client->client.gssprinc.princname);
			break;
		case PROTO_CLIENT:
		case MATCH_ANY_CLIENT:
		case BAD_CLIENT:
			/* Do nothing for these client types */
			break;
		}
		free_func(client);
	}
}

void *base_client_allocator(void)
{
	return gsh_calloc(1, sizeof(struct base_client_entry));
}

/**
 * @brief Expand the client name token into one or more client entries
 *
 * @param component     [IN]  component for logging
 * @param client_list   [IN]  the client list this gets linked to (in tail order)
 * @param client_tok    [IN]  the name string.  We modify it.
 * @param type_hint     [IN]  type hint from parser for client_tok
 * @param cnode         [IN]  opaque pointer needed for config_proc_error()
 * @param err_type      [OUT] error handling ref
 * @param cle_allocator [IN]  function to allocate a list entry
 * @param cle_filler    [IN]  function to fill in a list entry
 * @param private_data  [IN]  data to be passed to cle_filler function
 *
 * @returns 0 on success, error count on failure
 */

int add_client(enum log_components component,
	       struct glist_head *client_list,
	       const char *client_tok,
	       enum term_type type_hint,
	       void *cnode,
	       struct config_error_type *err_type,
	       client_list_entry_allocator_t cle_allocator,
	       client_list_entry_filler_t cle_filler,
	       void *private_data)
{
	int errcnt = 0;
	struct addrinfo *info;
	CIDR *cidr;
	int rc;
	struct base_client_entry *cli;

	if (cle_allocator == NULL)
		cle_allocator = base_client_allocator;

	cli = cle_allocator();

	cli->client.network.cidr = NULL;
	glist_init(&cli->cle_list);
	switch (type_hint) {
	case TERM_V4_ANY:
		cli->type = MATCH_ANY_CLIENT;
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
		cli->client.netgroup.netgroupname = gsh_strdup(client_tok + 1);
		cli->type = NETGROUP_CLIENT;
		break;
	case TERM_V4CIDR:
	case TERM_V6CIDR:
	case TERM_V4ADDR:
	case TERM_V6ADDR:
		cidr = cidr_from_str(client_tok);
		if (cidr == NULL) {
			switch (type_hint) {
			case TERM_V4CIDR:
				config_proc_error(cnode, err_type,
						  "Expected a IPv4 CIDR address, got (%s)",
						  client_tok);
				break;
			case TERM_V6CIDR:
				config_proc_error(cnode, err_type,
						  "Expected a IPv6 CIDR address, got (%s)",
						  client_tok);
				break;
			case TERM_V4ADDR:
				config_proc_error(cnode, err_type,
						  "IPv4 addr (%s) not in presentation format",
						  client_tok);
				break;
			case TERM_V6ADDR:
				config_proc_error(cnode, err_type,
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
		cli->client.network.cidr = cidr;
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
		cli->client.wildcard.wildcard = gsh_strdup(client_tok);
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
				LogFullDebug(COMPONENT_EXPORT,
					     "flags=%d family=%d socktype=%d protocol=%d addrlen=%d name=%s",
					     ap->ai_flags,
					     ap->ai_family,
					     ap->ai_socktype,
					     ap->ai_protocol,
					     (int) ap->ai_addrlen,
					     ap->ai_canonname);

				if (cli == NULL) {
					cli = cle_allocator();
					glist_init(&cli->cle_list);
				}

				if (ap->ai_family == AF_INET &&
				    (ap->ai_socktype == SOCK_STREAM ||
				     ap->ai_socktype == SOCK_DGRAM)) {
					struct in_addr infoaddr =
						((struct sockaddr_in *)
						 ap->ai_addr)->sin_addr;
					if (ap_last != NULL &&
					    ap_last->ai_family
					    == ap->ai_family &&
					    memcmp(&infoaddr,
						   &in_addr_last,
						   sizeof(struct in_addr)) == 0)
						continue;
					cli->client.network.cidr =
						cidr_from_inaddr(&infoaddr);
					cli->type = NETWORK_CLIENT;
					ap_last = ap;
					in_addr_last = infoaddr;

				} else if (ap->ai_family == AF_INET6 &&
					   (ap->ai_socktype == SOCK_STREAM ||
					    ap->ai_socktype == SOCK_DGRAM)) {
					struct in6_addr infoaddr =
						((struct sockaddr_in6 *)
						 ap->ai_addr)->sin6_addr;

					if (ap_last != NULL &&
					    ap_last->ai_family == ap->ai_family
					    &&  !memcmp(&infoaddr,
						       &in6_addr_last,
						       sizeof(struct in6_addr)))
						continue;
					/* IPv6 address */
					cli->client.network.cidr =
						cidr_from_in6addr(&infoaddr);
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
				  config_term_desc(type_hint),
				  client_tok);
		err_type->bogus = true;
		errcnt++;
		goto out;
	}

	if (cle_filler != NULL)
		cle_filler(cli, private_data);
	else
		LogMidDebug_ClientListEntry(component, "", cli);

	glist_add_tail(client_list, &cli->cle_list);
	cli = NULL;
out:
	gsh_free(cli);
	return errcnt;
}

/**
 * @brief Match a specific client in a client list
 *
 * @param[in]  hostaddr      Host to search for
 * @param[in]  clients       Client list to search
 *
 * @return the client entry or NULL if failure.
 */
struct base_client_entry *client_match(enum log_components component,
				       const char *str,
				       sockaddr_t *clientaddr,
				       struct glist_head *clients)
{
	struct glist_head *glist;
	int rc;
	int ipvalid = -1;	/* -1 need to print, 0 - invalid, 1 - ok */
	char hostname[NI_MAXHOST];
	char ipstring[SOCK_NAME_MAX];
	CIDR *host_prefix = NULL;
	struct base_client_entry *client;
	sockaddr_t alt_hostaddr;
	sockaddr_t *hostaddr = NULL;

	hostaddr = convert_ipv6_to_ipv4(clientaddr, &alt_hostaddr);

	if (isMidDebug(component)) {
		char ipstring[SOCK_NAME_MAX];
		struct display_buffer dspbuf = {
					sizeof(ipstring), ipstring, ipstring};

		display_sockip(&dspbuf, hostaddr);

		LogMidDebug(component,
			    "Check for address %s%s",
			    ipstring, str ? str : "");
	}

	glist_for_each(glist, clients) {
		client = glist_entry(glist, struct base_client_entry, cle_list);
		LogMidDebug_ClientListEntry(component, "Match V4: ", client);

		switch (client->type) {
		case NETWORK_CLIENT:
			if (host_prefix == NULL) {
				if (hostaddr->ss_family == AF_INET6) {
					host_prefix = cidr_from_in6addr(
						&((struct sockaddr_in6 *)
							hostaddr)->sin6_addr);
				} else {
					host_prefix = cidr_from_inaddr(
						&((struct sockaddr_in *)
							hostaddr)->sin_addr);
				}
			}

			if (cidr_contains(client->client.network.cidr,
					  host_prefix) == 0) {
				goto out;
			}
			break;

		case NETGROUP_CLIENT:
			/* Try to get the entry from th IP/name cache */
			rc = nfs_ip_name_get(hostaddr, hostname,
					     sizeof(hostname));

			if (rc == IP_NAME_NOT_FOUND) {
				/* IPaddr was not cached, add it to the cache */
				rc = nfs_ip_name_add(hostaddr,
						     hostname,
						     sizeof(hostname));
			}

			if (rc != IP_NAME_SUCCESS)
				break; /* Fatal failure */

			/* At this point 'hostname' should contain the
			 * name that was found
			 */
			if (ng_innetgr(client->client.netgroup.netgroupname,
				    hostname)) {
				goto out;
			}
			break;

		case WILDCARDHOST_CLIENT:
			/* Now checking for IP wildcards */
			if (ipvalid < 0)
				ipvalid = sprint_sockip(hostaddr,
							ipstring,
							sizeof(ipstring));

			if (ipvalid &&
			    (fnmatch(client->client.wildcard.wildcard,
				     ipstring,
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
				rc = nfs_ip_name_add(hostaddr,
						     hostname,
						     sizeof(hostname));
			}

			if (rc != IP_NAME_SUCCESS)
				break;

			/* At this point 'hostname' should contain the
			 * name that was found
			 */
			if (fnmatch
			    (client->client.wildcard.wildcard, hostname,
			     FNM_PATHNAME) == 0) {
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

	if (host_prefix != NULL)
		cidr_free(host_prefix);

	return client;

}

bool haproxy_match(SVCXPRT *xprt)
{
	struct base_client_entry *host = NULL;

	if (glist_empty(&nfs_param.core_param.haproxy_hosts))
		return false;

	/* Does the host match anyone on the host list? */
	host = client_match(COMPONENT_DISPATCH, " for HAProxy",
			    &xprt->xp_proxy.ss,
			    &nfs_param.core_param.haproxy_hosts);

	return host != NULL;
}

/** @} */
