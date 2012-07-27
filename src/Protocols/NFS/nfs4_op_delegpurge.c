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
 * @file    nfs4_op_delegpurge.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
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
#include "HashData.h"
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
 * @brief NFS4_OP_DELEGPURGE
 *
 * This function implements the NFS4_OP_DELEGPURGE operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC 5661, pp. 363-4
 *
 */
#define arg_DELEGPURGE4 op->nfs_argop4_u.opdelegpurge
#define res_DELEGPURGE4 resp->nfs_resop4_u.opdelegpurge

int nfs4_op_delegpurge(struct nfs_argop4 *op,
                       compound_data_t *data,
                       struct nfs_resop4 *resp)
{
  /* Lock are not supported */
  resp->resop = NFS4_OP_DELEGPURGE;
  res_DELEGPURGE4.status = NFS4_OK;

  return res_DELEGPURGE4.status;
} /* nfs4_op_delegpurge */

/**
 * @brief Free memory allocated for DELEGPURGE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_DELEGPURGE operation.
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_delegpurge_Free(DELEGPURGE4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_delegpurge_Free */
