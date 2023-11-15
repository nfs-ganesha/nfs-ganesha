// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
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
 * @file xprt_handler.c
 * @brief This file handles functionality related to service transport.
 */

#include "xprt_handler.h"
#include "nfs_core.h"
#include "sal_functions.h"

/**
 * @brief Inits the xprt's user-data represented by the `xprt_custom_data_t`
 * struct.
 *
 * For each xprt, it must be called only once, during the xprt initialisation.
 */
void init_custom_data_for_xprt(SVCXPRT *xprt)
{
	xprt_custom_data_t *xprt_data;
	char sockaddr_str[SOCK_NAME_MAX] = "\0";
	struct display_buffer db = {
		sizeof(sockaddr_str), sockaddr_str, sockaddr_str};

	assert(xprt->xp_u1 == NULL);
	xprt_data = gsh_malloc(sizeof(xprt_custom_data_t));

	glist_init(&xprt_data->nfs41_sessions_holder.sessions);
	PTHREAD_RWLOCK_init(
		&xprt_data->nfs41_sessions_holder.sessions_lock, NULL);
	xprt->xp_u1 = (void *) xprt_data;
	xprt_data->status = ASSOCIATED_TO_XPRT;

	display_xprt_sockaddr(&db, xprt);
	LogInfo(COMPONENT_XPRT,
		"xp_u1 initialised for xprt with FD: %d and socket-addr: %s",
		xprt->xp_fd, sockaddr_str);
}

/**
 * @brief Adds the nfs41_session to the xprt session-list
 *
 * @note The caller must call this only after verifying that the xprt
 * is not already associated with the session
 */
bool add_nfs41_session_to_xprt(SVCXPRT *xprt, nfs41_session_t *session)
{
	xprt_custom_data_t *xprt_data;
	nfs41_session_list_entry_t *new_entry;

	assert(xprt->xp_u1 != NULL);
	xprt_data = (xprt_custom_data_t *) xprt->xp_u1;

	new_entry = gsh_malloc(sizeof(nfs41_session_list_entry_t));
	new_entry->session = session;
	inc_session_ref(session);

	PTHREAD_RWLOCK_wrlock(
		&xprt_data->nfs41_sessions_holder.sessions_lock);

	/* It is possible that the xprt_data is already being dissociated from
	 * xprt. If so, we do not want to associate such xprt to the session.
	 */
	if (xprt_data->status == DISSOCIATED_FROM_XPRT) {
		PTHREAD_RWLOCK_unlock(
			&xprt_data->nfs41_sessions_holder.sessions_lock);
		LogWarn(COMPONENT_SESSIONS,
			"Do not associate xprt-data under dissociation with xprt FD: %d to the session",
			xprt->xp_fd);
		dec_session_ref(session);
		gsh_free(new_entry);
		return false;
	}

	glist_add_tail(&xprt_data->nfs41_sessions_holder.sessions,
		&new_entry->node);
	PTHREAD_RWLOCK_unlock(
		&xprt_data->nfs41_sessions_holder.sessions_lock);
	return true;
}

/**
 * @brief Removes the nfs41_session from the xprt session-list
 */
void remove_nfs41_session_from_xprt(SVCXPRT *xprt,
	nfs41_session_t *session)
{
	struct glist_head *curr_node, *next_node;
	xprt_custom_data_t *xprt_data;
	nfs41_sessions_holder_t *sessions_holder;

	assert(xprt->xp_u1 != NULL);
	xprt_data = (xprt_custom_data_t *) xprt->xp_u1;
	sessions_holder = &xprt_data->nfs41_sessions_holder;

	PTHREAD_RWLOCK_wrlock(&sessions_holder->sessions_lock);

	glist_for_each_safe(curr_node, next_node, &sessions_holder->sessions) {
		nfs41_session_list_entry_t * const curr_entry = glist_entry(
			curr_node, nfs41_session_list_entry_t, node);

		if (curr_entry->session == session) {
			dec_session_ref(curr_entry->session);
			glist_del(curr_node);
			gsh_free(curr_entry);
		}
	}
	PTHREAD_RWLOCK_unlock(&sessions_holder->sessions_lock);
}

/**
 * @brief handles cleanup of the custom data associated with the xprt
 * (if any), after the xprt is destroyed
 *
 * It is supposed to be invoked only once after the xprt's connection is
 * closed.
 */
void destroy_custom_data_for_destroyed_xprt(SVCXPRT *xprt)
{
	char sockaddr_str[SOCK_NAME_MAX] = "\0";
	struct display_buffer db = {
		sizeof(sockaddr_str), sockaddr_str, sockaddr_str};
	xprt_custom_data_t *xprt_data;

	display_xprt_sockaddr(&db, xprt);
	LogInfo(COMPONENT_XPRT,
		"Processing custom data for destroyed xprt: %p with FD: %d, socket-addr: %s",
		xprt, xprt->xp_fd, sockaddr_str);
	assert(xprt->xp_flags & SVC_XPRT_FLAG_DESTROYED);

	if (xprt->xp_u1 == NULL) {
		LogInfo(COMPONENT_XPRT,
			"No custom data to destroy for the destroyed xprt");
		return;
	}
	xprt_data = (xprt_custom_data_t *) xprt->xp_u1;
	assert(glist_empty(&xprt_data->nfs41_sessions_holder.sessions));
	assert(xprt_data->status == DISSOCIATED_FROM_XPRT);

	PTHREAD_RWLOCK_destroy(
		&xprt_data->nfs41_sessions_holder.sessions_lock);
	xprt_data->status = DESTROYED;
	gsh_free(xprt_data);
	xprt->xp_u1 = NULL;
}
