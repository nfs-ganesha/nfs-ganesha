/*
 * avltree - Implements an AVL tree with parent pointers.
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
#include <assert.h>

#include "avltree.h"


#if !defined UINTPTR_MAX || UINTPTR_MAX != UINT64_MAX

static inline int is_root(struct avltree_node *node)
{
	return node->parent == NULL;
}

static inline void INIT_NODE(struct avltree_node *node)
{
	node->left = NULL;
	node->right = NULL;
	node->parent = NULL;
	node->balance = 0;
}

static inline signed get_balance(struct avltree_node *node)
{
	return node->balance;
}

static inline void set_balance(int balance, struct avltree_node *node)
{
	node->balance = balance;
}

static inline int inc_balance(struct avltree_node *node)
{
	return ++node->balance;
}

static inline int dec_balance(struct avltree_node *node)
{
	return --node->balance;
}

static inline struct avltree_node *get_parent(const struct avltree_node *node)
{
	return node->parent;
}

static inline void set_parent(struct avltree_node *parent,
			      struct avltree_node *node)
{
	node->parent = parent;
}

#else

static inline int is_root(struct avltree_node *node)
{
	return !(node->parent & ~7UL);
}

static inline void INIT_NODE(struct avltree_node *node)
{
	node->left = NULL;
	node->right = NULL;
	node->parent = 2;
}

static inline signed get_balance(struct avltree_node *node)
{
	return (int)(node->parent & 7) - 2;
}

static inline void set_balance(int balance, struct avltree_node *node)
{
	node->parent = (node->parent & ~7UL) | (balance + 2);
}

static inline int inc_balance(struct avltree_node *node)
{
	return (int)(++node->parent & 7) - 2;
}

static inline int dec_balance(struct avltree_node *node)
{
	return (int)(--node->parent & 7) - 2;
}

static inline struct avltree_node *get_parent(const struct avltree_node *node)
{
	return (struct avltree_node *)(node->parent & ~7UL);
}

static inline void set_parent(const struct avltree_node *parent,
			      struct avltree_node *node)
{
	node->parent = (uintptr_t)parent | (node->parent & 7);
}

#endif

/*
 * Iterators
 */
static inline struct avltree_node *get_first(struct avltree_node *node)
{
	while (node->left)
		node = node->left;
	return node;
}

static inline struct avltree_node *get_last(struct avltree_node *node)
{
	while (node->right)
		node = node->right;
	return node;
}

struct avltree_node *avltree_first(const struct avltree *tree)
{
	return tree->first;
}

struct avltree_node *avltree_last(const struct avltree *tree)
{
	return tree->last;
}

struct avltree_node *avltree_next(const struct avltree_node *node)
{
	struct avltree_node *parent;

	if (node->right)
		return get_first(node->right);

	while ((parent = get_parent(node)) && parent->right == node)
		node = parent;
	return parent;
}

struct avltree_node *avltree_prev(const struct avltree_node *node)
{
	struct avltree_node *parent;

	if (node->left)
		return get_last(node->left);

	while ((parent = get_parent(node)) && parent->left == node)
		node = parent;
	return parent;
}

uint64_t avltree_size(const struct avltree *tree)
{
    return tree->size;
}

/*
 * The AVL tree is more rigidly balanced than Red-Black trees, leading
 * to slower insertion and removal but faster retrieval.
 */

/* node->balance = height(node->right) - height(node->left); */
static void rotate_left(struct avltree_node *node, struct avltree *tree)
{
	struct avltree_node *p = node;
	struct avltree_node *q = node->right; /* can't be NULL */
	struct avltree_node *parent = get_parent(p);

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

static void rotate_right(struct avltree_node *node, struct avltree *tree)
{
	struct avltree_node *p = node;
	struct avltree_node *q = node->left; /* can't be NULL */
	struct avltree_node *parent = get_parent(p);

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

/*
 * 'pparent', 'unbalanced' and 'is_left' are only used for
 * insertions. Normally GCC will notice this and get rid of them for
 * lookups.
 */
static inline struct avltree_node *do_lookup(const struct avltree_node *key,
					     const struct avltree *tree,
					     struct avltree_node **pparent,
					     struct avltree_node **unbalanced,
					     int *is_left)
{
	struct avltree_node *node = tree->root;
	int res = 0;

	*pparent = NULL;
	*unbalanced = node;
	*is_left = 0;

	while (node) {
		if (get_balance(node) != 0)
			*unbalanced = node;

		res = tree->cmp_fn(node, key);
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

struct avltree_node *avltree_lookup(const struct avltree_node *key,
				    const struct avltree *tree)
{
	struct avltree_node *parent, *unbalanced;
	int is_left;

	return do_lookup(key, tree, &parent, &unbalanced, &is_left);
}

struct avltree_node *avltree_inf(const struct avltree_node *key,
				 const struct avltree *tree,
				 struct avltree_node **glb)
{
    struct avltree_node *parent, *lb, *unbalanced;
    struct avltree_node *node = tree->root;
    int is_left = 0;
    int res = 0;

    lb = avltree_first(tree); /* at worst, the first entry */
    unbalanced = node;
    parent = NULL;
    is_left = 0;

    while (node) {
	if (get_balance(node) != 0)
	    unbalanced = node;
	res = tree->cmp_fn(node, key);
	if (res == 0)
	    return node;
	else if (res < 1) /* lb is less than key */
	    lb = node;
	parent = node;
	if ((is_left = res > 0))
	    node = node->left;
	else
	    node = node->right;
    } /* while */

    /* pass up the glb */
    if (lb)
	*glb = lb;

    return NULL;
}

static void set_child(struct avltree_node *child,
		      struct avltree_node *node, int left)
{
	if (left)
		node->left = child;
	else
		node->right = child;
}

/* Insertion never needs more than 2 rotations */
struct avltree_node *avltree_insert(struct avltree_node *node, struct avltree *tree)
{
	struct avltree_node *key, *parent, *unbalanced;
	int is_left;

	key = do_lookup(node, tree, &parent, &unbalanced, &is_left);
	if (key)
		return key;

	INIT_NODE(node);

	if (!parent) {
		tree->root = node;
		tree->first = tree->last = node;
		tree->height++;
		tree->size++;
		return NULL;
	}
	if (is_left) {
		if (parent == tree->first)
			tree->first = node;
	} else {
		if (parent == tree->last)
			tree->last = node;
	}
	set_parent(parent, node);
	set_child(node, parent, is_left);

	for (;;) {
		if (parent->left == node)
			dec_balance(parent);
		else
			inc_balance(parent);

		if (parent == unbalanced)
			break;
		node = parent;
		parent = get_parent(parent);
	}

	/* inserted */
	tree->size++;

	switch (get_balance(unbalanced)) {
	case  1: case -1:
		tree->height++;
		/* fall through */
	case 0:
		break;
	case 2: {
		struct avltree_node *right = unbalanced->right;

		if (get_balance(right) == 1) {
			set_balance(0, unbalanced);
			set_balance(0, right);
		} else {
			switch (get_balance(right->left)) {
			case 1:
				set_balance(-1, unbalanced);
				set_balance( 0, right);
				break;
			case 0:
				set_balance(0, unbalanced);
				set_balance(0, right);
				break;
			case -1:
				set_balance(0, unbalanced);
				set_balance(1, right);
				break;
			}
			set_balance(0, right->left);

			rotate_right(right, tree);
		}
		rotate_left(unbalanced, tree);
		break;
	}
	case -2: {
		struct avltree_node *left = unbalanced->left;

		if (get_balance(left) == -1) {
			set_balance(0, unbalanced);
			set_balance(0, left);
		} else {
			switch (get_balance(left->right)) {
			case 1:
				set_balance( 0, unbalanced);
				set_balance(-1, left);
				break;
			case 0:
				set_balance(0, unbalanced);
				set_balance(0, left);
				break;
			case -1:
				set_balance(1, unbalanced);
				set_balance(0, left);
				break;
			}
			set_balance(0, left->right);

			rotate_left(left, tree);
		}
		rotate_right(unbalanced, tree);
		break;
	}
	}
	return NULL;
}

/* Deletion might require up to log(n) rotations */
void avltree_remove(struct avltree_node *node, struct avltree *tree)
{
	struct avltree_node *parent = get_parent(node);
	struct avltree_node *left = node->left;
	struct avltree_node *right = node->right;
	struct avltree_node *next;
	int is_left = is_left;

	if (node == tree->first)
		tree->first = avltree_next(node);
	if (node == tree->last)
		tree->last = avltree_prev(node);

	if (!left)
		next = right;
	else if (!right)
		next = left;
	else
		next = get_first(right);

	if (parent) {
		is_left = parent->left == node;
		set_child(next, parent, is_left);
	} else
		tree->root = next;

	if (left && right) {
		set_balance(get_balance(node), next);

		next->left = left;
		set_parent(next, left);

		if (next != right) {
			parent = get_parent(next);
			set_parent(get_parent(node), next);

			node = next->right;
			parent->left = node;
			is_left = 1;

			next->right = right;
			set_parent(next, right);
		} else {
			set_parent(parent, next);
			parent = next;
			node = parent->right;
			is_left = 0;
		}
		assert(parent != NULL);
	} else
		node = next;

	if (node)
		set_parent(parent, node);

	/* removed */
	tree->size--;

	/*
	 * At this point, 'parent' can only be null, if 'node' is the
	 * tree's root and has at most one child.
	 *
	 * case 1: the subtree is now balanced but its height has
	 * decreased.
	 *
	 * case 2: the subtree is mostly balanced and its height is
	 * unchanged.
	 *
	 * case 3: the subtree is unbalanced and its height may have
	 * been changed during the rebalancing process, see below.
	 *
	 * case 3.1: after a left rotation, the subtree becomes mostly
	 * balanced and its height is unchanged.
	 *
	 * case 3.2: after a left rotation, the subtree becomes
	 * balanced but its height has decreased.
	 *
	 * case 3.3: after a left and a right rotation, the subtree
	 * becomes balanced or mostly balanced but its height has
	 * decreased for all cases.
	 */
	while (parent) {
		int balance;
		node   = parent;
		parent = get_parent(parent);

		if (is_left) {
			is_left = parent && parent->left == node;

			balance = inc_balance(node);
			if (balance == 0)		/* case 1 */
				continue;
			if (balance == 1)		/* case 2 */
				return;
			right = node->right;		/* case 3 */
			switch (get_balance(right)) {
			case 0:				/* case 3.1 */
				set_balance( 1, node);
				set_balance(-1, right);
				rotate_left(node, tree);
				return;
			case 1:				/* case 3.2 */
				set_balance(0, node);
				set_balance(0, right);
				break;
			case -1:			/* case 3.3 */
				switch (get_balance(right->left)) {
				case 1:
					set_balance(-1, node);
					set_balance( 0, right);
					break;
				case 0:
					set_balance(0, node);
					set_balance(0, right);
					break;
				case -1:
					set_balance(0, node);
					set_balance(1, right);
					break;
				}
				set_balance(0, right->left);

				rotate_right(right, tree);
			}
			rotate_left(node, tree);
		} else {
			is_left = parent && parent->left == node;

			balance = dec_balance(node);
			if (balance == 0)
				continue;
			if (balance == -1)
				return;
			left = node->left;
			switch (get_balance(left)) {
			case 0:
				set_balance(-1, node);
				set_balance(1, left);
				rotate_right(node, tree);
				return;
			case -1:
				set_balance(0, node);
				set_balance(0, left);
				break;
			case 1:
				switch (get_balance(left->right)) {
				case 1:
					set_balance(0, node);
					set_balance(-1, left);
					break;
				case 0:
					set_balance(0, node);
					set_balance(0, left);
					break;
				case -1:
					set_balance(1, node);
					set_balance(0, left);
					break;
				}
				set_balance(0, left->right);

				rotate_left(left, tree);
			}
			rotate_right(node, tree);
		}
	}
	tree->height--;
}

void avltree_replace(struct avltree_node *old, struct avltree_node *new,
		     struct avltree *tree)
{
	struct avltree_node *parent = get_parent(old);

	if (parent)
		set_child(parent, new, parent->left == old);
	else {
	    /* I'm skeptical this case should be permitted--this
	     * says that if old is not in the tree, just make new
	     * the root of the tree */
	      tree->root = new;
	      tree->size++;
	}

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

int avltree_init(struct avltree *tree, avltree_cmp_fn_t cmp, unsigned long flags)
{
	if (flags)
		return -1;
	tree->root = NULL;
	tree->cmp_fn = cmp;
	tree->height = -1;
	tree->first = NULL;
	tree->last = NULL;
	tree->size = 0;
	return 0;
}
