// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright © 2017, Red Hat, Inc.
 * Author: Matt Benjamin <mbenjamin@redhat.com>
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
 * -------------
 */

/**
 * @file   FSAL_RGW/up.c
 * @author Matt Benjamin <mbenjamin@redhat.com>
 * @date   Fri Jan 19 18:07:01 2017
 *
 * @brief Upcalls
 *
 * Use new generic invalidate hook to drive upcalls.
 */

#include <fcntl.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"

/**
 * @brief Invalidate an inode (dispatch upcall)
 *
 * This function terminates an invalidate upcall from librgw.  Since
 * upcalls are asynchronous, no upcall thread is required.
 *
 * @param[in] cmount The mount context
 * @param[in] fh_hk The object being invalidated
 * @param[in] arg Opaque argument, currently a pointer to export
 *
 * @return FSAL status codes.
 */

void rgw_fs_invalidate(void *handle, struct rgw_fh_hk fh_hk)
{
	struct rgw_export *export = (struct rgw_export *) handle;
	const struct fsal_up_vector *up_ops;

	LogFullDebug(COMPONENT_FSAL_UP,
		"%s: invalidate on fh_hk %" PRIu64 ":%" PRIu64 "\n",
		__func__, fh_hk.bucket, fh_hk.object);

	if (!export) {
		LogMajor(COMPONENT_FSAL_UP,
			"up/invalidate: called w/nil export");
		return;
	}

	up_ops = export->export.up_ops;
	if (!up_ops) {
		LogMajor(COMPONENT_FSAL_UP,
			"up/invalidate: nil FSAL_UP ops vector");
		return;
	}

	fsal_status_t status;
	struct gsh_buffdesc fh_desc;

	fh_desc.addr = &fh_hk;
	fh_desc.len = sizeof(struct rgw_fh_hk);

	/* invalidate me, my man */
	status = up_ops->invalidate(up_ops, &fh_desc, FSAL_UP_INVALIDATE_CACHE);
	if (FSAL_IS_ERROR(status)) {
		LogMajor(COMPONENT_FSAL_UP,
			"up/invalidate: error invalidating fh_hk %"
			PRIu64 ":%" PRIu64 "\n",
			fh_hk.bucket, fh_hk.object);
	}
}
