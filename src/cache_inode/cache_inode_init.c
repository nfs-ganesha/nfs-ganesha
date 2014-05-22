/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 */

/**
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file cache_inode_init.c
 * @brief Init the cache_inode.
 */

#include "config.h"
#include "log.h"
#include "hashtable.h"
#include "cache_inode.h"
#include "cache_inode_hash.h"

/**
 *
 * @brief Initialize the caching layer
 *
 * This function initializes the memory pools, lookup table, and weakref
 * table used for cache management.
 *
 * @return CACHE_INODE_SUCCESS or errors.
 *
 */
cache_inode_status_t
cache_inode_init(void)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	cache_inode_entry_pool =
	    pool_init("Entry Pool", sizeof(cache_entry_t), pool_basic_substrate,
		      NULL, NULL, NULL);
	if (!(cache_inode_entry_pool)) {
		LogCrit(COMPONENT_CACHE_INODE, "Can't init Entry Pool");
		status = CACHE_INODE_INVALID_ARGUMENT;
	}

	cih_pkginit();

	return status;
}				/* cache_inode_init */

/**
 * @brief Destroy the cache inode avl tree and pool.
 *
 */

void
cache_inode_destroyer(void)
{
	/* Destroy the cache inode AVL tree */
	cih_pkgdestroy();

	/* Destroy the cache inode entry pool */
	pool_destroy(cache_inode_entry_pool);
}

/** @} */
