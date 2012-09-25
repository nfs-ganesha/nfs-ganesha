/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "generic_weakref.h"

/**
 *
 * \file generic_weakref.c
 * \author Matt Benjamin
 * \brief Generic weak reference package
 *
 * \section DESCRIPTION
 *
 * This module defines an infrastructure for enforcement of
 * reference counting guarantees, eviction safety, and access restrictions
 * using ordinary object addresses.
 *
 */

#define CACHE_LINE_SIZE 64
#define CACHE_PAD(_n) char __pad ## _n [CACHE_LINE_SIZE]

/**
 * @brief The table partition
 *
 * Each tree is independent, having its own lock and generation
 * number, thus reducing thread contention.
 */

typedef struct gweakref_partition_
{
    pthread_rwlock_t lock;
    struct avltree t;
    uint64_t genctr;
    struct avltree_node **cache;
    CACHE_PAD(0);
} gweakref_partition_t;

/**
 * @brief The weakref table structure
 *
 * This is the structure corresponding to a single table of weakrefs.
 */

struct gweakref_table_
{
    CACHE_PAD(0);
    gweakref_partition_t *partition;
    CACHE_PAD(1);
    uint32_t npart;
    uint32_t cache_sz;
};

/**
 * @brief Find the correct partition for a pointer
 *
 * To lower thread contention, the table is composed of multiple
 * trees, with the tree that receives a pointer determined by a
 * modulus.  This macro yields an expression that yields a pointer to
 * the correct partition.
 */

#define gwt_partition_of_addr_k(xt, k) \
    (((xt)->partition)+(((uint64_t)k)%(xt)->npart))

/**
 * @brief Element within the AVL tree implementing weak references
 *
 * In this implementation, weak references are stored in an AVL tree
 * of (pointer, generation) pairs.  The pointer acts as the key in the
 * tree, and a lookup is successful only if the generation number
 * matches that stored in the found node.  The same pointer that
 * serves as the key serves as the value.
 */

typedef struct gweakref_priv_
{
    struct avltree_node node_k; /*< The link to the rest of the tree */
    gweakref_t k; /*< The (pointer, generation) pair */
} gweakref_priv_t;

/**
 * @brief Comparison function for weak references
 *
 * This is the comparison function for weak references necessary to
 * arrange them in an AVL tree.  It treats the stashed pointers as
 * integers and compares them numerically.
 *
 * @param lhs [in] First node
 * @param rhs [in] Second node
 *
 * @retval -1: lhs compares as less than rhs
 * @retval 0: lhs and rhs compare equal
 * @retval 1: lhs is greater than rhs
 */

static inline int wk_cmpf(const struct avltree_node *lhs,
                          const struct avltree_node *rhs)
{
    gweakref_priv_t *lk, *rk;

    lk = avltree_container_of(lhs, gweakref_priv_t, node_k);
    rk = avltree_container_of(rhs, gweakref_priv_t, node_k);

    if (lk->k.ptr < rk->k.ptr)
        return (-1);

    if (lk->k.ptr == rk->k.ptr)
        return (0);

    return (1);
}

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
static inline int
cache_offsetof(gweakref_table_t *wt, void *ptr)
{
    return ((uintptr_t) ptr % wt->cache_sz);
}

/**
 * @brief Create a weak reference table
 *
 * This function creates a new, empty weak reference table possessing
 * the specified number of partitions.  This table must be freed with
 * gweakref_destroy rather than simply deallocated.
 *
 * @param npart [in] The number of partitions for the table
 *
 * @return The address of the newly created table, NULL on failure.
 */

gweakref_table_t *gweakref_init(uint32_t npart, uint32_t cache_sz)
{
    int ix = 0;
    pthread_rwlockattr_t rwlock_attr;
    gweakref_partition_t *wp = NULL;
    gweakref_table_t *wt = NULL;

    wt = gsh_calloc(1, sizeof(gweakref_table_t));
    if (!wt)
        goto out;

    /* prior versions of Linux tirpc are subject to default prefer-reader
     * behavior (so have potential for writer starvation) */
    pthread_rwlockattr_init(&rwlock_attr);
#ifdef GLIBC
    pthread_rwlockattr_setkind_np(
        &rwlock_attr,
        PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif

    /* npart should be a small integer */
    wt->npart = npart;
    wt->partition = gsh_calloc(npart, sizeof(gweakref_partition_t));
    for (ix = 0; ix < npart; ++ix) {
        wp = &wt->partition[ix];
        pthread_rwlock_init(&wp->lock, &rwlock_attr);
        avltree_init(&wp->t, wk_cmpf, 0 /* must be 0 */);
        if (cache_sz > 0) {
            wt->cache_sz = cache_sz;
            wp->cache = gsh_calloc(cache_sz, sizeof(struct avltree_node *));
        }
        wp->genctr = 0;
    }

out:
    return (wt);
}

/**
 * @brief Insert a pointer into the weakref table
 *
 * This function inserts a pointer into the weak reference table and
 * returns a weak reference, consisting of a poitner and generation
 * number.  If the given pointer already exists within the table, a
 * weak reference consisting of the address NULL and the generation
 * number 0 is returned.
 *
 * @param wt [in] The table in which to add the pointer
 * @param obj [in] The address to insert
 *
 * @return The weak reference created.
 */

gweakref_t gweakref_insert(gweakref_table_t *wt, void *obj)
{
    gweakref_t ret;
    gweakref_priv_t *ref;
    gweakref_partition_t *wp;
    struct avltree_node *node;

    ref = gsh_calloc(1, sizeof(gweakref_priv_t));
    ref->k.ptr = obj;

    wp = (gweakref_partition_t *) gwt_partition_of_addr_k(wt, ref->k.ptr);

    /* XXX initially wt had a single atomic counter, but for any address,
     * partition is fixed, and we must take the partition lock exclusive
     * in any case */
    PTHREAD_RWLOCK_WRLOCK(&wp->lock);

    ref->k.gen = ++(wp->genctr);

    node = avltree_insert(&ref->node_k, &wp->t);
    if (! node) {
        /* success */
        ret = ref->k;
    } else {
        /* matching key existed */
        ret.ptr = NULL;
        ret.gen = 0;
    }
    PTHREAD_RWLOCK_UNLOCK(&wp->lock);

    return (ret);
}

/**
 * @brief Search the table for an entry
 *
 * This function searches the weakref table for the supplied entry.
 * If the entry is found, it is returned and the partition tree is
 * held read-locked, to be unlocked by the caller.  Otherwise NULL is
 * returned and the partition tree is unlocked.
 *
 * @param wt [in] The table to search
 * @param ref [in] The reference to search for
 * @param lock [out] if the object is found, the lock for the tree.
 *
 * @return The found object, otherwise NULL.
 */

void *gweakref_lookupex(gweakref_table_t *wt, gweakref_t *ref,
                        pthread_rwlock_t **lock)
{
    struct avltree_node *node = NULL;
    gweakref_priv_t refk, *tref;
    gweakref_partition_t *wp;
    void *ret = NULL;

    /* look up ref.ptr--return !NULL iff ref.ptr is found and
     * ref.gen == found.gen */

    refk.k = *ref;
    wp = gwt_partition_of_addr_k(wt, refk.k.ptr);
    PTHREAD_RWLOCK_RDLOCK(&wp->lock);

    /* check cache */
    if (wp->cache)
        node = wp->cache[cache_offsetof(wt, refk.k.ptr)];

    if (! node)
        node = avltree_lookup(&refk.node_k, &wp->t);

    if (node) {
        /* found it, maybe */
        tref = avltree_container_of(node, gweakref_priv_t, node_k);
        if (tref->k.gen == ref->gen) {
            ret = ref->ptr;
            if (wp->cache)
                wp->cache[cache_offsetof(wt, refk.k.ptr)] = node;
        }
    }

    if (ret) {
        *lock = &wp->lock;
    } else {
        PTHREAD_RWLOCK_UNLOCK(&wp->lock);
    }

    return (ret);
}

/**
 * @brief Wrapper around gweakref_lookupex
 *
 * This function is a wrapper around gweakref_lookupex that frees the
 * tree lock after the call.
 *
 * @param wt [in] The table to search
 * @param ref [in] The reference to search for
 *
 * @return The found object, otherwise NULL.
 */

void *gweakref_lookup(gweakref_table_t *wt, gweakref_t *ref)
{
    pthread_rwlock_t *treelock = NULL;
    void *result = NULL;

    result = gweakref_lookupex(wt, ref, &treelock);

    if (result) {
        PTHREAD_RWLOCK_UNLOCK(treelock);
    }

    return result;
}


#define GWR_FLAG_NONE    0x0000
#define GWR_FLAG_WLOCKED  0x0001

/**
 * @brief Implements deletion functionality
 *
 * This function deletes and frees the given entry from the weakref
 * table.  Nothing is done if the entry cannot be found.
 *
 * @param wt [in,out] The table from which to delete the entry
 * @param ref [in] The entry to delete
 * @param flags [in] Flags controlling behaviour:
 *                        GWR_FLAG_NONE: Null flag
 *                        GWR_FLAG_WLOCKED: Write lock already held
 */

static inline void gweakref_delete_impl(gweakref_table_t *wt, gweakref_t *ref,
                                        uint32_t flags)
{
    struct avltree_node *node;
    gweakref_priv_t refk, *tref;
    gweakref_partition_t *wp;

    /* lookup up ref.ptr, delete iff ref.ptr is found and
     * ref.gen == found.gen */

    refk.k = *ref;
    wp = gwt_partition_of_addr_k(wt, refk.k.ptr);
    if (!(flags & GWR_FLAG_WLOCKED))
        PTHREAD_RWLOCK_WRLOCK(&wp->lock);
    node = avltree_lookup(&refk.node_k, &wp->t);
    if (node) {
        /* found it, maybe */
        tref = avltree_container_of(node, gweakref_priv_t, node_k);
        /* XXX generation mismatch would be in error, we think */
        if (tref->k.gen == ref->gen) {
            /* unhook it */
            avltree_remove(node, &wp->t);
            gsh_free(tref);
            if (wp->cache)
                wp->cache[cache_offsetof(wt, refk.k.ptr)] = NULL;
        }
    }
    if (!(flags & GWR_FLAG_WLOCKED))
        PTHREAD_RWLOCK_UNLOCK(&wp->lock);
}

/**
 * @brief Delete an entry from the table
 *
 * This function deletes and frees the given entry from the weakref
 * table.
 *
 * @param wt [in,out] The table from which to delete the entry
 * @param ref [in] The entry to delete
 */

void gweakref_delete(gweakref_table_t *wt, gweakref_t *ref)
{
    gweakref_delete_impl(wt, ref, GWR_FLAG_NONE);
}

/**
 * @brief Destroy a weakref table
 *
 * This function frees all entries in a weakref table, then all
 * partition subtrees.
 *
 * @param wt [in,out] The table to be freed
 */

void gweakref_destroy(gweakref_table_t *wt)
{
    struct avltree_node *node, *onode;
    gweakref_partition_t *wp;
    gweakref_priv_t *tref;
    int ix;

    /* quiesce the server, then... */

    for (ix = 0; ix < wt->npart; ++ix) {
        wp = &wt->partition[ix];
        onode = NULL;
        node = avltree_first(&wp->t);
        do {
            if (onode) {
                tref = avltree_container_of(onode, gweakref_priv_t, node_k);
                gsh_free(tref);
            }
        } while ((onode = node) && (node = avltree_next(node)));
        if (onode) {
            tref = avltree_container_of(onode, gweakref_priv_t, node_k);
            gsh_free(tref);
        }
        avltree_init(&wp->t, wk_cmpf, 0 /* must be 0 */);
        if (wp->cache)
            gsh_free(wp->cache);
    }
    gsh_free(wt->partition);
    gsh_free(wt);
}
