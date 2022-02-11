// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2013, Panasas Inc.
 * Contributor : Jim Lieb <jlieb@panasas.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "gsh_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "nfs_rpc_callback.h"
#include "gsh_dbus.h"
#include <os/memstream.h>
#include "dbus_priv.h"

/**
 * @TODO@ This is a preliminary implementation to get
 * the basics down.  Subject to change when we get real signals to send...
 */

/**
 * @brief Append a string to a signal
 */

int dbus_append_signal_string(DBusMessageIter *args, void *sig_string)
{
	char *sigvalue = (char *)sig_string;

	if (!dbus_message_iter_append_basic(args, DBUS_TYPE_STRING, &sigvalue))
		return ENOMEM;
	return 0;
}

/**
 * @brief Process a signal and send it
 */

int dbus_send_signal(DBusConnection *conn, char *obj_name, char *int_name,
		     char *sig_name, int (*payload) (DBusMessageIter *signal,
						     void *args),
		     void *sig_args)
{
	static dbus_uint32_t serial;
	DBusMessage *msg;
	DBusMessageIter sig_iter;
	int retval = 0;

	msg = dbus_message_new_signal(obj_name, int_name, sig_name);
	if (msg == NULL)
		return EINVAL;
	dbus_message_iter_init_append(msg, &sig_iter);
	retval = payload(&sig_iter, sig_args);
	if (retval != 0)
		return retval;
	if (!dbus_connection_send(conn, msg, &serial))
		return ENOMEM;
	dbus_connection_flush(conn);
	dbus_message_unref(msg);
	return retval;
}
