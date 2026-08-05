// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include <mcheck.h> /* For mtrace/muntrace */
#endif
#ifndef __APPLE__
#include <malloc.h>
#endif

#include "nfs_core.h"
#include "log.h"
#include "sal_functions.h"
#include "sal_data.h"
#include "idmapper.h"
#include "uid2grp.h"
#include "delayed_exec.h"
#include "export_mgr.h"
#include "pnfs_utils.h"
#include "fsal.h"
#include "netgroup_cache.h"
#include "nfs_dupreq.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#include "mdcache.h"
#include "nfs_lib.h"
#endif
#include "conf_url.h"
#include "nfs_rpc_callback.h"
#ifdef USE_MONITORING
#include "prometheus_exposer.h"
#endif
#include "nfs_qos.h"
#ifdef USE_GRPC
#include "GrpcServerInit.h"
#endif
#ifdef USE_DBUS
#ifdef _INTERNAL_STATD
#include "cluster_members.h"
#endif
#endif

/**
 * @brief Mutex protecting shutdown flag.
 */

static pthread_mutex_t admin_control_mtx;

/**
 * @brief Condition variable to signal change in shutdown flag.
 */

static pthread_cond_t admin_control_cv;

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
static bool admin_dbus_get_grace(DBusMessageIter *args, DBusMessage *reply,
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_get_grace = {
	.name = "get_grace",
	.method = admin_dbus_get_grace,
	.args = { {
			  .name = "isgrace",
			  .type = "b",
			  .direction = "out",
		  },
		  STATUS_REPLY,
		  END_ARG_LIST }
};

/**
 * @brief Dbus method start grace period
 *
 * @param[in]  args  Unused
 * @param[out] reply Unused
 */

static bool admin_dbus_grace(DBusMessageIter *args, DBusMessage *reply,
			     DBusError *error)
{
	char *errormsg = "Started grace period";
	bool success = true;
	DBusMessageIter iter;
	nfs_grace_start_t gsp = {
		.event = EVENT_JUST_GRACE,
		.nodeid = -1,
		.ipaddr = NULL,
	};
	char *input = NULL;
	char *end = NULL;
	char *arg = NULL;
	size_t size;
	int ret;

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

	gsp.event = strtol(input, &end, 10);
	if (end == input) {
		goto invalid_dbus_arg;
	}
	if (*end != '\0' && *end != ':') {
		goto invalid_dbus_arg;
	}
	if (*end == ':') {
		arg = end + 1;
	} else {
		arg = NULL;
	}
	/* Backward compatibility: treat empty argument as no argument */
	if (arg != NULL && *arg == '\0') {
		arg = NULL;
	}

	switch (gsp.event) {
	case EVENT_JUST_GRACE:
	case EVENT_CLEAR_BLOCKED:
	case EVENT_UPDATE_CLIENTS:
		if (arg != NULL) {
			goto invalid_dbus_arg;
		}
		break;
	case EVENT_RELEASE_IP:
	case EVENT_TAKE_IP:
		if (arg == NULL) {
			goto invalid_dbus_arg;
		}
		ret = ip_str_to_sockaddr(COMPONENT_DBUS, arg, &gsp.sa);
		if (ret != 0) {
			goto invalid_dbus_arg;
		}
		size = strlen(arg) + 1;
		gsp.ipaddr = alloca(size);
		memcpy(gsp.ipaddr, arg, size);
		break;
	case EVENT_TAKE_NODEID:
		if (arg == NULL) {
			goto invalid_dbus_arg;
		}
		gsp.nodeid = strtol(arg, &end, 10);
		if (end == arg) {
			goto invalid_dbus_arg;
		}
		break;
	default:
		goto invalid_dbus_arg;
	}

	do {
		ret = nfs_start_grace(&gsp);
		/*
		 * grace could fail if there are refs taken.
		 * wait for no refs and retry.
		 */
		if (ret == -EAGAIN) {
			LogEvent(COMPONENT_DBUS, "Retry grace");
			nfs_wait_for_grace_norefs();
		} else if (ret) {
			LogCrit(COMPONENT_DBUS, "Start grace failed %d", ret);
			success = false;
			errormsg = "Unable to start grace";
			break;
		}
	} while (ret);
out:
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;

invalid_dbus_arg:
	success = false;
	errormsg = "Unable to start grace (invalid argument)";
	LogWarn(COMPONENT_DBUS, "%s", errormsg);
	goto out;
}

static struct gsh_dbus_method method_grace_period = {
	.name = "grace",
	.method = admin_dbus_grace,
	.args = { IPADDR_ARG, STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for shutting down Ganesha
 *
 * @param[in]  args  Unused
 * @param[out] reply Unused
 */

static bool admin_dbus_shutdown(DBusMessageIter *args, DBusMessage *reply,
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_shutdown = { .name = "shutdown",
						  .method = admin_dbus_shutdown,
						  .args = { STATUS_REPLY,
							    END_ARG_LIST } };

static void drc_to_dbus(drc_t *drc, void *state)
{
	DBusMessageIter struct_iter;
	DBusMessageIter *array_iter = (DBusMessageIter *)state;
	char client_ip[SOCK_NAME_MAX] = { 0 };
	char *ipaddr = client_ip;
	int port;
	const char *str = NULL;

	if (!drc) {
		LogEvent(COMPONENT_DBUS, "Skipping NULL drc");
		return;
	}

	if (!sprint_sockip(&drc->d_u.tcp.addr, ipaddr, SOCK_NAME_MAX))
		(void)strlcpy(ipaddr, "<unknown>", SOCK_NAME_MAX);

	port = get_sockport(&drc->d_u.tcp.addr);

	dbus_message_iter_open_container(array_iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	str = "Client addr:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &str);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &ipaddr);
	str = "Client port:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &str);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32, &port);
	str = "Number of DRC entries:";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &str);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &drc->size);
	dbus_message_iter_close_container(array_iter, &struct_iter);
}

/**
 * @brief Dbus method get the drc info
 *
 * @param[in]  args
 */
static bool admin_dbus_get_drc_info(DBusMessageIter *args, DBusMessage *reply,
				    DBusError *error)
{
	char *errormsg = "get drc into success";
	const char *str = NULL;
	bool success = true;
	uint32_t counter;
	DBusMessageIter iter, array_iter, struct_iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Get drc info takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(sssusu)",
					 &array_iter);

	counter = for_each_tcp_drc(drc_to_dbus, (void *)&array_iter);

	dbus_message_iter_close_container(&iter, &array_iter);

	str = "Number of DRCs:";
	dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &str);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &counter);
	dbus_message_iter_close_container(&iter, &struct_iter);

	counter = get_tcp_drc_recycle_qlen();
	str = "Number of inactive DRCs:";
	dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &str);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &counter);
	dbus_message_iter_close_container(&iter, &struct_iter);

out:
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_get_drc_info = {
	.name = "get_drc_info",
	.method = admin_dbus_get_drc_info,
	.args = { {
			  .name = "drc_info",
			  .type = "a(sssusu)",
			  .direction = "out",
		  },
		  {
			  .name = "num_of_drcs",
			  .type = "(su)",
			  .direction = "out",
		  },
		  {
			  .name = "num_of_inactive_drcs",
			  .type = "(su)",
			  .direction = "out",
		  },
		  STATUS_REPLY,
		  END_ARG_LIST }
};

/**
 * @brief Dbus method for flushing manage gids cache
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_purge_gids(DBusMessageIter *args, DBusMessage *reply,
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_purge_gids = {
	.name = "purge_gids",
	.method = admin_dbus_purge_gids,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for flushing netgroup cache
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_purge_netgroups(DBusMessageIter *args,
				       DBusMessage *reply, DBusError *error)
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_purge_netgroups = {
	.name = "purge_netgroups",
	.method = admin_dbus_purge_netgroups,
	.args = { STATUS_REPLY, END_ARG_LIST }
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_purge_idmapper_cache = {
	.name = "purge_idmapper_cache",
	.method = admin_dbus_purge_idmapper_cache,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for flushing idmapper negative cache
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_purge_idmapper_negative_cache(DBusMessageIter *args,
						     DBusMessage *reply,
						     DBusError *error)
{
	char *errormsg = "Purge idmapper negative cache";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	if (args != NULL) {
		errormsg = "Purge idmapper negative cache takes no arguments.";
		success = false;
		LogWarn(COMPONENT_DBUS, "%s", errormsg);
		goto out;
	}
	idmapper_negative_cache_clear();
out:
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_purge_idmapper_negative_cache = {
	.name = "purge_idmapper_negative_cache",
	.method = admin_dbus_purge_idmapper_negative_cache,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for updating open fd limit
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_init_fds_limit(DBusMessageIter *args, DBusMessage *reply,
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_init_fds_limit = {
	.name = "init_fds_limit",
	.method = admin_dbus_init_fds_limit,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for enabling malloc trace
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_malloc_trace(DBusMessageIter *args, DBusMessage *reply,
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

/**
 * @brief Dbus method for disabling malloc trace
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_malloc_untrace(DBusMessageIter *args, DBusMessage *reply,
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
	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_malloc_trace = {
	.name = "malloc_trace",
	.method = admin_dbus_malloc_trace,
	.args = { { .name = "tracefile", .type = "s", .direction = "in" },
		  STATUS_REPLY,
		  END_ARG_LIST }
};

static struct gsh_dbus_method method_malloc_untrace = {
	.name = "malloc_untrace",
	.method = admin_dbus_malloc_untrace,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for enabling malloc trim
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_trim_enable(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	char *errormsg = "Malloc trim enabled";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	LogEvent(COMPONENT_MEMLEAKS, "enabling malloc_trim");
	nfs_param.core_param.malloc_trim = true;
	gsh_dbus_status_reply(&iter, success, errormsg);

	return success;
}

static struct gsh_dbus_method method_trim_enable = {
	.name = "trim_enable",
	.method = admin_dbus_trim_enable,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for disabling malloc trim
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_trim_disable(DBusMessageIter *args, DBusMessage *reply,
				    DBusError *error)
{
	char *errormsg = "Malloc trim disabled";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	LogEvent(COMPONENT_MEMLEAKS, "disabling malloc_trim");
	nfs_param.core_param.malloc_trim = false;
	gsh_dbus_status_reply(&iter, success, errormsg);

	return success;
}

static struct gsh_dbus_method method_trim_disable = {
	.name = "trim_disable",
	.method = admin_dbus_trim_disable,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for calling malloc_trim()
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_trim_call(DBusMessageIter *args, DBusMessage *reply,
				 DBusError *error)
{
	char *errormsg = "malloc_trim() called";
	bool success = true;
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	LogEvent(COMPONENT_MEMLEAKS, "Calling malloc_trim");
	malloc_trim(0);
	gsh_dbus_status_reply(&iter, success, errormsg);

	return success;
}

static struct gsh_dbus_method method_trim_call = {
	.name = "trim_call",
	.method = admin_dbus_trim_call,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for getting trim status
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_dbus_trim_status(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	char *errormsg = "Malloc trim status: enabled";
	bool success = true;
	DBusMessageIter iter;
	char hostname[64 + 1] = { 0 };
	char name[100];
	FILE *fp;

	/* log malloc_info() as a side effect! */
	(void)gethostname(hostname, sizeof(hostname));
	snprintf(name, sizeof(name), "/tmp/mallinfo-%s.%d.txt", hostname,
		 getpid());
	fp = fopen(name, "w");
	if (fp != NULL) {
		malloc_info(0, fp);
		fclose(fp);
	}

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.malloc_trim)
		errormsg = "Malloc trim status: disabled";
	gsh_dbus_status_reply(&iter, success, errormsg);

	return success;
}

static struct gsh_dbus_method method_trim_status = {
	.name = "trim_status",
	.method = admin_dbus_trim_status,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Dbus method for getting trim status
 *
 * @param[in]  args
 * @param[out] reply
 */
static bool admin_reread_config(DBusMessageIter *args, DBusMessage *reply,
				DBusError *error)
{
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);

	bool success = reread_config();

	if (!success)
		errormsg = "Failed to reread config";

	gsh_dbus_status_reply(&iter, success, errormsg);
	return success;
}

static struct gsh_dbus_method method_reread_config = {
	.name = "reread_config",
	.method = admin_reread_config,
	.args = { STATUS_REPLY, END_ARG_LIST }
};

#ifdef _INTERNAL_STATD
#define CLUSTER_MEMBERS_ARG \
	{ .name = "ipaddrs", .type = "as", .direction = "in" }

#define CLUSTER_MEMBERS_REPLY \
	{ .name = "ipaddrs", .type = "as", .direction = "out" }

/**
 * @brief Replace Cluster_Members at runtime (integrated NSM peer list)
 */
static bool admin_dbus_set_cluster_members(DBusMessageIter *args,
					   DBusMessage *reply, DBusError *error)
{
	char *errormsg = "OK";
	bool success = true;
	DBusMessageIter iter;
	DBusMessageIter array_iter;
	char **ips = NULL;
	size_t count = 0;
	size_t capacity = 0;

	dbus_message_iter_init_append(reply, &iter);

	if (args == NULL ||
	    dbus_message_iter_get_arg_type(args) != DBUS_TYPE_ARRAY) {
		success = false;
		errormsg = "SetClusterMembers expects an array of IP strings";
		goto out;
	}

	dbus_message_iter_recurse(args, &array_iter);

	while (dbus_message_iter_get_arg_type(&array_iter) ==
	       DBUS_TYPE_STRING) {
		char *ip = NULL;

		dbus_message_iter_get_basic(&array_iter, &ip);

		if (count == capacity) {
			capacity = capacity == 0 ? 8 : capacity * 2;
			ips = gsh_realloc(ips, capacity * sizeof(*ips),
					  MEM_COMP_MANAGE);
		}
		ips[count++] = ip;
		dbus_message_iter_next(&array_iter);
	}

	success = set_cluster_members(ips, count, &errormsg);

out:
	gsh_free(ips, MEM_COMP_MANAGE);
	gsh_dbus_status_reply(&iter, success, errormsg);
	/* Always return true so STATUS_REPLY is delivered (see AddClient). */
	return true;
}

static struct gsh_dbus_method method_set_cluster_members = {
	.name = "SetClusterMembers",
	.method = admin_dbus_set_cluster_members,
	.args = { CLUSTER_MEMBERS_ARG, STATUS_REPLY, END_ARG_LIST }
};

/**
 * @brief Show current Cluster_Members peer list
 */
static bool admin_dbus_show_cluster_members(DBusMessageIter *args,
					    DBusMessage *reply,
					    DBusError *error)
{
	char *errormsg = "OK";
	bool success = true;
	DBusMessageIter iter;
	DBusMessageIter array_iter;
	char **ips = NULL;
	size_t count = 0;
	size_t i;

	dbus_message_iter_init_append(reply, &iter);

	if (args != NULL) {
		success = false;
		errormsg = "ShowClusterMembers takes no arguments";
		/* Keep reply layout: as (ipaddrs) + STATUS_REPLY */
		if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
						      "s", &array_iter) ||
		    !dbus_message_iter_close_container(&iter, &array_iter)) {
			errormsg = "Failed to build ShowClusterMembers reply";
		}
		gsh_dbus_status_reply(&iter, success, errormsg);
		return true;
	}

	success = show_cluster_members(&ips, &count, &errormsg);

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s",
					      &array_iter)) {
		success = false;
		errormsg = "Failed to build ShowClusterMembers reply";
		goto out;
	}

	if (success) {
		for (i = 0; i < count; i++) {
			const char *ip = ips[i];

			if (!dbus_message_iter_append_basic(
				    &array_iter, DBUS_TYPE_STRING, &ip)) {
				success = false;
				errormsg =
					"Failed to append Cluster_Members addr";
				break;
			}
		}
	}

	dbus_message_iter_close_container(&iter, &array_iter);

out:
	free_cluster_members_list(ips, count);
	gsh_dbus_status_reply(&iter, success, errormsg);
	/* Always return true so STATUS_REPLY is delivered (see AddClient). */
	return true;
}

static struct gsh_dbus_method method_show_cluster_members = {
	.name = "ShowClusterMembers",
	.method = admin_dbus_show_cluster_members,
	.args = { CLUSTER_MEMBERS_REPLY, STATUS_REPLY, END_ARG_LIST }
};
#endif /* _INTERNAL_STATD */

static struct gsh_dbus_method *admin_methods[] = {
	&method_shutdown,
	&method_grace_period,
	&method_get_grace,
	&method_purge_gids,
	&method_purge_netgroups,
	&method_init_fds_limit,
	&method_purge_idmapper_cache,
	&method_purge_idmapper_negative_cache,
	&method_malloc_trace,
	&method_malloc_untrace,
	&method_trim_enable,
	&method_trim_disable,
	&method_trim_call,
	&method_trim_status,
	&method_reread_config,
#ifdef _INTERNAL_STATD
	&method_set_cluster_members,
	&method_show_cluster_members,
#endif
	&method_get_drc_info,
	NULL
};

/* clang-format off */

#define HANDLE_VERSION_PROP(prop_name, prop_string)                           \
	static bool dbus_prop_get_VERSION_##prop_name(DBusMessageIter *reply) \
	{                                                                     \
		const char *version_string = prop_string;                     \
		if (!dbus_message_iter_append_basic(reply, DBUS_TYPE_STRING,  \
						    &version_string))         \
			return false;                                         \
		return true;                                                  \
	}                                                                     \
									      \
	static struct gsh_dbus_prop VERSION_##prop_name##_prop = {            \
		.name = "VERSION_" #prop_name,                                \
		.access = DBUS_PROP_READ,                                     \
		.type = "s",                                                  \
		.get = dbus_prop_get_VERSION_##prop_name,                     \
		.set = NULL                                                   \
	}

/* clang-format on */

#define VERSION_PROPERTY_ITEM(name) (&VERSION_##name##_prop)

HANDLE_VERSION_PROP(RELEASE, GANESHA_VERSION);

#if !GANESHA_BUILD_RELEASE
HANDLE_VERSION_PROP(COMPILE_DATE, __DATE__);
HANDLE_VERSION_PROP(COMPILE_TIME, __TIME__);
HANDLE_VERSION_PROP(COMMENT, VERSION_COMMENT);
HANDLE_VERSION_PROP(GIT_HEAD, _GIT_HEAD_COMMIT);
HANDLE_VERSION_PROP(GIT_DESCRIBE, _GIT_DESCRIBE);
#endif

static struct gsh_dbus_prop *admin_props[] = {
	VERSION_PROPERTY_ITEM(RELEASE),
#if !GANESHA_BUILD_RELEASE
	VERSION_PROPERTY_ITEM(COMPILE_DATE),
	VERSION_PROPERTY_ITEM(COMPILE_TIME),
	VERSION_PROPERTY_ITEM(COMMENT),
	VERSION_PROPERTY_ITEM(GIT_HEAD),
	VERSION_PROPERTY_ITEM(GIT_DESCRIBE),
#endif
	NULL
};

static struct gsh_dbus_signal heartbeat_signal = { .name = HEARTBEAT_NAME,
						   .signal = NULL,
						   .args = { HEARTBEAT_ARG,
							     END_ARG_LIST } };

static struct gsh_dbus_signal *admin_signals[] = { &heartbeat_signal, NULL };

static struct gsh_dbus_interface admin_interface = { .name = DBUS_ADMIN_IFACE,
						     .props = admin_props,
						     .methods = admin_methods,
						     .signals = admin_signals };

static struct gsh_dbus_interface *admin_interfaces[] = {
	&admin_interface, &log_interface, &log_conditional_interface, NULL
};

#endif /* USE_DBUS */

/**
 * @brief Initialize admin thread control state and DBUS methods.
 */

void nfs_Init_admin_thread(void)
{
	PTHREAD_MUTEX_init(&admin_control_mtx, NULL);
	PTHREAD_COND_init(&admin_control_cv, NULL);
#ifdef USE_DBUS
	gsh_dbus_register_path("admin", admin_interfaces);
#endif /* USE_DBUS */
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
		PTHREAD_COND_broadcast(&admin_control_cv);
	}

	PTHREAD_MUTEX_unlock(&admin_control_mtx);
	LogEvent(COMPONENT_MAIN, "NFS EXIT: %s done", __func__);
}

static void do_shutdown(void)
{
	int rc = 0;
	bool disorderly = false;

	LogEvent(COMPONENT_MAIN, "NFS EXIT: stopping NFS service");

	gsh_rados_url_shutdown_watch();

	config_url_shutdown();

#ifdef USE_DBUS
	/* DBUS shutdown */
	gsh_dbus_pkgshutdown();
#endif

#ifdef USE_GRPC
	/* GRPC shutdown  */
	if (nfs_param.grpc_param.grpc_enable)
		grpc_shutdown();
#endif
	LogEvent(COMPONENT_MAIN, "Stopping delayed executor.");
	delayed_shutdown();
	LogEvent(COMPONENT_MAIN, "Delayed executor stopped.");

#ifdef ENABLE_QOS
	/* QOS shutdown */
	if (qos_block_config.enable_qos)
		shutdown_qos();
#endif

	LogEvent(COMPONENT_MAIN, "Stopping state asynchronous request thread");
	rc = state_async_shutdown();
	if (rc != 0) {
		LogMajor(
			COMPONENT_THREAD,
			"Error shutting down state asynchronous request system: %d",
			rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD,
			 "State asynchronous request system shut down.");
	}

#ifdef _INTERNAL_STATD
	shutdown_nsm_notify_thread();
#endif

	LogEvent(COMPONENT_MAIN, "Unregistering ports used by NFS service");
	/* finalize RPC package */
	Clean_RPC();

#ifdef USE_MONITORING
	/* close monitoring service */
	if (nfs_param.core_param.enable_metrics) {
		prometheus_exposer__stop(monitoring__get_registry_handle());
	}
#endif

	LogEvent(COMPONENT_MAIN, "Shutting down RPC services");
	(void)svc_shutdown(SVC_SHUTDOWN_FLAG_NONE);

	LogEvent(COMPONENT_MAIN, "Stopping reaper threads");
	rc = reaper_shutdown();
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Error shutting down reaper thread: %d", rc);
		disorderly = true;
	} else {
		LogEvent(COMPONENT_THREAD, "Reaper thread shut down.");
	}

	LogEvent(COMPONENT_MAIN, "Stopping worker threads");
#ifdef _USE_9P
	if (nfs_param.core_param.core_options & CORE_OPTION_9P) {
		rc = _9p_worker_shutdown();

		if (rc != 0) {
			LogMajor(COMPONENT_THREAD,
				 "Unable to shut down worker threads: %d", rc);
			disorderly = true;
		} else {
			LogEvent(COMPONENT_THREAD,
				 "Worker threads successfully shut down.");
		}
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

	rc = nfs4_copy_fridge_shutdown();
	if (rc != 0 && rc != ETIMEDOUT) {
		LogMajor(COMPONENT_THREAD,
			 "Error shutting down copy fridge (xcopy): %d", rc);
		disorderly = true;
	}

	/* We have to remove DS before exports, DS refer to exports but
	 * exports do not refer to DS. This SHOULD remove every single DS.
	 */
	LogEvent(COMPONENT_MAIN, "Removing all DSs.");
	remove_all_dss();

	LogEvent(COMPONENT_MAIN, "Removing all exports.");
	remove_all_exports();

	nfs4_recovery_shutdown();
	nfs_rpc_cb_pkgshutdown();

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
	PTHREAD_MUTEX_destroy(&admin_control_mtx);
	PTHREAD_COND_destroy(&admin_control_cv);
	LogEvent(COMPONENT_MAIN, "NFS EXIT: %s done", __func__);
}

void *admin_thread(void *UnusedArg)
{
	SetNameFunction("Admin");
	rcu_register_thread();

	PTHREAD_MUTEX_lock(&admin_control_mtx);

	while (!admin_shutdown) {
		/* Wait for shutdown indication. */
		PTHREAD_COND_wait(&admin_control_cv, &admin_control_mtx);
	}

	PTHREAD_MUTEX_unlock(&admin_control_mtx);

	do_shutdown();

	rcu_unregister_thread();
	return NULL;
}
