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
#include "nfs_core.h"
#include "log.h"
#include "sal_functions.h"
#include "sal_data.h"
#include "cache_inode_lru.h"
#include "idmapper.h"
#include "delayed_exec.h"
#include "export_mgr.h"
#ifdef USE_DBUS
#include "ganesha_dbus.h"
#endif

struct glist_head temp_exportlist;

/**
 * @brief Mutex protecting command and status
 */

static pthread_mutex_t admin_control_mtx = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Condition variable on which commands and states are
 * signalled.
 */

static pthread_cond_t admin_control_cv = PTHREAD_COND_INITIALIZER;

/**
 * @brief Commands issued to the admin thread
 */

typedef enum {
	admin_none_pending,	/*< No command.  The admin thread sets this on
				   startup and after */
	admin_shutdown		/*< Shut down Ganesha */
} admin_command_t;

/**
 * @brief Current state of the admin thread
 */

typedef enum {
	admin_stable,		/*< The admin thread is not doing an action. */
	admin_shutting_down,	/*< The admin thread is shutting down Ganesha */
	admin_halted		/*< All threads should exit. */
} admin_status_t;

static admin_command_t admin_command;
static admin_status_t admin_status;

#ifdef USE_DBUS

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
	if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(args)) {
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
	nfs4_start_grace(&gsp);
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

static struct gsh_dbus_method *admin_methods[] = {
	&method_shutdown,
	&method_grace_period,
	&method_purge_gids,
	NULL
};

static struct gsh_dbus_interface admin_interface = {
	.name = "org.ganesha.nfsd.admin",
	.props = NULL,
	.methods = admin_methods,
	.signals = NULL
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
	admin_command = admin_none_pending;
	admin_status = admin_stable;
#ifdef USE_DBUS
	gsh_dbus_register_path("admin", admin_interfaces);
#endif				/* USE_DBUS */
	LogEvent(COMPONENT_NFS_CB, "Admin thread initialized");
}

static void admin_issue_command(admin_command_t command)
{
	pthread_mutex_lock(&admin_control_mtx);
	while ((admin_command != admin_none_pending)
	       && ((admin_status != admin_stable)
		   || (admin_status != admin_halted))) {
		pthread_cond_wait(&admin_control_cv, &admin_control_mtx);
	}
	if (admin_status == admin_halted) {
		pthread_mutex_unlock(&admin_control_mtx);
		return;
	}
	admin_command = command;
	pthread_cond_broadcast(&admin_control_cv);
	pthread_mutex_unlock(&admin_control_mtx);
}

/**
 * @brief Signal the admin thread to replace the exports
 */

void admin_replace_exports(void)
{
	admin_issue_command(admin_command);
}

/**
 * @brief Signal the admin thread to shut down the system
 */

void admin_halt(void)
{
	admin_issue_command(admin_shutdown);
}

static void do_shutdown(void)
{
	int rc = 0;
	bool disorderly = false;

	LogEvent(COMPONENT_MAIN, "NFS EXIT: stopping NFS service");

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

	LogEvent(COMPONENT_MAIN, "Stopping request listener threads.");
	nfs_rpc_dispatch_stop();

	LogEvent(COMPONENT_MAIN, "Stopping request decoder threads");
	rc = fridgethr_sync_command(req_fridge, fridgethr_comm_stop, 120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_THREAD,
			 "Shutdown timed out, cancelling threads!");
		fridgethr_cancel(req_fridge);
		disorderly = true;
	} else if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Failed to shut down the request thread fridge: %d!",
			 rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD, "Request threads shut down.");
	}

	LogEvent(COMPONENT_MAIN, "Stopping worker threads");

	rc = worker_shutdown();

	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to shut down worker threads: %d", rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD,
			 "Worker threads successfully shut down.");
	}

	/* finalize RPC package */
	Clean_RPC(); /* we MUST do this first */
	(void)svc_shutdown(SVC_SHUTDOWN_FLAG_NONE);

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

	LogEvent(COMPONENT_MAIN, "Stopping LRU thread.");
	rc = cache_inode_lru_pkgshutdown();
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Error shutting down LRU thread: %d",
			 rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD, "LRU thread system shut down.");
	}

	LogEvent(COMPONENT_MAIN, "Removing all exports.");
	remove_all_exports();

	if (disorderly) {
		LogMajor(COMPONENT_MAIN,
			 "Error in shutdown, taking emergency cleanup.");
		/* We don't attempt to free state, clean the cache,
		   or unload the FSALs more cleanly, since doing
		   anything more than this risks hanging up on
		   potentially invalid locks. */
		emergency_cleanup_fsals();
	} else {
		LogEvent(COMPONENT_MAIN, "Destroying the inode cache.");
		cache_inode_destroyer();
		LogEvent(COMPONENT_MAIN, "Inode cache destroyed.");

		LogEvent(COMPONENT_MAIN, "Destroying the FSAL system.");
		destroy_fsals();
		LogEvent(COMPONENT_MAIN, "FSAL system destroyed.");
	}

	unlink(pidfile_path);
}

void *admin_thread(void *UnusedArg)
{
	SetNameFunction("Admin");

	pthread_mutex_lock(&admin_control_mtx);
	while (admin_command != admin_shutdown) {
		if (admin_command != admin_none_pending)
			continue;
		pthread_cond_wait(&admin_control_cv, &admin_control_mtx);
	}

	admin_command = admin_none_pending;
	admin_status = admin_shutting_down;
	pthread_cond_broadcast(&admin_control_cv);
	pthread_mutex_unlock(&admin_control_mtx);
	do_shutdown();
	pthread_mutex_lock(&admin_control_mtx);
	admin_status = admin_halted;
	pthread_cond_broadcast(&admin_control_cv);
	pthread_mutex_unlock(&admin_control_mtx);

	return NULL;
}				/* admin_thread */
