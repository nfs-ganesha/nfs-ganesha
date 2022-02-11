// SPDX-License-Identifier: LGPL-3.0-or-later
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

int dbus_heartbeat_cb(void *arg)
{
	SetNameFunction("dbus_heartbeat");
	int err = 0;
	int rc = BCAST_STATUS_OK;
	dbus_bool_t ishealthy = nfs_health();

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
	add_dbus_broadcast(&dbus_heartbeat_cb,
			   NULL,
			   nfs_param.core_param.heartbeat_freq*NS_PER_MSEC,
			   BCAST_FOREVER);
}
