/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

#ifndef GSH_DBUS_H
#define GSH_DBUS_H

#include "config.h"
#include <dbus/dbus.h>
#include "log.h"

#ifdef _USE_9P
#include "9p_types.h"
#endif

/**
 *
 * \file gsh_dbus.h
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
#define HEARTBEAT_NAME "heartbeat"

#define DBUS_PATH "/org/ganesha/nfsd/"
#define DBUS_ADMIN_IFACE "org.ganesha.nfsd.admin"

#define HEARTBEAT_ARG        \
{                            \
	.name = "isHealthy", \
	.type = "b",         \
	.direction = "out"   \
}

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
	.type = "q",		\
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

#define FSAL_ARG		\
{				\
	.name = "fsal",		\
	.type = "s",		\
	.direction = "in"	\
}

#define STAT_TYPE_ARG		\
{				\
	.name = "stat_type",		\
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
	 bool (*get)(DBusMessageIter *reply);
	 bool (*set)(DBusMessageIter *args);
};

struct gsh_dbus_arg {
	const char *name;
	const char *type;
	const char *direction;	/* not used for signals */
};

struct gsh_dbus_method {
	const char *name;
	 bool (*method)(DBusMessageIter *args,
			DBusMessage *reply,
			DBusError *error);
	struct gsh_dbus_arg args[];
};

struct gsh_dbus_signal {
	const char *name;
	bool (*signal)(DBusMessageIter *args, DBusMessage *reply);
	struct gsh_dbus_arg args[];
};

struct gsh_dbus_interface {
	const char *name;
	bool signal_props;
	struct gsh_dbus_prop **props;
	struct gsh_dbus_method **methods;
	struct gsh_dbus_signal **signals;
};

/**
  * @brief Default value for heartbeat frequency in ms
  */
#define HEARTBEAT_FREQ_DEFAULT 1000

#define BCAST_FOREVER       -1

#define BCAST_STATUS_OK    0x00
#define BCAST_STATUS_WARN  0x01
#define BCAST_STATUS_FATAL 0x02

typedef int (*dbus_bcast_callback)(void *);
struct dbus_bcast_item {
	struct timespec next_bcast_time;
	uint32_t bcast_interval;
	uint32_t count;
	void *bcast_arg;
	dbus_bcast_callback bcast_callback;
	struct glist_head dbus_bcast_q;
};
struct dbus_bcast_item *add_dbus_broadcast(
					dbus_bcast_callback bcast_callback,
					void *bcast_arg,
					uint32_t bcast_interval,
					int count);
void del_dbus_broadcast(struct dbus_bcast_item *to_remove);

/* heartbeat function call back */
int dbus_heartbeat_cb(void *arg);
void init_heartbeat(void);

void gsh_dbus_pkginit(void);
void gsh_dbus_pkgshutdown(void);
void *gsh_dbus_thread(void *arg);

/* callout method */
void gsh_dbus_append_timestamp(DBusMessageIter *iterp, struct timespec *ts);
void gsh_dbus_status_reply(DBusMessageIter *iter, bool success, char *errormsg);
int32_t gsh_dbus_register_path(const char *name,
			       struct gsh_dbus_interface **interfaces);
int gsh_dbus_broadcast(char *obj_name, char *int_name,
		       char *sig_name, int type, ...);
/* more to come */

#ifdef _USE_9P
bool arg_9p_op(DBusMessageIter *args, u8 *opcode, char **errormsg);
#endif

#endif				/* GSH_DBUS_H */
