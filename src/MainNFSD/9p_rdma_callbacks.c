/*
 *  vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright CEA/DAM/DIF  (2012)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 3 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 *  ---------------------------------------
 */

/**
 *  \file    9p_rdma_callbacks.c
 *  \brief   This file contains the callbacks used for 9P/RDMA.
 *
 *  9p_rdma_callbacks.c: This file contains the callbacks used for 9P/RDMA.
 *
 */
#include "config.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "log.h"
#include "abstract_mem.h"
#include "abstract_atomic.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "server_stats.h"
#include "9p.h"

#include <mooshika.h>

void _9p_rdma_callback_send(msk_trans_t *trans, msk_data_t *data, void *arg)
{
	struct _9p_rdma_priv *priv = _9p_rdma_priv_of(trans);

	PTHREAD_MUTEX_lock(&priv->outqueue->lock);
	data->next = priv->outqueue->data;
	priv->outqueue->data = data;
	pthread_cond_signal(&priv->outqueue->cond);
	PTHREAD_MUTEX_unlock(&priv->outqueue->lock);

	server_stats_transport_done(priv->pconn->client,
				    0, 0, 0,
				    data->size, 1, 0);

}

void _9p_rdma_callback_send_err(msk_trans_t *trans, msk_data_t *data,
				void *arg)
{
	struct _9p_rdma_priv *priv = _9p_rdma_priv_of(trans);
	/**
	 * @todo: This should probably try to send again a few times
	 * before unlocking
	 */

	if (priv && priv->outqueue) {
		PTHREAD_MUTEX_lock(&priv->outqueue->lock);
		data->next = priv->outqueue->data;
		priv->outqueue->data = data;
		pthread_cond_signal(&priv->outqueue->cond);
		PTHREAD_MUTEX_unlock(&priv->outqueue->lock);
	}
	if (priv && priv->pconn && priv->pconn->client)
		server_stats_transport_done(priv->pconn->client,
			    0, 0, 0,
			    0, 0, 1);
}

void _9p_rdma_callback_recv_err(msk_trans_t *trans, msk_data_t *data,
				void *arg)
{
	struct _9p_rdma_priv *priv = _9p_rdma_priv_of(trans);

	if (trans->state == MSK_CONNECTED) {
		msk_post_recv(trans, data, _9p_rdma_callback_recv,
			      _9p_rdma_callback_recv_err, arg);
		if (priv && priv->pconn && priv->pconn->client)
			server_stats_transport_done(priv->pconn->client,
						    0, 0, 1,
						    0, 0, 0);
	}
}

void _9p_rdma_callback_disconnect(msk_trans_t *trans)
{
	if (!trans || !trans->private_data)
		return;

	_9p_rdma_cleanup_conn(trans);
}

void _9p_rdma_process_request(struct _9p_request_data *req9p)
{
	uint32_t msglen;
	int rc = 0;
	msk_trans_t *trans = req9p->pconn->trans_data.rdma_trans;
	struct _9p_rdma_priv *priv = _9p_rdma_priv_of(trans);
	msk_data_t *dataout;

	/* get output buffer and move forward in queue */
	PTHREAD_MUTEX_lock(&priv->outqueue->lock);
	while (priv->outqueue->data == NULL) {
		LogDebug(COMPONENT_9P,
			 "Waiting for outqueue buffer on trans %p\n", trans);
		pthread_cond_wait(&priv->outqueue->cond, &priv->outqueue->lock);
	}

	dataout = priv->outqueue->data;
	priv->outqueue->data = dataout->next;
	dataout->next = NULL;
	PTHREAD_MUTEX_unlock(&priv->outqueue->lock);

	dataout->size = 0;
	dataout->mr = priv->pernic->outmr;

	/* Use buffer received via RDMA as a 9P message */
	req9p->_9pmsg = req9p->data->data;
	msglen = *(uint32_t *)req9p->_9pmsg;

	if (req9p->data->size < _9P_HDR_SIZE
	    || msglen != req9p->data->size) {
		LogMajor(COMPONENT_9P,
			 "Malformed 9P/RDMA packet, bad header size");
		/* send a rerror ? */
		msk_post_recv(trans, req9p->data, _9p_rdma_callback_recv,
			      _9p_rdma_callback_recv_err, NULL);
	} else {
		LogFullDebug(COMPONENT_9P,
			     "Received 9P/RDMA message of size %u",
			     msglen);

		rc = _9p_process_buffer(req9p, dataout->data, &dataout->size);
		if (rc != 1) {
			LogMajor(COMPONENT_9P,
				 "Could not process 9P buffer on trans %p",
				 req9p->pconn->trans_data.rdma_trans);
		}

		msk_post_recv(trans, req9p->data, _9p_rdma_callback_recv,
			      _9p_rdma_callback_recv_err, NULL);

		/* If earlier processing succeeded, post it */
		if (rc == 1) {
			if (0 !=
			    msk_post_send(trans, dataout,
					  _9p_rdma_callback_send,
					  _9p_rdma_callback_send_err,
					  NULL))
				rc = -1;
		}

		if (rc != 1) {
			LogMajor(COMPONENT_9P,
				 "Could not send buffer on trans %p",
				 req9p->pconn->trans_data.rdma_trans);
			/* Give the buffer back right away
			 * since no buffer is being sent */
			PTHREAD_MUTEX_lock(&priv->outqueue->lock);
			dataout->next = priv->outqueue->data;
			priv->outqueue->data = dataout;
			pthread_cond_signal(&priv->outqueue->cond);
			PTHREAD_MUTEX_unlock(&priv->outqueue->lock);
		}
	}
	_9p_DiscardFlushHook(req9p);
}

void _9p_rdma_callback_recv(msk_trans_t *trans, msk_data_t *data, void *arg)
{
	struct _9p_request_data *req = NULL;
	u16 tag = 0;
	char *_9pmsg = NULL;

	(void) atomic_inc_uint64_t(&nfs_health_.enqueued_reqs);
	req = gsh_calloc(1, sizeof(struct _9p_request_data));

	req->_9pmsg = _9pmsg;
	req->pconn = _9p_rdma_priv_of(trans)->pconn;
	req->data = data;

	/* Add this request to the request list, should it be flushed later. */
	_9pmsg = data->data;
	tag = *(u16 *) (_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE);
	_9p_AddFlushHook(req, tag, req->pconn->sequence++);

	DispatchWork9P(req);
	server_stats_transport_done(_9p_rdma_priv_of(trans)->pconn->client,
				    data->size, 1, 0,
				    0, 0, 0);

}				/* _9p_rdma_callback_recv */
