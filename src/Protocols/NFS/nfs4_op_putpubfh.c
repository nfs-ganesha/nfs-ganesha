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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

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

  psfsentry = *(data->pseudofs->reverse_tab[0]);

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

#define arg_PUTPUBFH4 op->nfs_argop4_u.opputpubfh
#define res_PUTPUBFH4 resp->nfs_resop4_u.opputpubfh

int nfs4_op_putpubfh(struct nfs_argop4 *op,
                     compound_data_t *data,
                     struct nfs_resop4 *resp)
{
  resp->resop = NFS4_OP_PUTPUBFH;
  res_PUTPUBFH4.status = NFS4_OK;

  /* For now, GANESHA makes no difference betzeen PUBLICFH and ROOTFH */
  res_PUTPUBFH4.status = CreatePUBFH4(&(data->publicFH), data);
  if(res_PUTPUBFH4.status != NFS4_OK)
    return res_PUTPUBFH4.status;

  /* If there is no currentFH, teh  return an error */
  if(nfs4_Is_Fh_Empty(&(data->publicFH)))
    {
      /* There is no current FH, return NFS4ERR_NOFILEHANDLE */
      res_PUTPUBFH4.status = NFS4ERR_NOFILEHANDLE;
      return res_PUTPUBFH4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->publicFH)))
    {
      res_PUTPUBFH4.status = NFS4ERR_BADHANDLE;
      return res_PUTPUBFH4.status;
    }

  /* Tests if teh Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->publicFH)))
    {
      res_PUTPUBFH4.status = NFS4ERR_FHEXPIRED;
      return res_PUTPUBFH4.status;
    }

  /* I copy the root FH to the currentFH and, if not already done, to
     the publicFH */
  /* For the moment, I choose to have rootFH = publicFH */
  /* For initial mounted_on_FH, I'll use the rootFH, this will change
     at junction traversal */
  if(data->currentFH.nfs_fh4_len == 0)
    {
      res_PUTPUBFH4.status = nfs4_AllocateFH(&(data->currentFH));
      if(res_PUTPUBFH4.status != NFS4_OK)
        return res_PUTPUBFH4.status;
    }

  /* Copy the data from current FH to saved FH */
  memcpy(data->currentFH.nfs_fh4_val, data->publicFH.nfs_fh4_val,
         data->publicFH.nfs_fh4_len);

  res_PUTPUBFH4.status = NFS4_OK ;

  return res_PUTPUBFH4.status;
}                               /* nfs4_op_putpubfh */

/**
 * @brief Free memory allocated for PUTPUBFH result
 *
 * This function frees the memory allocated for the result of the
 * NFS4_OP_PUTPUBFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_putpubfh_Free(PUTPUBFH4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_putpubfh_Free */
