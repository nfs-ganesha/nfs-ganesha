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
 * @file    nfs4_op_putrootfh.c
 * @brief   Routines used for managing the NFS4_OP_PUTROOTFH operation.
 *
 * Routines used for managing the NFS4_OP_PUTROOTFH operation.
 */
#include "config.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"

/**
 * @brief Create the pseudo FS root filehandle
 *
 * This function creates the pseudo FS root filehandle.
 *
 * @param[out]    fh   File handle to be built
 * @param[in,out] data Request compound data
 *
 * @retval NFS4_OK if successful.
 * @retval NFS4ERR_BADHANDLE otherwise.
 *
 * @see nfs4_op_putrootfh
 *
 */

static int CreateROOTFH4(nfs_fh4 *fh, compound_data_t *data)
{
  pseudofs_entry_t psfsentry;
  int              status = 0;

  psfsentry = *(data->pseudofs->reverse_tab[0]);

  /* If rootFH already set, return success */
  if(data->rootFH.nfs_fh4_len != 0)
    return NFS4_OK;

  if((status = nfs4_AllocateFH(&(data->rootFH))) != NFS4_OK)
    return status;

  if(!nfs4_PseudoToFhandle(&(data->rootFH), &psfsentry))
    return NFS4ERR_BADHANDLE;

  LogHandleNFS4("CREATE ROOT FH: ", &data->rootFH);

  return NFS4_OK;
}                               /* CreateROOTFH4 */

/**
 *
 * @brief The NFS4_OP_PUTROOTFH operation.
 *
 * This functions handles the NFS4_OP_PUTROOTFH operation in
 * NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 371
 *
 * @see CreateROOTFH4
 *
 */

int nfs4_op_putrootfh(struct nfs_argop4 *op,
                      compound_data_t *data,
                      struct nfs_resop4 *resp)
{
  PUTROOTFH4res *const res_PUTROOTFH4 = &resp->nfs_resop4_u.opputrootfh;

  /* First of all, set the reply to zero to make sure it contains no
     parasite information */
  memset(resp, 0, sizeof(struct nfs_resop4));
  resp->resop = NFS4_OP_PUTROOTFH;

  res_PUTROOTFH4->status = CreateROOTFH4(&(data->rootFH), data);
  if(res_PUTROOTFH4->status != NFS4_OK)
    return res_PUTROOTFH4->status;

  /* Fill in compound data */
  set_compound_data_for_pseudo(data);

  /* I copy the root FH to the currentFH */
  if(data->currentFH.nfs_fh4_len == 0)
    {
      res_PUTROOTFH4->status = nfs4_AllocateFH(&(data->currentFH));
      if(res_PUTROOTFH4->status != NFS4_OK)
        return res_PUTROOTFH4->status;
    }
  memcpy(data->currentFH.nfs_fh4_val, data->rootFH.nfs_fh4_val,
         data->rootFH.nfs_fh4_len);
  data->currentFH.nfs_fh4_len = data->rootFH.nfs_fh4_len;

  if(data->publicFH.nfs_fh4_len == 0)
    {
      res_PUTROOTFH4->status = nfs4_AllocateFH(&(data->publicFH));
      if(res_PUTROOTFH4->status != NFS4_OK)
        return res_PUTROOTFH4->status;
    }
  /* Copy the data where they are supposed to be */
  memcpy(data->publicFH.nfs_fh4_val, data->rootFH.nfs_fh4_val,
         data->rootFH.nfs_fh4_len);
  data->publicFH.nfs_fh4_len = data->rootFH.nfs_fh4_len;

  LogHandleNFS4("NFS4 PUTROOTFH ROOT    FH: ", &data->rootFH);
  LogHandleNFS4("NFS4 PUTROOTFH CURRENT FH: ", &data->currentFH);

  LogFullDebug(COMPONENT_NFS_V4,
                    "NFS4 PUTROOTFH: Ending on status %d",
                    res_PUTROOTFH4->status);

  return res_PUTROOTFH4->status;
}                               /* nfs4_op_putrootfh */

/**
 * @brief Free memory allocated for PUTROOTFH result
 *
 * This function frees any memory allocated for the result of
 * the NFS4_OP_PUTROOTFH function.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_putrootfh_Free(nfs_resop4 *resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_putrootfh_Free */
