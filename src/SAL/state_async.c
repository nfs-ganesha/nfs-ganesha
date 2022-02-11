// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file  state_async.c
 * @brief Management of SAL asynchronous processing
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "sal_functions.h"
#include "fridgethr.h"
#include "gsh_config.h"
#include "nfs_core.h"

struct fridgethr *state_async_fridge;
struct fridgethr *state_poll_fridge;

/**
 * @brief Process a blocked lock request
 *
 * We use this wrapper so we can void rewriting stuff.  We can change
 * this later.
 *
 * @param[in] ctx Thread fridge context, containing arguments.
 */

static void state_blocked_lock_caller(struct fridgethr_context *ctx)
{
	state_block_data_t *block = ctx->arg;
	struct gsh_export *export;
	bool set_op_ctx = false;
	struct req_op_context op_context;

	export = block->sbd_lock_entry->sle_export;
	if (export_ready(export)) {
		get_gsh_export_ref(export);
		/* Initialize a root context, need to get a valid export. */
		init_op_context(&op_context, export, export->fsal_export,
				NULL, 0, 0, UNKNOWN_REQUEST);
		set_op_ctx = true;
	}

	process_blocked_lock_upcall(block);

	if (set_op_ctx)
		release_op_context();
}

/**
 * @brief Process an async request
 *
 * We use this wrapper so we can avoid having to rewrite every async
 * func.  Later on we might want to remove it.
 *
 * @param[in] ctx Thread fridge context, containing arguments.
 */
static void state_async_func_caller(struct fridgethr_context *ctx)
{
	state_async_queue_t *entry = ctx->arg;

	entry->state_async_func(entry);
}

/**
 * @brief Schedule an asynchronous action
 *
 * @param[in] arg Request to schedule
 *
 * @return State status.
 */
state_status_t state_async_schedule(state_async_queue_t *arg)
{
	int rc;

	LogFullDebug(COMPONENT_STATE, "Schedule %p", arg);

	rc = fridgethr_submit(state_async_fridge, state_async_func_caller, arg);

	if (rc != 0)
		LogCrit(COMPONENT_STATE, "Unable to schedule request: %d", rc);

	return rc == 0 ? STATE_SUCCESS : STATE_SIGNAL_ERROR;
}

/**
 * @brief Schedule a lock notification
 *
 * @param[in] block Lock to schedule
 *
 * @return State status.
 */
state_status_t state_block_schedule(state_block_data_t *block)
{
	int rc;

	LogFullDebug(COMPONENT_STATE, "Schedule notification %p", block);

	rc = fridgethr_submit(state_async_fridge, state_blocked_lock_caller,
			      block);

	if (rc != 0)
		LogMajor(COMPONENT_STATE, "Unable to schedule request: %d", rc);

	return rc == 0 ? STATE_SUCCESS : STATE_SIGNAL_ERROR;
}

/**
 * @brief Initialize asynchronous request system
 *
 * @return State status.
 */
state_status_t state_async_init(void)
{
	int rc = 0;
	struct fridgethr_params frp;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.deferment = fridgethr_defer_queue;

	rc = fridgethr_init(&state_async_fridge, "State_Async", &frp);

	if (rc != 0) {
		LogMajor(COMPONENT_STATE,
			 "Unable to initialize state async thread fridge: %d",
			 rc);
		return STATE_INIT_ENTRY_FAILED;
	}

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.thr_min = 1;
	frp.thread_delay = nfs_param.core_param.blocked_lock_poller_interval;
	frp.flavor = fridgethr_flavor_looper;

	rc = fridgethr_init(&state_poll_fridge, "state_poll", &frp);

	if (rc != 0) {
		LogMajor(COMPONENT_STATE,
			 "Unable to initialize state blocked lock polling thread fridge: %d",
			 rc);
		return STATE_INIT_ENTRY_FAILED;
	}

	rc = fridgethr_submit(state_poll_fridge, blocked_lock_polling, NULL);

	if (rc != 0) {
		LogMajor(COMPONENT_STATE,
			 "Unable to start blocked lock polling thread, error code %d.",
			 rc);
		return STATE_INIT_ENTRY_FAILED;
	}

	return STATE_SUCCESS;
}

/**
 * @brief Shut down asynchronous request system
 *
 * @return State status.
 */
state_status_t state_async_shutdown(void)
{
	int rc1, rc2;

	rc1 = fridgethr_sync_command(state_async_fridge,
				     fridgethr_comm_stop,
				     120);

	if (rc1 == ETIMEDOUT) {
		LogMajor(COMPONENT_STATE,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(state_async_fridge);
	} else if (rc1 != 0) {
		LogMajor(COMPONENT_STATE,
			 "Failed shutting down state async thread: %d", rc1);
	}

	rc2 = fridgethr_sync_command(state_poll_fridge,
				     fridgethr_comm_stop,
				     120);

	if (rc2 == ETIMEDOUT) {
		LogMajor(COMPONENT_STATE,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(state_poll_fridge);
	} else if (rc2 != 0) {
		LogMajor(COMPONENT_STATE,
			 "Failed shutting down state blocked lock polling thread: %d",
			 rc2);
	}

	return ((rc1 == 0) && (rc2 == 0)) ? STATE_SUCCESS : STATE_SIGNAL_ERROR;
}

/** @} */
