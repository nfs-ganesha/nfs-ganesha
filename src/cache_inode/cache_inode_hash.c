/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2013, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * -------------
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_hash.h"

/**
 * @addtogroup cache_inode
 * @{
 */

/**
 *
 * @file cache_inode_hash.c
 * @author Matt Benjamin
 * @brief Cache inode hashed dictionary package
 *
 * @section Description
 *
 * This module exports an interface for efficient lookup of cache entries
 * by file handle.  Refactored from the prior abstract HashTable
 * implementation.
 */

struct cih_lookup_table cih_fhcache;
static bool initialized;

/**
 * @brief Initialize the package.
 */
void
cih_pkginit(void)
{
	pthread_rwlockattr_t rwlock_attr;
	cih_partition_t *cp;
	uint32_t npart;
	uint32_t cache_sz = 32767;	/* XXX */
	int ix;

	/* avoid writer starvation */
	pthread_rwlockattr_init(&rwlock_attr);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&rwlock_attr,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	npart = cache_param.nparts;
	cih_fhcache.npart = npart;
	cih_fhcache.partition = gsh_calloc(npart, sizeof(cih_partition_t));
	for (ix = 0; ix < npart; ++ix) {
		cp = &cih_fhcache.partition[ix];
		cp->part_ix = ix;
		pthread_rwlock_init(&cp->lock, &rwlock_attr);
		avltree_init(&cp->t, cih_fh_cmpf, 0 /* must be 0 */);
		cih_fhcache.cache_sz = cache_sz;
		cp->cache = gsh_calloc(cache_sz, sizeof(struct avltree_node *));
	}
	initialized = true;
}

/**
 * @brief Destroy the package.
 */
void
cih_pkgdestroy(void)
{
	/* Index over partitions */
	int ix = 0;

	/* Destroy the partitions, warning if not empty */
	for (ix = 0; ix < cih_fhcache.npart; ++ix) {
		if (avltree_first(&cih_fhcache.partition[ix].t) != NULL)
			LogMajor(COMPONENT_CACHE_INODE,
				 "Cache inode AVL tree not empty");
		pthread_rwlock_destroy(&cih_fhcache.partition[ix].lock);
		gsh_free(cih_fhcache.partition[ix].cache);
	}
	/* Destroy the partition table */
	gsh_free(cih_fhcache.partition);
	cih_fhcache.partition = NULL;
	initialized = false;
}

/** @} */
