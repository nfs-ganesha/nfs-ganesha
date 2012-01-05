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
 * \file   nfs_Link.c
 * \author  $Author: deniel $
 * \data    $Date: 2006/01/24 11:43:05 $
 * \version $Revision: 1.16 $
 * \brief   everything that is needed to handle NFS PROC2-3 LINK.
 *
 *	nfs_Link - everything that is needed to handle NFS PROC2-3 LINK
 *      LINK Performs hardlink through NFS. 
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
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"

/**
 *
 * nfs_Link: The NFS PROC2 and PROC3 LINK
 *
 * The NFS PROC2 and PROC3 LINK. 
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return NFS_REQ_OK if successfull \n
 *         NFS_REQ_DROP if failed but retryable  \n
 *         NFS_REQ_FAILED if failed and not retryable.
 *
 */

int nfs_Link(nfs_arg_t * parg,
             exportlist_t * pexport,
             fsal_op_context_t * pcontext,
             cache_inode_client_t * pclient,
             hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Link";

  char *str_link_name = NULL;
  fsal_name_t link_name;
  cache_entry_t *target_pentry;
  cache_entry_t *parent_pentry;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  int rc;
  fsal_attrib_list_t *ppre_attr;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t target_attr;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attr_parent_after;
  cache_inode_file_type_t parent_filetype;
  cache_inode_file_type_t target_filetype;
  short to_exportid = 0;
  short from_exportid = 0;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char strto[LEN_FH_STR], strfrom[LEN_FH_STR];

      switch (preq->rq_vers)
        {
        case NFS_V2:
            str_link_name = parg->arg_link2.to.name;
            break;

        case NFS_V3:
            str_link_name = parg->arg_link3.link.name;
            break;
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_link2.from),
                       &(parg->arg_link3.file),
                       NULL,
                       strfrom);

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_link2.to.dir),
                       &(parg->arg_link3.link.dir),
                       NULL,
                       strto);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Link handle: %s to handle: %s name: %s",
               strfrom, strto, str_link_name);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_link3.LINK3res_u.resfail.file_attributes.attributes_follow = FALSE;
      pres->res_link3.LINK3res_u.resfail.linkdir_wcc.before.attributes_follow = FALSE;
      pres->res_link3.LINK3res_u.resfail.linkdir_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  /* Get entry for parent directory */
  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_link2.to.dir),
                                         &(parg->arg_link3.link.dir),
                                         NULL,
                                         &(pres->res_stat2),
                                         &(pres->res_link3.status),
                                         NULL,
                                         &parent_attr,
                                         pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }
  ppre_attr = &parent_attr;

  if((target_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_link2.from),
                                         &(parg->arg_link3.file),
                                         NULL,
                                         &(pres->res_stat2),
                                         &(pres->res_link3.status),
                                         NULL,
                                         &target_attr,
                                         pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Extract the filetype */
  parent_filetype = cache_inode_fsal_type_convert(parent_attr.type);
  target_filetype = cache_inode_fsal_type_convert(target_attr.type);

  /*
   * Sanity checks: 
   */
  if(parent_filetype != DIRECTORY)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_stat2 = NFSERR_NOTDIR;
          break;
        case NFS_V3:
          pres->res_link3.status = NFS3ERR_NOTDIR;
          break;
        }
      return NFS_REQ_OK;
    }

  switch (preq->rq_vers)
    {
    case NFS_V2:
      {
        str_link_name = parg->arg_link2.to.name;
        to_exportid = nfs2_FhandleToExportId(&(parg->arg_link2.to.dir));
        from_exportid = nfs2_FhandleToExportId(&(parg->arg_link2.from));
        break;
      }
    case NFS_V3:
      {
        str_link_name = parg->arg_link3.link.name;
        to_exportid = nfs3_FhandleToExportId(&(parg->arg_link3.link.dir));
        from_exportid = nfs3_FhandleToExportId(&(parg->arg_link3.file));
        break;
      }
    }

  // if(str_link_name == NULL || strlen(str_link_name) == 0)
  if(str_link_name == NULL || *str_link_name == '\0' )
    {
      if(preq->rq_vers == NFS_V2)
        pres->res_stat2 = NFSERR_IO;
      if(preq->rq_vers == NFS_V3)
        pres->res_link3.status = NFS3ERR_INVAL;
    }
  else
    {
      /*
       * Both objects have to be in the same filesystem 
       */

      if(to_exportid != from_exportid)
        {
          if(preq->rq_vers == NFS_V2)
            pres->res_stat2 = NFSERR_PERM;
          if(preq->rq_vers == NFS_V3)
            pres->res_link3.status = NFS3ERR_XDEV;
        }
      else
        {
          /* Make the link */
          if((cache_status = cache_inode_error_convert(FSAL_str2name(str_link_name,
                                                                     FSAL_MAX_NAME_LEN,
                                                                     &link_name))) ==
             CACHE_INODE_SUCCESS)
            {
              if(cache_inode_link(target_pentry,
                                  parent_pentry,
                                  &link_name,
                                  pexport->cache_inode_policy,
                                  &attr,
                                  ht,
                                  pclient,
                                  pcontext, &cache_status) == CACHE_INODE_SUCCESS)
                {
                  if(cache_inode_getattr(parent_pentry,
                                         &attr_parent_after,
                                         ht,
                                         pclient,
                                         pcontext, &cache_status) == CACHE_INODE_SUCCESS)
                    {
                      switch (preq->rq_vers)
                        {
                        case NFS_V2:
                          pres->res_stat2 = NFS_OK;
                          break;

                        case NFS_V3:
                          /*
                           * Build post op file
                           * attributes 
                           */

                          nfs_SetPostOpAttr(pcontext, pexport,
                                            target_pentry,
                                            &attr,
                                            &(pres->res_link3.LINK3res_u.resok.
                                              file_attributes));

                          /*
                           * Build Weak Cache Coherency
                           * data 
                           */
                          nfs_SetWccData(pcontext, pexport,
                                         parent_pentry,
                                         ppre_attr,
                                         &attr_parent_after,
                                         &(pres->res_link3.LINK3res_u.resok.linkdir_wcc));

                          pres->res_link3.status = NFS3_OK;
                          break;
                        }       /* switch */

                      return NFS_REQ_OK;

                    }           /* if( cache_inode_link ... */
                }               /* if( cache_inode_getattr ... */
            }                   /* else */
        }
    }

  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;
    }

  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_stat2,
                      &pres->res_link3.status,
                      target_pentry,
                      &(pres->res_link3.LINK3res_u.resfail.file_attributes),
                      parent_pentry,
                      ppre_attr,
                      &(pres->res_link3.LINK3res_u.resfail.linkdir_wcc),
                      NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs_Link */

/**
 * nfs_Link_Free: Frees the result structure allocated for nfs_Link.
 * 
 * Frees the result structure allocated for nfs_Link.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Link_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Link_Free */
