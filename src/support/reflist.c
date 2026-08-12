// SPDX-License-Identifier: LGPL-3.0-or-later
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

#include "config.h"
#include <stddef.h>
#include <urcu/ref.h>
#include "gsh_refstr.h"
#include "abstract_mem.h"
#include "gsh_list.h"
#include "gsh_reflist.h"

struct gsh_reflist *gsh_reflist_alloc(glist_release release,
				      mem_components_t comp)
{
	struct gsh_reflist *grl;

	grl = gsh_malloc(sizeof(*grl), comp);
	urcu_ref_init(&grl->grl_ref);
	grl->grl_comp = comp;
	grl->grl_release = release;

	/* Make the list empty initially */
	glist_init(&grl->grl_list);

	return grl;
}

void gsh_reflist_release(struct urcu_ref *ref)
{
	struct gsh_reflist *grl;
	struct glist_head *glist, *glistn;

	grl = container_of(ref, struct gsh_reflist, grl_ref);

	glist_for_each_safe(glist, glistn, &grl->grl_list) {
		/* Remove entry from list */
		glist_del(glist);

		/* Call the release function to free any associated memory */
		grl->grl_release(glist, grl->grl_comp);
	}

	gsh_free(grl, grl->grl_comp);
}
