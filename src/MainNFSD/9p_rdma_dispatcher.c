/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_dispatcher.c
 * \date    $Date: 2006/02/23 12:33:05 $
 * \brief   The file that contain the '_9p_dispatcher_thread' routine for ganesha.
 *
 * 9p_dispatcher.c : The file that contain the '_9p_dispatcher_thread' routine for ganesha (and all
 * the related stuff).
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
#include <sys/file.h>           /* for having FNDELAY */
#include <sys/select.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <arpa/inet.h>
#include "HashTable.h"
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

#include <infiniband/arch.h>
#include <rdma/rdma_cma.h>
#include "mooshika.h"

#ifndef _USE_TIRPC_IPV6
  #define P_FAMILY AF_INET
#else
  #define P_FAMILY AF_INET6
#endif

/**
 * _9p_rdma_handle_trans_thr: 9P/RDMA listener
 * 
 * @param Arg : contains the child trans to be managed
 * 
 * @return NULL 
 * 
 */
static int _9p_rdma_register_buffer( msk_trans_t * trans, pthread_mutex_t * plock, pthread_cond_t * pcond, _9p_conn_t * pconn )
{
  uint8_t       * rdmabuf = NULL ;
  struct ibv_mr * mr      = NULL ;
  msk_data_t    * ackdata = NULL ;
  msk_data_t   ** rdata   = NULL ;
  struct _9p_datamr * datamr  = NULL ;
  unsigned int i = 0 ;
  int rc = 0 ;

  /* Alloc rdmabuf */
  if( ( rdmabuf = gsh_malloc( (_9P_RDMA_BUFF_NUM+2)*_9P_RDMA_CHUNK_SIZE)) == NULL ) 
    LogFatal( COMPONENT_9P, "9P/RDMA: trans handler could not malloc rdmabuf" ) ;

  /* Memset it to 0 (always useful) */
  memset( rdmabuf, 0, (_9P_RDMA_BUFF_NUM+2)*_9P_RDMA_CHUNK_SIZE);
  
  /* Register rdmabuf */
  if( ( mr = msk_reg_mr( trans, 
                         rdmabuf, 
                         (_9P_RDMA_BUFF_NUM+2)*_9P_RDMA_CHUNK_SIZE, 
                         IBV_ACCESS_LOCAL_WRITE)) == NULL  )
    LogFatal( COMPONENT_9P, "9P/RDMA: trans handler could not register rdmabuf" ) ;

  /* Get prepared to recv data */
 if( ( ackdata = gsh_malloc( sizeof( msk_data_t ) ) ) == NULL )
    LogFatal( COMPONENT_9P, "9P/RDMA: trans handler could not malloc ackdata" ) ;

  ackdata->data = rdmabuf+(_9P_RDMA_BUFF_NUM+1)*_9P_RDMA_CHUNK_SIZE;
  ackdata->max_size = _9P_RDMA_CHUNK_SIZE;
  ackdata->size = 1;
  ackdata->data[0] = 0;

  if( ( rdata = gsh_malloc( _9P_RDMA_BUFF_NUM * sizeof(msk_data_t* ) ) ) == NULL )
    LogFatal( COMPONENT_9P, "9P/RDMA: trans handler could not malloc rdata" ) ;

  if( (datamr = gsh_malloc(_9P_RDMA_BUFF_NUM*sizeof(struct _9p_datamr))) == NULL )
    LogFatal( COMPONENT_9P, "9P/RDMA: trans handler could not malloc datamr" ) ;

  for( i=0; i < _9P_RDMA_BUFF_NUM; i++)
   {
      if( ( rdata[i] = gsh_malloc( sizeof( msk_data_t ) ) ) == NULL )
         LogFatal( COMPONENT_9P, "9P/RDMA: trans handler could not malloc rdata[%u]", i ) ;

      rdata[i]->data=rdmabuf+i*_9P_RDMA_CHUNK_SIZE ;
      rdata[i]->max_size=_9P_RDMA_CHUNK_SIZE ;
      datamr[i].data = rdata[i];
      datamr[i].mr = mr;
      datamr[i].ackdata = ackdata;
      datamr[i].lock = plock;
      datamr[i].cond = pcond;
      datamr[i].pconn = pconn;

      if( i < _9P_RDMA_OUT )
       {
         if( ( rc = msk_post_recv( trans, 
                                   rdata[i], 
                                   mr, 
                                   _9p_rdma_callback_recv, 
                                  &(datamr[i]) ) ) != 0 )
           {
             LogEvent( COMPONENT_9P,  "9P/RDMA: trans handler could recv first byte of datamr[%u], rc=%u", i, rc ) ;
             goto exit ;
           }
       }
   } /*  for (unsigned int i=0; i < _9P_RDMA_BUFF_NUM; i++)  */

  for( i = 0 ; i <  _9P_RDMA_BUFF_NUM; i++)
   if(  i < _9P_RDMA_OUT )
      datamr[i].sender = &datamr[i+_9P_RDMA_OUT] ;
   else
      datamr[i].sender = NULL ;


  /* Finalize accept */
  if( ( rc = msk_finalize_accept( trans ) ) != 0 )
    {
      LogMajor( COMPONENT_9P, "9P/RDMA: trans handler could not finalize accept, rc=%u", rc ) ;
      goto exit ;
    }

  return 0 ; /* OK status */

exit:  
  for( i = 0 ; i < _9P_RDMA_BUFF_NUM ; i++ )
    if( !rdata[i] ) gsh_free( rdata[i] ) ;

  if( !datamr ) gsh_free( datamr ) ;
  if( !rdata ) gsh_free( rdata ) ;
  if( !ackdata ) gsh_free( ackdata ) ;
  if( !rdmabuf ) gsh_free( rdmabuf ) ;
  
  return 1 ;

} /* _9p_rdma_register_buffer */

/**
 * _9p_rdma_handle_trans_thr: 9P/RDMA listener
 * 
 * @param Arg : contains the child trans to be managed
 * 
 * @return NULL 
 * 
 */

/* Equivalent du _9p_socket_thread( */
void * _9p_rdma_thread( void * Arg )
{
  msk_trans_t   * trans   = Arg  ;
  pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER ;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER ;
  unsigned int i = 0 ;

  _9p_conn_t _9p_conn ;

  for (i = 0; i < FLUSH_BUCKETS; i++) 
   {
     pthread_mutex_init(&_9p_conn.flush_buckets[i].lock, NULL);
     init_glist(&_9p_conn.flush_buckets[i].list);
   }
  _9p_conn.sequence = 0 ;
  atomic_store_uint32_t(&_9p_conn.refcount, 0);

  if( _9p_rdma_register_buffer( trans, &lock, &cond, &_9p_conn ) )
   { 
      msk_destroy_trans( &trans ) ;
      pthread_exit(NULL);
   }

 
  while( trans->state == MSK_CONNECTED )
   {
     pthread_mutex_lock(&lock);

     pthread_cond_wait(&cond, &lock);
     pthread_mutex_unlock(&lock);
   }

  pthread_exit(NULL);
} /* _9p_rdma_handle_trans */


/**
 * _9p_rdma_dispatcher_thread: 9P/RDMA dispatcher
 *
 * @param Arg the socket number cast as a void * in pthread_create
 *
 * @return NULL 
 *
 */
void * _9p_rdma_dispatcher_thread( void * Arg )
{
  msk_trans_t *trans;
  msk_trans_t *child_trans;

  msk_trans_attr_t trans_attr;
  pthread_attr_t attr_thr ; 
  pthread_t thrid_handle_trans ;

  memset(&trans_attr, 0, sizeof(msk_trans_attr_t));

  trans_attr.server = _9P_RDMA_BACKLOG; 
  trans_attr.rq_depth = _9P_RDMA_BUFF_NUM+2;
  trans_attr.addr.sa_in.sin_family = AF_INET;
  trans_attr.addr.sa_in.sin_port =  htons(nfs_param._9p_param._9p_rdma_port) ;
  trans_attr.disconnect_callback = _9p_rdma_callback_disconnect;
  inet_pton(AF_INET, "0.0.0.0", &trans_attr.addr.sa_in.sin_addr);

  SetNameFunction("_9p_rdma_dispatch_thr" ) ;

  /* Calling dispatcher main loop */
  LogInfo(COMPONENT_9P_DISPATCH,
          "Entering 9P/RDMA dispatcher");

  LogDebug(COMPONENT_9P_DISPATCH,
           "My pthread id is %p", (caddr_t) pthread_self());

  /* Prepare attr_thr for dispatch */
  memset( (char *)&attr_thr, 0 , sizeof( attr_thr ) ) ;

  /* Set the pthread attributes */
  if( pthread_attr_init( &attr_thr ) )
   LogFatal( COMPONENT_9P, "9P/RDMA dispatcher could not init pthread_attr_t" ) ;

  if( pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) )
   LogFatal( COMPONENT_9P, "9P/RDMA dispatcher could not set pthread_attr_t:scope_system" ) ;

  if( pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE ) )
   LogFatal( COMPONENT_9P, "9P/RDMA dispatcher could not set pthread_attr_t:create_joignable" ) ;

  /* Init RDMA via mooshika */
  if( msk_init( &trans, &trans_attr ) )
    LogFatal( COMPONENT_9P, "9P/RDMA dispatcher could not start mooshika engine" ) ;
  else
    LogEvent( COMPONENT_9P, "Mooshika engine is started" ) ;

  /* Bind Mooshika */
  if( msk_bind_server(trans ) )
    LogFatal( COMPONENT_9P, "9P/RDMA dispatcher could not bind mooshika engine" ) ;
  else
    LogEvent( COMPONENT_9P, "Mooshika engine is bound" ) ;
 
 
  /* Start infinite loop here */
  while( 1 ) 
    {
      if( ( child_trans = msk_accept_one( trans ) ) == NULL )
        LogMajor( COMPONENT_9P, "9P/RDMA : dispatcher failed to accept a new client" ) ;
      else
       {
         if( pthread_create( &thrid_handle_trans, 
                             &attr_thr, 
                             _9p_rdma_thread, 
                             child_trans ) )
           LogMajor( COMPONENT_9P, "9P/RDMA : dispatcher accepted a new client but could not spawn a related thread" ) ;
         else
	   LogEvent( COMPONENT_9P, "9P/RDMA: thread #%u spawned to managed new child_trans", 
		     (unsigned int)thrid_handle_trans ) ;
       }
    } /* for( ;; ) */
 
   return NULL ;
} /* _9p_rdma_dispatcher_thread */


