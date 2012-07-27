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
 * \file    nfs4_op_link.c
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
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
#include "HashData.h"
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

/**
 * @brief The NFS4_OP_LINK operation.
 *
 * This functions handles the NFS4_OP_LINK operation in NFSv4. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 367
 */

#define arg_LINK4 op->nfs_argop4_u.oplink
#define res_LINK4 resp->nfs_resop4_u.oplink

int nfs4_op_link(struct nfs_argop4 *op,
                 compound_data_t *data,
                 struct nfs_resop4 *resp)
{
  cache_entry_t        * dir_pentry = NULL;
  cache_entry_t        * file_pentry = NULL;
  cache_inode_status_t   cache_status = CACHE_INODE_SUCCESS;
  struct attrlist        attr;
  char                 * newname = NULL;

  resp->resop = NFS4_OP_LINK;
  res_LINK4.status = NFS4_OK;

  /* Do basic checks on a filehandle */
  res_LINK4.status = nfs4_sanity_check_FH(data, DIRECTORY, FALSE);
  if(res_LINK4.status != NFS4_OK)
    goto out;

  /* If data->exportp is null, a junction from pseudo fs was
     traversed, credp and exportp have to be updated */
  if(data->pexport == NULL)
    {
      res_LINK4.status = nfs4_SetCompoundExport(data);
      if(res_LINK4.status != NFS4_OK)
        goto out;
    }

  /*
   * This operation creates a hard link, for the file represented by
   * the saved FH, in directory represented by currentFH under the
   * name arg_LINK4.target
   */

  /* Validate and convert the UFT8 objname to a regular string */
  res_LINK4.status = nfs4_utf8string2dynamic(&arg_LINK4.newname,
					     UTF8_SCAN_ALL,
					     &newname);
  if (res_LINK4.status != NFS4_OK) {
      goto out;
  }

  /* Destination FH (the currentFH) must be a directory */
  if(data->current_filetype != DIRECTORY)
    {
      res_LINK4.status = NFS4ERR_NOTDIR;
      goto out;
    }

  /* Target object (the savedFH) must be real.
   * which is the case if a SAVEFH was not done before here */
  if(data->saved_filetype == NO_FILE_TYPE || data->saved_entry == NULL)
    {
      res_LINK4.status = NFS4ERR_NOFILEHANDLE;
      goto out;
    }

  /* Target object (the savedFH) must not be a directory */
  if(data->saved_filetype == DIRECTORY)
    {
      res_LINK4.status = NFS4ERR_ISDIR;
      goto out;
    }

  /* get info from compound data */
  dir_pentry = data->current_entry;

  /* We have to keep track of the 'change' file attribute for reply
     structure */
  if((cache_status
      = cache_inode_getattr(dir_pentry,
                            &attr,
                            &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_LINK4.status = nfs4_Errno(cache_status);
      goto out;
    }

  res_LINK4.LINK4res_u.resok4.cinfo.before
       = cache_inode_get_changeid4(dir_pentry);

  /* Convert savedFH into a vnode */
  file_pentry = data->saved_entry;

  /* make the link */
  if(cache_inode_link(file_pentry,
                      dir_pentry,
                      newname,
                      &attr,
                      data->req_ctx, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_LINK4.status = nfs4_Errno(cache_status);
      goto out;
    }

  res_LINK4.LINK4res_u.resok4.cinfo.after
       = cache_inode_get_changeid4(dir_pentry);
  res_LINK4.LINK4res_u.resok4.cinfo.atomic = FALSE;

  res_LINK4.status = NFS4_OK;

 out:

  if (newname) {
    gsh_free(newname);
  }
  return res_LINK4.status;
}                               /* nfs4_op_link */

/**
 * @brief Free memory allocated for LINK result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LINK operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_link_Free(LINK4res *resp)
{
  return;
} /* nfs4_op_link_Free */
