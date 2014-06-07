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
 * @file cache_inode_statfs.c
 * @brief Get and eventually cache an entry.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include "hashtable.h"
#include "log.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_exports.h"
#include "export_mgr.h"

cache_inode_status_t
cache_inode_statfs(cache_entry_t *entry,
		   fsal_dynamicfsinfo_t *dynamicinfo)
{
	fsal_status_t fsal_status;
	struct fsal_export *export;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	export = op_ctx->export->fsal_export;
	/* Get FSAL to get dynamic info */
	fsal_status =
	    export->ops->get_fs_dynamic_info(export, entry->obj_handle,
					     dynamicinfo);
	if (FSAL_IS_ERROR(fsal_status)) {
		status = cache_inode_error_convert(fsal_status);
		if (fsal_status.major == ERR_FSAL_STALE) {
			LogEvent(COMPONENT_CACHE_INODE,
				 "FSAL returned STALE from fsinfo");
			cache_inode_kill_entry(entry);
		}
	}
	LogFullDebug(COMPONENT_CACHE_INODE,
		     "cache_inode_statfs: dynamicinfo: {total_bytes = %" PRIu64
		     ", " "free_bytes = %" PRIu64 ", avail_bytes = %" PRIu64
		     ", total_files = %" PRIu64 ", free_files = %" PRIu64
		     ", avail_files = %" PRIu64 "}", dynamicinfo->total_bytes,
		     dynamicinfo->free_bytes, dynamicinfo->avail_bytes,
		     dynamicinfo->total_files, dynamicinfo->free_files,
		     dynamicinfo->avail_files);
	return status;
}				/* cache_inode_statfs */

/** @} */
