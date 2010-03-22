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
 * \file    nfs_Symlink.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:54 $
 * \version $Revision: 1.17 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Symlink.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"

/**
 *
 * nfs_Create: The NFS PROC2 and PROC3 SYMLINK
 *
 * Implements the NFS PROC SYMLINK function (for V2 and V3).
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

int nfs_Symlink(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_op_context_t * pcontext /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ )
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Symlink";

  char *str_symlink_name = NULL;
  fsal_name_t symlink_name;
  char *str_target_path = NULL;
  cache_inode_create_arg_t create_arg;
  fsal_accessmode_t mode = 0777;
  cache_entry_t *symlink_pentry;
  cache_entry_t *parent_pentry;
  cache_inode_file_type_t parent_filetype;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t attr_symlink;
  fsal_attrib_list_t attributes_symlink;
  fsal_attrib_list_t attr_parent_after;
  fsal_attrib_list_t *ppre_attr;
  int rc;
  cache_inode_status_t cache_status;
  cache_inode_status_t cache_status_parent;
  fsal_handle_t *pfsal_handle;

  if (preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_symlink3.SYMLINK3res_u.resfail.dir_wcc.before.attributes_follow = FALSE;
      pres->res_symlink3.SYMLINK3res_u.resfail.dir_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  /* Convert directory file handle into a vnode */
  if ((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                          &(parg->arg_symlink2.from.dir),
                                          &(parg->arg_symlink3.where.dir),
                                          NULL,
                                          &(pres->res_stat2),
                                          &(pres->res_symlink3.status),
                                          NULL,
                                          &parent_attr,
                                          pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &parent_attr;

  /* Extract the filetype */
  parent_filetype = cache_inode_fsal_type_convert(parent_attr.type);

  /*
   * Sanity checks: new directory name must be non-null; parent must be
   * a directory. 
   */
  if (parent_filetype != DIR_BEGINNING && parent_filetype != DIR_CONTINUE)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_stat2 = NFSERR_NOTDIR;
          break;

        case NFS_V3:
          pres->res_symlink3.status = NFS3ERR_NOTDIR;
          break;
        }

      return NFS_REQ_OK;
    }

  switch (preq->rq_vers)
    {
    case NFS_V2:
      str_symlink_name = parg->arg_symlink2.from.name;
      str_target_path = parg->arg_symlink2.to;
      break;

    case NFS_V3:
      str_symlink_name = parg->arg_symlink3.where.name;
      str_target_path = parg->arg_symlink3.symlink.symlink_data;
      break;
    }

  symlink_pentry = NULL;

  if (str_symlink_name == NULL ||
      strlen(str_symlink_name) == 0 ||
      str_target_path == NULL ||
      strlen(str_target_path) == 0 ||
      FSAL_IS_ERROR(FSAL_str2name(str_symlink_name, FSAL_MAX_NAME_LEN, &symlink_name)) ||
      FSAL_IS_ERROR(FSAL_str2path
                    (str_target_path, FSAL_MAX_PATH_LEN, &create_arg.link_content)))
    {
      cache_status = CACHE_INODE_INVALID_ARGUMENT;
    }
  else
    {
      /* Make the symlink */
      if ((symlink_pentry = cache_inode_create(parent_pentry,
                                               &symlink_name,
                                               SYMBOLIC_LINK,
                                               mode,
                                               &create_arg,
                                               &attr_symlink,
                                               ht,
                                               pclient, pcontext, &cache_status)) != NULL)
        {
          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_stat2 = NFS_OK;
              break;

            case NFS_V3:
              /* Build file handle */
              if ((pfsal_handle = cache_inode_get_fsal_handle(symlink_pentry,
                                                              &cache_status)) == NULL)
                {
                  pres->res_symlink3.status = NFS3ERR_IO;
                  return NFS_REQ_OK;
                }

              /* Some clients (like the Spec NFS benchmark) set attributes with the NFSPROC3_SYMLINK request */
              if (nfs3_Sattr_To_FSALattr(&attributes_symlink,
                                         &parg->arg_symlink3.symlink.
                                         symlink_attributes) == 0)
                {
                  pres->res_create3.status = NFS3ERR_INVAL;
                  return NFS_REQ_OK;
                }

              /* Mode is managed above (in cache_inode_create), there is no need 
               * to manage it */
              if (attributes_symlink.asked_attributes & FSAL_ATTR_MODE)
                attributes_symlink.asked_attributes &= ~FSAL_ATTR_MODE;

              /* Some clients (like Solaris 10) try to set the size of the file to 0
               * at creation time. The FSAL create empty file, so we ignore this */
              if (attributes_symlink.asked_attributes & FSAL_ATTR_SIZE)
                attributes_symlink.asked_attributes &= ~FSAL_ATTR_SIZE;

              if (attributes_symlink.asked_attributes & FSAL_ATTR_SPACEUSED)
                attributes_symlink.asked_attributes &= ~FSAL_ATTR_SPACEUSED;

              /* Are there attributes to be set (additional to the mode) ? */
              if (attributes_symlink.asked_attributes != 0ULL &&
                  attributes_symlink.asked_attributes != FSAL_ATTR_MODE)
                {
                  /* A call to cache_inode_setattr is required */
                  if (cache_inode_setattr(symlink_pentry,
                                          &attributes_symlink,
                                          ht,
                                          pclient,
                                          pcontext, &cache_status) != CACHE_INODE_SUCCESS)
                    {
                      /* If we are here, there was an error */
                      nfs_SetFailedStatus(pcontext, pexport,
                                          preq->rq_vers,
                                          cache_status,
                                          &pres->res_dirop2.status,
                                          &pres->res_symlink3.status,
                                          NULL, NULL,
                                          parent_pentry,
                                          ppre_attr,
                                          &(pres->res_symlink3.SYMLINK3res_u.resok.
                                            dir_wcc), NULL, NULL, NULL);

                      if (nfs_RetryableError(cache_status))
                        return NFS_REQ_DROP;

                      return NFS_REQ_OK;
                    }
                }

              if ((pres->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle.data.
                   data_val = Mem_Alloc(NFS3_FHSIZE)) == NULL)
                {
                  pres->res_symlink3.status = NFS3ERR_IO;
                  return NFS_REQ_OK;
                }

              if (nfs3_FSALToFhandle
                  (&pres->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle,
                   pfsal_handle, pexport) == 0)
                {
                  Mem_Free((char *)pres->res_symlink3.SYMLINK3res_u.resok.obj.
                           post_op_fh3_u.handle.data.data_val);

                  pres->res_symlink3.status = NFS3ERR_BADHANDLE;
                  return NFS_REQ_OK;
                }

              /* The the parent pentry attributes for building Wcc Data */
              if (cache_inode_getattr(parent_pentry,
                                      &attr_parent_after,
                                      ht,
                                      pclient,
                                      pcontext,
                                      &cache_status_parent) != CACHE_INODE_SUCCESS)
                {
                  Mem_Free((char *)pres->res_symlink3.SYMLINK3res_u.resok.obj.
                           post_op_fh3_u.handle.data.data_val);

                  pres->res_symlink3.status = NFS3ERR_BADHANDLE;
                  return NFS_REQ_OK;
                }

              /* Set Post Op Fh3 structure */
              pres->res_symlink3.SYMLINK3res_u.resok.obj.handle_follows = TRUE;
              pres->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle.data.
                  data_len = sizeof(file_handle_v3_t);

              /* Build entry attributes */
              nfs_SetPostOpAttr(pcontext, pexport,
                                symlink_pentry,
                                &attr_symlink,
                                &(pres->res_symlink3.SYMLINK3res_u.resok.obj_attributes));

              /* Build Weak Cache Coherency data */
              nfs_SetWccData(pcontext, pexport,
                             parent_pentry,
                             ppre_attr,
                             &attr_parent_after,
                             &(pres->res_symlink3.SYMLINK3res_u.resok.dir_wcc));

              pres->res_symlink3.status = NFS3_OK;
              break;
            }                   /* switch */

          return NFS_REQ_OK;
        }
    }

  /* If we are here, there was an error */
  if (nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;
    }

  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_stat2,
                      &pres->res_symlink3.status,
                      NULL, NULL,
                      parent_pentry,
                      ppre_attr,
                      &(pres->res_symlink3.SYMLINK3res_u.resfail.dir_wcc),
                      NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs_Symlink */

/**
 * nfs_Symlink_Free: Frees the result structure allocated for nfs_Symlink.
 * 
 * Frees the result structure allocated for nfs_Symlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Symlink_Free(nfs_res_t * resp)
{
  if ((resp->res_symlink3.status == NFS3_OK) &&
      (resp->res_symlink3.SYMLINK3res_u.resok.obj.handle_follows == TRUE))
    Mem_Free(resp->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle.data.
             data_val);
}                               /* nfs_Symlink_Free */
