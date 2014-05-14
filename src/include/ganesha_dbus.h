/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#ifndef _GANESHA_DBUS_H
#define _GANESHA_DBUS_H

#include "config.h"
#include <dbus/dbus.h>
#include "log.h"

/**
 *
 * \file ganesha_dbus.h
 * \author Matt Benjamin and Lee Dobryden
 * \brief Low-level DBUS message server and callout framework.
 *
 * \section DESCRIPTION
 *
 * This module implements a very simple service provider interface for a
 * shared DBUS event loop.
 *
 * To use the service, a client implements the gsh_dbus_method_t interface,
 * then registers its callout routine(s) with gsh_dbus_register_method.
 */

/**
 * @brief Introspection contents and method dispatches.
 *
 * An interface array is passed when a path is registered by
 * a facility inside the server.  A NULL interface pointer
 * terminates the list.
 *
 * Each interface has NULL terminated arrays of properties, methods,
 * and signals.
 */

#define STATUS_REPLY      \
{                         \
	.name = "status", \
	.type = "b",      \
	.direction = "out"\
},                        \
{                         \
	.name = "error",  \
	.type = "s",      \
	.direction = "out"\
}

#define MESSAGE_REPLY		\
{				\
	.name = "message",	\
	.type = "s",		\
	.direction = "out"	\
}

#define END_ARG_LIST {NULL, NULL, NULL}

#define IPADDR_ARG       \
{                        \
	.name = "ipaddr",\
	.type = "s",     \
	.direction = "in"\
}

#define ID_ARG			\
{				\
	.name = "id",		\
	.type = "i",		\
	.direction = "in"	\
}

#define PATH_ARG		\
{				\
	.name = "path",		\
	.type = "s",		\
	.direction = "in"	\
}

#define EXPR_ARG		\
{				\
	.name = "expr",		\
	.type = "s",		\
	.direction = "in"	\
}

/* Properties list helper macros
 */

#define END_ARG_LIST {NULL, NULL, NULL}

#define END_PROPS_LIST {NULL, DBUS_PROP_READ, "", NULL, NULL}

typedef enum {
	DBUS_PROP_READ = 0,
	DBUS_PROP_WRITE,
	DBUS_PROP_READWRITE
} dbus_prop_access_t;

struct gsh_dbus_prop {
	const char *name;
	dbus_prop_access_t access;
	const char *type;
	 bool(*get) (DBusMessageIter *reply);
	 bool(*set) (DBusMessageIter *args);
};

struct gsh_dbus_arg {
	const char *name;
	const char *type;
	const char *direction;	/* not used for signals */
};

struct gsh_dbus_method {
	const char *name;
	 bool(*method) (DBusMessageIter *args,
			DBusMessage *reply,
			DBusError *error);
	struct gsh_dbus_arg args[];
};

struct gsh_dbus_signal {
	const char *name;
	 bool(*signal) (DBusMessageIter *args, DBusMessage *reply);
	struct gsh_dbus_arg args[];
};

struct gsh_dbus_interface {
	const char *name;
	bool signal_props;
	struct gsh_dbus_prop **props;
	struct gsh_dbus_method **methods;
	struct gsh_dbus_signal **signals;
};

struct gsh_dbus_interface log_interface;

void gsh_dbus_pkginit(void);
void gsh_dbus_pkgshutdown(void);
void *gsh_dbus_thread(void *arg);

/* callout method */
void dbus_append_timestamp(DBusMessageIter *iterp, struct timespec *ts);
void dbus_status_reply(DBusMessageIter *iter, bool success, char *errormsg);
int32_t gsh_dbus_register_path(const char *name,
			       struct gsh_dbus_interface **interfaces);

/* more to come */

#endif				/* _GANESHA_DBUS_H */
