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
 * \file    nfs3_Pathconf.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.12 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_Pathconf.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 * nfs3_Pathconf: Implements NFSPROC3_PATHCONF
 *
 * Implements NFSPROC3_ACCESS.
 * 
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK (this routine does nothing)
 *
 */

int nfs3_Pathconf(nfs_arg_t * parg,
                  exportlist_t * pexport,
                  struct user_cred *creds,
                  cache_inode_client_t * pclient,
                  struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs3_Pathconf";

  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t attr;
  int rc = NFS_REQ_OK;
  struct fsal_export *exp_hdl = pexport->export_hdl;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];
      sprint_fhandle3(str, &(parg->arg_pathconf3.object));
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs3_Pathconf handle: %s", str);
    }

  /* to avoid setting it on each error case */
  pres->res_pathconf3.PATHCONF3res_u.resfail.obj_attributes.attributes_follow = FALSE;

  /* Convert file handle into a fsal_handle */
  if(nfs3_FhandleToFSAL(&(parg->arg_pathconf3.object),
			&fsal_data.fh_desc,
			pexport->export_hdl) == 0)
    {
      rc = NFS_REQ_DROP;
      goto out;
    }

  /* Get the entry in the cache_inode */
  if((pentry = cache_inode_get(&fsal_data,
                               &attr,
                               pclient,
                               NULL,
                               &cache_status)) == NULL)
    {
      /* Stale NFS FH ? */
      pres->res_pathconf3.status = NFS3ERR_STALE;
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Build post op file attributes */
  nfs_SetPostOpAttr(pexport,
                    &attr,
                    &(pres->res_pathconf3
                      .PATHCONF3res_u.resok.obj_attributes));

  pres->res_pathconf3.PATHCONF3res_u.resok.linkmax
	  = exp_hdl->ops->fs_maxlink(exp_hdl);
  pres->res_pathconf3.PATHCONF3res_u.resok.name_max
	  = exp_hdl->ops->fs_maxnamelen(exp_hdl);
  pres->res_pathconf3.PATHCONF3res_u.resok.no_trunc
	  = exp_hdl->ops->fs_supports(exp_hdl, no_trunc);
  pres->res_pathconf3.PATHCONF3res_u.resok.chown_restricted
	  = exp_hdl->ops->fs_supports(exp_hdl, chown_restricted);
  pres->res_pathconf3.PATHCONF3res_u.resok.case_insensitive
	  = exp_hdl->ops->fs_supports(exp_hdl, case_insensitive);
  pres->res_pathconf3.PATHCONF3res_u.resok.case_preserving
	  = exp_hdl->ops->fs_supports(exp_hdl, case_preserving);

 out:

  if (pentry)
    {
      cache_inode_put(pentry, pclient);
    }

  return rc;
}                               /* nfs3_Pathconf */

/**
 * nfs3_Pathconf_Free: Frees the result structure allocated for nfs3_Pathconf.
 * 
 * Frees the result structure allocated for nfs3_Pathconf.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Pathconf_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* nfs3_Pathconf_Free */
