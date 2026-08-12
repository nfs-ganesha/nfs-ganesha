/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * Copyright (c) 2026 Frank Filz <ffilzlnx@mindspring.com>
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
 */
#ifndef _GSH_REFLIST_H
#define _GSH_REFLIST_H
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <urcu/ref.h>
#include "abstract_mem.h"
#include "gsh_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function to release the members of a gsh_reflist.
 *
 * Each element is expected to be completely released by this function including
 * removing it from the list with glist_del and freeing all memory associated
 * with the list element.
 *
 * @param[in]	list	The list element to release.
 * @param[in]	comp	The memory component to use for release.
 */
typedef void (*glist_release)(struct glist_head *list, mem_components_t comp);

/**
 * @brief Refcounted glist
 *
 * This struct contains an atomic refcount and a glist_head. They are allocated
 * via gsh_reflist_alloc, and then users can acquire and release references to
 * the list and its members via gsh_reflist_get and gsh_reflist_put. The
 * release function is called to release the members of the list.
 *
 * The expectation is that the list members can be used lock free with the
 * gsh_reflist being sufficient to retain the member's lifetime.
 *
 * It is conceivable to have the list members have pointers to other urcu
 * referenced objects, but management of those would be up to the individual
 * list user.
 */
struct gsh_reflist {
	struct urcu_ref grl_ref; /* refcount */
	mem_components_t grl_comp; /* memory component used for alloc/free */
	glist_release grl_release;
	struct glist_head grl_list;
};

/**
 * @brief allocate a new gsh_reflist
 *
 * Allocate a new gsh_reflist.
 *
 * Note that this creates an empty list. The caller is expected to fill the
 * list before making the gsh_reflist visible.
 *
 * @param[in]	release	Function to release the list members.
 * @param[in]	comp	Memory component used for allocation.
 */
struct gsh_reflist *gsh_reflist_alloc(glist_release release,
				      mem_components_t comp);

/**
 * @brief free the given gsh_reflist
 *
 * A callback function that the refcounting code can use to free a gsh_reflist.
 *
 * @param[in] ref	pointer to the gr_ref field in the structure
 */
void gsh_reflist_release(struct urcu_ref *ref);

/**
 * @brief acquire a reference to the given gsh_reflist
 *
 * This is only safe to use when we know that the refcount is not zero. The
 * typical use it to use rcu_dereference to fetch an rcu-managed pointer
 * and use this function to take a reference to it inside of the rcu_read_lock.
 *
 * Returns the same pointer passed in (for convenience).
 *
 * @param[in]	grl	Pointer to gsh_reflist
 */
#ifdef HAVE_URCU_REF_GET_UNLESS_ZERO
static inline struct gsh_reflist *gsh_reflist_get(struct gsh_reflist *grl)
{
	/* The assumption is that the persistent reference to the object is
	 * only put after an RCU grace period has settled.
	 */
	if (!urcu_ref_get_unless_zero(&grl->grl_ref))
		abort();
	return grl;
}
#else /* HAVE_URCU_REF_GET_UNLESS_ZERO */
/*
 * Older versions of liburcu do not have urcu_ref_get_unless_zero, so we open
 * code it here for now.
 */
static inline struct gsh_reflist *gsh_reflist_get(struct gsh_reflist *grl)
{
	struct urcu_ref *ref = &grl->grl_ref;
	long cur;

	/* The assumption is that the persistent reference to the object is
	 * only put after an RCU grace period has settled. So, we abort if
	 * it's already zero or if it looks like the counter will wrap to 0.
	 */
	cur = uatomic_read(&ref->refcount);
	for (;;) {
		long new_val, old_val = cur;

		if (old_val == 0 || old_val == LONG_MAX)
			abort();

		new_val = old_val + 1;
		cur = uatomic_cmpxchg(&ref->refcount, old_val, new_val);
		if (cur == old_val)
			break;
	}
	return grl;
}
#endif /* HAVE_URCU_REF_GET_UNLESS_ZERO */

/**
 * @brief release a gsh_reflist reference
 *
 * Use this to release a gsh_reflist reference.
 *
 * @param[in]	grl	Pointer to gsh_reflist
 */
static inline void gsh_reflist_put(struct gsh_reflist *grl)
{
	urcu_ref_put(&grl->grl_ref, gsh_reflist_release);
}

#ifdef __cplusplus
}
#endif /* extern "C" */

#endif /* _GSH_REFLIST_H */
