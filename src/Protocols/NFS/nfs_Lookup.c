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
 * \file nfs_lookup.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.18 $
 * \brief   everything that is needed to handle NFS PROC2-3 LINK.
 *
 *  nfs_Lookup - everything that is needed to handle NFS PROC2-3 LOOKUP
 *      NFS V2-3 generic file browsing function
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
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFS PROC2 and PROC3 LOOKUP
 *
 * Implements the NFS PROC LOOKUP function (for V2 and V3).
 *
 * @param[in] parg     NFS arguments union
 * @param[in] pexport  NFS export list
 * @param[in] pcontext Credentials to be used for this request
 * @param[in] pworker  Worker thread data
 * @param[in] preq     SVC request related to this call
 * @param[out] pres    Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs_Lookup(nfs_arg_t *parg,
               exportlist_t *pexport,
               fsal_op_context_t *pcontext,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t * pres)
{
  cache_entry_t *pentry_dir = NULL;
  cache_entry_t *pentry_file = NULL;
  cache_inode_status_t cache_status;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attrdir;
  char *strpath = NULL;
  char strname[MAXNAMLEN+1];
  unsigned int xattr_found = FALSE;
  fsal_name_t name;
  fsal_handle_t *pfsal_handle;
  int rc = NFS_REQ_OK;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];

      switch (preq->rq_vers)
        {
        case NFS_V2:
          strpath = parg->arg_lookup2.name;
          break;

        case NFS_V3:
          strpath = parg->arg_lookup3.what.name;
          break;
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_lookup2.dir),
                       &(parg->arg_lookup3.what.dir),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Lookup handle: %s name: %s",
               str, strpath);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_lookup3.LOOKUP3res_u.resfail.dir_attributes.attributes_follow = FALSE;
    }

  if((pentry_dir = nfs_FhandleToCache(preq->rq_vers,
                                      &(parg->arg_lookup2.dir),
                                      &(parg->arg_lookup3.what.dir),
                                      NULL,
                                      &(pres->res_dirop2.status),
                                      &(pres->res_lookup3.status),
                                      NULL,
                                      &attrdir, pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  switch (preq->rq_vers)
    {
    case NFS_V2:
      strpath = parg->arg_lookup2.name;
      break;

    case NFS_V3:
      strpath = parg->arg_lookup3.what.name;
      break;
    }

  /* Do the lookup */

#ifndef _NO_XATTRD
  /* Is this a .xattr.d.<object> name ? */
  if(nfs_XattrD_Name(strpath, strname))
    {
      strpath = strname;
      xattr_found = TRUE;
    }
#endif

  if((preq->rq_vers == NFS_V3) &&
     (nfs3_Is_Fh_Xattr(&(parg->arg_lookup3.what.dir)))) {
      rc = nfs3_Lookup_Xattr(parg, pexport, pcontext, preq, pres);
      goto out;
  }

  if((cache_status = cache_inode_error_convert(FSAL_str2name(strpath,
                                                             0,
                                                             &name))) ==
     CACHE_INODE_SUCCESS)
    {
      /* BUGAZOMEU: Faire la gestion des cross junction traverse */
      if((pentry_file
          = cache_inode_lookup(pentry_dir,
                               &name,
                               &attr,
                               pcontext,
                               &cache_status)) != NULL)
        {
          /* Do not forget cross junction management */
          pfsal_handle = &pentry_file->handle;
          if(cache_status == CACHE_INODE_SUCCESS)
            {
              switch (preq->rq_vers)
                {
                case NFS_V2:
                  /* Build file handle */
                  if(nfs2_FSALToFhandle
                     (&(pres->res_dirop2.DIROP2res_u.diropok.file), pfsal_handle,
                      pexport))
                    {
                      if(nfs2_FSALattr_To_Fattr(pexport, &attr,
                                                &(pres->res_dirop2.DIROP2res_u.diropok.
                                                  attributes)))
                        {
                          pres->res_dirop2.status = NFS_OK;
                        }
                    }
                  break;

                case NFS_V3:
                  /* Build FH */
                  if((pres->res_lookup3.LOOKUP3res_u.resok.object.data.data_val =
                      gsh_malloc(sizeof(struct alloc_file_handle_v3))) == NULL)
                    pres->res_lookup3.status = NFS3ERR_INVAL;
                  else
                    {
                      if(nfs3_FSALToFhandle
                         ((nfs_fh3 *) &
                          (pres->res_lookup3.LOOKUP3res_u.resok.object.data),
                          pfsal_handle, pexport))
                        {

#ifndef _NO_XATTRD
                          /* If this is a xattr ghost directory name, update the FH */
                          if(xattr_found == TRUE)
                            {
                              pres->res_lookup3.status =
                                  nfs3_fh_to_xattrfh((nfs_fh3 *) &
                                                     (pres->res_lookup3.LOOKUP3res_u.
                                                      resok.object.data),
                                                     (nfs_fh3 *) & (pres->res_lookup3.
                                                                    LOOKUP3res_u.resok.
                                                                    object.data));
                              /* Build entry attributes */
                              nfs_SetPostOpXAttrDir(pcontext, pexport,
                                                    &attr,
                                                    &(pres->res_lookup3.LOOKUP3res_u.
                                                      resok.obj_attributes));

                            }
                          else
#endif
                            /* Build entry attributes */
                            nfs_SetPostOpAttr(pexport,
                                              &attr,
                                              &(pres->res_lookup3
                                                .LOOKUP3res_u.resok
                                                .obj_attributes));

                          /* Build directory attributes */
                          nfs_SetPostOpAttr(pexport,
                                            &attrdir,
                                            &(pres->res_lookup3
                                              .LOOKUP3res_u.resok
                                              .dir_attributes));

                          pres->res_lookup3.status = NFS3_OK;
                        }       /* if */
                    }           /* else */
                  break;
                }               /* case */
            }                   /* if( cache_status == CACHE_INODE_SUCCESS ) */
        }                       /* if( ( pentry_file = cache_inode_lookup... */
    }

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      rc = nfs_SetFailedStatus(pexport,
                               preq->rq_vers,
                               cache_status,
                               &pres->res_dirop2.status,
                               &pres->res_lookup3.status,
                               &(pres->res_lookup3.LOOKUP3res_u.resfail.dir_attributes),
                               NULL, NULL, NULL, NULL);
    }
  else
    {
      rc = NFS_REQ_OK;
    }

out:
  /* return references */
  if (pentry_dir)
      cache_inode_put(pentry_dir);

  if (pentry_file)
      cache_inode_put(pentry_file);

  return (rc);

}                               /* nfs_Lookup */

/**
 * nfs_Lookup_Free: Frees the result structure allocated for nfs_Lookup.
 * 
 * Frees the result structure allocated for nfs_Lookup.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Lookup_Free(nfs_res_t * resp)
{
  if(resp->res_lookup3.status == NFS3_OK)
    gsh_free(resp->res_lookup3.LOOKUP3res_u.resok.object.data.data_val);
}                               /* nfs_Lookup_Free */

/**
 * nfs_Lookup_Free: Frees the result structure allocated for nfs_Lookup.
 *
 * Frees the result structure allocated for nfs_Lookup.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Lookup_Free(nfs_res_t * resp)
{
  /* nothing to do */
  return;
}                               /* nfs_Lookup_Free */
