/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * \file    nfs_dupreq.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:36:19 $
 * \version $Revision: 1.9 $
 * \brief   Prototypes for duplicate requsts cache management.
 *
 * nfs_dupreq.h : Prototypes for duplicate requsts cache management.
 *
 *
 */

#ifndef _NFS_DUPREQ_H
#define _NFS_DUPREQ_H

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#ifdef _SOLARIS
#ifndef _USE_SNMP
typedef unsigned long u_long;
#endif
#endif                          /* _SOLARIS */

#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"

typedef struct dupreq_entry__
{
  long xid;
  nfs_res_t res_nfs;
  u_long rq_prog;               /* service program number        */
  u_long rq_vers;               /* service protocol version      */
  u_long rq_proc;
  time_t timestamp;
  struct dupreq_entry__ *next_alloc;
} dupreq_entry_t;

unsigned int get_rpc_xid(struct svc_req *reqp);

int compare_xid(hash_buffer_t * buff1, hash_buffer_t * buff2);
int print_entry_dupreq(LRU_data_t data, char *str);
int clean_entry_dupreq(LRU_entry_t * pentry, void *addparam);
int nfs_dupreq_gc_function(LRU_entry_t * pentry, void *addparam);

nfs_res_t nfs_dupreq_get(long xid, int *pstatus);

int nfs_dupreq_add(long xid,
                   struct svc_req *ptr_req,
                   nfs_res_t * p_res_nfs,
                   LRU_list_t * lru_dupreq, dupreq_entry_t ** dupreq_pool);

unsigned long dupreq_value_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef);
unsigned long dupreq_rbt_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);
void nfs_dupreq_get_stats(hash_stat_t * phstat);

#define DUPREQ_SUCCESS             0
#define DUPREQ_INSERT_MALLOC_ERROR 1
#define DUPREQ_NOT_FOUND           2

#endif                          /* _NFS_DUPREQ_H */
