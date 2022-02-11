/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libtree.h - this file is part of Libtree.
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
#ifndef _LIBTREE_H
#define _LIBTREE_H

#include <stdint.h>
#include <stddef.h>

/*
 * The definition has been stolen from the Linux kernel.
 */
#ifdef __GNUC__
#define bstree_container_of(node, type, member) ({			\
	const struct bstree_node *__mptr = (node);			\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#define rbtree_container_of(node, type, member) ({			\
	const struct rbtree_node *__mptr = (node);			\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#define avltree_container_of(node, type, member) ({			\
	const struct avltree_node *__mptr = (node);			\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#define splaytree_container_of(node, type, member) ({			\
	const struct splaytree_node *__mptr = (node);			\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#else
#define bstree_container_of(node, type, member)			\
	((type *)((char *)(node) - offsetof(type, member)))
#define rbtree_container_of(node, type, member)			\
	((type *)((char *)(node) - offsetof(type, member)))
#define avltree_container_of(node, type, member)			\
	((type *)((char *)(node) - offsetof(type, member)))
#define splaytree_container_of(node, type, member)			\
	((type *)((char *)(node) - offsetof(type, member)))
#endif				/* __GNUC__ */

/*
 * Threaded binary search tree
 */
#ifdef UINTPTR_MAX

struct bstree_node {
	uintptr_t left, right;
} __attribute__ ((aligned(2)));

#else

struct bstree_node {
	struct bstree_node *left, *right;
	unsigned left_is_thread:1;
	unsigned right_is_thread:1;
};

#endif				/* UINTPTR_MAX */

typedef int (*bstree_cmp_fn_t) (const struct bstree_node *,
				const struct bstree_node *);

struct bstree {
	struct bstree_node *root;
	bstree_cmp_fn_t cmp_fn;
	struct bstree_node *first, *last;
	uint64_t reserved[4];
};

struct bstree_node *bstree_first(const struct bstree *tree);
struct bstree_node *bstree_last(const struct bstree *tree);
struct bstree_node *bstree_next(const struct bstree_node *node);
struct bstree_node *bstree_prev(const struct bstree_node *node);

struct bstree_node *bstree_lookup(const struct bstree_node *key,
				  const struct bstree *tree);
struct bstree_node *bstree_insert(struct bstree_node *node,
				  struct bstree *tree);
void bstree_remove(struct bstree_node *node, struct bstree *tree);
void bstree_replace(struct bstree_node *old, struct bstree_node *newe,
		    struct bstree *tree);
int bstree_init(struct bstree *tree, bstree_cmp_fn_t cmp, unsigned long flags);

/*
 * Red-black tree
 */
enum rb_color {
	RB_BLACK,
	RB_RED,
};

#ifdef UINTPTR_MAX

struct rbtree_node {
	struct rbtree_node *left, *right;
	uintptr_t parent;
} __attribute__ ((aligned(2)));

#else

struct rbtree_node {
	struct rbtree_node *left, *right;
	struct rbtree_node *parent;
	enum rb_color color;
};

#endif				/* UINTPTR_MAX */

typedef int (*rbtree_cmp_fn_t) (const struct rbtree_node *,
				const struct rbtree_node *);

struct rbtree {
	struct rbtree_node *root;
	rbtree_cmp_fn_t cmp_fn;
	struct rbtree_node *first, *last;
	uint64_t reserved[4];
};

struct rbtree_node *rbtree_first(const struct rbtree *tree);
struct rbtree_node *rbtree_last(const struct rbtree *tree);
struct rbtree_node *rbtree_next(const struct rbtree_node *node);
struct rbtree_node *rbtree_prev(const struct rbtree_node *node);

struct rbtree_node *rbtree_lookup(const struct rbtree_node *key,
				  const struct rbtree *tree);
struct rbtree_node *rbtree_insert(struct rbtree_node *node,
				  struct rbtree *tree);
void rbtree_remove(struct rbtree_node *node, struct rbtree *tree);
void rbtree_replace(struct rbtree_node *old, struct rbtree_node *newe,
		    struct rbtree *tree);
int rbtree_init(struct rbtree *tree, rbtree_cmp_fn_t cmp, unsigned long flags);

/*
 * AVL tree
 */
#if defined UINTPTR_MAX && UINTPTR_MAX == UINT64_MAX

struct avltree_node {
	struct avltree_node *left, *right;
	uintptr_t parent;	/* balance factor [0:4] */
} __attribute__ ((aligned(8)));

static inline signed int get_balance(struct avltree_node *node)
{
	return (int)(node->parent & 7) - 2;
}

#else

struct avltree_node {
	struct avltree_node *left, *right;
	struct avltree_node *parent;
	signed balance:3;	/* balance factor [-2:+2] */
};

static inline signed int get_balance(struct avltree_node *node)
{
	return node->balance;
}

#endif

typedef int (*avltree_cmp_fn_t) (const struct avltree_node *,
				 const struct avltree_node *);

struct avltree {
	struct avltree_node *root;
	avltree_cmp_fn_t cmp_fn;
	int height;
	struct avltree_node *first, *last;
	uint64_t size;
#if 0
	uint64_t reserved[4];
#endif
};

/**
 * @brief Perform a lookup in an AVL tree, returning useful bits for
 * subsequent inser.
 *
 * 'pparent', 'unbalanced' and 'is_left' are only used for
 * insertions. Normally GCC will notice this and get rid of them for
 * lookups.
 *
 * @param[in]     key         Key to look for
 * @param[in]     tree        AVL tree to look in
 * @param[in,out] pparent     Parent of key
 * @param[in,out] unbalanced  Unbalanced parent
 * @param[in,out] is_left     True if key would be to left of parent
 * @param[in]     cmp_fn      Comparison function to use
 *
 * @returns The node found if any
 */
static inline
struct avltree_node *avltree_do_lookup(const struct avltree_node *key,
				       const struct avltree *tree,
				       struct avltree_node **pparent,
				       struct avltree_node **unbalanced,
				       int *is_left,
				       avltree_cmp_fn_t cmp_fn)
{
	struct avltree_node *node = tree->root;
	int res = 0;

	*pparent = NULL;
	*unbalanced = node;
	*is_left = 0;

	while (node) {
		if (get_balance(node) != 0)
			*unbalanced = node;

		res = cmp_fn(node, key);
		if (res == 0)
			return node;
		*pparent = node;
		*is_left = res > 0;
		if (*is_left)
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

static inline
struct avltree_node *avltree_inline_lookup(const struct avltree_node *key,
					   const struct avltree *tree,
					   avltree_cmp_fn_t cmp_fn)
{
	struct avltree_node *parent, *unbalanced;
	int is_left;

	return avltree_do_lookup(key, tree, &parent, &unbalanced, &is_left,
				 cmp_fn);
}

static inline
struct avltree_node *avltree_lookup(const struct avltree_node *key,
				    const struct avltree *tree)
{
	return avltree_inline_lookup(key, tree, tree->cmp_fn);
}

void avltree_do_insert(struct avltree_node *node,
		       struct avltree *tree,
		       struct avltree_node *parent,
		       struct avltree_node *unbalanced,
		       int is_left);

static inline
struct avltree_node *avltree_inline_insert(struct avltree_node *node,
					   struct avltree *tree,
					   avltree_cmp_fn_t cmp_fn)
{
	struct avltree_node *found, *parent, *unbalanced;
	int is_left;

	found = avltree_do_lookup(node, tree, &parent, &unbalanced, &is_left,
				  cmp_fn);

	if (found)
		return found;

	avltree_do_insert(node, tree, parent, unbalanced, is_left);

	return NULL;
}

static inline
struct avltree_node *avltree_insert(struct avltree_node *node,
				    struct avltree *tree)
{
	return avltree_inline_insert(node, tree, tree->cmp_fn);
}

static inline
struct avltree_node *avltree_first(const struct avltree *tree)
{
	return tree->first;
}

static inline
struct avltree_node *avltree_last(const struct avltree *tree)
{
	return tree->last;
}

struct avltree_node *avltree_next(const struct avltree_node *node);
struct avltree_node *avltree_prev(const struct avltree_node *node);
uint64_t avltree_size(const struct avltree *tree);
struct avltree_node *avltree_inf(const struct avltree_node *key,
				 const struct avltree *tree);
struct avltree_node *avltree_sup(const struct avltree_node *key,
				 const struct avltree *tree);
void avltree_remove(struct avltree_node *node, struct avltree *tree);
void avltree_replace(struct avltree_node *old, struct avltree_node *newe,
		     struct avltree *tree);
int avltree_init(struct avltree *tree, avltree_cmp_fn_t cmp,
		 unsigned long flags);

/*
 * Splay tree
 */
#ifdef UINTPTR_MAX

struct splaytree_node {
	uintptr_t left, right;
} __attribute__ ((aligned(2)));

#else

struct splaytree_node {
	struct splaytree_node *left, *right;
	unsigned left_is_thread:1;
	unsigned right_is_thread:1;
};

#endif

typedef int (*splaytree_cmp_fn_t) (const struct splaytree_node *,
				   const struct splaytree_node *);

struct splaytree {
	struct splaytree_node *root;
	struct splaytree_node *first, *last;
	splaytree_cmp_fn_t cmp_fn;
	uint64_t reserved[4];
};

struct splaytree_node *splaytree_first(const struct splaytree *tree);
struct splaytree_node *splaytree_last(const struct splaytree *tree);
struct splaytree_node *splaytree_next(const struct splaytree_node *node);
struct splaytree_node *splaytree_prev(const struct splaytree_node *node);

struct splaytree_node *splaytree_lookup(const struct splaytree_node *key,
					struct splaytree *tree);
struct splaytree_node *splaytree_insert(struct splaytree_node *node,
					struct splaytree *tree);
void splaytree_remove(struct splaytree_node *node, struct splaytree *tree);
void splaytree_replace(struct splaytree_node *old, struct splaytree_node *newe,
		       struct splaytree *tree);
int splaytree_init(struct splaytree *tree, splaytree_cmp_fn_t cmp,
		   unsigned long flags);

#endif				/* _LIBTREE_H */
