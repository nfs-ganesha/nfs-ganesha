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
 * \file    nfs4_op_readlink.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.15 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_readlink.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs4_op_readlink: The NFS4_OP_READLINK operation. 
 *
 * This function implements the NFS4_OP_READLINK operation.
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

#define arg_READLINK4 op->nfs_argop4_u.opreadlink
#define res_READLINK4 resp->nfs_resop4_u.opreadlink

int nfs4_op_readlink(struct nfs_argop4 *op,
                     compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_inode_status_t cache_status;
  fsal_path_t symlink_path;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_readlink";

  resp->resop = NFS4_OP_READLINK;
  res_READLINK4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_READLINK4.status = NFS4ERR_NOFILEHANDLE;
      return NFS4ERR_NOFILEHANDLE;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_READLINK4.status = NFS4ERR_BADHANDLE;
      return NFS4ERR_BADHANDLE;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_READLINK4.status = NFS4ERR_FHEXPIRED;
      return NFS4ERR_FHEXPIRED;
    }

  /* You can readlink only on a link ... */
  if(data->current_filetype != SYMBOLIC_LINK)
    {
      /* As said on page 194 of RFC3530, return NFS4ERR_INVAL in this case */
      res_READLINK4.status = NFS4ERR_INVAL;
      return res_READLINK4.status;
    }

  /* Using cache_inode_readlink */
  if(cache_inode_readlink(data->current_entry,
                          &symlink_path,
                          data->ht,
                          data->pclient,
                          data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
    {
      /* Alloc read link */

      if((res_READLINK4.READLINK4res_u.resok4.link.utf8string_val =
          (char *)Mem_Alloc_Label(symlink_path.len, "nfs4_op_readlink")) == NULL)
        {
          res_READLINK4.status = NFS4ERR_INVAL;
          return res_READLINK4.status;
        }

      /* convert the fsal path to a utf8 string */
      if(str2utf8((char *)symlink_path.path, &res_READLINK4.READLINK4res_u.resok4.link)
         == -1)
        {
          res_READLINK4.status = NFS4ERR_INVAL;
          return res_READLINK4.status;
        }

      res_READLINK4.status = NFS4_OK;
      return res_READLINK4.status;
    }

  res_READLINK4.status = nfs4_Errno(cache_status);
  return res_READLINK4.status;
}                               /* nfs4_op_readlink */

/**
 * nfs4_op_readlink_Free: frees what was allocared to handle nfs4_op_readlink.
 * 
 * Frees what was allocared to handle nfs4_op_readlink.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_readlink_Free(READLINK4res * resp)
{
  if(resp->status == NFS4_OK && resp->READLINK4res_u.resok4.link.utf8string_len > 0)
    Mem_Free((char *)resp->READLINK4res_u.resok4.link.utf8string_val);

  return;
}                               /* nfs4_op_readlink_Free */
