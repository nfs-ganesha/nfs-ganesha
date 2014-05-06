/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
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
 * ---------------------------------------
 *
 *
 */

#ifndef _GANESHA_LIST_H
#define _GANESHA_LIST_H

#include <stddef.h>

struct glist_head {
	struct glist_head *next;
	struct glist_head *prev;
};

/**
 * @brief List head initialization
 *
 * These macros and functions are only for list heads,
 * not nodes.  The head always points to something and
 * if the list is empty, it points to itself.
 */

#define GLIST_HEAD_INIT(name) { &(name), &(name) }

#define GLIST_HEAD(name) \
	struct glist_head name = GLIST_HEAD_INIT(name)

static inline void glist_init(struct glist_head *head)
{				/* XXX glist_init? */
	head->next = head;
	head->prev = head;
}

/* Add the new element between left and right */
static inline void __glist_add(struct glist_head *left,
			       struct glist_head *right, struct glist_head *new)
{
	new->prev = left;
	new->next = right;
	left->next = new;
	right->prev = new;
}

static inline void glist_add_tail(struct glist_head *head,
				  struct glist_head *new)
{

	__glist_add(head->prev, head, new);
}

/* add after the specified entry*/
static inline void glist_add(struct glist_head *head, struct glist_head *new)
{
	__glist_add(head, head->next, new);
}

static inline void glist_del(struct glist_head *node)
{
	struct glist_head *left = node->prev;
	struct glist_head *right = node->next;
	if (left != NULL)
		left->next = right;
	if (right != NULL)
		right->prev = left;
	node->next = NULL;
	node->prev = NULL;
}

/**
 * @brief Test if the list in this head is empty
 */
static inline int glist_empty(struct glist_head *head)
{
	return head->next == head;
}

/**
 * @brief Test if this node is not on a list.
 *
 * NOT to be confused with glist_empty which is just
 * for heads.  We poison with NULL for disconnected nodes.
 */

static inline int glist_null(struct glist_head *head)
{
	return (head->next == NULL) && (head->prev == NULL);
}

static inline void glist_add_list_tail(struct glist_head *list,
				       struct glist_head *new)
{
	struct glist_head *first = new->next;
	struct glist_head *last = new->prev;

	if (glist_empty(new)) {
		/* nothing to add */
		return;
	}

	first->prev = list->prev;
	list->prev->next = first;

	last->next = list;
	list->prev = last;
}

/* Move all of src onto the tail of tgt.  Clears src. */
static inline void glist_splice_tail(struct glist_head *tgt,
				     struct glist_head *src)
{
	if (glist_empty(src))
		return;

	src->next->prev = tgt->prev;
	tgt->prev->next = src->next;
	src->prev->next = tgt;
	tgt->prev = src->prev;

	glist_init(src);
}

#define glist_for_each(node, head) \
	for (node = (head)->next; node != head; node = node->next)

#define glist_for_each_next(start, node, head)				\
	for (node = (start)->next; node != head; node = node->next)

static inline size_t glist_length(struct glist_head *head)
{
	size_t length = 0;
	struct glist_head *dummy = NULL;
	glist_for_each(dummy, head) {
		++length;
	}
	return length;
}

#define container_of(addr, type, member) ({			\
	const typeof(((type *) 0)->member) * __mptr = (addr);	\
	(type *)((char *) __mptr - offsetof(type, member)); })

#define glist_first_entry(head, type, member) \
	((head)->next != (head) ? \
	container_of((head)->next, type, member) : NULL)

#define glist_entry(node, type, member) \
	container_of(node, type, member)

#define glist_for_each_safe(node, noden, head)		\
	for (node = (head)->next, noden = node->next;	\
	     node != (head);				\
	     node = noden, noden = node->next)

#define glist_for_each_next_safe(start, node, noden, head)	\
	for (node = (start)->next, noden = node->next;	\
	     node != (head);				\
	     node = noden, noden = node->next)

#endif				/* _GANESHA_LIST_H */
