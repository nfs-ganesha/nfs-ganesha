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
 * \file    nfs_Readlink.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.13 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Readlink.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs_Readlink: The NFS PROC2 and PROC3 READLINK.
 *
 *  Implements the NFS PROC2-3 READLINK function. 
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @see cache_inode_readlink
 *
 *----------------------------------------------------------------------------*/

int nfs_Readlink(nfs_arg_t * parg,
                 exportlist_t * pexport,
                 fsal_op_context_t * pcontext,
                 cache_inode_client_t * pclient,
                 hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Readlink";

  cache_entry_t *pentry = NULL;
  fsal_attrib_list_t attr;
  cache_inode_file_type_t filetype;
  cache_inode_status_t cache_status;
  int rc;
  fsal_path_t symlink_data;
  char *ptr = NULL;

  if (preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_readlink3.READLINK3res_u.resfail.symlink_attributes.attributes_follow =
          FALSE;
    }
  /* Convert file handle into a vnode */
  if ((pentry = nfs_FhandleToCache(preq->rq_vers,
                                   &(parg->arg_readlink2),
                                   &(parg->arg_readlink3.symlink),
                                   NULL,
                                   &(pres->res_readlink2.status),
                                   &(pres->res_readlink3.status),
                                   NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Extract the filetype */
  filetype = cache_inode_fsal_type_convert(attr.type);

  /* Sanity Check: the pentry must be a link */
  if (filetype != SYMBOLIC_LINK)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_readlink2.status = NFSERR_IO;
          break;

        case NFS_V3:
          pres->res_readlink3.status = NFS3ERR_INVAL;
        }                       /* switch */
      return NFS_REQ_OK;
    }

  /* if */
  /* Perform readlink on the pentry */
  if (cache_inode_readlink(pentry,
                           &symlink_data,
                           ht, pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
    {
      if ((ptr = Mem_Alloc(FSAL_MAX_NAME_LEN)) == NULL)
        {
          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_readlink2.status = NFSERR_NXIO;
              break;

            case NFS_V3:
              pres->res_readlink3.status = NFS3ERR_IO;
            }                   /* switch */
          return NFS_REQ_OK;
        }

      strcpy(ptr, symlink_data.path);

      /* Reply to the client (think about Mem_Free data after use ) */
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_readlink2.READLINK2res_u.data = ptr;
          pres->res_readlink2.status = NFS_OK;
          break;

        case NFS_V3:
          pres->res_readlink3.READLINK3res_u.resok.data = ptr;
          nfs_SetPostOpAttr(pcontext, pexport,
                            pentry,
                            &attr,
                            &(pres->res_readlink3.READLINK3res_u.resok.
                              symlink_attributes));
          pres->res_readlink3.status = NFS3_OK;
          break;
        }
      return NFS_REQ_OK;
    }

  /* If we are here, there was an error */
  if (nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;
    }

  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_readlink2.status,
                      &pres->res_readlink3.status,
                      pentry,
                      &(pres->res_readlink3.READLINK3res_u.resfail.symlink_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs_Readlink */

/**
 * nfs2_Readlink_Free: Frees the result structure allocated for nfs2_Readlink.
 * 
 * Frees the result structure allocated for nfs2_Readlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Readlink_Free(nfs_res_t * resp)
{
  if (resp->res_readlink2.status == NFS_OK)
    Mem_Free(resp->res_readlink2.READLINK2res_u.data);
}                               /* nfs2_Readlink_Free */

/**
 * nfs3_Readlink_Free: Frees the result structure allocated for nfs3_Readlink.
 * 
 * Frees the result structure allocated for nfs3_Readlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Readlink_Free(nfs_res_t * resp)
{
  if (resp->res_readlink3.status == NFS3_OK)
    Mem_Free(resp->res_readlink3.READLINK3res_u.resok.data);
}                               /* nfs3_Readlink_Free */
