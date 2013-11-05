/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * -------------
 */

/**
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file cache_inode_avl.c
 * @brief AVL tree for caching directory entries
 */

#include "config.h"

#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "murmur3.h"
#include "city.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

void
cache_inode_avl_init(cache_entry_t *entry)
{
	avltree_init(&entry->object.dir.avl.t, avl_dirent_hk_cmpf,
		     0 /* flags */);
	avltree_init(&entry->object.dir.avl.c, avl_dirent_hk_cmpf,
		     0 /* flags */);
}

static inline struct avltree_node *
avltree_inline_lookup(
	const struct avltree_node *key,
	const struct avltree *tree)
{
	struct avltree_node *node = tree->root;
	int res = 0;

	while (node) {
		res = avl_dirent_hk_cmpf(node, key);
		if (res == 0)
			return node;
		if (res > 0)
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

void
avl_dirent_set_deleted(cache_entry_t *entry, cache_inode_dir_entry_t *v)
{
	struct avltree *t = &entry->object.dir.avl.t;
	struct avltree_node *node;

	assert(!(v->flags & DIR_ENTRY_FLAG_DELETED));

	node = avltree_inline_lookup(&v->node_hk, t);
	assert(node);
	avltree_remove(&v->node_hk, &entry->object.dir.avl.t);

#if EXTRA_CHECK_DELETED_WORKED
	node = avltree_inline_lookup(&v->node_hk, c);
	assert(!node);
#endif

	v->flags |= DIR_ENTRY_FLAG_DELETED;
	cache_inode_key_delete(&v->ckey);

	/* save cookie in deleted avl */
	avltree_insert(&v->node_hk, &entry->object.dir.avl.c);
}

void
avl_dirent_clear_deleted(cache_entry_t *entry,
			 cache_inode_dir_entry_t *v)
{
	struct avltree *t = &entry->object.dir.avl.t;
	struct avltree *c = &entry->object.dir.avl.c;
	struct avltree_node *node;

	node = avltree_inline_lookup(&v->node_hk, c);
	assert(node);
	avltree_remove(&v->node_hk, c);
	memset(&v->node_hk, 0, sizeof(struct avltree_node));

	node = avltree_insert(&v->node_hk, t);
	assert(!node);

	v->flags &= ~DIR_ENTRY_FLAG_DELETED;
}

static inline int
cache_inode_avl_insert_impl(cache_entry_t *entry,
			    cache_inode_dir_entry_t *v,
			    int j, int j2)
{
	int code = -1;
	struct avltree_node *node;
	struct avltree *t = &entry->object.dir.avl.t;
	struct avltree *c = &entry->object.dir.avl.c;

	/* first check for a previously-deleted entry */
	node = avltree_inline_lookup(&v->node_hk, c);

	/* XXX we must not allow persist-cookies to overrun resource
	 * management processes (ie, more coming in CIR/LRU) */
	if ((!node) && (avltree_size(c) > 65535)) {
		/* ie, recycle the smallest deleted entry */
		node = avltree_first(c);
	}

	/* We can't really re-use slots safely, since filenames can
	   have wildly differing lengths. */
#if 0
	if (node) {
		/* reuse the slot */
		v_exist =
		    avltree_container_of(node, cache_inode_dir_entry_t,
					 node_hk);
		FSAL_namecpy(&v_exist->name, &v->name);
		v_exist->entry = v->entry;
		avl_dirent_clear_deleted(entry, v_exist);
		v = v_exist;
		code = 1;	/* tell client to dispose v */
	} else {
		/* try to insert active */
		node = avltree_insert(&v->node_hk, t);
		if (!node)
			code = 0;
	}
#endif				/* 0 */

	if (node) {
		avltree_remove(node, c);
		node = NULL;
	}
	node = avltree_insert(&v->node_hk, t);
	if (!node)
		code = 0;

	switch (code) {
	case 0:
		/* success, note iterations */
		v->hk.p = j + j2;
		if (entry->object.dir.avl.collisions < v->hk.p)
			entry->object.dir.avl.collisions = v->hk.p;

		LogDebug(COMPONENT_CACHE_INODE,
			 "inserted new dirent on entry=%p cookie=%" PRIu64
			 " collisions %d", entry, v->hk.k,
			 entry->object.dir.avl.collisions);
		break;
	default:
		/* already inserted, or, keep trying at current j, j2 */
		LogDebug(COMPONENT_CACHE_INODE,
			 "Already existant when inserting new dirent on entry=%p "
			 "cookie=%" PRIu64 " this should never happen.", entry,
			 v->hk.k);
		break;
	}
	return code;
}

#define MIN_COOKIE_VAL 3

/*
 * Insert with quadatic, linear probing.  A unique k is assured for
 * any k whenever size(t) < max(uint64_t).
 *
 * First try quadratic probing, with coeff. 2 (since m = 2^n.)
 * A unique k is not assured, since the codomain is not prime.
 * If this fails, fall back to linear probing from hk.k+1.
 *
 * On return, the stored key is in v->hk.k, the iteration
 * count in v->hk.p.
 **/
int
cache_inode_avl_qp_insert(cache_entry_t *entry,
			  cache_inode_dir_entry_t *v)
{
#if AVL_HASH_MURMUR3
	uint32_t hk[4];
#endif
	int j, j2, code = -1;

	/* don't permit illegal cookies */
#if AVL_HASH_MURMUR3
	MurmurHash3_x64_128(v->name, strlen(v->name), 67, hk);
	memcpy(&v->hk.k, hk, 8);
#else
	v->hk.k = CityHash64WithSeed(v->name, strlen(v->name), 67);
#endif

#ifdef _USE_9P
	/* tmp hook : it seems like client running v9fs dislike "negative"
	 * cookies just kill the sign bit, making
	 * cookies 63 bits... */
	v->hk.k &= ~(1L << 63);
#endif

	/* XXX would we really wait for UINT64_MAX?  if not, how many
	 * probes should we attempt? */

	for (j = 0; j < UINT64_MAX; j++) {
		v->hk.k = (v->hk.k + (j * 2));

		/* reject values 0, 1 and 2 */
		if (v->hk.k < MIN_COOKIE_VAL)
			continue;

		code = cache_inode_avl_insert_impl(entry, v, j, 0);
		if (code >= 0)
			return code;
	}

	LogCrit(COMPONENT_CACHE_INODE,
		"cache_inode_avl_qp_insert_s: could not insert at j=%d (%s)", j,
		v->name);

#ifdef _USE_9P
	/* tmp hook : it seems like client running v9fs dislike "negative"
	 * cookies  */
	v->hk.k &= ~(1L << 63);
#endif
	for (j2 = 1 /* tried j=0 */; j2 < UINT64_MAX; j2++) {
		v->hk.k = v->hk.k + j2;
		code = cache_inode_avl_insert_impl(entry, v, j, j2);
		if (code >= 0)
			return code;
		j2++;
	}

	LogCrit(COMPONENT_CACHE_INODE,
		"cache_inode_avl_qp_insert_s: could not insert at j=%d (%s)", j,
		v->name);

	return -1;
}

cache_inode_dir_entry_t *
cache_inode_avl_lookup_k(cache_entry_t *entry, uint64_t k, uint32_t flags)
{
	struct avltree *t = &entry->object.dir.avl.t;
	struct avltree *c = &entry->object.dir.avl.c;
	cache_inode_dir_entry_t dirent_key[1], *dirent = NULL;
	struct avltree_node *node, *node2;

	dirent_key->hk.k = k;

	node = avltree_inline_lookup(&dirent_key->node_hk, t);
	if (node) {
		if (flags & CACHE_INODE_FLAG_NEXT_ACTIVE)
			/* client wants the cookie -after- the last we sent, and
			 * the Linux 3.0 and 3.1.0-rc7 clients misbehave if we
			 * resend the last one */
			node = avltree_next(node);
		if (!node) {
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "seek to cookie=%" PRIu64
				     " fail (no next entry)", k);
			goto out;
		}
	}

	/* Try the deleted AVL.  If a node with hk.k == v->hk.k is found,
	 * return its least upper bound in -t-, if any. */
	if (!node) {
		node2 = avltree_inline_lookup(&dirent_key->node_hk, c);
		if (node2)
			node = avltree_sup(&dirent_key->node_hk, t);
		LogDebug(COMPONENT_NFS_READDIR,
			 "node %p found deleted supremum %p", node2, node);
	}

	if (node)
		dirent =
		    avltree_container_of(node, cache_inode_dir_entry_t,
					 node_hk);

 out:
	return dirent;
}

cache_inode_dir_entry_t *
cache_inode_avl_qp_lookup_s(cache_entry_t *entry, const char *name, int maxj)
{
	struct avltree *t = &entry->object.dir.avl.t;
	struct avltree_node *node;
	cache_inode_dir_entry_t *v2;
#if AVL_HASH_MURMUR3
	uint32_t hashbuff[4];
#endif
	int j;
	size_t namelen = strlen(name);
	cache_inode_dir_entry_t v;

#if AVL_HASH_MURMUR3
	MurmurHash3_x64_128(name, namelen, 67, hashbuff);
	/* This seems to be correct.  The avltree_lookup function looks
	   as hk.k, but does no namecmp on its own, so there's no need to
	   allocate space for or copy the name in the key. */
	memcpy(&v.hk.k, hashbuff, 8);
#else
	v.hk.k = CityHash64WithSeed(name, namelen, 67);
#endif

#ifdef _USE_9P
	/* tmp hook : it seems like client running v9fs dislike "negative"
	 * cookies */
	v.hk.k &= ~(1L << 63);
#endif

	for (j = 0; j < maxj; j++) {
		v.hk.k = (v.hk.k + (j * 2));
		node = avltree_lookup(&v.node_hk, t);
		if (node) {
			/* ensure that node is related to v */
			v2 = avltree_container_of(node, cache_inode_dir_entry_t,
						  node_hk);
			if (strcmp(name, v2->name) == 0) {
				assert(!(v2->flags & DIR_ENTRY_FLAG_DELETED));
				return v2;
			}
		}
	}

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "cache_inode_avl_qp_lookup_s: entry not found at j=%d (%s)",
		     j, name);

	return NULL;
}

/** @} */
