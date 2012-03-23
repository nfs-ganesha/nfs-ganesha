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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "stuff_alloc.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "murmur3.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

void cache_inode_avl_init(cache_entry_t *entry)
{
    avltree_init(&entry->object.dir.avl, avl_dirent_hk_cmpf, 0 /* flags */);
}

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
int cache_inode_avl_qp_insert(
    cache_entry_t *entry, cache_inode_dir_entry_t *v)
{
    struct avltree *t = &entry->object.dir.avl;
    struct avltree_node *node;
    uint32_t hk[4];
    int j, j2;

    assert(avltree_size(t) < UINT64_MAX);

    MurmurHash3_x64_128(v->name.name,  FSAL_MAX_NAME_LEN, 67, hk);
    memcpy(&v->hk.k, hk, 8);

    for (j = 0; j < UINT64_MAX; j++) {
        v->hk.k = (v->hk.k + (j * 2));
        node = avltree_insert(&v->node_hk, t);
        if (! node) {
            /* success, note iterations and return */
            v->hk.p = j;
            if (entry->object.dir.collisions < j)
                entry->object.dir.collisions = j;
            return (0);
        }
    }
    
    LogCrit(COMPONENT_CACHE_INODE,
            "cache_inode_avl_qp_insert_s: could not insert at j=%d (%s)\n",
            j, v->name.name);

    memcpy(&v->hk.k, hk, 8);
    for (j2 = 1 /* tried j=0 */; j2 < UINT64_MAX; j2++) {
        v->hk.k = v->hk.k + j2;
        node = avltree_insert(&v->node_hk, t);
        if (! node) {
            /* success, note iterations and return */
            v->hk.p = j + j2;
            if (entry->object.dir.collisions < v->hk.p)
                entry->object.dir.collisions = v->hk.p;
            return (0);
        }
        j2++;
    }

    LogCrit(COMPONENT_CACHE_INODE,
            "cache_inode_avl_qp_insert_s: could not insert at j=%d (%s)\n",
            j, v->name.name);

    return (-1);
}

static inline struct avltree_node *
avltree_inline_lookup(
    const struct avltree_node *key,
    const struct avltree *tree)
{
    struct avltree_node *node = tree->root;
    int is_left = 0, res = 0;

    while (node) {
        res = avl_dirent_hk_cmpf(node, key);
        if (res == 0)
            return node;
        if ((is_left = res > 0))
            node = node->left;
        else
            node = node->right;
    }
    return NULL;
}

cache_inode_dir_entry_t *
cache_inode_avl_lookup_k(
    cache_entry_t *entry, cache_inode_dir_entry_t *v)
{
    struct avltree *t = &entry->object.dir.avl;
    struct avltree_node *node;

    node = avltree_inline_lookup(&v->node_hk, t);
    if (node)
        return (avltree_container_of(node, cache_inode_dir_entry_t, node_hk));
    else
        return (NULL);
}

cache_inode_dir_entry_t *
cache_inode_avl_qp_lookup_s(
    cache_entry_t *entry, cache_inode_dir_entry_t *v, int maxj)
{
    struct avltree *t = &entry->object.dir.avl;
    struct avltree_node *node;
    cache_inode_dir_entry_t *v2;
    uint32_t hk[4];
    int j;

    MurmurHash3_x64_128(v->name.name,  FSAL_MAX_NAME_LEN, 67, hk);
    memcpy(&v->hk.k, hk, 8);

    for (j = 0; j < maxj; j++) {
        v->hk.k = (v->hk.k + (j * 2));
        node = avltree_inline_lookup(&v->node_hk, t);
        if (node) {
            /* ensure that node is related to v */
            v2 = avltree_container_of(node, cache_inode_dir_entry_t, node_hk);
            if (! FSAL_namecmp(&v->name, &v2->name)) {
                return (v2);
            }
        }
    }

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "cache_inode_avl_qp_lookup_s: entry not found at j=%d (%s)\n",
                 j, v->name.name);

    node = avltree_first(t);
    while (node) {
        v2 = avltree_container_of(node, cache_inode_dir_entry_t, node_hk);
        if (! FSAL_namecmp(&v->name, &v2->name))
            return (v2);
        else
            node = avltree_next(node);
    }

    return (NULL);
}
