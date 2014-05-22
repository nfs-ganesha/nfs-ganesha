/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2013, The Linux Box Corporation
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
 * @file cache_inode_hash.h
 * @brief Cache inode hashed dictionary package
 *
 * This module exports an interface for efficient lookup of cache entries
 * by file handle, (etc?).  Refactored from the prior abstract HashTable
 * implementation.
 */

#ifndef CACHE_INODE_HASH_H
#define CACHE_INODE_HASH_H

#include "config.h"
#include "log.h"
#include "abstract_atomic.h"
#include "cache_inode.h"
#include "gsh_intrinsic.h"
#include "cache_inode_lru.h"
#include "city.h"
#include <libgen.h>

/**
 * @brief The table partition
 *
 * Each tree is independent, having its own lock, thus reducing thread
 * contention.
 */
typedef struct cih_partition {
	uint32_t part_ix;
	pthread_rwlock_t lock;
	struct avltree t;
	struct avltree_node **cache;
	struct {
		char *func;
		uint32_t line;
	} locktrace;
	 CACHE_PAD(0);
} cih_partition_t;

/**
 * @brief The weakref table structure
 *
 * This is the structure corresponding to a single table of weakrefs.
 */
struct cih_lookup_table {
	CACHE_PAD(0);
	cih_partition_t *partition;
	uint32_t npart;
	uint32_t cache_sz;
};

/* Support inline lookups */
extern struct cih_lookup_table cih_fhcache;

/**
 * @brief Initialize the package.
 */
void cih_pkginit(void);

/**
 * @brief Destroy the package.
 */
void cih_pkgdestroy(void);

/**
 * @brief Find the correct partition for a pointer
 *
 * To lower thread contention, the table is composed of multiple
 * trees, with the tree that receives a pointer determined by a
 * modulus.  This macro yields an expression that yields a pointer to
 * the correct partition.
 */

#define cih_partition_of_scalar(lt, k) \
	(((lt)->partition)+(((uint64_t)k)%(lt)->npart))

/**
 * @brief Compute cache slot for an entry
 *
 * This function computes a hash slot, taking an address modulo the
 * number of cache slotes (which should be prime).
 *
 * @param wt [in] The table
 * @param ptr [in] Entry address
 *
 * @return The computed offset.
 */
static inline uint32_t
cih_cache_offsetof(struct cih_lookup_table *lt, uint64_t k)
{
	return k % lt->cache_sz;
}

/**
 * @brief Cache inode FH hashed comparison function.
 *
 * Entries are ordered by integer hash first, and second by bitwise
 * comparison of the corresponding file handle.
 *
 * For key prototypes, which have no object handle, the buffer pointed to
 * by fh_k.fh_desc_k is taken to be the file handle.  Further, ONLY key
 * prototype entries may have a non-NULL value for fh_k.fh_desc_k.
 *
 * @param lhs [in] First node
 * @param rhs [in] Second node
 *
 * @retval -1: lhs compares as less than rhs
 * @retval 0: lhs and rhs compare equal
 * @retval 1: lhs is greater than rhs
 */
static inline int cih_fh_cmpf(const struct avltree_node *lhs,
			      const struct avltree_node *rhs)
{
	cache_entry_t *lk, *rk;

	lk = avltree_container_of(lhs, cache_entry_t, fh_hk.node_k);
	rk = avltree_container_of(rhs, cache_entry_t, fh_hk.node_k);

	if (likely(lk->fh_hk.key.hk < rk->fh_hk.key.hk))
		return -1;

	if (likely(lk->fh_hk.key.hk > rk->fh_hk.key.hk))
		return 1;

	if (unlikely(lk->fh_hk.key.kv.len < rk->fh_hk.key.kv.len))
		return -1;

	if (unlikely(lk->fh_hk.key.kv.len > rk->fh_hk.key.kv.len))
		return 1;

	if (unlikely(lk->fh_hk.key.fsal < rk->fh_hk.key.fsal))
		return -1;

	if (unlikely(lk->fh_hk.key.fsal > rk->fh_hk.key.fsal))
		return 1;

	/* deep compare */
	return memcmp(lk->fh_hk.key.kv.addr,
		      rk->fh_hk.key.kv.addr,
		      lk->fh_hk.key.kv.len);
}

/**
 * @brief Open-coded avltree lookup
 *
 * Search for an entry matching key in avltree tree.
 *
 * @param tree [in] The avltree to search
 * @param key [in] Entry being searched for, as an avltree node
 *
 * @return Pointer to node if found, else NULL.
 */
static inline struct avltree_node *
cih_fhcache_inline_lookup(const struct avltree *tree,
			  const struct avltree_node *key)
{
	struct avltree_node *node = tree->root;
	int res = 0;

	while (node) {
		res = cih_fh_cmpf(node, key);
		if (res == 0)
			return node;
		if (res > 0)
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

#define CIH_HASH_NONE           0x0000
#define CIH_HASH_KEY_PROTOTYPE  0x0001

/**
 * @brief Convenience function to compute hash for cache_entry_t
 *
 * Computes hash of entry using input fh_desc.  If entry is not a
 * disposable key prototype, fh_desc is duplicated in entry.
 *
 * @param entry [in] Entry to be hashed
 * @param fh_desc [in] Hash input bytes
 *
 * @return (void)
 */
static inline bool
cih_hash_key(cache_inode_key_t *key,
	     struct fsal_module *fsal,
	     struct gsh_buffdesc *fh_desc,
	     uint32_t flags)
{
	key->fsal = fsal;

	/* fh prototype fixup */
	if (flags & CIH_HASH_KEY_PROTOTYPE) {
		key->kv = *fh_desc;
	} else {
		/* XXX dups fh_desc */
		key->kv.len = fh_desc->len;
		key->kv.addr = gsh_malloc(fh_desc->len);
		if (key->kv.addr == NULL)
			return false;
		memcpy(key->kv.addr, fh_desc->addr, fh_desc->len);
	}

	/* hash it */
	key->hk =
	    CityHash64WithSeed(fh_desc->addr, fh_desc->len, 557);

	return true;
}

#define CIH_GET_NONE           0x0000
#define CIH_GET_RLOCK          0x0001
#define CIH_GET_WLOCK          0x0002
#define CIH_GET_UNLOCK_ON_MISS 0x0004

/**
 * @brief Hash latch structure.
 *
 * Used to memoize a partition and its lock state between calls.  Nod
 * to Adam's precursor in HashTable.
 */
typedef struct cih_latch {
	cih_partition_t *cp;
} cih_latch_t;

static inline void
cih_latch_rele(cih_latch_t *latch)
{
	PTHREAD_RWLOCK_unlock(&(latch->cp->lock));
}

/**
 * @brief Lookup cache entry by key, optionally return with hash partition
 * shared or exclusive locked.
 *
 * Lookup cache entry by fh, optionally return with hash partition shared
 * or exclusive locked.  Differs from the fh variant in using the precomputed
 * hash stored with key.
 *
 * @param key [in] Key being searched
 * @param latch [out] Pointer to partition
 * @param flags [in] Flags
 *
 * @return Pointer to cache entry if found, else NULL
 */
static inline cache_entry_t *
cih_get_by_key_latched(cache_inode_key_t *key, cih_latch_t *latch,
		       uint32_t flags, const char *func, int line)
{
	cache_entry_t k_entry, *entry = NULL;
	struct avltree_node *node;
	cih_partition_t *cp;
	void **cache_slot;

	k_entry.fh_hk.key = *key;
	latch->cp = cp =
	    cih_partition_of_scalar(&cih_fhcache, k_entry.fh_hk.key.hk);

	if (flags & CIH_GET_WLOCK)
		PTHREAD_RWLOCK_wrlock(&cp->lock);	/* SUBTREE_WLOCK */
	else
		PTHREAD_RWLOCK_rdlock(&cp->lock);	/* SUBTREE_RLOCK */

	cp->locktrace.func = (char *)func;
	cp->locktrace.line = line;

	/* check cache */
	cache_slot = (void **)
	    &(cp->cache[cih_cache_offsetof(&cih_fhcache, key->hk)]);
	node = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
	if (node) {
		if (cih_fh_cmpf(&k_entry.fh_hk.node_k, node) == 0) {
			/* got it in 1 */
			LogDebug(COMPONENT_HASHTABLE_CACHE,
				 "cih cache hit slot %d",
				 cih_cache_offsetof(&cih_fhcache, key->hk));
			goto found;
		}
	}

	/* check AVL */
	node = cih_fhcache_inline_lookup(&cp->t, &k_entry.fh_hk.node_k);
	if (!node) {
		if (flags & CIH_GET_UNLOCK_ON_MISS)
			PTHREAD_RWLOCK_unlock(&cp->lock);
		LogDebug(COMPONENT_HASHTABLE_CACHE, "fdcache MISS");
		goto out;
	}

	/* update cache */
	atomic_store_voidptr(cache_slot, node);

	LogDebug(COMPONENT_HASHTABLE_CACHE, "cih AVL hit slot %d",
		 cih_cache_offsetof(&cih_fhcache, key->hk));

 found:
	entry = avltree_container_of(node, cache_entry_t, fh_hk.node_k);
 out:
	return entry;
}

/**
 * @brief Latch the partition of entry.
 *
 * Latch the partition of entry.
 *
 * @param entry [in] The entry
 * @param latch [inout] Latch
 * @param flags [in] Flags
 *
 * @return void
 */
static inline bool
cih_latch_entry(cache_entry_t *entry, cih_latch_t *latch, uint32_t flags,
		const char *func, int line)
{
	cih_partition_t *cp;

	latch->cp = cp =
	    cih_partition_of_scalar(&cih_fhcache, entry->fh_hk.key.hk);

	if (flags & CIH_GET_WLOCK)
		PTHREAD_RWLOCK_wrlock(&cp->lock);	/* SUBTREE_WLOCK */
	else
		PTHREAD_RWLOCK_rdlock(&cp->lock);	/* SUBTREE_RLOCK */

	cp->locktrace.func = (char *)func;
	cp->locktrace.line = line;

	return true;
}

#define CIH_SET_NONE     0x0000
#define CIH_SET_HASHED   0x0001	/* previously hashed entry */
#define CIH_SET_UNLOCK   0x0002

/**
 * @brief Insert cache entry on partition previously locked.
 *
 * Insert cache entry on partition previously locked.
 *
 * @param entry [in] Entry to be inserted
 * @param fh_desc [in] Hash input bytes (MUST be those used previously)
 * @param flags [in] Flags
 *
 * @return Pointer to cache entry if found, else NULL
 */
static inline int
cih_set_latched(cache_entry_t *entry, cih_latch_t *latch,
		struct fsal_module *fsal,
		struct gsh_buffdesc *fh_desc,
		uint32_t flags)
{
	cih_partition_t *cp = latch->cp;

	/* Omit hash if you are SURE we hashed it, and that the
	 * hash remains valid */
	if (unlikely(!(flags & CIH_SET_HASHED)))
		if (!cih_hash_key(&entry->fh_hk.key, fsal,
				  fh_desc, CIH_HASH_NONE))
			return 1;

	(void)avltree_insert(&entry->fh_hk.node_k, &cp->t);
	entry->fh_hk.inavl = true;

	if (likely(flags & CIH_SET_UNLOCK))
		PTHREAD_RWLOCK_unlock(&cp->lock);

	return 0;
}

/**
 * @brief Remove cache entry with existence check.
 *
 * Remove cache entry with existence check.  The entry is assumed to
 * be hashed.
 *
 * @param entry [in] Entry to be removed.0
 *
 * @return (void)
 */
static inline void
cih_remove_checked(cache_entry_t *entry)
{
	struct avltree_node *node;
	cih_partition_t *cp =
	    cih_partition_of_scalar(&cih_fhcache, entry->fh_hk.key.hk);

	PTHREAD_RWLOCK_wrlock(&cp->lock);
	node = cih_fhcache_inline_lookup(&cp->t, &entry->fh_hk.node_k);
	if (node) {
		avltree_remove(node, &cp->t);
		cp->cache[cih_cache_offsetof(&cih_fhcache,
					     entry->fh_hk.key.hk)] = NULL;
		entry->fh_hk.inavl = false;
		/* return sentinel ref */
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
	}
	PTHREAD_RWLOCK_unlock(&cp->lock);
}

/**
 * @brief Remove cache entry protected by latch
 *
 * Remove cache entry.
 *
 * @param entry [in] Entry to be removed.0
 *
 * @return true if entry is invalid
 */
#define CIH_REMOVE_NONE    0x0000
#define CIH_REMOVE_UNLOCK  0x0001
#define CIH_REMOVE_QLOCKED 0x0002

static inline bool
cih_remove_latched(cache_entry_t *entry, cih_latch_t *latch, uint32_t flags)
{
	cih_partition_t *cp =
	    cih_partition_of_scalar(&cih_fhcache, entry->fh_hk.key.hk);
	uint32_t lflags = LRU_FLAG_NONE;

	if (entry->fh_hk.inavl) {
		avltree_remove(&entry->fh_hk.node_k, &cp->t);
		cp->cache[cih_cache_offsetof(&cih_fhcache,
					     entry->fh_hk.key.hk)] = NULL;
		entry->fh_hk.inavl = false;
		if (flags & CIH_REMOVE_QLOCKED)
			lflags |= LRU_UNREF_QLOCKED;
		cache_inode_lru_unref(entry, lflags);
		if (flags & CIH_REMOVE_UNLOCK)
			PTHREAD_RWLOCK_unlock(&cp->lock);
		return true;
	}
	if (flags & CIH_REMOVE_UNLOCK)
		PTHREAD_RWLOCK_unlock(&cp->lock);

	return false;
}

#endif				/* CACHE_INODE_HASH_H */
/** @} */
