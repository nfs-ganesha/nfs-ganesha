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
 * @addtogroup idmapper
 * @{
 */

/**
 * @file    idmapper_negative_cache.c
 * @brief   Negative cache for entities that failed idmapping
 */

#include <pwd.h>
#include "common_utils.h"
#include "avltree.h"
#include "idmapper.h"
#include "nfs_core.h"
#include <misc/queue.h>

/* Struct representing a user entry in the negative cache */
struct negative_cache_user {
	struct gsh_buffdesc uname;	/*< Username */
	struct avltree_node uname_node;	/*< Node in the uname tree */
	time_t epoch; /*< User object creation timestamp */

	TAILQ_ENTRY(negative_cache_user) queue_entry; /*< Node in user queue */
};

/* Lock that protects the idmapper negative user cache */
pthread_rwlock_t idmapper_negative_cache_user_lock;

/** @brief The fifo queue for storing negative users
 *
 * A fifo queue mimics the order of expiration time of the cache entries,
 * since the expiration time is a linear function of the insertion time.
 *
 *   Expiration_time = Insertion_time + Cache_expiration_time (constant)
 *
 * The head of the queue contains the entry with least time-validity.
 * The tail of the queue contains the entry with most time-validity.
 * The eviction happens from the head, and insertion happens into the tail.
 */
static TAILQ_HEAD(, negative_cache_user) negative_user_fifo_queue;

/* AVL-Tree cache for storing negative cache user uname node */
static struct avltree uname_tree;


/**
 * @brief Checks if a negative user entry is expired
 *
 * @return true if expired, false otherwise
 */
static bool is_negative_cache_user_expired(struct negative_cache_user *user)
{
	return (time(NULL) - user->epoch) >
		nfs_param.directory_services_param.negative_cache_time_validity;
}

/**
 * @brief Remove negative user entry from all user cache data structures
 *
 * @note The caller must hold idmapper_negative_cache_user_lock for write.
 */
static void remove_negative_cache_user(struct negative_cache_user *user)
{
	avltree_remove(&user->uname_node, &uname_tree);
	TAILQ_REMOVE(&negative_user_fifo_queue, user, queue_entry);
	gsh_free(user);
}

/**
 * @brief Comparison function for negative-user-cache nodes
 *
 * @return -1 if @arg node1 is less than @arg node2
 * @return 0 if @arg node1 and @arg node2 are equal
 * @return 1 if @arg node1 is greater than @arg node2
 */
static int uname_comparator(const struct avltree_node *node1,
	const struct avltree_node *node2)
{
	struct negative_cache_user *user1 = avltree_container_of(node1,
		struct negative_cache_user, uname_node);
	struct negative_cache_user *user2 = avltree_container_of(node2,
		struct negative_cache_user, uname_node);

	return gsh_buffdesc_comparator(&user1->uname, &user2->uname);
}

/**
 * @brief Initialise the idmapper negative cache
 */
void idmapper_negative_cache_init(void)
{
	PTHREAD_RWLOCK_init(&idmapper_negative_cache_user_lock, NULL);
	avltree_init(&uname_tree, uname_comparator, 0);
	TAILQ_INIT(&negative_user_fifo_queue);
}

/**
 * @brief Add a user entry to the negative cache by name
 *
 * @note The caller must hold idmapper_negative_cache_user_lock for write
 *
 * @param[in] name The user's name for insertion
 */
void idmapper_negative_cache_add_user_by_name(const struct gsh_buffdesc *name)
{
	struct avltree_node *old_node;
	struct negative_cache_user *old_user;
	struct negative_cache_user *new_user;
	struct negative_cache_user *negative_user_fifo_queue_head;

	new_user = gsh_malloc(sizeof(struct negative_cache_user) + name->len);
	new_user->uname.addr = (char *)new_user +
		sizeof(struct negative_cache_user);
	new_user->uname.len = name->len;
	memcpy(new_user->uname.addr, name->addr, name->len);
	new_user->epoch = time(NULL);

	old_node = avltree_insert(&new_user->uname_node, &uname_tree);

	/* Unlikely that the node already exists. If it does, we update it */
	if (unlikely(old_node)) {
		old_user = avltree_container_of(old_node,
			struct negative_cache_user, uname_node);
		old_user->epoch = time(NULL);
		/* Move user to the tail of the queue */
		TAILQ_REMOVE(&negative_user_fifo_queue, old_user, queue_entry);
		TAILQ_INSERT_TAIL(&negative_user_fifo_queue, old_user,
			queue_entry);

		gsh_free(new_user);
		return;
	}
	TAILQ_INSERT_TAIL(&negative_user_fifo_queue, new_user, queue_entry);

	/* If we breach max-cache capacity, remove the user queue's head node */
	if (avltree_size(&uname_tree) > nfs_param.directory_services_param
		.negative_cache_users_max_count) {
		LogInfo(COMPONENT_IDMAPPER,
			"Cache size limit violated, removing user with least time validity");
		negative_user_fifo_queue_head =
			TAILQ_FIRST(&negative_user_fifo_queue);
		remove_negative_cache_user(negative_user_fifo_queue_head);
	}
}

/**
 * @brief Look up a user by name in negative cache
 *
 * @note The caller must hold idmapper_negative_cache_user_lock for read
 *
 * @param[in] name The user name to look up.
 *
 * @return true on success, false otherwise.
 */
bool idmapper_negative_cache_lookup_user_by_name(
	const struct gsh_buffdesc *name)
{
	struct negative_cache_user *cache_user;
	struct negative_cache_user prototype = {
		.uname = *name
	};
	struct avltree_node *cache_node = avltree_lookup(&prototype.uname_node,
		&uname_tree);

	if (!cache_node)
		return false;

	cache_user = avltree_container_of(cache_node,
		struct negative_cache_user, uname_node);

	return is_negative_cache_user_expired(cache_user) ? false : true;
}

/**
 * @brief Clear the idmapper negative cache
 */
void idmapper_negative_cache_clear(void)
{
	struct avltree_node *node;

	PTHREAD_RWLOCK_wrlock(&idmapper_negative_cache_user_lock);

	for (node = avltree_first(&uname_tree);
	     node != NULL;
	     node = avltree_first(&uname_tree)) {
		struct negative_cache_user *user;

		user = avltree_container_of(node, struct negative_cache_user,
			uname_node);
		remove_negative_cache_user(user);
	}
	assert(avltree_first(&uname_tree) == NULL);

	PTHREAD_RWLOCK_unlock(&idmapper_negative_cache_user_lock);
}

/**
 * @brief Clean up the idmapper negative cache
 */
void idmapper_negative_cache_destroy(void)
{
	idmapper_negative_cache_clear();
	PTHREAD_RWLOCK_destroy(&idmapper_negative_cache_user_lock);
}

/** @} */
