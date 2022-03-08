/* SPDX-License-Identifier: GPL-2.0-or-later WITH special exception */
/**
 * @addtogroup hashtable
 * @{
 */

/**
 * @file rbt_tree.h
 * @brief Red-Black Tree
 */
/*
 * Implementation of RedBlack trees
 * Macros and algorithms
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

#ifndef RBT_TREE_H
#define RBT_TREE_H

/*
 * For RBT_HEAD_INIT, RBT_COUNT, RBT_RIGHTMOST and RBT_LEFTMOST :
 *   __header is the header
 */
#define RBT_HEAD_INIT(__header)						\
	((__header)->root = 0,						\
	 (__header)->leftmost = 0,					\
	 (__header)->rightmost = 0,					\
	 (__header)->rbt_num_node = 0)

#define RBT_COUNT(__header)	((__header)->rbt_num_node)

#define RBT_RIGHTMOST(__header)	((__header)->rightmost)

#define RBT_LEFTMOST(__header)	((__header)->leftmost)

/*
 * For RBT_VALUE :
 *   __node is any node
 */
#define RBT_VALUE(__node)	((__node)->rbt_value)

/*
 * For RBT_OPAQ :
 *   __node is any node
 */
#define RBT_OPAQ(__node)	((__node)->rbt_opaq)

/*
 * For RBT_INCREMENT and RBT_DECREMENT :
 *   __node is the starting node
 *   __x is a temporary variable
 *   __node is modified to point to the next/previous node
 */
#define RBT_INCREMENT(__node) ({					\
	if ((__node)->next) {						\
		__node = (__node)->next;				\
		while ((__node)->left)					\
			(__node) = (__node)->left;			\
	} else {							\
		struct rbt_node *__x;					\
		do {							\
			__x = (__node);					\
		} while ((((__node) = (__node)->parent)) &&		\
			 ((__node)->next == __x));			\
	}								\
})


#define RBT_DECREMENT(__node) ({					\
	if ((__node)->left) {						\
		__node = (__node)->left;				\
		while ((__node)->next)					\
			(__node) = (__node)->next;			\
	} else {							\
		struct rbt_node *__x;					\
		do {							\
			__x = (__node);					\
		} while ((((__node) = (__node)->parent)) &&		\
			 ((__node)->left == __x));			\
	}								\
})

/*
 * For RBT_LOOP and RBT_LOOP_REVERSE :
 *   __header is the header
 *   __it is the iterator (type rbt_node *)
 *
 * These macros must be used with, respectively,
 * RBT_INCREMENT and RBT_DECREMENT.
 */
#define RBT_LOOP(__header, __it)					\
	for ((__it) = (__header)->leftmost; (__it);)

#define RBT_LOOP_REVERSE(__header, __it)				\
	for ((__it) = (__header)->rightmost; (__it);)

/*
 * For RBT_ROTATE_LEFT and RBT_ROTATE_RIGHT :
 *   __xx is  pointer to the pivotal node
 *   __yy is a temporary variable
 *   the pivotal node is not modified except its links in the tree
 * For RBT_ROTATE_LEFT, (__xx)->next must not be zero.
 * For RBT_ROTATE_RIGHT, (__xx)->left must not be zero.
 */
#define RBT_ROTATE_LEFT(__xx) ({					\
	struct rbt_node *__yy;						\
	__yy = (__xx)->next;						\
	(__xx)->next = __yy->left;					\
		if (((__xx)->next)) {					\
			__yy->left->parent = (__xx);			\
			__yy->left->anchor = &(__xx)->next;		\
		}							\
	__yy->parent = (__xx)->parent;					\
	__yy->left = (__xx);						\
	__yy->anchor = (__xx)->anchor;					\
	(__xx)->parent = __yy;						\
	(__xx)->anchor = &__yy->left;					\
	*__yy->anchor = __yy;						\
})

#define RBT_ROTATE_RIGHT(__xx) ({					\
	struct rbt_node *__yy;						\
	__yy = (__xx)->left;						\
	(__xx)->left = __yy->next;					\
	if ((__xx)->left) {						\
		__yy->next->parent = (__xx);				\
		__yy->next->anchor = &(__xx)->left;			\
	}								\
	__yy->parent = (__xx)->parent;					\
	__yy->next = (__xx);						\
	__yy->anchor = (__xx)->anchor;					\
	(__xx)->parent = __yy;						\
	(__xx)->anchor = &__yy->next;					\
	*__yy->anchor = __yy;						\
})

/*
 * For RBT_INSERT :
 *   __node is the new node to be inserted
 *   __par is the node which will be the first parent of __node
 *   __node and __par are not modified
 *   *__node and *__par are modified
 *   __header is the header node
 *   __x and __y are temporary variables
 *
 * __par must have been returned by RBT_FIND (successful or not).
 * If RBT_FIND was not successful, __par cannot have two children :
 *  - If __node->rbt_value > __par->rbt_value, then __par->next is NULL.
 *    __node will be installed at __par->next.
 *  - If __node->rbt_value < __par->rbt_value, then __par->left is NULL.
 *    __node will be installed at __par->next.
 * If RBT_FIND was successful :
 *  - If __par has two children, search the previous node and replace
 *    __par by this previous node. Then insert __node at __par->next.
 *  - If __par->left is free, install __node at __par->left.
 *  - Otherwise, install __node at __par->next.
 *
 * If this insertion unbalances the tree, __node may end in a different
 * position.
 */
#define RBT_INSERT(__header, __node, __par) ({				\
	struct rbt_node *__x, *__y;					\
	(__header)->rbt_num_node++;					\
	__y = (__par);							\
	if (__y == 0) {							\
		(__node)->anchor = &(__header)->root;			\
		(__header)->root = (__node);				\
		(__header)->rightmost = (__node);			\
		(__header)->leftmost = (__node);			\
	} else if (((__node)->rbt_value == __y->rbt_value) &&		\
		   __y->next && __y->left) {				\
		__y = __y->left;					\
		while (__y->next)					\
			__y = __y->next;				\
		__y->next = (__node);					\
		(__node)->anchor = &__y->next;				\
	} else if (((__node)->rbt_value > __y->rbt_value) ||		\
		   (((__node)->rbt_value == __y->rbt_value)		\
		    && __y->left)) {					\
		__y->next = (__node);					\
		(__node)->anchor = &__y->next;				\
		if (__y == (__header)->rightmost) {			\
			(__header)->rightmost = (__node);		\
		}							\
	} else {							\
		__y->left = (__node);					\
		(__node)->anchor = &__y->left;				\
		if (__y == (__header)->leftmost) {			\
			(__header)->leftmost = (__node);		\
		}							\
	}								\
	(__node)->rbt_flags = 0;					\
	(__node)->parent = __y;						\
	(__node)->left = 0;						\
	(__node)->next = 0;						\
	__x = (__node);							\
	while (__x->parent) {						\
		__x->rbt_flags |= RBT_RED;				\
		if ((__x->parent->rbt_flags & RBT_RED) == 0)		\
			break;						\
		if (__x->parent == __x->parent->parent->left) {		\
			__y = __x->parent->parent->next;		\
			if ((__y == 0) || ((__y->rbt_flags & RBT_RED)	\
					   == 0)) {			\
				if (__x == __x->parent->next) {		\
					__x = __x->parent;		\
					RBT_ROTATE_LEFT(__x);		\
				}					\
				__x->parent->rbt_flags &= ~RBT_RED;	\
				__x = __x->parent->parent;		\
				__x->rbt_flags |= RBT_RED;		\
				RBT_ROTATE_RIGHT(__x);			\
				break;					\
			}						\
		} else {						\
			__y = __x->parent->parent->left;		\
			if ((__y == 0) || ((__y->rbt_flags & RBT_RED)	\
					   == 0)) {			\
				if (__x == __x->parent->left) {		\
					__x = __x->parent;		\
					RBT_ROTATE_RIGHT(__x);		\
				}					\
				__x->parent->rbt_flags &= ~RBT_RED;	\
				__x = __x->parent->parent;		\
				__x->rbt_flags |= RBT_RED;		\
				RBT_ROTATE_LEFT(__x);			\
				break;					\
			}						\
		}							\
		__x->parent->rbt_flags &= ~RBT_RED;			\
		__y->rbt_flags &= ~RBT_RED;				\
		__x = __x->parent->parent;				\
	}								\
})

/*
 * For RBT_UNLINK :
 *   __node is the node to unlink
 *   __header is the header node
 *   __x, __z and __y are temporary variables
 *   __node->rbt_flags may be modified
 *   otherwise, __node is not modified
 */

#define RBT_UNLINK(__header, __node) ({					\
	struct rbt_node *__x, *__y, *__z;				\
	(__header)->rbt_num_node--;					\
	if ((__node)->left && (__node)->next) {				\
		__y = (__node)->next;					\
		while (__y->left)					\
			__y = __y->left;				\
		if (((__node)->rbt_flags & RBT_RED) !=			\
		    (__y->rbt_flags & RBT_RED)) {			\
			(__node)->rbt_flags ^= RBT_RED;			\
			__y->rbt_flags ^= RBT_RED;			\
		}							\
		__x = __y->next;					\
		(__node)->left->parent = __y;				\
		(__node)->left->anchor = &__y->left;			\
		__y->left = (__node)->left;				\
		if (__y == (__node)->next) {				\
			__z = __y;					\
		} else {						\
			__z = __y->parent;				\
			if (__x) {					\
				__x->parent = __z;			\
				__x->anchor = &__z->left;		\
			}						\
			__z->left = __x;  /* __y was a child of left */	\
			__y->next = (__node)->next;			\
			(__node)->next->parent = __y;			\
			(__node)->next->anchor = &__y->next;		\
		}							\
		__y->parent = (__node)->parent;				\
		__y->anchor = (__node)->anchor;				\
		*(__node)->anchor = __y;				\
	} else {							\
		__z = (__node)->parent;					\
		__x = (__node)->next;     /* __x might be NULL */	\
		if (__x == 0)						\
			__x = (__node)->left;				\
		if (__x) {						\
			__x->parent = __z;				\
			__x->anchor = (__node)->anchor;			\
		}							\
		if ((__header)->leftmost == (__node)) {			\
			if (__x) {					\
				__y = __x;				\
				while (__y->left)			\
					__y = __y->left;		\
				(__header)->leftmost = __y;		\
			} else {					\
				(__header)->leftmost = __z;		\
			}						\
		}							\
		if ((__header)->rightmost == (__node)) {		\
			if (__x) {					\
				__y = __x;				\
				while (__y->next)			\
					__y = __y->next;		\
				(__header)->rightmost = __y;		\
			} else {					\
				(__header)->rightmost = __z;		\
			}						\
		}							\
		*(__node)->anchor = __x;				\
	}								\
	if (!((__node)->rbt_flags & RBT_RED)) {				\
		while ((__z) &&						\
		       ((__x == 0) || !(__x->rbt_flags & RBT_RED))) {	\
			if (__x == __z->left) {				\
				__y = __z->next;			\
				if (__y->rbt_flags & RBT_RED) {		\
					__y->rbt_flags &= ~RBT_RED;	\
					__z->rbt_flags |= RBT_RED;	\
					RBT_ROTATE_LEFT(__z);		\
					__y = __z->next;		\
				}					\
				if ((__y->left == 0 ||			\
				     !(__y->left->rbt_flags & RBT_RED)) \
				    && (__y->next == 0 ||		\
					!(__y->next->rbt_flags &	\
					  RBT_RED))) {			\
					__y->rbt_flags |= RBT_RED;	\
					__x = __z;			\
					__z = __z->parent;		\
				} else {				\
					if (__y->next == 0 ||		\
					    !(__y->next->rbt_flags &	\
					      RBT_RED)) {		\
						if (__y->left)		\
							__y->left->rbt_flags \
								&= ~RBT_RED; \
						__y->rbt_flags |= RBT_RED; \
						RBT_ROTATE_RIGHT(__y);	\
						__y = __z->next;	\
					}				\
					__y->rbt_flags &= ~RBT_RED;	\
					__y->rbt_flags |=		\
						__z->rbt_flags & RBT_RED; \
					__z->rbt_flags &= ~RBT_RED;	\
					if (__y->next)			\
						__y->next->rbt_flags	\
							&= ~RBT_RED;	\
					RBT_ROTATE_LEFT(__z);		\
					break;				\
				}					\
			} else {					\
				__y = __z->left;			\
				if (__y->rbt_flags & RBT_RED) {		\
					__y->rbt_flags &= ~RBT_RED;	\
					__z->rbt_flags |= RBT_RED;	\
					RBT_ROTATE_RIGHT(__z);		\
					__y = __z->left;		\
				}					\
				if ((__y->left == 0 ||			\
				     !(__y->left->rbt_flags & RBT_RED)) \
				    && (__y->next == 0 ||		\
					!(__y->next->rbt_flags &	\
					  RBT_RED))) {			\
					__y->rbt_flags |= RBT_RED;	\
					__x = __z;			\
					__z = __z->parent;		\
				} else {				\
					if (__y->left == 0 ||		\
					    !(__y->left->rbt_flags &	\
					      RBT_RED)) {		\
						if (__y->next)		\
							__y->next->rbt_flags \
								&= ~RBT_RED; \
						__y->rbt_flags |= RBT_RED; \
						RBT_ROTATE_LEFT(__y);	\
						__y = __z->left;	\
					}				\
					__y->rbt_flags &= ~RBT_RED;	\
					__y->rbt_flags |= __z->rbt_flags\
						& RBT_RED;		\
					__z->rbt_flags &= ~RBT_RED;	\
					if (__y->left)			\
						__y->left->rbt_flags	\
							&= ~RBT_RED;	\
					RBT_ROTATE_RIGHT(__z);		\
					break;				\
			}					\
			}						\
		}							\
		if (__x)						\
			__x->rbt_flags &= ~RBT_RED;			\
	}								\
})

/*
 * For RBT_FIND
 *   __header is the header node
 *   __node will contain the found node
 *   __val is a uint64_t and contains the value to search
 *   __x is a temporary variable
 * No nodes are modified
 * __node is modified
 *
 * When RBT_FIND returns, __node points to the node whose value is __val.
 * If multiple nodes have the value __val, only one is returned.
 * If no node has the value __val, __node points to the preceeding
 * or the following node and __node cannot have two children.
 * After the call, if __node is NULL, the tree is empty.
 * To check for success :
 *   if (((__node) != 0) && (RBT_VALUE(__node) == (__val))) {
 *     -- node found --
 *     ...
 *   }
 *
 * RBT_FIND must be called before inserting a node using RBT_INSERT.
 */
#define RBT_FIND(__header, __node, __val) ({				\
	struct rbt_node *__x;						\
	(__node) = (__header)->root;					\
	__x = (__header)->root;						\
	while (__x) {							\
		(__node) = __x;						\
		if (__x->rbt_value > (__val)) {				\
			__x = __x->left;				\
		} else if (__x->rbt_value < (__val)) {			\
			__x = __x->next;				\
		} else {						\
			break;						\
		}							\
	}								\
})

/*
 * For RBT_FIND_LEFT
 *   __header is the header node
 *   __node will contain the found node
 *   __val is a uint64_t and contains the value to search
 *   __x is a temporary variable
 * No nodes are modified
 * __node is modified
 *
 * When RBT_FIND_LEFT returns, __node points to the leftmost node
 * whose value is __val.
 * If multiple nodes have the value __val, only one is returned.
 * If no node has the value __val, __node is NULL.
 * This is different from RBT_FIND. RBT_FIND_LEFT cannot be used
 * to insert a new node.
 * To check for success :
 *   if ((__node) != 0) {
 *     -- node found --
 *     ...
 *   }
 */
#define RBT_FIND_LEFT(__header, __node, __val) ({			\
	struct rbt_node *__x;						\
	(__node) = 0;							\
	__x = (__header)->root;						\
	while (__x) {							\
		if (__x->rbt_value > (__val)) {				\
			__x = __x->left;				\
		} else if (__x->rbt_value < (__val)) {			\
			__x = __x->next;				\
		} else {						\
			(__node) = __x;					\
			while (__x) {					\
				while ((__x = __x->left)) {		\
					if (__x->rbt_value < (__val))	\
						break;			\
					(__node) = __x;			\
				}					\
				if (__x == 0)				\
					break;				\
				while ((__x = __x->next)) {		\
					if (__x->rbt_value ==		\
					    (__val)) {			\
						(__node) = __x;		\
						break;			\
					}				\
				}					\
			}						\
			break;						\
		}							\
	}								\
})

/*
 * RBT_BLACK_COUNT counts the number of black nodes in the parents of a node
 */
#define RBT_BLACK_COUNT(__node, __sum) ({				\
	for ((__sum) = 0; (__node); (__node) = (__node)->parent) {	\
		if (!((__node)->rbt_flags & RBT_RED))			\
			++(__sum);					\
	}								\
})

#define RBT_VERIFY(__header, __it, __error) ({				\
	int __len, __num, __sum;					\
	struct rbt_node *__L, *__R;					\
	(__error) = 0;							\
	if ((__header)->rbt_num_node == 0) {				\
		if (((__header)->leftmost) ||				\
		    ((__header)->rightmost) ||				\
		    ((__header)->root)) {				\
			(__error) = 1;					\
			(__it) = 0;					\
		}							\
	} else {							\
		__L = (__header)->leftmost;				\
		RBT_BLACK_COUNT(__L, __len)				\
			__num = 0;					\
		RBT_LOOP((__header), (__it)) {				\
			if ((__it)->parent == 0) {			\
				if (((__it) != (__header)->root) ||	\
				    ((__it)->anchor !=			\
				     &(__header)->root)) {		\
					(__error) = 2;			\
					break;				\
				}					\
			} else {					\
				if ((((__it) == (__it)->parent->next) && \
				     ((__it)->anchor !=			\
				      &(__it)->parent->next)) ||	\
				    (((__it) == (__it)->parent->left) && \
				     ((__it)->anchor !=			\
				      &(__it)->parent->left))) {	\
					(__error) = 2;			\
					break;				\
				}					\
			}						\
			__L = (__it)->left;				\
			__R = (__it)->next;				\
			if (((__it)->rbt_flags & RBT_RED) &&		\
			    ((__L && (__L->rbt_flags & RBT_RED)) ||	\
			     (__R && (__R->rbt_flags & RBT_RED)))) {	\
				(__error) = 3;				\
				break;					\
			}						\
			if (__L && (__L->rbt_value > (__it)->rbt_value)) { \
				(__error) = 4;				\
				break;					\
			}						\
			if (__R && (__R->rbt_value < (__it)->rbt_value)) { \
				(__error) = 5;				\
				break;					\
			}						\
			if (!__L && !__R) {				\
				__L = (__it);				\
				RBT_BLACK_COUNT(__L, __sum)		\
					if (__sum != __len) {		\
						(__error) = 6;		\
						break;			\
					}				\
			}						\
			__num++;					\
			RBT_INCREMENT(__it)				\
				}					\
		if (((__error) == 0) && (__num !=			\
					 (__header)->rbt_num_node)) {	\
			(__error) = 7;					\
			(__it) = 0;					\
		}							\
		/* test RBT_DECREMENT */				\
		__num = 0;						\
		RBT_LOOP_REVERSE(__header, __it) {			\
			__num++;					\
			RBT_DECREMENT(__it)				\
				}					\
		if (((__error) == 0) && (__num !=			\
					 (__header)->rbt_num_node)) {	\
			(__error) = 8;					\
			(__it) = 0;					\
		}							\
		if ((__error) == 0) {					\
			__L = (__header)->root;				\
			while ((__L)->left)				\
				(__L) = (__L)->left;			\
			__R = (__header)->root;				\
			while ((__R)->next)				\
				(__R) = (__R)->next;			\
			if ((__L != (__header)->leftmost) ||		\
			    (__R != (__header)->rightmost)) {		\
				(__error) = 9;				\
				(__it) = 0;				\
			}						\
		}							\
		if (((__error) == 0) && ((__header)->root) &&		\
		    !((__header)->root->parent == 0)) {			\
			(__error) = 10;					\
			(__it) = 0;					\
		}							\
	}								\
})

#endif /* RBT_TREE_H */

/** @{ */
