// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright 2018 VMware, Inc.
 * Contributor: Sriram Patil <sriramp@vmware.com>
 *
 * --------------------------
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 *
 */

#include <errno.h>
#include <string.h>

#include "nfs4_fs_locations.h"
#include "fsal_types.h"
#include "common_utils.h"

static fsal_fs_locations_t *nfs4_fs_locations_alloc(const unsigned int count)
{
	fsal_fs_locations_t *fs_locations;

	fs_locations = gsh_calloc(1, sizeof(fsal_fs_locations_t));
	if (count)
		fs_locations->server = gsh_calloc(count, sizeof(utf8string));
	if (pthread_rwlock_init(&(fs_locations->lock), NULL) != 0) {
		nfs4_fs_locations_free(fs_locations);
		LogCrit(COMPONENT_NFS_V4,
			"New fs locations RW lock init returned %d (%s)", errno,
			strerror(errno));
		return NULL;
	}

	return fs_locations;
}

void nfs4_fs_locations_free(fsal_fs_locations_t *fs_locations)
{
	unsigned int i;

	if (!fs_locations)
		return;

	gsh_free(fs_locations->fs_root);
	gsh_free(fs_locations->rootpath);

	for (i = 0; i < fs_locations->nservers; ++i)
		gsh_free(fs_locations->server[i].utf8string_val);

	gsh_free(fs_locations->server);
	gsh_free(fs_locations);
}

void nfs4_fs_locations_get_ref(fsal_fs_locations_t *fs_locations)
{
	PTHREAD_RWLOCK_wrlock(&fs_locations->lock);
	fs_locations->ref++;
	LogFullDebug(COMPONENT_NFS_V4, "(fs_locations, ref) = (%p, %u)",
		     fs_locations, fs_locations->ref);
	PTHREAD_RWLOCK_unlock(&fs_locations->lock);
}

/* Must be called with lock held */
static void nfs4_fs_locations_put_ref(fsal_fs_locations_t *fs_locations)
{
	fs_locations->ref--;
	LogFullDebug(COMPONENT_NFS_V4, "(fs_locations, ref) = (%p, %u)",
		     fs_locations, fs_locations->ref);
}

void nfs4_fs_locations_release(fsal_fs_locations_t *fs_locations)
{
	if (fs_locations == NULL)
		return;

	PTHREAD_RWLOCK_wrlock(&fs_locations->lock);
	if (fs_locations->ref > 1) {
		nfs4_fs_locations_put_ref(fs_locations);
		PTHREAD_RWLOCK_unlock(&fs_locations->lock);
		return;
	} else {
		LogFullDebug(COMPONENT_NFS_V4, "Free fs_locations: %p",
			     fs_locations);
	}

	PTHREAD_RWLOCK_unlock(&fs_locations->lock);

	// Releasing fs_locations
	nfs4_fs_locations_free(fs_locations);
}

fsal_fs_locations_t *nfs4_fs_locations_new(const char *fs_root,
					   const char *rootpath,
					   const unsigned int count)
{
	fsal_fs_locations_t *fs_locations;

	fs_locations = nfs4_fs_locations_alloc(count);
	if (fs_locations == NULL) {
		LogCrit(COMPONENT_NFS_V4, "Could not allocate fs_locations");
		return NULL;
	}

	fs_locations->fs_root = gsh_strdup(fs_root);
	fs_locations->rootpath = gsh_strdup(rootpath);
	fs_locations->ref = 1;

	return fs_locations;
}
