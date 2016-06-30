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
 * @addtogroup FSAL_MDCACHE
 * @{
 */

/**
 * @file mdcache_avl.c
 * @brief AVL tree for caching directory entries
 */

#include "config.h"

#include "log.h"
#include "fsal.h"
#include "mdcache_int.h"
#include "mdcache_avl.h"
#include "murmur3.h"
#include "city.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

void
mdcache_avl_init(mdcache_entry_t *entry)
{
	avltree_init(&entry->fsobj.fsdir.avl.t, avl_dirent_hk_cmpf,
		     0 /* flags */);
	avltree_init(&entry->fsobj.fsdir.avl.c, avl_dirent_hk_cmpf,
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
avl_dirent_set_deleted(mdcache_entry_t *entry, mdcache_dir_entry_t *v)
{
	struct avltree_node *node;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Delete dir entry %p %s",
		     v, v->name);

	assert(!(v->flags & DIR_ENTRY_FLAG_DELETED));

	node = avltree_inline_lookup(&v->node_hk, &entry->fsobj.fsdir.avl.t);
	assert(node);
	avltree_remove(&v->node_hk, &entry->fsobj.fsdir.avl.t);

	v->flags |= DIR_ENTRY_FLAG_DELETED;
	mdcache_key_delete(&v->ckey);

	/* save cookie in deleted avl */
	node = avltree_insert(&v->node_hk, &entry->fsobj.fsdir.avl.c);
	assert(!node);
}

/**
 * @brief Insert a dirent into the cache.
 *
 * @param[in] entry The cache entry the dirent points to
 * @param[in] v     The dirent
 * @param[in] j     Part of iteration count
 * @param[in] j2    Part of iterarion count
 *
 * @retval 0  Success
 * @retval -1 Failure
 *
 */
static inline int
mdcache_avl_insert_impl(mdcache_entry_t *entry, mdcache_dir_entry_t *v,
			int j, int j2)
{
	int code = -1;
	struct avltree_node *node;
	struct avltree *t = &entry->fsobj.fsdir.avl.t;
	struct avltree *c = &entry->fsobj.fsdir.avl.c;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Insert dir entry %p %s j=%d j2=%d",
		     v, v->name, j, j2);

	/* first check for a previously-deleted entry */
	node = avltree_inline_lookup(&v->node_hk, c);

	/* We must not allow persist-cookies to overrun resource
	 * management processes.  Note this is not a limit on
	 * directory size, but rather indirectly on the lifetime
	 * of cookie offsets of directories under mutation. */
	if ((!node) && (avltree_size(c) >
			mdcache_param.dir.avl_max_deleted)) {
		/* ie, recycle the smallest deleted entry */
		node = avltree_first(c);
	}

	if (node) {
		/* We can't really re-use slots safely since filenames can
		 * have wildly differing lengths, so remove and free the
		 * "deleted" entry.
		 */
		mdcache_dir_entry_t *dirent =
		   avltree_container_of(node, mdcache_dir_entry_t, node_hk);

		avltree_remove(node, c);
		/* Don't need to free ckey; it was freed when marked deleted */
		gsh_free(dirent);
		node = NULL;
	}

	node = avltree_insert(&v->node_hk, t);

	if (!node) {
		/* success, note iterations */
		v->hk.p = j + j2;
		if (entry->fsobj.fsdir.avl.collisions < v->hk.p)
			entry->fsobj.fsdir.avl.collisions = v->hk.p;

		LogDebug(COMPONENT_CACHE_INODE,
			 "inserted new dirent for %s on entry=%p cookie=%"
			 PRIu64 " collisions %d", v->name, entry, v->hk.k,
			 entry->fsobj.fsdir.avl.collisions);
		code = 0;
	} else {
		/* already inserted, or, keep trying at current j, j2 */
		LogDebug(COMPONENT_CACHE_INODE,
			"Already existent when inserting new dirent on entry=%p name=%s, cookie=%"
			PRIu64 " this should never happen.",
			entry, v->name, v->hk.k);
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
 *
 * In the case of a name collision, assuming the ckey in the dirents matches,
 * and the flags are the same,  then this will be treated as a success and the
 * dirent passed in will be freed and the dirent will be set to the found one.
 *
 * If any error occurs, the passed in dirent will be freed and the dirent
 * will be set to NULL.
 *
 * @param[in] entry  The cache entry
 * @param[in] dirent The dirent
 *
 * @retval 0   Success
 * @retval -1  Hash collision after 2^65 attempts
 * @retval -2  Name collision
 *
 **/
int
mdcache_avl_qp_insert(mdcache_entry_t *entry, mdcache_dir_entry_t **dirent)
{
	mdcache_dir_entry_t *v = *dirent, *v2;

#if AVL_HASH_MURMUR3
	uint32_t hk[4];
#endif
	int j, j2, code = -1;

	LogFullDebug(COMPONENT_CACHE_INODE,
		     "Insert dir entry %p %s",
		     v, v->name);

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
	v->hk.k &= ~(1ULL << 63);
#endif

	/* XXX would we really wait for UINT64_MAX?  if not, how many
	 * probes should we attempt? */

	for (j = 0; j < UINT64_MAX; j++) {
		v->hk.k = (v->hk.k + (j * 2));

		/* reject values 0, 1 and 2 */
		if (v->hk.k < MIN_COOKIE_VAL)
			continue;

		code = mdcache_avl_insert_impl(entry, v, j, 0);
		if (code >= 0)
			return code;
		/* detect name conflict */
		if (j == 0) {
			v2 = mdcache_avl_lookup_k(entry, v->hk.k,
						  MDCACHE_FLAG_ONLY_ACTIVE);
			assert(v != v2);
			if (v2 && (strcmp(v->name, v2->name) == 0)) {
				LogDebug(COMPONENT_CACHE_INODE,
					 "name conflict dirent %p and %p both have name %s",
					 v, v2, v->name);
				if (mdcache_key_cmp(&v->ckey, &v2->ckey) == 0 &&
				    v->flags == v2->flags) {
					/* This appears to be the same entry,
					 * return the one from the table (v2)
					 * and free the passed in one and
					 * return success.
					 */
					code = 0;
				} else {
					/* Discard the new dirent and set to
					 * NULL.
					 */
					code = -2;
					v2 = NULL;
				}
				goto out;
			}
		}
	}

	LogCrit(COMPONENT_CACHE_INODE, "could not insert at j=%d (%s)", j,
		v->name);

#ifdef _USE_9P
	/* tmp hook : it seems like client running v9fs dislike "negative"
	 * cookies  */
	v->hk.k &= ~(1ULL << 63);
#endif
	for (j2 = 1 /* tried j=0 */; j2 < UINT64_MAX; j2 += 2) {
		v->hk.k = v->hk.k + j2;
		code = mdcache_avl_insert_impl(entry, v, j, j2);
		if (code >= 0)
			return code;
	}

	LogCrit(COMPONENT_CACHE_INODE,
		"could not insert at j=%d (%s)", j,
		v->name);

	code = -1;
	v2 = NULL;

out:

	mdcache_key_delete(&v->ckey);
	gsh_free(v);
	*dirent = v2;

	return code;
}

mdcache_dir_entry_t *
mdcache_avl_lookup_k(mdcache_entry_t *entry, uint64_t k, uint32_t flags)
{
	struct avltree *t = &entry->fsobj.fsdir.avl.t;
	struct avltree *c = &entry->fsobj.fsdir.avl.c;
	mdcache_dir_entry_t dirent_key[1], *dirent = NULL;
	struct avltree_node *node, *node2;

	dirent_key->hk.k = k;

	node = avltree_inline_lookup(&dirent_key->node_hk, t);
	if (node) {
		if (flags & MDCACHE_FLAG_NEXT_ACTIVE)
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

	/* only the forward AVL is valid for conflict checking */
	if (flags & MDCACHE_FLAG_ONLY_ACTIVE)
		goto done;

	/* Try the deleted AVL.  If a node with hk.k == v->hk.k is found,
	 * return its least upper bound in -t-, if any. */
	if (!node) {
		node2 = avltree_inline_lookup(&dirent_key->node_hk, c);
		if (node2)
			node = avltree_sup(&dirent_key->node_hk, t);
		LogDebug(COMPONENT_NFS_READDIR,
			 "node %p found deleted supremum %p", node2, node);
	}

done:
	if (node)
		dirent =
		    avltree_container_of(node, mdcache_dir_entry_t, node_hk);

out:
	return dirent;
}

mdcache_dir_entry_t *
mdcache_avl_qp_lookup_s(mdcache_entry_t *entry, const char *name, int maxj)
{
	struct avltree *t = &entry->fsobj.fsdir.avl.t;
	struct avltree_node *node;
	mdcache_dir_entry_t *v2;
#if AVL_HASH_MURMUR3
	uint32_t hashbuff[4];
#endif
	int j;
	size_t namelen = strlen(name);
	mdcache_dir_entry_t v;

	LogFullDebug(COMPONENT_CACHE_INODE, "Lookup %s", name);

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
	v.hk.k &= ~(1ULL << 63);
#endif

	for (j = 0; j < maxj; j++) {
		v.hk.k = (v.hk.k + (j * 2));
		node = avltree_lookup(&v.node_hk, t);
		if (node) {
			/* ensure that node is related to v */
			v2 = avltree_container_of(node, mdcache_dir_entry_t,
						  node_hk);
			if (strcmp(name, v2->name) == 0) {
				assert(!(v2->flags & DIR_ENTRY_FLAG_DELETED));
				return v2;
			}
		}
	}

	LogFullDebug(COMPONENT_CACHE_INODE, "entry not found at j=%d (%s)", j,
		     name);

	return NULL;
}

/**
 * @brief Remove and free all dirents from a dirent tree
 *
 * @param[in] tree	Tree to remove from
 */
void mdcache_avl_clean_tree(struct avltree *tree)
{
	struct avltree_node *dirent_node = NULL;
	mdcache_dir_entry_t *dirent = NULL;

	while ((dirent_node = avltree_first(tree))) {
		dirent = avltree_container_of(dirent_node, mdcache_dir_entry_t,
					      node_hk);
		LogFullDebug(COMPONENT_CACHE_INODE, "Invalidate %p %s",
			     dirent, dirent->name);
		avltree_remove(dirent_node, tree);
		if (dirent->ckey.kv.len)
			mdcache_key_delete(&dirent->ckey);
		gsh_free(dirent);
	}
}

/** @} */
