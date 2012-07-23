/*
 *  vim:expandtab:shiftwidth=8:tabstop=8:
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>	//printf
#include <stdlib.h>	//malloc
#include <string.h>	//memcpy
#include <unistd.h>	//read
#include <getopt.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "log.h"
#include "abstract_mem.h"
#include "nfs_init.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"
#include "9p.h"

#include <infiniband/arch.h>
#include <rdma/rdma_cma.h>
#include "trans_rdma.h"

#define TEST_Z(x)  do { if ( (x)) { LogFatal(COMPONENT_9P,"error: " #x " failed (returned non-zero)." ); }} while (0)
#define TEST_NZ(x) do { if (!(x)) { LogFatal(COMPONENT_9P,"error: " #x " failed (returned zero/null)."); }} while (0)

void _9p_rdma_callback_send(msk_trans_t *trans, void *arg) {

}

void _9p_rdma_callback_disconnect(msk_trans_t *trans) {
	if (!trans->private_data)
		return;

	struct _9p_datamr *_9p_datamr = trans->private_data;
	pthread_mutex_lock(_9p_datamr->lock);
	pthread_cond_signal(_9p_datamr->cond);
	pthread_mutex_unlock(_9p_datamr->lock);
}

void _9p_rdma_callback_recv(msk_trans_t *trans, void *arg) {
	struct _9p_datamr *_9p_datamr = arg;
	if (!_9p_datamr) {
		LogEvent( COMPONENT_9P, "no callback_arg in _9p_rdma_callback_recv");
		return;
	}

	msk_data_t *pdata = _9p_datamr->data;

	if (pdata->size != 1 || pdata->data[0] != '\0') {
		write(1, (char *)pdata->data, pdata->size);
		fflush(stdout);

		msk_post_recv(trans, pdata, 1, _9p_datamr->mr, _9p_rdma_callback_recv, _9p_datamr);
		msk_post_send(trans, _9p_datamr->ackdata, 1, _9p_datamr->mr, NULL, NULL);
	} else {
		msk_post_recv(trans, pdata, 1, _9p_datamr->mr, _9p_rdma_callback_recv, _9p_datamr);

		pthread_mutex_lock(_9p_datamr->lock);
		pthread_cond_signal(_9p_datamr->cond);
		pthread_mutex_unlock(_9p_datamr->lock);
	}
}

void* _9p_rdma_handle_trans(void *arg) {
	msk_trans_t *trans = arg;
	uint8_t *rdmabuf;
	struct ibv_mr *mr;
	msk_data_t *wdata;


	TEST_NZ(rdmabuf = malloc((_9P_RDMA_RECV_NUM+2)*_9P_RDMA_CHUNK_SIZE*sizeof(char)));
	memset(rdmabuf, 0, (_9P_RDMA_RECV_NUM+2)*_9P_RDMA_CHUNK_SIZE*sizeof(char));
	TEST_NZ(mr = msk_reg_mr(trans, rdmabuf, (_9P_RDMA_RECV_NUM+2)*_9P_RDMA_CHUNK_SIZE*sizeof(char), IBV_ACCESS_LOCAL_WRITE));



	msk_data_t *ackdata;
	TEST_NZ(ackdata = malloc(sizeof(msk_data_t)));
	ackdata->data = rdmabuf+(_9P_RDMA_RECV_NUM+1)*_9P_RDMA_CHUNK_SIZE*sizeof(char);
	ackdata->max_size = _9P_RDMA_CHUNK_SIZE*sizeof(char);
	ackdata->size = 1;
	ackdata->data[0] = 0;

	pthread_mutex_t lock;
	pthread_cond_t cond;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);

	msk_data_t **rdata;
	struct _9p_datamr *_9p_datamr;
	int i;

	TEST_NZ(rdata = malloc(_9P_RDMA_RECV_NUM*sizeof(msk_data_t*)));
	TEST_NZ(_9p_datamr = malloc(_9P_RDMA_RECV_NUM*sizeof(struct _9p_datamr)));

	for (i=0; i < _9P_RDMA_RECV_NUM; i++) {
		TEST_NZ(rdata[i] = malloc(sizeof(msk_data_t)));
		rdata[i]->data=rdmabuf+i*_9P_RDMA_CHUNK_SIZE*sizeof(char);
		rdata[i]->max_size=_9P_RDMA_CHUNK_SIZE*sizeof(char);
		_9p_datamr[i].data = rdata[i];
		_9p_datamr[i].mr = mr;
		_9p_datamr[i].ackdata = ackdata; 
		_9p_datamr[i].lock = &lock;
		_9p_datamr[i].cond = &cond;
		TEST_Z(msk_post_recv(trans, rdata[i], 1, mr, _9p_rdma_callback_recv, &(_9p_datamr[i])));
	}

	trans->private_data = _9p_datamr;

	if (trans->server) {
		TEST_Z(msk_finalize_accept(trans));
	} else {
		TEST_Z(msk_finalize_connect(trans));
	}

	TEST_NZ(wdata = malloc(sizeof(msk_data_t)));
	wdata->data = rdmabuf+_9P_RDMA_RECV_NUM*_9P_RDMA_CHUNK_SIZE*sizeof(char);
	wdata->max_size = _9P_RDMA_CHUNK_SIZE*sizeof(char);

	struct pollfd pollfd_stdin;
	pollfd_stdin.fd = 0; // stdin
	pollfd_stdin.events = POLLIN | POLLPRI;
	pollfd_stdin.revents = 0;

	while (trans->state == MSK_CONNECTED) {

		i = poll(&pollfd_stdin, 1, 100);

		if (i == -1)
			break;

		if (i == 0)
			continue;

		wdata->size = read(0, (char*)wdata->data, wdata->max_size);
		if (wdata->size == 0)
			break;

		pthread_mutex_lock(&lock);
		TEST_Z(msk_post_send(trans, wdata, 1, mr, NULL, NULL));
		pthread_cond_wait(&cond, &lock);
		pthread_mutex_unlock(&lock);
	}	


	msk_destroy_trans(&trans);

	pthread_exit(NULL);
}

