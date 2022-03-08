/* SPDX-License-Identifier: GPL-2.0-or-later WITH special exception */
/**
 * @addtogroup hashtable
 * @{
 */

/**
 * @file rbt_node.h
 * @brief Red-black tree node structure
 */

/*
 * Implementation of RedBlack trees
 * Definitions and structures
 */

/*
 * This implementation of RedBlack trees was copied from
 * the STL library and adapted to a C environment.
 */

/*
 * RB tree implementation -*- C++ -*-

 * Copyright (C) 2001 Free Software Foundation, Inc.
 *
 * This file is part of the GNU ISO C++ Library.  This library is free
 * software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option)
 * any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this library; see the file COPYING.  If not, write to the Free
 * Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.

 * As a special exception, you may use this file as part of a free software
 * library without restriction.  Specifically, if other files instantiate
 * templates or use macros or inline functions from this file, or you compile
 * this file and link it with other files to produce an executable, this
 * file does not by itself cause the resulting executable to be covered by
 * the GNU General Public License.  This exception does not however
 * invalidate any other reasons why the executable file might be covered by
 * the GNU General Public License.
 */

/*
 *
 * Copyright (c) 1996,1997
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 */

/* NOTE: This is an internal header file, included by other STL headers.
 *   You should not attempt to use it directly.
 */

/*

Red-black tree class, designed for use in implementing STL
associative containers (set, multiset, map, and multimap). The
insertion and deletion algorithms are based on those in Cormen,
Leiserson, and Rivest, Introduction to Algorithms (MIT Press, 1990),
except that

(1) the header cell is maintained with links not only to the root
but also to the leftmost node of the tree, to enable constant time
begin(), and to the rightmost node of the tree, to enable linear time
performance when used with the generic set algorithms (set_union,
etc.);

(2) when a node being deleted has two children its successor node is
relinked into its place, rather than copied, so that the only
iterators invalidated are those referring to the deleted node.

*/

#ifndef RBT_NODE_H
#define RBT_NODE_H

#include <stdint.h>

/*
 * allocation parameters
 */
#define RBT_NUM 16 /* allocate nodes RBT_NUM at a time */

/*
 * rbt_head is the head structure.
 * There is one rbt_head structure per tree.
 */
struct rbt_head {
	struct rbt_node *root; /* root node       */
	struct rbt_node *leftmost; /* leftmost node   */
	struct rbt_node *rightmost; /* rightmost nodei */
	unsigned int rbt_num_node; /* number of nodes */
};

/*
 * rbt_node is the node structure.
 * There is one rbt_tree structure per node.
 *
 * Usually, rbt_node is part of a bigger structure.
 * In this case, rbt_opaq point to this bigger structure.
 *
 * The field anchor is never NULL. It points to a pointer
 * containing the rbt_node address. This pointer is either
 * - the field left in the parent node
 * - the field next in the parent node
 * - the field root in the rbt_head structure (for the root node)
 */

typedef struct rbt_node {
	unsigned int rbt_flags;
	struct rbt_node **anchor; /* anchor for this node */
	struct rbt_node *parent; /* parent node or NULL for root */
	struct rbt_node *left; /* left node */
	struct rbt_node *next; /* "right" node */
	uint64_t rbt_value; /* used for order */
	void *rbt_opaq; /* pointer for external object */
} rbt_node_t;

/*
 * flags for rbt_node
 */
#define RBT_RED 01	/* red node */

#endif /* RBT_NODE_H */

/** @} */
