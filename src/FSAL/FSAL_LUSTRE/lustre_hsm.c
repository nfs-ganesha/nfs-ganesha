/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2016)
 * contributeur : Patrice LUCAS patrice.lucas@cea.fr
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* global include */
#include <errno.h>  /*EINVAL, ENOMEM*/
#include <string.h> /*strerror*/

/* ganesha global include */
#include "fsal.h"       /*op_ctx*/
#include "gsh_list.h"   /*container_of*/
#include "fsal_api.h"   /*fsal_obj_handle*/
#include "fsal_types.h" /*fsal_status_t, fsalstat, ERR_FSAL_XXXX*/
#include "log.h"        /*LogCrit, LogEvent, LogInfo, COMPONENT_FSAL*/
#include "fsal_convert.h" /*posix2fsal_error*/

/* fsal lustre include */
#include "fsal_handle.h"    /*lustre_handle_to_path*/
#include "lustre_methods.h" /*lustre_fsal_obj_handle, lustre_filesystem*/

/* include lustre api for lustre_hsm calls
 *
 * llapi_hsm_state_get, HS_RELEASED, hsm_user_request,
 * llapi_hsm_user_request_alloc, HUA_RESTORE, llapi_hsm_request
 */
#include <lustre/lustreapi.h>

fsal_status_t lustre_hsm_restore(struct fsal_obj_handle *obj_hdl)
{
	/* call "lustre_hsm restore" if file is not online */
	struct lustre_fsal_obj_handle *myself = NULL;
	struct hsm_user_state hus;
	char fsal_path[MAXPATHLEN];
	int rc = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle,
			      obj_handle);

	rc = lustre_handle_to_path(obj_hdl->fs->path, myself->handle,
				   fsal_path);
	if (rc < 0) {
		LogCrit(COMPONENT_FSAL,
			"lustre_handle_to_path failed in lustre_hsm_restore");
		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	rc = llapi_hsm_state_get(fsal_path, &hus);
	if (rc) {
		LogEvent(COMPONENT_FSAL,
			 "Error retrieving lustre_hsm status of %s: %s",
			 fsal_path, strerror(-rc));

		return fsalstat(posix2fsal_error(-rc), rc);
	}
	if (hus.hus_states & HS_RELEASED) {
		/* restore file in LUSTRE */
		struct hsm_user_request *hur;

		LogInfo(COMPONENT_FSAL,
			"File is offline: triggering lustre_hsm restore of %s",
			fsal_path);

		/* allocating request : one item, no extra data */
		hur = llapi_hsm_user_request_alloc(1, 0);
		if (hur == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Error allocating hsm_user_request");
			return fsalstat(ERR_FSAL_NOMEM, ENOMEM);
		}

		/* filling request */
		hur->hur_request.hr_action = HUA_RESTORE; /*restore action*/
		hur->hur_request.hr_archive_id = 0; /*only use by archiving*/
		hur->hur_request.hr_flags = 0; /*no flags*/
		hur->hur_request.hr_itemcount = 1; /*only one file*/
		hur->hur_request.hr_data_len = 0; /*no extra data*/

		/* filling item */
		hur->hur_user_item[0].hui_fid = myself->handle->fid;
		hur->hur_user_item[0].hui_extent.offset = 0; /*file from start*/
		hur->hur_user_item[0].hui_extent.length = -1; /*whole file*/

		rc = llapi_hsm_request(obj_hdl->fs->path, hur);
		if (rc) {
			LogEvent(COMPONENT_FSAL,
				 "Error requesting restore of %s: %s",
				 obj_hdl->fs->path, strerror(-rc));

			return fsalstat(posix2fsal_error(-rc), rc);
		}

		/* return ERR_FSAL_DELAY */
		return fsalstat(ERR_FSAL_DELAY, rc);
	}
	/* else: we can open file directly */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
