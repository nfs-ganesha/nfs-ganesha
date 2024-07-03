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
 * @file connection_manager.h
 * @author Yoni Couriel <yonic@google.com>
 * @brief Allows a client to be connected to a single Ganesha server at a time.
 *
 * This modules mitigates the Exactly-Once-Semantics issue when running
 * multiple Ganesha servers in a cluster.
 *
 * A client is all the connections from the same source IP address.
 *
 * The scenario is described here:
 * https://www.rfc-editor.org/rfc/rfc8881.html#section-2.10.6-6
 * When applied to multiple Ganesha servers this scenario can happen since
 * Ganesha servers don't share their EOS-reply-cache with each other:
 * 1. The Client sends "WRITE A" to Server 1
 * 2. Server 1 is slow to process the request (it might have too much load from
 * other clients)
 * 3. The Client connects to Server 2 (it might be due to load balancing)
 * 4. The Client establishes a session with Server 2, and sends "WRITE A"
 * 5. Server 2 executes the request and sends a success response
 * 6. The Client sends "WRITE B" to Server 2, and the server executes it
 * 7. Server 1 executes the old "WRITE A" request from step (1), overriding
 * "WRITE B" from step (6)
 *
 * Step (5) above won't happen if we had a cluster-wide EOS-reply-cache, or if
 * the client can't execute requests in Server 2 before all its requests
 * are completed in Server 1. This module implements the latter:
 * 1. When a client connects to a new Ganesha server, the server sends a "DRAIN"
 * request to all the other Ganesha servers in the cluster
 * 2. When a Ganesha server gets a "DRAIN" request, it closes and waits for the
 * connections of that client
 * 3. Only after a successful "DRAIN", the client is allowed to connect to the
 * new Ganesha server
 *
 * When an NFSv4 client connects to a new server, they are allowed to RECLAIM
 * their state. When using the Connection Manager, we need to extend the lease
 * time of the client state after draining, otherwise we are exposed to the
 * following (rare) scenario:
 * 1. The Client has a lock in Server 1
 * 2. The Client connects to Server 2 (it might be due to load balancing)
 * 3. Server 2 starts draining all the other servers in the cluster
 * 4. Server 1 is successfully drained, and the lease timeout is set to
 *      now + Lease_Lifetime
 * 5. Server 3, however, is very slow to respond, and delays the draining
 * 6. The lease times-out, and Server 1 releases the Client's lock
 * 7. Another client takes that lock
 * 8. Server 3 either: finishes the drain, or "kicked-out" from the cluster
 * 9. The Client is finally allowed to connect to Server 2, but it won't be able
 *      to RECLAIM their lock (because of step 7)
 * The solution is to extend the lease time after draining, to be:
 * now + Lease_Lifetime + "Max time before kicking-out non-responsive servers"
 *
 * Usage:
 * 1. Set "Enable_Connection_Manager" in the config.
 * 2. Use connection_manager__callback_set to register a callback that
 * sends the "DRAIN" request to the other Ganesha servers in the cluster. The
 * callback is called each time a new client connects to the current Ganesha
 * server, and we block-wait until the callback finishes successfully before
 * we allow that client to issue requests.
 * 3. When receiving a "DRAIN" request, use
 * connection_manager__drain_and_disconnect_local to drain the current Ganesha
 * server.
 */
#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "common_utils.h"

enum connection_manager__drain_t {
	/* Drain was successful */
	CONNECTION_MANAGER__DRAIN__SUCCESS = 0,
	/* Drain was vacuously successful, there were no active connections by
	 * the client */
	CONNECTION_MANAGER__DRAIN__SUCCESS_NO_CONNECTIONS,
	/* Drain failed, most likely due to a new incoming connection that
	 * aborted the draining process, or because we were busy draining other
	 * servers */
	CONNECTION_MANAGER__DRAIN__FAILED,
	/* Drain failed due to timeout */
	CONNECTION_MANAGER__DRAIN__FAILED_TIMEOUT,

	/* Number of drain results (for monitoring) */
	CONNECTION_MANAGER__DRAIN__LAST,
};

typedef enum connection_manager__drain_t
(*connection_manager__callback_drain_t)(
	/* User provided context */
	void *user_context,
	/* Client to drain */
	const sockaddr_t *client_address,
	/* Client address string for logging/debugging */
	const char *client_address_str,
	/* Timeout for the draining */
	const struct timespec *timeout);

typedef struct connection_manager__callback_context_t {
	/* User provided context */
	void *user_context;
	/* Sends a "DRAIN" request to the other Ganesha servers in the
	 * cluster */
	connection_manager__callback_drain_t
		drain_and_disconnect_other_servers;
} connection_manager__callback_context_t;

/* A client steady state can be either DRAINED or ACTIVE.
 * The transition DRAINED -> ACTIVE is called ACTIVATING.
 * The transition ACTIVE -> DRAINED is called DRAINING.
 * If the transition fails, the state reverts back:
 *
 *         +-----------+            +----------+
 *   +----->  DRAINED  <---Success--+ DRAINING +-----+
 *   |     +----+------+            +----^-----+     |
 * Failed       |                        |           |
 *   |     New connection          Drain request     |
 *   |          |                        |        Failed
 *   |     +----v-------+           +----+-----+     |
 *   +-----+ ACTIVATING +--Success-->  ACTIVE  <-----+
 *         +------------+           +----------+
 *
 * Created with: asciiflow.com
 */
enum connection_manager__client_state_t {
	/* In this state, new connections will transition to ACTIVATING state
	 * and try to drain other servers */
	CONNECTION_MANAGER__CLIENT_STATE__DRAINED = 0,
	/* In this state, new connections will block-wait until the state is
	 * changed */
	CONNECTION_MANAGER__CLIENT_STATE__ACTIVATING,
	/* In this state, new connections are allowed immediately, without going
	 * through the process of draining other servers */
	CONNECTION_MANAGER__CLIENT_STATE__ACTIVE,
	/* In this state, new connections will abort the local draining process
	 * and transition back to ACTIVE state */
	CONNECTION_MANAGER__CLIENT_STATE__DRAINING,

	/* Number of client states (for monitoring) */
	CONNECTION_MANAGER__CLIENT_STATE__LAST,
};

enum connection_manager__connection_started_t {
	/* The new connection is allowed to be created and execute requests */
	CONNECTION_MANAGER__CONNECTION_STARTED__ALLOW = 0,
	/* The draining process in other servers failed, the new connection
	 * should be dropped */
	CONNECTION_MANAGER__CONNECTION_STARTED__DROP,

	/* Number of connection started results (for monitoring) */
	CONNECTION_MANAGER__CONNECTION_STARTED__LAST,
};

typedef struct connection_manager__connection_t {
	/* When false, fields below are unused */
	bool is_managed;
	/* We don't have ownership on XPRT, when the XPRT is destroyed it calls
	 * connection_manager__connection_finished which destroys this struct */
	SVCXPRT *xprt;
	/* We have ownership on gsh_client, and it should be released when this
	 * struct is destroyed */
	struct gsh_client *gsh_client;
	/* connections list in connection_manager__client_t */
	struct glist_head node;
} connection_manager__connection_t;

typedef struct connection_manager__client_t {
	enum connection_manager__client_state_t state;
	/* Protects this struct */
	pthread_mutex_t mutex;
	/* Notified on state/connections change */
	pthread_cond_t cond_change;
	/* List of connection_manager__connection_t */
	struct glist_head connections;
	uint32_t connections_count;
} connection_manager__client_t;

/**
 * Sets the callbacks to be called on draining
 * Can be called only on init or after "clear" was called
 */
void connection_manager__callback_set(connection_manager__callback_context_t);
/**
 * Clears the drain callbacks, and returns the last stored callbacks struct
 * Can be called only after "set" was called
 */
connection_manager__callback_context_t connection_manager__callback_clear(void);

/**
 * Called from client_mgr when a new gsh_client is created.
 */
void connection_manager__client_init(connection_manager__client_t *);
/**
 * Called from client_mgr when a gsh_client is destroyed.
 */
void connection_manager__client_fini(connection_manager__client_t *);

/**
 * Called when a new connection is created.
 */
void connection_manager__connection_init(SVCXPRT *);

/**
 * Called after connection_init and when client address is determined.
 * Client address could be obtained on connection establish, however, in case of
 * proxy protocol, client address can be obtained with the first request
 * received on the connection.
 * When this module is enabled, this method blocks until all the other Ganesha
 * servers in the cluster drain and close the connections by this client
 * Might fail when timeout is reached, and in that case the connection is
 * destroyed.
 * In case there are any draining processes in progress
 * (connection_manager__drain_and_disconnect_local), they are aborted so the new
 * connection gets the priority.
 */
enum connection_manager__connection_started_t
connection_manager__connection_started(SVCXPRT *);

/**
 * Called when a connection is closed
 * Updates the connection list, and potentially notifies the draining process
 */
void connection_manager__connection_finished(const SVCXPRT *);

/**
 * Calls SVC_DESTROY on the client's connections, and block-waits until they
 * are closed, or until timeout.
 * The "drain_and_disconnect_other_servers" callback should send a "DRAIN"
 * request to the other Ganesha servers in the cluster. When a "DRAIN" request
 * is received, this method should be called.
 */
enum connection_manager__drain_t
connection_manager__drain_and_disconnect_local(sockaddr_t *);

#endif /* CONNECTION_MANAGER_H */
