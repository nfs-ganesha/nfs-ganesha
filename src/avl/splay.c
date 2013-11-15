/*
 * splaytree - Implements a top-down threaded splay tree.
 *
 * Copyright (C) 2010 Franck Bui-Huu <fbuihuu@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include "config.h"
#include <assert.h>

#include "avltree.h"

#ifdef UINTPTR_MAX

#define NODE_INIT	{ 0, }

static inline void INIT_NODE(struct splaytree_node *node)
{
	node->left = 0;
	node->right = 0;
}

static inline void set_thread(struct splaytree_node *t, uintptr_t * p)
{
	*p = (uintptr_t) t | 1;
}

static inline struct splaytree_node *get_thread(uintptr_t u)
{
	return (struct splaytree_node *)((u & -(int)(u & 1)) & ~1UL);
}

static inline void set_link(struct splaytree_node *n, uintptr_t * p)
{
	*p = (uintptr_t) n;
}

static inline struct splaytree_node *get_link(uintptr_t u)
{
	return (struct splaytree_node *)(u & ((int)(u & 1) - 1));
}

#define set_left(l,n)	set_link(l, &(n)->left)
#define set_right(r,n)	set_link(r, &(n)->right)
#define set_prev(p,n)	set_thread(p, &(n)->left)
#define set_next(s,n)	set_thread(s, &(n)->right)

#define get_left(n)	get_link((n)->left)
#define get_right(n)	get_link((n)->right)
#define get_prev(n)	get_thread((n)->left)
#define get_next(n)	get_thread((n)->right)

#else				/* !UINTPTR_MAX */

#define NODE_INIT	{ NULL, }

static inline void INIT_NODE(struct splaytree_node *node)
{
	node->left = NULL;
	node->right = NULL;
	node->left_is_thread = 0;
	node->right_is_thread = 0;
}

static inline void set_left(struct splaytree_node *l, struct splaytree_node *n)
{
	n->left = l;
	n->left_is_thread = 0;
}

static inline void set_right(struct splaytree_node *r, struct splaytree_node *n)
{
	n->right = r;
	n->right_is_thread = 0;
}

static inline void set_prev(struct splaytree_node *t, struct splaytree_node *n)
{
	n->left = t;
	n->left_is_thread = 1;
}

static inline void set_next(struct splaytree_node *t, struct splaytree_node *n)
{
	n->right = t;
	n->right_is_thread = 1;
}

static inline struct splaytree_node *get_left(const struct splaytree_node *n)
{
	if (!n->left_is_thread)
		return n->left;
	return NULL;
}

static inline struct splaytree_node *get_right(const struct splaytree_node *n)
{
	if (!n->right_is_thread)
		return n->right;
	return NULL;
}

static inline struct splaytree_node *get_prev(const struct splaytree_node *n)
{
	if (n->left_is_thread)
		return n->left;
	return NULL;
}

static inline struct splaytree_node *get_next(const struct splaytree_node *n)
{
	if (n->right_is_thread)
		return n->right;
	return NULL;
}

#endif				/* UINTPTR_MAX */

/*
 * Iterators
 */
static inline struct splaytree_node *get_first(struct splaytree_node *node)
{
	struct splaytree_node *left;
	while ((left = get_left(node)))
		node = left;
	return node;
}

static inline struct splaytree_node *get_last(struct splaytree_node *node)
{
	struct splaytree_node *right;
	while ((right = get_right(node)))
		node = right;
	return node;
}

struct splaytree_node *splaytree_first(const struct splaytree *tree)
{
	return tree->first;
}

struct splaytree_node *splaytree_last(const struct splaytree *tree)
{
	return tree->last;
}

struct splaytree_node *splaytree_next(const struct splaytree_node *node)
{
	struct splaytree_node *right = get_right(node);
	if (right)
		return get_first(right);
	return get_next(node);
}

struct splaytree_node *splaytree_prev(const struct splaytree_node *node)
{
	struct splaytree_node *left = get_left(node);
	if (left)
		return get_last(left);
	return get_prev(node);
}

static inline void rotate_right(struct splaytree_node *node)
{
	struct splaytree_node *left = get_left(node);	/* can't be NULL */
	struct splaytree_node *r = get_right(left);

	if (r)
		set_left(r, node);
	else
		set_prev(left, node);
	set_right(node, left);
}

static inline void rotate_left(struct splaytree_node *node)
{
	struct splaytree_node *right = get_right(node);	/* can't be NULL */
	struct splaytree_node *l = get_left(right);

	if (l)
		set_right(l, node);
	else
		set_next(right, node);
	set_left(node, right);
}

static int do_splay(const struct splaytree_node *key, struct splaytree *tree)
{
	struct splaytree_node subroots = NODE_INIT;
	struct splaytree_node *subleft = &subroots, *subright = &subroots;
	struct splaytree_node *root = tree->root;
	splaytree_cmp_fn_t cmp = tree->cmp_fn;
	int rv;

	for (;;) {
		rv = cmp(key, root);
		if (rv == 0)
			break;
		if (rv < 0) {
			struct splaytree_node *left;

			left = get_left(root);
			if (!left)
				break;
			if ((rv = cmp(key, left)) < 0) {
				rotate_right(root);
				root = left;
				left = get_left(root);
				if (!left)
					break;
			}
			/* link left */
			set_left(root, subright);
			subright = root;
			root = left;
		} else {
			struct splaytree_node *right;

			right = get_right(root);
			if (!right)
				break;
			if ((rv = cmp(key, right)) > 0) {
				rotate_left(root);
				root = right;
				right = get_right(root);
				if (!right)
					break;
			}
			/* link right */
			set_right(root, subleft);
			subleft = root;
			root = right;
		}
	}
	/* assemble */
	if (get_left(root))
		set_right(get_left(root), subleft);
	else
		set_next(root, subleft);

	if (get_right(root))
		set_left(get_right(root), subright);
	else
		set_prev(root, subright);

	set_left(get_right(&subroots), root);
	set_right(get_left(&subroots), root);
	tree->root = root;
	return rv;
}

struct splaytree_node *splaytree_lookup(const struct splaytree_node *key,
					struct splaytree *tree)
{
	if (!tree->root)
		return NULL;
	if (do_splay(key, tree) != 0)
		return NULL;
	return tree->root;
}

struct splaytree_node *splaytree_insert(struct splaytree_node *node,
					struct splaytree *tree)
{
	struct splaytree_node *root = tree->root;
	int res;

	if (!root) {
		INIT_NODE(node);
		tree->root = node;
		tree->first = node;
		tree->last = node;
		return NULL;
	}

	res = do_splay(node, tree);
	if (res == 0)
		return tree->root;

	root = tree->root;
	if (res < 0) {
		struct splaytree_node *left = get_left(root);

		set_left(left, node);
		set_right(root, node);
		if (left)
			set_next(node, get_last(left));
		else
			tree->first = node;
		set_prev(node, root);
	} else {
		struct splaytree_node *right = get_right(root);

		set_right(right, node);
		set_left(root, node);
		if (right)
			set_prev(node, get_first(right));
		else
			tree->last = node;
		set_next(node, root);
	}
	tree->root = node;
	return NULL;
}

void splaytree_remove(struct splaytree_node *node, struct splaytree *tree)
{
	struct splaytree_node *right, *left, *prev;

	do_splay(node, tree);
	assert(tree->root == node);	/* 'node' must be present */

	right = get_right(node);
	left = get_left(node);
	if (!left) {
		tree->root = right;
		tree->first = splaytree_next(node);
		prev = NULL;
	} else {
		tree->root = left;
		do_splay(node, tree);
		set_right(right, tree->root);
		prev = tree->root;
	}
	if (right)
		set_prev(prev, get_first(right));
	else
		tree->last = prev;
}

void splaytree_replace(struct splaytree_node *old, struct splaytree_node *new,
		       struct splaytree *tree)
{
	do_splay(old, tree);
	assert(tree->root == old);

	tree->root = new;
	if (tree->first == old)
		tree->first = new;
	if (tree->last == old)
		tree->last = new;

	*new = *old;
}

int splaytree_init(struct splaytree *tree, splaytree_cmp_fn_t cmp,
		   unsigned long flags)
{
	if (flags)
		return -1;
	tree->root = NULL;
	tree->first = NULL;
	tree->last = NULL;
	tree->cmp_fn = cmp;
	return 0;
}
