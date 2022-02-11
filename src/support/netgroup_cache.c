// SPDX-License-Identifier: LGPL-3.0-or-later
/*
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

#include "config.h"
#include "log.h"
#include "config_parsing.h"
#include <string.h>
#include <unistd.h>
#include "gsh_intrinsic.h"
#include "gsh_types.h"
#include "common_utils.h"
#include "avltree.h"
#include "abstract_atomic.h"
#include "netdb.h"
#include "abstract_mem.h"
#include "netgroup_cache.h"

/* Netgroup cache information */
struct ng_cache_info {
	struct avltree_node ng_node;
	struct gsh_buffdesc ng_group;
	struct gsh_buffdesc ng_host;
	time_t ng_epoch;
};

#define NG_CACHE_SIZE 1009

/* Uses FNV hash */
#define FNV_PRIME32 16777619
#define FNV_OFFSET32 2166136261U
static int ng_hash_key(struct ng_cache_info *info)
{
	uint32_t hash = FNV_OFFSET32;
	char *bp, *end;

	bp = info->ng_host.addr;
	end = bp + info->ng_host.len;
	while (bp < end) {
		hash ^= *bp++;
		hash *= FNV_PRIME32;
	}
	bp = info->ng_group.addr;
	end = bp + info->ng_group.len;
	while (bp < end) {
		hash ^= *bp++;
		hash *= FNV_PRIME32;
	}
	return hash % NG_CACHE_SIZE;
}

static struct avltree_node *ng_cache[NG_CACHE_SIZE];

pthread_rwlock_t ng_lock = PTHREAD_RWLOCK_INITIALIZER;

/* Positive and negative cache trees */
static struct avltree pos_ng_tree;
static struct avltree neg_ng_tree;

static inline int buffdesc_comparator(const struct gsh_buffdesc *buff1,
				      const struct gsh_buffdesc *buff2)
{
	if (buff1->len < buff2->len)
		return -1;
	if (buff1->len > buff2->len)
		return 1;
	return memcmp(buff1->addr, buff2->addr, buff1->len);
}

static int ng_comparator(const struct avltree_node *node1,
			 const struct avltree_node *node2)
{
	int rc;
	struct ng_cache_info *info1;
	struct ng_cache_info *info2;

	info1 = avltree_container_of(node1, struct ng_cache_info, ng_node);
	info2 = avltree_container_of(node2, struct ng_cache_info, ng_node);

	/* compare host followed by group if needed */
	rc = buffdesc_comparator(&info1->ng_host, &info2->ng_host);
	if (rc == 0)
		rc = buffdesc_comparator(&info1->ng_group, &info2->ng_group);

	return rc;
}

static bool ng_expired(struct avltree_node *node)
{
	struct ng_cache_info *info;

	info = avltree_container_of(node, struct ng_cache_info, ng_node);

	/* Hardcoded to 30 minutes for now */
	if (time(NULL) - info->ng_epoch > 30 * 60)
		return true;

	return false;
}



/**
 * @brief Initialize the netgroups cache
 */
void ng_cache_init(void)
{
	avltree_init(&pos_ng_tree, ng_comparator, 0);
	avltree_init(&neg_ng_tree, ng_comparator, 0);
	memset(ng_cache, 0, NG_CACHE_SIZE * sizeof(struct avltree_node *));
}

static void ng_free(struct ng_cache_info *info)
{
	gsh_free(info->ng_group.addr);
	gsh_free(info->ng_host.addr);
	gsh_free(info);
}

/* The caller must hold ng_lock for write */
static void ng_remove(struct ng_cache_info *info, bool negative)
{
	if (negative) {
		avltree_remove(&info->ng_node, &neg_ng_tree);
	} else {
		ng_cache[ng_hash_key(info)] = NULL;
		avltree_remove(&info->ng_node, &pos_ng_tree);
	}
}

 /* The caller must hold ng_lock for write */
static void ng_add(const char *group, const char *host, bool negative)
{
	struct ng_cache_info *info;
	struct avltree_node *found_node;
	struct ng_cache_info *found_info;

	info = gsh_malloc(sizeof(struct ng_cache_info));
	if (!info)
		LogFatal(COMPONENT_IDMAPPER, "memory alloc failed");

	info->ng_group.addr = gsh_strdup(group);
	info->ng_group.len = strlen(group)+1;
	info->ng_host.addr = gsh_strdup(host);
	info->ng_host.len = strlen(host)+1;
	info->ng_epoch = time(NULL);

	if (negative) {
		/* @todo check positive cache first? */
		found_node = avltree_insert(&info->ng_node, &neg_ng_tree);

		/* If an already existing entry is found, keep the old
		 * entry, and free the current entry
		 */
		if (found_node) {
			found_info = avltree_container_of(found_node,
					struct ng_cache_info, ng_node);
			found_info->ng_epoch = info->ng_epoch;
			ng_free(info);
		}
	} else {
		/* @todo delete from negative cache if there? */
		found_node = avltree_insert(&info->ng_node, &pos_ng_tree);

		/* If an already existing entry is found, keep the old
		 * entry, and free the current entry
		 */
		if (found_node) {
			found_info = avltree_container_of(found_node,
					struct ng_cache_info, ng_node);
			ng_cache[ng_hash_key(found_info)] = found_node;
			found_info->ng_epoch = info->ng_epoch;
			ng_free(info);
		} else {
			ng_cache[ng_hash_key(info)] = &info->ng_node;
		}
	}
}

/* The caller must hold ng_lock for read */
static bool ng_lookup(const char *group, const char *host, bool negative)
{
	struct ng_cache_info prototype = {
		.ng_group.addr = (char *)group,
		.ng_group.len = strlen(group)+1,
		.ng_host.addr = (char *)host,
		.ng_host.len = strlen(host)+1
	};
	struct avltree_node *node;
	struct ng_cache_info *info;
	void **cache_slot;

	if (negative) {
		node = avltree_lookup(&prototype.ng_node, &neg_ng_tree);
		if (!node)
			return false;

		if (!ng_expired(node))
			return true;

		goto expired;
	}

	/* Positive lookups are stored in the cache */
	cache_slot = (void **)&ng_cache[ng_hash_key(&prototype)];
	node = atomic_fetch_voidptr(cache_slot);
	if (node && ng_comparator(node, &prototype.ng_node) == 0) {
		if (!ng_expired(node))
			return true;
		goto expired;
	}

	/* cache miss, search AVL tree */
	node = avltree_lookup(&prototype.ng_node, &pos_ng_tree);
	if (!node)
		return false;

	if (ng_expired(node))
		goto expired;

	atomic_store_voidptr(cache_slot, node);

	return true;

expired:
	/* entry expired, acquire write mode lock for removal */
	PTHREAD_RWLOCK_unlock(&ng_lock);
	PTHREAD_RWLOCK_wrlock(&ng_lock);

	/* Since we dropped the read mode lock and acquired write mode
	 * lock, make sure that the entry is still in the tree.
	 */
	if (negative)
		node = avltree_lookup(&prototype.ng_node, &neg_ng_tree);
	else
		node = avltree_lookup(&prototype.ng_node, &pos_ng_tree);

	if (node) {
		info = avltree_container_of(node, struct ng_cache_info,
					    ng_node);
		ng_remove(info, negative);
		ng_free(info);
	}
	PTHREAD_RWLOCK_unlock(&ng_lock);
	PTHREAD_RWLOCK_rdlock(&ng_lock);
	return false;
}

/**
 * @brief Verify if the given host is in the given netgroup or not
 */
bool ng_innetgr(const char *group, const char *host)
{
	int rc;

	/* Check positive lookup and then negative lookup.  If absent in
	 * both, then do a real innetgr call and cache the results.
	 */
	PTHREAD_RWLOCK_rdlock(&ng_lock);
	if (ng_lookup(group, host, false)) { /* positive lookup */
		PTHREAD_RWLOCK_unlock(&ng_lock);
		return true;
	}
	if (ng_lookup(group, host, true)) { /* negative lookup */
		PTHREAD_RWLOCK_unlock(&ng_lock);
		return false;
	}
	PTHREAD_RWLOCK_unlock(&ng_lock);

	/* Call innetgr() under a lock. It is supposed to be thread safe
	 * but sssd doesn't handle multiple threads calling innetgr() at
	 * the same time resulting in erratic returns. sssd team will
	 * fix this behavior in a future release but we can make it
	 * single threaded as a workaround.  Shouldn't be a performance
	 * issue as this shouldn't happen often.
	 */
	PTHREAD_RWLOCK_wrlock(&ng_lock);
	rc = innetgr(group, host, NULL, NULL);
	if (rc)
		ng_add(group, host, false);	/* positive lookup */
	else
		ng_add(group, host, true);	/* negative lookup */
	PTHREAD_RWLOCK_unlock(&ng_lock);

	return rc;
}

/**
 * @brief Wipe out the netgroup cache
 */
void ng_clear_cache(void)
{
	struct avltree_node *node;
	struct ng_cache_info *info;

	PTHREAD_RWLOCK_wrlock(&ng_lock);

	while ((node = avltree_first(&pos_ng_tree))) {
		info = avltree_container_of(node, struct ng_cache_info,
					    ng_node);
		ng_remove(info, false);
		ng_free(info);
	}

	while ((node = avltree_first(&neg_ng_tree))) {
		info = avltree_container_of(node, struct ng_cache_info,
					    ng_node);
		ng_remove(info, true);
		ng_free(info);
	}

	assert(avltree_first(&pos_ng_tree) == NULL);
	assert(avltree_first(&neg_ng_tree) == NULL);

	PTHREAD_RWLOCK_unlock(&ng_lock);
}
