/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

#ifndef DBUS_PRIV_H
#define DBUS_PRIV_H

bool dbus_proc_property(const char *method, DBusMessage *msg,
			DBusMessage *reply, DBusError *error,
			struct gsh_dbus_interface **interfaces);

int dbus_append_signal_string(DBusMessageIter *args, void *sig_string);

int dbus_send_signal(DBusConnection *conn, char *obj_name, char *int_name,
		     char *sig_name, int (*payload) (DBusMessageIter *signal,
						     void *args),
		     void *sig_args);

#endif				/* DBUS_PRIV_H */
