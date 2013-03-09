/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "nfs_rpc_callback.h"
#include "ganesha_dbus.h"

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

struct ganesha_dbus_handler
{
    char *name;
    struct avltree_node node_k;
    DBusObjectPathVTable vtable;
};
typedef struct ganesha_dbus_handler ganesha_dbus_handler_t;

struct _dbus_thread_state
{
    int initialized;
    pthread_t thread_id;
    wait_entry_t we;
    DBusConnection* dbus_conn;
    DBusError dbus_err;
    uint32_t dbus_serial;
    struct avltree callouts;
    uint32_t flags;
};

static struct _dbus_thread_state thread_state;

static inline int
dbus_callout_cmpf(const struct avltree_node *lhs,
                  const struct avltree_node *rhs)
{
    ganesha_dbus_handler_t *lk, *rk;

    lk = avltree_container_of(lhs, ganesha_dbus_handler_t, node_k);
    rk = avltree_container_of(rhs, ganesha_dbus_handler_t, node_k);

    return (strcmp(lk->name, rk->name));
}

void gsh_dbus_pkginit(void)
{
    char regbuf[128];
    int code = 0;

    LogDebug(COMPONENT_DBUS, "init");

    avltree_init(&thread_state.callouts, dbus_callout_cmpf, 0 /* must be 0 */);

    dbus_error_init(&thread_state.dbus_err); /* sigh */
    thread_state.dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM,
                                          &thread_state.dbus_err);
    if (dbus_error_is_set(&thread_state.dbus_err)) {
        LogCrit(COMPONENT_DBUS,
                "dbus_bus_get failed (%s)", thread_state.dbus_err.message);
        dbus_error_free(&thread_state.dbus_err);
        goto out;
    }

    snprintf(regbuf, 128, "org.ganesha.nfsd");
    code = dbus_bus_request_name(thread_state.dbus_conn, regbuf,
                                 DBUS_NAME_FLAG_REPLACE_EXISTING ,
                                 &thread_state.dbus_err);
    if (dbus_error_is_set(&thread_state.dbus_err)) { 
        LogCrit(COMPONENT_DBUS, "server bus reg failed (%s, %s)", regbuf,
                thread_state.dbus_err.message); 
        dbus_error_free(&thread_state.dbus_err);
        if (! code)
            code = EINVAL;
        goto out;
    }
    if (code != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        LogCrit(COMPONENT_DBUS, "server failed becoming primary bus owner "
                "(%s, %d)", regbuf, code);
        goto out;
    }

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

#define PROPERTIES_INTERFACE \
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
"    </method>\n" \
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

static bool dbus_reply_introspection(
	DBusConnection *conn,
	DBusMessage *reply,
	struct gsh_dbus_interface **interfaces)
{
	DBusMessageIter iter;
	FILE *fp;
	char *introspection_xml = NULL;
	size_t xml_size = 0;
	struct gsh_dbus_interface **iface;
	bool have_props = false;
	bool retval = true;

	fp = open_memstream(&introspection_xml, &xml_size);
	if(fp == NULL) {
		LogCrit(COMPONENT_DBUS,
			"open_memstream for introspection failed");
		retval = false;
		goto out;
	}
	fputs(INTROSPECT_HEAD, fp);
	for(iface = interfaces; *iface; iface++) {
		fprintf(fp, INTROSPECT_INTERFACE_HEAD,
			(*iface)->name);
		if((*iface)->props != NULL) {
			struct gsh_dbus_prop **prop;

			for(prop = (*iface)->props; *prop; prop++) {
				fprintf(fp, INTROSPECT_PROPERTY,
					(*prop)->name,
					(*prop)->type,
					(*prop)->access);
			}
			have_props = true;
		}
		if((*iface)->methods != NULL) {
			struct gsh_dbus_method **method, *mp;
			struct gsh_dbus_arg *arg;

			for(method = (*iface)->methods; *method; method++) {
				mp = *method;
				fprintf(fp, INTROSPECT_METHOD_HEAD,
					mp->name);
				for(arg = mp->args; arg->name != NULL; arg++) {
					fprintf(fp, INTROSPECT_METHOD_ARG,
						arg->name,
						arg->direction,
						arg->type);
				}
				fputs(INTROSPECT_METHOD_TAIL, fp);
				
			}
			if((*iface)->signals != NULL) {
			struct gsh_dbus_signal **signal;
			struct gsh_dbus_arg *arg;

			for(signal = (*iface)->signals; *signal; signal++) {
				fprintf(fp, INTROSPECT_SIGNAL_HEAD,
					(*signal)->name);
				for(arg = (*signal)->args; arg->name != NULL; arg++) {
					fprintf(fp, INTROSPECT_SIGNAL_ARG,
						arg->name,
						arg->type);
				}
				fputs(INTROSPECT_SIGNAL_TAIL, fp);
				
			}
		}
		}
		fputs(INTROSPECT_INTERFACE_TAIL, fp);
	}
	if(have_props)
		fputs(PROPERTIES_INTERFACE, fp);
	fputs(INTROSPECT_TAIL, fp);
	if(ferror(fp)) {
		LogCrit(COMPONENT_DBUS,
			"file error while constructing introspect xml");
	}
	fclose(fp);
	if(introspection_xml == NULL) {
		LogCrit(COMPONENT_DBUS,
			"close of memstream for introspection failed");
		retval = false;
		goto out;
	}
		
	/* create a reply from the message */
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
				       &introspection_xml);
	free(introspection_xml);

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

void dbus_status_reply(DBusMessageIter *iter,
			 bool success,
			 char *errormsg)
{
	char *error;
	int retcode = success;

	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_BOOLEAN,
				       &retcode);
	if(success) {
		error = "OK";
	} else if(errormsg == NULL) {
		error = "BUSY";
	} else {
		error = errormsg;
	}
	dbus_message_iter_append_basic(iter,
				       DBUS_TYPE_STRING,
				       &error);
}

void dbus_append_timestamp(DBusMessageIter *iterp,
			   struct timespec *timestamp)
{
	DBusMessageIter ts_iter;

	dbus_message_iter_open_container(iterp, DBUS_TYPE_STRUCT,
					 NULL,
					 &ts_iter);
	dbus_message_iter_append_basic(&ts_iter,
				       DBUS_TYPE_UINT64,
				       &timestamp->tv_sec);
	dbus_message_iter_append_basic(&ts_iter,
				       DBUS_TYPE_UINT64,
				       &timestamp->tv_nsec);
	dbus_message_iter_close_container(iterp, &ts_iter);
}

static DBusHandlerResult dbus_message_entrypoint(
	DBusConnection *conn,
	DBusMessage *msg,
	void *user_data)
{
	const char *interface = dbus_message_get_interface(msg);
	const char *method = dbus_message_get_member(msg);
	struct gsh_dbus_interface **interfaces = user_data;
	DBusMessage *reply;
	DBusMessageIter args, *argsp;
	bool retval = false;
	static uint32_t serial = 1;

	if (interface == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if(dbus_message_iter_init(msg, &args))
		argsp = &args;
	else
		argsp = NULL;
	reply = dbus_message_new_method_return(msg);
	if((!strcmp(interface, DBUS_INTERFACE_INTROSPECTABLE)) ||
	   (method && (!strcmp(method, "Introspect")))) {
		retval = dbus_reply_introspection(conn, reply, interfaces);
	} else if(!strcmp(interface, DBUS_INTERFACE_PROPERTIES)) {
		retval = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	} else {
		struct gsh_dbus_interface **iface;

		for(iface = interfaces; *iface; iface++) {
			if(strcmp(interface, (*iface)->name) == 0) {
				struct gsh_dbus_method **m;

				for(m = (*iface)->methods; *m; m++) {
					if(strcmp(method, (*m)->name) == 0) {
						retval = (*m)->method(argsp, reply);
						goto done;
					}
				}
				LogMajor(COMPONENT_DBUS,
					 "Unknown method (%s) on interface (%s)",
					 method,
					 interface);
				goto done;
			}
		}
		LogMajor(COMPONENT_DBUS,
			 "Unknown interface (%s)",
			 interface);
	}
done:
	if( !retval) {
		LogMajor(COMPONENT_DBUS,
			 "Method (%s) on interface (%s) failed",
			 method,
			 interface);
	}
	if (!dbus_connection_send(conn, reply, &serial)) {
		LogCrit(COMPONENT_DBUS, "reply failed");
	}
	dbus_connection_flush(conn);
	dbus_message_unref(reply);
	serial++;
	return (retval ? DBUS_HANDLER_RESULT_HANDLED
		: DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}

static void
path_unregistered_func (DBusConnection  *connection,
                        void            *user_data)
{
  /* connection was finalized -- do nothing */
}

int32_t gsh_dbus_register_path(const char *name,
			       struct gsh_dbus_interface **interfaces)
{
    ganesha_dbus_handler_t *handler;
    struct avltree_node *node;
    char path[512];
    int code = 0;

    /* XXX if this works, add ifc level */
    snprintf(path, 512, "/org/ganesha/nfsd/%s", name);

    handler = (ganesha_dbus_handler_t *)
        gsh_malloc(sizeof(ganesha_dbus_handler_t));
    handler->name = gsh_strdup(path);
    handler->vtable.unregister_function = path_unregistered_func;
    handler->vtable.message_function =  dbus_message_entrypoint;

    if (!thread_state.dbus_conn) {
        LogCrit(COMPONENT_DBUS, "dbus_connection_register_object_path "
                "called with no DBUS connection");
        goto out;
    }

    code = dbus_connection_register_object_path (thread_state.dbus_conn,
                                                 handler->name,
                                                 &handler->vtable,
                                                 (void*)interfaces);
    if (! code) {
        LogFatal(COMPONENT_DBUS, "dbus_connection_register_object_path "
                 "failed");
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
    return (code);
}

void gsh_dbus_pkgshutdown(void)
{
    struct avltree_node *node, *onode;
    ganesha_dbus_handler_t *handler;

    LogDebug(COMPONENT_DBUS, "shutdown");

    /* remove and free handlers */
    onode = NULL;
    node = avltree_first(&thread_state.callouts);
    do {
        if (onode) {
            handler = avltree_container_of(onode, ganesha_dbus_handler_t,
                                           node_k);
            dbus_bus_release_name(thread_state.dbus_conn, handler->name,
                                         &thread_state.dbus_err);
            if (dbus_error_is_set(&thread_state.dbus_err)) { 
                LogCrit(COMPONENT_DBUS, "err releasing name (%s, %s)",
                        handler->name, thread_state.dbus_err.message);
                dbus_error_free(&thread_state.dbus_err);
            }
            gsh_free(handler->name);
            gsh_free(handler);
        }
    } while ((onode = node) && (node = avltree_next(node)));
    if (onode) {
        handler = avltree_container_of(onode, ganesha_dbus_handler_t,
                                       node_k);
        dbus_bus_release_name(thread_state.dbus_conn, handler->name,
                                     &thread_state.dbus_err);
        if (dbus_error_is_set(&thread_state.dbus_err)) { 
            LogCrit(COMPONENT_DBUS, "err releasing name (%s, %s)",
                    handler->name, thread_state.dbus_err.message);
            dbus_error_free(&thread_state.dbus_err);
        }
        gsh_free(handler->name);
        gsh_free(handler);
    }
    avltree_init(&thread_state.callouts, dbus_callout_cmpf, 0);

    /* shutdown bus */
    if (thread_state.dbus_conn)
        dbus_connection_close(thread_state.dbus_conn);
}

void *gsh_dbus_thread(void *arg)
{

    SetNameFunction("gsh_dbus_thread");

    if (! thread_state.dbus_conn) {
        LogCrit(COMPONENT_DBUS, "DBUS not initialized, service thread "
                "exiting");
        goto out;
    }

    while (1) {
        if (thread_state.flags & GSH_DBUS_SHUTDOWN)
            break;

        LogFullDebug(COMPONENT_DBUS, "top of poll loop");

        /* do stuff */
        if( !dbus_connection_read_write_dispatch(thread_state.dbus_conn,
						 30*1000)) {
		LogCrit(COMPONENT_DBUS,
			"read_write_dispatch, got disconnected signal");
		break;
	}
	/* here is where we do other stuff between messages */
    } /* 1 */

out:
    LogCrit(COMPONENT_DBUS, "shutdown");

    return (NULL);
}

void gsh_dbus_wake_thread(uint32_t flags)
{
    if (thread_state.flags & GSH_DBUS_SLEEPING)
        pthread_cond_signal(&thread_state.we.cv);
}
