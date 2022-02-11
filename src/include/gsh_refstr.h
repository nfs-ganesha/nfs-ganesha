/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * Copyright (c) 2018 Jeff Layton <jlayton@redhat.com>
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
#ifndef _GSH_REFSTR_H
#define _GSH_REFSTR_H
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <urcu/ref.h>
#include <string.h>

/**
 * @brief Refcounted strings
 *
 * This struct contains an atomic refcount and a flexarray intended to hold a
 * NULL terminated string. They are allocated via gsh_refstr_alloc, and then
 * users can acquire and release references to them via gsh_refstr_get and
 * gsh_refstr_put.
 */
struct gsh_refstr {
	struct urcu_ref	gr_ref;		/* refcount */
	char		gr_val[];	/* buffer */
};

/**
 * @brief allocate a new gsh_refstr
 *
 * Allocate a new gsh_refstr with a gr_val buffer of the given length.
 *
 * Note that if allocating for a string, ensure that you pass in a length that
 * includes the NULL byte.
 *
 * @param[in]	len	Length of the embedded buffer
 */
struct gsh_refstr *gsh_refstr_alloc(size_t len);

/**
 * @brief create a new gsh_refstr by duplicating an existing string
 *
 * Allocate a new gsh_refstr with a gr_val buffer to contain the duplicate.
 *
 * @param[in]	str	The string to be duplicated in the new gsh_refstr
 */
static inline struct gsh_refstr *gsh_refstr_dup(const char *str)
{
	size_t len = strlen(str) + 1;
	struct gsh_refstr *refstr = gsh_refstr_alloc(len);

	memcpy(refstr->gr_val, str, len);

	return refstr;
}

/**
 * @brief free the given gsh_refstr
 *
 * A callback function that the refcounting code can use to free a gsh_refstr.
 *
 * @param[in]	pointer to the gr_ref field in the structure
 */
void gsh_refstr_release(struct urcu_ref *ref);

/**
 * @brief acquire a reference to the given gsh_refstr
 *
 * This is only safe to use when we know that the refcount is not zero. The
 * typical use it to use rcu_dereference to fetch an rcu-managed pointer
 * and use this function to take a reference to it inside of the rcu_read_lock.
 *
 * Returns the same pointer passed in (for convenience).
 *
 * @param[in]	gr	Pointer to gsh_refstr
 */
#ifdef HAVE_URCU_REF_GET_UNLESS_ZERO
static inline struct gsh_refstr *gsh_refstr_get(struct gsh_refstr *gr)
{
	/*
	 * The assumption is that the persistent reference to the object is
	 * only put after an RCU grace period has settled.
	 */
	if (!urcu_ref_get_unless_zero(&gr->gr_ref))
		abort();
	return gr;
}
#else /* HAVE_URCU_REF_GET_UNLESS_ZERO */
/*
 * Older versions of liburcu do not have urcu_ref_get_unless_zero, so we open
 * code it here for now.
 */
static inline struct gsh_refstr *gsh_refstr_get(struct gsh_refstr *gr)
{
	struct urcu_ref	*ref = &gr->gr_ref;
	long cur;

	/*
	 * The assumption is that the persistent reference to the object is
	 * only put after an RCU grace period has settled. So, we abort if
	 * it's already zero or if it looks like the counter will wrap to 0.
	 */
	cur = uatomic_read(&ref->refcount);
	for (;;) {
		long new, old = cur;

		old = cur;
		if (old == 0 || old == LONG_MAX)
			abort();

		new = old + 1;
		cur = uatomic_cmpxchg(&ref->refcount, old, new);
		if (cur == old)
			break;
	}
	return gr;
}
#endif /* HAVE_URCU_REF_GET_UNLESS_ZERO */

/**
 * @brief release a gsh_refstr reference
 *
 * Use this to release a gsh_refstr reference.
 *
 * @param[in]	gr	Pointer to gsh_refstr
 */
static inline void gsh_refstr_put(struct gsh_refstr *gr)
{
	return urcu_ref_put(&gr->gr_ref, gsh_refstr_release);
}
#endif /* _GSH_REFSTR_H */
