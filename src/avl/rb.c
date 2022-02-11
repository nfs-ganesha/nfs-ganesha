// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * rbtree - Implements a red-black tree with parent pointers.
 *
 * Copyright (C) 2010-2014 Franck Bui-Huu <fbuihuu@gmail.com>
 *
 * This file is part of libtree which is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Lesser
 * General Public License as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the LICENSE file for license rights and limitations.
 */

/*
 * For recall a red-black tree has the following properties:
 *
 *     1. All nodes are either BLACK or RED
 *     2. Leafs are BLACK
 *     3. A RED node has BLACK children only
 *     4. Path from a node to any leafs has the same number of BLACK nodes.
 *
 */
#include "avltree.h"

/*
 * Some helpers
 */
#ifdef UINTPTR_MAX

static inline enum rb_color get_color(const struct rbtree_node *node)
{
	return node->parent & 1;
}

static inline void set_color(enum rb_color color, struct rbtree_node *node)
{
	node->parent = (node->parent & ~1UL) | color;
}

static inline struct rbtree_node *get_parent(const struct rbtree_node *node)
{
	return (struct rbtree_node *)(node->parent & ~1UL);
}

static inline void set_parent(struct rbtree_node *parent,
			      struct rbtree_node *node)
{
	node->parent = (uintptr_t) parent | (node->parent & 1);
}

#else

static inline enum rb_color get_color(const struct rbtree_node *node)
{
	return node->color;
}

static inline void set_color(enum rb_color color, struct rbtree_node *node)
{
	node->color = color;
}

static inline struct rbtree_node *get_parent(const struct rbtree_node *node)
{
	return node->parent;
}

static inline void set_parent(struct rbtree_node *parent,
			      struct rbtree_node *node)
{
	node->parent = parent;
}

#endif				/* UINTPTR_MAX */

static inline int is_root(struct rbtree_node *node)
{
	return get_parent(node) == NULL;
}

static inline int is_black(struct rbtree_node *node)
{
	return get_color(node) == RB_BLACK;
}

static inline int is_red(struct rbtree_node *node)
{
	return !is_black(node);
}

/*
 * Iterators
 */
static inline struct rbtree_node *get_first(struct rbtree_node *node)
{
	while (node->left)
		node = node->left;
	return node;
}

static inline struct rbtree_node *get_last(struct rbtree_node *node)
{
	while (node->right)
		node = node->right;
	return node;
}

struct rbtree_node *rbtree_first(const struct rbtree *tree)
{
	return tree->first;
}

struct rbtree_node *rbtree_last(const struct rbtree *tree)
{
	return tree->last;
}

struct rbtree_node *rbtree_next(const struct rbtree_node *node)
{
	struct rbtree_node *parent;

	if (node->right)
		return get_first(node->right);

	while ((parent = get_parent(node)) && parent->right == node)
		node = parent;
	return parent;
}

struct rbtree_node *rbtree_prev(const struct rbtree_node *node)
{
	struct rbtree_node *parent;

	if (node->left)
		return get_last(node->left);

	while ((parent = get_parent(node)) && parent->left == node)
		node = parent;
	return parent;
}

/*
 * 'pparent' and 'is_left' are only used for insertions. Normally GCC
 * will notice this and get rid of them for lookups.
 */
static inline struct rbtree_node *do_lookup(const struct rbtree_node *key,
					    const struct rbtree *tree,
					    struct rbtree_node **pparent,
					    int *is_left)
{
	struct rbtree_node *node = tree->root;

	*pparent = NULL;
	*is_left = 0;

	while (node) {
		int res = tree->cmp_fn(node, key);
		if (res == 0)
			return node;
		*pparent = node;
		if ((*is_left = res > 0))
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

/*
 * Rotate operations (They preserve the binary search tree property,
 * assuming that the keys are unique).
 */
static void rotate_left(struct rbtree_node *node, struct rbtree *tree)
{
	struct rbtree_node *p = node;
	struct rbtree_node *q = node->right;	/* can't be NULL */
	struct rbtree_node *parent = get_parent(p);

	if (!is_root(p)) {
		if (parent->left == p)
			parent->left = q;
		else
			parent->right = q;
	} else
		tree->root = q;
	set_parent(parent, q);
	set_parent(q, p);

	p->right = q->left;
	if (p->right)
		set_parent(p, p->right);
	q->left = p;
}

static void rotate_right(struct rbtree_node *node, struct rbtree *tree)
{
	struct rbtree_node *p = node;
	struct rbtree_node *q = node->left;	/* can't be NULL */
	struct rbtree_node *parent = get_parent(p);

	if (!is_root(p)) {
		if (parent->left == p)
			parent->left = q;
		else
			parent->right = q;
	} else
		tree->root = q;
	set_parent(parent, q);
	set_parent(q, p);

	p->left = q->right;
	if (p->left)
		set_parent(p, p->left);
	q->right = p;
}

struct rbtree_node *rbtree_lookup(const struct rbtree_node *key,
				  const struct rbtree *tree)
{
	struct rbtree_node *parent;
	int is_left;

	return do_lookup(key, tree, &parent, &is_left);
}

static void set_child(struct rbtree_node *child, struct rbtree_node *node,
		      int left)
{
	if (left)
		node->left = child;
	else
		node->right = child;
}

struct rbtree_node *rbtree_insert(struct rbtree_node *node, struct rbtree *tree)
{
	struct rbtree_node *key, *parent;
	int is_left;

	key = do_lookup(node, tree, &parent, &is_left);
	if (key)
		return key;

	node->left = NULL;
	node->right = NULL;
	set_color(RB_RED, node);
	set_parent(parent, node);

	if (parent) {
		if (is_left) {
			if (parent == tree->first)
				tree->first = node;
		} else {
			if (parent == tree->last)
				tree->last = node;
		}
		set_child(node, parent, is_left);
	} else {
		tree->root = node;
		tree->first = node;
		tree->last = node;
	}

	/*
	 * Fixup the modified tree by recoloring nodes and performing
	 * rotations (2 at most) hence the red-black tree properties are
	 * preserved.
	 */
	while ((parent = get_parent(node)) && is_red(parent)) {
		struct rbtree_node *grandpa = get_parent(parent);

		if (parent == grandpa->left) {
			struct rbtree_node *uncle = grandpa->right;

			if (uncle && is_red(uncle)) {
				set_color(RB_BLACK, parent);
				set_color(RB_BLACK, uncle);
				set_color(RB_RED, grandpa);
				node = grandpa;
			} else {
				if (node == parent->right) {
					rotate_left(parent, tree);
					node = parent;
					parent = get_parent(node);
				}
				set_color(RB_BLACK, parent);
				set_color(RB_RED, grandpa);
				rotate_right(grandpa, tree);
			}
		} else {
			struct rbtree_node *uncle = grandpa->left;

			if (uncle && is_red(uncle)) {
				set_color(RB_BLACK, parent);
				set_color(RB_BLACK, uncle);
				set_color(RB_RED, grandpa);
				node = grandpa;
			} else {
				if (node == parent->left) {
					rotate_right(parent, tree);
					node = parent;
					parent = get_parent(node);
				}
				set_color(RB_BLACK, parent);
				set_color(RB_RED, grandpa);
				rotate_left(grandpa, tree);
			}
		}
	}
	set_color(RB_BLACK, tree->root);
	return NULL;
}

void rbtree_remove(struct rbtree_node *node, struct rbtree *tree)
{
	struct rbtree_node *parent = get_parent(node);
	struct rbtree_node *left = node->left;
	struct rbtree_node *right = node->right;
	struct rbtree_node *next;
	enum rb_color color;

	if (node == tree->first)
		tree->first = rbtree_next(node);
	if (node == tree->last)
		tree->last = rbtree_prev(node);

	if (!left)
		next = right;
	else if (!right)
		next = left;
	else
		next = get_first(right);

	if (parent)
		set_child(next, parent, parent->left == node);
	else
		tree->root = next;

	if (left && right) {
		color = get_color(next);
		set_color(get_color(node), next);

		next->left = left;
		set_parent(next, left);

		if (next != right) {
			parent = get_parent(next);
			set_parent(get_parent(node), next);

			node = next->right;
			parent->left = node;

			next->right = right;
			set_parent(next, right);
		} else {
			set_parent(parent, next);
			parent = next;
			node = next->right;
		}
	} else {
		color = get_color(node);
		node = next;
	}
	/*
	 * 'node' is now the sole successor's child and 'parent' its
	 * new parent (since the successor can have been moved).
	 */
	if (node)
		set_parent(parent, node);

	/*
	 * The 'easy' cases.
	 */
	if (color == RB_RED)
		return;
	if (node && is_red(node)) {
		set_color(RB_BLACK, node);
		return;
	}

	do {
		if (node == tree->root)
			break;

		if (node == parent->left) {
			struct rbtree_node *sibling = parent->right;

			if (is_red(sibling)) {
				set_color(RB_BLACK, sibling);
				set_color(RB_RED, parent);
				rotate_left(parent, tree);
				sibling = parent->right;
			}
			if ((!sibling->left || is_black(sibling->left))
			    && (!sibling->right || is_black(sibling->right))) {
				set_color(RB_RED, sibling);
				node = parent;
				parent = get_parent(parent);
				continue;
			}
			if (!sibling->right || is_black(sibling->right)) {
				set_color(RB_BLACK, sibling->left);
				set_color(RB_RED, sibling);
				rotate_right(sibling, tree);
				sibling = parent->right;
			}
			set_color(get_color(parent), sibling);
			set_color(RB_BLACK, parent);
			set_color(RB_BLACK, sibling->right);
			rotate_left(parent, tree);
			node = tree->root;
			break;
		} else {
			struct rbtree_node *sibling = parent->left;

			if (is_red(sibling)) {
				set_color(RB_BLACK, sibling);
				set_color(RB_RED, parent);
				rotate_right(parent, tree);
				sibling = parent->left;
			}
			if ((!sibling->left || is_black(sibling->left))
			    && (!sibling->right || is_black(sibling->right))) {
				set_color(RB_RED, sibling);
				node = parent;
				parent = get_parent(parent);
				continue;
			}
			if (!sibling->left || is_black(sibling->left)) {
				set_color(RB_BLACK, sibling->right);
				set_color(RB_RED, sibling);
				rotate_left(sibling, tree);
				sibling = parent->left;
			}
			set_color(get_color(parent), sibling);
			set_color(RB_BLACK, parent);
			set_color(RB_BLACK, sibling->left);
			rotate_right(parent, tree);
			node = tree->root;
			break;
		}
	} while (is_black(node));

	if (node)
		set_color(RB_BLACK, node);
}

void rbtree_replace(struct rbtree_node *old, struct rbtree_node *new,
		    struct rbtree *tree)
{
	struct rbtree_node *parent = get_parent(old);

	if (parent)
		set_child(new, parent, parent->left == old);
	else
		tree->root = new;

	if (old->left)
		set_parent(new, old->left);
	if (old->right)
		set_parent(new, old->right);

	if (tree->first == old)
		tree->first = new;
	if (tree->last == old)
		tree->last = new;

	*new = *old;
}

int rbtree_init(struct rbtree *tree, rbtree_cmp_fn_t fn, unsigned long flags)
{
	if (flags)
		return -1;
	tree->root = NULL;
	tree->cmp_fn = fn;
	tree->first = NULL;
	tree->last = NULL;
	return 0;
}
