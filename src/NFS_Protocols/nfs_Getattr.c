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
 * \file	  nfs_Geattr.c 
 * \author  $Author: deniel $
 * \data    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.15 $
 * \brief   Implements NFS PROC2 GETATTR and NFS PROC3 GETATTR.
 *
 *  Implements the GETATTR function in V2 and V3. This function is used by
 *  the client to get attributes about a filehandle.
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
#include "nfs_proto_tools.h"

/**
 *
 * nfs_Getattr: get attributes for a file
 *
 * Get attributes for a file. Implements  NFS PROC2 GETATTR and NFS PROC3 GETATTR.
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

int nfs_Getattr(nfs_arg_t * parg,
                exportlist_t * pexport,
                fsal_op_context_t * pcontext,
                cache_inode_client_t * pclient,
                hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Getattr";

  fsal_attrib_list_t attr;
  cache_entry_t *pentry = NULL;
  cache_inode_status_t cache_status;
  int rc = 0;

  if ((pentry = nfs_FhandleToCache(preq->rq_vers,
                                   &(parg->arg_getattr2),
                                   &(parg->arg_getattr3.object),
                                   NULL,
                                   &(pres->res_attr2.status),
                                   &(pres->res_getattr3.status),
                                   NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  if ((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_getattr3.object))))
    return nfs3_Getattr_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  /*
   * Get attributes.  Use NULL for the file name since we have the
   * vnode to define the file. 
   */
  if (cache_inode_getattr(pentry,
                          &attr,
                          ht, pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
    {
      /*
       * Client API should be keeping us from crossing junctions,
       * but double check to be sure. 
       */

      switch (preq->rq_vers)
        {

        case NFS_V2:
          /* Copy data from vattr to Attributes */
          if (nfs2_FSALattr_To_Fattr(pexport, &attr,
                                     &(pres->res_attr2.ATTR2res_u.attributes)) == 0)
            {
              nfs_SetFailedStatus(pcontext, pexport,
                                  preq->rq_vers,
                                  CACHE_INODE_INVALID_ARGUMENT,
                                  &pres->res_attr2.status,
                                  &pres->res_getattr3.status,
                                  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
              return NFS_REQ_OK;
            }
          pres->res_attr2.status = NFS_OK;
          break;

        case NFS_V3:
          if (nfs3_FSALattr_To_Fattr(pexport, &attr,
                                     &(pres->res_getattr3.GETATTR3res_u.
                                       resok.obj_attributes)) == 0)
            {
              nfs_SetFailedStatus(pcontext, pexport,
                                  preq->rq_vers,
                                  CACHE_INODE_INVALID_ARGUMENT,
                                  &pres->res_attr2.status,
                                  &pres->res_getattr3.status,
                                  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

              return NFS_REQ_OK;
            }
          pres->res_getattr3.status = NFS3_OK;
          break;
        }                       /* switch */

      return NFS_REQ_OK;
    }

  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      CACHE_INODE_INVALID_ARGUMENT,
                      &pres->res_attr2.status,
                      &pres->res_getattr3.status,
                      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs_Getattr */

/**
 * nfs_Getattr_Free: Frees the result structure allocated for nfs_Getattr.
 * 
 * Frees the result structure allocated for nfs_Getattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Getattr_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Getattr_Free */
