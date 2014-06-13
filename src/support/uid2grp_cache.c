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
#include "ganesha_types.h"
#include "common_utils.h"
#include "avltree.h"
#include "uid2grp.h"
#include "abstract_atomic.h"

/**
 * @brief User entry in the IDMapper cache
 */

struct cache_info {
	uid_t uid;		/*< Corresponding UID */
	struct gsh_buffdesc uname;
	struct group_data *gdata;
	struct avltree_node uname_node;	/*< Node in the name tree */
	struct avltree_node uid_node;	/*< Node in the UID tree */
};

/**
 * @brief Number of entires in the UID cache, should be prime.
 */

#define id_cache_size 1009

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

pthread_rwlock_t uid2grp_user_lock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * @brief Tree of users, by name
 */

static struct avltree uname_tree;

/**
 * @brief Tree of users, by ID
 */

static struct avltree uid_tree;

/**
 * @brief Compare two buffers
 *
 * Handle the case where one buffer is a left sub-buffer of another
 * buffer by counting the longer one as larger.
 *
 * @param[in] buff1 A buffer
 * @param[in] buffa Another buffer
 *
 * @retval -1 if buff1 is less than buffa
 * @retval 0 if buff1 and buffa are equal
 * @retval 1 if buff1 is greater than buffa
 */

static inline int buffdesc_comparator(const struct gsh_buffdesc *buffa,
				      const struct gsh_buffdesc *buff1)
{
	int mr = memcmp(buff1->addr, buffa->addr, MIN(buff1->len,
						      buffa->len));
	if (unlikely(mr == 0)) {
		if (buff1->len < buffa->len)
			return -1;
		else if (buff1->len > buffa->len)
			return 1;
		else
			return 0;
	} else {
		return mr;
	}
}

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

	return buffdesc_comparator(&user1->uname, &usera->uname);
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

/**
 * @brief Initialize the IDMapper cache
 */

void uid2grp_cache_init(void)
{
	avltree_init(&uname_tree, uname_comparator, 0);
	avltree_init(&uid_tree, uid_comparator, 0);
	memset(uid_grplist_cache, 0,
	       id_cache_size * sizeof(struct avltree_node *));
}

/* Remove given user/cache_info from the AVL trees
 *
 * @note The caller must hold uid2grp_user_lock for write.
 */
static void uid2grp_remove_user(struct cache_info *info)
{
	uid_grplist_cache[info->uid % id_cache_size] = NULL;
	avltree_remove(&info->uid_node, &uid_tree);
	avltree_remove(&info->uname_node, &uname_tree);
	/* We decrement hold on group data when it is
	 * removed from cache trees.
	 */
	uid2grp_release_group_data(info->gdata);
	gsh_free(info);
}

/**
 * @brief Add a user entry to the cache
 *
 * @note The caller must hold uid2grp_user_lock for write.
 *
 * @param[in] group_data that has supplementary groups allocated
 *
 * @retval true on success.
 * @retval false if our reach exceeds our grasp.
 */
bool uid2grp_add_user(struct group_data *gdata)
{
	struct avltree_node *name_node;
	struct avltree_node *id_node;
	struct avltree_node *name_node2 = NULL;
	struct avltree_node *id_node2 = NULL;
	struct cache_info *info;
	struct cache_info *tmp;

	info = gsh_malloc(sizeof(struct cache_info));
	if (!info) {
		LogEvent(COMPONENT_IDMAPPER, "memory alloc failed");
		return false;
	}
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
	}
	uid_grplist_cache[info->uid % id_cache_size] = &info->uid_node;

	if (name_node && id_node)
		LogWarn(COMPONENT_IDMAPPER, "shouldn't happen, internal error");
	if ((name_node && name_node2) || (id_node && id_node2))
		LogWarn(COMPONENT_IDMAPPER, "shouldn't happen, internal error");

	return true;
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
 * @brief Look up a user by name
 *
 * @note The caller must hold uid2grp_user_lock for read.
 *
 * @param[in]  name The user name to look up.
 * @param[out] uid  The user ID found.  May be NULL if the caller
 *                  isn't interested in the UID.  (This seems
 *                  unlikely.)
 * @gdata[out] group_data containing supplementary groups.
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
 * @brief Look up a user by ID
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

void uid2grp_remove_by_uid(const uid_t uid)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uid(uid, &info);
	if (success)
		uid2grp_remove_user(info);
}

void uid2grp_remove_by_uname(const struct gsh_buffdesc *name)
{
	struct cache_info *info;
	bool success;

	success = lookup_by_uname(name, &info);
	if (success)
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
