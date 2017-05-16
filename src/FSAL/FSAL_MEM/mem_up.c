/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright © 2017, Red Hat, Inc.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * @file   FSAL_MEM/mem_up.c
 * @author Daniel Gryniewicz <dang@redhat.com>
 *
 * @brief Upcalls
 *
 * Implement upcalls for testing purposes
 */

#include <fcntl.h>
#include <stdlib.h>
#include "fsal.h"
#include "fsal_convert.h"
#include "mem_int.h"

static struct fridgethr *mem_up_fridge;

/**
 * @brief Invalidate an object
 *
 * This function sends an invalidate for an object.  The object itself is not
 * really deleted, since there's no way to get it back, but it should allow
 * testing of the invalidate UP call.
 *
 */

static void
mem_invalidate(struct mem_fsal_export *mfe, struct mem_fsal_obj_handle *hdl)
{
	const struct fsal_up_vector *up_ops = mfe->export.up_ops;
	fsal_status_t status;
	struct gsh_buffdesc fh_desc;

	LogFullDebug(COMPONENT_FSAL_UP, "invalidating %s", hdl->m_name);

	hdl->obj_handle.obj_ops.handle_to_key(&hdl->obj_handle, &fh_desc);

	/* invalidate me, my man */
	status = up_ops->invalidate(up_ops, &fh_desc, FSAL_UP_INVALIDATE_CACHE);
	if (FSAL_IS_ERROR(status)) {
		LogMajor(COMPONENT_FSAL_UP, "error invalidating %s",
			 hdl->m_name);
	}
}

/**
 * @brief Select a random obj from an export
 *
 * @param[in] mfe	Export to select from
 * @return Obj on success, NULL on failure
 */
struct mem_fsal_obj_handle *
mem_rand_obj(struct mem_fsal_export *mfe)
{
	struct mem_fsal_obj_handle *res = NULL;
	struct glist_head *glist, *glistn;
	uint32_t n = 2;

	if (glist_empty(&mfe->mfe_objs))
		return NULL;

	srand(time(NULL));
	glist_for_each_safe(glist, glistn, &mfe->mfe_objs) {
		if (res == NULL) {
			/* Grab first entry */
			res = glist_entry(glist, struct mem_fsal_obj_handle,
					  mfo_exp_entry);
			continue;
		}

		if (rand() % n == 0) {
			/* Replace with current */
			res = glist_entry(glist, struct mem_fsal_obj_handle,
					  mfo_exp_entry);
		}
		n++;
	}

	return res;
}

/**
 * @brief Run an iteration of the UP call thread
 *
 * Each iteration exercises various UP calls.
 *
 * - Pick a random obj in each export, and invalidate it
 *
 * @param[in] ctx	Thread context
 * @return Return description
 */
static void
mem_up_run(struct fridgethr_context *ctx)
{
	struct glist_head *glist, *glistn;

	glist_for_each_safe(glist, glistn, &MEM.mem_exports) {
		struct mem_fsal_export *mfe;
		struct mem_fsal_obj_handle *hdl;

		mfe = glist_entry(glist, struct mem_fsal_export, export_entry);
		hdl = mem_rand_obj(mfe);
		if (hdl)
			mem_invalidate(mfe, hdl);
	}
}

/**
 * Initialize subsystem
 */
fsal_status_t
mem_up_pkginit(void)
{
	/* Return code from system calls */
	int code = 0;
	struct fridgethr_params frp;

	if (MEM.up_interval == 0) {
		/* Don't run up-thread */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.thr_min = 1;
	frp.thread_delay = MEM.up_interval;
	frp.flavor = fridgethr_flavor_looper;

	/* spawn MEM_UP background thread */
	code = fridgethr_init(&mem_up_fridge, "MEM_UP_fridge", &frp);
	if (code != 0) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Unable to initialize MEM_UP fridge, error code %d.",
			 code);
		return posix2fsal_status(code);
	}

	code = fridgethr_submit(mem_up_fridge, mem_up_run, NULL);
	if (code != 0) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Unable to start MEM_UP thread, error code %d.", code);
		return fsalstat(posix2fsal_error(code), code);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * Shutdown subsystem
 *
 * @return FSAL status
 */
fsal_status_t
mem_up_pkgshutdown(void)
{
	int rc = fridgethr_sync_command(mem_up_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(mem_up_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Failed shutting down MEM_UP thread: %d", rc);
	}
	return fsalstat(posix2fsal_error(rc), rc);
}
