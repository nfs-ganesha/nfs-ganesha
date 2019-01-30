/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file  nfs_admin_thread.c
 * @brief The admin_thread and support code.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <urcu-bp.h>
#ifdef LINUX
#include <mcheck.h>		/* For mtrace/muntrace */
#endif

#include "nfs_core.h"
#include "log.h"
#include "sal_functions.h"
#include "sal_data.h"
#include "idmapper.h"
#include "delayed_exec.h"
#include "export_mgr.h"
#include "pnfs_utils.h"
#include "fsal.h"
#include "netgroup_cache.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#include "mdcache.h"
#endif
#include "conf_url.h"
#include "conf_url_rados.h"

/**
 * @brief Mutex protecting shutdown flag.
 */

static pthread_mutex_t admin_control_mtx = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Condition variable to signal change in shutdown flag.
 */

static pthread_cond_t admin_control_cv = PTHREAD_COND_INITIALIZER;

/**
 * @brief Flag to indicate shutdown Ganesha.
 *
 * Protected by admin_control_mtx and signaled by admin_control_cv.
 */
bool admin_shutdown;

#ifdef USE_DBUS

/**
 * @brief Dbus method get grace period status
 *
 * @param[in]  args  dbus args
 * @param[out] reply dbus reply message with grace period status
 */
static bool admin_dbus_get_grace(DBusMessageIter *args,
				 DBusMessage *reply,
				 DBusError *error)
{
	char *errormsg = "get grace success";
	bool success = true;
	DBusMessageIter iter;
	dbus_bool_t ingrace;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Get grace takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}

	ingrace = nfs_in_grace();
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &ingrace);

 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_get_grace = {
	.name = "get_grace",
	.method = admin_dbus_get_grace,
	.args = {
		 {.name = "isgrace",
		  .type = "b",
		  .direction = "out",
		 },
		 STATUS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Dbus method start grace period
 *
 * @param[in]  args  Unused
 * @param[out] reply Unused
 */

static bool admin_dbus_grace(DBusMessageIter *args,
			     DBusMessage *reply,
			     DBusError *error)
{
	char *errormsg = "Started grace period";
	bool success = true;
	DBusMessageIter iter;
	nfs_grace_start_t gsp;
	char *input = NULL;
	char *ip;

	dbus_message_iter_init_append(reply, &iter);
	if (args == NULL) {
		errormsg = "Grace period take 1 arguments: event:IP-address.";
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		success = false;
		goto out;
	}
	if (dbus_message_iter_get_arg_type(args) != DBUS_TYPE_STRING) {
		errormsg = "Grace period arg 1 not a string.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}
	dbus_message_iter_get_basic(args, &input);

	gsp.nodeid = -1;
	gsp.event = EVENT_TAKE_IP;

	ip = index(input, ':');
	if (ip == NULL)
		gsp.ipaddr = input;	/* no event specified */
	else {
		char *buf = alloca(strlen(input) + 1);

		gsp.ipaddr = ip + 1;	/* point at the ip passed the : */
		strcpy(buf, input);
		ip = strstr(buf, ":");
		if (ip != NULL) {
			*ip = '\0';	/* replace ":" with null */
			gsp.event = atoi(buf);
		}
		if (gsp.event == EVENT_TAKE_NODEID)
			gsp.nodeid = atoi(gsp.ipaddr);
	}
	nfs_start_grace(&gsp);
 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_grace_period = {
	.name = "grace",
	.method = admin_dbus_grace,
	.args = {IPADDR_ARG,
		 STATUS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Dbus method for shutting down Ganesha
 *
 * @param[in]  args  Unused
 * @param[out] reply Unused
 */

static bool admin_dbus_shutdown(DBusMessageIter *args,
				DBusMessage *reply,
				DBusError *error)
{
	char *errormsg = "Server shut down";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Shutdown takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}

	admin_halt();

 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_shutdown = {
	.name = "shutdown",
	.method = admin_dbus_shutdown,
	.args = {STATUS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Dbus method for flushing manage gids cache
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_purge_gids(DBusMessageIter *args,
				  DBusMessage *reply,
				  DBusError *error)
{
	char *errormsg = "Purge gids cache";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Purge gids takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}

	uid2grp_clear_cache();

 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_purge_gids = {
	.name = "purge_gids",
	.method = admin_dbus_purge_gids,
	.args = {STATUS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Dbus method for flushing netgroup cache
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_purge_netgroups(DBusMessageIter *args,
				       DBusMessage *reply,
				       DBusError *error)
{
	char *errormsg = "Purge netgroup cache";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Purge netgroup takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}

	ng_clear_cache();

 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_purge_netgroups = {
	.name = "purge_netgroups",
	.method = admin_dbus_purge_netgroups,
	.args = {STATUS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Dbus method for flushing idmapper cache
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_purge_idmapper_cache(DBusMessageIter *args,
					    DBusMessage *reply,
					    DBusError *error)
{
	char *errormsg = "Purge idmapper cache";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Purge idmapper takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}
	idmapper_clear_cache();
 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_purge_idmapper_cache = {
	.name = "purge_idmapper_cache",
	.method = admin_dbus_purge_idmapper_cache,
	.args = {STATUS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Dbus method for updating open fd limit
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_init_fds_limit(DBusMessageIter *args,
				       DBusMessage *reply,
				       DBusError *error)
{
	char *errormsg = "Init fds limit";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Init fds limit takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}

	init_fds_limit();

 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_init_fds_limit = {
	.name = "init_fds_limit",
	.method = admin_dbus_init_fds_limit,
	.args = {STATUS_REPLY,
		 END_ARG_LIST}
};

/**
 * @brief Dbus method for enabling malloc trace
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool
admin_dbus_malloc_trace(DBusMessageIter *args,
			DBusMessage *reply,
			DBusError *error)
{
	char *errormsg = "malloc trace";
	bool success = true;
	DBusMessageIter iter;
	char *filename;

	dbus_message_iter_init_append(reply, &iter);
	if (args == NULL) {
		errormsg = "malloc trace needs trace filename.";
		success = false;
		goto out;
	}

	if (dbus_message_iter_get_arg_type(args) != DBUS_TYPE_STRING) {
		errormsg = "malloc trace needs trace filename.";
		success = false;
		goto out;
	}

	dbus_message_iter_get_basic(args, &filename);

#ifdef LINUX
	LogEvent(COMPONENT_DBUS, "enabling malloc trace to %s.", filename);
	setenv("MALLOC_TRACE", filename, 1);
	mtrace();
#else
	errormsg = "malloc trace is not supported";
	success = false;
#endif

 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

/**
 * @brief Dbus method for disabling malloc trace
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool
admin_dbus_malloc_untrace(DBusMessageIter *args,
			  DBusMessage *reply,
			  DBusError *error)
{
	char *errormsg = "malloc untrace";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "malloc untrace takes no arguments.";
		success = false;
		goto out;
	}

#ifdef LINUX
	LogEvent(COMPONENT_DBUS, "disabling malloc trace.");
	muntrace();
#else
	errormsg = "malloc untrace is not supported";
	success = false;
#endif

 out:
	dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_malloc_trace = {
	.name = "malloc_trace",
	.method = admin_dbus_malloc_trace,
	.args = {{ .name = "tracefile",
		   .type = "s",
		   .direction = "in"},
		 STATUS_REPLY,
		 END_ARG_LIST}
};

static struct gsh_dbus_method method_malloc_untrace = {
	.name = "malloc_untrace",
	.method = admin_dbus_malloc_untrace,
	.args = {STATUS_REPLY,
		 END_ARG_LIST}
};

static struct gsh_dbus_method *admin_methods[] = {
	&method_shutdown,
	&method_grace_period,
	&method_get_grace,
	&method_purge_gids,
	&method_purge_netgroups,
	&method_init_fds_limit,
	&method_purge_idmapper_cache,
	&method_malloc_trace,
	&method_malloc_untrace,
	NULL
};

static struct gsh_dbus_signal heartbeat_signal = {
	.name = HEARTBEAT_NAME,
	.signal = NULL,
	.args = {HEARTBEAT_ARG,
		 END_ARG_LIST}
};

static struct gsh_dbus_signal *admin_signals[] = {
	&heartbeat_signal,
	NULL
};

static struct gsh_dbus_interface admin_interface = {
	.name = DBUS_ADMIN_IFACE,
	.props = NULL,
	.methods = admin_methods,
	.signals = admin_signals
};

static struct gsh_dbus_interface *admin_interfaces[] = {
	&admin_interface,
	&log_interface,
	NULL
};

#endif				/* USE_DBUS */

/**
 * @brief Initialize admin thread control state and DBUS methods.
 */

void nfs_Init_admin_thread(void)
{
#ifdef USE_DBUS
	gsh_dbus_register_path("admin", admin_interfaces);
#endif				/* USE_DBUS */
	LogEvent(COMPONENT_NFS_CB, "Admin thread initialized");
}

/**
 * @brief Signal the admin thread to shut down the system
 */

void admin_halt(void)
{
	PTHREAD_MUTEX_lock(&admin_control_mtx);

	if (!admin_shutdown) {
		admin_shutdown = true;
		pthread_cond_broadcast(&admin_control_cv);
	}

	PTHREAD_MUTEX_unlock(&admin_control_mtx);
}

static void do_shutdown(void)
{
	int rc = 0;
	bool disorderly = false;

	LogEvent(COMPONENT_MAIN, "NFS EXIT: stopping NFS service");

	rados_url_shutdown_watch();
	config_url_shutdown();

#ifdef USE_DBUS
	/* DBUS shutdown */
	gsh_dbus_pkgshutdown();
#endif

	LogEvent(COMPONENT_MAIN, "Stopping delayed executor.");
	delayed_shutdown();
	LogEvent(COMPONENT_MAIN, "Delayed executor stopped.");

	LogEvent(COMPONENT_MAIN, "Stopping state asynchronous request thread");
	rc = state_async_shutdown();
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Error shutting down state asynchronous request system: %d",
			 rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD,
			 "State asynchronous request system shut down.");
	}

	LogEvent(COMPONENT_MAIN, "Unregistering ports used by NFS service");
	/* finalize RPC package */
	Clean_RPC();

	LogEvent(COMPONENT_MAIN, "Shutting down RPC services");
	(void)svc_shutdown(SVC_SHUTDOWN_FLAG_NONE);

	LogEvent(COMPONENT_MAIN, "Stopping worker threads");
#ifdef _USE_9P
	rc = _9p_worker_shutdown();

	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to shut down worker threads: %d", rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD,
			 "Worker threads successfully shut down.");
	}
#endif

	rc = general_fridge_shutdown();
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Error shutting down general fridge: %d", rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD, "General fridge shut down.");
	}

	rc = reaper_shutdown();
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Error shutting down reaper thread: %d", rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD, "Reaper thread shut down.");
	}

	LogEvent(COMPONENT_MAIN, "Removing all exports.");
	remove_all_exports();

	LogEvent(COMPONENT_MAIN, "Removing all DSs.");
	remove_all_dss();

	nfs4_recovery_shutdown();

	if (disorderly) {
		LogMajor(COMPONENT_MAIN,
			 "Error in shutdown, taking emergency cleanup.");
		/* We don't attempt to free state, clean the cache,
		   or unload the FSALs more cleanly, since doing
		   anything more than this risks hanging up on
		   potentially invalid locks. */
		emergency_cleanup_fsals();
	} else {

		LogEvent(COMPONENT_MAIN, "Destroying the FSAL system.");
		destroy_fsals();
		LogEvent(COMPONENT_MAIN, "FSAL system destroyed.");
	}

	unlink(nfs_pidfile_path);
}

void *admin_thread(void *UnusedArg)
{
	SetNameFunction("Admin");
	rcu_register_thread();

	PTHREAD_MUTEX_lock(&admin_control_mtx);

	while (!admin_shutdown) {
		/* Wait for shutdown indication. */
		pthread_cond_wait(&admin_control_cv, &admin_control_mtx);
	}

	PTHREAD_MUTEX_unlock(&admin_control_mtx);

	do_shutdown();

	rcu_unregister_thread();
	return NULL;
}
