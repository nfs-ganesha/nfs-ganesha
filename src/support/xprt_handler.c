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
	LogDebug(COMPONENT_XPRT,
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
 * @brief Removes xprt references, both of the xprt from the custom-data
 * components, and of the custom-data components from the xprt.
 *
 * This function should be called when destroying a xprt, in order to release
 * the above mentioned references.
 */
void dissociate_custom_data_from_xprt(SVCXPRT *xprt)
{
	struct glist_head *curr_node, *next_node;
	struct glist_head duplicate_sessions;
	char xprt_addr_str[SOCK_NAME_MAX] = "\0";
	struct display_buffer db = {
		sizeof(xprt_addr_str), xprt_addr_str, xprt_addr_str};
	xprt_custom_data_t *xprt_data;
	nfs41_sessions_holder_t *sessions_holder;

	display_xprt_sockaddr(&db, xprt);

	if (xprt->xp_u1 == NULL) {
		LogInfo(COMPONENT_XPRT,
			"The xprt FD: %d, socket-addr: %s is not associated with any custom-data, done un-referencing.",
			xprt->xp_fd, xprt_addr_str);
		return;
	}
	LogDebug(COMPONENT_XPRT,
		"About to un-reference custom-data from xprt with FD: %d, socket-addr: %s",
		xprt->xp_fd, xprt_addr_str);

	xprt_data = (xprt_custom_data_t *) xprt->xp_u1;
	assert(xprt_data->status == ASSOCIATED_TO_XPRT);

	sessions_holder = &xprt_data->nfs41_sessions_holder;

	/* Copy the xprt sessions to a new list to avoid deadlock that can
	 * happen if we take xprt's session-list lock followed by session's
	 * connections lock (this lock order is reverse of the order used
	 * during xprt connection association and dis-association with a
	 * session)
	 *
	 * With the below change, we do not acquire the nested session's
	 * connections lock, while holding the xprt's session-list lock. We
	 * first release the xprt's session-list lock after copying the xprt's
	 * sessions to a duplicate session-list. We then acquire the session's
	 * connection lock to process each session in the duplicate list. That
	 * is, both the mentioned operations are not done atomically.
	 *
	 * This can result in possible situations where the xprt's session-list
	 * has been cleared after the first operation, but those cleared
	 * sessions still have the xprt's reference, until the second operation.
	 * During this interval between the two operation, it is possible that
	 * another thread (in a different operation) sees a missing session on
	 * this xprt's session-list and tries to add it to that xprt, even
	 * though that session already had a reference to this xprt. If this
	 * situation happens, such a session added to this xprt's session-list
	 * will be at risk of never getting un-referenced. Also, such a xprt's
	 * reference would get re-added to the session, and then the xprt would
	 * be at risk of never getting destroyed. However, since this other
	 * thread additionally MUST also check if the xprt under consideration
	 * is being destroyed (that is, xprt's custom-data is dissociated from
	 * xprt) before adding the session to it, we are able to avoid this
	 * situation.
	 *
	 * The same situation can also happen after the xprt is un-referenced
	 * through this function, but another in-flight request is still
	 * operating on this same xprt (under destruction). The above mentioned
	 * check will also prevent this from happening.
	 */

	glist_init(&duplicate_sessions);
	PTHREAD_RWLOCK_wrlock(&sessions_holder->sessions_lock);
	/* Move all xprt-data sessions to the duplicate-sessions list */
	glist_splice_tail(&duplicate_sessions, &sessions_holder->sessions);
	xprt_data->status = DISSOCIATED_FROM_XPRT;
	PTHREAD_RWLOCK_unlock(&sessions_holder->sessions_lock);

	/* Now process the duplicate list: for each session referenced by the
	 * xprt, destroy the backchannel and release the connection_xprt held
	 * by the session.
	 */
	glist_for_each_safe(curr_node, next_node, &duplicate_sessions) {
		nfs41_session_list_entry_t *curr_entry = glist_entry(curr_node,
			nfs41_session_list_entry_t, node);

		nfs41_Session_Destroy_Backchannel_For_Xprt(curr_entry->session,
			xprt);
		nfs41_Session_Remove_Connection(curr_entry->session, xprt);

		/* Release session reference */
		dec_session_ref(curr_entry->session);

		/* Free the session-node allocated for the xprt */
		glist_del(curr_node);
		gsh_free(curr_entry);
	}
	LogDebug(COMPONENT_XPRT,
		"Done un-referencing of xprt with FD: %d, socket-addr: %s",
		xprt->xp_fd, xprt_addr_str);
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

	if (xprt->xp_u1 == NULL) {
		LogDebug(COMPONENT_XPRT,
		    "No custom data to destroy for the destroyed xprt");
		return;
	}

	display_xprt_sockaddr(&db, xprt);
	LogDebug(COMPONENT_XPRT,
		"Processing custom data for destroyed xprt: %p with FD: %d, socket-addr: %s",
		xprt, xprt->xp_fd, sockaddr_str);
	assert(xprt->xp_flags & SVC_XPRT_FLAG_DESTROYED);

	xprt_data = (xprt_custom_data_t *) xprt->xp_u1;
	assert(glist_empty(&xprt_data->nfs41_sessions_holder.sessions));
	assert(xprt_data->status == DISSOCIATED_FROM_XPRT);

	PTHREAD_RWLOCK_destroy(
		&xprt_data->nfs41_sessions_holder.sessions_lock);
	xprt_data->status = DESTROYED;
	gsh_free(xprt_data);
	xprt->xp_u1 = NULL;
}
