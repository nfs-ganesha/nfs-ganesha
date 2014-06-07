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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "lustre_methods.h"
#include "fsal_handle.h"
#include <stdbool.h>
#include "shook_svr.h"

/**
 *
 * \file    shook_glue.c
 *
 */

fsal_status_t lustre_shook_restore(struct fsal_obj_handle *obj_hdl,
				   bool do_truncate,
				   int *trunc_done)
{
	/* call "shook restore" if file is not online
	* or "shook restore_trunc" if file is not online and openflag
	* includes O_TRUNC
	*/
	struct lustre_fsal_obj_handle *myself = NULL;
	struct lustre_filesystem *lustre_fs = NULL;
	shook_state state;
	char fsal_path[MAXPATHLEN];
	int errsv = 0;
	int rc = 0;

	if (trunc_done)
		*trunc_done = 0;

	myself =
	    container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	lustre_fs =  obj_hdl->fs->private;

	rc = lustre_handle_to_path(obj_hdl->fs->path,
					myself->handle,
					fsal_path);
	if (rc < 0) {
		LogCrit(COMPONENT_FSAL,
			    "lustre_handle_to_path returned %d",
			    rc);

		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	rc = shook_get_status(fsal_path, &state, 0);
	if (rc) {
		LogEvent(COMPONENT_FSAL,
			 "Error retrieving shook status of %s: %s",
			 fsal_path, strerror(-rc));

		return fsalstat(posix2fsal_error(-rc), rc);
	} else if (state != SS_ONLINE) {
		LogInfo(COMPONENT_FSAL,
			"File is offline: triggering shook restore");

		if (do_truncate) {
			rc = truncate(fsal_path, 0);
			errsv = errno;

			if (rc == 0) {
				/* use a short timeout of 2s */
				rc = shook_server_call(SA_RESTORE_TRUNC,
						       lustre_fs->fsname,
						       &myself->handle->fid,
						       2);
				if (rc)
					return fsalstat(posix2fsal_error(-rc),
							-rc);
				else {
					/* check that file is online,
					 * else operation
					 * is still in progress
					 * => return err jukebox */
					rc = shook_get_status(fsal_path,
								  &state,
								  FALSE);

					if (rc) {
						LogEvent(COMPONENT_FSAL,
							 "Error retrieving shook status of %s: %s",
							 fsal_path,
							 strerror(-rc));
						return fsalstat(
							  posix2fsal_error(-rc),
							  -rc);
					} else if (state != SS_ONLINE)
						return fsalstat(ERR_FSAL_DELAY,
								 -rc);
					/* else: OK */
				}
				if (trunc_done != NULL)
					*trunc_done = 1;
			} else {
				 if (errsv == ENOENT)
					return fsalstat(ERR_FSAL_STALE, errsv);
				else
					return fsalstat(
						 posix2fsal_error(errsv),
						 errsv);
			}
			/* continue to open */
		} else {
			/* trigger restore. Give it a chance to retrieve
			 * the file in less than a second.
			 * Else, it returns ETIME (converted to ERR_DELAY) */
			rc = shook_server_call(SA_RESTORE,
						   lustre_fs->fsname,
						   &myself->handle->fid,
						   1);
			if (rc)
				return fsalstat(posix2fsal_error(-rc),
						-rc);
			else {
				/* check that file is online
				 * if not, operation is still
				 * in progress: return err jukebox */
				rc = shook_get_status(fsal_path,
							  &state,
							  FALSE);
				if (rc) {
					LogEvent(COMPONENT_FSAL,
						 "Error retrieving shook status of %s: %s",
						 fsal_path, strerror(-rc));
					return fsalstat(
						posix2fsal_error(-rc),
						-rc);
				} else if (state != SS_ONLINE)
					return fsalstat(ERR_FSAL_DELAY,
							-rc);
				/* else: OK */
			}
			/* if rc = 0, file can be opened */
		}
	}
	/* else: we can open file directly */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
