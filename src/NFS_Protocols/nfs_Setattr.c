/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
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

  if (preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_setattr3.SETATTR3res_u.resfail.obj_wcc.before.attributes_follow = FALSE;
      pres->res_setattr3.SETATTR3res_u.resfail.obj_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  /* Convert file handle into a vnode */
  if ((pentry = nfs_FhandleToCache(preq->rq_vers,
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

  if ((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_setattr3.object))))
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
      if ((parg->arg_setattr2.attributes.size != (u_int) - 1) &&
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

      if (nfs2_Sattr_To_FSALattr(&setattr, &new_attributes2) == 0)
        {
          pres->res_attr2.status = NFSERR_IO;
          return NFS_REQ_OK;
        }

      if (new_attributes2.size != (u_int) - 1)
        do_trunc = TRUE;

      break;

    case NFS_V3:
      new_attributes3 = parg->arg_setattr3.new_attributes;

      if (parg->arg_setattr3.guard.check == TRUE)
        {
          /* This pack of lines implements the "guard check" setattr.
           * This feature of nfsv3 is used to avoid several setattr 
           * to occur concurently on the same object, from different clients */
          fattr3 attributes;

          if (nfs3_FSALattr_To_Fattr(pexport, ppre_attr, &attributes) == 0)
            {
              pres->res_setattr3.status = NFS3ERR_NOT_SYNC;
              return NFS_REQ_OK;
            }
#ifdef _DEBUG_NFSPROTO
          printf("css=%d acs=%d    csn=%d acn=%d\n",
                 parg->arg_setattr3.guard.sattrguard3_u.obj_ctime.seconds,
                 attributes.ctime.seconds,
                 parg->arg_setattr3.guard.sattrguard3_u.obj_ctime.nseconds,
                 attributes.ctime.nseconds);
#endif
          if ((parg->arg_setattr3.guard.sattrguard3_u.obj_ctime.seconds !=
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
      if (nfs3_Sattr_To_FSALattr(&setattr, &new_attributes3) == 0)
        {
          pres->res_setattr3.status = NFS3ERR_INVAL;
          return NFS_REQ_OK;
        }

      if (new_attributes3.size.set_it)
        {
          do_trunc = TRUE;
        }

      break;
    }

  /*
   * trunc may change Xtime so we have to start with trunc and finish
   * by the mtime and atime 
   */
  if (do_trunc)
    {
      /* Should not be done on a directory */
      if (pentry->internal_md.type == DIR_BEGINNING
          || pentry->internal_md.type == DIR_CONTINUE)
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

  if (cache_status == CACHE_INODE_SUCCESS)
    {
      /* Add code to support partially completed setattr */
      if (do_trunc)
        {
          if (setattr.asked_attributes != 0)
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

  if (cache_status == CACHE_INODE_SUCCESS)
    {
      /* Set the NFS return */
      switch (preq->rq_vers)
        {
        case NFS_V2:
          /* Copy data from vattr to Attributes */
          if (nfs2_FSALattr_To_Fattr
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
#ifdef _DEBUG_NFSPROTO
  printf("nfs_Setattr: failed\n");
#endif

  /* If we are here, there was an error */
  if (nfs_RetryableError(cache_status))
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
