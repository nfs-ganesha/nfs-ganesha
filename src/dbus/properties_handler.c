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

#ifndef DBUS_ERROR_UNKNOWN_INTERFACE
#define DBUS_ERROR_UNKNOWN_INTERFACE \
"org.freedesktop.DBus.Error.UnknownInterface"
#endif

#ifndef DBUS_ERROR_UNKNOWN_PROPERTY
#define DBUS_ERROR_UNKNOWN_PROPERTY \
"org.freedesktop.DBus.Error.UnknownProperty"
#endif

#ifndef DBUS_ERROR_PROPERTY_READ_ONLY
#define DBUS_ERROR_PROPERTY_READ_ONLY \
"org.freedesktop.DBus.Error.PropertyReadOnly"
#endif

/**
 * @brief The properties interface for properties interfaces
 *
 * Seems if introspect says we have a properties interface for an
 * object, scanners want to wall it to see if it has its own props.
 * Fake one here because properties doesn't have properties; signals
 * maybe but no props.
 */

static struct gsh_dbus_interface props_interface = {
	.name = DBUS_INTERFACE_PROPERTIES,
	.props = NULL,
	.methods = NULL,
	.signals = NULL
};

static struct gsh_dbus_interface *props_ptr = &props_interface;

static inline struct gsh_dbus_interface **lookup_interface(const char
							   *interface,
							   struct
							   gsh_dbus_interface
							   **interfaces,
							   DBusError *error)
{
	struct gsh_dbus_interface **iface;

	if (strcmp(interface, DBUS_INTERFACE_PROPERTIES) == 0)
		return &props_ptr;
	for (iface = interfaces; *iface; iface++) {
		if (strcmp(interface, (*iface)->name) == 0)
			break;
	}
	if (*iface == NULL) {
		dbus_set_error(error, DBUS_ERROR_UNKNOWN_INTERFACE,
			       "Requested interface: %s", interface);
	}
	return iface;
}

static inline struct gsh_dbus_prop **lookup_property(const char *prop_name,
						     struct gsh_dbus_interface
						     **iface,
						     DBusError *error)
{
	struct gsh_dbus_prop **prop;

	for (prop = (*iface)->props; prop && *prop; prop++) {
		if (strcmp(prop_name, (*prop)->name) == 0)
			break;
	}
	if (prop == NULL || *prop == NULL) {
		dbus_set_error(error, DBUS_ERROR_UNKNOWN_PROPERTY,
			       "Requested property: %s from %s", prop_name,
			       (*iface)->name);
		return NULL;
	}
	return prop;
}

/**
 *
 * @brief Handle object properties
 *
 * Handle the three methods of the properties interface.
 *
 * @param argsp  [IN]     The message
 * @param reply [IN/OUT] Our reply
 *
 * @return bool success or failure if bad request
 */

bool dbus_proc_property(const char *method, DBusMessage *msg,
			DBusMessage *reply, DBusError *error,
			struct gsh_dbus_interface **interfaces)
{
	const char *interface;
	const char *prop_name;
	bool retval = false;
	struct gsh_dbus_interface **iface;
	struct gsh_dbus_prop **prop;
	DBusMessageIter reply_iter;

	dbus_message_iter_init_append(reply, &reply_iter);

	if (strcmp(method, "GetAll") == 0) {
		DBusMessageIter getall_dict, dict_entry, val_iter;

		if (!dbus_message_get_args
		    (msg, error, DBUS_TYPE_STRING, &interface,
		     DBUS_TYPE_INVALID))
			goto err_out;
		iface = lookup_interface(interface, interfaces, error);
		if (*iface == NULL)
			goto err_out;
		if (!dbus_message_iter_open_container
		    (&reply_iter, DBUS_TYPE_ARRAY,
		     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
		     DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
		     DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &getall_dict))
			goto getall_err;
		for (prop = (*iface)->props; prop && *prop; prop++) {
			prop_name = (*prop)->name;
			if ((*prop)->access == DBUS_PROP_READ
			    || (*prop)->access == DBUS_PROP_READWRITE) {
				if (!dbus_message_iter_open_container
				    (&getall_dict, DBUS_TYPE_DICT_ENTRY, NULL,
				     &dict_entry))
					goto getall_err;
				if (!dbus_message_iter_append_basic
				    (&dict_entry, DBUS_TYPE_STRING, &prop_name))
					goto getall_err;
				if (!dbus_message_iter_open_container
				    (&dict_entry, DBUS_TYPE_VARIANT,
				     (*prop)->type, &val_iter))
					goto getall_err;
				if (!(*prop)->get(&val_iter))
					goto getall_err;
				if (!dbus_message_iter_close_container
				    (&dict_entry, &val_iter))
					goto getall_err;
				if (!dbus_message_iter_close_container
				    (&getall_dict, &dict_entry))
					goto getall_err;
			} else {
				dbus_set_error(error,
					       DBUS_ERROR_PROPERTY_READ_ONLY,
					       "%s of %s from %s (write only?)",
					       method, prop_name, interface);
				/** @todo@ check does write only make sense?? */
				goto err_out;
			}
		}
		if (!dbus_message_iter_close_container
		    (&reply_iter, &getall_dict))
			goto getall_err;

		return true;	/* DONE! */

	} else if (strcmp(method, "Get") == 0) {
		if (!dbus_message_get_args
		    (msg, error, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING,
		     &prop_name, DBUS_TYPE_INVALID))
			goto err_out;
		iface = lookup_interface(interface, interfaces, error);
		if (*iface == NULL)
			goto err_out;
		prop = lookup_property(prop_name, iface, error);
		if (prop == NULL)
			goto err_out;
		if ((*prop)->access == DBUS_PROP_READ
		    || (*prop)->access == DBUS_PROP_READWRITE) {
			DBusMessageIter variant_iter;

			if (!dbus_message_iter_open_container
			    (&reply_iter, DBUS_TYPE_VARIANT, (*prop)->type,
			     &variant_iter)) {
				dbus_set_error_const(error, DBUS_ERROR_FAILED,
						     "Couldn't open Get container");
				goto err_out;
			}
			retval = (*prop)->get(&variant_iter);
			if (retval == false ||
			    !dbus_message_iter_close_container(&reply_iter,
							       &variant_iter)) {
				dbus_set_error_const(error, DBUS_ERROR_FAILED,
						     "Couldn't close Get container");
				goto err_out;
			}
		} else {
			dbus_set_error(error, DBUS_ERROR_PROPERTY_READ_ONLY,
				       "%s of %s from %s (write only?)", method,
				       prop_name, interface);
			/** @todo@ check does write only make sense?? */
			goto err_out;
		}

		return true;	/* DONE! */

	} else if (strcmp(method, "Set") == 0) {
		DBusMessageIter iter_args;

		if (!dbus_message_iter_init(msg, &iter_args)
		    || dbus_message_iter_get_arg_type(&iter_args) !=
		    DBUS_TYPE_STRING) {
			goto invalid_args;
		}
		dbus_message_iter_get_basic(&iter_args, &interface);
		if (!dbus_message_iter_next(&iter_args)
		    || dbus_message_iter_get_arg_type(&iter_args) !=
		    DBUS_TYPE_STRING) {
			goto invalid_args;
		}
		dbus_message_iter_get_basic(&iter_args, &prop_name);
		if (!dbus_message_iter_next(&iter_args)
		    || dbus_message_iter_get_arg_type(&iter_args) !=
		    DBUS_TYPE_VARIANT
		    || dbus_message_iter_has_next(&iter_args)) {
			goto invalid_args;
		}
		iface = lookup_interface(interface, interfaces, error);
		if (*iface == NULL)
			goto err_out;
		prop = lookup_property(prop_name, iface, error);
		if (prop == NULL)
			goto err_out;
		if ((*prop)->access == DBUS_PROP_WRITE
		    || (*prop)->access == DBUS_PROP_READWRITE) {
			DBusMessageIter arg;

			dbus_message_iter_recurse(&iter_args, &arg);

			return (*prop)->set(&arg);	/* DONE! */

		} else {
			dbus_set_error(error, DBUS_ERROR_PROPERTY_READ_ONLY,
				       "%s of %s from %s", method, prop_name,
				       interface);
			goto err_out;
		}
	} else {
		dbus_set_error(error, DBUS_ERROR_UNKNOWN_METHOD,
			       "Requested method: %s", method);
	}
	return retval;

 getall_err:
	dbus_set_error(error, DBUS_ERROR_FAILED, "GetAll container failure");
	goto err_out;

 invalid_args:
	dbus_set_error(error, DBUS_ERROR_INVALID_ARGS, "Method %s", method);

 err_out:
	return retval;
}
