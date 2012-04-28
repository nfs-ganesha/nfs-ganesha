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
 * \file    nfs_Write.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:54 $
 * \version $Revision: 1.14 $
 * \brief   Routines used for managing the Write requests.
 *
 * nfs_Write.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <assert.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
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
 *
 * nfs_Write: The NFS PROC2 and PROC3 WRITE
 *
 * Implements the NFS PROC WRITE function (for V2 and V3).
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param creds   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return NFS_REQ_OK if successfull \n
 *         NFS_REQ_DROP if failed but retryable  \n
 *         NFS_REQ_FAILED if failed and not retryable.
 *
 */
extern writeverf3 NFS3_write_verifier;  /* NFS V3 write verifier      */

int nfs_Write(nfs_arg_t * parg,
              exportlist_t * pexport,
              struct user_cred *creds,
              cache_inode_client_t * pclient,
              struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Write";

  cache_entry_t *pentry;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t *ppre_attr;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  size_t size = 0;
  size_t written_size;
  fsal_off_t offset = 0;
  caddr_t data = NULL;
  fsal_boolean_t eof_met;
  cache_inode_stability_t stability = CACHE_INODE_SAFE_WRITE_TO_FS;
  int rc = NFS_REQ_OK;
#ifdef _USE_QUOTA
  fsal_status_t fsal_status ;
#endif

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR], *stables = "";

      switch (preq->rq_vers)
        {
        case NFS_V2:
          offset = parg->arg_write2.offset;
          size = parg->arg_write2.data.nfsdata2_len;
          stables = "FILE_SYNC";
          break;
        case NFS_V3:
          offset = parg->arg_write3.offset;
          size = parg->arg_write3.count;
          switch (parg->arg_write3.stable)
            {
              case UNSTABLE:  stables = "UNSTABLE"; break;
              case DATA_SYNC: stables = "DATA_SYNC"; break;
              case FILE_SYNC: stables = "FILE_SYNC"; break;
            }
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_write2.file),
                       &(parg->arg_write3.file),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Write handle: %s start: %llx len: %llx %s",
               str,
               (unsigned long long) offset,
               (unsigned long long) size,
               stables);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_write3.WRITE3res_u.resfail.file_wcc.before.attributes_follow = FALSE;
      pres->res_write3.WRITE3res_u.resfail.file_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  /* Convert file handle into a cache entry */
  if((pentry = nfs_FhandleToCache(preq->rq_vers,
                                  &(parg->arg_write2.file),
                                  &(parg->arg_write3.file),
                                  NULL,
                                  &(pres->res_attr2.status),
                                  &(pres->res_write3.status),
                                  NULL, &pre_attr, pexport, pclient, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_write3.file))))
  {
    rc = nfs3_Write_Xattr(parg, pexport, creds, pclient, preq, pres);
    goto out;
  }

  if(cache_inode_access(pentry,
                        FSAL_WRITE_ACCESS,
                        pclient,
                        creds,
                        &cache_status) != CACHE_INODE_SUCCESS)
    {
      /* NFSv3 exception : if user wants to write to a file that is readonly 
       * but belongs to him, then allow it to do it, push the permission check
       * to the client side */
      if( ( cache_status == CACHE_INODE_FSAL_EACCESS  ) &&
          ( pentry->obj_handle->attributes.owner ==  creds->caller_uid) )
       {
          LogDebug( COMPONENT_NFSPROTO,
                    "Exception management: allowed user %u to write to read-only file belonging to him",
                    pentry->obj_handle->attributes.owner ) ;
       }
      else
       {
         switch (preq->rq_vers)
           {
           case NFS_V2:
             pres->res_attr2.status = nfs2_Errno(cache_status);
             break;

           case NFS_V3:
             pres->res_write3.status = nfs3_Errno(cache_status);
             break;
           }
         rc = NFS_REQ_OK;
         goto out;
       }

      rc = NFS_REQ_OK;
      goto out;
    }

  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &pre_attr;

  /* Sanity check: write only a regular file */
  if(pre_attr.type != REGULAR_FILE)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          /*
           * In the RFC tell it not good but it does
           * not tell what to do ... 
           * We use NFSERR_ISDIR for lack of better
           */
          pres->res_attr2.status = NFSERR_ISDIR;
          break;

        case NFS_V3:
          if(pre_attr.type == DIRECTORY)
            pres->res_write3.status = NFS3ERR_ISDIR;
          else
            pres->res_write3.status = NFS3ERR_INVAL;
          break;
        }
      rc = NFS_REQ_OK;
      goto out;
    }

  /* For MDONLY export, reject write operation */
  /* Request of type MDONLY_RO were rejected at the nfs_rpc_dispatcher level */
  /* This is done by replying EDQUOT (this error is known for not disturbing the client's requests cache */
  if( pexport->access_type != ACCESSTYPE_RW )
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          switch( pexport->access_type )
            {
                case ACCESSTYPE_MDONLY:
                case ACCESSTYPE_MDONLY_RO:
                  pres->res_attr2.status = NFSERR_DQUOT;
                  break;

                case ACCESSTYPE_RO:
                  pres->res_attr2.status = NFSERR_ROFS;
                  break ;

                default:
                  assert(0); // if we get here than the value is an invalid enum - corruption
                  break ;
             }
           break ;

        case NFS_V3:
          switch( pexport->access_type )
            {
                case ACCESSTYPE_MDONLY:
                case ACCESSTYPE_MDONLY_RO:
                  pres->res_write3.status = NFS3ERR_DQUOT;
                  break;

                case ACCESSTYPE_RO:
                  pres->res_write3.status = NFS3ERR_ROFS;
                  break ;

                default:
                  assert(0); // if we get here than the value is an invalid enum - corruption
                  break ;
             }
          break;
        } /* switch (preq->rq_vers) */

      nfs_SetFailedStatus(pexport,
                          preq->rq_vers,
                          cache_status,
                          &pres->res_attr2.status,
                          &pres->res_write3.status,
                          NULL, NULL,
                          pentry,
                          ppre_attr,
                          &(pres->res_write3.WRITE3res_u.resfail.file_wcc),
                          NULL, NULL, NULL);

      rc = NFS_REQ_OK;
      goto out;
    }

#ifdef _USE_QUOTA
    /* if quota support is active, then we should check is the FSAL allows inode creation or not */
    fsal_status = pexport->export_hdl->ops->check_quota(pexport->export_hdl,
							pexport->fullpath, 
							FSAL_QUOTA_BLOCKS,
							creds) ;
    if( FSAL_IS_ERROR( fsal_status ) )
     {

       switch (preq->rq_vers)
         {
           case NFS_V2:
             pres->res_attr2.status = NFSERR_DQUOT;
             break;

           case NFS_V3:
             pres->res_write3.status = NFS3ERR_DQUOT;
             break;
         }

       rc = NFS_REQ_OK ;
       goto out;
     }
#endif /* _USE_QUOTA */


  /* Extract the argument from the request */
  switch (preq->rq_vers)
    {
    case NFS_V2:
      if(ppre_attr && ppre_attr->filesize > NFS2_MAX_FILESIZE)
        {
          /*
           *  V2 clients don't understand filesizes >
           *  2GB, so we don't allow them to alter
           *  them in any way. BJP 6/26/2001
           */
          pres->res_attr2.status = NFSERR_FBIG;
          rc = NFS_REQ_OK;
          goto out;
        }

      offset = parg->arg_write2.offset; /* beginoffset is obsolete */
      size = parg->arg_write2.data.nfsdata2_len;        /* totalcount is obsolete  */
      data = parg->arg_write2.data.nfsdata2_val;
      if (pexport->use_commit == TRUE)
        stability = CACHE_INODE_SAFE_WRITE_TO_FS;
      break;

    case NFS_V3:
      offset = parg->arg_write3.offset;
      size = parg->arg_write3.count;

      if(size > parg->arg_write3.data.data_len)
        {
          /* should never happen */
          pres->res_write3.status = NFS3ERR_INVAL;
          rc = NFS_REQ_OK;
          goto out;
        }

      if((pexport->use_commit == TRUE) &&
         (pexport->use_ganesha_write_buffer == FALSE) &&
         (parg->arg_write3.stable == UNSTABLE))
        {
          stability = CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER;
        }
      else if((pexport->use_commit == TRUE) &&
              (pexport->use_ganesha_write_buffer == TRUE) &&
              (parg->arg_write3.stable == UNSTABLE))
        {
          stability = CACHE_INODE_UNSAFE_WRITE_TO_GANESHA_BUFFER;
        }
      else
        {
          stability = CACHE_INODE_SAFE_WRITE_TO_FS;
        }
      data = parg->arg_write3.data.data_val;
      break;
    }

  /*
   * do not exceed maxium WRITE offset if set
   */
  if((pexport->options & EXPORT_OPTION_MAXOFFSETWRITE) == EXPORT_OPTION_MAXOFFSETWRITE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "-----> Write offset=%llu count=%llu MaxOffSet=%llu",
                   (unsigned long long) offset,
                   (unsigned long long) size,
                   (unsigned long long) pexport->MaxOffsetWrite);

      if((fsal_off_t) (offset + size) > pexport->MaxOffsetWrite)
        {
          LogEvent(COMPONENT_NFSPROTO,
                   "NFS WRITE: A client tryed to violate max file size %llu for exportid #%hu",
                   (unsigned long long) pexport->MaxOffsetWrite, pexport->id);

          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_attr2.status = NFSERR_DQUOT;
              break;

            case NFS_V3:
              pres->res_write3.status = NFS3ERR_INVAL;
              break;
            }

          nfs_SetFailedStatus(pexport,
                              preq->rq_vers,
                              cache_status,
                              &pres->res_attr2.status,
                              &pres->res_write3.status,
                              NULL, NULL,
                              pentry,
                              ppre_attr,
                              &(pres->res_write3.WRITE3res_u.resfail.file_wcc),
                              NULL, NULL, NULL);

          rc = NFS_REQ_OK;
          goto out;
        }
    }

  /*
   * We should take care not to exceed FSINFO wtmax
   * field for the size 
   */
  if(((pexport->options & EXPORT_OPTION_MAXWRITE) == EXPORT_OPTION_MAXWRITE) &&
     size > pexport->MaxWrite)
    {
      /*
       * The client asked for too much data, we
       * must restrict him 
       */
      size = pexport->MaxWrite;
    }

  if(size == 0)
    {
      cache_status = CACHE_INODE_SUCCESS;
      written_size = 0;
    }
  else
    {
      /* An actual write is to be made, prepare it */
      if((cache_inode_rdwr(pentry,
                           CACHE_INODE_WRITE,
                           offset,
                           size,
                           &written_size,
                           data,
                           &eof_met,
                           pclient,
                           creds,
                           stability,
                           &cache_status) == CACHE_INODE_SUCCESS) &&
         (cache_inode_getattr(pentry, &attr, pclient, pcontext,
                              &cache_status) == CACHE_INODE_SUCCESS)) {

          switch (preq->rq_vers)
            {
            case NFS_V2:
              nfs2_FSALattr_To_Fattr(pexport,
                                     &attr, &(pres->res_attr2.ATTR2res_u.attributes));

              pres->res_attr2.status = NFS_OK;
              break;

            case NFS_V3:

              /* Build Weak Cache Coherency data */
              nfs_SetWccData(pexport, ppre_attr,
                             &attr, &(pres->res_write3.WRITE3res_u.resok.file_wcc));

              /* Set the written size */
              pres->res_write3.WRITE3res_u.resok.count = written_size;

              /* How do we commit data ? */
              if(stability == CACHE_INODE_SAFE_WRITE_TO_FS)
                {
                  pres->res_write3.WRITE3res_u.resok.committed = FILE_SYNC;
                }
              else
                {
                  pres->res_write3.WRITE3res_u.resok.committed = UNSTABLE;
                }

              /* Set the write verifier */
              memcpy(pres->res_write3.WRITE3res_u.resok.verf,
                     NFS3_write_verifier, sizeof(writeverf3));

              pres->res_write3.status = NFS3_OK;
              break;
            }

          rc = NFS_REQ_OK;
          goto out;
        }
    }

  LogFullDebug(COMPONENT_NFSPROTO,
               "---> failed write: cache_status=%d", cache_status);

  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    {
      rc = NFS_REQ_DROP;
      goto out;
    }

  nfs_SetFailedStatus(pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_attr2.status,
                      &pres->res_write3.status,
                      NULL, NULL,
                      pentry,
                      ppre_attr,
                      &(pres->res_write3.WRITE3res_u.resfail.file_wcc), NULL, NULL, NULL);

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry, pclient);

  return (rc);

}                               /* nfs_Write.c */

/**
 * nfs_Write_Free: Frees the result structure allocated for nfs_Write.
 * 
 * Frees the result structure allocated for nfs_Write.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Write_Free(nfs_res_t * resp)
{
  return;
}                               /* nfs_Write_Free */
