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
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
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

/* Clients are stored in an AVL tree
 */

struct client_by_ip {
	struct avltree t;
	pthread_rwlock_t lock;
	struct avltree_node **cache;
	uint32_t cache_sz;
};

static struct client_by_ip client_by_ip;

/**
 * @brief Compute cache slot for an entry
 *
 * This function computes a hash slot, taking an address modulo the
 * number of cache slotes (which should be prime).
 *
 * @param wt [in] The table
 * @param ptr [in] Entry address
 *
 * @return The computed offset.
 */
static inline uint32_t eip_cache_offsetof(struct client_by_ip *eid, uint32_t k)
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
	if (lk->addr.len != rk->addr.len)
		return (lk->addr.len < rk->addr.len) ? -1 : 1;
	else
		return memcmp(lk->addr.addr, rk->addr.addr, lk->addr.len);
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
	char hoststr[SOCK_NAME_MAX];
	uint8_t *addr = NULL;
	uint32_t ipaddr;
	int addr_len = 0;
	void **cache_slot;

	switch (client_ipaddr->ss_family) {
	case AF_INET:
		addr =
		    (uint8_t *) &((struct sockaddr_in *)client_ipaddr)->
		    sin_addr;
		addr_len = 4;
		memcpy(&ipaddr,
		       (uint8_t *) &((struct sockaddr_in *)client_ipaddr)->
		       sin_addr, sizeof(ipaddr));
		break;
	case AF_INET6:
		addr =
		    (uint8_t *) &((struct sockaddr_in6 *)client_ipaddr)->
		    sin6_addr;
		addr_len = 16;
		memcpy(&ipaddr,
		       (uint8_t *) &((struct sockaddr_in6 *)client_ipaddr)->
		       sin6_addr, sizeof(ipaddr));
		break;
	default:
		assert(0);
	}
	v.addr.addr = addr;
	v.addr.len = addr_len;

	PTHREAD_RWLOCK_rdlock(&client_by_ip.lock);

	/* check cache */
	cache_slot = (void **)
	    &(client_by_ip.cache[eip_cache_offsetof(&client_by_ip, ipaddr)]);
	node = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
	if (node) {
		if (client_ip_cmpf(&v.node_k, node) == 0) {
			/* got it in 1 */
			LogDebug(COMPONENT_HASHTABLE_CACHE,
				 "client_mgr cache hit slot %d",
				 eip_cache_offsetof(&client_by_ip, ipaddr));
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
		PTHREAD_RWLOCK_unlock(&client_by_ip.lock);
		return NULL;
	}
	PTHREAD_RWLOCK_unlock(&client_by_ip.lock);

	server_st = gsh_calloc(1, (sizeof(struct server_stats) + addr_len));

	if (server_st == NULL)
		return NULL;

	cl = &server_st->client;
	memcpy(cl->addrbuf, addr, addr_len);
	cl->addr.addr = cl->addrbuf;
	cl->addr.len = addr_len;
	cl->refcnt = 0;		/* we will hold a ref starting out... */
	sprint_sockip(client_ipaddr, hoststr, SOCK_NAME_MAX);
	cl->hostaddr_str = gsh_strdup(hoststr);

	PTHREAD_RWLOCK_wrlock(&client_by_ip.lock);
	node = avltree_insert(&cl->node_k, &client_by_ip.t);
	if (node) {
		gsh_free(server_st);	/* somebody beat us to it */
		cl = avltree_container_of(node, struct gsh_client, node_k);
	} else {
		pthread_rwlock_init(&cl->lock, NULL);
	}

 out:
	atomic_inc_int64_t(&cl->refcnt);
	PTHREAD_RWLOCK_unlock(&client_by_ip.lock);
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
 * @return true if removed or was not found, false if busy.
 */

bool remove_gsh_client(sockaddr_t *client_ipaddr)
{
	struct avltree_node *node = NULL;
	struct avltree_node *cnode = NULL;
	struct gsh_client *cl = NULL;
	struct server_stats *server_st;
	struct gsh_client v;
	uint8_t *addr = NULL;
	uint32_t ipaddr;
	int addr_len = 0;
	bool removed = true;
	void **cache_slot;

	switch (client_ipaddr->ss_family) {
	case AF_INET:
		addr =
		    (uint8_t *) &((struct sockaddr_in *)client_ipaddr)->
		    sin_addr;
		addr_len = 4;
		memcpy(&ipaddr,
		       (uint8_t *) &((struct sockaddr_in *)client_ipaddr)->
		       sin_addr, sizeof(ipaddr));
		break;
	case AF_INET6:
		addr =
		    (uint8_t *) &((struct sockaddr_in6 *)client_ipaddr)->
		    sin6_addr;
		addr_len = 16;
		memcpy(&ipaddr,
		       (uint8_t *) &((struct sockaddr_in6 *)client_ipaddr)->
		       sin6_addr, sizeof(ipaddr));
		break;
	default:
		assert(0);
	}
	v.addr.addr = addr;
	v.addr.len = addr_len;

	PTHREAD_RWLOCK_wrlock(&client_by_ip.lock);
	node = avltree_lookup(&v.node_k, &client_by_ip.t);
	if (node) {
		cl = avltree_container_of(node, struct gsh_client, node_k);
		if (cl->refcnt > 0) {
			removed = false;
			goto out;
		}
		cache_slot = (void **)
		    &(client_by_ip.
		      cache[eip_cache_offsetof(&client_by_ip, ipaddr)]);
		cnode = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
		if (node == cnode)
			atomic_store_voidptr(cache_slot, NULL);
		avltree_remove(node, &client_by_ip.t);
	}
 out:
	PTHREAD_RWLOCK_unlock(&client_by_ip.lock);
	if (removed && node) {
		server_st = container_of(cl, struct server_stats, client);
		server_stats_free(&server_st->st);
		if (cl->hostaddr_str != NULL)
			gsh_free(cl->hostaddr_str);
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

	PTHREAD_RWLOCK_rdlock(&client_by_ip.lock);
	for (client_node = avltree_first(&client_by_ip.t); client_node != NULL;
	     client_node = avltree_next(client_node)) {
		cl = avltree_container_of(client_node, struct gsh_client,
					  node_k);
		if (!cb(cl, state))
			break;
		cnt++;
	}
	PTHREAD_RWLOCK_unlock(&client_by_ip.lock);
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
	unsigned char addrbuf[16];
	bool success = true;

	if (args == NULL) {
		success = false;
		*errormsg = "message has no arguments";
	} else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(args)) {
		success = false;
		*errormsg = "arg not a string";
	} else {
		dbus_message_iter_get_basic(args, &client_addr);
		if (inet_pton(AF_INET, client_addr, addrbuf) == 1) {
			sp->ss_family = AF_INET;
			memcpy(&(((struct sockaddr_in *)sp)->sin_addr), addrbuf,
			       4);
		} else if (inet_pton(AF_INET6, client_addr, addrbuf) == 1) {
			sp->ss_family = AF_INET6;
			memcpy(&(((struct sockaddr_in6 *)sp)->sin6_addr),
			       addrbuf, 16);
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
	dbus_status_reply(&iter, success, errormsg);
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
	bool success = true;
	char *errormsg;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	success = arg_ipaddr(args, &sockaddr, &errormsg);
	if (success)
		success = remove_gsh_client(&sockaddr);
	errormsg = success ? "OK" : "Client with that address not found";
	dbus_status_reply(&iter, success, errormsg);
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

static bool client_to_dbus(struct gsh_client *cl_node, void *state)
{
	struct showclients_state *iter_state =
	    (struct showclients_state *)state;
	struct server_stats *cl;
	char ipaddr[64];
	const char *addrp;
	int addr_type;
	DBusMessageIter struct_iter;
	struct timespec last_as_ts = ServerBootTime;

	cl = container_of(cl_node, struct server_stats, client);
	addr_type = (cl_node->addr.len == 4) ? AF_INET : AF_INET6;
	addrp =
	    inet_ntop(addr_type, cl_node->addr.addr, ipaddr, sizeof(ipaddr));
	timespec_add_nsecs(cl_node->last_update, &last_as_ts);
	dbus_message_iter_open_container(&iter_state->client_iter,
					 DBUS_TYPE_STRUCT, NULL, &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &addrp);
	server_stats_summary(&struct_iter, &cl->st);
	dbus_append_timestamp(&struct_iter, &last_as_ts);
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
	dbus_append_timestamp(&iter, &timestamp);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 "(sbbbbbbbb(tt))",
					 &iter_state.client_iter);

	(void)foreach_gsh_client(client_to_dbus, (void *)&iter_state);

	dbus_message_iter_close_container(&iter, &iter_state.client_iter);
	return true;
}

static struct gsh_dbus_method cltmgr_show_clients = {
	.name = "ShowClients",
	.method = gsh_client_showclients,
	.args = {TIMESTAMP_REPLY,
		 {
		  .name = "clients",
		  .type = "a(sbbbbbbbb(tt))",
		  .direction = "out"},
		 END_ARG_LIST}
};

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
	bool success = true;

	success = arg_ipaddr(args, &sockaddr, errormsg);

	if (success)
		return get_gsh_client(&sockaddr, true);
	else
		return NULL;
}

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
	dbus_status_reply(&iter, success, errormsg);
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
	dbus_status_reply(&iter, success, errormsg);
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
	dbus_status_reply(&iter, success, errormsg);
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
	dbus_status_reply(&iter, success, errormsg);
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

	dbus_status_reply(&iter, success, errormsg);
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
	dbus_status_reply(&iter, success, errormsg);
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
	dbus_status_reply(&iter, success, errormsg);
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


static struct gsh_dbus_method *cltmgr_stats_methods[] = {
	&cltmgr_show_v3_io,
	&cltmgr_show_v40_io,
	&cltmgr_show_v41_io,
	&cltmgr_show_v41_layouts,
	&cltmgr_show_delegations,
	&cltmgr_show_9p_io,
	&cltmgr_show_9p_trans,
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

/**
 * @brief Initialize client manager
 */

void client_pkginit(void)
{
	pthread_rwlockattr_t rwlock_attr;

	pthread_rwlockattr_init(&rwlock_attr);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&rwlock_attr,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	pthread_rwlock_init(&client_by_ip.lock, &rwlock_attr);
	avltree_init(&client_by_ip.t, client_ip_cmpf, 0);
	client_by_ip.cache_sz = 32767;
	client_by_ip.cache =
	    gsh_calloc(client_by_ip.cache_sz, sizeof(struct avltree_node *));
}

/** @} */
