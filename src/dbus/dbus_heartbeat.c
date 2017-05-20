/*
 * Copyright (C) IBM Inc., 2014
 * Author: Jeremy Bongio <jbongio@us.ibm.com>
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
 * -------------
 */

/*
 * @file dbus_heartbeat.c
 * @author Jeremy Bongio <jbongio@us.ibm.com>
 * @brief DBUS Heartbeat
 */

#include "config.h"

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "avltree.h"
#include "gsh_types.h"
#include "gsh_dbus.h"
#include "abstract_atomic.h"
#include "gsh_intrinsic.h"

struct _ganesha_health {
	uint32_t old_enqueue;
	uint32_t old_dequeue;
};

struct _ganesha_health healthstats;

bool get_ganesha_health(struct _ganesha_health *hstats)
{
	uint32_t newenq, newdeq;
	uint32_t dequeue_diff, enqueue_diff;
	bool healthy;

	newenq = get_enqueue_count();
	newdeq = get_dequeue_count();
	enqueue_diff = newenq - hstats->old_enqueue;
	dequeue_diff = newdeq - hstats->old_dequeue;

	/* Consider healthy and making progress if we have dequeued some
	 * requests or there is nothing to dequeue.
	 */
	healthy = dequeue_diff > 0 || enqueue_diff == 0;

	if (!healthy) {
		LogWarn(COMPONENT_DBUS,
			"Health status is unhealthy.enq new: %u, old: %u, "
			"deq new: %u, old: %u", newenq, hstats->old_enqueue,
			newdeq, hstats->old_dequeue);
	}

	hstats->old_enqueue = newenq;
	hstats->old_dequeue = newdeq;

	return healthy;
}

int dbus_heartbeat_cb(void *arg)
{
	SetNameFunction("dbus_heartbeat");
	int err = 0;
	int rc = BCAST_STATUS_OK;
	dbus_bool_t ishealthy = get_ganesha_health(&healthstats);

	if (ishealthy) {
		/* send the heartbeat pulse */
		err = gsh_dbus_broadcast(DBUS_PATH HEARTBEAT_NAME,
					 DBUS_ADMIN_IFACE,
					 HEARTBEAT_NAME,
					 DBUS_TYPE_BOOLEAN,
					 &ishealthy,
					 DBUS_TYPE_INVALID);
		if (err) {
			LogCrit(COMPONENT_DBUS,
				"heartbeat broadcast failed. err:%d",
				err);
			rc = BCAST_STATUS_WARN;
		}
	}

	return rc;
}

void init_heartbeat(void)
{
	healthstats.old_enqueue = get_enqueue_count();
	healthstats.old_dequeue = get_dequeue_count();

	add_dbus_broadcast(&dbus_heartbeat_cb,
			   NULL,
			   nfs_param.core_param.heartbeat_freq*NS_PER_MSEC,
			   BCAST_FOREVER);
}
