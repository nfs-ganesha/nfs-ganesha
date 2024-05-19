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
 * @file    idmapper_cache.c
 * @brief   Id mapping cache functions
 */
#include "config.h"
#include "log.h"
#include "config_parsing.h"
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include "gsh_intrinsic.h"
#include "gsh_types.h"
#include "gsh_list.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "common_utils.h"
#include "avltree.h"
#include "idmapper.h"
#include "nfs_core.h"
#include "abstract_atomic.h"
#include "server_stats_private.h"
#include "idmapper_monitoring.h"

/**
 * @brief User entry in the IDMapper cache
 */

struct cache_user {
	struct gsh_buffdesc uname;	/*< Username */
	uid_t uid;		/*< Corresponding UID */
	gid_t gid;		/*< Corresponding GID */
	bool gid_set;		/*< if the GID has been set */
	struct avltree_node uname_node;	/*< Node in the name tree */
	struct avltree_node uid_node;	/*< Node in the UID tree */
	bool in_uidtree;		/* true iff this is in uid_tree */
	time_t epoch;
	TAILQ_ENTRY(cache_user) queue_entry; /* Node in user-fifo-queue */
};

#define user_expired(user) (time(NULL) - (user)->epoch > \
		nfs_param.directory_services_param.idmapped_user_time_validity)

/**
 * @brief Group entry in the IDMapper cache
 */

struct cache_group {
	struct gsh_buffdesc gname;	/*< Group name */
	gid_t gid;		/*< Group ID */
	struct avltree_node gname_node;	/*< Node in the name tree */
	struct avltree_node gid_node;	/*< Node in the GID tree */
	TAILQ_ENTRY(cache_group) queue_entry; /* Node in group-fifo-queue */
	time_t epoch;
};

#define group_expired(group) (time(NULL) - (group)->epoch > \
		nfs_param.directory_services_param.idmapped_group_time_validity)

/**
 * @brief Number of entries in the UID cache, should be prime.
 */

#define id_cache_size 1009

/**
 * @brief A user fifo queue ordered by insertion timestamp
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
static TAILQ_HEAD(, cache_user) user_fifo_queue;

/* Similar to above, this is the fifo queue containing group entries */
static TAILQ_HEAD(, cache_group) group_fifo_queue;

/**
 * @brief UID cache, may only be accessed with idmapper_user_lock
 * held.  If idmapper_user_lock is held for read, it must be accessed
 * atomically.  (For a write, normal fetch/store is sufficient since
 * others are kept out.)
 */

static struct avltree_node *uid_cache[id_cache_size];

/**
 * @brief GID cache, may only be accessed with idmapper_group_lock
 * held.  If idmapper_group_lock is held for read, it must be accessed
 * atomically.  (For a write, normal fetch/store is sufficient since
 * others are kept out.)
 */

static struct avltree_node *gid_cache[id_cache_size];

/**
 * @brief Lock that protects the idmapper user cache
 */

pthread_rwlock_t idmapper_user_lock;

/**
 * @brief Lock that protects the idmapper group cache
 */

pthread_rwlock_t idmapper_group_lock;

/**
 * @brief Tree of users, by name
 */

static struct avltree uname_tree;

/**
 * @brief Tree of users, by ID
 */

static struct avltree uid_tree;

/**
 * @brief Tree of groups, by name
 */
static struct avltree gname_tree;

/**
 * @brief Tree of groups, by ID
 */

static struct avltree gid_tree;

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
	struct cache_user *user1 =
	    avltree_container_of(node1, struct cache_user,
				 uname_node);
	struct cache_user *usera =
	    avltree_container_of(nodea, struct cache_user,
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
	struct cache_user *user1 =
	    avltree_container_of(node1, struct cache_user,
				 uid_node);
	struct cache_user *usera =
	    avltree_container_of(nodea, struct cache_user,
				 uid_node);

	if (user1->uid < usera->uid)
		return -1;
	else if (user1->uid > usera->uid)
		return 1;
	else
		return 0;
}

/**
 * @brief Comparison for group names
 *
 * @param[in] node1 A node
 * @param[in] nodea Another node
 *
 * @retval -1 if node1 is less than nodea
 * @retval 0 if node1 and nodea are equal
 * @retval 1 if node1 is greater than nodea
 */

static inline int gname_comparator(const struct avltree_node *node1,
				   const struct avltree_node *nodea)
{
	struct cache_group *group1 =
	    avltree_container_of(node1, struct cache_group,
				 gname_node);
	struct cache_group *groupa =
	    avltree_container_of(nodea, struct cache_group,
				 gname_node);

	return gsh_buffdesc_comparator(&group1->gname, &groupa->gname);
}

/**
 * @brief Comparison for GIDs
 *
 * @param[in] node1 A node
 * @param[in] nodea Another node
 *
 * @retval -1 if node1 is less than nodea
 * @retval 0 if node1 and nodea are equal
 * @retval 1 if node1 is greater than nodea
 */

static int gid_comparator(const struct avltree_node *node1,
			  const struct avltree_node *nodea)
{
	struct cache_group *group1 =
	    avltree_container_of(node1, struct cache_group,
				 gid_node);
	struct cache_group *groupa =
	    avltree_container_of(nodea, struct cache_group,
				 gid_node);

	if (group1->gid < groupa->gid)
		return -1;
	else if (group1->gid > groupa->gid)
		return 1;
	else
		return 0;
}

/**
 * @brief Remove user entry from all user cache data structures
 *
 * @note The caller must hold idmapper_user_lock for write.
 */
static void remove_cache_user(struct cache_user *user)
{
	avltree_remove(&user->uname_node, &uname_tree);
	if (user->in_uidtree) {
		uid_cache[user->uid % id_cache_size] = NULL;
		avltree_remove(&user->uid_node, &uid_tree);
	}
	/* Remove from users fifo queue */
	TAILQ_REMOVE(&user_fifo_queue, user, queue_entry);
	gsh_free(user);
}

/**
 * @brief Remove group entry from all group cache data structures
 *
 * @note The caller must hold idmapper_group_lock for write.
 */
static void remove_cache_group(struct cache_group *group)
{
	gid_cache[group->gid % id_cache_size] = NULL;
	avltree_remove(&group->gid_node, &gid_tree);
	avltree_remove(&group->gname_node, &gname_tree);
	/* Remove from groups fifo queue */
	TAILQ_REMOVE(&group_fifo_queue, group, queue_entry);
	gsh_free(group);
}

/**
 * @brief Reaps the cache user entries
 *
 * Since the user-fifo queue stores entries in increasing order of time
 * validity, the reaper reaps from the queue head in the same order. It
 * stops when it first encounters a non-expired entry.
 */
static void reap_users_cache(void)
{
	struct cache_user *user;

	LogFullDebug(COMPONENT_IDMAPPER,
		"Idmapper user-cache reaper run started");
	PTHREAD_RWLOCK_wrlock(&idmapper_user_lock);

	for (user = TAILQ_FIRST(&user_fifo_queue); user != NULL;) {
		if (!user_expired(user))
			break;
		remove_cache_user(user);
		user = TAILQ_FIRST(&user_fifo_queue);
	}
	PTHREAD_RWLOCK_unlock(&idmapper_user_lock);
	LogFullDebug(COMPONENT_IDMAPPER,
		"Idmapper user-cache reaper run ended");
}

/**
 * @brief Reaps the cache group entries
 *
 * Since the group-fifo queue stores entries in increasing order of time
 * validity, the reaper reaps from the queue head in the same order. It
 * stops when it first encounters a non-expired entry.
 */
static void reap_groups_cache(void)
{
	struct cache_group *group;

	LogFullDebug(COMPONENT_IDMAPPER,
		"Idmapper group-cache reap run started");
	PTHREAD_RWLOCK_wrlock(&idmapper_group_lock);

	for (group = TAILQ_FIRST(&group_fifo_queue); group != NULL;) {
		if (!group_expired(group))
			break;
		remove_cache_group(group);
		group = TAILQ_FIRST(&group_fifo_queue);
	}
	PTHREAD_RWLOCK_unlock(&idmapper_group_lock);
	LogFullDebug(COMPONENT_IDMAPPER,
		"Idmapper group-cache reaper run ended");
}

/**
 * @brief Reaps the cache user, group entries
 */
void idmapper_cache_reap(void)
{
	reap_users_cache();
	reap_groups_cache();
}

/**
 * @brief Initialize the IDMapper cache
 */

void idmapper_cache_init(void)
{
	PTHREAD_RWLOCK_init(&idmapper_user_lock, NULL);
	PTHREAD_RWLOCK_init(&idmapper_group_lock, NULL);

	avltree_init(&uname_tree, uname_comparator, 0);
	avltree_init(&uid_tree, uid_comparator, 0);
	memset(uid_cache, 0, id_cache_size * sizeof(struct avltree_node *));

	avltree_init(&gname_tree, gname_comparator, 0);
	avltree_init(&gid_tree, gid_comparator, 0);
	memset(gid_cache, 0, id_cache_size * sizeof(struct avltree_node *));

	TAILQ_INIT(&user_fifo_queue);
	TAILQ_INIT(&group_fifo_queue);
}

/**
 * @brief Add a user entry to the cache
 *
 * @note The caller must hold idmapper_user_lock for write.
 *
 * @param[in] name The user name
 * @param[in] uid  The user ID
 * @param[in] gid  Optional.  Set to NULL if no gid is known.
 * @param[in] gss_princ true when name is gss principal.
 *                      The uid to name map is not added for gss principals.
 *
 * @retval true on success.
 * @retval false if our reach exceeds our grasp.
 */

bool idmapper_add_user(const struct gsh_buffdesc *name, uid_t uid,
		       const gid_t *gid, bool gss_princ)
{
	struct avltree_node *found_name;
	struct avltree_node *found_id;
	struct cache_user *old;
	struct cache_user *new;
	struct cache_user *user_fifo_queue_head_node;

	new = gsh_malloc(sizeof(struct cache_user) + name->len);
	new->epoch = time(NULL);
	new->uname.addr = (char *)new + sizeof(struct cache_user);
	new->uname.len = name->len;
	new->uid = uid;
	memcpy(new->uname.addr, name->addr, name->len);
	if (gid) {
		new->gid = *gid;
		new->gid_set = true;
	} else {
		new->gid = -1;
		new->gid_set = false;
	}
	new->in_uidtree = (gss_princ) ? false : true;

	/*
	 * There are 3 cases why we find an existing cache entry.
	 *
	 * Case 1:
	 * The threads that lookup by-name or by-id use the read lock.
	 * If they don't find an entry, then they release the read lock,
	 * acquire the write lock and then add the entry. So it is
	 * possible that multiple threads may fail to find an entry at
	 * one point and they all try to add. In this case, we will be
	 * trying to insert same name,id mapping.
	 *
	 * Case 2:
	 * It is also possible that name got a different id or an id got
	 * a different name causing us to find an existing entry when we
	 * are trying to add an entry. This case calls for removing the
	 * stale entry and update with this new entry.
	 *
	 * Case 3:
	 * The username to id mapping could be from plain nfs idmapping
	 * in which case we will not have a valid gid. If this is for a
	 * kerberos principal mapping, we will have uid and gid but we
	 * will not have "uid to name" cache entry (the reverse
	 * mapping). This case requires us to combine the old entry and
	 * the new entry!
	 *
	 * Note that the 3rd case happens if and only if IDMAPD_DOMAIN
	 * and LOCAL_REALMS are set to the same value!
	 */
	found_name = avltree_insert(&new->uname_node, &uname_tree);
	if (unlikely(found_name)) {
		old = avltree_container_of(found_name, struct cache_user,
					   uname_node);
		/* Combine non-expired old into new if uid's match */
		if ((old->uid == new->uid) && !user_expired(old)) {
			if (!new->gid_set && old->gid_set) {
				new->gid = old->gid;
				new->gid_set = true;
			}
			if (!new->in_uidtree && old->in_uidtree)
				new->in_uidtree = true;
		}

		/* Remove the old and insert the new */
		remove_cache_user(old);
		found_name = avltree_insert(&new->uname_node, &uname_tree);
		assert(found_name == NULL);
	}

	if (!new->in_uidtree) /* all done */
		goto add_to_queue;

	found_id = avltree_insert(&new->uid_node, &uid_tree);
	if (unlikely(found_id)) {
		old = avltree_container_of(found_id, struct cache_user,
					   uid_node);
		remove_cache_user(old);
		found_id = avltree_insert(&new->uid_node, &uid_tree);
		assert(found_id == NULL);
	}
	uid_cache[uid % id_cache_size] = &new->uid_node;

 add_to_queue:

	TAILQ_INSERT_TAIL(&user_fifo_queue, new, queue_entry);

	/* If we breach max-cache capacity, remove the user queue's head node */
	if (avltree_size(&uname_tree) >
		nfs_param.directory_services_param.cache_users_max_count) {
		LogDebug(COMPONENT_IDMAPPER,
			"Cache size limit violated, removing user with least time validity");
		user_fifo_queue_head_node = TAILQ_FIRST(&user_fifo_queue);
		const time_t cached_duration = time(NULL) -
			user_fifo_queue_head_node->epoch;
		remove_cache_user(user_fifo_queue_head_node);
		idmapper_monitoring__evicted_cache_entity(
			IDMAPPING_CACHE_ENTITY_USER, cached_duration);
	}
	return true;
}

/**
 * @brief Add a group entry to the cache
 *
 * @note The caller must hold idmapper_group_lock for write.
 *
 * @param[in] name The user name
 * @param[in] gid  The group id
 *
 * @retval true on success.
 * @retval false if our reach exceeds our grasp.
 */

bool idmapper_add_group(const struct gsh_buffdesc *name, const gid_t gid)
{
	struct avltree_node *found_name;
	struct avltree_node *found_id;
	struct cache_group *tmp;
	struct cache_group *new;
	struct cache_group *group_fifo_queue_head_node;

	new = gsh_malloc(sizeof(struct cache_group) + name->len);
	new->epoch = time(NULL);
	new->gname.addr = (char *)new + sizeof(struct cache_group);
	new->gname.len = name->len;
	new->gid = gid;
	memcpy(new->gname.addr, name->addr, name->len);

	/*
	 * The threads that lookup by-name or by-id use the read lock. If
	 * they don't find an entry, then they release the read lock,
	 * acquire the write lock and then add the entry. So it is
	 * possible that multiple threads may fail to find an entry at
	 * one point and they all try to add. In this case, we will be
	 * trying to insert same name,id mapping. It is also possible
	 * that name got a different id or an id got a different name
	 * causing us to find an existing entry when we are trying to
	 * add an entry!
	 *
	 * If we find an existing entry, we remove it from both the name
	 * and the id AVL trees, and then add the new entry.
	 */
	found_name = avltree_insert(&new->gname_node, &gname_tree);
	if (unlikely(found_name)) {
		tmp = avltree_container_of(found_name, struct cache_group,
					   gname_node);
		remove_cache_group(tmp);
		found_name = avltree_insert(&new->gname_node, &gname_tree);
		assert(found_name == NULL);
	}

	found_id = avltree_insert(&new->gid_node, &gid_tree);
	if (unlikely(found_id)) {
		tmp = avltree_container_of(found_id, struct cache_group,
					   gid_node);
		remove_cache_group(tmp);
		found_id = avltree_insert(&new->gid_node, &gid_tree);
		assert(found_id == NULL);
	}
	gid_cache[gid % id_cache_size] = &new->gid_node;

	TAILQ_INSERT_TAIL(&group_fifo_queue, new, queue_entry);

	/* If we breach max-cache capacity, remove the user queue's head node */
	if (avltree_size(&gname_tree) >
		nfs_param.directory_services_param.cache_groups_max_count) {
		LogDebug(COMPONENT_IDMAPPER,
			"Cache size limit violated, removing group with least time validity");
		group_fifo_queue_head_node = TAILQ_FIRST(&group_fifo_queue);
		const time_t cached_duration = time(NULL) -
			group_fifo_queue_head_node->epoch;
		remove_cache_group(group_fifo_queue_head_node);
		idmapper_monitoring__evicted_cache_entity(
			IDMAPPING_CACHE_ENTITY_GROUP, cached_duration);
	}
	return true;
}

/**
 * @brief Look up a user by name
 *
 * @note The caller must hold idmapper_user_lock for read.
 *
 * @param[in]  name The user name to look up.
 * @param[out] uid  The user ID found.  May be NULL if the caller
 *                  isn't interested in the UID.  (This seems
 *                  unlikely.)
 * @param[out] gid  The GID for the user, or NULL if there is
 *                  none. The caller may specify NULL if it isn't
 *                  interested.
 *
 * @retval true on success.
 * @retval false if we need to try, try again.
 */

bool idmapper_lookup_by_uname(const struct gsh_buffdesc *name, uid_t *uid,
			      const gid_t **gid, bool gss_princ)
{
	struct cache_user prototype = {
		.uname = *name
	};
	struct avltree_node *found_node = avltree_lookup(&prototype.uname_node,
							 &uname_tree);
	struct cache_user *found_user;
	void **cache_slot;

	if (unlikely(!found_node))
		return false;

	found_user =
	    avltree_container_of(found_node, struct cache_user, uname_node);
	if (!gss_princ) {
		/* I assume that if someone likes this user enough to look it
		   up by name, they'll like it enough to look it up by ID
		   later.

		   If the name is gss principal it does not have entry
		   in uid tree */

		cache_slot = (void **)
			&uid_cache[found_user->uid % id_cache_size];
		atomic_store_voidptr(cache_slot, &found_user->uid_node);
	}

	if (likely(uid))
		*uid = found_user->uid;

	if (unlikely(gid))
		*gid = (found_user->gid_set ? &found_user->gid : NULL);

	return user_expired(found_user) ? false : true;
}

/**
 * @brief Look up a user by ID
 *
 * @note The caller must hold idmapper_user_lock for read.
 *
 * @param[in]  uid  The user ID to look up.
 * @param[out] name The user name to look up. (May be NULL if the user
 *                  doesn't care about the name.)
 * @param[out] gid  The GID for the user, or NULL if there is
 *                  none. The caller may specify NULL if it isn't
 *                  interested.
 *
 * @retval true on success.
 * @retval false if we weren't so successful.
 */

bool idmapper_lookup_by_uid(const uid_t uid, const struct gsh_buffdesc **name,
			    const gid_t **gid)
{
	struct cache_user prototype = {
		.uid = uid
	};
	void **cache_slot = (void **)&uid_cache[uid % id_cache_size];
	struct avltree_node *found_node = atomic_fetch_voidptr(cache_slot);
	struct cache_user *found_user;
	bool found = false;

	if (likely(found_node)) {
		found_user = avltree_container_of(found_node,
						  struct cache_user,
						  uid_node);
		if (found_user->uid == uid)
			found = true;
	}

	if (unlikely(!found)) {
		found_node = avltree_lookup(&prototype.uid_node, &uid_tree);
		if (unlikely(!found_node))
			return false;

		atomic_store_voidptr(cache_slot, found_node);
		found_user = avltree_container_of(found_node,
						  struct cache_user,
						  uid_node);
	}

	if (likely(name))
		*name = &found_user->uname;

	if (gid)
		*gid = (found_user->gid_set ? &found_user->gid : NULL);

	return user_expired(found_user) ? false : true;
}

/**
 * @brief Lookup a group by name
 *
 * @note The caller must hold idmapper_group_lock for read.
 *
 * @param[in]  name The user name to look up.
 * @param[out] gid  The group ID found.  May be NULL if the caller
 *                  isn't interested in the GID.  (This seems
 *                  unlikely, since you can't get anything else from
 *                  this function.)
 *
 * @retval true on success.
 * @retval false if we need to try, try again.
 */

bool idmapper_lookup_by_gname(const struct gsh_buffdesc *name, uid_t *gid)
{
	struct cache_group prototype = {
		.gname = *name
	};
	struct avltree_node *found_node = avltree_lookup(&prototype.gname_node,
							 &gname_tree);
	struct cache_group *found_group;
	void **cache_slot;

	if (unlikely(!found_node))
		return false;

	found_group =
	    avltree_container_of(found_node, struct cache_group, gname_node);

	/* I assume that if someone likes this group enough to look it
	   up by name, they'll like it enough to look it up by ID
	   later. */

	cache_slot = (void **)&gid_cache[found_group->gid % id_cache_size];
	atomic_store_voidptr(cache_slot, &found_group->gid_node);

	if (likely(gid))
		*gid = found_group->gid;
	else
		LogDebug(COMPONENT_IDMAPPER, "Caller is being weird.");

	return group_expired(found_group) ? false : true;
}

/**
 * @brief Look up a group by ID
 *
 * @note The caller must hold idmapper_group_lock for read.
 *
 * @param[in]  gid  The group ID to look up.
 * @param[out] name The user name to look up. (May be NULL if the user
 *                  doesn't care about the name, which would be weird.)
 *
 * @retval true on success.
 * @retval false if we're most unfortunate.
 */

bool idmapper_lookup_by_gid(const gid_t gid, const struct gsh_buffdesc **name)
{
	struct cache_group prototype = {
		.gid = gid
	};
	void **cache_slot = (void **)&gid_cache[gid % id_cache_size];
	struct avltree_node *found_node = atomic_fetch_voidptr(cache_slot);
	struct cache_group *found_group;
	bool found = false;

	if (likely(found_node)) {
		found_group = avltree_container_of(found_node,
						   struct cache_group,
						   gid_node);
		if (found_group->gid == gid)
			found = true;
	}

	if (unlikely(!found)) {
		found_node = avltree_lookup(&prototype.gid_node, &gid_tree);
		if (unlikely(!found_node))
			return false;

		atomic_store_voidptr(cache_slot, found_node);
		found_group = avltree_container_of(found_node,
						   struct cache_group,
						   gid_node);
	}

	if (likely(name))
		*name = &found_group->gname;
	else
		LogDebug(COMPONENT_IDMAPPER, "Caller is being weird.");

	return group_expired(found_group) ? false : true;
}

/**
 * @brief Wipe out the idmapper cache
 */

void idmapper_clear_cache(void)
{
	struct avltree_node *node;

	PTHREAD_RWLOCK_wrlock(&idmapper_user_lock);
	PTHREAD_RWLOCK_wrlock(&idmapper_group_lock);

	memset(uid_cache, 0, id_cache_size * sizeof(struct avltree_node *));
	memset(gid_cache, 0, id_cache_size * sizeof(struct avltree_node *));

	for (node = avltree_first(&uname_tree);
	     node != NULL;
	     node = avltree_first(&uname_tree)) {
		struct cache_user *user;

		user = avltree_container_of(node,
					    struct cache_user, uname_node);
		remove_cache_user(user);
	}

	assert(avltree_first(&uid_tree) == NULL);

	for (node = avltree_first(&gname_tree);
	     node != NULL;
	     node = avltree_first(&gname_tree)) {
		struct cache_group *group;

		group = avltree_container_of(node,
					     struct cache_group, gname_node);
		remove_cache_group(group);
	}

	assert(avltree_first(&gid_tree) == NULL);

	PTHREAD_RWLOCK_unlock(&idmapper_group_lock);
	PTHREAD_RWLOCK_unlock(&idmapper_user_lock);
}

/**
 * @brief Destroy the IDMapper cache
 *
 * This function clears the cache, and destroys its locks.
 */
void idmapper_destroy_cache(void)
{
	idmapper_clear_cache();
	PTHREAD_RWLOCK_destroy(&idmapper_user_lock);
	PTHREAD_RWLOCK_destroy(&idmapper_group_lock);
}

#ifdef USE_DBUS

 /**
 *@brief Dbus method for showing idmapper cache
 *
 *@param[in]  args
 *@param[out] reply
 */
static bool show_idmapper(DBusMessageIter *args,
			  DBusMessage *reply,
			  DBusError *error)
{
	struct timespec timestamp;
	struct avltree_node *node;
	uint32_t val;
	DBusMessageIter iter, sub_iter, id_iter;
	char *namebuff = gsh_malloc(1024);
	dbus_bool_t gid_set;

	dbus_message_iter_init_append(reply, &iter);
	now(&timestamp);
	gsh_dbus_append_timestamp(&iter, &timestamp);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					"(subu)",
					&sub_iter);

	PTHREAD_RWLOCK_rdlock(&idmapper_user_lock);
	/* Traverse idmapper cache */
	for (node = avltree_first(&uname_tree);
		node != NULL;
		node = avltree_next(node)) {
		struct cache_user *user;

		user = avltree_container_of(node,
				struct cache_user, uname_node);
		dbus_message_iter_open_container(&sub_iter,
				DBUS_TYPE_STRUCT, NULL, &id_iter);
		memcpy(namebuff, user->uname.addr, user->uname.len);
		if (user->uname.len > 255)
			/*Truncate the name */
			*(namebuff + 255) = '\0';
		else
			*(namebuff + user->uname.len) = '\0';

		dbus_message_iter_append_basic(&id_iter,
				DBUS_TYPE_STRING, &namebuff);
		val = user->uid;
		dbus_message_iter_append_basic(&id_iter, DBUS_TYPE_UINT32,
				&val);

		if (user->gid_set) {
			val = user->gid;
			gid_set = true;
		} else {
			val = 0;
			gid_set = false;
		}
		dbus_message_iter_append_basic(&id_iter, DBUS_TYPE_BOOLEAN,
				&gid_set);
		dbus_message_iter_append_basic(&id_iter, DBUS_TYPE_UINT32,
				&val);
		dbus_message_iter_close_container(&sub_iter,
				&id_iter);
	}
	PTHREAD_RWLOCK_unlock(&idmapper_user_lock);
	free(namebuff);
	dbus_message_iter_close_container(&iter, &sub_iter);
	return true;
}


struct gsh_dbus_method cachemgr_show_idmapper = {
	.name = "showidmapper",
	.method = show_idmapper,
	.args = {TIMESTAMP_REPLY,
		{
		  .name = "ids",
		  .type = "a(subu)",
		  .direction = "out"},
		END_ARG_LIST}
};
#endif

/** @} */
