/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2018 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * @file mdcache_debug.h
 * @brief MDCache debug interface
 *
 * This is a debug interface for MDCACHE.  It should *not* be used for any
 * production codepaths, but only for debugging and white-box-testing.
 */

#ifndef MDCACHE_DEBUG_H
#define MDCACHE_DEBUG_H

#include "mdcache_int.h"
#include "mdcache_lru.h"

/**
 * @brief Get the sub-FSAL handle from an MDCACHE handle
 *
 * This allows access down the stack to the sub-FSAL's handle, given an MDCACHE
 * handle.  It can be used to bypass MDCACHE for a debug operation.
 *
 * @note Keep a ref on the MDCACHE handle for the duration of the use of the
 * sub-FSAL's handle, or it might be freed from under you.
 *
 * @param[in] obj_hdl	MDCACHE handle
 * @return sub-FSAL handle on success, NULL on failure
 */
struct fsal_obj_handle *mdcdb_get_sub_handle(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);

	return entry->sub_handle;
}


#endif /* MDCACHE_DEBUG_H */
