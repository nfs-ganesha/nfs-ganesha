/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (C) 2010, Linux Box Corporation
 * All Rights Reserved
 *
 * Contributor: Matt Benjamin
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
 * -------------
 */

/**
 * @addtogroup FSAL_MDCACHE
 * @{
 */

/**
 * @file mdcache_avl.h
 * @author Matt Benjamin
 * @brief Definitions supporting AVL dirent representation
 *
 */

/**
 * @page AVLOverview Overview
 *
 * Definitions supporting AVL dirent representation.  The current
 * design represents dirents as a single AVL tree ordered by a
 * collision-resistent hash function (currently, Murmur3, which
 * appears to be several times faster than lookup3 on x86_64
 * architecture).  Quadratic probing is used to emulate perfect
 * hashing.  Worst case behavior is challenging to reproduce.
 * Heuristic methods are used to detect worst-case scenarios and fall
 * back to tractable (e.g., lookup) algorthims.
 *
 */

#ifndef MDCACHE_AVL_H
#define MDCACHE_AVL_H

#include "config.h"
#include "log.h"
#include "mdcache_int.h"
#include "avltree.h"

static inline int avl_dirent_name_cmpf(const struct avltree_node *lhs,
				       const struct avltree_node *rhs)
{
	mdcache_dir_entry_t *lk, *rk;

	lk = avltree_container_of(lhs, mdcache_dir_entry_t, node_name);
	rk = avltree_container_of(rhs, mdcache_dir_entry_t, node_name);

	if (lk->namehash < rk->namehash)
		return -1;

	if (lk->namehash > rk->namehash)
		return 1;

	return strcmp(lk->name, rk->name);
}

static inline int avl_dirent_ck_cmpf(const struct avltree_node *lhs,
				     const struct avltree_node *rhs)
{
	mdcache_dir_entry_t *lk, *rk;

	lk = avltree_container_of(lhs, mdcache_dir_entry_t, node_ck);
	rk = avltree_container_of(rhs, mdcache_dir_entry_t, node_ck);

	if (lk->ck < rk->ck)
		return -1;

	if (lk->ck == rk->ck)
		return 0;

	return 1;
}

static inline int avl_dirent_sorted_cmpf(const struct avltree_node *lhs,
					 const struct avltree_node *rhs)
{
	mdcache_dir_entry_t *lk, *rk;
	int rc;

	lk = avltree_container_of(lhs, mdcache_dir_entry_t, node_sorted);
	rk = avltree_container_of(rhs, mdcache_dir_entry_t, node_sorted);

	/* On create a dirent will not yet belong to a chunk, but only
	 * one of the two nodes in comparison can not belong to a chunk.
	 */
	subcall(
		if (lk->chunk != NULL)
			rc = lk->chunk->parent->sub_handle->obj_ops->dirent_cmp(
				lk->chunk->parent->sub_handle,
				lk->name, lk->ck,
				rk->name, rk->ck);
		else
			rc = rk->chunk->parent->sub_handle->obj_ops->dirent_cmp(
				rk->chunk->parent->sub_handle,
				lk->name, lk->ck,
				rk->name, rk->ck)
	       );

	return rc;
}

void mdcache_avl_remove(mdcache_entry_t *parent, mdcache_dir_entry_t *dirent);
void avl_dirent_set_deleted(mdcache_entry_t *entry, mdcache_dir_entry_t *v);
void mdcache_avl_init(mdcache_entry_t *entry);
int mdcache_avl_insert(mdcache_entry_t *entry, mdcache_dir_entry_t **dirent);
int mdcache_avl_insert_ck(mdcache_entry_t *entry, mdcache_dir_entry_t *v);

bool mdcache_avl_lookup_ck(mdcache_entry_t *entry, uint64_t ck,
			   mdcache_dir_entry_t **dirent);
mdcache_dir_entry_t *mdcache_avl_lookup(mdcache_entry_t *entry,
					const char *name);
void mdcache_avl_clean_trees(mdcache_entry_t *parent);

void unchunk_dirent(mdcache_dir_entry_t *dirent);
#endif				/* MDCACHE_AVL_H */

/** @} */
