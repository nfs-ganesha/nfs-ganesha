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
 * @file    nfs4_op_putpubfh.c
 * @brief   Routines used for managing the NFS4_OP_PUTPUBFH operation.
 *
 * Routines used for managing the NFS4_OP_PUTPUBFH operation.
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 * @brief Create the pseudo FS public filehandle
 *
 * This function creates the pseudo FS public filehandle.
 *
 * @param[out]    fh   The file handle to be built.
 * @param[in,out] data request compound data
 *
 * @retval NFS4_OK if successful.
 * @retval NFS4ERR_BADHANDLE otherwise.
 *
 * @see nfs4_op_putrootfh
 *
 */

static int CreatePUBFH4(nfs_fh4 *fh, compound_data_t *data)
{
  pseudofs_entry_t psfsentry;
  int status = 0;

  /* For the moment, I choose to have rootFH = publicFH */
  psfsentry = *(data->pseudofs->reverse_tab[0]);

  /* If publicFH already set, return success */
  if(data->publicFH.nfs_fh4_len != 0)
    return NFS4_OK;

  if((status = nfs4_AllocateFH(&(data->publicFH))) != NFS4_OK)
    return status;

  if(!nfs4_PseudoToFhandle(&(data->publicFH), &psfsentry))
    return NFS4ERR_BADHANDLE;

  LogHandleNFS4("CREATE PUB FH: ", &data->publicFH);

  return NFS4_OK;
} /* CreatePUBFH4 */

/**
 * @brief The NFS4_OP_PUTFH operation
 *
 * This function sets the publicFH for the current compound requests
 * as the current FH.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 371
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_putpubfh(struct nfs_argop4 *op,
                     compound_data_t *data,
                     struct nfs_resop4 *resp)
{
  PUTPUBFH4res *const res_PUTPUBFH4 = &resp->nfs_resop4_u.opputpubfh;

  /* First of all, set the reply to zero to make sure
   * it contains no parasite information */
  memset(resp, 0, sizeof(struct nfs_resop4));
  resp->resop = NFS4_OP_PUTPUBFH;
  res_PUTPUBFH4->status = NFS4_OK;

  /* For now, GANESHA makes no difference between PUBLICFH and ROOTFH */
  res_PUTPUBFH4->status = CreatePUBFH4(&(data->publicFH), data);
  if(res_PUTPUBFH4->status != NFS4_OK)
    goto out;

  /* Fill in compound data */
  set_compound_data_for_pseudo(data);

  /* I copy the root FH to the currentFH */
  if(data->currentFH.nfs_fh4_len == 0)
    {
      res_PUTPUBFH4->status = nfs4_AllocateFH(&(data->currentFH));
      if(res_PUTPUBFH4->status != NFS4_OK)
	goto out;
    }

  /* Copy the data from current FH to saved FH */
  memcpy(data->currentFH.nfs_fh4_val, data->publicFH.nfs_fh4_val,
	 data->publicFH.nfs_fh4_len);

  res_PUTPUBFH4->status = NFS4_OK ;

out:
  LogHandleNFS4("NFS4 PUTPUBFH PUBLIC  FH: ", &data->publicFH);
  LogHandleNFS4("NFS4 PUTPUBFH CURRENT FH: ", &data->currentFH);

  LogFullDebug(COMPONENT_NFS_V4,
                    "NFS4 PUTPUBFH: Ending on status %d",
                    res_PUTPUBFH4->status);

  return res_PUTPUBFH4->status;
}

/**
 * @brief Free memory allocated for PUTPUBFH result
 *
 * This function frees the memory allocated for the result of the
 * NFS4_OP_PUTPUBFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_putpubfh_Free(nfs_resop4 *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_putpubfh_Free */
