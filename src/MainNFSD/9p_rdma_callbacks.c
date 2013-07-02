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
#include "config.h"
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
#include "9p.h"

#include <mooshika.h>

void DispatchWork9P( request_data_t *preq );

void _9p_rdma_callback_send(msk_trans_t *trans, msk_data_t *pdata, void *arg) {
  _9p_datalock_t * outdatalock =  (_9p_datalock_t*) arg;
  pthread_mutex_unlock(&outdatalock->lock);
}

void _9p_rdma_callback_send_err(msk_trans_t *trans, msk_data_t *pdata, void *arg) {
  _9p_datalock_t * outdatalock =  (_9p_datalock_t*) arg;

  /** @todo: This should probably try to send again a few times before unlocking */

  pthread_mutex_unlock(&outdatalock->lock);
}

void _9p_rdma_callback_recv_err(msk_trans_t *trans, msk_data_t *pdata, void *arg) {

  if( trans->state == MSK_CONNECTED )
    msk_post_recv(trans, pdata, _9p_rdma_callback_recv, _9p_rdma_callback_recv_err, arg);
}

void _9p_rdma_callback_disconnect(msk_trans_t *trans) {
  if( !trans || !trans->private_data )
    return;

  _9p_rdma_cleanup_conn(trans);
}

void _9p_rdma_process_request( _9p_request_data_t * preq9p, nfs_worker_data_t * pworker_data ) 
{
  msk_trans_t * trans =  preq9p->pconn->trans_data.rdma_trans ;


  msk_data_t *pdata = preq9p->datalock->data ;
  _9p_datalock_t * datalock =  preq9p->datalock ;

  /** @todo: don't need another datalock for sender, only put data there */
  _9p_datalock_t * outdatalock =  preq9p->datalock->sender ;
  msk_data_t *poutdata =   outdatalock->data ;



  uint32_t * p_9pmsglen = NULL ;
  u32 outdatalen = 0 ;
  int rc = 0 ; 

  if( pdata->size < _9P_HDR_SIZE )
   {
      LogMajor( COMPONENT_9P, "Malformed 9P/RDMA packet, bad header size" ) ;
      msk_post_recv(trans, pdata, _9p_rdma_callback_recv, _9p_rdma_callback_recv_err, datalock);
   }
  else
   {
      p_9pmsglen = (uint32_t *)pdata->data ;

      LogFullDebug( COMPONENT_9P,
                    "Received 9P/RDMA message of size %u",
                     *p_9pmsglen ) ;

      /* Use buffer received via RDMA as a 9P message */
      preq9p->_9pmsg = pdata->data ;

      /* We start using the send buffer. Lock it. */
      pthread_mutex_lock(&outdatalock->lock);

      if ( ( rc = _9p_process_buffer( preq9p, pworker_data, poutdata->data, &outdatalen ) ) != 1 )
       {
         LogMajor( COMPONENT_9P, "Could not process 9P buffer on socket #%lu", preq9p->pconn->trans_data.sockfd ) ;
       }

      /* Mark the buffer ready for later receive and post the reply */
      msk_post_recv(trans, pdata, _9p_rdma_callback_recv, _9p_rdma_callback_recv_err, datalock);

      /* If earlier processing succeeded, post it */
      if (rc == 1)
       {
         poutdata->size = outdatalen ;
         if (0 != msk_post_send( trans, poutdata, _9p_rdma_callback_send, _9p_rdma_callback_send_err, (void*) outdatalock ))
                 rc = -1;
       } 
       
       if (rc != 1)  {
             /* Unlock the buffer right away since no message is being sent */
             pthread_mutex_unlock(&outdatalock->lock);
       }


      _9p_DiscardFlushHook(preq9p);
   }
}

void _9p_rdma_callback_recv(msk_trans_t *trans, msk_data_t *pdata, void *arg)
{
  struct _9p_datalock *_9p_datalock = arg;
  request_data_t *preq = NULL;
  u16 tag = 0 ;
  char * _9pmsg = NULL ;

  if (!_9p_datalock)
   {
      LogEvent( COMPONENT_9P, "no callback_arg in _9p_rdma_callback_recv");
      return;
   }

  preq = pool_alloc( request_pool, NULL ) ;
 
  preq->rtype = _9P_REQUEST ;
  preq->r_u._9p._9pmsg = _9pmsg;
  preq->r_u._9p.pconn = _9p_rdma_priv_of(trans)->pconn;
  preq->r_u._9p.datalock = _9p_datalock ;

  /* Add this request to the request list, should it be flushed later. */
  _9pmsg = pdata->data ;
  tag = *(u16*) (_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE);
  _9p_AddFlushHook(&preq->r_u._9p, tag, preq->r_u._9p.pconn->sequence++);

  DispatchWork9P( preq ) ;
} /* _9p_rdma_callback_recv */
