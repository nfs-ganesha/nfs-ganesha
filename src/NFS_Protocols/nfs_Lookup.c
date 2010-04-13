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
 *  nfs_Lookup: The NFS PROC2 and PROC3 LOOKUP
 *
 * Implements the NFS PROC LOOKUP function (for V2 and V3).
 * 
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK (this routine does nothing)
 *
 */

int nfs_Lookup(nfs_arg_t * parg,
               exportlist_t * pexport,
               fsal_op_context_t * pcontext,
               cache_inode_client_t * pclient,
               hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Lookup";

  cache_entry_t *pentry_dir;
  cache_entry_t *pentry_file;
  cache_inode_status_t cache_status;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attrdir;
  char *strpath = NULL;
  char strname[MAXNAMLEN];
  unsigned int xattr_found = FALSE;
  fsal_name_t name;
  fsal_handle_t *pfsal_handle;
  int rc;

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
                                      &attrdir, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
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
  pentry_file = NULL;

#ifndef _NO_XATTRD
  /* Is this a .xattr.d.<object> name ? */
  if(nfs_XattrD_Name(strpath, strname))
    {
      strpath = strname;
      xattr_found = TRUE;
    }
#endif

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_lookup3.what.dir))))
    return nfs3_Lookup_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  if((cache_status = cache_inode_error_convert(FSAL_str2name(strpath,
                                                             FSAL_MAX_NAME_LEN,
                                                             &name))) ==
     CACHE_INODE_SUCCESS)
    {
      /* BUGAZOMEU: Faire la gestion des cross junction traverse */
      if((pentry_file = cache_inode_lookup(pentry_dir,
                                           &name,
                                           &attr,
                                           ht, pclient, pcontext, &cache_status)) != NULL)
        {
          /* Do not forget cross junction management */
          pfsal_handle = cache_inode_get_fsal_handle(pentry_file, &cache_status);
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
                                                &(pres->res_dirop2.DIROP2res_u.
                                                  diropok.attributes)))
                        {
                          pres->res_dirop2.status = NFS_OK;
                        }
                    }
                  break;

                case NFS_V3:
                  /* Build FH */
                  if((pres->res_lookup3.LOOKUP3res_u.resok.object.data.data_val =
                      Mem_Alloc(NFS3_FHSIZE)) == NULL)
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
                                                     (pres->res_lookup3.
                                                      LOOKUP3res_u.resok.object.data),
                                                     (nfs_fh3 *) & (pres->
                                                                    res_lookup3.LOOKUP3res_u.
                                                                    resok.object.data));
                              /* Build entry attributes */
                              nfs_SetPostOpXAttrDir(pcontext, pexport,
                                                    &attr,
                                                    &(pres->res_lookup3.
                                                      LOOKUP3res_u.resok.obj_attributes));

                            }
                          else
#endif
                            /* Build entry attributes */
                            nfs_SetPostOpAttr(pcontext, pexport,
                                              pentry_file,
                                              &attr,
                                              &(pres->res_lookup3.LOOKUP3res_u.
                                                resok.obj_attributes));

                          /* Build directory attributes */
                          nfs_SetPostOpAttr(pcontext, pexport,
                                            pentry_dir,
                                            &attrdir,
                                            &(pres->res_lookup3.LOOKUP3res_u.
                                              resok.dir_attributes));

                          pres->res_lookup3.status = NFS3_OK;
                        }       /* if */
                    }           /* else */
                  break;
                }               /* case */
            }                   /* if( cache_status == CACHE_INODE_SUCCESS ) */
        }                       /* if( ( pentry_file = cache_inode_lookup... */
    }

  /* if( ( cache_status = cache_inode_error_convert... */
  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;
    }

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      nfs_SetFailedStatus(pcontext, pexport,
                          preq->rq_vers,
                          cache_status,
                          &pres->res_dirop2.status,
                          &pres->res_lookup3.status,
                          pentry_dir,
                          &(pres->res_lookup3.LOOKUP3res_u.resfail.dir_attributes),
                          NULL, NULL, NULL, NULL, NULL, NULL);
    }

  return NFS_REQ_OK;
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
    Mem_Free(resp->res_lookup3.LOOKUP3res_u.resok.object.data.data_val);
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
