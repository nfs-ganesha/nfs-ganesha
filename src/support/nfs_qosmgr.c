// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM. All rights reserved.
 * Author: Deeraj Patil <deeraj.patil@ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

/**
 * @file nfs_qosmgr.c
 * @brief Routines used for managing the QOS via DBUS.
 *	-> Run time updation of BW etc.
 *
 *
 */
#include "nfs_core.h"
#include "nfs_qos.h"
#include "nfs_qosmgr.h"
#ifdef USE_GRPC
#include <string.h>
#include "nfs_qos_grpc.h"
#include "export_mgr.h"
#include "client_mgr.h"
#include "ip_utils.h"
#include "abstract_mem.h"
#endif
/*  QoS Method Arguments */
#define CLIENT_IP_ARG { "client_ip", "s", "in" }
#define EXPORT_ID_ARG { "id", "q", "in" }

#define READ_BW_IN_ARG { "read_bw", "t", "in" }
#define WRITE_BW_IN_ARG { "write_bw", "t", "in" }
#define READ_BW_OUT_ARG { "read_bw", "t", "out" }
#define WRITE_BW_OUT_ARG { "write_bw", "t", "out" }

#define READ_IOPS_IN_ARG { "read_iops", "t", "in" }
#define WRITE_IOPS_IN_ARG { "write_iops", "t", "in" }
#define READ_IOPS_OUT_ARG { "read_iops", "t", "out" }
#define WRITE_IOPS_OUT_ARG { "write_iops", "t", "out" }

#define MAX_TOKENS_IN { "max_tokens", "u", "in" }
#define MAX_TOKENS_OUT { "max_tokens", "u", "out" }
#define TOKEN_RENEW_IN { "token_renewal", "t", "in" }
#define TOKEN_RENEW_OUT { "token_renewal", "t", "out" }

#define CLIENT_LIST_ARG { "client_list", "a(sttuu)", "out" }
#define TOTAL_CLIENTS { "total_clients", "u", "out" }
#define OFFSET_ARG { "offset", "u", "in" }
#define LIMIT_ARG { "limit", "u", "in" }
#define QOS_CLIENT_CONTAINER "(s(ss)(ss)(ss))"
#define QOS_CLIENTS_REPLY { "clients", "a(s(ss)(ss)(ss))", "out" }
#define SUCCESS_ARG { "success", "b", "out" }

struct showclients_state {
	DBusMessageIter client_iter;
};

/* Use the MACROs to directly return from function.
 * Otherwise use the below functions equivalent to macros for checking
 * and releasing the resource before returning */

#define CHECK_DBUS_NEXT_ARG_OR_return(args, type, iter, msg)        \
	do {                                                        \
		if (!dbus_message_iter_next(args) ||                \
		    dbus_message_iter_get_arg_type(args) != type) { \
			gsh_dbus_status_reply(&iter, false, msg);   \
			return false;                               \
		}                                                   \
	} while (0)

#define CHECK_DBUS_ARG_OR_return(args, type, iter, msg)                      \
	do {                                                                 \
		if (!args || dbus_message_iter_get_arg_type(args) != type) { \
			gsh_dbus_status_reply(&iter, false, msg);            \
			return false;                                        \
		}                                                            \
	} while (0)

#define CHECK_ARG_OR_return(args, iter, msg)                      \
	do {                                                      \
		if (!args) {                                      \
			gsh_dbus_status_reply(&iter, false, msg); \
			return false;                             \
		}                                                 \
	} while (0)

/**
 * @brief Check if the next argument is of the expected type.
 * @param args DBusMessageIter pointer
 * @param type expected type
 * @param iter DBusMessageIter pointer
 * @param msg error message
 * @return true if the argument is of the expected type, false otherwise
 */
static bool check_dbus_next_arg(DBusMessageIter *args, int type,
				DBusMessageIter *iter, char *msg)
{
	if (!dbus_message_iter_next(args) ||
	    dbus_message_iter_get_arg_type(args) != type) {
		gsh_dbus_status_reply(iter, false, msg);
		return false;
	}
	return true;
}

/**
 * @brief Check if the argument is valid and of the expected type.
 * @param args DBusMessageIter pointer
 * @param type expected type
 * @param iter DBusMessageIter pointer
 * @param msg error message
 * @return true if the argument is valid and of the expected type, else false.
 */
static bool check_dbus_arg(DBusMessageIter *args, int type,
			   DBusMessageIter *iter, char *msg)
{
	if (!args || dbus_message_iter_get_arg_type(args) != type) {
		gsh_dbus_status_reply(iter, false, msg);
		return false;
	}
	return true;
}

/**
 * @brief Check if the argument is not null.
 * @param args pointer to check
 * @param iter DBusMessageIter pointer
 * @param msg error message
 * @return true if the argument is not null, false otherwise
 */
static bool check_arg(void *args, DBusMessageIter *iter, char *msg)
{
	if (!args) {
		gsh_dbus_status_reply(iter, false, msg);
		return false;
	}
	return true;
}

/**
 * @brief Set bandwidth settings for a client.
 *
 * This function sets the bandwidth limits for a specified client.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_client_bw_set(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	struct gsh_client *gsh_client;
	uint64_t read_bw, write_bw;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool ret = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_STRING, iter,
				 "Invalid arg ClientIP");
	gsh_client = lookup_client(args, &errormsg);
	CHECK_ARG_OR_return(gsh_client, iter, "Client IP address not found");

	CHECK_DBUS_NEXT_ARG_OR_return(args, DBUS_TYPE_UINT64, iter,
				      "Invalid arg read_bw");
	dbus_message_iter_get_basic(args, &read_bw);
	CHECK_DBUS_NEXT_ARG_OR_return(args, DBUS_TYPE_UINT64, iter,
				      "Invalid arg write_bw");
	dbus_message_iter_get_basic(args, &write_bw);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_client->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_bw_allowed = read_bw;
		qos_class->wbucket.max_bw_allowed = write_bw;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		ret = true;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	return ret;
}

/**
 * @brief Set token settings for a client.
 *
 * This function sets the token limits for a specified client.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_client_token_set(DBusMessageIter *args, DBusMessage *reply,
				      DBusError *error)
{
	struct gsh_client *gsh_client;
	uint64_t max_tokens;
	uint64_t token_renewal;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool ret = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_STRING, iter,
				 "Invalid arg ClientIP");
	gsh_client = lookup_client(args, &errormsg);
	CHECK_ARG_OR_return(gsh_client, iter, errormsg);

	CHECK_DBUS_NEXT_ARG_OR_return(args, DBUS_TYPE_UINT64, iter,
				      "Invalid arg max_token");
	dbus_message_iter_get_basic(args, &max_tokens);
	CHECK_DBUS_NEXT_ARG_OR_return(args, DBUS_TYPE_UINT64, iter,
				      "Invalid arg token_renewal");
	dbus_message_iter_get_basic(args, &token_renewal);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_client->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_available_tokens = max_tokens;
		qos_class->wbucket.max_available_tokens = max_tokens;
		qos_class->rbucket.tokens_renew_time = token_renewal;
		qos_class->wbucket.tokens_renew_time = token_renewal;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		ret = true;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	return ret;
}

/**
 * @brief Set IOPS settings for a client.
 *
 * This function sets the IOPS limits for a specified client.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_client_iops_set(DBusMessageIter *args, DBusMessage *reply,
				     DBusError *error)
{
	struct gsh_client *gsh_client;
	uint64_t read_iops, write_iops;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool ret = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_STRING, iter,
				 "Invalid arg ClientIP");
	gsh_client = lookup_client(args, &errormsg);
	CHECK_ARG_OR_return(gsh_client, iter, errormsg);

	CHECK_DBUS_NEXT_ARG_OR_return(args, DBUS_TYPE_UINT64, iter,
				      "Invalid arg read_iops");
	dbus_message_iter_get_basic(args, &read_iops);
	CHECK_DBUS_NEXT_ARG_OR_return(args, DBUS_TYPE_UINT64, iter,
				      "Invalid arg write_iops");
	dbus_message_iter_get_basic(args, &write_iops);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_client->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_iops_allowed = read_iops;
		qos_class->wbucket.max_iops_allowed = write_iops;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		ret = true;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	return ret;
}

/**
 * @brief Get token settings for a client.
 *
 * This function retrieves the current token limits for a given client.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_client_token_get(DBusMessageIter *args, DBusMessage *reply,
				      DBusError *error)
{
	struct gsh_client *gsh_client;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool ret = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_STRING, iter,
				 "Invalid arg ClientIP");
	gsh_client = lookup_client(args, &errormsg);
	CHECK_ARG_OR_return(gsh_client, iter, errormsg);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_client->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		uint32_t max_tokens = qos_class->rbucket.max_available_tokens;
		uint64_t token_renewal = qos_class->rbucket.tokens_renew_time;

		PTHREAD_MUTEX_unlock(&qos_class->lock);
		dbus_message_iter_append_basic(args, DBUS_TYPE_UINT32,
					       &max_tokens);
		dbus_message_iter_append_basic(args, DBUS_TYPE_UINT64,
					       &token_renewal);
		ret = true;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	return ret;
}

/**
 * @brief Get bandwidth settings for a client.
 *
 * This function retrieves the current bandwidth settings for a given client.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_client_bw_get(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	struct gsh_client *gsh_client;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool ret = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_STRING, iter,
				 "Invalid arg ClientIP");
	gsh_client = lookup_client(args, &errormsg);
	CHECK_ARG_OR_return(gsh_client, iter, errormsg);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_client->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		uint64_t read_bw = qos_class->rbucket.max_bw_allowed;
		uint64_t write_bw = qos_class->wbucket.max_bw_allowed;

		PTHREAD_MUTEX_unlock(&qos_class->lock);
		dbus_message_iter_append_basic(args, DBUS_TYPE_UINT64,
					       &read_bw);
		dbus_message_iter_append_basic(args, DBUS_TYPE_UINT64,
					       &write_bw);
		ret = true;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	return ret;
}

/**
 * @brief Get IOPS settings for a client.
 *
 * This function retrieves the current IOPS limits for a given client.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_client_iops_get(DBusMessageIter *args, DBusMessage *reply,
				     DBusError *error)
{
	struct gsh_client *gsh_client;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool ret = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_STRING, iter,
				 "Invalid arg ClientIP");
	gsh_client = lookup_client(args, &errormsg);
	CHECK_ARG_OR_return(gsh_client, iter, errormsg);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_client->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		uint64_t read_iops = qos_class->rbucket.max_iops_allowed;
		uint64_t write_iops = qos_class->wbucket.max_iops_allowed;

		PTHREAD_MUTEX_unlock(&qos_class->lock);
		dbus_message_iter_append_basic(args, DBUS_TYPE_UINT64,
					       &read_iops);
		dbus_message_iter_append_basic(args, DBUS_TYPE_UINT64,
					       &write_iops);
		ret = true;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	return ret;
}

/**
 * @brief Convert a QoS client to D-Bus format.
 *
 * This function converts the details of a QoS client into a
 * D-Bus structure for sending user the client details.
 *
 * @param [in] client qos_class_t pointer pointing to the client
 * @param [in] state void pointer containing the D-Bus message iterator state
 *
 * @return true if successful, false otherwise
 */
static bool client_qos_to_dbus(qos_class_t *qos_class, void *state)
{
	struct showclients_state *iter_state = state;
	DBusMessageIter client_struct, bw_struct;
	char ipaddr[INET6_ADDRSTRLEN];
	const char *ip_str = ipaddr;
	char read_bw_str[32], write_bw_str[32], enable_bw_str[32];
	const char *read_bw_ptr, *write_bw_ptr, *enable_bw_ptr;
	const char *enable_bw_label = "BW_ENABLED";
	const char *read_label = "READ_BW";
	const char *write_label = "WRITE_BW";

	if (qos_class->gsh_client->cl_addrbuf.ss_family == AF_INET) {
		struct sockaddr_in *sin =
			(struct sockaddr_in *)qos_class->gsh_client;
		inet_ntop(AF_INET, &sin->sin_addr, ipaddr, sizeof(ipaddr));
	} else {
		struct sockaddr_in6 *sin6 =
			(struct sockaddr_in6 *)qos_class->gsh_client;
		inet_ntop(AF_INET6, &sin6->sin6_addr, ipaddr, sizeof(ipaddr));
	}

	dbus_message_iter_open_container(&iter_state->client_iter,
					 DBUS_TYPE_STRUCT, NULL,
					 &client_struct);

	dbus_message_iter_append_basic(&client_struct, DBUS_TYPE_STRING,
				       &ip_str);

	snprintf(read_bw_str, sizeof(read_bw_str), "%" PRIu64,
		 qos_class->rbucket.max_bw_allowed);
	read_bw_ptr = read_bw_str;

	snprintf(write_bw_str, sizeof(write_bw_str), "%" PRIu64,
		 qos_class->wbucket.max_bw_allowed);
	write_bw_ptr = write_bw_str;

	snprintf(enable_bw_str, sizeof(enable_bw_str), "%d",
		 qos_class->bw_enabled);
	enable_bw_ptr = enable_bw_str;

	dbus_message_iter_open_container(&client_struct, DBUS_TYPE_STRUCT, NULL,
					 &bw_struct);
	dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
				       &enable_bw_label);
	dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
				       &enable_bw_ptr);
	dbus_message_iter_close_container(&client_struct, &bw_struct);

	dbus_message_iter_open_container(&client_struct, DBUS_TYPE_STRUCT, NULL,
					 &bw_struct);
	dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
				       &read_label);
	dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
				       &read_bw_ptr);
	dbus_message_iter_close_container(&client_struct, &bw_struct);

	dbus_message_iter_open_container(&client_struct, DBUS_TYPE_STRUCT, NULL,
					 &bw_struct);
	dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
				       &write_label);
	dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
				       &write_bw_ptr);
	dbus_message_iter_close_container(&client_struct, &bw_struct);

	dbus_message_iter_close_container(&iter_state->client_iter,
					  &client_struct);

	return true;
}

/**
 * @brief Get bandwidth settings for clients in a pepc configuration.
 * This function retrieves the current bandwidth limits for clients
 * under a pepc configuration.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_pepc_clients_bw_list(DBusMessageIter *args,
					  DBusMessage *reply, DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	struct showclients_state iter_state;
	uint32_t total_clients = 0;
	qos_class_t *sub_qos_class;
	struct glist_head *glist;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");
	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		dbus_message_iter_init_append(reply, &iter);
		total_clients = get_export_client_count(qos_class);
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32,
					       &total_clients);
		dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
						 QOS_CLIENT_CONTAINER,
						 &iter_state.client_iter);

		PTHREAD_MUTEX_lock(&qos_class->lock);
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);

			PTHREAD_MUTEX_lock(&sub_qos_class->lock);
			client_qos_to_dbus(sub_qos_class, &iter_state);
			PTHREAD_MUTEX_unlock(&sub_qos_class->lock);
		}
		PTHREAD_MUTEX_unlock(&qos_class->lock);

		dbus_message_iter_close_container(&iter,
						  &iter_state.client_iter);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Get bandwidth settings for a export.
 *
 * This function retrieves the current bandwidth limits for a given export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_bw_get(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter, bw_struct;
	char read_bw_str[32], write_bw_str[32], enable_bw_str[32];
	const char *read_bw_ptr, *write_bw_ptr, *enable_bw_ptr;
	const char *read_label = "READ_BW";
	const char *write_label = "WRITE_BW";
	const char *bw_label = "ENABLE_BW";
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");
	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);

		dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL,
						 &bw_struct);

		snprintf(enable_bw_str, sizeof(enable_bw_str), "%d",
			 qos_class->bw_enabled);
		enable_bw_ptr = enable_bw_str;
		snprintf(read_bw_str, sizeof(read_bw_str), "%" PRIu64,
			 qos_class->rbucket.max_bw_allowed);
		read_bw_ptr = read_bw_str;
		snprintf(write_bw_str, sizeof(write_bw_str), "%" PRIu64,
			 qos_class->wbucket.max_bw_allowed);
		write_bw_ptr = write_bw_str;

		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &bw_label);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &enable_bw_ptr);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &read_label);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &read_bw_ptr);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &write_label);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &write_bw_ptr);

		dbus_message_iter_close_container(&iter, &bw_struct);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Get token settings for a export.
 *
 * This function retrieves the current token limits for a given export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_token_get(DBusMessageIter *args, DBusMessage *reply,
				      DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter, token_struct;
	char read_token_str[32], write_token_str[32];
	const char *read_token_ptr, *write_token_ptr;
	const char *read_label = "READ_TOKENS";
	const char *write_label = "WRITE_TOKENS";
	bool retval = false;

	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		dbus_message_iter_init_append(reply, &iter);
		dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL,
						 &token_struct);

		snprintf(read_token_str, sizeof(read_token_str), "%" PRIu64,
			 qos_class->rbucket.max_available_tokens);
		read_token_ptr = read_token_str;
		snprintf(write_token_str, sizeof(write_token_str), "%" PRIu64,
			 qos_class->wbucket.max_available_tokens);
		write_token_ptr = write_token_str;

		dbus_message_iter_append_basic(&token_struct, DBUS_TYPE_STRING,
					       &read_label);
		dbus_message_iter_append_basic(&token_struct, DBUS_TYPE_STRING,
					       &read_token_ptr);
		dbus_message_iter_append_basic(&token_struct, DBUS_TYPE_STRING,
					       &write_label);
		dbus_message_iter_append_basic(&token_struct, DBUS_TYPE_STRING,
					       &write_token_ptr);

		dbus_message_iter_close_container(&iter, &token_struct);
		PTHREAD_MUTEX_unlock(&qos_class->lock);

		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Set IOPS settings for an export.
 *
 * This function sets the IOPS limits for a specified export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_iops_set(DBusMessageIter *args, DBusMessage *reply,
				     DBusError *error)
{
	uint16_t export_id;
	uint64_t read_iops, write_iops;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	if (!check_dbus_arg(args, DBUS_TYPE_UINT64, &iter,
			    "Invalid arg read_iops"))
		goto out;
	dbus_message_iter_get_basic(args, &read_iops);

	if (!check_dbus_arg(args, DBUS_TYPE_UINT64, &iter,
			    "Invalid arg write_iops"))
		goto out;
	dbus_message_iter_get_basic(args, &write_iops);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_iops_allowed = read_iops;
		qos_class->wbucket.max_iops_allowed = write_iops;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
out:
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Get default client bandwidth settings for a export.
 * This function retrieves the current default client bandwidth limits
 * under a given export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_default_client_bw_get(DBusMessageIter *args,
						  DBusMessage *reply,
						  DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter, bw_struct;
	char read_bw_str[32], write_bw_str[32];
	const char *read_bw_ptr, *write_bw_ptr;
	const char *read_label = "MAX_CLIENT_READ_BW";
	const char *write_label = "MAX_CLIENT_WRITE_BW";
	bool retval = false;

	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");
	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		dbus_message_iter_init_append(reply, &iter);
		dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL,
						 &bw_struct);

		snprintf(read_bw_str, sizeof(read_bw_str), "%" PRIu64,
			 gsh_export->qos_block->max_client_read_bw);
		read_bw_ptr = read_bw_str;
		snprintf(write_bw_str, sizeof(write_bw_str), "%" PRIu64,
			 gsh_export->qos_block->max_client_write_bw);
		write_bw_ptr = write_bw_str;

		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &read_label);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &read_bw_ptr);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &write_label);
		dbus_message_iter_append_basic(&bw_struct, DBUS_TYPE_STRING,
					       &write_bw_ptr);

		dbus_message_iter_close_container(&iter, &bw_struct);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Set bandwidth settings for a export.
 *
 * This function sets the bandwidth limits for a specified export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_bw_set(DBusMessageIter *args, DBusMessage *reply,
				   DBusError *error)
{
	uint16_t export_id;
	uint64_t read_bw, write_bw;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	bool retval = false;
	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
				 "Invalid arg read_bw"))
		goto put_export;
	dbus_message_iter_get_basic(args, &read_bw);

	if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
				 "Invalid arg write_bw"))
		goto put_export;
	dbus_message_iter_get_basic(args, &write_bw);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_bw_allowed = read_bw;
		qos_class->wbucket.max_bw_allowed = write_bw;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}

	PTHREAD_MUTEX_unlock(&g_qos_config_lock);

put_export:
	put_gsh_export(gsh_export);
	return retval;
}
/**
 * @brief Set token settings for a export.
 *
 * This function sets the token limits for a specified export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_token_set(DBusMessageIter *args, DBusMessage *reply,
				      DBusError *error)
{
	uint16_t export_id;
	uint64_t max_tokens;
	uint64_t token_renewal;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
				 "Invalid arg max_token"))
		goto put_export;
	dbus_message_iter_get_basic(args, &max_tokens);

	if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
				 "Invalid arg token_renewal"))
		goto put_export;
	dbus_message_iter_get_basic(args, &token_renewal);

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_available_tokens = max_tokens;
		qos_class->wbucket.max_available_tokens = max_tokens;
		qos_class->rbucket.tokens_renew_time = token_renewal;
		qos_class->wbucket.tokens_renew_time = token_renewal;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}

	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
put_export:
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Get IOPS settings for an export.
 *
 * This function retrieves the current IOPS limits for a given export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_iops_get(DBusMessageIter *args, DBusMessage *reply,
				     DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter, iops_struct;
	char read_iops_str[32], write_iops_str[32], enable_iops_str[32];
	const char *read_iops_ptr, *write_iops_ptr, *enable_iops_ptr;
	const char *read_label = "READ_IOPS";
	const char *write_label = "WRITE_IOPS";
	const char *iops_label = "ENABLE_IOPS";
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");
	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);

		dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL,
						 &iops_struct);

		snprintf(enable_iops_str, sizeof(enable_iops_str), "%d",
			 qos_class->iops_enabled);
		enable_iops_ptr = enable_iops_str;
		snprintf(read_iops_str, sizeof(read_iops_str), "%" PRIu64,
			 qos_class->rbucket.max_iops_allowed);
		read_iops_ptr = read_iops_str;
		snprintf(write_iops_str, sizeof(write_iops_str), "%" PRIu64,
			 qos_class->wbucket.max_iops_allowed);
		write_iops_ptr = write_iops_str;

		dbus_message_iter_append_basic(&iops_struct, DBUS_TYPE_STRING,
					       &iops_label);
		dbus_message_iter_append_basic(&iops_struct, DBUS_TYPE_STRING,
					       &enable_iops_ptr);
		dbus_message_iter_append_basic(&iops_struct, DBUS_TYPE_STRING,
					       &read_label);
		dbus_message_iter_append_basic(&iops_struct, DBUS_TYPE_STRING,
					       &read_iops_ptr);
		dbus_message_iter_append_basic(&iops_struct, DBUS_TYPE_STRING,
					       &write_label);
		dbus_message_iter_append_basic(&iops_struct, DBUS_TYPE_STRING,
					       &write_iops_ptr);

		dbus_message_iter_close_container(&iter, &iops_struct);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Set bandwidth settings for a client in a pepc configuration.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_pepc_clients_bw_set(DBusMessageIter *args,
					 DBusMessage *reply, DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	qos_class_t *sub_qos_class;
	struct gsh_client *gsh_client;
	uint64_t read_bw, write_bw;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		dbus_message_iter_next(args);
		if (!check_dbus_arg(args, DBUS_TYPE_STRING, &iter,
				    "Invalid arg ClientIP"))
			goto out;

		gsh_client = lookup_client(args, &errormsg);
		if (!check_arg(gsh_client, &iter, "lookup client failed"))
			goto out;

		if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
					 "Invalid arg read_bw"))
			goto out;
		dbus_message_iter_get_basic(args, &read_bw);

		if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
					 "Invalid arg write_bw"))
			goto out;
		dbus_message_iter_get_basic(args, &write_bw);

		PTHREAD_MUTEX_lock(&qos_class->lock);
		sub_qos_class = pepc_get_client_from_list(&qos_class->clients,
							  gsh_client);
		if (sub_qos_class) {
			PTHREAD_MUTEX_lock(&sub_qos_class->lock);
			sub_qos_class->bw_enabled = true;
			sub_qos_class->rbucket.max_bw_allowed = read_bw;
			sub_qos_class->wbucket.max_bw_allowed = write_bw;
			PTHREAD_MUTEX_unlock(&sub_qos_class->lock);
		}
		PTHREAD_MUTEX_unlock(&qos_class->lock);

		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}

out:
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Set default client bandwidth settings for a export.
 *
 * function sets the default client bandwidth limits for a specified export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_export_default_client_bw_set(DBusMessageIter *args,
						  DBusMessage *reply,
						  DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	uint64_t read_bw, write_bw;
	DBusMessageIter iter;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");
	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
					 "Invalid arg read_bw"))
			goto out;
		dbus_message_iter_get_basic(args, &read_bw);

		if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
					 "Invalid arg write_bw"))
			goto out;
		dbus_message_iter_get_basic(args, &write_bw);

		gsh_export->qos_block->max_client_read_bw = read_bw;
		gsh_export->qos_block->max_client_write_bw = write_bw;
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}

out:
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Enable bandwidth control for a export.
 *
 * This function enables bandwidth control for a specified export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_enable_bw_control_ps(DBusMessageIter *args,
					  DBusMessage *reply, DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (g_qos_config->enable_qos && g_qos_config->enable_bw_control &&
	    qos_class) {
		qos_perexport_insert(gsh_export, NULL);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

static bool dbus_qos_disable_bw_control_ps(DBusMessageIter *args,
					   DBusMessage *reply, DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_drain_bw_ios(qos_class);
		PTHREAD_MUTEX_unlock(&qos_class->lock);

		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Disable bandwidth control for a export under a pepc configuration.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_disable_bw_control_pepc(DBusMessageIter *args,
					     DBusMessage *reply,
					     DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	qos_class_t *sub_qos_class;
	struct glist_head *glist;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);
			qos_drain_bw_ios(sub_qos_class);
		}
		qos_drain_bw_ios(qos_class);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Enable bandwidth control for a export under a pepc configuration.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_enable_bw_control_pepc(DBusMessageIter *args,
					    DBusMessage *reply,
					    DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	qos_class_t *sub_qos_class;
	struct glist_head *glist;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);
			sub_qos_class->bw_enabled = true;
		}
		qos_class->bw_enabled = true;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Enable IOPS control for all clients in a pepc configuration.
 *
 * This function enables IOPS control for all clients under a specified export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_enable_iops_control_pepc(DBusMessageIter *args,
					      DBusMessage *reply,
					      DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	qos_class_t *sub_qos_class;
	struct glist_head *glist;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);
			sub_qos_class->iops_enabled = true;
		}
		qos_class->iops_enabled = true;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Disable IOPS control for all clients in a pepc configuration.
 *
 * This function disables IOPS control for all clients under a specified export.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_disable_iops_control_pepc(DBusMessageIter *args,
					       DBusMessage *reply,
					       DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	qos_class_t *sub_qos_class;
	struct glist_head *glist;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);
			/* Drain IOPS operations for this client and
			 * mark as disable */
			qos_drain_iops_ios(sub_qos_class);
		}
		/* Drain IOPS operations for the export itself and
		 * mark as disable */
		qos_drain_iops_ios(qos_class);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Get IOPS settings for clients in a pepc configuration.
 *
 * This function retrieves the current IOPS limits for clients under
 * a pepc configuration.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_pepc_clients_iops_list(DBusMessageIter *args,
					    DBusMessage *reply,
					    DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	DBusMessageIter iter;
	struct showclients_state iter_state;
	uint32_t total_clients = 0;
	qos_class_t *sub_qos_class;
	struct glist_head *glist;
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");
	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (qos_class != NULL) {
		dbus_message_iter_init_append(reply, &iter);
		total_clients = get_export_client_count(qos_class);
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32,
					       &total_clients);
		dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
						 QOS_CLIENT_CONTAINER,
						 &iter_state.client_iter);

		PTHREAD_MUTEX_lock(&qos_class->lock);
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);

			PTHREAD_MUTEX_lock(&sub_qos_class->lock);
			client_qos_to_dbus(sub_qos_class, &iter_state);
			PTHREAD_MUTEX_unlock(&sub_qos_class->lock);
		}
		PTHREAD_MUTEX_unlock(&qos_class->lock);

		dbus_message_iter_close_container(&iter,
						  &iter_state.client_iter);
		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/**
 * @brief Set IOPS settings for a client in a pepc configuration.
 *
 * @param [in] args DBusMessageIter containing the input arguments
 * @param [in] reply DBusMessage to store the output result
 * @param [in] error DBusError object to handle any errors that occur
 *
 * @return true if successful, false otherwise
 */
static bool dbus_qos_pepc_clients_iops_set(DBusMessageIter *args,
					   DBusMessage *reply, DBusError *error)
{
	uint16_t export_id;
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;
	qos_class_t *sub_qos_class;
	struct gsh_client *gsh_client;
	uint64_t read_iops, write_iops;
	DBusMessageIter iter;
	char *errormsg = "OK";
	bool retval = false;

	dbus_message_iter_init_append(reply, &iter);
	CHECK_DBUS_ARG_OR_return(args, DBUS_TYPE_UINT16, iter,
				 "Invalid arg exportid");
	dbus_message_iter_get_basic(args, &export_id);
	gsh_export = get_gsh_export(export_id);
	CHECK_ARG_OR_return(gsh_export, iter, "Export id not found");

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		dbus_message_iter_next(args);
		if (!check_dbus_arg(args, DBUS_TYPE_STRING, &iter,
				    "Invalid arg ClientIP"))
			goto out;

		gsh_client = lookup_client(args, &errormsg);
		if (!check_arg(gsh_client, &iter, "lookup client failed"))
			goto out;

		CHECK_ARG_OR_return(gsh_client, iter, errormsg);

		if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
					 "Invalid arg read_iops"))
			goto out;
		dbus_message_iter_get_basic(args, &read_iops);

		if (!check_dbus_next_arg(args, DBUS_TYPE_UINT64, &iter,
					 "Invalid arg write_iops"))
			goto out;
		dbus_message_iter_get_basic(args, &write_iops);

		PTHREAD_MUTEX_lock(&qos_class->lock);
		sub_qos_class = pepc_get_client_from_list(&qos_class->clients,
							  gsh_client);
		if (sub_qos_class) {
			PTHREAD_MUTEX_lock(&sub_qos_class->lock);
			sub_qos_class->iops_enabled = true;
			sub_qos_class->rbucket.max_iops_allowed = read_iops;
			sub_qos_class->wbucket.max_iops_allowed = write_iops;
			PTHREAD_MUTEX_unlock(&sub_qos_class->lock);
		}
		PTHREAD_MUTEX_unlock(&qos_class->lock);

		retval = true;
	} else {
		gsh_dbus_status_reply(&iter, false, "check config values");
		retval = false;
	}
out:
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return retval;
}

/* Perexport-PerClient implemnentation */
static struct gsh_dbus_method qos_export_clients_list = {
	.name = "ListExportClientsBandwidth",
	.method = dbus_qos_pepc_clients_bw_list,
	.args = { EXPORT_ID_ARG, TOTAL_CLIENTS, QOS_CLIENTS_REPLY, SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_pepc_clients_bw_set = {
	.name = "SetExportClientBandwidth",
	.method = dbus_qos_pepc_clients_bw_set,
	.args = { EXPORT_ID_ARG, IPADDR_ARG, READ_BW_IN_ARG, WRITE_BW_IN_ARG,
		  SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_default_client_bw_get = {
	.name = "GetExportDefaultClientBandwidth",
	.method = dbus_qos_export_default_client_bw_get,
	.args = { EXPORT_ID_ARG,
		  { "default_client_bandwidth", "a(ss)", "out" },
		  SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_default_client_bw_set = {
	.name = "SetExportDefaultClientBandwidth",
	.method = dbus_qos_export_default_client_bw_set,
	.args = { EXPORT_ID_ARG, READ_BW_IN_ARG, WRITE_BW_IN_ARG, SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_enable_all_clients_bw_pepc = {
	.name = "EnableAllClientQosBwControlpepc",
	.method = dbus_qos_enable_bw_control_pepc,
	.args = { EXPORT_ID_ARG, SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_disable_all_clients_bw_pepc = {
	.name = "DisableExportQosBwControlpepc",
	.method = dbus_qos_disable_bw_control_pepc,
	.args = { EXPORT_ID_ARG, SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_enable_all_clients_iops_pepc = {
	.name = "EnableAllClientQosIopsControlpepc",
	.method = dbus_qos_enable_iops_control_pepc,
	.args = { EXPORT_ID_ARG, SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_disable_all_clients_iops_pepc = {
	.name = "DisableExporttQosIopsControlpepc",
	.method = dbus_qos_disable_iops_control_pepc,
	.args = { EXPORT_ID_ARG, SUCCESS_ARG, END_ARG_LIST }
};

/* Perexport-PerClient implemnentation */
static struct gsh_dbus_method qos_export_clients_iops_list = {
	.name = "ListExportClientsIOPS",
	.method = dbus_qos_pepc_clients_iops_list,
	.args = { EXPORT_ID_ARG, TOTAL_CLIENTS, QOS_CLIENTS_REPLY, SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_pepc_clients_iops_set = {
	.name = "SetExportClientIOPS",
	.method = dbus_qos_pepc_clients_iops_set,
	.args = { EXPORT_ID_ARG, IPADDR_ARG, READ_IOPS_IN_ARG,
		  WRITE_IOPS_IN_ARG, SUCCESS_ARG, END_ARG_LIST }
};

/* Per export but is common for Perexport-PerClient also*/
static struct gsh_dbus_method qos_export_bw_get = {
	.name = "GetExportBandwidth",
	.method = dbus_qos_export_bw_get,
	.args = { EXPORT_ID_ARG,
		  { "bandwidth", "a(sss)", "out" },
		  SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_bw_set = {
	.name = "SetExportBandwidth",
	.method = dbus_qos_export_bw_set,
	.args = { EXPORT_ID_ARG, READ_BW_IN_ARG, WRITE_BW_IN_ARG, SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_token_get = {
	.name = "GetExportTokens",
	.method = dbus_qos_export_token_get,
	.args = { EXPORT_ID_ARG,
		  { "tokens", "a(ss)", "out" },
		  SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_token_set = {
	.name = "SetExportTokens",
	.method = dbus_qos_export_token_set,
	.args = { EXPORT_ID_ARG, MAX_TOKENS_IN, TOKEN_RENEW_IN, SUCCESS_ARG,
		  END_ARG_LIST }
};

/* IOPS methods for exports */
static struct gsh_dbus_method qos_export_iops_get = {
	.name = "GetExportIOPS",
	.method = dbus_qos_export_iops_get,
	.args = { EXPORT_ID_ARG,
		  { "iops", "a(sss)", "out" },
		  SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_iops_set = {
	.name = "SetExportIOPS",
	.method = dbus_qos_export_iops_set,
	.args = { EXPORT_ID_ARG, READ_IOPS_IN_ARG, WRITE_IOPS_IN_ARG,
		  SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method *qos_methods_pepc[] = {
	&qos_export_bw_get,
	&qos_export_bw_set,
	&qos_export_token_get,
	&qos_export_token_set,
	&qos_export_iops_get,
	&qos_export_iops_set,
	&qos_export_clients_list,
	&qos_pepc_clients_bw_set,
	&qos_export_default_client_bw_get,
	&qos_export_default_client_bw_set,
	&qos_export_enable_all_clients_bw_pepc,
	&qos_export_disable_all_clients_bw_pepc,
	&qos_export_enable_all_clients_iops_pepc,
	&qos_export_disable_all_clients_iops_pepc,
	&qos_export_clients_iops_list,
	&qos_pepc_clients_iops_set,
	NULL
};

static struct gsh_dbus_method qos_export_enable_bw_control = {
	.name = "EnableExportQosBwControl",
	.method = dbus_qos_enable_bw_control_ps,
	.args = { EXPORT_ID_ARG, SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method qos_export_disable_bw_control = {
	.name = "DisableExportQosBwControl",
	.method = dbus_qos_disable_bw_control_ps,
	.args = { EXPORT_ID_ARG, SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method *qos_methods_ps[] = {
	&qos_export_bw_get,
	&qos_export_bw_set,
	&qos_export_token_get,
	&qos_export_token_set,
	&qos_export_iops_get,
	&qos_export_iops_set,
	&qos_export_enable_bw_control,
	&qos_export_disable_bw_control,
	NULL
};

/* PerClient implemnentation */
static struct gsh_dbus_method qos_client_bw_set = {
	.name = "SetClientBandwidth",
	.method = dbus_qos_client_bw_set,
	.args = { CLIENT_IP_ARG, READ_BW_IN_ARG, WRITE_BW_IN_ARG, SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_client_bw_get = {
	.name = "GetClientBandwidth",
	.method = dbus_qos_client_bw_get,
	.args = { CLIENT_IP_ARG, READ_BW_OUT_ARG, WRITE_BW_OUT_ARG, SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_client_token_set = {
	.name = "SetClientTokens",
	.method = dbus_qos_client_token_set,
	.args = { CLIENT_IP_ARG, MAX_TOKENS_IN, TOKEN_RENEW_IN, SUCCESS_ARG,
		  END_ARG_LIST }
};

static struct gsh_dbus_method qos_client_token_get = {
	.name = "GetClientTokens",
	.method = dbus_qos_client_token_get,
	.args = { CLIENT_IP_ARG, MAX_TOKENS_OUT, TOKEN_RENEW_OUT, SUCCESS_ARG,
		  END_ARG_LIST }
};

/* IOPS methods for clients */
static struct gsh_dbus_method qos_client_iops_set = {
	.name = "SetClientIOPS",
	.method = dbus_qos_client_iops_set,
	.args = { CLIENT_IP_ARG, READ_IOPS_IN_ARG, WRITE_IOPS_IN_ARG,
		  SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method qos_client_iops_get = {
	.name = "GetClientIOPS",
	.method = dbus_qos_client_iops_get,
	.args = { CLIENT_IP_ARG, READ_IOPS_OUT_ARG, WRITE_IOPS_OUT_ARG,
		  SUCCESS_ARG, END_ARG_LIST }
};

static struct gsh_dbus_method *qos_methods_pc[] = { &qos_client_bw_get,
						    &qos_client_bw_set,
						    &qos_client_token_get,
						    &qos_client_token_set,
						    &qos_client_iops_get,
						    &qos_client_iops_set,
						    NULL };

static struct gsh_dbus_interface qos_interface = {
	.name = "org.ganesha.nfsd.qos",
	.props = NULL,
	.methods = NULL,
	.signals = NULL
};

static struct gsh_dbus_interface *dbus_qos_interface[] = { &qos_interface,
							   NULL };

/**
 * @brief Initialize QoS manager based on global QOS config type.
 *
 */
void dbus_qosmgr_init(void)
{
	if (g_qos_config->enable_qos) {
		switch (g_qos_config->qos_type) {
		case QOS_NOT_ENABLED:
			break;
		case QOS_PER_EXPORT_ENABLED:
			qos_interface.methods = qos_methods_ps;
			break;
		case QOS_PER_CLIENT_ENABLED:
			qos_interface.methods = qos_methods_pc;
			break;
		case QOS_PEREXPORT_PERCLIENT_ENABLED:
			qos_interface.methods = qos_methods_pepc;
			break;
		}
		gsh_dbus_register_path("QosMgr", dbus_qos_interface);
	}
}

#ifdef USE_GRPC
/**
 * @brief Fill success and error-message fields for a QoS gRPC helper.
 *
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message buffer.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @param [in] ok true if the QoS operation succeeded.
 * @param [in] msg Status string to copy into errmsg.
 */
static void grpc_qos_set_status(bool *success, char *errmsg, size_t errmsg_len,
				bool ok, const char *msg)
{
	*success = ok;
	snprintf(errmsg, errmsg_len, "%s", msg);
}

/**
 * @brief Get export-level bandwidth limits for gRPC QoS requests.
 *
 * @param [in] export_id Export identifier.
 * @param [out] out Populated bandwidth values and enable flag.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_get_export_bandwidth(uint16_t export_id,
				   struct grpc_qos_bw_limits *out,
				   bool *success, char *errmsg,
				   size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	memset(out, 0, sizeof(*out));
	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		out->enabled = qos_class->bw_enabled;
		out->read_bw = qos_class->rbucket.max_bw_allowed;
		out->write_bw = qos_class->wbucket.max_bw_allowed;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	} else {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return true;
}

/**
 * @brief Set export-level bandwidth limits from gRPC.
 *
 * @param [in] export_id Export identifier.
 * @param [in] read_bw Max read bandwidth.
 * @param [in] write_bw Max write bandwidth.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_set_export_bandwidth(uint16_t export_id, uint64_t read_bw,
				   uint64_t write_bw, bool *success,
				   char *errmsg, size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_bw_allowed = read_bw;
		qos_class->wbucket.max_bw_allowed = write_bw;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	} else {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return true;
}

/**
 * @brief Get export-level token limits for gRPC QoS requests.
 *
 * @param [in] export_id Export identifier.
 * @param [out] out Populated token limits.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_get_export_tokens(uint16_t export_id,
				struct grpc_qos_token_limits *out,
				bool *success, char *errmsg, size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	memset(out, 0, sizeof(*out));
	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		out->max_tokens = qos_class->rbucket.max_available_tokens;
		out->token_renewal = qos_class->rbucket.tokens_renew_time;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	} else {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return true;
}

/**
 * @brief Set export-level token limits from gRPC.
 *
 * @param [in] export_id Export identifier.
 * @param [in] max_tokens Max token budget.
 * @param [in] token_renewal Token renewal period.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_set_export_tokens(uint16_t export_id, uint64_t max_tokens,
				uint64_t token_renewal, bool *success,
				char *errmsg, size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_available_tokens = max_tokens;
		qos_class->wbucket.max_available_tokens = max_tokens;
		qos_class->rbucket.tokens_renew_time = token_renewal;
		qos_class->wbucket.tokens_renew_time = token_renewal;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	} else {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return true;
}

/**
 * @brief Get export-level IOPS limits for gRPC QoS requests.
 *
 * @param [in] export_id Export identifier.
 * @param [out] out Populated IOPS values and enable flag.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_get_export_iops(uint16_t export_id,
			      struct grpc_qos_iops_limits *out, bool *success,
			      char *errmsg, size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	memset(out, 0, sizeof(*out));
	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		out->enabled = qos_class->iops_enabled;
		out->read_iops = qos_class->rbucket.max_iops_allowed;
		out->write_iops = qos_class->wbucket.max_iops_allowed;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	} else {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return true;
}

/**
 * @brief Set export-level IOPS limits from gRPC.
 *
 * @param [in] export_id Export identifier.
 * @param [in] read_iops Max read IOPS.
 * @param [in] write_iops Max write IOPS.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_set_export_iops(uint16_t export_id, uint64_t read_iops,
			      uint64_t write_iops, bool *success, char *errmsg,
			      size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_class->rbucket.max_iops_allowed = read_iops;
		qos_class->wbucket.max_iops_allowed = write_iops;
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	} else {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return true;
}

/**
 * @brief Enable export-level bandwidth control for gRPC QoS requests.
 *
 * @param [in] export_id Export identifier.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_enable_export_bw_control(uint16_t export_id, bool *success,
				       char *errmsg, size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;

	if (!(g_qos_config->enable_qos && g_qos_config->enable_bw_control &&
	      qos_class)) {
		PTHREAD_MUTEX_unlock(&g_qos_config_lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
		put_gsh_export(gsh_export);
		return true;
	}

	/*
	 * Drop g_qos_config_lock before qos_perexport_insert().
	 *
	 * qos_perexport_insert() takes the same lock internally.
	 * g_qos_config_lock is a non-recursive mutex (PTHREAD_MUTEX_init
	 * with default attributes). Holding it here and taking it again
	 * inside qos_perexport_insert() deadlocks the calling thread
	 * (D-Bus or gRPC EnableExportQosBwControl hangs with no reply).
	 */
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);

	qos_perexport_insert(gsh_export, NULL);
	grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	put_gsh_export(gsh_export);
	return true;
}

/**
 * @brief Disable export-level bandwidth control for gRPC QoS requests.
 *
 * @param [in] export_id Export identifier.
 * @param [out] success Operation status.
 * @param [out] errmsg Error/status message.
 * @param [in] errmsg_len Size of errmsg buffer.
 * @return true always; success/failure is reported through @success.
 */
bool grpc_qos_disable_export_bw_control(uint16_t export_id, bool *success,
					char *errmsg, size_t errmsg_len)
{
	struct gsh_export *gsh_export;
	qos_class_t *qos_class;

	gsh_export = get_gsh_export(export_id);
	if (gsh_export == NULL) {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "Export id not found");
		return true;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	qos_class = gsh_export->qos_class;
	if (qos_class != NULL) {
		PTHREAD_MUTEX_lock(&qos_class->lock);
		qos_drain_bw_ios(qos_class);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		grpc_qos_set_status(success, errmsg, errmsg_len, true, "OK");
	} else {
		grpc_qos_set_status(success, errmsg, errmsg_len, false,
				    "check config values");
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
	put_gsh_export(gsh_export);
	return true;
}

#endif /* USE_GRPC */
