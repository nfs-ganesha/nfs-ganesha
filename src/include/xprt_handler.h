/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2023 Google LLC
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
 * @file xprt_handler.h
 * @brief Functionality related to service transport.
 */

#ifndef XPRT_HANDLER_H
#define XPRT_HANDLER_H

#include "gsh_rpc.h"
#include "sal_data.h"
#include "connection_manager.h"

typedef struct nfs41_session_list_entry {
	nfs41_session_t *session;
	struct glist_head node;
} nfs41_session_list_entry_t;

typedef struct nfs41_sessions_holder {
	pthread_rwlock_t sessions_lock;
	struct glist_head sessions;
} nfs41_sessions_holder_t;

typedef enum xprt_custom_data_status {
	ASSOCIATED_TO_XPRT = 0,
	DISSOCIATED_FROM_XPRT,
	DESTROYED,
	XPRT_CUSTOM_DATA_STATUS_COUNT,
} xprt_custom_data_status_t;

/* Represents miscellaneous data related to the svc-xprt */
typedef struct xprt_custom_data {
	nfs41_sessions_holder_t nfs41_sessions_holder;
	xprt_custom_data_status_t status;
	connection_manager__connection_t managed_connection;
} xprt_custom_data_t;

void init_custom_data_for_xprt(SVCXPRT *);
void destroy_custom_data_for_destroyed_xprt(SVCXPRT *);
void dissociate_custom_data_from_xprt(SVCXPRT *);
bool add_nfs41_session_to_xprt(SVCXPRT *, nfs41_session_t *);
void remove_nfs41_session_from_xprt(SVCXPRT *, nfs41_session_t *);

#endif				/* XPRT_HANDLER_H */
