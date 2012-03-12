/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

#ifndef _GANESHA_DBUS_H
#define _GANESHA_DBUS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

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

void gsh_dbus_pkginit(void);
void gsh_dbus_pkgshutdown(void);
void *gsh_dbus_thread(void *arg);

/* callout method */
int32_t gsh_dbus_register_path(const char *name,
                               DBusObjectPathMessageFunction method);

/* more to come */

#endif /* _GANESHA_DBUS_H */
