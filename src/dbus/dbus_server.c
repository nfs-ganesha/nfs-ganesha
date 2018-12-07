/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
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

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "gsh_list.h"

#include <errno.h>
#include <dbus/dbus.h>
#include <ctype.h>
#include <urcu-bp.h>

#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "nfs_rpc_callback.h"
#include "gsh_dbus.h"
#include <os/memstream.h>
#include "dbus_priv.h"
#include "nfs_init.h"

/**
 *
 * \file dbus_server.c
 * \author Matt Benjamin
 * \brief Low-level DBUS message server and callout framework.
 *
 * \section DESCRIPTION
 *
 * This module implements a (somewhat) generic service handler, initially to
 * support a planned callback simulator.  Developers are encouraged to expand
 * on this interface to support other use cases.
 *
 * This module should be initialized before any service provider module
 * calls gsh_dbus_register_msg();
 *
 */

#define GSH_DBUS_NONE      0x0000
#define GSH_DBUS_SHUTDOWN  0x0001
#define GSH_DBUS_SLEEPING  0x0002
static const char dbus_name[] = "org.ganesha.nfsd";

/*
 * List and mutex used by the dbus broadcast service
 */
struct glist_head dbus_broadcast_list;
pthread_mutex_t dbus_bcast_lock;

struct ganesha_dbus_handler {
	char *name;
	struct avltree_node node_k;
	DBusObjectPathVTable vtable;
};

struct _dbus_thread_state {
	int initialized;
	pthread_t thread_id;
	wait_entry_t we;
	DBusConnection *dbus_conn;
	DBusError dbus_err;
	uint32_t dbus_serial;
	struct avltree callouts;
	uint32_t flags;
};

static struct _dbus_thread_state thread_state;

static inline int dbus_callout_cmpf(const struct avltree_node *lhs,
				    const struct avltree_node *rhs)
{
	struct ganesha_dbus_handler *lk, *rk;

	lk = avltree_container_of(lhs, struct ganesha_dbus_handler, node_k);
	rk = avltree_container_of(rhs, struct ganesha_dbus_handler, node_k);

	return strcmp(lk->name, rk->name);
}

static inline bool is_valid_dbus_prefix(const char *prefix)
{
	if (prefix == NULL || *prefix == '\0')
		return false;

	if (!isalpha(*prefix) && *prefix != '_')
		return false;

	prefix++;
	while (*prefix != '\0') {
		if (!isalnum(*prefix) && *prefix != '_')
			return false;
		prefix++;
	}

	return true;
}

static inline void dbus_name_with_prefix(char *prefixed_dbus_name,
			const char *default_name, const char *prefix)
{
	int prefix_len, total_len;

	if (!is_valid_dbus_prefix(prefix)) {
		if (prefix != NULL && prefix[0] != '\0') {
			LogEvent(COMPONENT_DBUS,
				"Dbus name prefix is invalid. Ignoring the prefix.");
		}
		strcpy(prefixed_dbus_name, default_name);
		return;
	}

	prefix_len = strlen(prefix);

	/* Additional length for separator (.) and null character */
	total_len = strlen(default_name) + prefix_len + 2;
	if (total_len > NAME_MAX) {
		LogEvent(COMPONENT_DBUS,
			"Dbus name prefix too long. Ignoring the prefix.");
		strcpy(prefixed_dbus_name, default_name);
		return;
	}

	strcpy(prefixed_dbus_name, prefix);
	prefixed_dbus_name[prefix_len] = '.';
	strcpy(prefixed_dbus_name + prefix_len + 1, default_name);
}

/*
 * @brief compare routine used to sort broadcast items
 *
 * Function used to sort the broadcast items according
 * to time expirey.  Conforms to the glist_compare
 * function signature so it can be used with glist_insert_sorted
 *
 * @param a: Pointer to the glist of a dbus_bcast_item
 * @param b: Pointer to the glist of another dbus_bcast_item
 *           to compare the first to
 */
int dbus_bcast_item_compare(struct glist_head *a,
			    struct glist_head *b)
{
	struct dbus_bcast_item *bcast_item_a;
	struct dbus_bcast_item *bcast_item_b;

	bcast_item_a = glist_entry(a,
				   struct dbus_bcast_item,
				   dbus_bcast_q);
	bcast_item_b = glist_entry(b,
				   struct dbus_bcast_item,
				   dbus_bcast_q);
	return gsh_time_cmp(&bcast_item_a->next_bcast_time,
			    &bcast_item_b->next_bcast_time);
}

/*
 * @brief del_dbus_broadcast: Delete a broadcast item from the
 *                            broadcast service
 *
 * Function to be called by any thread wanting to remove a broadcast item
 * from the dbus broadcast service
 *
 * @param to_remove: The pointer to the dbus_bcast_item
 *                   returned from add_dbus_broadcast
 */
void del_dbus_broadcast(struct dbus_bcast_item *to_remove)
{
	PTHREAD_MUTEX_lock(&dbus_bcast_lock);
	glist_del(&to_remove->dbus_bcast_q);
	PTHREAD_MUTEX_unlock(&dbus_bcast_lock);

	gsh_free(to_remove);
}

/*
 * @brief add_dbus_broadcast: Add a callback to the broadcast service
 *
 * Function to be called by any thread that wants to add a callback
 * to the dbus broadcast service
 *
 * @param bcast_callback: Function pointer to be called
 * @param arg: Arg that will be passed to the callback
 * @param bcast_interval: The time in nsec between calls
 * @param count: The number of times to invoke the callback
 *               Pass BCAST_FOREVER to call indefinitely
 *
 * @return: The pointer to the dbus_bcast_item created
 */
struct dbus_bcast_item *add_dbus_broadcast(
					dbus_bcast_callback bcast_callback,
					void *bcast_arg,
					uint32_t bcast_interval,
					int count)
{
	struct dbus_bcast_item *new_bcast = NULL;

	new_bcast = (struct dbus_bcast_item *)
		gsh_malloc(sizeof(struct dbus_bcast_item));

	now(&new_bcast->next_bcast_time);
	new_bcast->bcast_interval = bcast_interval;
	new_bcast->count = count;
	new_bcast->bcast_arg = bcast_arg;
	new_bcast->bcast_callback = bcast_callback;

	PTHREAD_MUTEX_lock(&dbus_bcast_lock);
	glist_insert_sorted(&dbus_broadcast_list,
			    &(new_bcast->dbus_bcast_q),
			    &dbus_bcast_item_compare);
	PTHREAD_MUTEX_unlock(&dbus_bcast_lock);

	return new_bcast;
}

/*
 * @brief init_dbus_broadcast: Initializes broadcast list and mutex
 */
void init_dbus_broadcast(void)
{
	PTHREAD_MUTEX_init(&dbus_bcast_lock, NULL);
	glist_init(&dbus_broadcast_list);

	if (nfs_param.core_param.heartbeat_freq > 0)
		init_heartbeat();
}

void gsh_dbus_pkginit(void)
{
	int code = 0;
	char prefixed_dbus_name[NAME_MAX];

	LogDebug(COMPONENT_DBUS, "init");

	avltree_init(&thread_state.callouts, dbus_callout_cmpf,
		     0 /* must be 0 */);

	dbus_error_init(&thread_state.dbus_err);	/* sigh */
	thread_state.dbus_conn =
	    dbus_bus_get(DBUS_BUS_SYSTEM, &thread_state.dbus_err);
	if (dbus_error_is_set(&thread_state.dbus_err)) {
		LogCrit(COMPONENT_DBUS, "dbus_bus_get failed (%s)",
			thread_state.dbus_err.message);
		dbus_error_free(&thread_state.dbus_err);
		goto out;
	}

	dbus_name_with_prefix(prefixed_dbus_name, dbus_name,
				nfs_param.core_param.dbus_name_prefix);
	code =
	    dbus_bus_request_name(thread_state.dbus_conn, prefixed_dbus_name,
				  DBUS_NAME_FLAG_REPLACE_EXISTING,
				  &thread_state.dbus_err);
	if (dbus_error_is_set(&thread_state.dbus_err)) {
		LogCrit(COMPONENT_DBUS, "server bus reg failed (%s, %s)",
			prefixed_dbus_name, thread_state.dbus_err.message);
		dbus_error_free(&thread_state.dbus_err);
		if (!code)
			code = EINVAL;
		goto out;
	}
	if (code != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		LogCrit(COMPONENT_DBUS,
			"server failed becoming primary bus owner (%s, %d)",
			prefixed_dbus_name, code);
		goto out;
	}

	init_dbus_broadcast();

	thread_state.initialized = true;

 out:
	return;
}

#define INTROSPECT_HEAD \
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n" \
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n" \
"<node>\n" \
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n" \
"    <method name=\"Introspect\">\n" \
"      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n" \
"    </method>\n" \
"  </interface>\n"

#define INTROSPECT_TAIL \
"</node>\n"

#define PROPERTIES_INTERFACE_HEAD \
"  <interface name=\"org.freedesktop.DBus.Properties\">\n" \
"    <method name=\"Get\">\n" \
"      <arg name=\"interface\" direction=\"in\" type=\"s\"/>\n" \
"      <arg name=\"propname\" direction=\"in\" type=\"s\"/>\n" \
"      <arg name=\"value\" direction=\"out\" type=\"v\"/>\n" \
"    </method>\n" \
"    <method name=\"Set\">\n" \
"      <arg name=\"interface\" direction=\"in\" type=\"s\"/>\n" \
"      <arg name=\"propname\" direction=\"in\" type=\"s\"/>\n" \
"      <arg name=\"value\" direction=\"in\" type=\"v\"/>\n" \
"    </method>\n" \
"    <method name=\"GetAll\">\n" \
"      <arg name=\"interface\" direction=\"in\" type=\"s\"/>\n" \
"      <arg name=\"props\" direction=\"out\" type=\"a{sv}\"/>\n" \
"    </method>\n"

#define PROPERTIES_INTERFACE_SIGNAL \
"    <signal name=\"PropertiesChanged\">\n" \
"      <arg name=\"interface\" type=\"s\"/>\n" \
"      <arg name=\"changed_properties\" type=\"a{sv}\"/>\n" \
"      <arg name=\"invalidated_properties\" type=\"as\"/>\n" \
"    </signal>\n"

#define PROPERTIES_INTERFACE_TAIL \
"  </interface>\n"

#define INTROSPECT_INTERFACE_HEAD \
"  <interface name=\"%s\">\n"

#define INTROSPECT_INTERFACE_TAIL \
"  </interface>\n"

#define INTROSPECT_METHOD_HEAD \
"    <method name=\"%s\">\n"

#define INTROSPECT_METHOD_TAIL \
"    </method>\n"

#define INTROSPECT_METHOD_ARG \
"      <arg name=\"%s\" direction=\"%s\" type=\"%s\"/>\n"

#define INTROSPECT_PROPERTY \
"      <property name=\"%s\" type=\"%s\" access=\"%s\"/>\n"

#define INTROSPECT_SIGNAL_HEAD \
"    <signal name=\"%s\">\n"

#define INTROSPECT_SIGNAL_TAIL \
"    </signal>\n"

#define INTROSPECT_SIGNAL_ARG \
"      <arg name=\"%s\" type=\"%s\"/>\n"

static const char * const prop_access[] = {
	[DBUS_PROP_READ] = "read",
	[DBUS_PROP_WRITE] = "write",
	[DBUS_PROP_READWRITE] = "readwrite"
};

static bool dbus_reply_introspection(DBusMessage *reply,
				     struct gsh_dbus_interface **interfaces)
{
	DBusMessageIter iter;
	FILE *fp;
	char *introspection_xml = NULL;
	size_t xml_size = 0;
	struct gsh_dbus_interface **iface;
	bool have_props = false;
	bool props_signal = false;
	bool retval = true;

	fp = open_memstream(&introspection_xml, &xml_size);
	if (fp == NULL) {
		LogCrit(COMPONENT_DBUS,
			"open_memstream for introspection failed");
		retval = false;
		goto out;
	}
	fputs(INTROSPECT_HEAD, fp);
	for (iface = interfaces; *iface; iface++) {
		fprintf(fp, INTROSPECT_INTERFACE_HEAD, (*iface)->name);
		if ((*iface)->props != NULL) {
			struct gsh_dbus_prop **prop;

			for (prop = (*iface)->props; *prop; prop++) {
				fprintf(fp, INTROSPECT_PROPERTY, (*prop)->name,
					(*prop)->type,
					prop_access[(*prop)->access]);
			}
			have_props = true;
			if ((*iface)->signal_props)
				props_signal = true;
		}
		if ((*iface)->methods != NULL) {
			struct gsh_dbus_method **method, *mp;
			struct gsh_dbus_arg *arg;

			for (method = (*iface)->methods; *method; method++) {
				mp = *method;
				fprintf(fp, INTROSPECT_METHOD_HEAD, mp->name);
				for (arg = mp->args; arg->name != NULL; arg++) {
					fprintf(fp, INTROSPECT_METHOD_ARG,
						arg->name, arg->direction,
						arg->type);
				}
				fputs(INTROSPECT_METHOD_TAIL, fp);

			}
		}
		if ((*iface)->signals != NULL) {
			struct gsh_dbus_signal **signal;
			struct gsh_dbus_arg *arg;

			for (signal = (*iface)->signals; *signal; signal++) {
				fprintf(fp, INTROSPECT_SIGNAL_HEAD,
					(*signal)->name);
				for (arg = (*signal)->args; arg->name != NULL;
				     arg++) {
					fprintf(fp, INTROSPECT_SIGNAL_ARG,
						arg->name, arg->type);
				}
				fputs(INTROSPECT_SIGNAL_TAIL, fp);

			}
		}
		fputs(INTROSPECT_INTERFACE_TAIL, fp);
	}
	if (have_props) {
		fputs(PROPERTIES_INTERFACE_HEAD, fp);
		if (props_signal)
			fputs(PROPERTIES_INTERFACE_SIGNAL, fp);
		fputs(PROPERTIES_INTERFACE_TAIL, fp);
	}
	fputs(INTROSPECT_TAIL, fp);
	if (ferror(fp)) {
		LogCrit(COMPONENT_DBUS,
			"file error while constructing introspect xml");
	}
	fclose(fp);
	if (introspection_xml == NULL) {
		LogCrit(COMPONENT_DBUS,
			"close of memstream for introspection failed");
		retval = false;
		goto out;
	}

	/* create a reply from the message */
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
				       &introspection_xml);
	gsh_free(introspection_xml);

 out:
	return retval;
}

/* @brief Stuff a status into the reply
 *
 * status reply is the first part of every reply message
 * dbus has its own error handling but that is for the connection.
 * this status is for ganesha level method result reporting.
 * If a NULL is passed for error message, we stuff a default
 * "BUSY".  The error message is for things like GUI display or
 * logging but use the status bool for code flow.
 *
 * @param iter       [IN] the iterator to append to
 * @param success    [IN] the method stastus
 * @param errmessage [IN] an error message string
 */

void dbus_status_reply(DBusMessageIter *iter, bool success, char *errormsg)
{
	char *error;
	dbus_bool_t retcode = success;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &retcode);
	if (errormsg == NULL)
		error = success ? "OK" : "BUSY";
	else
		error = errormsg;
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &error);
}

void dbus_append_timestamp(DBusMessageIter *iterp, struct timespec *timestamp)
{
	DBusMessageIter ts_iter;

	/* tv_sec and tv_nsec may not be same size as dbus_uint64_t on
	 * 32 bit systems, so copy them here to dbus_uint64_t sized
	 * symbols.
	 */
	dbus_uint64_t sec = timestamp->tv_sec;
	dbus_uint64_t nsec = timestamp->tv_nsec;

	dbus_message_iter_open_container(iterp, DBUS_TYPE_STRUCT, NULL,
					 &ts_iter);
	dbus_message_iter_append_basic(&ts_iter, DBUS_TYPE_UINT64, &sec);
	dbus_message_iter_append_basic(&ts_iter, DBUS_TYPE_UINT64, &nsec);
	dbus_message_iter_close_container(iterp, &ts_iter);
}

static DBusHandlerResult dbus_message_entrypoint(DBusConnection *conn,
						 DBusMessage *msg,
						 void *user_data)
{
	const char *interface = dbus_message_get_interface(msg);
	const char *method = dbus_message_get_member(msg);
	struct gsh_dbus_interface **interfaces = user_data;
	DBusMessage *reply = NULL;
	DBusError error;
	DBusMessageIter args, *argsp;
	bool success = false;
	DBusHandlerResult result = DBUS_HANDLER_RESULT_HANDLED;
	static uint32_t serial = 1;

	dbus_error_init(&error);
	if (interface == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	reply = dbus_message_new_method_return(msg);
	if ((!strcmp(interface, DBUS_INTERFACE_INTROSPECTABLE))
	    || (method && (!strcmp(method, "Introspect")))) {
		success = dbus_reply_introspection(reply, interfaces);
		goto done;
	}
	if (method == NULL) {
		method = "No method arg";
		goto done;
	}
	if (!strcmp(interface, DBUS_INTERFACE_PROPERTIES)) {
		success = dbus_proc_property(method, msg,
					    reply, &error, interfaces);
	} else {
		struct gsh_dbus_interface **iface;

		if (dbus_message_iter_init(msg, &args))
			argsp = &args;
		else
			argsp = NULL;
		for (iface = interfaces; *iface; iface++) {
			if (strcmp(interface, (*iface)->name) == 0) {
				struct gsh_dbus_method **m;

				for (m = (*iface)->methods; m && *m; m++) {
					if (strcmp(method, (*m)->name) == 0) {
						success = (*m)->method(argsp,
								       reply,
								       &error);
						goto done;
					}
				}
				LogMajor(COMPONENT_DBUS,
					 "Unknown method (%s) on interface (%s)",
					 method, interface);
				result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
				goto done;
			}
		}
		LogMajor(COMPONENT_DBUS, "Unknown interface (%s)", interface);
	}
 done:
	if (!success) {
		const char *err_name, *err_text;

		if (dbus_error_is_set(&error)) {
			err_name = error.name;
			err_text = error.message;
		} else {
			err_name = interface;
			err_text = method;
		}
		LogMajor(COMPONENT_DBUS,
			 "Method (%s) on (%s) failed: name = (%s), message = (%s)",
			 method, interface, err_name, err_text);
		dbus_message_unref(reply);
		reply = dbus_message_new_error(msg, err_name, err_text);
	}
	success = dbus_connection_send(conn, reply, &serial);
	if (!success) {
		LogCrit(COMPONENT_DBUS, "reply failed");
		result = DBUS_HANDLER_RESULT_NEED_MEMORY;
		dbus_connection_flush(conn);
	}
	if (reply)
		dbus_message_unref(reply);
	dbus_error_free(&error);
	serial++;
	return result;
}

static void path_unregistered_func(DBusConnection *connection, void *user_data)
{
	/* connection was finalized -- do nothing */
}

int32_t gsh_dbus_register_path(const char *name,
			       struct gsh_dbus_interface **interfaces)
{
	struct ganesha_dbus_handler *handler;
	struct avltree_node *node;
	char path[512];
	int code = 0;

	/* XXX if this works, add ifc level */
	snprintf(path, 512, "%s%s", DBUS_PATH, name);

	handler = (struct ganesha_dbus_handler *)
	    gsh_malloc(sizeof(struct ganesha_dbus_handler));
	handler->name = gsh_strdup(path);
	handler->vtable.unregister_function = path_unregistered_func;
	handler->vtable.message_function = dbus_message_entrypoint;

	if (!thread_state.dbus_conn) {
		LogCrit(COMPONENT_DBUS,
			"dbus_connection_register_object_path called with no DBUS connection");
		gsh_free(handler->name);
		gsh_free(handler);
		goto out;
	}

	code =
	    dbus_connection_register_object_path(thread_state.dbus_conn,
						 handler->name,
						 &handler->vtable,
						 (void *)interfaces);
	if (!code) {
		LogFatal(COMPONENT_DBUS,
			 "dbus_connection_register_object_path failed");
		gsh_free(handler->name);
		gsh_free(handler);
		goto out;
	}

	node = avltree_insert(&handler->node_k, &thread_state.callouts);
	if (node) {
		LogFatal(COMPONENT_DBUS, "failed inserting method %s", path);
		code = EINVAL;
	}

	LogDebug(COMPONENT_DBUS, "registered handler for %s", path);

 out:
	return code;
}

void gsh_dbus_pkgshutdown(void)
{
	struct avltree_node *node, *next_node;
	struct ganesha_dbus_handler *handler;
	int code = 0;
	char prefixed_dbus_name[NAME_MAX];

	LogDebug(COMPONENT_DBUS, "shutdown");

	/* Shutdown gsh_dbus_thread */
	thread_state.flags |= GSH_DBUS_SHUTDOWN;
	pthread_join(gsh_dbus_thrid, NULL);

	/* remove and free handlers */
	node = avltree_first(&thread_state.callouts);
	while (node) {
		next_node = avltree_next(node);
		handler = avltree_container_of(node,
					 struct ganesha_dbus_handler,
					 node_k);
		/* Unregister handler */
		code = dbus_connection_unregister_object_path(
						thread_state.dbus_conn,
						handler->name);
		if (!code) {
			LogCrit(COMPONENT_DBUS,
				"dbus_connection_unregister_object_path called with no DBUS connection");
		}
		avltree_remove(&handler->node_k, &thread_state.callouts);
		gsh_free(handler->name);
		gsh_free(handler);
		node = next_node;
	}

	avltree_init(&thread_state.callouts, dbus_callout_cmpf, 0);

	/* Unassign the name from dbus connection */
	if (thread_state.dbus_conn) {
		dbus_name_with_prefix(prefixed_dbus_name, dbus_name,
					nfs_param.core_param.dbus_name_prefix);
		dbus_bus_release_name(thread_state.dbus_conn,
				      prefixed_dbus_name,
				      &thread_state.dbus_err);
		if (dbus_error_is_set(&thread_state.dbus_err)) {
			LogCrit(COMPONENT_DBUS, "err releasing name (%s, %s)",
				prefixed_dbus_name,
				thread_state.dbus_err.message);
			dbus_error_free(&thread_state.dbus_err);
		}

		/*
		 * Shutdown bus: As per D-Bus documentation, a shared
		 * connection created with dbus_connection_open() or
		 * dbus_bus_get() should not be closed but instead be unref'ed
		 */
		dbus_connection_unref(thread_state.dbus_conn);
	}
}

void *gsh_dbus_thread(void *arg)
{
	struct glist_head *glist = NULL;
	struct glist_head *glistn = NULL;
	struct timespec current_time;
	int time_expired;
	int rc = 0;

	SetNameFunction("dbus");

	rcu_register_thread();

	if (!thread_state.initialized) {
		LogCrit(COMPONENT_DBUS,
			"DBUS not initialized, service thread exiting");
		goto out;
	}

	while (1) {
		if (thread_state.flags & GSH_DBUS_SHUTDOWN)
			break;

		LogFullDebug(COMPONENT_DBUS, "top of poll loop");

		PTHREAD_MUTEX_lock(&dbus_bcast_lock);
		glist_for_each_safe(glist, glistn, &dbus_broadcast_list) {
			struct dbus_bcast_item *bcast_item = glist_entry(glist,
							struct dbus_bcast_item,
							dbus_bcast_q);
			now(&current_time);
			time_expired = gsh_time_cmp(
				&current_time, &bcast_item->next_bcast_time);

			/*
			 * list is sorted by soonest to latest
			 * Break now if the next is not ready
			 */
			if (time_expired < 0)
				break;

			bcast_item->next_bcast_time = current_time;
			timespec_add_nsecs(bcast_item->bcast_interval,
					   &bcast_item->next_bcast_time);
			rc = bcast_item->bcast_callback(bcast_item->bcast_arg);
			if (rc == BCAST_STATUS_WARN) {
				LogWarn(COMPONENT_DBUS,
					"Broadcast callback %p returned BCAST_STATUS_WARN",
					bcast_item);
			} else if (rc == BCAST_STATUS_FATAL) {
				LogWarn(COMPONENT_DBUS,
					"Broadcast callback %p returned BCAST_STATUS_FATAL",
					bcast_item);
				glist_del(&bcast_item->dbus_bcast_q);
				continue;
			}

			if (bcast_item->count > 0)
				bcast_item->count--;

			glist_del(&bcast_item->dbus_bcast_q);

			/*
			 * If the callback should be called again, put it
			 * back in the list sorted by soonest to longest
			 */
			if (bcast_item->count > 0 ||
			    bcast_item->count == BCAST_FOREVER) {
				glist_insert_sorted(&dbus_broadcast_list,
						    &bcast_item->dbus_bcast_q,
						    &dbus_bcast_item_compare);
			}
		}
		PTHREAD_MUTEX_unlock(&dbus_bcast_lock);

		/* do stuff */
		if (!dbus_connection_read_write_dispatch
		    (thread_state.dbus_conn, 100)) {
			LogCrit(COMPONENT_DBUS,
				"read_write_dispatch, got disconnected signal");
			break;
		}
		/* here is where we do other stuff between messages */
	}			/* 1 */

 out:
	LogEvent(COMPONENT_DBUS, "shutdown");
	rcu_unregister_thread();
	return NULL;
}

void gsh_dbus_wake_thread(uint32_t flags)
{
	if (thread_state.flags & GSH_DBUS_SLEEPING)
		pthread_cond_signal(&thread_state.we.cv);
}

/*
 * @brief gsh_dbus_broadcast: Broadcast a dbus message
 *
 * Function to be called by a thread's callback routine
 * in order to broadcast a message over dbus
 *
 * @param obj_name: The path to the object emitting the signal
 *                  Ex: "/org/ganesha/nfsd/heartbeat"
 *
 * @param int_name: The interface the signal is emitted from
 *                  Ex: "org.ganesha.nfsd.heartbeat"
 *
 * @param sig_name: The name of the signal
 *                  Ex: "heartbeat"
 *
 * @param first_arg_type: The type of the first argument passed
 *                  Ex: DBUS_TYPE_STRING, DBUS_TYPE_UINT32, etc...
 *
 * @param ... :     An alternating list of types and data
 *                  All data args must be passed by reference
 *                  Ex: &my_int,
 *                      DBUS_TYPE_STRING, &charPtr,
 *                      DBUS_TYPE_BOOLEAN, &my_bool
 *
 * @return 0 on sucess or errno on failure
 */
int gsh_dbus_broadcast(char *obj_name, char *int_name,
		       char *sig_name, int first_arg_type, ...)
{
	static dbus_uint32_t serial;
	DBusMessage *msg;
	va_list arguments;
	int rc = 0;

	msg = dbus_message_new_signal(obj_name, int_name, sig_name);
	if (msg == NULL)
		return -EINVAL;

	va_start(arguments, first_arg_type);
	dbus_message_append_args_valist(msg, first_arg_type, arguments);
	va_end(arguments);

	if (!dbus_connection_send(thread_state.dbus_conn, msg, &serial))
		rc = -ENOMEM;

	dbus_message_unref(msg);

	return rc;
}

#ifdef _USE_9P

/* parse the 9P operation in args
 */
bool arg_9p_op(DBusMessageIter *args, u8 *opcode, char **errormsg)
{
	char *opname;
	u8 opc;
	bool success = true;

	if (args == NULL) {
		success = false;
		*errormsg = "message is missing argument";
	} else if (dbus_message_iter_get_arg_type(args) != DBUS_TYPE_STRING) {
		success = false;
		*errormsg = "arg not a string";
	} else {
		dbus_message_iter_get_basic(args, &opname);
		for (opc = _9P_TSTATFS; opc <= _9P_TWSTAT; opc++) {
			if (_9pfuncdesc[opc].funcname != NULL &&
			    !strcmp(opname, _9pfuncdesc[opc].funcname))
				break;
		}
		if (opc > _9P_TWSTAT) {
			success = false;
			*errormsg = "arg not a known 9P operation";
		} else {
			*opcode = opc;
		}
	}
	return success;
}

#endif
