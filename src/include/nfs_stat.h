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
 * \file    nfs_stat.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:15 $
 * \version $Revision: 1.6 $
 * \brief   Functions to be used for nfs and mount statistics
 *
 * nfs_stat.h :  Functions to be used for nfs and mount statistics.
 *
 *
 */

#ifndef _NFS_STAT_H
#define _NFS_STAT_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include "rpc.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"

#define NFS_V2_NB_COMMAND 18
extern char *nfsv2_function_names[];

#define NFS_V3_NB_COMMAND 22
extern char *nfsv3_function_names[];

#define NFS_V4_NB_COMMAND 2
extern char *nfsv4_function_names[];

#define MNT_V1_NB_COMMAND 6
#define MNT_V3_NB_COMMAND 6
extern char *mnt_function_names[];

#define RQUOTA_NB_COMMAND 5
extern char *rquota_functions_names[];

#define NFS_V40_NB_OPERATION 39
#define NFS_V41_NB_OPERATION 58

#define ERR_STAT_NO_ERROR 0
#define ERR_STAT_ERROR    1

typedef enum nfs_stat_type__
{ GANESHA_STAT_SUCCESS = 0,
  GANESHA_STAT_DROP = 1
} nfs_stat_type_t;

/* we support only upto NLMPROC4_UNLOCK */
#define NLM_V4_NB_OPERATION 5

typedef struct nfs_op_stat_item__
{
  unsigned int total;
  unsigned int success;
  unsigned int failed;
} nfs_op_stat_item_t;

typedef struct nfs_request_stat_item__
{
  unsigned int total;
  unsigned int success;
  unsigned int dropped;
  unsigned int tot_latency;
  unsigned int min_latency;
  unsigned int max_latency;
  unsigned int tot_await_time;
} nfs_request_stat_item_t;

typedef struct nfs_request_stat__
{
  unsigned int nb_mnt1_req;
  unsigned int nb_mnt3_req;
  unsigned int nb_nfs2_req;
  unsigned int nb_nfs3_req;
  unsigned int nb_nfs4_req;
  unsigned int nb_nfs40_op;
  unsigned int nb_nfs41_op;
  unsigned int nb_nlm4_req;
  unsigned int nb_rquota1_req;
  unsigned int nb_rquota2_req;
  nfs_request_stat_item_t stat_req_mnt1[MNT_V1_NB_COMMAND];
  nfs_request_stat_item_t stat_req_mnt3[MNT_V3_NB_COMMAND];
  nfs_request_stat_item_t stat_req_nfs2[NFS_V2_NB_COMMAND];
  nfs_request_stat_item_t stat_req_nfs3[NFS_V3_NB_COMMAND];
  nfs_request_stat_item_t stat_req_nfs4[NFS_V4_NB_COMMAND];
  nfs_op_stat_item_t stat_op_nfs40[NFS_V40_NB_OPERATION];
  nfs_op_stat_item_t stat_op_nfs41[NFS_V41_NB_OPERATION];
  nfs_request_stat_item_t stat_req_nlm4[NLM_V4_NB_OPERATION];
  nfs_request_stat_item_t stat_req_rquota1[RQUOTA_NB_COMMAND];
  nfs_request_stat_item_t stat_req_rquota2[RQUOTA_NB_COMMAND];
} nfs_request_stat_t;

typedef enum
{
  SVC_TIME = 0,
  AWAIT_TIME
} nfs_stat_latency_type_t;

typedef struct nfs_request_latency_stat__
{
  nfs_stat_latency_type_t type;
  unsigned int latency;
} nfs_request_latency_stat_t;

typedef enum
{
  PER_SERVER = 0,
  PER_SERVER_DETAIL,
  PER_CLIENT,
  PER_SHARE,
  PER_CLIENTSHARE
} nfs_stat_client_req_type_t;

typedef struct
{
  int nfs_version;
  nfs_stat_client_req_type_t stat_type;
  char client_name[1024];
  char share_name[1024];
} nfs_stat_client_req_t;

void nfs_stat_update(nfs_stat_type_t type,
                     nfs_request_stat_t * pstat_req, struct svc_req *preq,
                     nfs_request_latency_stat_t * lstat_req);

void set_min_latency(nfs_request_stat_item_t *cur_stat, unsigned int val);

void set_max_latency(nfs_request_stat_item_t *cur_stat, unsigned int val);

struct timeval time_diff(struct timeval time_from, struct timeval time_to);

#endif                          /* _NFS_STAT_H */
