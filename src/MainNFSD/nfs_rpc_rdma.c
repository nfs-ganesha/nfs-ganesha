/*
 * Copyright © 2015, CohortFS, LLC.
 *
 * contributeur : William Allen Simpson <bill@cohortfs.com>
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
 * -------------
 */

/**
 * @file    nfs_rpc_rdma.c
 * @author  William Allen Simpson <bill@cohortfs.com>
 * @date    2015/02/12 15:18:52
 * @brief   Contains the 'nfs_rdma_dispatcher_thread' routine for the nfsd.
 *
 * @note    Extracted from previous work by
 *          Dominique Martinet <dominique.martinet@cea.fr>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "gsh_rpc.h"
#include "nfs_init.h"

/**
 * rpc_rdma_disconnect_callback: placeholder
 *
 * @param[inout] xprt	must be init first
 */
static void
rpc_rdma_disconnect_callback(SVCXPRT *xprt)
{
}

/**
 * rpc_rdma_dispatcher_thread: setup NFS/RDMA engine
 *
 * Initially creates a listener and its connection manager epoll thread.
 *
 * Each connection request creates a child.
 *
 * The completion queue epoll thread is shared among all children.
 *
 * @param[in] xa	must be init first
 *
 * @return NULL
 */
void *
nfs_rdma_dispatcher_thread(void *nullarg)
{
	struct rpc_rdma_attr xa = {
		.statistics_prefix = NULL,
		.node = "::",
		.port = "20049",
		.disconnect_cb = rpc_rdma_disconnect_callback,
		.request_cb = thr_decode_rpc_request,
		.timeout = 30000,		/* in ms */
		.sq_depth = 32,			/* default was 50 */
		.max_send_sge = 32,		/* minimum 2 */
		.rq_depth = 32,			/* default was 50 */
		.max_recv_sge = 31,		/* minimum 1 */
		.backlog = 10,			/* minimum 2 */
		.credits = 30,			/* default 10 */
		.worker_count = 4,		/* default 0 */
		.worker_queue_size = 256,	/* default 0 */
		.destroy_on_disconnect = true,
		.use_srq = false,
	};
	SVCXPRT *l_xprt = rpc_rdma_create(&xa);

	if (!l_xprt) {
		LogCrit(COMPONENT_DISPATCH,
			"NFS/RDMA dispatcher could not start engine");
		return NULL;
	}
	LogEvent(COMPONENT_DISPATCH,
		"NFS/RDMA engine initialized");

	/* All clones and large allocations are done in this loop,
	 * avoiding contention in the heap(s), serialized by the
	 * connection_requests queue.
	 */
	while (l_xprt->xp_refs > 0) {
		/* values used in Mooshika were 8*1024, 4*8*1024;
		 * should be configurable.
		 */
		SVCXPRT *c_xprt = svc_rdma_create(l_xprt, 4*1024, 4*1024,
							SVC_XPRT_FLAG_NONE);
		if (!c_xprt) {
			/* message already logged */
			continue;
		}

		LogEvent(COMPONENT_DISPATCH,
			"cloned (child) transport %p",
			c_xprt);
	}

	/* We never get here, xp_refs is always > 0 until destroy */
	SVC_DESTROY(l_xprt);

	return NULL;
} /* rpc_rdma_dispatcher_thread */
