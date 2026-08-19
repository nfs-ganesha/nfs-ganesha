// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
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
 * @file nfs_qos.c
 * @brief Routines used for managing the QOS.
 *
 * Routines used for managing the NFS4 QOS.
 *
 *
 */
#include "nfs_core.h"
#include "nfs_qos.h"
#include "nfs_cluster_qos.h"

#ifdef USE_MONITORING
#include "ip_utils.h"
#endif

/* Token Exhausted IO's delay controlling macros
 * These delays are designed to prevent immediate retries by the client,
 * which could otherwise lead to excessive and unnecessary network traffic.
 *
 * Additionally, number of suspended IOs in QOS is controlled by
 * SUSPEND_SOCKET_IO_LIMIT which make sures to throttle further socket-level IO
 * to maintain system stability.
 */
#define TOKEN_NFS_ERR_DELAY_DEFAULT 15
#define TOKEN_NFS_ERR_DELAY_IMMED 1
#define SUSPEND_SOCKET_IO_LIMIT 5

/* Currently BW controlling using SYNC is disabled
 * This is compile time config */
#define BW_SYNC_ENABLE 0
#define BW_ASYNC_ENABLE !BW_SYNC_ENABLE

/* Monitoring related macros used for labeling*/
#ifdef USE_MONITORING

#define QOS_MAX_LONG_STR_LEN 22
#define MAX_LABEL_KEY_LEN 60
#define MAX_METADATA_KEY_LEN 80
#define MAX_METRIC_NAME_LEN 80

#endif

#define QOS_PRINT_EXPORT(str, gsh_export)                                     \
	do {                                                                  \
		struct tmp_export_paths tmp;                                  \
		tmp_get_exp_paths(&tmp, gsh_export);                          \
		LogDebug(COMPONENT_QOS, "%s SI: %d path: %s pseudo: %s", str, \
			 gsh_export->export_id, TMP_FULLPATH(&tmp),           \
			 TMP_PSEUDOPATH(&tmp));                               \
		tmp_put_exp_paths(&tmp);                                      \
	} while (0)

#define QOS_PRINT_CLIENT(str, gsh_client)                                      \
	do {                                                                   \
		char client[LOG_BUFF_LEN] = "\0";                              \
		struct display_buffer db = { sizeof(client), client, client }; \
		display_sockaddr(&db, &(gsh_client->cl_addrbuf));              \
		LogDebug(COMPONENT_QOS, "%s CI: %s", str, client);             \
	} while (0)

#define QOS_PRINT_CLASS(str, class)                               \
	do {                                                      \
		if (class->type == QOS_EXPORT) {                  \
			QOS_PRINT_EXPORT(str, class->gsh_export); \
		} else {                                          \
			QOS_PRINT_CLIENT(str, class->gsh_client); \
		}                                                 \
	} while (0)

/* Flag to control QOS thread lifecycle */
#define QOS_THREAD_RUNNABLE 1 << 0
uint32_t qos_bits;

typedef void (*qos_svc_rcb)(void *);
static void setNode_pc(qos_class_t *node, struct gsh_client *gsh_client,
		       struct qos_block_config *qos_block);

qos_block_config_t qos_block_config;
qos_block_config_t *g_qos_config = (qos_block_config_t *)&qos_block_config;

/*
 * locking rules:
 *
 * 1) Config update path:
 * Take the g_qos_config_lock and add the config.
 *
 * 2) Config update from IO PATH (runtime conf creation and applying the value):
 * Take the g_qos_iopath_lock (to sync between the multiple incoming IO's)
 * Take the g_qos_config_lock (to sync between Config path and IO path)
 * then update/add the values.
 *
 * 3) IO Producer logic: (i.e Adding IO to OQS buckets SVC thread):
 * Take g_qos_iopath_lock (If config is yet to be populated).
 * Take bucket->lock (Bucket in which IO is going to be added).
 *
 * 4) IO Consumer Logic: (Which resumes the IO from bucket, QOS thread)
 *	In per_export or per_client config need to hold lock based on bucket:
 *		qos_class->rbucket->lock  or qos_class->wbucket->lock;
 *
 *	In perExport_perClient config IO's are rescheduled via holding:
 *		qos_class->rbucket->lock; &&
 *			qos_class->sub_qos_class->rbucket->lock;
 *		qos_class->wbucket->lock; &&
 *			qos_class->sub_qos_class->wbucket->lock;
 *
 *	In all the cases IO'ss are resumed via:
 *		qos_class->rbucket->lock; or qos_class->wbucket->lock;
 *
 * Advantages of using the bucket->lock: provides finegrain locking
 *	eg: QOS has rbucket, wbucket due to which
 *	    read and write contentions are avoided in IO path.
 *
 * Before doing any manupluation of the list entries in bucket,
 * its the resposiblity of the function/caller-function to hold the relavant
 * required locks as mentioned above.
 * */

pthread_mutex_t g_qos_iopath_lock;
pthread_mutex_t g_qos_config_lock;

/*  Currently only 2 threads which are handling the read and write
 *  Future Implementation : Per export/per client thread
 *			    Load based thread swapnning.
 *			    Avoiding calling foreach_gsh_export/_client
 * */
pthread_t qos_thread[2];

/**
 * Function to get the callback strings for debugging.
 * Print the string while creating the timer for IO entry.
 * Print the string while resuming the timer entry.
 * this will help in tracking the IO flow.
 *
 * @param [in] callback function's string representation is needed.
 *
 * @return Pointer to an callback string.
 */
const char *cb1 = "read_cb";
const char *cb2 = "write_cb";
const char *cb3 = "iops_cb";
const char *qos_cb_str(void (*callback)(void *))
{
	if (callback == nfs4_qos_compound_cb)
		return cb3;
	else if (callback == nfs4_qos_read_cb)
		return cb1;
	else if (callback == nfs4_qos_write_cb)
		return cb2;
	return NULL;
}

/**
 * Function to get the QoS resume callback for the given operation type.
 *	Applicable for BW and token (Read/Write only)
 *
 * @param [in] op_type Type of the operation (read/write).
 * @return Pointer to the qos_svc_rcb function for the given operation type.
 */
qos_svc_rcb get_qos_resume_func(qos_op_type_t op_type)
{
	return (op_type == QOS_READ) ? nfs4_qos_read_cb : nfs4_qos_write_cb;
}

/**
 * Function to get the current time in microseconds.
 *
 * @return Current time in microseconds.
 */
uint64_t get_time_in_usec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	/* Convert to uSeconds and return */
	return ((ts.tv_sec * 1000000) + (ts.tv_nsec / 1000));
}

/**
 * Function to calculate the future time in microseconds w.r.t passed value.
 *
 * @param [in] ctime    Current time in microseconds.
 * @param [in] seconds  Number of seconds to add to the current time.
 * @param [in] mseconds Number of milliseconds to add to the current time.
 * @param [in] useconds Number of microseconds to add to the current time.
 * @return Future time in microseconds.
 */
static inline uint64_t get_time_future_useconds(uint64_t ctime,
						uint64_t seconds,
						uint64_t mseconds,
						uint64_t useconds)
{
	if (!ctime) {
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		/*  Convert to uSeconds */
		ctime = (ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
	}
	return (ctime + (seconds * 1000000) + (mseconds * 1000) + useconds);
}

/**
 * Function to allocate qos_class_t struture and setting the appropriate class
 * value i.e type Only 2 types: QOS_EXPORT/QOS_EXPORT.
 *
 * node->type should be set only in this function i.e time of allocation.
 * no code flow should update node->type.
 *
 * @param [in] Class type
 * @return Pointer to the newly allocated qos_class_t structure.
 */
qos_class_t *allocate_qos_class(qos_class_type_t class_type)
{
	qos_class_t *node = gsh_calloc(1, sizeof(qos_class_t), MEM_COMP_QOS);

	node->type = class_type;
#ifdef USE_MONITORING
	node->enabled_bw_metric = false;
	node->enabled_iops_metric = false;
	node->enabled_tokens_metric = false;
#endif
	PTHREAD_MUTEX_init(&(node->lock), NULL);
	PTHREAD_MUTEX_init(&(node->rbucket.lock), NULL);
	PTHREAD_MUTEX_init(&(node->wbucket.lock), NULL);
	glist_init(&node->rbucket.io_waitlist_qos_bc);
	glist_init(&node->wbucket.io_waitlist_qos_bc);
	glist_init(&node->rbucket.io_waitlist_qos_iops);
	glist_init(&node->wbucket.io_waitlist_qos_iops);
	glist_init(&node->clients);
	glist_init(&node->client_entries);
#if ENABLE_CLUSTER_QOS
	avltree_init(&node->cqos_subnodes, cqos_addr_cmpf, 0);
#endif
	return node;
}

/**
 * Function to allocate a client details structure for Token exhaust
 *
 * @param [in] data Compound data associated with the I/O request
 * @return Pointer to the newly allocated client details structure
 */
static qos_client_entry_t *alloc_clientdetails_pe(compound_data_t *data)
{
	qos_client_entry_t *new_entry = NULL;

	new_entry = gsh_calloc(1, sizeof(qos_client_entry_t), MEM_COMP_QOS);
	new_entry->gsh_client = op_ctx->client;
	new_entry->data = data;
	new_entry->rq_xprt = data->req->rq_xprt;
	glist_init(&new_entry->token_ex_cl);
	QOS_PRINT_CLIENT("adding client entry:", op_ctx->client);
	return new_entry;
}

/**
 * Function to allocate memory for qos_op_cb_arg structure and initialize it.
 *
 * @param [in] caller_data Pointer to the caller data.
 * @param [in] ratecontrol Rate control flag.
 * @return Pointer to the newly allocated qos_op_cb_arg structure.
 */
static inline struct qos_op_cb_arg *alloc_qos_cb_args(void *caller_data,
						      int ratecontrol)
{
	struct qos_op_cb_arg *qos_cb_args = NULL;

	qos_cb_args = gsh_calloc(1, sizeof(struct qos_op_cb_arg), MEM_COMP_QOS);
	qos_cb_args->caller_data = caller_data;
	qos_cb_args->ratecontrol = ratecontrol;
	return qos_cb_args;
}

/**
 * Function to create a new timer entry
 *
 * @param [in] expiry Time for expiry of the timer in microseconds
 * @param [in] callback call-back function to be executed when the timer expires
 * @param [in] args Arguments to pass to the callback function
 * @return Pointer to the newly created timer entry
 */
static inline timer_entry_t *create_timer_entry(uint64_t expiry,
						void (*callback)(void *),
						void *args)
{
	timer_entry_t *new_entry =
		gsh_calloc(1, sizeof(timer_entry_t), MEM_COMP_QOS);

	new_entry->expiry = expiry;
	new_entry->callback = callback;
	new_entry->args = args;
	glist_init(&new_entry->timer_list);
	LogDebug(COMPONENT_QOS, "Timer entry created: %p cb: %s", new_entry,
		 qos_cb_str(callback));
	return new_entry;
}

/**
 * Function to get and insert a client details structure into
 * the token exhaust waitlist
 *
 * @param [in] head Pointer to the head of the waitlist
 * @param [in] data Compound data associated with the I/O request
 * @return Pointer to the client details structure
 */
static qos_client_entry_t *
get_and_insert_client_details(struct glist_head *head, compound_data_t *data)
{
	qos_client_entry_t *new_entry = NULL;
	qos_client_entry_t *current = NULL;
	struct glist_head *glist;

	glist_for_each(glist, head) {
		current = glist_entry(glist, qos_client_entry_t, token_ex_cl);
		if (current->gsh_client == op_ctx->client)
			return current;
	}

	new_entry = alloc_clientdetails_pe(data);
	glist_add_tail(head, &new_entry->token_ex_cl);
	return new_entry;
}

/**
 * Function to insert a timer entry into the waitlist
 *
 * Note : An entry may be a new entry, or it may be an entry being
 *	  moved from another sorted list to this list.
 *	  function returns the current entry as list head his is done so that
 *	  caller will send the current glist as head to next entry addition,
 *        which will save iteration again from start of list.
 *
 * @param [in] head Pointer to the head of the waitlist
 *	  head Pointer can be :
 *		1. Token Exhausted waitlist.
 *		2. BW control waitlist.
 *		3. IOPS control waitlist.
 * @param [in] new_entry Pointer to the new timer entry,
 *
 * @return Ptr to glist_head.
 */
struct glist_head *insert_timer_entry_sorted(struct glist_head *head,
					     timer_entry_t *new_entry)
{
	struct glist_head *glist;
	timer_entry_t *current;

	if (!new_entry) {
		LogCrit(COMPONENT_QOS, "ERROR new entry is NULL");
		return NULL;
	}

	glist_for_each(glist, head) {
		current = glist_entry(glist, timer_entry_t, timer_list);
		if (current->expiry > new_entry->expiry) {
			glist_add(glist->prev, &new_entry->timer_list);
			return &new_entry->timer_list;
		}
	}
	glist_add_tail(head, &new_entry->timer_list);
	return &new_entry->timer_list;
}

/**
 * Function to insert a timer entry into the waitlist tail
 *
 * Note: Caller should hold export and client bucket->lock.
 *
 * @param [in] head Pointer to the head of the waitlist
 *	  head Pointer can be :
 *		1. Token Exhausted waitlist.
 *		2. BW control waitlist.
 *		3. IOPS control waitlist.
 * @param [in] new_entry Pointer to the new timer entry.
 */
static inline void insert_timer_entry(struct glist_head *head,
				      timer_entry_t *new_entry)
{
	glist_add_tail(head, &new_entry->timer_list);
}

/**
 * Function to resume the given timerentry and free the resource
 *
 * Note: Caller should hold export and client bucket->lock.
 *
 * @param [in] entry Pointer for which resume need to be called and freed.
 */
static inline void resume_timer_entry(timer_entry_t *entry)
{
	LogDebug(COMPONENT_QOS, "Timer entry resume: %p cb: %s", entry,
		 qos_cb_str(entry->callback));

	entry->callback(entry->args);
	glist_del(&entry->timer_list);
	gsh_free(entry, MEM_COMP_QOS);
}

/**
 * Function to release I/Os from the waitlist (Force release IO)
 *
 * @param [in] head Pointer to the head of the waitlist
 * @param [in] counter1 Pointer to a counter for the number of waiting I/Os.
 * @param [in] counter2 Pointer to counter for the number of waiting I/Os.
 *			will be used in case of IO got blocked on token exhaust
 */
static inline void release_wait_ios(struct glist_head *head,
				    unsigned int *counter1,
				    unsigned int *counter2)
{
	struct glist_head *glist, *glistn;
	timer_entry_t *entry;

	glist_for_each_safe(glist, glistn, head) {
		entry = glist_entry(glist, timer_entry_t, timer_list);
		resume_timer_entry(entry);
		--*counter1;
		--*counter2;
	}
}

/**
 * Function to execute expired timers in the waitlist
 *
 * @param [in] head Pointer to the head of the waitlist
 * @param [in] counter1 Pointer to a counter for the number of waiting I/Os.
 * @param [in] counter2 Pointer to another counter for the number of wait I/Os.
 */

#if ENABLE_CLUSTER_QOS
static inline void execute_qos_expired_timers(struct glist_head *head,
						unsigned int *counter1,
						unsigned int *counter2,
						uint64_t *cqos_value)
#else
static inline void execute_qos_expired_timers(struct glist_head *head,
						unsigned int *counter1,
						unsigned int *counter2)
#endif
{
	uint64_t ctime = get_time_in_usec();
	struct glist_head *glist, *glistn;
	timer_entry_t *entry;

	glist_for_each_safe(glist, glistn, head) {
		entry = glist_entry(glist, timer_entry_t, timer_list);
		if (entry->expiry <= ctime) {
#if ENABLE_CLUSTER_QOS
			if (cqos_initialized)
				*cqos_value += entry->size;
#endif
			resume_timer_entry(entry);
			--*counter1;
			--*counter2;
			continue;
		}
		break;
	}
}

/**
 * Function to get the bucket for a given QoS class and operation type.
 *			and operation type.
 * This function should be used directly while updating the config values.
 *
 * @param [in] entry Pointer to the entry (qos_class_t or qos_class_t).
 * @param [in] op_type Type of the operation (read/write).
 * @return Pointer to qos_bucket_t forr the specified entry,
 *			class type, and operation type.
 */
static inline qos_bucket_t *qos_get_bucket(qos_class_t *qos_class,
					   qos_op_type_t op_type)
{
	return (op_type == QOS_READ) ? &(qos_class->rbucket)
				     : &(qos_class->wbucket);
}

/**
 * Function to get the token bucket for a given QoS class and operation type.
 * Functions should not be used while updating the conf values.
 *
 * @param [in] qos_class Pointer to the QoS export or client object
 * @param [in] op_type Type of the operation (read/write)
 * @return Pointer to the bandwidth bucket, or NULL if not found
 */
static inline qos_bucket_t *qos_get_token_bucket(qos_class_t *qos_class,
						 qos_op_type_t op_type)
{
	if (!qos_class->token_enabled)
		return NULL;
	else if (qos_class->combined_rw_token_control)
		op_type = QOS_WRITE;
	return qos_get_bucket(qos_class, op_type);
}

/**
 * Function to get the bandwidth bucket for a given QoS class and operation type
 * Functions should not be used while updating the conf values.
 *
 * @param [in] qos_class Pointer to the QoS export or client object
 * @param [in] op_type Type of the operation (read/write)
 * @return Pointer to the bandwidth bucket, or NULL if not found
 */
qos_bucket_t *qos_get_bw_bucket(qos_class_t *qos_class,
					      qos_op_type_t op_type)
{
	if (!qos_class->bw_enabled)
		return NULL;
	else if (qos_class->combined_rw_bw_control)
		op_type = QOS_WRITE;
	return qos_get_bucket(qos_class, op_type);
}

/**
 * Function to get the IOPS bucket for a given QoS class and operation type
 * Functions should not be used while updating the conf values.
 *
 * @param [in] qos_class Pointer to the QoS export or client object
 * @param [in] op_type Type of the operation (read/write)
 * @return Pointer to the IOPS bucket, or NULL if not found
 */
qos_bucket_t *qos_get_iops_bucket(qos_class_t *qos_class,
						qos_op_type_t op_type)
{
	if (!qos_class->iops_enabled)
		return NULL;
	else if (qos_class->combined_rw_iops_control)
		op_type = QOS_WRITE;
	return qos_get_bucket(qos_class, op_type);
}

/**
 * Function to allocate a new qos_class_t structure.
 *
 * @return Pointer to the newly allocated qos_class_t structure.
 */
qos_class_t *allocate_client(struct gsh_client *gsh_client,
			     struct qos_block_config *qos_block)
{
	qos_class_t *node;

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	if (!gsh_client->qos_class)
		node = allocate_qos_class(QOS_CLIENT);
	else
		node = gsh_client->qos_class;
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);

	setNode_pc(node, gsh_client, qos_block);
	return node;
}

/**
 * Function to get the client from export list.
 *
 * Note : Caller should hold export->lock.
 *
 * @param [in] head Pointer to the client list.
 * @param [in] gsh_client Address of the client.
 * @return Pointer to client, if available in the given list, else NULL.
 */
qos_class_t *pepc_get_client_from_list(struct glist_head *head,
				       struct gsh_client *gsh_client)
{
	struct glist_head *glist;
	qos_class_t *qos_class;

	glist_for_each(glist, head) {
		qos_class = glist_entry(glist, qos_class_t, clients);
		if (qos_class->gsh_client == gsh_client)
			return qos_class;
	}
	/* Client Not Found */
	return NULL;
}

/**
 * Function to get a specific client from export list in pepc.
 * If client not present allocate a new client and
 * add to the export list and return the new client
 *
 * @param [in] export Pointer to qos_class_t structure representing the export.
 * @param [in] gsh_client Address of the client.
 * @return Pointer to retrieved qos_class_t structure, or NULL if not found.
 */
qos_class_t *pepc_alloc_get_client(qos_class_t *qos_class,
				   struct gsh_client *gsh_client)
{
	qos_class_t *sub_qos_class;

	PTHREAD_MUTEX_lock(&qos_class->lock);
	sub_qos_class =
		pepc_get_client_from_list(&qos_class->clients, gsh_client);
	/* Client Not Found */
	if (!sub_qos_class) {
		sub_qos_class = allocate_client(gsh_client,
						op_ctx->ctx_export->qos_block);
		glist_add_tail(&(qos_class->clients), &sub_qos_class->clients);
	}
	PTHREAD_MUTEX_unlock(&qos_class->lock);

	return sub_qos_class;
}

/**
 * Function to remove/delete all the client from list.
 *
 * @param [in] head Pointer to the client list.
 * @return void
 */
static inline void pepc_free_client_list(struct glist_head *head)
{
	struct glist_head *glist, *glistn;
	qos_class_t *qos_class;

	glist_for_each_safe(glist, glistn, head) {
		qos_class = glist_entry(glist, qos_class_t, clients);
		glist_del(&qos_class->clients);
		gsh_free(qos_class, MEM_COMP_QOS);
	}
}

/**
 * @brief Get the number of clients associated with a QoS export.
 *
 * This function counts the number of clients in a given QoS export class.
 * Used in case of reply to User via DBUS
 *
 * @param [in] e_qos_class Pointer to qos_class_t representing the QoS export
 *
 * @return uint32_t number of clients in the export, 0 if the export is NULL
 */

uint32_t get_export_client_count(qos_class_t *qos_class)
{
	uint32_t count = 0;
	struct glist_head *glist;

	if (!qos_class)
		return count;

	PTHREAD_MUTEX_lock(&qos_class->lock);
	glist_for_each(glist, &qos_class->clients) {
		count++;
	}
	PTHREAD_MUTEX_unlock(&qos_class->lock);

	return count;
}

/**
 * Function to drain all Token I/Os for a given QoS class
 *
 * Note : Caller should hold the qos_class->lock.
 *
 * @param [in] qos_class Pointer to the QoS export or client object
 */
static inline void qos_drain_token_ios(qos_class_t *qos_class)
{
	struct glist_head *client_list;
	uint32_t *num_ios_waiting = NULL;
	bool token_enabled = false;

	token_enabled = qos_class->token_enabled;
	client_list = &(qos_class->client_entries);
	num_ios_waiting = &(qos_class->num_ios_waiting);

	if (token_enabled && !glist_empty(client_list)) {
		struct glist_head *glist, *glistn;
		qos_client_entry_t *token_client;

		glist_for_each_safe(glist, glistn, client_list) {
			token_client = glist_entry(glist, qos_client_entry_t,
						   token_ex_cl);
			release_wait_ios(&token_client->io_waitlist_qos,
					 num_ios_waiting,
					 &token_client->num_ios_waiting);
		}
	}

#ifdef USE_MONITORING
	if (qos_class->enabled_tokens_metric) {
		qos_bucket_t *rbucket =
			qos_get_token_bucket(qos_class, QOS_READ);
		qos_bucket_t *wbucket =
			qos_get_token_bucket(qos_class, QOS_WRITE);

		monitoring__gauge_set(rbucket->tokens_metric_handler, 0);
		monitoring__gauge_set(wbucket->tokens_metric_handler, 0);
		qos_class->enabled_tokens_metric = false;
	}
#endif
}

/**
 * Function to drain all BW I/Os for a given QoS class
 *
 * Note : Caller should hold the qos_class->lock.
 *
 * @param [in] qos_class Pointer to the QoS export or client object
 */
void qos_drain_bw_ios(qos_class_t *qos_class)
{
	bool bw_enabled = false;
	qos_bucket_t *rbucket = qos_get_bw_bucket(qos_class, QOS_READ);
	qos_bucket_t *wbucket = qos_get_bw_bucket(qos_class, QOS_WRITE);
	int dummy_counter = 0;

	bw_enabled = qos_class->bw_enabled;
	qos_class->bw_enabled = 0;

	if (bw_enabled) {
		if (!glist_empty(&rbucket->io_waitlist_qos_bc)) {
			release_wait_ios(&(rbucket->io_waitlist_qos_bc),
					 &(rbucket->num_ios_waiting),
					 &dummy_counter);
		}
		if (!glist_empty(&wbucket->io_waitlist_qos_bc)) {
			release_wait_ios(&(wbucket->io_waitlist_qos_bc),
					 &(wbucket->num_ios_waiting),
					 &dummy_counter);
		}
	}
#ifdef USE_MONITORING
	if (qos_class->enabled_bw_metric) {
		monitoring__gauge_set(rbucket->bw_metric_handler, 0);
		monitoring__gauge_set(wbucket->bw_metric_handler, 0);
		qos_class->enabled_bw_metric = false;
	}
#endif
}

/**
 * Function to drain all IOPS I/Os for a given QoS class
 *
 * Note : Caller should hold the qos_class->lock.
 *
 * @param [in] qos_class Pointer to the QoS export or client object
 */
void qos_drain_iops_ios(qos_class_t *qos_class)
{
	bool iops_enabled = false;
	qos_bucket_t *rbucket = qos_get_iops_bucket(qos_class, QOS_READ);
	qos_bucket_t *wbucket = qos_get_iops_bucket(qos_class, QOS_WRITE);
	int dummy_counter = 0;

	iops_enabled = qos_class->iops_enabled;
	qos_class->iops_enabled = 0;

	if (iops_enabled) {
		if (!glist_empty(&rbucket->io_waitlist_qos_iops)) {
			release_wait_ios(&(rbucket->io_waitlist_qos_iops),
					 &(rbucket->num_ios_waiting),
					 &dummy_counter);
		}
		if (!glist_empty(&wbucket->io_waitlist_qos_iops)) {
			release_wait_ios(&(wbucket->io_waitlist_qos_iops),
					 &(wbucket->num_ios_waiting),
					 &dummy_counter);
		}
#ifdef USE_MONITORING
		if (qos_class->enabled_iops_metric) {
			monitoring__gauge_set(rbucket->iops_metric_handler, 0);
			monitoring__gauge_set(wbucket->iops_metric_handler, 0);
			qos_class->enabled_iops_metric = false;
		}
#endif
	}
}

/**
 * Function to drain all I/Os for a given QoS class.
 *
 * @param [in] qos_class Pointer to the QoS export or client object.
 */
void qos_drain_ios(qos_class_t *qos_class)
{
	if (!qos_class)
		return;

	QOS_PRINT_CLASS(__func__, qos_class);
	PTHREAD_MUTEX_lock(&(qos_class->lock));
	qos_drain_token_ios(qos_class);
	qos_drain_bw_ios(qos_class);
	qos_drain_iops_ios(qos_class);
	PTHREAD_MUTEX_unlock(&(qos_class->lock));
}

/**
 * Callback function to QoS free memory associated with a gsh_export structure.
 *
 * @param [in] export Pointer to the gsh_export structure.
 * @param [in] state Pointer to the struct gsh_client structure representing
 *			the client address.
 *
 * @return True, so that the iteration should continue.
 */
bool pepc_per_export_free_mem_iter(struct gsh_export *gsh_export, void *state)
{
	qos_class_t *qos_class = gsh_export->qos_class;
	qos_class_t *sub_qos_class = NULL;

	if (!qos_class)
		return true;

	PTHREAD_MUTEX_lock(&qos_class->lock);
	/* list is not populated */
	if (glist_empty(&qos_class->clients)) {
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		return true;
	}

	sub_qos_class = pepc_get_client_from_list(&(qos_class->clients),
						  (struct gsh_client *)state);
	if (sub_qos_class)
		glist_del(&sub_qos_class->clients);
	PTHREAD_MUTEX_unlock(&qos_class->lock);

	if (sub_qos_class) {
		QOS_PRINT_EXPORT("freeing client for export", gsh_export);
		qos_drain_ios(sub_qos_class);
		gsh_free(sub_qos_class, MEM_COMP_QOS);
	} else {
		QOS_PRINT_EXPORT("tried client for export", gsh_export);
		QOS_PRINT_CLIENT(__func__, ((struct gsh_client *)state));
	}
	/* Continue the Iteration for next export */
	return true;
}

/**
 * Function to free memory associated with a QoS class and its buckets.
 * QOS Structures and function hooks are placed in such a way that
 * gsh_export gets freed only after the asociated freeing of qos struct.
 * the same is applicable to gsh_client, first qos struct gets freed and
 * then gsh_client.
 * @param [in] gsh_ptr Pointer to the gsh_export or gsh_client structure.
 * @param [in] qos_class_type Type of the QoS class (export/client).
 */
void qos_free_mem(void *gsh_ptr, qos_class_type_t class_type)
{
	struct gsh_export *export = gsh_ptr;
	struct gsh_client *client = gsh_ptr;

	if (class_type == QOS_EXPORT) {
		if (!export || !export->qos_class)
			return;
	} else {
		if (!client)
			return;
	}

	PTHREAD_MUTEX_lock(&g_qos_config_lock);

	switch (g_qos_config->qos_type) {
	case QOS_NOT_ENABLED:
		LogDebug(COMPONENT_QOS, "QOS not enabled: %d",
			 g_qos_config->qos_type);
		break;
	case QOS_PER_EXPORT_ENABLED:
		if (class_type == QOS_EXPORT) {
			qos_drain_ios(export->qos_class);
			gsh_free(export->qos_class, MEM_COMP_QOS);
			if (export->qos_block)
				gsh_free(export->qos_block, MEM_COMP_QOS);
			export->qos_class = NULL;
			export->qos_block = NULL;
		}
		break;
	case QOS_PER_CLIENT_ENABLED:
		if (class_type == QOS_CLIENT && client->qos_class) {
			qos_drain_ios(client->qos_class);
			gsh_free(client->qos_class, MEM_COMP_QOS);
			client->qos_class = NULL;
		}
		break;
	case QOS_PEREXPORT_PERCLIENT_ENABLED:
		if (class_type == QOS_EXPORT) {
			qos_class_t *qos_class = export->qos_class;
			qos_class_t *sub_qos_class;
			struct glist_head *glist;

			/* releasing waiting io's */
			PTHREAD_MUTEX_lock(&qos_class->lock);
			glist_for_each(glist, &export->clients) {
				sub_qos_class = glist_entry(glist, qos_class_t,
							    clients);
				qos_drain_ios(sub_qos_class);
			}
			pepc_free_client_list(&qos_class->clients);
			PTHREAD_MUTEX_unlock(&qos_class->lock);
			qos_drain_ios(qos_class);
			gsh_free(export->qos_class, MEM_COMP_QOS);
			if (export->qos_block)
				gsh_free(export->qos_block, MEM_COMP_QOS);
			export->qos_class = NULL;
			export->qos_block = NULL;
		} else {
			QOS_PRINT_CLIENT("trying to free from all exports",
					 ((struct gsh_client *)gsh_ptr));
			foreach_gsh_export(pepc_per_export_free_mem_iter, false,
					   client)
				;
		}
		break;
	default:
		LogDebug(COMPONENT_QOS, " Something really wrong: %d ",
			 g_qos_config->qos_type);
	}

	PTHREAD_MUTEX_unlock(&g_qos_config_lock);
}

/**
 * Function to copy memory from one gsh_export structure to another.
 *
 * @param [in] dest Pointer to the destination gsh_export structure.
 * @param [in] src Pointer to the source gsh_export structure.
 */
void copy_gsh_qos_conf(struct gsh_export *dest, struct gsh_export *src)
{
	if (!g_qos_config->enable_qos)
		return;

	switch (g_qos_config->qos_type) {
	case QOS_NOT_ENABLED:
		LogDebug(COMPONENT_QOS, "QOS not enabled: %d",
			 g_qos_config->qos_type);
		break;
	case QOS_PER_EXPORT_ENABLED:
		qos_block_config_t *new_values_pe = NULL;

		if (src->qos_block) {
			if (dest->qos_block) {
				memcpy(dest->qos_block, src->qos_block,
				       sizeof(qos_block_config_t));
				/*  Set the new values to the qos_class
				 *  using new dest->qos_block value */
				new_values_pe = dest->qos_block;
			} else {
				/* Refelect the new values to export
				 * and new struct will be freed
				 * newly provided qos_block in config file*/
				new_values_pe = src->qos_block;
			}
		} else {
			/* Refelect the global values to export*/
			new_values_pe = g_qos_config;
		}
		LogDebug(COMPONENT_QOS, "eid: %d sb: %p dp: %p gv: %p nv: %p",
			 dest->export_id, src->qos_block, dest->qos_block,
			 g_qos_config, new_values_pe);
		qos_perexport_insert(dest, new_values_pe);
		break;

	case QOS_PER_CLIENT_ENABLED:
		LogDebug(COMPONENT_QOS, "NOT expected");
		break;

	case QOS_PEREXPORT_PERCLIENT_ENABLED:
		qos_block_config_t *new_values_pepc = NULL;
		qos_class_t *qos_class = NULL;
		qos_class_t *sub_qos_class = NULL;
		struct glist_head *glist;

		if (src->qos_block) {
			if (dest->qos_block) {
				memcpy(dest->qos_block, src->qos_block,
				       sizeof(qos_block_config_t));
				new_values_pepc = dest->qos_block;
			} else {
				new_values_pepc = src->qos_block;
			}
		} else {
			new_values_pepc = g_qos_config;
		}
		LogDebug(COMPONENT_QOS, "eid: %d sb: %p dp: %p gv: %p nv: %p",
			 dest->export_id, src->qos_block, dest->qos_block,
			 g_qos_config, new_values_pepc);
		qos_perexport_insert(dest, new_values_pepc);
		qos_class = dest->qos_class;
		PTHREAD_MUTEX_lock(&qos_class->lock);
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);
			PTHREAD_MUTEX_lock(&sub_qos_class->lock);
			setNode_pc(sub_qos_class, sub_qos_class->gsh_client,
				   dest->qos_block);
			PTHREAD_MUTEX_unlock(&sub_qos_class->lock);
		}
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		break;
	default:
		LogDebug(COMPONENT_QOS, " Something really wrong: %d ",
			 g_qos_config->qos_type);
	}
}

/**
 * Function to print the values of a qos_bucket_t structure.
 *
 * @param [in] bucket Pointer to the qos_bucket_t structure to be printed.
 */
static inline void print_bucket_values(qos_bucket_t *bucket)
{
	LogDebug(COMPONENT_QOS,
		 "wio: %" PRIu32 " bw: %" PRIu64 " bw_ldct: %" PRIu64
		 " mat: %" PRIu64 " tc: %" PRIu64 " trt: %" PRIu64
		 " ltct: %" PRIu64 " max_iops: %" PRIu64 " iops_ldct: %" PRIu64
		 " iops_consumed: %" PRIu64,
		 bucket->num_ios_waiting, bucket->max_bw_allowed,
		 bucket->bw_ldct, bucket->max_available_tokens,
		 bucket->tokens_consumed, bucket->tokens_renew_time,
		 bucket->token_ldct, bucket->max_iops_allowed,
		 bucket->iops_ldct, bucket->iops_consumed);
}

/**
 * Function to print the values of a QoS class (export/client).
 *
 * @param [in] qos_class Pointer to the QoS client or export structure.
 * @param [in] str Unique string to be printed before the class details.
 */
static inline void print_class_values(qos_class_t *class, const char *str)
{
	QOS_PRINT_CLASS(str, class);

	LogDebug(COMPONENT_QOS,
		 "wio: %" PRIu32
		 " bw_e: %d t_e: %d iops_e: %d c_bw: %d c_t: %d c_iops: %d",
		 class->num_ios_waiting, class->bw_enabled,
		 class->token_enabled, class->iops_enabled,
		 class->combined_rw_bw_control,
		 class->combined_rw_token_control,
		 class->combined_rw_iops_control);
	print_bucket_values(&(class->rbucket));
	print_bucket_values(&(class->wbucket));
}

/**
 * Function to set the value of a token bucket.
 *
 * @param [in] bucket Pointer to the qos_bucket_t structure
 *		representing the token bucket.
 * @param [in] max_tokens Maximum number of tokens allowed in the bucket.
 * @param [in] tokens_renew_time Time interval for renewing tokens in seconds.
 */
static inline void set_bucket_value_token(qos_bucket_t *bucket,
					  uint64_t max_tokens,
					  uint64_t tokens_renew_time)
{
	bucket->max_available_tokens = max_tokens;
	bucket->tokens_renew_time = (tokens_renew_time * 1000000);
}

/**
 * Function to set the value of a bandwidth bucket.
 *
 * @param [in] bucket Pointer to the qos_bucket_t structure
 *			representing the bandwidth bucket.
 * @param [in] max_bw Maximum bandwidth allowed in bytes per second.
 */
static inline void set_bucket_value_bw(qos_bucket_t *bucket, uint64_t max_bw)
{
	bucket->max_bw_allowed = max_bw;
}

/**
 * Function to set the value of an IOPS bucket.
 *
 * @param [in] bucket Pointer to the qos_bucket_t structure
 *			representing the IOPS bucket.
 * @param [in] max_iops Maximum IOPS allowed.
 */
static inline void set_bucket_value_iops(qos_bucket_t *bucket,
					 uint64_t max_iops)
{
	bucket->max_iops_allowed = max_iops;
}

/**
 * Function to update Token values for a QoS class
 *
 * @param [in] class Pointer to the QoS export or client object
 * @param [in] in  New values which are to be applied to token
 */
static void update_class_token_values(qos_class_t *qos_class,
				      struct qos_block_config *in)
{
	qos_bucket_t *rbucket = qos_get_bucket(qos_class, QOS_READ);
	qos_bucket_t *wbucket = qos_get_bucket(qos_class, QOS_WRITE);
	bool *token_enabled = NULL;
	bool *combined_rw_token_control = NULL;
	uint64_t max_write_token = 0;
	uint64_t max_write_token_renew_time = 0;
	uint64_t max_read_token = 0;
	uint64_t max_read_token_renew_time = 0;

	token_enabled = &(qos_class->token_enabled);
	combined_rw_token_control = &(qos_class->combined_rw_token_control);

	if (qos_class->type == QOS_EXPORT) {
		max_write_token = in->max_export_write_tokens;
		max_write_token_renew_time = in->export_write_tokens_renew_time;
		max_read_token = in->max_export_read_tokens;
		max_read_token_renew_time = in->export_read_tokens_renew_time;
	} else {
		max_write_token = in->max_client_write_tokens;
		max_write_token_renew_time = in->client_write_tokens_renew_time;
		max_read_token = in->max_client_read_tokens;
		max_read_token_renew_time = in->client_read_tokens_renew_time;
	}
	/* if true : Runtime enabling/updating of values */
	if ((g_qos_config->enable_tokens && in->enable_tokens)) {
		qos_drain_token_ios(qos_class);
		if (in->combined_rw_token_control) {
			set_bucket_value_token(wbucket, max_write_token,
					       max_write_token_renew_time);
			*combined_rw_token_control = true;
		} else {
			set_bucket_value_token(wbucket, max_write_token,
					       max_write_token_renew_time);
			set_bucket_value_token(rbucket, max_read_token,
					       max_read_token_renew_time);
			*combined_rw_token_control = false;
		}
		*token_enabled = in->enable_tokens;
	} else {
		/* Runtime disabling of particular config */
		if (*token_enabled)
			qos_drain_token_ios(qos_class);
	}
}

/**
 * Function to update DS values for a QoS class
 * This requires parent control(BW/token) to be enabled first.
 *
 * @param [in] class Pointer to the QoS export or client object
 * @param [in] in  New values which are to be applied to DS
 */
static void update_class_ds_values(qos_class_t *qos_class,
				   struct qos_block_config *in)
{
	bool *ds_enabled = NULL;

	ds_enabled = &(qos_class->ds_enabled);

	if (g_qos_config->enable_ds_control && in->enable_ds_control) {
		*ds_enabled = true;
	} else {
		*ds_enabled = false;
	}
}

/**
 * Function to update BW values for a QoS class
 *
 * @param [in] class Pointer to the QoS export or client object
 * @param [in] in  New values which are to be applied to BW
 */
static void update_class_bw_values(qos_class_t *qos_class,
				   struct qos_block_config *in)
{
	qos_bucket_t *rbucket = qos_get_bucket(qos_class, QOS_READ);
	qos_bucket_t *wbucket = qos_get_bucket(qos_class, QOS_WRITE);
	bool *bw_enabled = NULL;
	bool *combined_rw_bw_control = NULL;
	uint64_t max_combined_bw = 0;
	uint64_t max_write_bw = 0;
	uint64_t max_read_bw = 0;
	int dummy_counter = UINT32_MAX;

	bw_enabled = &(qos_class->bw_enabled);
	combined_rw_bw_control = &(qos_class->combined_rw_bw_control);

	if (qos_class->type == QOS_EXPORT) {
		max_combined_bw = in->max_export_combined_bw;
		max_write_bw = in->max_export_write_bw;
		max_read_bw = in->max_export_read_bw;
	} else {
		max_combined_bw = in->max_client_combined_bw;
		max_write_bw = in->max_client_write_bw;
		max_read_bw = in->max_client_read_bw;
	}

	/* if true : Runtime enabling/updating of values */
	if ((g_qos_config->enable_bw_control && in->enable_bw_control)) {
		if (in->combined_rw_bw_control) {
			set_bucket_value_bw(wbucket, max_combined_bw);
			/* Run time switching from 2 bucket to 1 bucket */
			if (!glist_empty(&wbucket->io_waitlist_qos_bc)) {
				release_wait_ios(&(rbucket->io_waitlist_qos_bc),
						 &(rbucket->num_ios_waiting),
						 &dummy_counter);
			}
			*combined_rw_bw_control = true;
		} else {
			set_bucket_value_bw(wbucket, max_write_bw);
			set_bucket_value_bw(rbucket, max_read_bw);
			*combined_rw_bw_control = false;
		}
		*bw_enabled = in->enable_bw_control;
	} else {
		/* Runtime disabling of particular config */
		if (*bw_enabled)
			qos_drain_bw_ios(qos_class);
	}
}

/**
 * Function to update IOPS values for a QoS class
 *
 * @param [in] class Pointer to the QoS export or client object
 * @param [in] in  New values which are to be applied to iops
 */
static void update_class_iops_values(qos_class_t *qos_class,
				     struct qos_block_config *in)
{
	qos_bucket_t *rbucket = qos_get_bucket(qos_class, QOS_READ);
	qos_bucket_t *wbucket = qos_get_bucket(qos_class, QOS_WRITE);
	bool *iops_enabled = NULL;
	bool *combined_rw_iops_control = NULL;
	uint64_t max_combined_iops = 0;
	uint64_t max_write_iops = 0;
	uint64_t max_read_iops = 0;
	int dummy_counter = UINT32_MAX;

	iops_enabled = &(qos_class->iops_enabled);
	combined_rw_iops_control = &(qos_class->combined_rw_iops_control);

	if (qos_class->type == QOS_EXPORT) {
		max_combined_iops = in->max_export_combined_iops;
		max_write_iops = in->max_export_write_iops;
		max_read_iops = in->max_export_read_iops;
	} else {
		max_combined_iops = in->max_client_combined_iops;
		max_write_iops = in->max_client_write_iops;
		max_read_iops = in->max_client_read_iops;
	}

	/* if true : Runtime enabling/updating of values */
	if ((g_qos_config->enable_iops_control && in->enable_iops_control)) {
		if (in->combined_rw_iops_control) {
			set_bucket_value_iops(wbucket, max_combined_iops);
			/* Run time switching from 2 bucket to 1 bucket */
			if (!glist_empty(&rbucket->io_waitlist_qos_iops)) {
				release_wait_ios(
					&(rbucket->io_waitlist_qos_iops),
					&(rbucket->num_ios_waiting),
					&dummy_counter);
			}
			*combined_rw_iops_control = true;
		} else {
			set_bucket_value_iops(wbucket, max_write_iops);
			set_bucket_value_iops(rbucket, max_read_iops);
			*combined_rw_iops_control = false;
		}
		*iops_enabled = in->enable_iops_control;
	} else {
		/* Runtime disabling of particular config */
		if (*iops_enabled)
			qos_drain_iops_ios(qos_class);
	}
}

/**
 * Function to set values for a QoS class (export/client).
 *
 * @param [in] qos_class struture pointer to which values need to be applied.
 * @param [in] in Pointer to the qos_block_config_t structure containing
 *			new/updated configuration information.
 */
static void set_class_values(qos_class_t *qos_class,
			     struct qos_block_config *in)
{
	if (!in->enable_qos) {
		in->enable_tokens = false;
		in->enable_iops_control = false;
		in->enable_bw_control = false;
		in->enable_ds_control = false;
	}

	update_class_token_values(qos_class, in);
	update_class_bw_values(qos_class, in);
	update_class_ds_values(qos_class, in);
	update_class_iops_values(qos_class, in);
}

/**
 * Function to set values for a QoS export node.
 * qos_class->lock needs to held on config update path only.
 * g_qos_iopath_lock needs to be held if called from IO path.
 *
 * @param [in] node Pointer to qos_class_t structure representing the export.
 * @param [in] gsh_export->export_id ID of the export.
 * @param [in] qos_block Pointer to the qos_block_config_t structure containing
 *			new/updated configuration information.
 */
static void setNode_pe(qos_class_t *qos_class, struct gsh_export *export,
		       struct qos_block_config *qos_block)
{
	qos_class->gsh_export = export;

	if (!g_qos_config->enable_qos)
		return;

	LogDebug(COMPONENT_QOS, "Added new config:");
	set_class_values(qos_class, qos_block);
	print_class_values(qos_class, __func__);
}

/**
 * Function to set values for a QoS client node.
 * qos_class->lock needs to held on config update path only.
 * g_qos_iopath_lock needs to be held if called from IO path.
 *
 * @param [in] node Pointer qos_class_t structure representing the client.
 * @param [in] gsh_client Address of the client.
 * @param [in] qos_block Pointer to the qos_block_config_t structure containing
 *			new/updated configuration information.
 */
static void setNode_pc(qos_class_t *node, struct gsh_client *gsh_client,
		       struct qos_block_config *qos_block)
{
	node->gsh_client = gsh_client;

	if (!g_qos_config->enable_qos)
		return;

	LogDebug(COMPONENT_QOS, "Added new config:");
	set_class_values(node, qos_block);
	print_class_values(node, __func__);
}

/**
 * register_metric - Registers a QoS-related gauge metric with
 * the monitoring system.
 *
 * @class_type: Type of QoS class. Can be QOS_EXPORT, QOS_CLIENT,
 * or defaulting to "pepc".
 * @sub_type: A string indicating the subtype (e.g., "bw", "iops","tokens").
 * @rd_wr: A string specifying whether the metric is for read or
 *         write operations.
 * @handle: Pointer to a gauge metric handle that will be initialized
 *          upon successful registration.
 * @label: Array of metric labels. The second label (label[1]) will be
 *         populated with a key-value pair derived from class_type and sub_type.
 * @value: The metric value to be set upon registration.
 * @max_value: A string representing the maximum value associated with the
 *             metric (used as label value).
 *
 * This function constructs metric names, labels, and metadata based
 * on the input parameters, registers the gauge metric, and sets
 * its initial value.
 * It helps in dynamically tracking
 * performance metrics per QoS class and operation type.
**/

#ifdef USE_MONITORING
static void register_metric(char *sub_type, char *rd_wr,
			    struct gauge_metric_handle *handle,
			    metric_label_t *label, uint64_t value,
			    char *max_value)
{
	char label_key[MAX_LABEL_KEY_LEN];
	char *type =
		(g_qos_config->qos_type == QOS_PER_EXPORT_ENABLED)   ? "export"
		: (g_qos_config->qos_type == QOS_PER_CLIENT_ENABLED) ? "client"
								     : "pepc";

	snprintf(label_key, MAX_LABEL_KEY_LEN, "max_%s_of_%s", sub_type, type);
	label[1] = METRIC_LABEL(label_key, max_value);

	char meta_data_key[MAX_METADATA_KEY_LEN];

	snprintf(meta_data_key, MAX_METADATA_KEY_LEN,
		 "%s bucket of %s with value of %s consumed", rd_wr, type,
		 sub_type);
	metric_metadata_t meta_data =
		METRIC_METADATA(meta_data_key, METRIC_UNIT_NONE);

	char metric_name[MAX_METRIC_NAME_LEN];

	snprintf(metric_name, MAX_METRIC_NAME_LEN,
		 "QoS_per_%s_%sbucket_%s_info", type, rd_wr, sub_type);

	const metric_label_t labels[] = { label[0], label[1] };

	(*handle) = monitoring__register_gauge(metric_name, meta_data, labels,
					       ARRAY_SIZE(labels));

	monitoring__gauge_set((*handle), value);
}

/**
 * register_bucket_metrics - Register QoS bucket metrics for a class.
 *
 * Registers gauge metrics for bandwidth, IOPS, and token usage across
 * read/write buckets of a QoS class. Metrics are added only if the respective
 * features (bw, iops, tokens) are enabled in the class config.
 *
 * Uses `register_metric()` to construct and register each metric with the
 * appropriate labels and initial values.
 * @qos_class:   Pointer to a `qos_class_t` structure containing configuration
 *               flags and the QoS class type (e.g., client, export).
 * @rd_bucket:   Pointer to the QoS read bucket structure. Contains metrics and
 *               maximum allowed values for bandwidth, IOPS, and tokens.
 * @wr_bucket:   Pointer to the QoS write bucket structure. Similar to
 *               `rd_bucket`, but for write operations.
 * @label:       Array of metric labels passed to each registered metric. This
 *               typically includes information like the export_path or clientid
 *               or export_client.
 */

static void register_bucket_metrics(qos_class_t *qos_class,
				    qos_bucket_t *rd_bucket,
				    qos_bucket_t *wr_bucket,
				    metric_label_t *label)
{
	char max_value[QOS_MAX_LONG_STR_LEN];
	bool bw_enable = qos_class->bw_enabled;
	bool iops_enable = qos_class->iops_enabled;
	bool tokens_enable = qos_class->token_enabled;

	// Register metrics for Bandwidth if enabled.
	if (bw_enable && !(qos_class->enabled_bw_metric)) {
		// Read bucket BW
		snprintf(max_value, QOS_MAX_LONG_STR_LEN, "%" PRIu64,
			 rd_bucket->max_bw_allowed);
		register_metric("bw", "read", &(rd_bucket->bw_metric_handler),
				label, rd_bucket->data_consumed, max_value);
		// Write bucket BW
		snprintf(max_value, QOS_MAX_LONG_STR_LEN, "%" PRIu64,
			 wr_bucket->max_bw_allowed);
		register_metric("bw", "write", &(wr_bucket->bw_metric_handler),
				label, wr_bucket->data_consumed, max_value);
		qos_class->enabled_bw_metric = true;
	}
	// Register metrics for IOPS if enabled.
	if (iops_enable && !(qos_class->enabled_iops_metric)) {
		// Read bucket IOPS
		snprintf(max_value, QOS_MAX_LONG_STR_LEN, "%" PRIu64,
			 rd_bucket->max_iops_allowed);
		register_metric("iops", "read",
				&(rd_bucket->iops_metric_handler), label,
				rd_bucket->iops_consumed, max_value);
		// Write bucket IOPS
		snprintf(max_value, QOS_MAX_LONG_STR_LEN, "%" PRIu64,
			 wr_bucket->max_iops_allowed);
		register_metric("iops", "write",
				&(wr_bucket->iops_metric_handler), label,
				wr_bucket->iops_consumed, max_value);
		qos_class->enabled_iops_metric = true;
	}
	// Register metrics for Tokens if enabled.
	if (tokens_enable && !(qos_class->enabled_tokens_metric)) {
		// Read bucket Tokens
		snprintf(max_value, QOS_MAX_LONG_STR_LEN, "%" PRIu64,
			 rd_bucket->max_available_tokens);
		register_metric("tokens", "read",
				&(rd_bucket->tokens_metric_handler), label,
				rd_bucket->tokens_consumed, max_value);
		// Write bucket Tokens
		snprintf(max_value, QOS_MAX_LONG_STR_LEN, "%" PRIu64,
			 wr_bucket->max_available_tokens);
		register_metric("tokens", "write",
				&(wr_bucket->tokens_metric_handler), label,
				wr_bucket->tokens_consumed, max_value);
		qos_class->enabled_tokens_metric = true;
	}
}

/**
 * register_qos_metrics - Register metrics for a given QoS class.
 *
 * Initializes and registers gauge metrics for the provided QoS class based on
 * its type (EXPORT, CLIENT, or PEPC). For each type:
 *
 *   - EXPORT: Uses export path as a label.
 *   - CLIENT: Uses client IP address as a label.
 *   - PEPC: Iterates over associated client classes, combining export path and
 *           client address as the label.
 *
 * Delegates per-bucket metric registration to `register_bucket_metrics()` with
 * appropriate labels.
 */

static void register_qos_metrics(qos_class_t *qos_class)
{
	qos_bucket_t *rd_bucket = &(qos_class->rbucket);

	qos_bucket_t *wr_bucket = &(qos_class->wbucket);

	metric_label_t label[2];

	if (g_qos_config->qos_type == QOS_PER_EXPORT_ENABLED) {
		struct gsh_export *export = qos_class->gsh_export;

		label[0] = METRIC_LABEL("export_path", export->cfg_fullpath);
		register_bucket_metrics(qos_class, rd_bucket, wr_bucket, label);

	} else if (g_qos_config->qos_type == QOS_PER_CLIENT_ENABLED) {
		struct gsh_client *client = qos_class->gsh_client;

		char client_addr[SOCK_NAME_MAX];

		sprint_sockip(&(client->cl_addrbuf), client_addr,
			      SOCK_NAME_MAX);
		label[0] = METRIC_LABEL("client_Address", client_addr);
		register_bucket_metrics(qos_class, rd_bucket, wr_bucket, label);
	}

	else if (g_qos_config->qos_type == QOS_PEREXPORT_PERCLIENT_ENABLED) {
		struct glist_head *glist;
		struct gsh_export *export = qos_class->gsh_export;
		qos_class_t *client_qos_class;
		char client_addr[SOCK_NAME_MAX];
		char *export_client;
		int export_client_len;

		glist_for_each(glist, &qos_class->clients) {
			client_qos_class =
				glist_entry(glist, qos_class_t, clients);

			rd_bucket = &(client_qos_class->rbucket);
			wr_bucket = &(client_qos_class->wbucket);

			sprint_sockip(
				&(client_qos_class->gsh_client->cl_addrbuf),
				client_addr, SOCK_NAME_MAX);

			export_client_len = strlen(export->cfg_fullpath) +
					    strlen(client_addr) + 2;
			export_client = gsh_malloc(export_client_len,
						   MEM_COMP_QOS);
			snprintf(export_client, export_client_len, "%s_%s",
				 export->cfg_fullpath, client_addr);
			label[0] =
				METRIC_LABEL("export_clientid", export_client);
			register_bucket_metrics(client_qos_class, rd_bucket,
						wr_bucket, label);
			gsh_free(export_client, MEM_COMP_QOS);
		}
	}
}
#endif // USE_MONITORING

/**
 * Function to insert a new QoS configuration for a gsh_export and
 *		is used while updating new values at runtime.
 * g_qos_iopath_lock needs to be held, if getting called from IO path only.
 * Configpath doesnot require lock.
 *
 * @param [in] gsh_export Pointer to structure representing the gsh_export.
 * @param [in] qos_block Pointer to the qos_block_config_t structure
 *			new/updated containing configuration information.
 */
void qos_perexport_insert(struct gsh_export *gsh_export,
			  struct qos_block_config *qos_block)
{
	struct qos_block_config *lqos_block = NULL;

	if (qos_block)
		lqos_block = qos_block;
	else
		lqos_block = g_qos_config;

	PTHREAD_MUTEX_lock(&g_qos_config_lock);
	/*  Condition indicates this is new gsh_export, reexport or
	 *  run time enabled of QOS due to global config */
	if (!gsh_export->qos_class) {
		qos_class_t *node = allocate_qos_class(QOS_EXPORT);

		/* NULL Indicates QOS block is not populated
		 * i.e run time enabledment of QOS */
		if (!gsh_export->qos_block) {
			qos_block_config_t *new_block;

			new_block = gsh_calloc(1, sizeof(qos_block_config_t),
					       MEM_COMP_QOS);
			*new_block = *lqos_block;
			gsh_export->qos_block = new_block;
		}
		gsh_export->qos_class = node;
	} else {
		*(gsh_export->qos_block) = *lqos_block;
	}
	PTHREAD_MUTEX_unlock(&g_qos_config_lock);

	PTHREAD_MUTEX_lock(&gsh_export->qos_class->lock);
	setNode_pe(gsh_export->qos_class, gsh_export, lqos_block);
#ifdef USE_MONITORING
	/*
	 * Use lqos_block, not qos_block.
	 *
	 * Callers such as EnableExportQosBwControl pass qos_block = NULL
	 * to mean "apply the global QOS_DEFAULT_CONFIG". This function
	 * already resolves that to lqos_block = g_qos_config (see above).
	 * The rest of the insert path uses lqos_block. Dereferencing
	 * qos_block here SIGSEGV'd when monitoring was enabled:
	 * qos_block->qos_type with qos_block == NULL.
	 */
	if (lqos_block->qos_type == QOS_PER_EXPORT_ENABLED) {
		register_qos_metrics(gsh_export->qos_class);
	}
#endif
	PTHREAD_MUTEX_unlock(&gsh_export->qos_class->lock);
}

/**
 * Function to insert a new QoS configuration for a client.
 * Caller needs to hold the g_qos_iopath_lock
 *
 * @param [in] qos_block Pointer to the qos_block_config_t structure containing
 *			configuration information.
 * @param [in] client Pointer to gsh_client structure representing the client.
 */
void qos_perclientinsert(struct qos_block_config *qos_block,
			 struct gsh_client *client)
{
	struct qos_block_config *lqos_block = NULL;

	if (!qos_block)
		lqos_block = g_qos_config;
	else
		lqos_block = qos_block;

	client->qos_class = allocate_client(client, lqos_block);
#ifdef USE_MONITORING
	register_qos_metrics(client->qos_class);
#endif
}

/**
 * Function to check if there is enough token available in the bucket.
 *
 * @param [in] bucket Pointer to qos_bucket_t structure representing the token.
 * @param [in] rsize Size of the request for tokens.
 * @return True if the token is available, False otherwise.
 */
static inline bool qos_check_bucket_token_availability(qos_bucket_t *bucket,
						       uint64_t rsize)
{
	if (bucket->tokens_consumed <= bucket->max_available_tokens)
		return true;
	else
		return false;
}

/**
 * Function to consume tokens from the bucket.
 *
 * @param [in] bucket Pointer to qos_bucket_t structure representing the token.
 * @param [in] rsize Size of the request for tokens.
 */
static inline void qos_consume_bucket_token(qos_bucket_t *bucket,
					    uint64_t rsize)
{
	bucket->token_ldct = get_time_in_usec();
	bucket->tokens_consumed += rsize;
}

/**
 * Function to check if there is enough token available.
 *
 * @param [in] qos_class Pointer to the QoS client or export structure.
 * @param [in] rsize Size of the request for tokens.
 * @param [in] op_type Type of the operation (read/write).
 * @return True if token is consumed, False otherwise.
 */
static inline bool qos_check_token_availability(qos_class_t *qos_class,
						uint64_t rsize,
						qos_op_type_t op_type)
{
	qos_bucket_t *bucket = qos_get_token_bucket(qos_class, op_type);

	if (bucket == NULL)
		return true;
	return qos_check_bucket_token_availability(bucket, rsize);
}

/**
 * Function to consume tokens from the bucket.
 *
 * Note : Caller should hold qos_class->lock.
 *
 * @param [in] qos_class Pointer to the QoS client or export structure.
 * @param [in] rsize Size of the request for tokens.
 * @param [in] op_type Type of the operation (read/write).
 */
static inline void qos_consume_token(qos_class_t *qos_class, uint64_t rsize,
				     qos_op_type_t op_type)
{
	qos_bucket_t *bucket = qos_get_token_bucket(qos_class, op_type);

	if (bucket == NULL)
		return;
	qos_consume_bucket_token(bucket, rsize);
#ifdef USE_MONITORING
	if (qos_class->enabled_tokens_metric) {
		monitoring__gauge_set(bucket->tokens_metric_handler,
				      bucket->tokens_consumed);
	}
#endif
	return;
}

/**
 * Function to suspend a task for bandwidth control
 *
 * @param [in] bucket Pointer to the bucket object
 * @param [in] caller_data Caller data passed to the callback function
 * @param [in] rsize Size of the data in bytes
 * @param [in] timeout Task timeout in microseconds
 * @param [in] op_type Type of the operation (read/write)
 */
static inline void qos_bw_bucket_suspend_task(qos_bucket_t *bucket,
					      void *caller_data, uint64_t rsize,
					      uint64_t timeout,
					      qos_op_type_t op_type)
{
	struct qos_op_cb_arg *qos_cb_args =
		alloc_qos_cb_args(caller_data, RATELIMITING_IO);
	timer_entry_t *new_timer_entry =
		create_timer_entry(timeout, get_qos_resume_func(op_type),
				   (void *)qos_cb_args);
	new_timer_entry->size = rsize;
	PTHREAD_MUTEX_lock(&bucket->lock);
	insert_timer_entry(&(bucket->io_waitlist_qos_bc), new_timer_entry);
	++bucket->num_ios_waiting;
	PTHREAD_MUTEX_unlock(&bucket->lock);
}

/**
 * Function to check bandwidth and delay based on current settings
 *	NON-Blocking IO
 * Note: function accounts the IO in there respctive bucket, but that
 *       doesnt mean that IO gets reschduled.
 *
 * @param [in] bucket Pointer to the bucket object
 * @param [in] bytes Size of the data in bytes
 * @param [in] caller_data Caller data passed to the callback function
 * @param [in] op_type Type of the operation (read/write)
 * @return QOS_TASK_SUSPENDED or NOT.
 */
static inline qos_status_t qos_process_bw(qos_bucket_t *bucket, uint64_t bytes,
					  void *caller_data,
					  qos_op_type_t op_type)
{
	uint64_t last_time = bucket->bw_ldct;
	uint64_t ctime = get_time_in_usec();
	/* Microseconds required to meet bandwidth */
	uint64_t required_time = (bytes * 1000000) / bucket->max_bw_allowed;

	bucket->data_consumed += bytes;
	/* Condition will be true only if continuous IO
	 * else its IDLE to start IO condition */
	if (ctime < (last_time + required_time)) {
		/* Microseconds elapsed since last call */
		bucket->bw_ldct = last_time + required_time;
		qos_bw_bucket_suspend_task(bucket, caller_data, bytes,
					   bucket->bw_ldct, op_type);

		/* TASK queued in QOS (recheduling of task),
		 * IO will be resumed once timer gets expired */
		return QOS_TASK_SUSPENDED;
	} else {
		if (ctime < (last_time + required_time + BW_EXPORT_FU_IO))
			bucket->bw_ldct += required_time;
		else
			bucket->bw_ldct = ctime + required_time;
#if ENABLE_CLUSTER_QOS
		/*
		 * If I/O is not suspended then we increment counter with bytes
		 * else, we will update counter when it resumes.
		 */
		if (cqos_initialized && (bucket->bw_subscribed == true))
			bucket->bw_consumed_intime += bytes;
#endif
		/* TASK not scheduled in QOS queue, but accounting is done for
		 * this IO, now this IO will take the default path */
		return QOS_TASK_NOT_SUSPENDED;
	}
}

/**
 * Function to control bandwidth for the QoS class.
 *
 * Note : Caller should hold qos_class->lock.
 *
 * @param [in] qos_class Pointer to the QoS client or export structure.
 * @param [in] request_size Size of the request for bandwidth.
 * @param [in] op_type Type of the operation (read/write).
 * @param [in] caller_data Pointer to the caller data.
 * @return QOS_TASK_SUSPENDED or not.
 */
static inline qos_status_t qos_control_bw(qos_class_t *qos_class,
					  uint64_t rsize, qos_op_type_t op_type,
					  void *caller_data)
{
	qos_bucket_t *bucket = qos_get_bw_bucket(qos_class, op_type);

	if (bucket == NULL)
		return QOS_TASK_NOT_SUSPENDED;

	return qos_process_bw(bucket, rsize, caller_data, op_type);
}

/**
 * Function to suspend the task for bandwidth control.
 *
 * Note : Caller should hold qos_class->lock.
 *
 * @param [in] qos_class Pointer to the QoS client or export structure.
 * @param [in] caller_data Pointer to the caller data.
 * @param [in] rsize Size of the request.
 * @param [in] timeout Task timeout value.
 * @param [in] op_type Type of the operation (read/write).
 */
static inline void qos_bw_suspend_task(qos_class_t *qos_class,
				       void *caller_data, uint64_t rsize,
				       uint64_t timeout, qos_op_type_t op_type)
{
	qos_bucket_t *bucket = qos_get_bw_bucket(qos_class, op_type);

	if (bucket == NULL)
		return;

	qos_bw_bucket_suspend_task(bucket, caller_data, rsize, timeout,
				   op_type);
}

/**
 * Function to calculate the time until token refresh based on current time
 * and bucket information.
 *
 * @param [in] qos_class Pointer to the QoS client or export structure.
 * @param [in] op_type Type of the operation (read/write).
 * @param [in] ctime Current time in microseconds.
 * @return Time until token refresh in milliseconds.
 */
uint64_t qos_get_time_to_tokenrefresh(qos_class_t *qos_class,
				      qos_op_type_t op_type, uint64_t ctime)
{
	qos_bucket_t *bucket = qos_get_token_bucket(qos_class, op_type);
	uint64_t ret =
		(((bucket->token_ldct + bucket->tokens_renew_time) - ctime) /
		 1000000);
	LogDebug(COMPONENT_QOS,
		 "LTC: %" PRIu64 " TRT: %" PRIu64 " CT: %" PRIu64
		 " TO: %" PRIu64,
		 bucket->token_ldct, bucket->tokens_renew_time, ctime, ret);

	return ret;
}

/**
 * Function to handle the token exhausted case and suspend the task.
 *
 * Note : Caller should hold qos_class->lock
 *
 * @param [in] ptr Pointer to the QoS client or export structure.
 * @param [in] caller_data Pointer to the caller data.
 * @param [in] data Pointer to compound_data_t containing the request data.
 * @param [in] op_type Type of the operation (read/write).
 */
static void qos_token_exausted_suspend_task(qos_class_t *qos_class,
					    void *caller_data,
					    compound_data_t *data,
					    qos_op_type_t op_type)
{
	qos_client_entry_t *client = NULL;
	timer_entry_t *new_timer_entry = NULL;
	unsigned int *num_ios_waiting = NULL;
	uint64_t timeout = 0;
	struct qos_op_cb_arg *qos_cb_args =
		alloc_qos_cb_args(caller_data, NON_RATELIMITING_IO);
	uint64_t ltime = get_time_in_usec();
	uint64_t time_to_refresh =
		qos_get_time_to_tokenrefresh(qos_class, op_type, ltime);

	timeout = get_time_future_useconds(
		ltime, MIN(TOKEN_NFS_ERR_DELAY_DEFAULT, time_to_refresh), 0, 0);

	client = get_and_insert_client_details(&(qos_class->client_entries),
					       data);
	num_ios_waiting = &(qos_class->num_ios_waiting);

	if (client->num_ios_waiting >= SUSPEND_SOCKET_IO_LIMIT &&
	    !client->epoll_disabled) {
		client->epoll_disabled = 1;
		QOS_PRINT_CLIENT("Suspending socket", op_ctx->client);
		svc_rqst_qos_suspend_socket(client->rq_xprt);
	} else if (client->num_ios_waiting >= SUSPEND_SOCKET_IO_LIMIT &&
		   client->epoll_disabled == 1) {
		timeout = get_time_future_useconds(ltime,
						   TOKEN_NFS_ERR_DELAY_IMMED, 0,
						   0);
	}

	new_timer_entry = create_timer_entry(timeout,
					     get_qos_resume_func(op_type),
					     (void *)qos_cb_args);
	insert_timer_entry(&(client->io_waitlist_qos), new_timer_entry);
	client->num_ios_waiting++;
	(*num_ios_waiting)++;

	LogDebug(COMPONENT_QOS,
		 "new_timer: %p io_w: %" PRIu32 " c_io_w: %" PRIu32
		 " CI: %p CT: %" PRIu64 " TO: %" PRIu64,
		 new_timer_entry, *num_ios_waiting, client->num_ios_waiting,
		 client->gsh_client, ltime, timeout);
}

/**
 * Function to check if the QoS token is available and consume it if possible
 * along with it takecare of BW calculation also
 *
 * @param [in] class_ptr Pointer to the QoS export object
 * @param [in] rsize Size of the I/O request in bytes
 * @param [in] op_type Type of the operation (read/write)
 * @param [in] caller_data Caller data passed to the callback function
 * @param [in] data Compound data associated with the I/O request
 * @return QOS_TASK_SUSPENDED or not.
 */

static inline qos_status_t qos_check_pe_pc(qos_class_t *class_ptr,
					   uint64_t rsize,
					   qos_op_type_t op_type,
					   void *caller_data,
					   compound_data_t *data)
{
	qos_class_t *qos_class = class_ptr;
	qos_status_t ret = QOS_TASK_NOT_SUSPENDED;

	PTHREAD_MUTEX_lock(&qos_class->lock);
	if (!qos_check_token_availability(qos_class, rsize, op_type)) {
		qos_token_exausted_suspend_task(qos_class, caller_data, data,
						op_type);
		ret = QOS_TASK_SUSPENDED;
		goto out;
	}
	qos_consume_token(qos_class, rsize, op_type);
	ret = qos_control_bw(qos_class, rsize, op_type, caller_data);

#ifdef USE_MONITORING
	if (qos_class->enabled_bw_metric) {
		qos_bucket_t *bucket = qos_get_bw_bucket(qos_class, op_type);

		monitoring__gauge_set(bucket->bw_metric_handler,
				      bucket->data_consumed);
	}
#endif
out:
	PTHREAD_MUTEX_unlock(&qos_class->lock);
	return ret;
}

/**
 * Function to check if the QoS token is available and consume it if possible
 * along with it takecare of suspending the task to client specific Queue for BW
 *
 * @param [in] class_ptr Pointer to the QoS export object
 * @param [in] request_size Size of the I/O request in bytes
 * @param [in] op_type Type of the operation (read/write)
 * @param [in] caller_data Caller data passed to the callback function
 * @param [in] data Compound data associated with the I/O request
 * @return QOS_TASK_SUSPENDED or not.
 */
static inline qos_status_t qos_check_pepc(void *class_ptr, uint64_t rsize,
					  qos_op_type_t op_type,
					  void *caller_data,
					  compound_data_t *data)
{
	qos_class_t *qos_class = class_ptr;
	qos_class_t *sub_qos_class =
		pepc_alloc_get_client(qos_class, op_ctx->client);

	int export_token_available =
		qos_check_token_availability(qos_class, rsize, op_type);
	int client_token_available =
		qos_check_token_availability(sub_qos_class, rsize, op_type);

	/*  here for accounting info take exportlevel lock,
	 *  which will aloow us to handling the runtime disablement
	 *  and enablement of QOS BW and Token control.
	 *  IO Consumer thread will work on bucket locks */
	PTHREAD_MUTEX_lock(&qos_class->lock);
	if (!export_token_available) {
		qos_token_exausted_suspend_task(qos_class, caller_data, data,
						op_type);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		return QOS_TASK_SUSPENDED;
	} else if (!client_token_available) {
		qos_token_exausted_suspend_task(sub_qos_class, caller_data,
						data, op_type);
		PTHREAD_MUTEX_unlock(&qos_class->lock);
		return QOS_TASK_SUSPENDED;
	} else {
		/*  consume the Tokens and schedule for ASYNC in the
		 *  client buckets, rescheuling of IO to export bucket
		 *  will happend later.
		 *  BW limits also decided later */
		qos_consume_token(qos_class, rsize, op_type);
		qos_consume_token(sub_qos_class, rsize, op_type);
		if (sub_qos_class->bw_enabled) {
			qos_bw_suspend_task(sub_qos_class, caller_data, rsize,
					    get_time_in_usec(), op_type);
			PTHREAD_MUTEX_unlock(&qos_class->lock);
			return QOS_TASK_SUSPENDED;
		} else {
			PTHREAD_MUTEX_unlock(&qos_class->lock);
			return QOS_TASK_NOT_SUSPENDED;
		}
	}
	PTHREAD_MUTEX_unlock(&qos_class->lock);
	return QOS_TASK_NOT_SUSPENDED;
}

/**
 * Function to process a export request for BW and Token.
 *
 * @param [in] rsize Size of the request.
 * @param [in] caller_data Pointer to the caller data.
 * @param [in] data Pointer to the compound_data_t structure containing
 *			the request data.
 * @param [in] op_type Type of the operation (read/write).
 * @return : QOS_TASK_NOT_SUSPENDED / QOS_TASK_SUSPENDED
 */
static inline qos_status_t qos_process_pe(uint64_t rsize, void *caller_data,
					  compound_data_t *data,
					  qos_op_type_t op_type, bool is_ds)
{
	if (!op_ctx->ctx_export->qos_class) {
		PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
		if (!op_ctx->ctx_export->qos_class)
			qos_perexport_insert(op_ctx->ctx_export, g_qos_config);

		PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);
	}
	if (is_ds && !op_ctx->ctx_export->qos_class->ds_enabled)
		return QOS_TASK_NOT_SUSPENDED;

#if ENABLE_CLUSTER_QOS
	if (cqos_initialized) {
		check_cqos_bw_subscription(op_ctx->ctx_export->qos_class,
					   op_type, QOS_EXPORT);
	}
#endif
	return qos_check_pe_pc(op_ctx->ctx_export->qos_class, rsize, op_type,
			       caller_data, data);
}

/**
 * Function to process a client request for BW and Token.
 *
 * @param [in] rsize Size of the request.
 * @param [in] caller_data Pointer to the caller data.
 * @param [in] data Pointer to the compound_data_t structure containing
 *			the request data.
 * @param [in] op_type Type of the operation (read/write).
 * @return : QOS_TASK_NOT_SUSPENDED/ QOS_TASK_SUSPENDED
 */
static inline qos_status_t qos_process_pc(uint64_t rsize, void *caller_data,
					  compound_data_t *data,
					  qos_op_type_t op_type, bool is_ds)
{
	if (!op_ctx->client->qos_class) {
		PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
		/* Since this is QOS_PC, pass the global QOS values */
		if (!op_ctx->client->qos_class)
			qos_perclientinsert(g_qos_config, op_ctx->client);

		PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);
	}
	if (is_ds && !op_ctx->client->qos_class->ds_enabled)
		return QOS_TASK_NOT_SUSPENDED;
#if ENABLE_CLUSTER_QOS
	if (cqos_initialized) {
		check_cqos_bw_subscription(op_ctx->client->qos_class,
					   op_type, QOS_CLIENT);
	}
#endif
	return qos_check_pe_pc(op_ctx->client->qos_class, rsize, op_type,
			       caller_data, data);
}

/**
 * Function to process a pepc export request for BW and Token.
 *
 * @param [in] rsize Size of the request.
 * @param [in] caller_data Pointer to the caller data.
 * @param [in] data Pointer to the compound_data_t structure containing
 *			the request data.
 * @param [in] op_type Type of the operation (read/write).
 * @return : QOS_TASK_NOT_SUSPENDED/ QOS_TASK_SUSPENDED
 */
static inline qos_status_t qos_process_pepc(uint64_t rsize, void *caller_data,
					    compound_data_t *data,
					    qos_op_type_t op_type, bool is_ds)
{
	qos_class_t *qos_class = op_ctx->ctx_export->qos_class;
	qos_status_t ret = QOS_TASK_NOT_SUSPENDED;

	if (!qos_class) {
		/* Execution reached here means QOS is enabled but
		 * export level qos block is not present,
		 * apply global conf to export
		 * There is possiblity of multiple IO's rushing
		 * into this function for this paticular export,
		 * so recheck before going for allocation*/
		PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
		qos_class = op_ctx->ctx_export->qos_class;
		if (!qos_class) {
			qos_perexport_insert(op_ctx->ctx_export, g_qos_config);
			qos_class = op_ctx->ctx_export->qos_class;
		}
		PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);
	}
	if (is_ds && !qos_class->ds_enabled)
		return ret;

#if ENABLE_CLUSTER_QOS
	if (cqos_initialized) {
		check_cqos_bw_subscription(qos_class, op_type, QOS_EXPORT);
	}
#endif
	/* Is QOS disabled for this particular export */
	if (qos_class->bw_enabled || qos_class->token_enabled) {
		/* func call reassures the client is the in export list */
		ret = qos_check_pepc(qos_class, rsize, op_type, caller_data,
				     data);
	}
#ifdef USE_MONITORING
	register_qos_metrics(qos_class);
#endif

	return ret;
}

/**
 * Function to process a QoS request.
 *
 * @param [in] rsize Size of the request.
 * @param [in] caller_data Pointer to the caller data.
 * @param [in] data Pointer to the compound_data_t structure containing
 *			the request data.
 * @param [in] op_type Type of the operation (read/write).
 * @param [in] is_ds Whether caller is doing a DS read/write.
 * @return : QOS_TASK_NOT_SUSPENDED/ QOS_TASK_SUSPENDED
 */
qos_status_t qos_process(uint64_t rsize, void *caller_data,
			 compound_data_t *data, qos_op_type_t op_type,
			 bool is_ds)
{
	qos_status_t ret = QOS_TASK_NOT_SUSPENDED;

	if (!g_qos_config->enable_qos)
		return ret;

	if (is_ds && !g_qos_config->enable_ds_control)
		return ret;

	switch (g_qos_config->qos_type) {
	case QOS_NOT_ENABLED:
		break;
	case QOS_PER_EXPORT_ENABLED:
		ret = qos_process_pe(rsize, caller_data, data, op_type, is_ds);
		break;
	case QOS_PER_CLIENT_ENABLED:
		ret = qos_process_pc(rsize, caller_data, data, op_type, is_ds);
		break;
	case QOS_PEREXPORT_PERCLIENT_ENABLED:
		ret = qos_process_pepc(rsize, caller_data, data, op_type,
				       is_ds);
		break;
	default:
		LogDebug(COMPONENT_QOS, " INVALID QOS_TYPE: %d",
			 g_qos_config->qos_type);
	}
	return ret;
}

/**
 * Function to refresh tokens based on current settings
 *
 * @param [in] class_entry Pointer to the class entry object
 * @return True if tokens were refreshed, False otherwise
 */
static inline bool refresh_bucket_token(qos_class_t *qos_class,
					qos_op_type_t op_type)
{
	uint64_t ltime = get_time_in_usec();
	qos_bucket_t *bucket = qos_get_token_bucket(qos_class, op_type);

	if (bucket == NULL)
		return false;

	/* This is the logic for limiting the io based on  */
	if ((bucket->tokens_consumed >= bucket->max_available_tokens) &&
	    (ltime > (bucket->token_ldct + bucket->tokens_renew_time))) {
		LogDebug(COMPONENT_QOS,
			 "CT: %" PRIu64 " LCT: %" PRIu64 " TRT: %" PRIu64
			 " cal: %" PRIu64,
			 ltime, bucket->token_ldct, bucket->tokens_renew_time,
			 (bucket->token_ldct + bucket->tokens_renew_time));
		bucket->tokens_consumed = 0;
#ifdef USE_MONITORING
		if (qos_class->enabled_tokens_metric) {
			monitoring__gauge_set(bucket->tokens_metric_handler, 0);
		}
#endif
		return true;
	} else {
		return false;
	}
}

/**
 * Function to refresh tokens for a QoS export
 *
 * @param [in] qos_class Pointer to the QoS export object
 */
static inline bool refresh_tokens(qos_class_t *qos_class)
{
	/* since the io waitlist queue are same for read and write ios,
	 * check for both and then return */
	return (refresh_bucket_token(qos_class, QOS_READ) ||
		refresh_bucket_token(qos_class, QOS_WRITE));
}

/**
 * Function to refresh client entries based on current token
 *
 * Note : Caller should hold qos_class->lock to which client list belongs.
 *
 * @param [in] clients Pointer to the head of the client entry list
 * @param [in] tokens_refreshed Flag indicating if tokens were refreshed
 * @param [in] qos_class Pointer to the QoS export or client object
 */
static inline void refresh_qos_client(struct glist_head *clients,
				      bool tokens_refreshed,
				      qos_class_t *qos_class)
{
	struct glist_head *glist, *glistn;
	qos_client_entry_t *client;
	unsigned int *counter2;
	bool epd = 0;
	SVCXPRT *rq_xprt = NULL;
	struct gsh_client *gsh_client = NULL;
#if ENABLE_CLUSTER_QOS
	uint64_t cqos_value = 0;
#endif

	glist_for_each_safe(glist, glistn, clients) {
		client = glist_entry(glist, qos_client_entry_t, token_ex_cl);
		counter2 = &(qos_class->num_ios_waiting);
		LogFullDebug(COMPONENT_QOS, "CI: %p CWIO's: %" PRIu32,
			     client->gsh_client, client->num_ios_waiting);

		if (tokens_refreshed) {
			release_wait_ios(&client->io_waitlist_qos,
					 &client->num_ios_waiting, counter2);
		} else if (client->num_ios_waiting) {
#if ENABLE_CLUSTER_QOS
			execute_qos_expired_timers(&client->io_waitlist_qos,
						   &client->num_ios_waiting,
						   counter2, &cqos_value);
#else
			execute_qos_expired_timers(&client->io_waitlist_qos,
						   &client->num_ios_waiting,
						   counter2);
#endif
		}

		if (!client->num_ios_waiting) {
			rq_xprt = client->rq_xprt;
			epd = client->epoll_disabled;
			gsh_client = client->gsh_client;
			QOS_PRINT_CLIENT("resume socket ", gsh_client);
			LogDebug(COMPONENT_QOS, "Xprt: %p epd: %d xprt: %p",
				 client->rq_xprt, client->epoll_disabled,
				 rq_xprt);
			glist_del(&client->token_ex_cl);
			gsh_free(client, MEM_COMP_QOS);
			if (epd) {
				svc_rqst_qos_resume_socket(rq_xprt);
				epd = 0;
			}
			rq_xprt = NULL;
		}
	}
}

/**
 * Function to refresh tokens export/client
 *
 * @param [in] class Pointer to the QoS export object
 */
static inline void refresh_qos_token_by_class(qos_class_t *qos_class)
{
	bool tokens_refreshed = 0;

	tokens_refreshed = refresh_tokens(qos_class);
	QOS_PRINT_EXPORT(__func__, qos_class->gsh_export);
	LogDebug(COMPONENT_QOS, " TR: %d WIO's: %" PRIu32, tokens_refreshed,
		 qos_class->num_ios_waiting);
	PTHREAD_MUTEX_lock(&(qos_class->lock));
	refresh_qos_client(&qos_class->client_entries, tokens_refreshed,
			   qos_class);
	PTHREAD_MUTEX_unlock(&(qos_class->lock));
}

/**
 * Callback function to control tokens in pepc conf.
 *
 * @param [in] gsh_export ptr to gsh_export structure representing the export.
 * @param [in] state Pointer to the state data (not used).
 * @return True  always true so to continue the iterations.
 */
bool pepc_token_control_iter(struct gsh_export *gsh_export, void *state)
{
	qos_class_t *qos_class = gsh_export->qos_class;
	struct glist_head *glist;
	qos_class_t *sub_qos_class;

	if (qos_class != NULL && qos_class->token_enabled) {
		QOS_PRINT_EXPORT(__func__, gsh_export);
		refresh_qos_token_by_class(qos_class);

		/* Check all clients in qos_class for token exhaustion
		 * and replenishment */
		glist_for_each(glist, &qos_class->clients) {
			sub_qos_class =
				glist_entry(glist, qos_class_t, clients);
			refresh_qos_token_by_class(sub_qos_class);
		}
	}
	/* Continue iteration */
	return true;
}

/**
 * Callback function to control tokens in PS conf.
 *
 * @param [in] export Pointer to gsh_export structure representing the export.
 * @param [in] state Pointer to the state data (not used).
 * @return True  always true so to continue the iterations.
 */
bool ps_token_control_iter(struct gsh_export *gsh_export, void *state)
{
	qos_class_t *qos_class = gsh_export->qos_class;

	if (qos_class != NULL && qos_class->token_enabled) {
		QOS_PRINT_EXPORT(__func__, gsh_export);
		refresh_qos_token_by_class(qos_class);
	}
	/* Continue iteration */
	return true;
}

/**
 * Callback function to control tokens in PC conf.
 *
 * @param [in] cl Pointer to gsh_client structure representing the client.
 * @param [in] state Pointer to the state data (not used).
 * @return True  always true so to continue the iterations.
 */
bool pc_token_control_iter(struct gsh_client *gsh_client, void *state)
{
	qos_class_t *qos_class = gsh_client->qos_class;

	if (qos_class != NULL && qos_class->token_enabled) {
		QOS_PRINT_CLIENT(__func__, gsh_client);
		refresh_qos_token_by_class(qos_class);
	}
	/* Continue iteration */
	return true;
}

/**
 * Function to refresh tokens based on the QoS configuration.
 *
 * @return None
 */
static inline void refresh_qos_token(void)
{
	/*  Token refershing function calls here
	 *  export based refresing
	 *  Client based refresing
	 *  Perexport-PerClient based refresing
	 *  Group Based
	 *  Directory level */
	int op_type = 0;

	switch (g_qos_config->qos_type) {
	case QOS_NOT_ENABLED:
		LogDebug(COMPONENT_QOS, "QOS not enabled : %d",
			 g_qos_config->qos_type);
		break;
	case QOS_PER_EXPORT_ENABLED:
		foreach_gsh_export(ps_token_control_iter, false, &op_type)
			;
		break;
	case QOS_PER_CLIENT_ENABLED:
		foreach_gsh_client(pc_token_control_iter, &op_type)
			;
		break;
	case QOS_PEREXPORT_PERCLIENT_ENABLED:
		foreach_gsh_export(pepc_token_control_iter, false, &op_type)
			;
		break;
	default:
		LogDebug(COMPONENT_QOS, " Something really wrong: %d",
			 g_qos_config->qos_type);
		break;
	}
}

/**
 * Function to resume bandwidth I/Os form a bucket
 *
 * Note : Caller should hold bucket->lock.
 *
 * resumes the expired IO from the bucket.
 * @param [in] bucket Pointer to the bucket object
 */
static inline void resume_bw_bucket_io(qos_bucket_t *bucket)
{
	uint32_t dummy_counter = UINT32_MAX;
#if ENABLE_CLUSTER_QOS
	execute_qos_expired_timers(&(bucket->io_waitlist_qos_bc),
				   &(bucket->num_ios_waiting), &dummy_counter,
				   &(bucket->bw_consumed_intime));
#else
	execute_qos_expired_timers(&(bucket->io_waitlist_qos_bc),
				   &(bucket->num_ios_waiting), &dummy_counter);
#endif
}

/**
 * Function to resume bandwidth I/Os for a export level QoS configuration
 *
 * @param [in] export Pointer to the QoS export object
 * @param [in] op_type Type of the operation (read/write)
 */
static inline void resume_bw_io(qos_class_t *qos_class, qos_op_type_t op_type)
{
	qos_bucket_t *bucket = qos_get_bw_bucket(qos_class, op_type);

	if (bucket == NULL)
		return;

	PTHREAD_MUTEX_lock(&bucket->lock);
	resume_bw_bucket_io(bucket);
	PTHREAD_MUTEX_unlock(&bucket->lock);
}

/**
 * Function to resume bandwidth I/Os for a pepc QoS configuration
 *
 * @param [in] export Pointer to the QoS export object
 * @param [in] op_type Type of the operation (read/write)
 */
static inline void resume_bw_io_pepc(qos_class_t *qos_class,
				     qos_op_type_t op_type)
{
	qos_bucket_t *bucket = qos_get_bw_bucket(qos_class, op_type);
	uint64_t ctime = get_time_in_usec();
	int check_delay =
		((op_type == QOS_READ) ? BW_EXPORT_FU_IO : BW_DELAY_USEC);
	struct glist_head *glist, *glistn;
	timer_entry_t *io_entry;
	uint64_t rtime;
	uint64_t futime;

	if (bucket == NULL || ((ctime + check_delay) < bucket->bw_ldct))
		return;

	PTHREAD_MUTEX_lock(&(bucket->lock));
	glist_for_each_safe(glist, glistn, &bucket->io_waitlist_qos_bc) {
		io_entry = glist_entry(glist, timer_entry_t, timer_list);
		glist_del(&io_entry->timer_list);
		rtime = (io_entry->size * 1000000) / bucket->max_bw_allowed;
		futime = bucket->bw_ldct + rtime + BW_EXPORT_FU_IO;

		if (futime > ctime)
			bucket->bw_ldct = bucket->bw_ldct + rtime;
		else
			bucket->bw_ldct = ctime;

		bucket->data_consumed += io_entry->size;
		--bucket->num_ios_waiting;

#if ENABLE_CLUSTER_QOS
		if (cqos_initialized)
			bucket->bw_consumed_intime += io_entry->size;
#endif
		resume_timer_entry(io_entry);
		if (bucket->bw_ldct >= (ctime + check_delay))
			break;
	}

	PTHREAD_MUTEX_unlock(&(bucket->lock));
}

/**
 * Function to reschedule I/O operations from client bucket to export bucket.
 *
 * Note : Caller should hold exports bucket->lock and clients bucket->lock.
 *
 * @param [in] bucket Pointer to qos_bucket_t representing export bucket.
 * @param [in] sub_bucket Pointer to qos_bucket_t representing client bucket.
 * @param [in] ctime Current time in microseconds.
 */
static inline void pepc_rescedule_io_to_export(qos_bucket_t *bucket,
					       qos_bucket_t *sub_bucket,
					       uint64_t ctime)
{
	uint64_t clienttime = ctime;
	timer_entry_t *io_entry = NULL;
	uint64_t rtime = 0;
	uint64_t futime = 0;
	struct glist_head *glist, *glistn;
	struct glist_head *io_list = &bucket->io_waitlist_qos_bc;

	glist_for_each_safe(glist, glistn, &sub_bucket->io_waitlist_qos_bc) {
		io_entry = glist_entry(glist, timer_entry_t, timer_list);
		rtime = (io_entry->size * 1000000) / sub_bucket->max_bw_allowed;

		sub_bucket->data_consumed += io_entry->size;

		/* Check ensures we don't exceed the Client bucket Limit */
		if (clienttime + BW_DELAY_USEC >= sub_bucket->bw_ldct) {
			glist_del(&io_entry->timer_list);
			futime = sub_bucket->bw_ldct + rtime + BW_EXPORT_FU_IO;
			/* Ensure full BW is available for this client
			 * else indicate export limit has been reached
			 * so client is throttling */
			if (futime > ctime) {
				sub_bucket->bw_ldct =
					sub_bucket->bw_ldct + rtime;
				io_entry->expiry = sub_bucket->bw_ldct;
			} else {
				sub_bucket->bw_ldct = ctime;
				io_entry->expiry = ctime;
			}

			/* here making io_entry as io_list i.e head
			 * so that we dont have to iterate again from start */
			io_list = insert_timer_entry_sorted(io_list, io_entry);
			++bucket->num_ios_waiting;
			--sub_bucket->num_ios_waiting;
			/* Check ensures scheduling future IO till
			 * (ctime + BW_CLIENT_FU_IO) time */
			if (sub_bucket->bw_ldct > (ctime + BW_CLIENT_FU_IO)) {
				clienttime = sub_bucket->bw_ldct;
				break;
			}
		}
	}
}

/**
 * Function to reschedule bandwidth I/Os for a export
 *
 * @param [in] export Pointer to the QoS export object
 * @param [in] op_type Type of the operation (read/write).
 */
static inline void pepc_reschedule_bw_io(qos_class_t *qos_class,
					 qos_op_type_t op_type)
{
	uint64_t ctime = get_time_in_usec();
	qos_class_t *sub_qos_class;
	qos_bucket_t *bucket = qos_get_bw_bucket(qos_class, op_type);
	qos_bucket_t *sub_bucket = NULL;
	struct glist_head *glist;

	if (bucket == NULL || (ctime < bucket->bw_ldct))
		return;

	PTHREAD_MUTEX_lock(&bucket->lock);
	glist_for_each(glist, &qos_class->clients) {
		sub_qos_class = glist_entry(glist, qos_class_t, clients);
		sub_bucket = qos_get_bw_bucket(sub_qos_class, op_type);

		if (sub_bucket == NULL || (ctime <= sub_bucket->bw_ldct))
			continue;

		if (!glist_empty(&sub_bucket->io_waitlist_qos_bc)) {
			PTHREAD_MUTEX_lock(&sub_bucket->lock);
			pepc_rescedule_io_to_export(bucket, sub_bucket, ctime);
			PTHREAD_MUTEX_unlock(&sub_bucket->lock);
		}
#ifdef USE_MONITORING
		if (sub_qos_class->enabled_bw_metric)
			monitoring__gauge_set(sub_bucket->bw_metric_handler,
					      sub_bucket->data_consumed);
#endif
	}
	PTHREAD_MUTEX_unlock(&bucket->lock);
}

/**
 * @brief suspend IOPS task for a bucket.
 *
 * This function suspends an iops by adding it to the bucket's I/O wait list
 * and setting a timer to handle the task after a specified timeout.
 * It allocates memory for the QoS operation callback arguments,
 * creates a new timer entry
 * and inserts it into the appropriate waitlist.
 *
 * @param [in] bucket Pointer to the qos_bucket_t representing the bucket
 * @param [in] caller_data Caller-specific data to be passed with the IOPS task
 * @param [in] rsize Size of the IOPS task
 * @param [in] timeout in microseconds after which IOPS task should be executed.
 */
void qos_iops_suspend_task(qos_bucket_t *bucket, void *caller_data,
			   uint64_t rsize, uint64_t timeout)
{
	struct qos_op_cb_arg *qos_cb_args =
		alloc_qos_cb_args(caller_data, RATELIMITING_IO);
	timer_entry_t *new_timer_entry =
		create_timer_entry(timeout, nfs4_qos_compound_cb,
				   (void *)qos_cb_args);

	new_timer_entry->size = rsize;
	PTHREAD_MUTEX_lock(&bucket->lock);
	bucket->iops_consumed += rsize;
	insert_timer_entry(&(bucket->io_waitlist_qos_iops), new_timer_entry);
	++bucket->num_ios_waiting;
	PTHREAD_MUTEX_unlock(&bucket->lock);
}

/**
 * Function to check and take decision for IOPS.
 *
 * @param [in] data Compound data associated with the I/O request
 *
 * @return QOS_TASK_SUSPENDED or NOT.
 */
int qos_iops_check(compound_data_t *data, qos_bucket_t *bucket)
{
	uint64_t last_time = bucket->iops_ldct;
	uint64_t ctime = get_time_in_usec();
	uint64_t timeout = 0;
	/*max compound can be 100 only*/
	uint8_t num_ops = data->argarray_len;
	uint64_t rtime = (num_ops * (USEC_IN_SEC / bucket->max_iops_allowed));

	/*Set the compound ops accounted bit*/
	data->qos_flags |= IS_QOS_IOPS_ACCOUNTED;

	/* Accounting for the previous 5 Milliseconds also,
	 * so that algo doesn't missed the limit */
	if (ctime < (last_time + IOPS_DELAY_USEC + rtime)) {
		bucket->iops_ldct = bucket->iops_ldct + rtime;
	} else {
		bucket->iops_ldct = ctime + rtime;
	}
	timeout = bucket->iops_ldct;

	/* The whole compound has been Accounted for */
	if (timeout <= ctime) {
		bucket->iops_consumed += num_ops;
#if ENABLE_CLUSTER_QOS
	/*
	 * If I/O is not suspended then we increment counter with bytes
	 * else, we will update counter when it resumes.
	 */
	if (cqos_initialized && (bucket->iops_subscribed == true))
		bucket->iops_consumed_intime += num_ops;
#endif
		return QOS_TASK_NOT_SUSPENDED;
	} else {
		qos_iops_suspend_task(bucket, data, num_ops, timeout);
		return QOS_TASK_SUSPENDED;
	}
}

/**
 * Function to process IOPS for a export level QoS configuration
 *
 * @param [in] data Compound data associated with the I/O request
 *
 * @return QOS_TASK_SUSPENDED or NOT.
 */
qos_status_t qos_process_iops_pe(compound_data_t *data, uint32_t op_type)
{
	qos_class_t *qos_class = op_ctx->ctx_export->qos_class;
	qos_bucket_t *bucket = NULL;
	qos_status_t ret = QOS_TASK_NOT_SUSPENDED;

	if (isFullDebug(COMPONENT_QOS))
		QOS_PRINT_EXPORT(__func__, op_ctx->ctx_export);

	if (!qos_class) {
		PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
		if (!op_ctx->ctx_export->qos_class)
			qos_perexport_insert(op_ctx->ctx_export, g_qos_config);

		qos_class = op_ctx->ctx_export->qos_class;
		PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);
	}

	if (!qos_class->iops_enabled)
		return QOS_TASK_NOT_SUSPENDED;

	bucket = qos_get_iops_bucket(qos_class, op_type);
	if (bucket == NULL) {
		ret = QOS_TASK_NOT_SUSPENDED;
		goto out;
	}
#if ENABLE_CLUSTER_QOS
	if (cqos_initialized)
		check_cqos_iops_subscription(qos_class, op_type, QOS_EXPORT);
#endif
	PTHREAD_MUTEX_lock(&qos_class->lock);
	ret = qos_iops_check(data, bucket);
#ifdef USE_MONITORING
	if (qos_class->enabled_iops_metric) {
		monitoring__gauge_set(bucket->iops_metric_handler,
				      bucket->iops_consumed);
	}
#endif
	PTHREAD_MUTEX_unlock(&qos_class->lock);
out:
	return ret;
}

/**
 * Function to process IOPS for a client level QoS configuration
 *
 * @param [in] data Compound data associated with the I/O request.
 *
 * @return QOS_TASK_SUSPENDED or NOT.
 */
qos_status_t qos_process_iops_pc(compound_data_t *data, uint32_t op_type)
{
	qos_class_t *qos_class = op_ctx->client->qos_class;
	qos_bucket_t *bucket = NULL;
	qos_status_t ret = QOS_TASK_NOT_SUSPENDED;

	if (isFullDebug(COMPONENT_QOS))
		QOS_PRINT_CLIENT(__func__, op_ctx->client);

	if (!qos_class) {
		PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
		/* Since this is QOS_PC, pass the global QOS values */
		if (!op_ctx->client->qos_class)
			qos_perclientinsert(g_qos_config, op_ctx->client);

		qos_class = op_ctx->client->qos_class;
		PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);
	}

	if (!qos_class->iops_enabled)
		return QOS_TASK_NOT_SUSPENDED;

	bucket = qos_get_iops_bucket(qos_class, op_type);
	if (bucket == NULL) {
		ret = QOS_TASK_NOT_SUSPENDED;
		goto out;
	}
#if ENABLE_CLUSTER_QOS
	if (cqos_initialized)
		check_cqos_iops_subscription(qos_class, op_type, QOS_CLIENT);
#endif
	PTHREAD_MUTEX_lock(&qos_class->lock);
	ret = qos_iops_check(data, bucket);
#ifdef USE_MONITORING
	if (qos_class->enabled_iops_metric) {
		monitoring__gauge_set(bucket->iops_metric_handler,
				      bucket->iops_consumed);
	}
#endif

	PTHREAD_MUTEX_unlock(&qos_class->lock);
out:
	return ret;
}

/**
 * Function to process IOPS for pepc QoS configuration
 *
 * @param [in] data Compound data associated with the I/O request
 *
 * @return QOS_TASK_SUSPENDED or NOT.
 */
qos_status_t qos_process_iops_pepc(compound_data_t *data, uint32_t op_type)
{
	qos_class_t *qos_class = op_ctx->ctx_export->qos_class;
	struct gsh_client *gsh_client = op_ctx->client;

	if (isFullDebug(COMPONENT_QOS))
		QOS_PRINT_EXPORT(__func__, op_ctx->ctx_export);

	if (!qos_class) {
		/*  Execution reached here means QOS is enabled but,
		 *  QOS block is not populated for this export
		 *  so apply the global values to the export values
		 *  and mark the qos_enabled to false, we need this in case of
		 *  runtime enablement of QOS*/
		PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
		/* On runtime enablement of QOS, or due to only global config
		 * present, there is possiblity of multiple IO's rushing
		 * into this function for this paticular export,
		 * so recheck before going for allocation*/
		qos_class = op_ctx->ctx_export->qos_class;
		if (!qos_class) {
			qos_perexport_insert(op_ctx->ctx_export, g_qos_config);
			qos_class = op_ctx->ctx_export->qos_class;
		}
		PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);
	}

	/* Is QOS iops disabled for this particular export */
	if (qos_class->iops_enabled) {
		qos_class_t *sub_qos_class = NULL;
		qos_bucket_t *bucket = NULL;

		sub_qos_class = pepc_alloc_get_client(qos_class, gsh_client);
		bucket = qos_get_iops_bucket(sub_qos_class, op_type);

		if (bucket == NULL)
			goto out;
#if ENABLE_CLUSTER_QOS
		if (cqos_initialized) {
			check_cqos_iops_subscription(qos_class, op_type,
						     QOS_EXPORT);
		}
#endif
		data->qos_flags |= IS_QOS_IOPS_ACCOUNTED;
		qos_iops_suspend_task(bucket, data, data->argarray_len,
				      get_time_in_usec());
#ifdef USE_MONITORING
		if (sub_qos_class->enabled_iops_metric) {
			monitoring__gauge_set(bucket->iops_metric_handler,
					      bucket->iops_consumed);
		}
#endif
		return QOS_TASK_SUSPENDED;
	}

out:
	return QOS_TASK_NOT_SUSPENDED;
}

/**
 * Function to process IOPS (Hook)
 *
 * @param [in] data Compound data associated with the I/O request
 * @return QOS_TASK_SUSPENDED or NOT.
 */
qos_status_t qos_process_iops(compound_data_t *data)
{
	qos_status_t ret = QOS_TASK_NOT_SUSPENDED;
	uint32_t op_type = QOS_WRITE;

	/* Condition make sures to apply the IOPS on only valid exports,
	 * QOS engine should entertain accounting only on valid exports */
	if (!g_qos_config->enable_qos || !g_qos_config->enable_iops_control ||
	    (!op_ctx->ctx_export))
		return ret;

	switch (g_qos_config->qos_type) {
	case QOS_NOT_ENABLED:
		break;
	case QOS_PER_EXPORT_ENABLED:
		ret = qos_process_iops_pe(data, op_type);
		break;
	case QOS_PER_CLIENT_ENABLED:
		ret = qos_process_iops_pc(data, op_type);
		break;
	case QOS_PEREXPORT_PERCLIENT_ENABLED:
		ret = qos_process_iops_pepc(data, op_type);
		break;
	default:
		LogDebug(COMPONENT_QOS, " INVALID QOS_TYPE: %d",
			 g_qos_config->qos_type);
	}

	LogFullDebug(COMPONENT_QOS,
		     "oppos: %d opcode: %d qosflags: %d isaccouted: %d ret: %d",
		     data->oppos, data->opcode, data->qos_flags,
		     ((data->qos_flags & IS_QOS_IOPS_ACCOUNTED) ? 1 : 0), ret);
	return ret;
}

/**
 * Function to resume expired IOPS form bucket
 *
 * Note: Caller should hold export and client bucket->lock.
 *
 * @param [in] bucket Pointer to the bucket object
 */
static inline void resume_iops_bucket(qos_bucket_t *bucket)
{
	uint32_t dummy_counter = UINT32_MAX;

#if ENABLE_CLUSTER_QOS
	execute_qos_expired_timers(&(bucket->io_waitlist_qos_iops),
				   &(bucket->num_ios_waiting), &dummy_counter,
				   &(bucket->iops_consumed_intime));
#else
	execute_qos_expired_timers(&(bucket->io_waitlist_qos_iops),
				   &(bucket->num_ios_waiting), &dummy_counter);
#endif
}

/**
 * Function to resume IOPS io in Perexport Conf
 *
 * @param [in] export Pointer to the QoS export object
 * @param [in] op_type Type of the operation (read/write)
 */
static inline void resume_ops_io(qos_class_t *qos_class, qos_op_type_t op_type)
{
	qos_bucket_t *bucket = qos_get_iops_bucket(qos_class, op_type);

	if (bucket == NULL)
		return;

	PTHREAD_MUTEX_lock(&bucket->lock);
	resume_iops_bucket(bucket);
	PTHREAD_MUTEX_unlock(&bucket->lock);
}

/**
 * Function to reschedule IOPS from a client bucket to the export bucket
 *
 * Note: Caller should hold export and client bucket->lock.
 *
 * @param [in] bucket Pointer to the export bucket object
 * @param [in] sub_bucket Pointer to the client bucket object
 * @param [in] ctime Current time in microseconds
 */
static inline void pepc_rescedule_iops_to_export(qos_bucket_t *bucket,
						 qos_bucket_t *sub_bucket,
						 uint64_t ctime)
{
	uint64_t clienttime = ctime;
	timer_entry_t *io_entry = NULL;
	uint64_t rtime = 0;
	uint64_t futime = 0;
	struct glist_head *glist, *glistn;
	struct glist_head *io_list = &bucket->io_waitlist_qos_iops;
	uint64_t time_ops = (USEC_IN_SEC / sub_bucket->max_iops_allowed);

	glist_for_each_safe(glist, glistn, &sub_bucket->io_waitlist_qos_iops) {
		io_entry = glist_entry(glist, timer_entry_t, timer_list);
		rtime = (io_entry->size * time_ops);

		/* This check ensures we dont exceed the Client bucket Limit */
		if (clienttime + IOPS_DELAY_USEC >= sub_bucket->iops_ldct) {
			glist_del(&io_entry->timer_list);
			/* below if ensures full IOPS is available for this
			 * client else indicate export limit has been reached
			 * so client is trottling */
			futime = sub_bucket->iops_ldct + rtime +
				 IOPS_EXPORT_FU_IO;
			if (futime > ctime) {
				sub_bucket->iops_ldct =
					sub_bucket->iops_ldct + rtime;
				io_entry->expiry = sub_bucket->iops_ldct;
			} else {
				sub_bucket->iops_ldct = ctime;
				io_entry->expiry = ctime;
			}

			/* here making io_entry as io_list i.e head
			 * so that we dont have to iterate again from start */
			io_list = insert_timer_entry_sorted(io_list, io_entry);
			++bucket->num_ios_waiting;
			--sub_bucket->num_ios_waiting;

			/*  Check ensures scheduling future IO till
			 *  (ctime + IOPS_CLIENT_FU_IO) time */
			if (sub_bucket->iops_ldct > ctime + IOPS_CLIENT_FU_IO) {
				clienttime = sub_bucket->iops_ldct;
				break;
			}
		}
	}
}

/**
 * Function to reschedule IOPS io from client bucket to export bucket
 * Checks for client bucket limit.
 *
 * @param [in] export Pointer to the QoS export object
 * @param [in] op_type Type of the operation (read/write).
 */
static inline void pepc_reschedule_iops(qos_class_t *qos_class,
					qos_op_type_t op_type)
{
	uint64_t ctime = get_time_in_usec();
	qos_class_t *sub_qos_class;
	qos_bucket_t *bucket = qos_get_iops_bucket(qos_class, op_type);
	qos_bucket_t *sub_bucket = NULL;
	struct glist_head *glist;

	if (bucket == NULL || ctime < bucket->iops_ldct)
		return;

	PTHREAD_MUTEX_lock(&bucket->lock);
	glist_for_each(glist, &qos_class->clients) {
		sub_qos_class = glist_entry(glist, qos_class_t, clients);
		sub_bucket = qos_get_iops_bucket(sub_qos_class, op_type);

		if (sub_bucket == NULL || ctime <= sub_bucket->iops_ldct)
			continue;

		if (!glist_empty(&sub_bucket->io_waitlist_qos_iops)) {
			PTHREAD_MUTEX_lock(&sub_bucket->lock);
			pepc_rescedule_iops_to_export(bucket, sub_bucket,
						      ctime);
			PTHREAD_MUTEX_unlock(&sub_bucket->lock);
		}
#ifdef USE_MONITORING
		if (sub_qos_class->enabled_iops_metric) {
			monitoring__gauge_set(sub_bucket->iops_metric_handler,
					      sub_bucket->iops_consumed);
		}
#endif
	}
	PTHREAD_MUTEX_unlock(&bucket->lock);
}

/**
 * Function to resume the IOPS waiting in export based on limit set in conf
 *
 * @param [in] export Pointer to the QoS export object
 * @param [in] op_type Type of the operation (read/write).
 */
static inline void resume_iops_pepc(qos_class_t *qos_class,
				    qos_op_type_t op_type)
{
	qos_bucket_t *bucket = qos_get_iops_bucket(qos_class, op_type);
	uint64_t ctime = get_time_in_usec();
	int check_delay =
		((op_type == QOS_READ) ? IOPS_EXPORT_FU_IO : IOPS_DELAY_USEC);
	struct glist_head *glist, *glistn;
	timer_entry_t *io_entry;
	uint64_t rtime;
	uint64_t futime;
	uint64_t time_ops;

	if (bucket == NULL || ((ctime + check_delay) < bucket->iops_ldct))
		return;

	time_ops = (USEC_IN_SEC / bucket->max_iops_allowed);
	PTHREAD_MUTEX_lock(&(bucket->lock));
	glist_for_each_safe(glist, glistn, &bucket->io_waitlist_qos_iops) {
		io_entry = glist_entry(glist, timer_entry_t, timer_list);
		glist_del(&io_entry->timer_list);

		rtime = io_entry->size * time_ops;
		futime = bucket->iops_ldct + rtime + IOPS_EXPORT_FU_IO;

		if (futime > ctime)
			/* Resuming IOPS from last IO completion */
			bucket->iops_ldct = bucket->iops_ldct + rtime;
		else
			/* Resuming IOPS from IDLE */
			bucket->iops_ldct = ctime;

		bucket->iops_consumed += io_entry->size;
		--bucket->num_ios_waiting;
#if ENABLE_CLUSTER_QOS
		if (cqos_initialized)
			bucket->iops_consumed_intime += io_entry->size;
#endif
		resume_timer_entry(io_entry);

		if (bucket->iops_ldct >= (ctime + check_delay))
			break;
	}

	PTHREAD_MUTEX_unlock(&(bucket->lock));
}

/**
 * @brief Callback function for IO control of QoS export.
 *	Function responsible to initate the IO resume for export.
 *
 * @param [in] export Pointer to gsh_export structure.
 * @param [in] state Void pointer containing op_type.
 *
 * @return true to continue iteration
 */
bool ps_io_control_iter(struct gsh_export *gsh_export, void *state)
{
	qos_class_t *qos_class = gsh_export->qos_class;

	if (qos_class != NULL && qos_class->bw_enabled)
		resume_bw_io(qos_class, *(unsigned int *)state);

	if (qos_class != NULL && qos_class->iops_enabled)
		resume_ops_io(qos_class, *(unsigned int *)state);

	/* Continue iteration */
	return true;
}

/**
 * @brief Callback function for IO control of Qos Client.
 *	Function responsible to initate the IO resume for client
 *
 * @param [in] cl Pointer to gsh_client structure.
 * @param [in] state Void pointer containing op_type.
 *
 * @return true to continue iteration.
 */
bool pc_io_control_iter(struct gsh_client *gsh_client, void *state)
{
	qos_class_t *qos_class = gsh_client->qos_class;

	if (qos_class != NULL && qos_class->bw_enabled)
		resume_bw_io(qos_class, *(unsigned int *)state);

	if (qos_class != NULL && qos_class->iops_enabled)
		resume_ops_io(qos_class, *(unsigned int *)state);

	/* Continue iteration */
	return true;
}

/**
 * @brief Callback function for IO control of pepc.
 *	Function responsible to initate the IO resume at export
 *	and rescheduling IO from Client bucket to export bucket.
 *
 * @param [in] export Pointer to gsh_export structure representing the export
 * @param [in] state Void pointer containing op_type.
 *
 * @return true to continue iteration.
 */
bool pepc_io_control_iter(struct gsh_export *gsh_export, void *state)
{
	qos_class_t *qos_class = gsh_export->qos_class;

	if (qos_class != NULL && qos_class->bw_enabled) {
		resume_bw_io_pepc(qos_class, *(unsigned int *)state);
		pepc_reschedule_bw_io(qos_class, *(unsigned int *)state);
	}

	/*  Currently OPS in write bucket only, so ignore in READ thread*/
	if (qos_class != NULL && qos_class->iops_enabled &&
	    *(unsigned int *)state == QOS_WRITE) {
		resume_iops_pepc(qos_class, *(unsigned int *)state);
		pepc_reschedule_iops(qos_class, *(unsigned int *)state);
	}
	/* Continue iteration */
	return true;
}

/**
 * Function to resume bandwidth ios and OPS ios based on operation type
 *
 * @param [in] op_type Type of the operation (read/write)
 */
static inline void resume_io(qos_op_type_t op_type)
{
	switch (g_qos_config->qos_type) {
	case QOS_NOT_ENABLED:
		LogDebug(COMPONENT_QOS, "QOS not enabled: %d",
			 g_qos_config->qos_type);
		break;
	case QOS_PER_EXPORT_ENABLED:
		foreach_gsh_export(ps_io_control_iter, false, &op_type)
			;
		break;
	case QOS_PER_CLIENT_ENABLED:
		foreach_gsh_client(pc_io_control_iter, &op_type)
			;
		break;
	case QOS_PEREXPORT_PERCLIENT_ENABLED:
		foreach_gsh_export(pepc_io_control_iter, false, &op_type)
			;
		break;
	default:
		LogDebug(COMPONENT_QOS, " Something really wrong: %d",
			 g_qos_config->qos_type);
		break;
	}
}

/**
 * @brief Main function for QoS thread.
 *
 * This function is the entry point for each QoS worker thread.
 * It continuously runs, 2 threads are there which takes care of,
 * resuming bandwidth I/O operations,
 * resuming IOPS operations,
 * and handling token refresh tasks if enabled in the configuration.
 *
 * @param [in] arg Pointer to details of op_type (QOS_READ or QOS_WRITE).
 *
 * @return NULL on successful completion
 */
static void *qos_thread_func(void *arg)
{
	pthread_t current_thread_id = pthread_self();
	int counter = 0;
	qos_op_type_t op_type = (intptr_t)arg;
	const char *thread_name =
		((op_type == QOS_WRITE) ? "qos_write" : "qos_read");

	SetNameFunction(thread_name);
	LogDebug(COMPONENT_QOS, "Thread Started ..");
	while (atomic_fetch_uint32_t(&qos_bits) & QOS_THREAD_RUNNABLE) {
		resume_io(op_type);
		/* Currently combined accounting is enabled only in writbucket,
		 * once pnfs and nconnect gets properly enabled
		 * need to revisit this condition: "op_type == QOS_WRITE"
		 */
		if (g_qos_config->enable_tokens &&
		    counter >= TOKEN_REFRESH_DELAY && op_type == QOS_WRITE) {
			LogDebug(COMPONENT_QOS, "thread.id: %" PRIu64,
				 current_thread_id);
			refresh_qos_token();
			counter = 0;
		}
		/* Periodic Wakeup */
		usleep(BW_DELAY_USEC / 2);
		counter++;
	}
	return NULL;
}

/**
 * @brief Initialize the QoS threads.
 *
 * This function initializes the two QoS worker threads,
 * if QoS is enabled in the configuration.
 *
 * @note The threads are detached after creation.
 */
static void qos_thread_init(void)
{
	int rc = 0;

	if (!g_qos_config->enable_qos)
		return;
	PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
	if (!(atomic_fetch_uint32_t(&qos_bits) & QOS_THREAD_RUNNABLE)) {
		atomic_set_uint32_t_bits(&qos_bits, QOS_THREAD_RUNNABLE);
		/* 2 Threads named qos_read, qos_write */
		for (int i = 0; i < 2; i++) {
			/* Starting the threads for QOS */
			rc = PTHREAD_create(&qos_thread[i], NULL,
					    qos_thread_func,
					    (void *)(intptr_t)i);
			if (rc != 0) {
				LogFatal(COMPONENT_QOS,
					 "Thread creation failed error %d (%s)",
					 errno, strerror(errno));
			}
		}
	}
	PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);
}

/**
 * Function to initialize the QoS thread.
 * Will get called if qos is enabled after nfs_read_conf.
 * NOTE : Runtime enablement and disablement of QOS is not supported.
 *	  But in case have to support, then enablement function should call
 *		qos_thread_init() and not qos_init().
 *	  qos_init() should be called only one time throughout the life time of
 *	  ganesha process.
 *
 */
void qos_init(void)
{
	PTHREAD_MUTEX_init(&g_qos_iopath_lock, NULL);
	PTHREAD_MUTEX_init(&g_qos_config_lock, NULL);
	LogDebug(COMPONENT_QOS, "QOS thread_init");
	qos_thread_init();
}

/**
 * @brief Callback function for cleanup of IO in case of shutdown PE/PEPC.
 *
 * @param [in] Pointer to gsh_export structure.
 * @param [in] state Void pointer containing NULL.
 *
 * @return true to continue iteration
 */
bool pe_stop_iter(struct gsh_export *gsh_export, void *state)
{
	qos_class_t *qos_class = gsh_export->qos_class;

	if (qos_class != NULL)
		qos_free_mem(gsh_export, QOS_EXPORT);
	gsh_export->qos_class = NULL;
	/* Continue iteration */
	return true;
}

/**
 * @brief Callback function for cleanup of IO in case of shutdown for PerCient.
 *
 * @param [in] Pointer to gsh_client structure.
 * @param [in] state Void pointer containing NULL.
 *
 * @return true to continue iteration.
 */
bool pc_stop_iter(struct gsh_client *gsh_client, void *state)
{
	qos_class_t *qos_class = gsh_client->qos_class;

	if (qos_class != NULL)
		qos_free_mem(gsh_client, QOS_CLIENT);
	gsh_client->qos_class = NULL;

	/* Continue iteration */
	return true;
}

/**
 * Function to flush IO on stopping the QOS engine in shutdown case
 *
 * @param [in] void
 */
static inline void stop_qos_io(void)
{
	switch (g_qos_config->qos_type) {
	case QOS_NOT_ENABLED:
		LogDebug(COMPONENT_QOS, "QOS not enabled: %d",
			 g_qos_config->qos_type);
		break;
	case QOS_PEREXPORT_PERCLIENT_ENABLED:
	case QOS_PER_EXPORT_ENABLED:
		foreach_gsh_export(pe_stop_iter, false, NULL)
			;
		break;
	case QOS_PER_CLIENT_ENABLED:
		foreach_gsh_client(pc_stop_iter, NULL)
			;
		break;
	}
}

/**
 * Function to shutdown the QOS activity called on AdminHalt
 */
void shutdown_qos(void)
{
	LogEvent(COMPONENT_QOS, "Initating QOS shutdown");

	if ((!(atomic_fetch_uint32_t(&qos_bits) & QOS_THREAD_RUNNABLE))) {
		LogEvent(COMPONENT_QOS, "QOS shutdown completed");
		return;
	}

	PTHREAD_MUTEX_lock(&g_qos_iopath_lock);
	/* Signal threads to exit */
	atomic_clear_uint32_t_bits(&qos_bits, QOS_THREAD_RUNNABLE);
	/* Disable IO submission path, Config Update path*/
	g_qos_config->enable_qos = false;

	/* Join threads to stop IO churning */
	for (int i = 0; i < 2; i++) {
		pthread_join(qos_thread[i], NULL);
	}

	LogEvent(COMPONENT_QOS, "Initating QOS IO cleanup");
	stop_qos_io();
	LogEvent(COMPONENT_QOS, "QOS IO Cleanup done, QOS threads stopped");
	PTHREAD_MUTEX_unlock(&g_qos_iopath_lock);

	PTHREAD_MUTEX_destroy(&g_qos_iopath_lock);
	PTHREAD_MUTEX_destroy(&g_qos_config_lock);
}
