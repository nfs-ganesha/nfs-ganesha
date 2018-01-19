/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2017-2018)
 * contributeur : Patrice LUCAS patrice.lucas@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#include "config.h"
#include "../vfs_methods.h"

#include "fsal_convert.h"

#ifdef USE_LLAPI
	#include <lustre/lustreapi.h>
#endif

/**
 * @brief Call "lustre_hsm restore" if file is released.
 *
 * @param[in] fd System file descriptor on open file to check
 *
 * @return ERR_FSAL_DELAY if we call a restore, else ERR_FSAL_NO_ERROR.
 */
fsal_status_t check_hsm_by_fd(int fd)
{
	struct vfs_fsal_export *vfs_export;

	vfs_export = container_of(op_ctx->fsal_export,
		struct vfs_fsal_export, export);

	/* check async_hsm_restore option */
	if (!vfs_export->async_hsm_restore) {
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

#ifndef USE_LLAPI
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
#else
	struct hsm_user_state hus;
	int rc = 0;

	rc = llapi_hsm_state_get_fd(fd, &hus);
	if (rc) {
		LogEvent(COMPONENT_FSAL,
			 "Error retrieving lustre_hsm status : %s",
			 strerror(-rc));
		return fsalstat(posix2fsal_error(-rc), rc);
	}

	if (!(hus.hus_states & HS_RELEASED))
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	lustre_fid fid;

	/* restore file in LUSTRE */
	struct hsm_user_request *hur;

	LogInfo(COMPONENT_FSAL,
		"File is offline: triggering lustre_hsm restore");
	/* allocating request : one item, no extra data */
	hur = llapi_hsm_user_request_alloc(1, 0);
	if (hur == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Error allocating hsm_user_request");
		return fsalstat(ERR_FSAL_NOMEM, ENOMEM);
	}

	/* filling request */
	hur->hur_request.hr_action = HUA_RESTORE; /*restore action*/
	hur->hur_request.hr_archive_id = 0; /*only used for archiving*/
	hur->hur_request.hr_flags = 0; /*no flags*/
	hur->hur_request.hr_itemcount = 1; /*only one file*/
	hur->hur_request.hr_data_len = 0; /*no extra data*/

	/* getting fid */
	rc = llapi_fd2fid(fd, &fid);
	if (rc) {
		LogEvent(COMPONENT_FSAL,
			 "Error retrieving fid from fd : %s",
			 strerror(-rc));
		return fsalstat(posix2fsal_error(-rc), rc);
	}

	/* filling item */
	hur->hur_user_item[0].hui_fid = fid;
	hur->hur_user_item[0].hui_extent.offset = 0; /*file from start*/
	hur->hur_user_item[0].hui_extent.length = -1; /*whole file*/
	rc = llapi_hsm_request(vfs_export->root_fs->path, hur);
	if (rc) {
		LogEvent(COMPONENT_FSAL,
			 "Error requesting a restore : %s",
			 strerror(-rc));
		return fsalstat(posix2fsal_error(-rc), rc);
	}

	/* return ERR_FSAL_DELAY */
	return fsalstat(ERR_FSAL_DELAY, rc);
#endif /*USE_LLAPI*/
}
