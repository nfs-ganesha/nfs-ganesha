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
#include <grp.h>
#include "common_utils.h"
#include "avltree.h"
#include "idmapper.h"
#include "nfs_core.h"
#include <misc/queue.h>
#include "idmapper_monitoring.h"

/* Struct representing the negative cache entity */
typedef enum negative_cache_entity_type {
	USER,
	GROUP
} negative_cache_entity_type_t;

/* Struct representing a user or group entry in the negative cache */
typedef struct negative_cache_entity {
	struct gsh_buffdesc name;	/*< Entity name */
	struct avltree_node name_node;	/*< Entity tree node */
	time_t epoch; /*< Entity creation timestamp */

	TAILQ_ENTRY(negative_cache_entity) queue_entry; /*< Entity queue node */
	char name_buffer[];	/*< Entity name buffer */
} negative_cache_entity_t;

/* Lock that protects the idmapper negative user cache */
pthread_rwlock_t idmapper_negative_cache_user_lock;

/* Lock that protects the idmapper negative group cache */
pthread_rwlock_t idmapper_negative_cache_group_lock;

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
static TAILQ_HEAD(idmapping_negative_cache_queue, negative_cache_entity)
	negative_user_fifo_queue;

/* The fifo queue (similar to above) for storing negative groups */
struct idmapping_negative_cache_queue negative_group_fifo_queue;

/* AVL-Tree cache for storing negative cache user name node */
static struct avltree uname_tree;

/* AVL-Tree cache for storing negative cache group name node */
static struct avltree gname_tree;


/**
 * @brief Checks if a negative user/group entry is expired
 *
 * @return true if expired, false otherwise
 */
static bool is_negative_cache_entity_expired(negative_cache_entity_t *entity)
{
	return (time(NULL) - entity->epoch) >
		nfs_param.directory_services_param.negative_cache_time_validity;
}

/**
 * @brief Remove negative entity entry from all its cache data structures
 *
 * @note The caller must hold the corresponding entity lock for write.
 */
static void remove_negative_cache_entity(negative_cache_entity_t *entity,
	negative_cache_entity_type_t entity_type)
{
	struct avltree *cache_tree;
	struct idmapping_negative_cache_queue *cache_queue;

	switch (entity_type) {
	case USER:
		cache_tree = &uname_tree;
		cache_queue = &negative_user_fifo_queue;
		break;
	case GROUP:
		cache_tree = &gname_tree;
		cache_queue = &negative_group_fifo_queue;
		break;
	default:
		LogFatal(COMPONENT_IDMAPPER,
			"Unknown negative cache entity type: %d", entity_type);
	}
	avltree_remove(&entity->name_node, cache_tree);
	TAILQ_REMOVE(cache_queue, entity, queue_entry);
	gsh_free(entity);
}

/**
 * @brief Reaps the negative cache entities
 *
 * Since the entity fifo queue stores entries in increasing order of time
 * validity, the reaper reaps from the queue head in the same order. It stops
 * when it first encounters a non-expired entry.
 */
static void reap_negative_cache_entities(
	negative_cache_entity_type_t entity_type)
{
	struct negative_cache_entity *entity;
	struct idmapping_negative_cache_queue *cache_queue;
	pthread_rwlock_t *entity_lock;

	switch (entity_type) {
	case USER:
		cache_queue = &negative_user_fifo_queue;
		entity_lock = &idmapper_negative_cache_user_lock;
		break;
	case GROUP:
		cache_queue = &negative_group_fifo_queue;
		entity_lock = &idmapper_negative_cache_group_lock;
		break;
	default:
		LogFatal(COMPONENT_IDMAPPER,
			"Unknown negative cache entity type: %d", entity_type);
	}

	PTHREAD_RWLOCK_wrlock(entity_lock);

	for (entity = TAILQ_FIRST(cache_queue); entity != NULL;) {
		if (!is_negative_cache_entity_expired(entity))
			break;
		remove_negative_cache_entity(entity, entity_type);
		entity = TAILQ_FIRST(cache_queue);
	}
	PTHREAD_RWLOCK_unlock(entity_lock);
}

/**
 * @brief Reaps the negative cache user and group entries
 */
void idmapper_negative_cache_reap(void)
{
	LogFullDebug(COMPONENT_IDMAPPER,
		"Idmapper negative-cache reaper run started");
	reap_negative_cache_entities(USER);
	reap_negative_cache_entities(GROUP);
	LogFullDebug(COMPONENT_IDMAPPER,
		"Idmapper negative-cache reaper run ended");
}

/*
 * @brief Comparison function for negative-cache-entity nodes
 *
 * @return -1 if @arg node1 is less than @arg node2
 * @return 0 if @arg node1 and @arg node2 are equal
 * @return 1 if @arg node1 is greater than @arg node2
 */
static int node_comparator(const struct avltree_node *node1,
	const struct avltree_node *node2)
{
	negative_cache_entity_t *entity1 =
		avltree_container_of(node1, negative_cache_entity_t, name_node);
	negative_cache_entity_t *entity2 =
		avltree_container_of(node2, negative_cache_entity_t, name_node);

	return gsh_buffdesc_comparator(&entity1->name, &entity2->name);
}

/**
 * @brief Initialise the idmapper negative cache
 */
void idmapper_negative_cache_init(void)
{
	PTHREAD_RWLOCK_init(&idmapper_negative_cache_user_lock, NULL);
	PTHREAD_RWLOCK_init(&idmapper_negative_cache_group_lock, NULL);
	avltree_init(&uname_tree, node_comparator, 0);
	avltree_init(&gname_tree, node_comparator, 0);
	TAILQ_INIT(&negative_user_fifo_queue);
	TAILQ_INIT(&negative_group_fifo_queue);
}

/**
 * @brief Add an entity to the negative cache by name
 *
 * @note The caller must hold the corresponding entity lock for write
 *
 * @param[in] name The entity's name for insertion
 * @param[in] entity_type The entity's type for insertion
 */
static void idmapper_negative_cache_add_entity_by_name(
	const struct gsh_buffdesc *name,
	negative_cache_entity_type_t entity_type)
{
	struct avltree *cache_tree;
	struct avltree_node *old_node;
	struct idmapping_negative_cache_queue *cache_queue;
	negative_cache_entity_t *old_entity, *new_entity, *cache_queue_head;
	uint32_t max_cache_entities;
	char *entity_type_string;
	idmapping_cache_entity_t cache_entity;

	new_entity = gsh_malloc(sizeof(negative_cache_entity_t) + name->len);
	new_entity->name.addr = new_entity->name_buffer;
	new_entity->name.len = name->len;
	memcpy(new_entity->name.addr, name->addr, name->len);
	new_entity->epoch = time(NULL);

	switch (entity_type) {
	case USER:
		cache_tree = &uname_tree;
		cache_queue = &negative_user_fifo_queue;
		max_cache_entities = nfs_param.directory_services_param
			.negative_cache_users_max_count;
		entity_type_string = (char *)"user";
		cache_entity = IDMAPPING_CACHE_ENTITY_NEGATIVE_USER;
		break;
	case GROUP:
		cache_tree = &gname_tree;
		cache_queue = &negative_group_fifo_queue;
		max_cache_entities = nfs_param.directory_services_param
			.negative_cache_groups_max_count;
		entity_type_string = (char *)"group";
		cache_entity = IDMAPPING_CACHE_ENTITY_NEGATIVE_GROUP;
		break;
	default:
		LogFatal(COMPONENT_IDMAPPER,
			"Unknown negative cache entity type: %d", entity_type);
	}

	old_node = avltree_insert(&new_entity->name_node, cache_tree);

	/* Unlikely that the node already exists. If it does, we update it */
	if (unlikely(old_node)) {
		old_entity = avltree_container_of(old_node,
			negative_cache_entity_t, name_node);
		old_entity->epoch = time(NULL);
		/* Move entity to the tail of the queue */
		TAILQ_REMOVE(cache_queue, old_entity, queue_entry);
		TAILQ_INSERT_TAIL(cache_queue, old_entity, queue_entry);

		gsh_free(new_entity);
		return;
	}
	TAILQ_INSERT_TAIL(cache_queue, new_entity, queue_entry);

	/* If we breach max-cache capacity, remove entity queue's head node */
	if (avltree_size(cache_tree) > max_cache_entities) {
		LogInfo(COMPONENT_IDMAPPER,
			"Cache size limit violated, removing %s with least time validity",
			entity_type_string);
		cache_queue_head = TAILQ_FIRST(cache_queue);
		const time_t cached_duration =
			time(NULL) - cache_queue_head->epoch;
		remove_negative_cache_entity(cache_queue_head, entity_type);
		idmapper_monitoring__evicted_cache_entity(cache_entity,
			cached_duration);
	}
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
	idmapper_negative_cache_add_entity_by_name(name, USER);
}

/**
 * @brief Add a group entry to the negative cache by name
 *
 * @note The caller must hold idmapper_negative_cache_group_lock for write
 *
 * @param[in] name The group name
 */
void idmapper_negative_cache_add_group_by_name(const struct gsh_buffdesc *name)
{
	idmapper_negative_cache_add_entity_by_name(name, GROUP);
}

/**
 * @brief Look up an entity by name in negative cache
 *
 * @note The caller must hold the corresponding entity lock for read
 *
 * @param[in] name The entity name to look up.
 * @param[in] entity_type The entity type to look up.
 *
 * @return true on success, false otherwise.
 */
static bool idmapper_negative_cache_lookup_entity_by_name(
	const struct gsh_buffdesc *name,
	negative_cache_entity_type_t entity_type)
{
	struct avltree *cache_tree;
	struct avltree_node *cache_node;
	negative_cache_entity_t *cache_entity;
	negative_cache_entity_t prototype = {
		.name = *name
	};

	switch (entity_type) {
	case USER:
		cache_tree = &uname_tree;
		break;
	case GROUP:
		cache_tree = &gname_tree;
		break;
	default:
		LogFatal(COMPONENT_IDMAPPER,
			"Unknown negative cache entity type: %d", entity_type);
	}
	cache_node = avltree_lookup(&prototype.name_node, cache_tree);
	if (!cache_node)
		return false;

	cache_entity = avltree_container_of(cache_node, negative_cache_entity_t,
		name_node);
	return is_negative_cache_entity_expired(cache_entity) ? false : true;
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
	return idmapper_negative_cache_lookup_entity_by_name(name, USER);
}

/**
 * @brief Look up a group by name in negative cache
 *
 * @note The caller must hold idmapper_negative_cache_group_lock for read
 *
 * @param[in] name The group name to look up.
 *
 * @return true on success, false otherwise.
 */
bool idmapper_negative_cache_lookup_group_by_name(
	const struct gsh_buffdesc *name)
{
	return idmapper_negative_cache_lookup_entity_by_name(name, GROUP);
}

/**
 * @brief Remove all negative cache entities of input @arg entity_type
 */
static void remove_all_negative_cache_entities(
	negative_cache_entity_type_t entity_type)
{
	struct avltree *cache_tree;
	pthread_rwlock_t *entity_lock;
	struct avltree_node *node;
	negative_cache_entity_t *entity;

	switch (entity_type) {
	case USER:
		cache_tree = &uname_tree;
		entity_lock = &idmapper_negative_cache_user_lock;
		break;
	case GROUP:
		cache_tree = &gname_tree;
		entity_lock = &idmapper_negative_cache_group_lock;
		break;
	default:
		LogFatal(COMPONENT_IDMAPPER,
			"Unknown negative cache entity type: %d", entity_type);
	}

	PTHREAD_RWLOCK_wrlock(entity_lock);
	for (node = avltree_first(cache_tree);
	     node != NULL;
	     node = avltree_first(cache_tree)) {
		entity = avltree_container_of(node, negative_cache_entity_t,
			name_node);
		remove_negative_cache_entity(entity, entity_type);
	}
	assert(avltree_first(cache_tree) == NULL);
	PTHREAD_RWLOCK_unlock(entity_lock);
}

/**
 * @brief Clear the idmapper negative cache
 */
void idmapper_negative_cache_clear(void)
{
	remove_all_negative_cache_entities(USER);
	remove_all_negative_cache_entities(GROUP);
}

/**
 * @brief Clean up the idmapper negative cache
 */
void idmapper_negative_cache_destroy(void)
{
	idmapper_negative_cache_clear();
	PTHREAD_RWLOCK_destroy(&idmapper_negative_cache_user_lock);
	PTHREAD_RWLOCK_destroy(&idmapper_negative_cache_group_lock);
}

/** @} */
