// SPDX-License-Identifier: LGPL-3.0-or-later
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

#include "config.h"
#include <stddef.h>
#include <urcu/ref.h>
#include "gsh_refstr.h"
#include "abstract_mem.h"
#include "gsh_list.h"

struct gsh_refstr *gsh_refstr_alloc(size_t len)
{
	struct gsh_refstr *gr;

	gr = gsh_malloc(sizeof(*gr) + len);
	urcu_ref_init(&gr->gr_ref);
	return gr;
}

void gsh_refstr_release(struct urcu_ref *ref)
{
	struct gsh_refstr *gr = container_of(ref, struct gsh_refstr, gr_ref);

	LogFullDebug(COMPONENT_EXPORT,
		     "Releasing refstr %s", gr->gr_val);

	gsh_free(gr);
}
