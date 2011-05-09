/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    nfs41_op_sequence.c
 * \author  $Author: deniel $
 * \brief   Routines used for managing the NFS4_OP_SEQUENCE operation.
 *
 * nfs41_op_sequence.c : Routines used for managing the NFS4_OP_SEQUENCE operation.
 *
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
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"

/**
 *
 * nfs41_op_set_ssv: the NFS4_OP_SET_SSV operation
 *
 * This functions handles the NFS4_OP_SET_SSV operation in NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs4_op_<*> function
 * @see nfs4_Compound
 *
 */
int nfs41_op_set_ssv(struct nfs_argop4 *op,
                     compound_data_t * data, struct nfs_resop4 *resp)
{
#define arg_SET_SSV4  op->nfs_argop4_u.opset_ssv
#define res_SET_SSV4  resp->nfs_resop4_u.opset_ssv

  resp->resop = NFS4_OP_SET_SSV;
  res_SET_SSV4.ssr_status = NFS4_OK;

  return res_SET_SSV4.ssr_status;       /* I know this is pretty dirty... But this is an early implementation... */
}                               /* nfs41_op_set_ssv */

/**
 * nfs41_op_set_ssv_Free: frees what was allocared to handle nfs41_op_set_ssv.
 * 
 * Frees what was allocared to handle nfs41_op_set_ssv.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_set_ssv_Free(SET_SSV4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_set_ssv_Free */
