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
 * @file    nfs4_op_restorefh.c
 * @brief   The NFS4_OP_RESTOREFH operation.
 *
 * Routines used for managing the NFS4_OP_RESTOREFH operation.
 */

#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFS4_OP_RESTOREFH operation.
 *
 * This functions handles the NFS4_OP_RESTOREFH operation in
 * NFSv4. This function can be called only from nfs4_Compound.  This
 * operation replaces the current FH with the previously saved FH.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_restorefh(struct nfs_argop4 *op,
                      compound_data_t * data, struct nfs_resop4 *resp)
{
  RESTOREFH4res *const res_RESTOREFH = &resp->nfs_resop4_u.oprestorefh;
  /* First of all, set the reply to zero to make sure it contains no
     parasite information */
  memset(resp, 0, sizeof(struct nfs_resop4));

  resp->resop = NFS4_OP_RESTOREFH;
  res_RESTOREFH->status = NFS4_OK;

  /* If there is no currentFH, then return an error */
  res_RESTOREFH->status = nfs4_sanity_check_saved_FH(data,
						     NO_FILE_TYPE,
						     true);
  if (res_RESTOREFH->status != NFS4_OK)
    return res_RESTOREFH->status;

  /* Copy the data from current FH to saved FH */
  memcpy(data->currentFH.nfs_fh4_val,
         data->savedFH.nfs_fh4_val,
         data->savedFH.nfs_fh4_len);

  data->currentFH.nfs_fh4_len = data->savedFH.nfs_fh4_len;

  if(data->req_ctx->export != NULL) {
      put_gsh_export(data->req_ctx->export);
  }
  /* Restore the export information */
  data->req_ctx->export = data->saved_export;
  data->saved_export = NULL;
  data->pexport      = &data->req_ctx->export->export;
  data->export_perms = data->saved_export_perms;

  /* No need to call nfs4_SetCompoundExport or nfs4_MakeCred
   * because we are restoring saved information, and the
   * credential checking may be skipped.
   */

  /* If current and saved entry are identical, get no references and
     make no changes. */

  if (data->current_entry == data->saved_entry) {
      goto out;
  }

  if (data->current_entry) {
      cache_inode_put(data->current_entry);
      data->current_entry = NULL;
  }
  if (data->current_ds) {
      data->current_ds->ops->put(data->current_ds);
      data->current_ds = NULL;
  }

  data->current_entry = data->saved_entry;
  data->current_filetype = data->saved_filetype;

  /* Take another reference.  As of now the filehandle is both saved
     and current and both must be counted.  Protect in case of
     pseudofs handle. */

  if (data->current_entry)
      cache_inode_lru_ref(data->current_entry, LRU_FLAG_NONE);

 out:

  if(isFullDebug(COMPONENT_NFS_V4))
    {
      char str[LEN_FH_STR];
      sprint_fhandle4(str, &data->currentFH);
      LogFullDebug(COMPONENT_NFS_V4,
                   "RESTORE FH: Current FH %s", str);
    }


  return NFS4_OK;
} /* nfs4_op_restorefh */

/**
 * @brief Free memory allocated for RESTOREFH result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_RESTOREFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_restorefh_Free(nfs_resop4 *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_restorefh_Free */
