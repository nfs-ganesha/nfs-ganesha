/*
 * Copyright CEA/DAM/DIF  (2012)
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
 * Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs_rpc_msk.c
 * \author  $Author: Dominique Martinet $
 * \date    $Date: 2012/08/31 12:33:05 $
 * \version #Revision: 0.1 #
 * \brief   Contains the 'rpc_msk_dispatcher_thread' routine for the nfsd.
 *
 * nfs_rpc_msk.c : The file that contain the 'rpc_msk_dispatcher_thread'
 * routine for the nfsd (and all the related stuff).
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>	   /* for having FNDELAY */
#include <sys/select.h>
#include <poll.h>
#include <assert.h>
#include "log.h"
#include "gsh_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include <mooshika.h>

enum xprt_stat thr_decode_rpc_request(struct fridgethr_context *thr_ctx,
				      SVCXPRT * xprt);


void nfs_msk_callback_disconnect(msk_trans_t *trans)
{
}

struct clx {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	SVCXPRT *xprt;
};

void nfs_msk_callback(void *arg)
{
	struct clx *clx = arg;
	PTHREAD_MUTEX_lock(&clx->lock);
	thr_decode_rpc_request(NULL, clx->xprt);
	pthread_cond_signal(&clx->cond);
	PTHREAD_MUTEX_unlock(&clx->lock);
}

/* extern
SVCXPRT *svc_msk_create(msk_trans_t *, u_int, void (*)(void *), void *);
*/

void *nfs_msk_thread(void *arg)
{
	msk_trans_t *trans = arg;
	SVCXPRT *xprt;
	struct clx clx;
	/*** alloc onfsreq ***/

	if (trans == NULL) {
		LogMajor(COMPONENT_NFS_MSK,
			"NFS/RDMA: handle thread started but no child_trans");
		return NULL;
	}

	PTHREAD_COND_init(&clx.cond, NULL);
	PTHREAD_MUTEX_init(&clx.lock, NULL);

	PTHREAD_MUTEX_lock(&clx.lock);

	xprt = svc_msk_create(trans, 30, nfs_msk_callback, &clx);
	clx.xprt = xprt;

	/* It's still safe to set stuff here that will be used in
	   dispatch_rpc_request because of the lock
	*/
	xprt->xp_u1 = alloc_gsh_xprt_private(xprt, XPRT_PRIVATE_FLAG_NONE);
	/* fixme: put something here, but make it not work on fd operations. */
	xprt->xp_fd = -1;

	/*
	while (trans->state == MSK_CONNECTED) {
	 ** use SVC_STAT(msk_xprt)
	 */
	while (SVC_STAT(xprt) == XPRT_IDLE)
		pthread_cond_wait(&clx.cond, &clx.lock);

	PTHREAD_MUTEX_unlock(&clx.lock);

	/* We never get here for some reason */
	msk_destroy_trans(&trans);

	return NULL;
}

void *nfs_msk_dispatcher_thread(void *nullarg)
{
	msk_trans_t *trans;	/* connection main trans */
	msk_trans_t *child_trans;  /* child trans */
	pthread_attr_t attr_thr;
	int rc;
	msk_trans_attr_t trans_attr;
	pthread_t thrid_handle_trans;

	memset(&trans_attr, 0, sizeof(trans_attr));
	trans_attr.debug = MSK_DEBUG_EVENT;
	trans_attr.server = 10;
	trans_attr.rq_depth = 32;
	trans_attr.sq_depth = 32;
	trans_attr.max_send_sge = 2;
	trans_attr.port = "20049";
	trans_attr.node = "::";
	trans_attr.disconnect_callback = nfs_msk_callback_disconnect;
	trans_attr.worker_count = 4;
	trans_attr.worker_queue_size = 256;

	/* Init for thread parameter (mostly for scheduling) */
	if (pthread_attr_init(&attr_thr))
		LogDebug(COMPONENT_NFS_MSK,
			"can't init pthread's attributes");

	if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM))
		LogDebug(COMPONENT_NFS_MSK,
			"can't set pthread's scope");

	if (pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE))
		LogDebug(COMPONENT_NFS_MSK,
			"can't set pthread's join state");

	/* Init RDMA via mooshika */
	if (msk_init(&trans, &trans_attr))
		LogFatal(COMPONENT_NFS_MSK,
			"9P/RDMA dispatcher could not start mooshika engine");
	else
		LogEvent(COMPONENT_NFS_MSK,
			"Mooshika engine is started");

	/* Bind Mooshika */
	if (msk_bind_server(trans))
		LogFatal(COMPONENT_NFS_MSK,
			"9P/RDMA dispatcher could not bind mooshika engine");
	else
		LogEvent(COMPONENT_NFS_MSK,
			"Mooshika engine is bound");


	while (1) {
		child_trans = msk_accept_one(trans);
		if (child_trans == NULL)
			LogMajor(COMPONENT_NFS_MSK,
				"NFS/RDMA: dispatcher "
				"failed to accept a new client");
		else {
			LogDebug(COMPONENT_NFS_MSK,
				"Got a new connection, "
				"spawning a polling thread");
			rc = pthread_create(&thrid_handle_trans,
					&attr_thr, nfs_msk_thread, child_trans);
			if (rc)
				LogMajor(COMPONENT_NFS_MSK,
					"NFS/RDMA: dipatcher "
					"accepted a new client "
					"but could not spawn a related thread");
			else
				LogEvent(COMPONENT_NFS_MSK,
					"NFS/RDMA: thread %u spawned "
					"to manage a new child_trans",
					(unsigned int)thrid_handle_trans);
		}
	} /* while (1) */

	return NULL;
} /* nfs_msk_dispatched_thread */

