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

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#endif

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"

#define NFS_V2_NB_COMMAND 18
static char *nfsv2_function_names[] = {
  "NFSv2_null", "NFSv2_getattr", "NFSv2_setattr", "NFSv2_root",
  "NFSv2_lookup", "NFSv2_readlink", "NFSv2_read", "NFSv2_writecache",
  "NFSv2_write", "NFSv2_create", "NFSv2_remove", "NFSv2_rename",
  "NFSv2_link", "NFSv2_symlink", "NFSv2_mkdir", "NFSv2_rmdir",
  "NFSv2_readdir", "NFSv2_statfs"
};

#define NFS_V3_NB_COMMAND 22
static char *nfsv3_function_names[] = {
  "NFSv3_null", "NFSv3_getattr", "NFSv3_setattr", "NFSv3_lookup",
  "NFSv3_access", "NFSv3_readlink", "NFSv3_read", "NFSv3_write",
  "NFSv3_create", "NFSv3_mkdir", "NFSv3_symlink", "NFSv3_mknod",
  "NFSv3_remove", "NFSv3_rmdir", "NFSv3_rename", "NFSv3_link",
  "NFSv3_readdir", "NFSv3_readdirplus", "NFSv3_fsstat",
  "NFSv3_fsinfo", "NFSv3_pathconf", "NFSv3_commit"
};

#define NFS_V4_NB_COMMAND 2
static char *nfsv4_function_names[] = {
  "NFSv4_null", "NFSv4_compound"
};

#define MNT_V1_NB_COMMAND 6
#define MNT_V3_NB_COMMAND 6
static char *mnt_function_names[] = {
  "MNT_null", "MNT_mount", "MNT_dump", "MNT_umount", "MNT_umountall", "MNT_export"
};

#define RQUOTA_NB_COMMAND 5
static char *rquota_functions_names[] = {
  "rquota_Null", "rquota_getquota", "rquota_getquotaspecific", "rquota_setquota",
  "rquota_setquotaspecific"
};

#define NFS_V40_NB_OPERATION 39
#define NFS_V41_NB_OPERATION 58

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

typedef struct nfs_request_latency_stat__
{
  unsigned int latency;
} nfs_request_latency_stat_t;

void nfs_stat_update(nfs_stat_type_t type,
                     nfs_request_stat_t * pstat_req, struct svc_req *preq,
                     nfs_request_latency_stat_t * lstat_req);

#endif                          /* _NFS_STAT_H */
