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
 * \file    nfs_Setattr.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:54 $
 * \version $Revision: 1.17 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Setattr.c : Routines used for managing the NFS4 COMPOUND functions.
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

/**
 *
 * nfs_Create: The NFS PROC2 and PROC3 SETATTR
 *
 * Implements the NFS PROC SETATTR function (for V2 and V3).
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

int nfs_Setattr(nfs_arg_t * parg,
                exportlist_t * pexport,
                fsal_op_context_t * pcontext,
                cache_inode_client_t * pclient,
                hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Setattr";

  sattr3 new_attributes3;
  sattr2 new_attributes2;
  fsal_attrib_list_t setattr;
  cache_entry_t *pentry = NULL;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t *ppre_attr;
  cache_inode_status_t cache_status;
  int rc;
  int do_trunc = FALSE;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];
      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_setattr2.file),
                       &(parg->arg_setattr3.object),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Setattr handle: %s", str);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_setattr3.SETATTR3res_u.resfail.obj_wcc.before.attributes_follow = FALSE;
      pres->res_setattr3.SETATTR3res_u.resfail.obj_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  /* Convert file handle into a vnode */
  if((pentry = nfs_FhandleToCache(preq->rq_vers,
                                  &(parg->arg_setattr2.file),
                                  &(parg->arg_setattr3.object),
                                  NULL,
                                  &(pres->res_attr2.status),
                                  &(pres->res_setattr3.status),
                                  NULL, &pre_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_setattr3.object))))
    {
      /* do nothing */
      nfs_SetWccData(pcontext, pexport,
                     pentry,
                     &pre_attr,
                     &pre_attr, &(pres->res_setattr3.SETATTR3res_u.resok.obj_wcc));

      pres->res_setattr3.status = NFS3_OK;
      return NFS_REQ_OK;
    }

  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &pre_attr;

  switch (preq->rq_vers)
    {
    case NFS_V2:

      new_attributes2 = parg->arg_setattr2.attributes;
      /*
       * Check for 2Gb limit in V2 
       */
      if((parg->arg_setattr2.attributes.size != (u_int) - 1) &&
         (pre_attr.filesize > NFS2_MAX_FILESIZE))
        {
          /*
           *  They are trying to set the size, the
           *  file is >= 2GB and V2 clients don't
           *  understand filesizes >= 2GB, so we
           *  don't allow them to alter them in any
           *  way.
           */
          pres->res_attr2.status = NFSERR_FBIG;
          return NFS_REQ_OK;
        }

      if(nfs2_Sattr_To_FSALattr(&setattr, &new_attributes2) == 0)
        {
          pres->res_attr2.status = NFSERR_IO;
          return NFS_REQ_OK;
        }

      if(new_attributes2.size != (u_int) - 1)
        do_trunc = TRUE;

      break;

    case NFS_V3:
      new_attributes3 = parg->arg_setattr3.new_attributes;

      if(parg->arg_setattr3.guard.check == TRUE)
        {
          /* This pack of lines implements the "guard check" setattr.
           * This feature of nfsv3 is used to avoid several setattr 
           * to occur concurently on the same object, from different clients */
          fattr3 attributes;

          if(nfs3_FSALattr_To_Fattr(pexport, ppre_attr, &attributes) == 0)
            {
              pres->res_setattr3.status = NFS3ERR_NOT_SYNC;
              return NFS_REQ_OK;
            }
          LogFullDebug(COMPONENT_NFSPROTO, "css=%d acs=%d    csn=%d acn=%d",
                 parg->arg_setattr3.guard.sattrguard3_u.obj_ctime.seconds,
                 attributes.ctime.seconds,
                 parg->arg_setattr3.guard.sattrguard3_u.obj_ctime.nseconds,
                 attributes.ctime.nseconds);

          if((parg->arg_setattr3.guard.sattrguard3_u.obj_ctime.seconds !=
              attributes.ctime.seconds)
             || (parg->arg_setattr3.guard.sattrguard3_u.obj_ctime.nseconds !=
                 attributes.ctime.nseconds))
            {
              pres->res_setattr3.status = NFS3ERR_NOT_SYNC;
              return NFS_REQ_OK;
            }
        }

      /* 
       * Conversion to FSAL attributes 
       */
      if(nfs3_Sattr_To_FSALattr(&setattr, &new_attributes3) == 0)
        {
          pres->res_setattr3.status = NFS3ERR_INVAL;
          return NFS_REQ_OK;
        }

      if(new_attributes3.size.set_it)
        {
          do_trunc = TRUE;
        }

      break;
    }

  /*
   * trunc may change Xtime so we have to start with trunc and finish
   * by the mtime and atime 
   */
  if(do_trunc)
    {
      /* Should not be done on a directory */
      if(pentry->internal_md.type == DIRECTORY)
        cache_status = CACHE_INODE_IS_A_DIRECTORY;
      else
        {
          cache_status = cache_inode_truncate(pentry,
                                              setattr.filesize,
                                              &parent_attr,
                                              ht, pclient, pcontext, &cache_status);
          setattr.asked_attributes &= ~FSAL_ATTR_SPACEUSED;
          setattr.asked_attributes &= ~FSAL_ATTR_SIZE;
        }
    }
  else
    cache_status = CACHE_INODE_SUCCESS;

  if(cache_status == CACHE_INODE_SUCCESS)
    {
      /* Add code to support partially completed setattr */
      if(do_trunc)
        {
          if(setattr.asked_attributes != 0)
            {
              cache_status = cache_inode_setattr(pentry,
                                                 &setattr,
                                                 ht, pclient, pcontext, &cache_status);
            }
          else
            cache_status = CACHE_INODE_SUCCESS;

          setattr.asked_attributes |= FSAL_ATTR_SPACEUSED;
          setattr.asked_attributes |= FSAL_ATTR_SIZE;
        }
      else
        cache_status = cache_inode_setattr(pentry,
                                           &setattr,
                                           ht, pclient, pcontext, &cache_status);
    }

  if(cache_status == CACHE_INODE_SUCCESS)
    {
      /* Set the NFS return */
      switch (preq->rq_vers)
        {
        case NFS_V2:
          /* Copy data from vattr to Attributes */
          if(nfs2_FSALattr_To_Fattr
             (pexport, &setattr, &(pres->res_attr2.ATTR2res_u.attributes)) == 0)
            pres->res_attr2.status = NFSERR_IO;
          else
            pres->res_attr2.status = NFS_OK;
          break;

        case NFS_V3:
          /* Build Weak Cache Coherency data */
          nfs_SetWccData(pcontext, pexport,
                         pentry,
                         ppre_attr,
                         &setattr, &(pres->res_setattr3.SETATTR3res_u.resok.obj_wcc));

          pres->res_setattr3.status = NFS3_OK;
          break;
        }

      return NFS_REQ_OK;
    }

  LogFullDebug(COMPONENT_NFSPROTO, "nfs_Setattr: failed");

  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    return NFS_REQ_DROP;

  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_attr2.status,
                      &pres->res_setattr3.status,
                      NULL, NULL,
                      pentry,
                      ppre_attr,
                      &(pres->res_setattr3.SETATTR3res_u.resfail.obj_wcc),
                      NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs_Setattr */

/**
 * nfs_Setattr_Free: Frees the result structure allocated for nfs_Setattr.
 * 
 * Frees the result structure allocated for nfs_Setattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Setattr_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Setattr_Free */
