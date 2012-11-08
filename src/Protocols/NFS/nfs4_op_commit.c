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
 * \file    nfs4_op_commit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.10 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_commit.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#ifdef _PNFS_DS
#include "fsal_pnfs.h"
#endif /* _PNFS_DS */

#ifdef _PNFS_DS
static int op_dscommit(struct nfs_argop4 *op,
                       compound_data_t * data,
                       struct nfs_resop4 *resp);
#endif /* _PNFS_DS */


/**
 *
 * nfs4_op_commit: Implemtation of NFS4_OP_COMMIT
 *
 * Implemtation of NFS4_OP_COMMIT. This is usually made for cache validator implementation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK
 *
 */

#define arg_COMMIT4 op->nfs_argop4_u.opcommit
#define res_COMMIT4 resp->nfs_resop4_u.opcommit

int nfs4_op_commit(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_commit";

  cache_inode_status_t cache_status;

  /* for the moment, read/write are not done asynchronously, no commit is necessary */
  resp->resop = NFS4_OP_COMMIT;
  res_COMMIT4.status = NFS4_OK;

  LogFullDebug(COMPONENT_NFS_V4,
               "      COMMIT4: Commit order over offset = %"PRIu64", size = %"PRIu32,
               arg_COMMIT4.offset, (uint32_t)arg_COMMIT4.count);

  /* 
   * Do basic checks on a filehandle 
   * Commit is done only on a file
   */
  res_COMMIT4.status = nfs4_sanity_check_FH(data, REGULAR_FILE);
  if(res_COMMIT4.status != NFS4_OK)
    return res_COMMIT4.status;

#ifdef _PNFS_DS
  if((data->minorversion == 1) &&
     (nfs4_Is_Fh_DSHandle(&data->currentFH)))
    {
      return(op_dscommit(op, data, resp));
    }
#endif /* _PNFS_DS */

  // FIX ME!! At the moment we just assume the user is _not_ using
  // the ganesha unsafe buffer. In the future, a check based on
  // export config params (similar to nfs3_Commit.c) should be made.
  if(cache_inode_commit(data->current_entry,
                        arg_COMMIT4.offset,
                        arg_COMMIT4.count,
                        CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER,
                        data->pcontext,
                        &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_COMMIT4.status = nfs4_Errno(cache_status);
      return res_COMMIT4.status;
    }

  memcpy(res_COMMIT4.COMMIT4res_u.resok4.writeverf, (char *)&NFS4_write_verifier,
         NFS4_VERIFIER_SIZE);

  /* If you reach this point, then an error occured */
  res_COMMIT4.status = NFS4_OK;
  return res_COMMIT4.status;
}                               /* nfs4_op_commit */

/**
 * nfs4_op_commit_Free: frees what was allocared to handle nfs4_op_commit.
 *
 * Frees what was allocared to handle nfs4_op_commit.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_commit_Free(COMMIT4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_commit_Free */

#ifdef _PNFS_DS
/**
 *
 * op_dscommit: Call pNFS data server commit
 *
 * @param op    [IN]     pointer to nfs4_op arguments
 * @param data  [IN/OUT] Pointer to the compound request's data
 * @param resp  [IN]     Pointer to nfs4_op results
 *
 * @return NFS4_OK
 *
 */

static int op_dscommit(struct nfs_argop4 *op,
                       compound_data_t * data,
                       struct nfs_resop4 *resp)
{
  /* FSAL file handle */
  fsal_handle_t handle;
  struct fsal_handle_desc fh_desc;
  /* NFSv4 status code */
  nfsstat4 nfs_status = 0;

  /* Construct the FSAL file handle */

  if ((nfs4_FhandleToFSAL(&data->currentFH,
                          &fh_desc,
                          data->pcontext)) == 0)
    {
      res_COMMIT4.status = NFS4ERR_INVAL;
      return res_COMMIT4.status;
    }

  memset(&handle, 0, sizeof(handle));
  memcpy(&handle, fh_desc.start, fh_desc.len);

  /* Call the commit operation */

  nfs_status = fsal_dsfunctions
    .DS_commit(&handle,
               data->pcontext,
               arg_COMMIT4.offset,
               arg_COMMIT4.count,
               &res_COMMIT4.COMMIT4res_u.resok4.writeverf);

  res_COMMIT4.status = nfs_status;
  return res_COMMIT4.status;
}

#endif /* _PNFS_DS */
