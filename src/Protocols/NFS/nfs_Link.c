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
#include "nfs_file_handle.h"

/**
 *
 * @brief The NFS PROC2 and PROC3 LINK
 *
 * The NFS PROC2 and PROC3 LINK.
 *
 * @param[in]  parg     NFS argument union
 * @param[in]  pexport  NFS export list
 * @param[in]  pcontext Credentials to be used for this request
 * @param[in]  pworker  Worker thread data
 * @param[in]  preq     SVC request related to this call
 * @param[out] pres     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs_Link(nfs_arg_t *parg,
             exportlist_t *pexport,
             fsal_op_context_t *pcontext,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t * pres)
{
  char *str_link_name = NULL;
  fsal_name_t link_name;
  cache_entry_t *target_pentry = NULL;
  cache_entry_t *parent_pentry = NULL;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  fsal_attrib_list_t *ppre_attr;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attr_parent_after;
  cache_inode_file_type_t parent_filetype;
  short to_exportid = 0;
  short from_exportid = 0;
  int rc = NFS_REQ_OK;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char                  strto[LEN_FH_STR];
      char                  strfrom[LEN_FH_STR];
      struct display_buffer dspbuf_to = {sizeof(strto), strto, strto};
      struct display_buffer dspbuf_from = {sizeof(strfrom), strfrom, strfrom};

      switch (preq->rq_vers)
        {
        case NFS_V2:
            str_link_name = parg->arg_link2.to.name;
            (void) display_fhandle2(&dspbuf_from, &(parg->arg_link2.from));
            (void) display_fhandle2(&dspbuf_to, &(parg->arg_link2.to.dir));
            break;

        case NFS_V3:
            str_link_name = parg->arg_link3.link.name;
            (void) display_fhandle3(&dspbuf_from, &(parg->arg_link3.file));
            (void) display_fhandle3(&dspbuf_to, &(parg->arg_link3.link.dir));
            break;
        }

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

  /* Get the exportids for the two handles. */
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

  /* Validate the to_exportid */
  if(to_exportid < 0)
    {
      LogInfo(COMPONENT_DISPATCH,
              "NFS%d LINK Request from client %s has badly formed handle for link dir",
              preq->rq_vers, pworker->hostaddr_str);

      /* Bad handle, report to client */
      if(preq->rq_vers == NFS_V2)
        pres->res_stat2 = NFSERR_STALE;
      else
        pres->res_link3.status = NFS3ERR_BADHANDLE;
      goto out;
    }

  if(nfs_Get_export_by_id(nfs_param.pexportlist,
                          to_exportid) == NULL)
    {
      LogInfo(COMPONENT_DISPATCH,
              "NFS%d LINK Request from client %s has invalid export %d for link dir",
              preq->rq_vers, pworker->hostaddr_str,
              to_exportid);

      /* Bad export, report to client */
      if(preq->rq_vers == NFS_V2)
        pres->res_stat2 = NFSERR_STALE;
      else
        pres->res_link3.status = NFS3ERR_STALE;
      goto out;
    }

  /*
   * Both objects have to be in the same filesystem 
   */

  if(to_exportid != from_exportid)
    {
      if(preq->rq_vers == NFS_V2)
        pres->res_stat2 = NFSERR_PERM;
      if(preq->rq_vers == NFS_V3)
        pres->res_link3.status = NFS3ERR_XDEV;
      goto out;
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
                                         pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }
  ppre_attr = &parent_attr;

  if((target_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_link2.from),
                                         &(parg->arg_link3.file),
                                         NULL,
                                         &(pres->res_stat2),
                                         &(pres->res_link3.status),
                                         NULL,
                                         NULL,
                                         pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;;
    }

  /* Extract the filetype */
  parent_filetype = parent_pentry->type;

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
      rc = NFS_REQ_OK;
      goto out;
    }

  // if(str_link_name == NULL || strlen(str_link_name) == 0)
  if(str_link_name == NULL || *str_link_name == '\0' )
    {
      if(preq->rq_vers == NFS_V2)
        pres->res_stat2 = NFSERR_IO;
      if(preq->rq_vers == NFS_V3)
        pres->res_link3.status = NFS3ERR_INVAL;
      goto out;
    }
  else
    {
          /* Make the link */
          if((cache_status = cache_inode_error_convert(FSAL_str2name(str_link_name,
                                                                     0,
                                                                     &link_name))) ==
             CACHE_INODE_SUCCESS)
            {
              if(cache_inode_link(target_pentry,
                                  parent_pentry,
                                  &link_name,
                                  &attr,
                                  pcontext, &cache_status) == CACHE_INODE_SUCCESS)
                {
                  attr_parent_after.asked_attributes = FSAL_ATTRS_V3;

                  if(cache_inode_getattr(parent_pentry,
                                         &attr_parent_after,
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

                          nfs_SetPostOpAttr(pexport,
                                            &attr,
                                            &(pres->res_link3.LINK3res_u.resok.
                                              file_attributes));

                          /*
                           * Build Weak Cache Coherency
                           * data 
                           */
                          nfs_SetWccData(pexport,
                                         ppre_attr,
                                         &attr_parent_after,
                                         &(pres->res_link3.LINK3res_u.resok.linkdir_wcc));

                          pres->res_link3.status = NFS3_OK;
                          break;
                        }       /* switch */

                      rc = NFS_REQ_OK;
                      goto out;

                    }           /* if( cache_inode_link ... */
                }               /* if( cache_inode_getattr ... */
            }                   /* else */
    }

  /* If we are here, there was an error */
  rc = nfs_SetFailedStatus(pexport,
                           preq->rq_vers,
                           cache_status,
                           &pres->res_stat2,
                           &pres->res_link3.status,
                           &(pres->res_link3.LINK3res_u.resfail.file_attributes),
                           ppre_attr,
                           &(pres->res_link3.LINK3res_u.resfail.linkdir_wcc),
                           NULL, NULL);
out:
  /* return references */
  if (target_pentry)
      cache_inode_put(target_pentry);

  if (parent_pentry)
      cache_inode_put(parent_pentry);

  return (rc);

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
