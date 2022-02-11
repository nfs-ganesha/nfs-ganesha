// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_flush_hook.c
 * \date    $Date: 2006/02/23 12:33:05 $
 * \brief   The file that contain the routines dedicated to TFLUSH management
 *
 * 9p_flush_hook.c.c : routines for TFLUSH management.
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include "hashtable.h"
#include "log.h"
#include "abstract_mem.h"
#include "abstract_atomic.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "9p.h"

struct flush_condition {
	pthread_cond_t condition;
	int reply_sent;
};

void _9p_AddFlushHook(struct _9p_request_data *req, int tag,
		      unsigned long sequence)
{
	int bucket = tag % FLUSH_BUCKETS;
	struct _9p_flush_hook *hook = &req->flush_hook;
	struct _9p_conn *conn = req->pconn;

	hook->tag = tag;
	hook->condition = NULL;
	hook->sequence = sequence;
	PTHREAD_MUTEX_lock(&conn->flush_buckets[bucket].lock);
	glist_add(&conn->flush_buckets[bucket].list, &hook->list);
	PTHREAD_MUTEX_unlock(&conn->flush_buckets[bucket].lock);
}

void _9p_FlushFlushHook(struct _9p_conn *conn, int tag, unsigned long sequence)
{
	int bucket = tag % FLUSH_BUCKETS;
	struct glist_head *node;
	struct _9p_flush_hook *hook = NULL;
	struct flush_condition fc;

	PTHREAD_MUTEX_lock(&conn->flush_buckets[bucket].lock);
	glist_for_each(node, &conn->flush_buckets[bucket].list) {
		hook = glist_entry(node, struct _9p_flush_hook, list);
		/* Cancel a request that has the right tag
		 * --AND-- is older than the flush request.
		 **/
		if ((hook->tag == tag) && (hook->sequence < sequence)) {
			PTHREAD_COND_init(&fc.condition, NULL);
			fc.reply_sent = 0;
			hook->condition = &fc;
			glist_del(&hook->list);
			LogFullDebug(COMPONENT_9P, "Found tag to flush %d\n",
				     tag);
			/*
			 * Now, wait until the request is complete
			 * so we can send the RFLUSH.
			 * warning: this will unlock the bucket lock */
			while (!fc.reply_sent)
				pthread_cond_wait(
					&fc.condition,
					&conn->flush_buckets[bucket].lock);
			break;
		}
	}
	PTHREAD_MUTEX_unlock(&conn->flush_buckets[bucket].lock);
}

void _9p_DiscardFlushHook(struct _9p_request_data *req)
{
	struct _9p_flush_hook *hook = &req->flush_hook;
	struct _9p_conn *conn = req->pconn;
	int bucket = hook->tag % FLUSH_BUCKETS;

	PTHREAD_MUTEX_lock(&conn->flush_buckets[bucket].lock);
	/* If no flush request arrived, we have to
	 * remove the hook from the list.
	 * If a flush request arrived, signal the thread that is waiting
	 */
	if (hook->condition == NULL)
		glist_del(&hook->list);
	else {
		hook->condition->reply_sent = 1;
		pthread_cond_signal(&hook->condition->condition);
	}

	PTHREAD_MUTEX_unlock(&conn->flush_buckets[bucket].lock);
}
