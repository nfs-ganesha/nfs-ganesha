/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

/*
 * @brief Compare routine that is used by glist_insert_sorted
 *
 * This routine can be defined by the calling function
 * to enable a sorted insert
 *
 * @param struct glist_head *: The first element to compare
 * @param struct glist_head *: The second element to compare
 *
 * @return  negative if the 1st element should appear before the 2nd element
 *          0 if the 1st and 2nd element are equal
 *          positive if the 1st element should appear after the 2nd element
 */
typedef int (*glist_compare) (struct glist_head *, struct glist_head *);

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
			       struct glist_head *right, struct glist_head *elt)
{
	elt->prev = left;
	elt->next = right;
	left->next = elt;
	right->prev = elt;
}

static inline void glist_add_tail(struct glist_head *head,
				  struct glist_head *elt)
{

	__glist_add(head->prev, head, elt);
}

/* add after the specified entry*/
static inline void glist_add(struct glist_head *head, struct glist_head *elt)
{
	__glist_add(head, head->next, elt);
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


/*
 * @brief Move the node to list tail
 *
 * This function would help to move node to the tail of list.
 * Calling `glist_del` and `glist_add_tail` would do extra
 * operations when the node is already tail.
 * This function does pre-check for the situation that node is
 * already tail so that would reduce some operations.
 *
 * @param struct glist_head *head: The first element of list
 * @param struct glist_head *node: The element that want to move to tail
 *
 * @return Nothing; The operation is done w/o return value.
 */
static inline void glist_move_tail(struct glist_head *head,
				   struct glist_head *node)
{
	/* already tail */
	if (node == head->prev)
		return;

	glist_del(node);
	__glist_add(head->prev, head, node);
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
				       struct glist_head *elt)
{
	struct glist_head *first = elt->next;
	struct glist_head *last = elt->prev;

	if (glist_empty(elt)) {
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

static inline void glist_swap_lists(struct glist_head *l1,
				    struct glist_head *l2)
{
	struct glist_head temp;

	if (glist_empty(l1)) {
		/* l1 was empty, so splice tail will accomplish swap. */
		glist_splice_tail(l1, l2);
		return;
	}

	if (glist_empty(l2)) {
		/* l2 was empty, so reverse splice tail will accomplish swap. */
		glist_splice_tail(l2, l1);
		return;
	}

	/* Both lists are non-empty */

	/* First swap the list pointers. */
	temp = *l1;
	*l1 = *l2;
	*l2 = temp;

	/* Then fixup first entry in each list prev to point to it's new head */
	l1->next->prev = l1;
	l2->next->prev = l2;

	/* And fixup the last entry in each list next to point to it's new head
	 */
	l1->prev->next = l1;
	l2->prev->next = l2;
}

/**
 * @brief Split list list1 into list2 at element.
 *
 * @note list2 is expected to be empty. list1 is expected to be non-empty (i.e.
 * element is NOT list1).
 *
 * @param[in,out] list1    Source list.
 * @param[in,out] list2    Destination list.
 * @param[in,out] element  List element to become first element in list2.
 *
 */
static inline void glist_split(struct glist_head *list1,
			       struct glist_head *list2,
			       struct glist_head *element)
{
	/* Set up list2 to contain element to the end. */
	list2->next = element;
	list2->prev = list1->prev;

	/* Fixup the last element of list1 to be the last element of list2, even
	 * if it was element.
	 */
	list2->prev->next = list2;

	/* Now fixup list1 even if element was first element of list1. */
	list1->prev = element->prev;

	/* Now fixup prev of element, even if element was first element of
	 * list1.
	 */
	element->prev->next = list1;

	/* Now fixup element */
	element->prev = list2;
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

#define glist_last_entry(head, type, member) \
	((head)->prev != (head) ? \
	container_of((head)->prev, type, member) : NULL)

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

/* Return the next entry in the list after node if any. */
#define glist_next_entry(head, type, member, node) \
	((node)->next != (head) ? \
	container_of((node)->next, type, member) : NULL)

/* Return the previous entry in the list after node if any. */
#define glist_prev_entry(head, type, member, node) \
	((node)->prev != (head) ? \
	container_of((node)->prev, type, member) : NULL)

static inline void glist_insert_sorted(struct glist_head *head,
				       struct glist_head *elt,
				       glist_compare compare)
{
	struct glist_head *next = NULL;

	if (glist_empty(head)) {
		glist_add_tail(head, elt);
		return;
	}
	glist_for_each(next, head) {
		if (compare(next, elt) > 0)
			break;
	}

	__glist_add(next->prev, next, elt);
}

#endif				/* _GANESHA_LIST_H */
