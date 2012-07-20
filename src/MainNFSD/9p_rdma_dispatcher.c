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
#include "HashData.h"
#include "HashTable.h"
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

#ifndef _USE_TIRPC_IPV6
  #define P_FAMILY AF_INET
#else
  #define P_FAMILY AF_INET6
#endif

/**
 * _9p_rdma_dispatcher_thread: 9P/RDMA listener and dispatcher
 *
 * @param Arg the socket number cast as a void * in pthread_create
 *
 * @return NULL 
 *
 */


void * _9p_rdma_dispatcher_thread( void * Arg )
{
  //msk_trans_t *trans;
  //msk_trans_t *child_trans;

  msk_trans_attr_t trans_attr;

  memset(&trans_attr, 0, sizeof(msk_trans_attr_t));

  trans_attr.server = 10;  /** @todo : why 10 ? To be seen with Dominique */

  trans_attr.rq_depth = _9P_RDMA_RECV_NUM+2;
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

 /* Start infinite loop here */
 for( ;; ) 
   {
 
      sleep( 1 ) ; 
   } /* for( ;; ) */
 
  return NULL ;
} /* _9p_rdma_dispatcher_thread */


