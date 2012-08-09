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
 * \file    nfs4_op_lookupp.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.16 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lookupp.c : Routines used for managing the NFS4 COMPOUND functions.
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

/**
 * nfs4_op_lookupp: looks up  for the parent into theFSAL.
 * 
 * looks up for the parent into the FSAL.In NFSv4 this is to be used instead of "LOOKUP( '..' )" .
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 *
 */
#define arg_LOOKUPP4 op->nfs_argop4_u.oplookupp
#define res_LOOKUPP4 resp->nfs_resop4_u.oplookupp

int nfs4_op_lookupp(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lookupp";

  fsal_name_t            name;
  cache_entry_t        * dir_pentry = NULL;
  cache_entry_t        * file_pentry = NULL;
  fsal_attrib_list_t     attrlookup;
  cache_inode_status_t   cache_status;
  fsal_handle_t        * pfsal_handle = NULL;

  resp->resop = NFS4_OP_LOOKUPP;
  res_LOOKUPP4.status = NFS4_OK;

  /* Do basic checks on a filehandle */
  res_LOOKUPP4.status = nfs4_sanity_check_FH(data, 0LL);
  if(res_LOOKUPP4.status != NFS4_OK)
    return res_LOOKUPP4.status;

  /* looking up for parent directory from ROOTFH return NFS4ERR_NOENT (RFC3530, page 166) */
  if(data->currentFH.nfs_fh4_len == data->rootFH.nfs_fh4_len
     && memcmp(data->currentFH.nfs_fh4_val, data->rootFH.nfs_fh4_val,
               data->currentFH.nfs_fh4_len) == 0)
    {
      /* Nothing to do, just reply with success */
      res_LOOKUPP4.status = NFS4ERR_NOENT;
      return res_LOOKUPP4.status;
    }

  /* If in pseudoFS, proceed with pseudoFS specific functions */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_lookupp_pseudo(op, data, resp);

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_lookupp_xattr(op, data, resp);

  /* Preparying for cache_inode_lookup ".." */
  file_pentry = NULL;
  dir_pentry = data->current_entry;
  name = FSAL_DOT_DOT;

  /* BUGAZOMEU: Faire la gestion des cross junction traverse */
  if((file_pentry
      = cache_inode_lookup(dir_pentry,
                           &name,
                           &attrlookup,
                           data->pcontext, &cache_status)) != NULL)
    {
      /* Extract the fsal attributes from the cache inode pentry */
      pfsal_handle = &file_pentry->handle;

      /* Convert it to a file handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, pfsal_handle, data))
        {
          res_LOOKUPP4.status = NFS4ERR_SERVERFAULT;
          cache_inode_put(file_pentry);
          return res_LOOKUPP4.status;
        }

      /* Copy this to the mounted on FH (if no junction is traversed */
      memcpy((char *)(data->mounted_on_FH.nfs_fh4_val),
             (char *)(data->currentFH.nfs_fh4_val), data->currentFH.nfs_fh4_len);
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

      /* Release dir_pentry, as it is not reachable from anywhere in
         compound after this function returns.  Count on later
         operations or nfs4_Compound to clean up current_entry. */

      if (dir_pentry)
        cache_inode_put(dir_pentry);

      /* Keep the pointer within the compound data */
      data->current_entry = file_pentry;
      data->current_filetype = file_pentry->type;

      /* Return successfully */
      res_LOOKUPP4.status = NFS4_OK;
      return NFS4_OK;

    }

  /* If the part of the code is reached, then something wrong occured in the
   * lookup process, status is not HPSS_E_NOERROR and contains the code for
   * the error */

  /* For any wrong file type LOOKUPP should return NFS4ERR_NOTDIR */
  res_LOOKUPP4.status = nfs4_Errno(cache_status);

  return res_LOOKUPP4.status;
}                               /* nfs4_op_lookupp */

/**
 * nfs4_op_lookupp_Free: frees what was allocared to handle nfs4_op_lookupp.
 *
 * Frees what was allocared to handle nfs4_op_lookupp.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_lookupp_Free(LOOKUPP4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_lookupp_Free */
