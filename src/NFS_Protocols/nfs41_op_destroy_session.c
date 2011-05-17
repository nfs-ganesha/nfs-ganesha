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
 * \file    nfs4_op_destroy_session.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 17:02:52 $
 * \brief   Routines used for managing the NFS4_OP_DESTROY_SESSION operation.
 *
 * nfs4_op_destroy_session.c :  Routines used for managing the NFS4_OP_DESTROY_SESSION operation.
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
#include "nfs_file_handle.h"
#include "nfs_tools.h"

/**
 *
 * nfs4_op_destroy_session:  The NFS4_OP_DESTROY_SESSION operation.
 *
 * The NFS4_OP_DESTROY_SESSION operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see nfs4_Compound
 *
 */

int nfs41_op_destroy_session(struct nfs_argop4 *op,
                             compound_data_t * data, struct nfs_resop4 *resp)
{

#define arg_DESTROY_SESSION4 op->nfs_argop4_u.opdestroy_session
#define res_DESTROY_SESSION4 resp->nfs_resop4_u.opdestroy_session

  resp->resop = NFS4_OP_DESTROY_SESSION;
  res_DESTROY_SESSION4.dsr_status = NFS4_OK;

  if(!nfs41_Session_Del(arg_DESTROY_SESSION4.dsa_sessionid))
    res_DESTROY_SESSION4.dsr_status = NFS4ERR_BADSESSION;
  else
    res_DESTROY_SESSION4.dsr_status = NFS4_OK;

  return res_DESTROY_SESSION4.dsr_status;
}                               /* nfs41_op_destroy_session */

/**
 * nfs41_op_destroy_session_Free: frees what was allocared to handle nfs41_op_destroy_session.
 * 
 * Frees what was allocared to handle nfs41_op_destroy_session.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_destroy_session_Free(DESTROY_SESSION4res * resp)
{
  /* To be completed */
  return;
}                               /* nfs41_op_destroy_session_Free */
