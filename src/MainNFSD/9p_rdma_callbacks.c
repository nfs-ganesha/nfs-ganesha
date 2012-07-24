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

void _9p_rdma_do_recv(msk_trans_t *trans, struct _9p_datamr *_9p_datamr )
{
        msk_data_t *pdata = _9p_datamr->data;

        if (pdata->size != 1 || pdata->data[0] != '\0') {
                fprintf( stdout, "Received %u bytes:", pdata->size ) ;
                fflush(stdout);
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

void _9p_rdma_callback_recv_old(msk_trans_t *trans, void *arg) {
   struct _9p_datamr *_9p_datamr = arg;
  if (!_9p_datamr)
    {
	LogEvent( COMPONENT_9P, "no callback_arg in _9p_rdma_callback_recv");
	return;
    }
  _9p_rdma_do_recv( trans, _9p_datamr ) ;
}

void _9p_rdma_callback_recv(msk_trans_t *trans, void *arg) 
{
  struct _9p_datamr *_9p_datamr = arg;
  unsigned int worker_index;
  request_data_t *preq = NULL;


 if (!_9p_datamr) 
  {
     LogEvent( COMPONENT_9P, "no callback_arg in _9p_rdma_callback_recv");
     return;
  }

  /* choose a worker depending on its queue length */
  worker_index = nfs_core_select_worker_queue( WORKER_INDEX_ANY );

  /* Get a preq from the worker's pool */
  P(workers_data[worker_index].request_pool_mutex);

  preq = pool_alloc( request_pool, NULL ) ;

  V(workers_data[worker_index].request_pool_mutex);

   preq->rtype = _9P_RDMA ;
   preq->r_u._9p_rdma.rdma_conn.datamr = _9p_datamr ;
   preq->r_u._9p_rdma.rdma_conn.trans = trans ;
 
   DispatchWork9P( preq, worker_index ) ;
  //_9p_rdma_do_recv( trans, _9p_datamr ) ;
}

