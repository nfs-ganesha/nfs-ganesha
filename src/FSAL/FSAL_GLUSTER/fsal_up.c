/*
 * Copyright (C) Red Hat  Inc., 2015
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
 * License along with this library.
 *
 * ---------------------------------------
 */

/**
 * @file    fsal_up.c
 *
 * @author  Soumya Koduri <skoduri@redhat.com>
 *
 * @brief   Upcall Interface for FSAL_GLUSTER
 *
 */

#include "config.h"

#include "fsal.h"
#include "fsal_up.h"
#include "gluster_internal.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>

int upcall_inode_invalidate(struct glusterfs_export *glfsexport,
			     struct glfs_object *object)
{
	int	     rc                             = -1;
	glfs_t          *fs                         = NULL;
	char            vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
	unsigned char   globjhdl[GLAPI_HANDLE_LENGTH];
	struct gsh_buffdesc         key;
	const struct fsal_up_vector *event_func;
	fsal_status_t fsal_status = {0, 0};

	fs = glfsexport->gl_fs;
	if (!fs) {
		LogCrit(COMPONENT_FSAL_UP,
			"Invalid fs object of the glfsexport(%p)",
			 glfsexport);
		goto out;
	}

	rc = glfs_h_extract_handle(object, globjhdl+GLAPI_UUID_LENGTH,
				   GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		LogDebug(COMPONENT_FSAL_UP,
			 "glfs_h_extract_handle failed %p",
			 fs);
		goto out;
	}

	rc = glfs_get_volumeid(fs, vol_uuid,
			       GLAPI_UUID_LENGTH);
	if (rc < 0) {
		LogDebug(COMPONENT_FSAL_UP,
			 "glfs_get_volumeid failed %p",
			 fs);
		goto out;
	}

	memcpy(globjhdl, vol_uuid, GLAPI_UUID_LENGTH);
	key.addr = &globjhdl;
	key.len = GLAPI_HANDLE_LENGTH;

	LogDebug(COMPONENT_FSAL_UP, "Received event to process for %p",
		 fs);

	event_func = glfsexport->export.up_ops;

	fsal_status = event_func->invalidate_close(
					event_func->up_export,
					&key,
					FSAL_UP_INVALIDATE_CACHE);

	rc = fsal_status.major;
	if (FSAL_IS_ERROR(fsal_status) && fsal_status.major != ERR_FSAL_NOENT) {
		LogWarn(COMPONENT_FSAL_UP,
			"Inode_Invalidate event could not be processed for fd %p, rc %d",
			glfsexport->gl_fs, rc);
	}

out:
	glfs_h_close(object);
	return rc;
}

void *GLUSTERFSAL_UP_Thread(void *Arg)
{
	struct glusterfs_export     *glfsexport                 = Arg;
	const struct fsal_up_vector *event_func;
	char                        thr_name[16];
	int                         rc                          = 0;
	struct callback_arg         callback;
	struct callback_inode_arg   *cbk_inode_arg              = NULL;
	int                         reason                      = 0;
	int                         retry                       = 0;
	int                         errsv                       = 0;


	snprintf(thr_name, sizeof(thr_name),
		 "fsal_up_%p",
		 glfsexport->gl_fs);
	SetNameFunction(thr_name);

	/* Set the FSAL UP functions that will be used to process events. */
	event_func = glfsexport->export.up_ops;

	if (event_func == NULL) {
		LogFatal(COMPONENT_FSAL_UP,
			 "FSAL up vector does not exist. Can not continue.");
		gsh_free(Arg);
		return NULL;
	}

	LogFullDebug(COMPONENT_FSAL_UP,
		     "Initializing FSAL Callback context for %p.",
		     glfsexport->gl_fs);

	if (!glfsexport->gl_fs) {
		LogCrit(COMPONENT_FSAL_UP,
			"FSAL Callback interface - Null glfs context.");
		goto out;
	}

	callback.fs = glfsexport->gl_fs;

	/* Start querying for events and processing. */
	/** @todo : Do batch processing instead */
	while (!atomic_fetch_int8_t(&glfsexport->destroy_mode)) {
		LogFullDebug(COMPONENT_FSAL_UP,
			     "Requesting event from FSAL Callback interface for %p.",
			     glfsexport->gl_fs);

		callback.reason = 0;

		rc = glfs_h_poll_upcall(glfsexport->gl_fs, &callback);
		errsv = errno;
		reason = callback.reason;

		if (rc != 0) {
			/* if ENOMEM retry for couple of times
			 * and then exit
			 */
			if ((errsv == ENOMEM) && (retry < 10)) {
				sleep(1);
				retry++;
				continue;
			} else {
				switch (errsv) {
				case ENOMEM:
					LogMajor(COMPONENT_FSAL_UP,
						 "Memory allocation failed during poll_upcall for (%p).",
						 glfsexport->gl_fs);
					abort();

				case ENOTSUP:
					LogEvent(COMPONENT_FSAL_UP,
						 "Upcall feature is not supported for (%p).",
						 glfsexport->gl_fs);
					break;
				default:
					LogCrit(COMPONENT_FSAL_UP,
						"Poll upcall failed for %p. rc %d errno %d (%s) reason %d",
						glfsexport->gl_fs, rc, errsv,
						strerror(errsv), reason);
				}
				return NULL;
			}
		}

		retry = 0;

		LogFullDebug(COMPONENT_FSAL_UP,
			     "Received upcall event: reason(%d)",
			     reason);

		/* Decide what type of event this is
		 * inode update / invalidate? */
		switch (reason) {
		case GFAPI_CBK_EVENT_NULL:
			usleep(10);
			continue;
		case GFAPI_INODE_INVALIDATE:
			cbk_inode_arg =
				(struct callback_inode_arg *)callback.event_arg;

			if (!cbk_inode_arg) {
				/* Could be ENOMEM issues. continue */
				LogWarn(COMPONENT_FSAL_UP,
					"Received NULL upcall event arg");
				break;
			}

			if (cbk_inode_arg->object)
				upcall_inode_invalidate(glfsexport,
							cbk_inode_arg->object);
			if (cbk_inode_arg->p_object)
				upcall_inode_invalidate(glfsexport,
						  cbk_inode_arg->p_object);
			if (cbk_inode_arg->oldp_object)
				upcall_inode_invalidate(glfsexport,
						  cbk_inode_arg->oldp_object);
			break;
		default:
			LogWarn(COMPONENT_FSAL_UP, "Unknown event: %d", reason);
			continue;
		}
		if (cbk_inode_arg) {
			free(cbk_inode_arg);
			cbk_inode_arg = NULL;
		}
		callback.event_arg = NULL;
	}

out:
	return NULL;
}				/* GLUSTERFSFSAL_UP_Thread */
