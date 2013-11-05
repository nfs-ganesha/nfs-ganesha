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
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "9p.h"

#include <mooshika.h>

void _9p_rdma_callback_send(msk_trans_t *trans, msk_data_t *data, void *arg)
{
	struct _9p_outqueue *outqueue =  (struct _9p_outqueue *) arg;

	pthread_mutex_lock(&outqueue->lock);
	data->next = outqueue->data;
	outqueue->data = data;
	pthread_cond_signal(&outqueue->cond);
	pthread_mutex_unlock(&outqueue->lock);
}

void _9p_rdma_callback_send_err(msk_trans_t *trans, msk_data_t *data,
				void *arg)
{
	struct _9p_outqueue *outqueue =  (struct _9p_outqueue *) arg;
	/**
	 * @todo: This should probably try to send again a few times
	 * before unlocking
	 */

	pthread_mutex_lock(&outqueue->lock);
	data->next = outqueue->data;
	outqueue->data = data;
	pthread_cond_signal(&outqueue->cond);
	pthread_mutex_unlock(&outqueue->lock);
}

void _9p_rdma_callback_recv_err(msk_trans_t *trans, msk_data_t *data,
				void *arg)
{

	if (trans->state == MSK_CONNECTED)
		msk_post_recv(trans, data, _9p_rdma_callback_recv,
			      _9p_rdma_callback_recv_err, arg);
}

void _9p_rdma_callback_disconnect(msk_trans_t *trans)
{
	if (!trans || !trans->private_data)
		return;

	_9p_rdma_cleanup_conn(trans);
}

void _9p_rdma_process_request(struct _9p_request_data *req9p,
			      nfs_worker_data_t *worker_data)
{
	msk_trans_t *trans = req9p->pconn->trans_data.rdma_trans;
	struct _9p_rdma_priv *priv = _9p_rdma_priv_of(trans);

	struct _9p_datalock *datalock = req9p->datalock;

	/* get output buffer and move forward in queue */
	pthread_mutex_lock(&priv->outqueue->lock);
	while (priv->outqueue->data == NULL)
		pthread_cond_wait(&priv->outqueue->cond, &priv->outqueue->lock);

	datalock->out = priv->outqueue->data;
	priv->outqueue->data = datalock->out->next;
	pthread_mutex_unlock(&priv->outqueue->lock);

	uint32_t msglen;
	u32 outdatalen = 0;
	int rc = 0;

	/* Use buffer received via RDMA as a 9P message */
	req9p->_9pmsg = datalock->data->data;
	msglen = *(uint32_t *)req9p->_9pmsg;

	if (datalock->data->size < _9P_HDR_SIZE
	    || msglen != datalock->data->size) {
		LogMajor(COMPONENT_9P,
			 "Malformed 9P/RDMA packet, bad header size");
		/* semd a rerror ? */
		msk_post_recv(trans, datalock->data, _9p_rdma_callback_recv,
			      _9p_rdma_callback_recv_err, datalock);
	} else {
		LogFullDebug(COMPONENT_9P,
			     "Received 9P/RDMA message of size %u",
			     msglen);

		rc = _9p_process_buffer(req9p, worker_data, datalock->out->data,
					&outdatalen);
		if (rc != 1) {
			LogMajor(COMPONENT_9P,
				 "Could not process 9P buffer on socket #%lu",
				 req9p->pconn->trans_data.sockfd);
		}

		/* Mark the buffer ready for later receive and post the reply */
		msk_post_recv(trans, datalock->data, _9p_rdma_callback_recv,
			      _9p_rdma_callback_recv_err, datalock);

		/* If earlier processing succeeded, post it */
		if (rc == 1) {
			datalock->out->size = outdatalen;
			datalock->out->mr = priv->outmr;
			if (0 !=
			    msk_post_send(trans, datalock->out,
					  _9p_rdma_callback_send,
					  _9p_rdma_callback_send_err,
					  priv->outqueue))
				rc = -1;
		}

		if (rc != 1) {
			/* Give the buffer back right away
			 * since no buffer is being sent */
			pthread_mutex_lock(&priv->outqueue->lock);
			datalock->out->next = priv->outqueue->data;
			priv->outqueue->data = datalock->out;
			pthread_cond_signal(&priv->outqueue->cond);
			pthread_mutex_unlock(&priv->outqueue->lock);
		}

		_9p_DiscardFlushHook(req9p);
	}
}

void _9p_rdma_callback_recv(msk_trans_t *trans, msk_data_t *data, void *arg)
{
	struct _9p_datalock *_9p_datalock = arg;
	request_data_t *req = NULL;
	u16 tag = 0;
	char *_9pmsg = NULL;

	if (!_9p_datalock) {
		LogEvent(COMPONENT_9P,
			 "no callback_arg in _9p_rdma_callback_recv");
		return;
	}

	req = pool_alloc(request_pool, NULL);

	req->rtype = _9P_REQUEST;
	req->r_u._9p._9pmsg = _9pmsg;
	req->r_u._9p.pconn = _9p_rdma_priv_of(trans)->pconn;
	req->r_u._9p.datalock = _9p_datalock;

	/* Add this request to the request list, should it be flushed later. */
	_9pmsg = data->data;
	tag = *(u16 *) (_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE);
	_9p_AddFlushHook(&req->r_u._9p, tag, req->r_u._9p.pconn->sequence++);

	DispatchWork9P(req);
}				/* _9p_rdma_callback_recv */
