// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @addtogroup idmapper
 * @{
 */

/**
 * @file    uid_grplist_cache.c
 * @brief   Uid->Group List mapping cache functions
 */
#include "config.h"
#include "log.h"
#include "config_parsing.h"
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include "gsh_intrinsic.h"
#include "gsh_types.h"
#include "common_utils.h"
#include "avltree.h"
#include "uid2grp.h"
#include "abstract_atomic.h"
#include "nfs_core.h"
#include <misc/queue.h>
#include "idmapper_monitoring.h"

/**
 * @brief User entry in the uid2grp cache
 */

struct cache_info {
	uid_t uid;		/*< Corresponding UID */
	struct gsh_buffdesc uname;
	struct group_data *gdata;
	struct avltree_node uname_node;	/*< Node in the name tree */
	struct avltree_node uid_node;	/*< Node in the UID tree */
	TAILQ_ENTRY(cache_info) queue_entry; /* Node in groups-fifo-queue */
};

/**
 * @brief Number of entries in the UID cache, should be prime.
 */

#define id_cache_size 1009

/**
 * @brief A user-groups fifo queue ordered by insertion timestamp
 *
 * This fifo queue also mimics the order of expiration time of the cache
 * entries, since the expiration time is a linear function of the insertion
 * time.
 *
 *   Expiration_time = Insertion_time + Cache_time_validity (constant)
 *
 * The head of the queue contains the entry with least time-validity.
 * The tail of the queue contains the entry with most time-validity.
 * The eviction happens from the head, and insertion happens at the tail.
 */
static TAILQ_HEAD(, cache_info) groups_fifo_queue;

/**
 * @brief UID cache, may only be accessed with uid2grp_user_lock
 * held.  If uid2grp_user_lock is held for read, it must be accessed
 * atomically.  (For a write, normal fetch/store is sufficient since
 * others are kept out.)
 */

static struct avltree_node *uid_grplist_cache[id_cache_size];

/**
 * @brief Lock that protects the idmapper user cache
 */

pthread_rwlock_t uid2grp_user_lock;

/**
 * @brief Tree of users, by name
 */

static struct avltree uname_tree;

/**
 * @brief Tree of users, by ID
 */

static struct avltree uid_tree;


/**
 * @brief Comparison for user names
 *
 * @param[in] node1 A node
 * @param[in] nodea Another node
 *
 * @retval -1 if node1 is less than nodea
 * @retval 0 if node1 and nodea are equal
 * @retval 1 if node1 is greater than nodea
 */

static int uname_comparator(const struct avltree_node *node1,
			    const struct avltree_node *nodea)
{
	struct cache_info *user1 =
	    avltree_container_of(node1, struct cache_info,
				 uname_node);
	struct cache_info *usera =
	    avltree_container_of(nodea, struct cache_info,
				 uname_node);

	return gsh_buffdesc_comparator(&user1->uname, &usera->uname);
}

/**
 * @brief Comparison for UIDs
 *
 * @param[in] node1 A node
 * @param[in] nodea Another node
 *
 * @retval -1 if node1 is less than nodea
 * @retval 0 if node1 and nodea are equal
 * @retval 1 if node1 is greater than nodea
 */

static int uid_comparator(const struct avltree_node *node1,
			  const struct avltree_node *nodea)
{
	struct cache_info *user1 =
	    avltree_container_of(node1, struct cache_info,
				 uid_node);
	struct cache_info *usera =
	    avltree_container_of(nodea, struct cache_info,
				 uid_node);

	if (user1->uid < usera->uid)
		return -1;
	else if (user1->uid > usera->uid)
		return 1;
	else
		return 0;
}

/* Cleanup on shutdown */
void uid2grp_cache_cleanup(void)
{
	uid2grp_clear_cache();
	PTHREAD_RWLOCK_destroy(&uid2grp_user_lock);
}

struct cleanup_list_element uid2grp_cache_cleanup_element = {
	.clean = uid2grp_cache_cleanup,
};

/* Remove given user/cache_info from the AVL trees
 *
 * @note The caller must hold uid2grp_user_lock for write.
 */
static void uid2grp_remove_user(struct cache_info *info)
{
	uid_grplist_cache[info->uid % id_cache_size] = NULL;
	avltree_remove(&info->uid_node, &uid_tree);
	avltree_remove(&info->uname_node, &uname_tree);
	TAILQ_REMOVE(&groups_fifo_queue, info, queue_entry);
	/* We decrement hold on group data when it is
	 * removed from cache trees.
	 */
	uid2grp_release_group_data(info->gdata);
	gsh_free(info);
}

/**
 * @brief Reaps the uid2grp cache entries
 *
 * Since the fifo queue stores entries in increasing order of time validity,
 * the reaper reaps from the queue head in the same order. It stops when it
 * first encounters a non-expired entry.
 */
void uid2grp_cache_reap(void)
{
	struct cache_info *groups;

	LogFullDebug(COMPONENT_IDMAPPER, "uid2grp cache reaper run started");
	PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);

	for (groups = TAILQ_FIRST(&groups_fifo_queue); groups != NULL;) {
		if (!uid2grp_is_group_data_expired(groups->gdata))
			break;
		uid2grp_remove_user(groups);
		groups = TAILQ_FIRST(&groups_fifo_queue);
	}
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
	LogFullDebug(COMPONENT_IDMAPPER, "uid2grp cache reaper run ended");
}

/**
 * @brief Initialize the user-groups cache
 */
void uid2grp_cache_init(void)
{
	PTHREAD_RWLOCK_init(&uid2grp_user_lock, NULL);
	if (nfs_param.core_param.max_uid_to_grp_reqs)
		sem_init(&uid2grp_sem, 0,
				nfs_param.core_param.max_uid_to_grp_reqs);
	avltree_init(&uname_tree, uname_comparator, 0);
	avltree_init(&uid_tree, uid_comparator, 0);
	memset(uid_grplist_cache, 0,
		id_cache_size * sizeof(struct avltree_node *));
	TAILQ_INIT(&groups_fifo_queue);
	RegisterCleanup(&uid2grp_cache_cleanup_element);
}

/**
 * @brief Add a user entry to the cache
 *
 * @note The caller must hold uid2grp_user_lock for write.
 *
 * @param[in] group_data that has supplementary groups allocated
 */
void uid2grp_add_user(struct group_data *gdata)
{
	struct avltree_node *name_node;
	struct avltree_node *id_node;
	struct avltree_node *name_node2 = NULL;
	struct avltree_node *id_node2 = NULL;
	struct cache_info *info;
	struct cache_info *tmp;
	struct cache_info *groups_fifo_queue_head_node;
	directory_services_param_t *ds_param =
		&nfs_param.directory_services_param;

	info = gsh_malloc(sizeof(struct cache_info));

	info->uid = gdata->uid;
	info->uname.addr = gdata->uname.addr;
	info->uname.len = gdata->uname.len;
	info->gdata = gdata;

	/* The refcount on group_data should be 1 when we put it in
	 * AVL trees.
	 */
	uid2grp_hold_group_data(gdata);

	/* We may have lost the race to insert. We remove existing
	 * entry and insert this new entry if so!
	 */
	name_node = avltree_insert(&info->uname_node, &uname_tree);
	if (unlikely(name_node)) {
		tmp = avltree_container_of(name_node,
					   struct cache_info,
					   uname_node);
		uid2grp_remove_user(tmp);
		name_node2 = avltree_insert(&info->uname_node, &uname_tree);
		assert(name_node2 == NULL);
	}

	id_node = avltree_insert(&info->uid_node, &uid_tree);
	if (unlikely(id_node)) {
		/* We should not come here unless someone changed uid of
		 * a user. Remove old entry and re-insert the new
		 * entry.
		 */
		tmp = avltree_container_of(id_node,
					   struct cache_info,
					   uid_node);
		uid2grp_remove_user(tmp);
		id_node2 = avltree_insert(&info->uid_node, &uid_tree);
		assert(id_node2 == NULL);
	}
	uid_grplist_cache[info->uid % id_cache_size] = &info->uid_node;
	TAILQ_INSERT_TAIL(&groups_fifo_queue, info, queue_entry);

	/* If we breach max-cache capacity, remove the queue's head node */
	if (avltree_size(&uname_tree) > ds_param->cache_user_groups_max_count) {
		LogInfo(COMPONENT_IDMAPPER,
			"Cache size limit violated, removing entry with least time validity");
		groups_fifo_queue_head_node = TAILQ_FIRST(&groups_fifo_queue);
		const time_t cached_duration = time(NULL) -
			groups_fifo_queue_head_node->gdata->epoch;
		uid2grp_remove_user(groups_fifo_queue_head_node);
		idmapper_monitoring__evicted_cache_entity(
			IDMAPPING_CACHE_ENTITY_USER_GROUPS, cached_duration);
	}

	if (name_node && id_node)
		LogWarn(COMPONENT_IDMAPPER, "shouldn't happen, internal error");
	if ((name_node && name_node2) || (id_node && id_node2))
		LogWarn(COMPONENT_IDMAPPER, "shouldn't happen, internal error");
}


static bool lookup_by_uname(const struct gsh_buffdesc *name,
			    struct cache_info **info)
{
	struct cache_info prototype = {
		.uname = *name
	};
	struct avltree_node *found_node = avltree_lookup(&prototype.uname_node,
							 &uname_tree);
	struct cache_info *found_info;
	void **cache_slot;

	if (unlikely(!found_node))
		return false;

	found_info = avltree_container_of(found_node,
					  struct cache_info,
					  uname_node);

	/* I assume that if someone likes this user enough to look it
	   up by name, they'll like it enough to look it up by ID
	   later. */

	cache_slot = (void **)
		&uid_grplist_cache[found_info->uid % id_cache_size];
	atomic_store_voidptr(cache_slot, &found_info->uid_node);

	*info = found_info;

	return true;
}

static bool lookup_by_uid(const uid_t uid, struct cache_info **info)
{
	struct cache_info prototype = {
		.uid = uid
	};
	void **cache_slot = (void **)
	    &uid_grplist_cache[prototype.uid % id_cache_size];
	struct avltree_node *found_node = atomic_fetch_voidptr(cache_slot);
	struct cache_info *found_info;
	bool found = false;

	/* Verify that the node found in the cache array is in fact what we
	 * want.
	 */
	if (likely(found_node)) {
		found_info =
		    avltree_container_of(found_node, struct cache_info,
					 uid_node);
		if (found_info->uid == uid)
			found = true;
	}

	if (unlikely(!found)) {
		found_node = avltree_lookup(&prototype.uid_node, &uid_tree);
		if (unlikely(!found_node))
			return false;

		atomic_store_voidptr(cache_slot, found_node);
		found_info =
		    avltree_container_of(found_node, struct cache_info,
					 uid_node);
	}

	*info = found_info;

	return true;
}

/**
 * @brief Look up a user by name (can return expired cache entry)
 *
 * @note The caller must hold uid2grp_user_lock for read.
 *
 * @param[in]  name The user name to look up.
 * @param[out] uid  The user ID found.  May be NULL if the caller
 *                  isn't interested in the UID.  (This seems
 *                  unlikely.)
 * @param[out] gdata group_data containing supplementary groups.
 *
 * @retval true on success.
 * @retval false if we need to try, try again.
 */

bool uid2grp_lookup_by_uname(const struct gsh_buffdesc *name, uid_t *uid,
			     struct group_data **gdata)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uname(name, &info);

	if (success) {
		*gdata = info->gdata;
		*uid = info->gdata->uid;
	}

	return success;
}

/**
 * @brief Look up a user by ID (can return expired cache entry)
 *
 * @note The caller must hold uid2grp_user_lock for read.
 *
 * @param[in]  uid  The user ID to look up.
 * @gdata[out] group_data containing supplementary groups.
 *
 * @retval true on success.
 * @retval false if we weren't so successful.
 */

bool uid2grp_lookup_by_uid(const uid_t uid, struct group_data **gdata)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uid(uid, &info);
	if (success)
		*gdata = info->gdata;

	return success;
}

bool uid2grp_is_group_data_expired(struct group_data *gdata)
{
	time_t gdata_age = time(NULL) - gdata->epoch;

	return gdata_age > nfs_param.core_param.manage_gids_expiration;
}

/**
 * @brief Remove a user by ID
 *
 * @note The caller must hold uid2grp_user_lock for write.
 *
 * @param[in]  uid  The user ID to remove.
 */

void uid2grp_remove_by_uid(const uid_t uid)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uid(uid, &info);
	if (success)
		uid2grp_remove_user(info);
}

/**
 * @brief Remove an expired user by ID
 *
 * @note The caller must hold uid2grp_user_lock for write.
 *
 * @param[in]  uid  The user ID to remove.
 */

void uid2grp_remove_expired_by_uid(const uid_t uid)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uid(uid, &info);
	if (success && uid2grp_is_group_data_expired(info->gdata))
		uid2grp_remove_user(info);
}

/**
 * @brief Remove a user by name
 *
 * @note The caller must hold uid2grp_user_lock for write.
 *
 * @param[in]  name  The user name to remove.
 */

void uid2grp_remove_by_uname(const struct gsh_buffdesc *name)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uname(name, &info);
	if (success)
		uid2grp_remove_user(info);
}

/**
 * @brief Remove an expired user by name
 *
 * @note The caller must hold uid2grp_user_lock for write.
 *
 * @param[in]  name  The user name to remove.
 */

void uid2grp_remove_expired_by_uname(const struct gsh_buffdesc *name)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uname(name, &info);
	if (success && uid2grp_is_group_data_expired(info->gdata))
		uid2grp_remove_user(info);
}

/**
 * @brief Wipe out the uid2grp cache
 */

void uid2grp_clear_cache(void)
{
	struct avltree_node *node;

	PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);

	while ((node = avltree_first(&uname_tree))) {
		struct cache_info *info = avltree_container_of(node,
							       struct
							       cache_info,
							       uname_node);
		uid2grp_remove_user(info);
	}

	assert(avltree_first(&uid_tree) == NULL);

	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
}

/** @} */
