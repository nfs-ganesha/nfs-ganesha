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
#include "abstract_atomic.h"
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

#include <mooshika.h>

void DispatchWork9P( request_data_t *preq );

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

void _9p_rdma_process_request( _9p_request_data_t * preq9p, nfs_worker_data_t * pworker_data ) 
{
  msk_trans_t * trans =  preq9p->pconn->trans_data.rdma_ep.trans ;

  msk_data_t *pdata = preq9p->pconn->trans_data.rdma_ep.datamr->data ;
  _9p_datamr_t * datamr =  preq9p->pconn->trans_data.rdma_ep.datamr ;

  _9p_datamr_t * outdatamr =  preq9p->pconn->trans_data.rdma_ep.datamr->sender ;
  msk_data_t *poutdata =   preq9p->pconn->trans_data.rdma_ep.datamr->sender->data ;



  uint32_t * p_9pmsglen = NULL ;
  u32 outdatalen = 0 ;
  int rc = 0 ; 

  if (pdata->size != 1 || pdata->data[0] != '\0')
   {
      printf( "Received %u bytes:\n", pdata->size ) ;

      if( pdata->size < _9P_HDR_SIZE )
	  LogMajor( COMPONENT_9P, "Malformed 9P/RDMA packet, bad header size" ) ;
      else
        {
	  p_9pmsglen = (uint32_t *)pdata->data ;

          outdatalen = _9P_MSG_SIZE  -  _9P_HDR_SIZE ;
          LogFullDebug( COMPONENT_9P,
                        "Received 9P/RDMA message of size %u",
                         *p_9pmsglen ) ;

          /* Use buffer received via RDMA as a 9P message */
          preq9p->_9pmsg = pdata->data ;

          if ( ( rc = _9p_process_buffer( preq9p, pworker_data, poutdata->data, &outdatalen ) ) != 1 )
           {
             LogMajor( COMPONENT_9P, "Could not process 9P buffer on socket #%lu", preq9p->pconn->trans_data.sockfd ) ;
           }
          else
           {
                 poutdata->size = outdatalen ;
                 msk_post_send( trans, poutdata, outdatamr->mr, _9p_rdma_callback_send, NULL ) ;
           }
           _9p_DiscardFlushHook(preq9p);
         }

      /* Mark the buffer ready for later recv */
      msk_post_recv(trans, pdata, datamr->mr, _9p_rdma_callback_recv, datamr);
   } 
  else
   {
      msk_post_recv(trans, pdata, datamr->mr, _9p_rdma_callback_recv, datamr);

      pthread_mutex_lock(datamr->lock);
      pthread_cond_signal(datamr->cond);
      pthread_mutex_unlock(datamr->lock);
   }
}

void _9p_rdma_callback_recv(msk_trans_t *trans, void *arg) 
{
  struct _9p_datamr *_9p_datamr = arg;
  request_data_t *preq = NULL;
  u16 tag = 0 ;
  char * _9pmsg = NULL ;

  if (!_9p_datamr) 
   {
      LogEvent( COMPONENT_9P, "no callback_arg in _9p_rdma_callback_recv");
      return;
   }

  preq = pool_alloc( request_pool, NULL ) ;
 
  preq->rtype = _9P_REQUEST ;
  preq->r_u._9p._9pmsg = _9pmsg;
  preq->r_u._9p.pconn = (_9p_conn_t *)_9p_datamr->pconn ;  
  preq->r_u._9p.pconn->trans_type = _9P_RDMA ;
  preq->r_u._9p.pconn->trans_data.rdma_ep.datamr = _9p_datamr ;
  preq->r_u._9p.pconn->trans_data.rdma_ep.trans = trans ;
  

  /* Add this request to the request list, should it be flushed later. */
  _9pmsg = _9p_datamr->data->data ;
  tag = *(u16*) (_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE);
  _9p_AddFlushHook(&preq->r_u._9p, tag, preq->r_u._9p.pconn->sequence++);

  DispatchWork9P( preq ) ;
} /* _9p_rdma_callback_recv */

