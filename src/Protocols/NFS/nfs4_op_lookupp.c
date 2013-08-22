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
 * @file    nfs4_op_lookupp.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"

/**
 * @brief NFS4_OP_LOOKUPP
 *
 * This function implements the NFS4_OP_LOOKUPP operation, which looks
 * up the parent of the supplied directory.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 369
 *
 */
int nfs4_op_lookupp(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  LOOKUPP4res *const res_LOOKUPP4 = &resp->nfs_resop4_u.oplookupp;
  cache_entry_t        * dir_entry = NULL;
  cache_entry_t        * file_entry = NULL;
  cache_inode_status_t   cache_status = CACHE_INODE_SUCCESS;

  resp->resop = NFS4_OP_LOOKUPP;
  res_LOOKUPP4->status = NFS4_OK;

  /* Do basic checks on a filehandle */
  res_LOOKUPP4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);
  if(res_LOOKUPP4->status != NFS4_OK)
    return res_LOOKUPP4->status;

  /* looking up for parent directory from ROOTFH return NFS4ERR_NOENT (RFC3530, page 166) */
  if(data->currentFH.nfs_fh4_len == data->rootFH.nfs_fh4_len
     && memcmp(data->currentFH.nfs_fh4_val, data->rootFH.nfs_fh4_val,
               data->currentFH.nfs_fh4_len) == 0)
    {
      /* Nothing to do, just reply with success */
      res_LOOKUPP4->status = NFS4ERR_NOENT;
      return res_LOOKUPP4->status;
    }

  /* If in pseudoFS, proceed with pseudoFS specific functions */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_lookupp_pseudo(op, data, resp);

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_lookupp_xattr(op, data, resp);

  /* If Filehandle points to the root of the current export, then backup
   * through junction into the pseudo file system.
   *
   * @todo FSF: eventually we need to support junctions between exports
   *            and that will require different code here.
   */
  if(data->current_entry->type == DIRECTORY &&
     data->current_entry == data->req_ctx->export->export.exp_root_cache_inode)
    return nfs4_op_lookupp_pseudo_by_exp(op, data, resp);

  /* Preparing for cache_inode_lookup ".." */
  file_entry = NULL;
  dir_entry = data->current_entry;

  cache_status = cache_inode_lookupp(dir_entry,
				     data->req_ctx,
				     &file_entry);
  if (file_entry != NULL)
    {
      /* Convert it to a file handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, file_entry->obj_handle))
        {
          res_LOOKUPP4->status = NFS4ERR_SERVERFAULT;
          cache_inode_put(file_entry);
          return res_LOOKUPP4->status;
        }

      /* Release dir_pentry, as it is not reachable from anywhere in
         compound after this function returns.  Count on later
         operations or nfs4_Compound to clean up current_entry. */

      if (dir_entry)
        cache_inode_put(dir_entry);

      /* Keep the pointer within the compound data */
      data->current_entry = file_entry;
      data->current_filetype = file_entry->type;

      /* Return successfully */
      res_LOOKUPP4->status = NFS4_OK;
      return NFS4_OK;

    }

  /* If the part of the code is reached, then something wrong occured in the
   * lookup process, status is not HPSS_E_NOERROR and contains the code for
   * the error */

  /* For any wrong file type LOOKUPP should return NFS4ERR_NOTDIR */
  res_LOOKUPP4->status = nfs4_Errno(cache_status);

  return res_LOOKUPP4->status;
}                               /* nfs4_op_lookupp */

/**
 * @brief Free memory allocated for LOOKUPP result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOOKUPP operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_lookupp_Free(nfs_resop4 *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_lookupp_Free */
