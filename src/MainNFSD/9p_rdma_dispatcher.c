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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_rdma_dispatcher.c
 * \date    $Date: 2006/02/23 12:33:05 $
 * \brief   The file that contains the '_9p_rdma_dispatcher_thread' function.
 *
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
#include <arpa/inet.h>
#include "hashtable.h"
#include "log.h"
#include "abstract_mem.h"
#include "abstract_atomic.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "client_mgr.h"
#include "9p.h"

#include <mooshika.h>

static void *_9p_rdma_cleanup_conn_thread(void *arg)
{
	msk_trans_t *trans = arg;
	struct _9p_rdma_priv *priv = _9p_rdma_priv_of(trans);

	if (priv) {
		if (priv->pconn) {
			LogDebug(COMPONENT_9P,
				 "9P/RDMA: waiting till we're done with all requests on trans [%p]",
				 trans);

			while (atomic_fetch_uint32_t(&priv->pconn->refcount) !=
			       0) {
				sleep(1);
			}
		}
		LogDebug(COMPONENT_9P,
			 "9P/RDMA: Freeing data associated with trans [%p]",
			 trans);

		if (priv->pconn) {
			if (priv->pconn->client != NULL)
				put_gsh_client(priv->pconn->client);

			_9p_cleanup_fids(priv->pconn);
		}

		if (priv->pconn)
			gsh_free(priv->pconn);

		gsh_free(priv);
	}

	msk_destroy_trans(&trans);
	pthread_exit(NULL);
}

void _9p_rdma_cleanup_conn(msk_trans_t *trans)
{
	pthread_attr_t attr_thr;
	pthread_t thr_id;

	/* Set the pthread attributes */
	memset((char *)&attr_thr, 0, sizeof(attr_thr));
	if (pthread_attr_init(&attr_thr) ||
	    pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) ||
	    pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_DETACHED))
		return;

	if (pthread_create(&thr_id, &attr_thr, _9p_rdma_cleanup_conn_thread,
			   trans))
		LogMajor(COMPONENT_9P,
			 "9P/RDMA : dispatcher cleanup could not spawn a related thread");
	else
		LogDebug(COMPONENT_9P,
			 "9P/RDMA: thread #%x spawned to cleanup trans [%p]",
			 (unsigned int)thr_id,
			 trans);
}

/**
 * _9p_rdma_handle_trans_thr: 9P/RDMA listener
 *
 * @param Arg : contains the child trans to be managed
 *
 * @return NULL
 *
 */
/* Equivalent du _9p_socket_thread */
void *_9p_rdma_thread(void *Arg)
{
	msk_trans_t *trans = Arg;

	struct _9p_rdma_priv *priv = NULL;
	struct _9p_conn *p_9p_conn = NULL;
	unsigned int i = 0;
	int rc = 0;
	struct _9p_outqueue *outqueue = trans->private_data;
	struct sockaddr *addrpeer;

	priv = gsh_calloc(1, sizeof(*priv));

	trans->private_data = priv;
	priv->pernic = msk_getpd(trans)->private;
	priv->outqueue = outqueue;

	p_9p_conn = gsh_calloc(1, sizeof(*p_9p_conn));

	priv->pconn = p_9p_conn;

	for (i = 0; i < FLUSH_BUCKETS; i++) {
		PTHREAD_MUTEX_init(&p_9p_conn->flush_buckets[i].lock, NULL);
		glist_init(&p_9p_conn->flush_buckets[i].list);
	}
	p_9p_conn->sequence = 0;
	atomic_store_uint32_t(&p_9p_conn->refcount, 0);
	p_9p_conn->trans_type = _9P_RDMA;
	p_9p_conn->trans_data.rdma_trans = trans;

	addrpeer = msk_get_dst_addr(trans);
	if (addrpeer == NULL) {
		LogCrit(COMPONENT_9P, "Cannot get peer address");
		goto error;
	}
	memcpy(&p_9p_conn->addrpeer, addrpeer,
	       MIN(sizeof(*addrpeer), sizeof(p_9p_conn->addrpeer)));
	p_9p_conn->client =
		get_gsh_client(&p_9p_conn->addrpeer, false);

	/* Init the fids pointers array */
	memset(&p_9p_conn->fids,
	       0,
	       _9P_FID_PER_CONN * sizeof(struct _9p_fid *));

	/* Set initial msize.
	 * Client may request a lower value during TVERSION */
	p_9p_conn->msize = _9p_param._9p_rdma_msize;

	if (gettimeofday(&p_9p_conn->birth, NULL) == -1)
		LogMajor(COMPONENT_9P, "Cannot get connection's time of birth");

	/* Finalize accept */
	rc = msk_finalize_accept(trans);
	if (rc != 0) {
		LogMajor(COMPONENT_9P,
			 "9P/RDMA: trans handler could not finalize accept, rc=%u",
			 rc);
		goto error;
	}

	pthread_exit(NULL);

 error:

	_9p_rdma_cleanup_conn_thread(trans);

	pthread_exit(NULL);
}				/* _9p_rdma_handle_trans */

static void _9p_rdma_setup_pernic(msk_trans_t *trans, uint8_t *outrdmabuf)
{
	struct _9p_rdma_priv_pernic *pernic;
	int rc, i;

	/* Do nothing if we already have stuff setup */
	if (msk_getpd(trans)->private)
		return;

	pernic = gsh_calloc(1, sizeof(*pernic));

	/* register output buffers */
	pernic->outmr = msk_reg_mr(trans, outrdmabuf,
			_9p_param._9p_rdma_outpool_size
			* _9p_param._9p_rdma_msize,
			IBV_ACCESS_LOCAL_WRITE);
	if (pernic->outmr == NULL) {
		rc = errno;
		LogFatal(COMPONENT_9P,
			 "9P/RDMA: pernic setup could not register outrdmabuf, errno: %s (%d)",
			 strerror(rc), rc);
	}

	/* register input buffers */
	/* Alloc rdmabuf */
	pernic->rdmabuf = gsh_malloc(_9p_param._9p_rdma_inpool_size *
				     _9p_param._9p_rdma_msize);

	/* Register rdmabuf */
	pernic->inmr = msk_reg_mr(trans, pernic->rdmabuf,
				  _9p_param._9p_rdma_inpool_size
					* _9p_param._9p_rdma_msize,
				  IBV_ACCESS_LOCAL_WRITE);
	if (pernic->inmr == NULL) {
		rc = errno;
		LogFatal(COMPONENT_9P,
			 "9P/RDMA: trans handler could not register rdmabuf, errno: %s (%d)",
			 strerror(rc), rc);
	}

	/* Get prepared to recv data */

	pernic->rdata = gsh_calloc(_9p_param._9p_rdma_inpool_size,
				   sizeof(*pernic->rdata));

	for (i = 0; i < _9p_param._9p_rdma_inpool_size; i++) {
		pernic->rdata[i].data = pernic->rdmabuf +
					i * _9p_param._9p_rdma_msize;
		pernic->rdata[i].max_size = _9p_param._9p_rdma_msize;
		pernic->rdata[i].mr = pernic->inmr;

		rc = msk_post_recv(trans, pernic->rdata + i,
				   _9p_rdma_callback_recv,
				   _9p_rdma_callback_recv_err,
				   NULL);
		if (rc != 0) {
			LogEvent(COMPONENT_9P,
				 "9P/RDMA: trans handler could post_recv first byte of data[%u], rc=%u",
				 i, rc);
			msk_dereg_mr(pernic->inmr);
			msk_dereg_mr(pernic->outmr);
			gsh_free(pernic->rdmabuf);
			gsh_free(pernic->rdata);
			gsh_free(pernic);
			return;
		}
	}

	msk_getpd(trans)->private = pernic;
}

static void _9p_rdma_setup_global(uint8_t **poutrdmabuf, msk_data_t **pwdata,
				  struct _9p_outqueue **poutqueue)
{
	uint8_t *outrdmabuf;
	int i;
	msk_data_t *wdata;
	struct _9p_outqueue *outqueue;

	outrdmabuf = gsh_malloc(_9p_param._9p_rdma_outpool_size
				* _9p_param._9p_rdma_msize);

	*poutrdmabuf = outrdmabuf;

	wdata = gsh_malloc(_9p_param._9p_rdma_outpool_size
			   * sizeof(*wdata));

	for (i = 0; i < _9p_param._9p_rdma_outpool_size; i++) {
		wdata[i].data = outrdmabuf +
				i * _9p_param._9p_rdma_msize;
		wdata[i].max_size = _9p_param._9p_rdma_msize;
		if (i != _9p_param._9p_rdma_outpool_size - 1)
			wdata[i].next = &wdata[i+1];
		else
			wdata[i].next = NULL;
	}
	*pwdata = wdata;

	outqueue = gsh_malloc(sizeof(*outqueue));

	PTHREAD_MUTEX_init(&outqueue->lock, NULL);
	PTHREAD_COND_init(&outqueue->cond, NULL);
	outqueue->data = wdata;
	*poutqueue = outqueue;
}


/**
 * _9p_rdma_dispatcher_thread: 9P/RDMA dispatcher
 *
 * @param Arg the socket number cast as a void * in pthread_create
 *
 * @return NULL
 *
 */
void *_9p_rdma_dispatcher_thread(void *Arg)
{
	msk_trans_t *trans;
	msk_trans_t *child_trans;

	msk_trans_attr_t trans_attr;
	pthread_attr_t attr_thr;
	pthread_t thrid_handle_trans;

	uint8_t *outrdmabuf = NULL;
	msk_data_t *wdata;
	struct _9p_outqueue *outqueue;

#define PORT_MAX_LEN 6
	char port[PORT_MAX_LEN];

	memset(&trans_attr, 0, sizeof(msk_trans_attr_t));

	trans_attr.server = _9p_param._9p_rdma_backlog;
	trans_attr.rq_depth = _9p_param._9p_rdma_inpool_size + 1;
	trans_attr.sq_depth = _9p_param._9p_rdma_outpool_size + 1;
	snprintf(port, PORT_MAX_LEN, "%d", _9p_param._9p_rdma_port);
	trans_attr.port = port;
	trans_attr.node = "::";
	trans_attr.use_srq = 1;
	trans_attr.disconnect_callback = _9p_rdma_callback_disconnect;
	trans_attr.worker_count = -1;
	/* if worker_count isn't -1: trans_attr.worker_queue_size = 256; */
	trans_attr.debug = MSK_DEBUG_EVENT;
	/* mooshika stats:
	 * trans_attr.stats_prefix + trans_attr.debug |= MSK_DEBUG_SPEED */

	SetNameFunction("_9p_rdma_disp");

	/* Calling dispatcher main loop */
	LogInfo(COMPONENT_9P_DISPATCH, "Entering 9P/RDMA dispatcher");

	LogDebug(COMPONENT_9P_DISPATCH, "My pthread id is %p",
		 (void *) pthread_self());

	/* Prepare attr_thr for dispatch */
	memset((char *)&attr_thr, 0, sizeof(attr_thr));

	/* Set the pthread attributes */
	if (pthread_attr_init(&attr_thr))
		LogFatal(COMPONENT_9P,
			 "9P/RDMA dispatcher could not init pthread_attr_t");

	if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM))
		LogFatal(COMPONENT_9P,
			 "9P/RDMA dispatcher could not set pthread_attr_t:scope_system");

	if (pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_DETACHED))
		LogFatal(COMPONENT_9P,
			 "9P/RDMA dispatcher could not set pthread_attr_t:create_joignable");

	/* Init RDMA via mooshika */
	if (msk_init(&trans, &trans_attr))
		LogFatal(COMPONENT_9P,
			 "9P/RDMA dispatcher could not start mooshika engine");
	else
		LogEvent(COMPONENT_9P, "Mooshika engine is started");

	/* Bind Mooshika */
	if (msk_bind_server(trans))
		LogFatal(COMPONENT_9P,
			 "9P/RDMA dispatcher could not bind mooshika engine");
	else
		LogEvent(COMPONENT_9P, "Mooshika engine is bound");

	/* Start infinite loop here */
	while (1) {
		child_trans = msk_accept_one(trans);
		if (child_trans == NULL)
			LogMajor(COMPONENT_9P,
				 "9P/RDMA : dispatcher failed to accept a new client");
		else {
			/* Create output buffers on first connection.
			 * need it here because we don't want multiple
			 * children to do this job.
			 */
			if (!outrdmabuf) {
				_9p_rdma_setup_global(&outrdmabuf,
						      &wdata,
						      &outqueue);
				/* this means ENOMEM - abort */
				if (!outrdmabuf || !wdata || !outqueue)
					break;
			}
			_9p_rdma_setup_pernic(child_trans, outrdmabuf);
			child_trans->private_data = outqueue;

			if (pthread_create(&thrid_handle_trans,
					   &attr_thr,
					   _9p_rdma_thread,
					   child_trans))
				LogMajor(COMPONENT_9P,
					 "9P/RDMA : dispatcher accepted a new client but could not spawn a related thread");
			else
				LogEvent(COMPONENT_9P,
					 "9P/RDMA: thread #%x spawned to managed new child_trans [%p]",
					 (unsigned int)thrid_handle_trans,
					 child_trans);
		}
	}			/* for( ;; ) */

	pthread_exit(NULL);
}				/* _9p_rdma_dispatcher_thread */
