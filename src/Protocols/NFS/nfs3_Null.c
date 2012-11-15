/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * @file  nfs3_Null.c
 * @brief NFSv3 NULL procedure.
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
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"

/**
 * @brief The NFS proc null function, for all versions.
 *
 * The NFS proc null function, for all versions.
 *
 * @param[in]  arg        ignored
 * @param[in]  exportlist ignored
 * @param[in]  creds      ignored
 * @param[in]  worker     ignored
 * @param[in]  req        ignored
 * @param[out] res        ignored
 */

int nfs_Null(nfs_arg_t *arg,
             exportlist_t *export,
	     struct req_op_context *req_ctx,
             nfs_worker_data_t *worker,
             struct svc_req *req,
             nfs_res_t *res)
{
  LogDebug(COMPONENT_NFSPROTO, "REQUEST PROCESSING: Calling nfs_Null");
  return NFS_OK;
} /* nfs_cb_Null */

/**
 * @brief Free memory allocated for the nfs4_cb_Null result
 *
 * This function frees any memory allocated for the result of the
 * nfs4_cb_Null operation.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs_Null_Free(nfs_res_t *res)
{
  /* Nothing to do here */
  return;
} /* nfs4_cb_Null_Free */
