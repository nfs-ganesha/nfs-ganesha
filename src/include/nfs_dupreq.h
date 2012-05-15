/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * \file    nfs_dupreq.h
 * \brief   Prototypes for duplicate requsts cache management.
 *
 * Prototypes for duplicate requsts cache management.
 *
 *
 */

#ifndef _NFS_DUPREQ_H
#define _NFS_DUPREQ_H

#ifdef _SOLARIS
#ifndef _USE_SNMP
typedef unsigned long u_long;
#endif
#endif                          /* _SOLARIS */

#include "ganesha_rpc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"

typedef struct dupreq_key__
{
  /* Each NFS request is identified by the client by an xid.
   * The same xids can be recycled by the same client or used
   * by different clients ... it's too weak to make the dup req
   * cache useful. */
  long xid;

  /* The IP and port are also used to identify duplicate requests.
   * This is much much stronger. */
  sockaddr_t addr;

  /* In very rare cases, ip/port/xid is not enough. In databases
   * and other specific applications this may be a greater concern.
   * In those cases a checksum of the first 200 bytes of the request
   * should be used */
  int checksum;
} dupreq_key_t;

typedef struct dupreq_entry__
{
  long xid;
  sockaddr_t addr;
  int checksum;
  int ipproto ;

  pthread_mutex_t dupreq_mutex;
  int processing; /* if currently being processed, this should be = 1 */

  nfs_res_t res_nfs;
  u_long rq_prog;               /* service program number        */
  u_long rq_vers;               /* service protocol version      */
  u_long rq_proc;
  time_t timestamp;
} dupreq_entry_t;

int compare_req(hash_buffer_t *buff1, hash_buffer_t *buff2);
int print_entry_dupreq(LRU_data_t data, char *str);
int clean_entry_dupreq(LRU_entry_t *pentry, void *addparam);
int nfs_dupreq_gc_function(LRU_entry_t *pentry, void *addparam);

nfs_res_t nfs_dupreq_get(struct svc_req *req, int *pstatus);
int nfs_dupreq_delete(struct svc_req *req);
int nfs_dupreq_add_not_finished(struct svc_req *req, nfs_res_t *res_nfs);

int nfs_dupreq_finish(struct svc_req *req, nfs_res_t *p_res_nfs,
                      LRU_list_t *lru_dupreq);

uint32_t dupreq_value_hash_func(hash_parameter_t *p_hparam,
                                     hash_buffer_t *buffclef);
uint64_t dupreq_rbt_hash_func(hash_parameter_t *p_hparam, hash_buffer_t *buffclef);
void nfs_dupreq_get_stats(hash_stat_t *phstat_udp, hash_stat_t *phstat_tcp ) ;

typedef enum dupreq_status
{
    DUPREQ_SUCCESS = 0,
    DUPREQ_INSERT_MALLOC_ERROR,
    DUPREQ_NOT_FOUND,
    DUPREQ_BEING_PROCESSED,
    DUPREQ_ALREADY_EXISTS,
} dupreq_status_t;

#endif                          /* _NFS_DUPREQ_H */
