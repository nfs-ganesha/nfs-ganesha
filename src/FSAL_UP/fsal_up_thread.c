/*
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
 * ---------------------------------------
 */

/**
 * @file fsal_up_thread.c
 */

#include "config.h"
#define FSAL_UP_THREAD_C

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "fsal_up.h"
#include "err_fsal.h"
#include "cache_inode_lru.h"

pool_t *fsal_up_pool = NULL;
struct fridgethr *fsal_up_fridge = NULL;

/**
 * @brief Clean up after an event is processed
 *
 * Currently we just release our claim on the export and free the
 * event.
 *
 * @param[in] ctx The thread context holding the event
 */

static void fsal_up_event_cleanup(struct fridgethr_context *ctx)
{
	struct fsal_up_event *e = ctx->arg;
	e->file.export->ops->put(e->file.export);
	fsal_up_free_event(e);
}


/**
 * @brief Submit an upcall event
 *
 * This function submits an upcall event.  The event type, event data,
 * and file must be filled out as appropriate, and the upcall function
 * vector must be set to upcall vector supplied to create_export.
 *
 * @param[in] event The event to submit
 *
 * @retval 0 Operation submitted successfully.
 * @retval EINVAL Operation malformed.
 * @retval EPIPE Upcall thread not running/shutting down.
 * @retval Other codes as specified by _imm call.
 */

int fsal_up_submit(struct fsal_up_event *event)
{
	int rc = 0;

	if (!event->functions ||
	    !event->file.export ||
	    (event->type < FSAL_UP_EVENT_LOCK_GRANT) ||
	    (event->type > FSAL_UP_EVENT_DELEGATION_RECALL)) {
		return EINVAL;
	}

	event->file.export->ops->get(event->file.export);

	if (event->functions->imm[event->type]) {
		rc = event->functions->imm[event->type](event);
	}

	if (rc != 0) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Immediate function failed: %d",
			 rc);
		event->file.export->ops->put(event->file.export);
		fsal_up_free_event(event);
		return rc;
	}

	if (event->functions->queue[event->type]) {
		rc = fridgethr_submit(fsal_up_fridge,
				      event->functions->queue[event->type],
				      event);
	} else {
		event->file.export->ops->put(event->file.export);
		fsal_up_free_event(event);
	}

	if (rc != 0) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Failed submitting event to thread fridge: %d",
			 rc);
		event->file.export->ops->put(event->file.export);
		fsal_up_free_event(event);
		return rc;
	}
	return 0;
}

/**
 * @brief Initialize the FSAL up-call system
 *
 * This function initializes the FSAL up-call system.
 *
 * @return 0 or POSIX errors.
 */

int fsal_up_init(void)
{
	struct fridgethr_params frp;
	int rc = 0;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.thread_delay = 600;
	frp.deferment = fridgethr_defer_queue;
	frp.task_cleanup = fsal_up_event_cleanup;

	/* Allocation of the FSAL UP pool */
	fsal_up_pool = pool_init("FSAL UP Data Pool",
				 sizeof(struct fsal_up_event),
				 pool_basic_substrate,
				 NULL,
				 NULL,
				 NULL);

	if (fsal_up_pool == NULL) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Error while initializing FSAL UP event pool");
		return ENOMEM;
	}

	rc = fridgethr_init(&fsal_up_fridge,
			    "FSAL UP",
			    &frp);

	if (rc != 0) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Error initializing FSAL UP thread fridge: %d",
			 rc);
	}

	return rc;
}

/**
 * @brief Shut down the FSAL upcall thread
 *
 * This function shuts down the FSAL upcall thread, returning when it
 * has exited.  The thread is shut down in an orderly fashion and
 * allowed to queued tasks.
 *
 * @retval 0 if the thread is shut down successfully.
 * @retval EBUSY if someone else has already signalled for the thread
 *         to shut down.
 * @retval EPIPE if the thread is not running.
 * @retval Errors from pthread_join.
 */

int fsal_up_shutdown(void)
{
	int rc = fridgethr_sync_command(fsal_up_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(fsal_up_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_FSAL_UP,
			 "Failed shutting down upcall thread: %d",
			 rc);
	}
	return rc;
}

struct fsal_up_event *fsal_up_alloc_event(void)
{
	if (fsal_up_pool) {
		return pool_alloc(fsal_up_pool, NULL);
	} else {
		return NULL;
	}
}

void fsal_up_free_event(struct fsal_up_event *event)
{
	if (event->file.key.addr) {
		gsh_free(event->file.key.addr);
		event->file.key.addr = NULL;
	}
	if (event->file.export) {
		event->file.export->ops->put(event->file.export);
		event->file.export = NULL;
	}
	pool_free(fsal_up_pool, event);
}
