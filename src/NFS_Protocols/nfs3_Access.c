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
 * \file    nfs3_Access.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.11 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_Access.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_proto_tools.h"
#include "nfs_tools.h"

/**
 * nfs2_Access: Implements NFSPROC3_ACCESS.
 *
 * Implements NFSPROC3_ACCESS.
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

int nfs3_Access(nfs_arg_t * parg,
                exportlist_t * pexport,
                fsal_op_context_t * pcontext,
                cache_inode_client_t * pclient,
                hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs3_Access";

  fsal_accessflags_t access_mode;
  cache_inode_status_t cache_status;
  cache_inode_file_type_t filetype;
  cache_entry_t *pentry = NULL;
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t attr;

  /* Is this a xattr FH ? */
  if (nfs3_Is_Fh_Xattr(&(parg->arg_access3.object)))
    return nfs3_Access_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  /* to avoid setting it on each error case */
  pres->res_access3.ACCESS3res_u.resfail.obj_attributes.attributes_follow = FALSE;

  /* Convert file handle into a fsal_handle */
  if (nfs3_FhandleToFSAL(&(parg->arg_access3.object), &fsal_data.handle, pcontext) == 0)
    return NFS_REQ_DROP;

  /* Get cache_inode related entry, but looking up at it */
  fsal_data.cookie = DIR_START;

  /* Get the entry in the cache_inode */
  if ((pentry = cache_inode_get(&fsal_data,
                                &attr, ht, pclient, pcontext, &cache_status)) == NULL)
    {
      if (nfs_RetryableError(cache_status))
        {
          return NFS_REQ_DROP;
        }
      else
        {
          pres->res_access3.status = nfs3_Errno(cache_status);
          return NFS_REQ_OK;
        }
    }

  /* Get file type */
  filetype = cache_inode_fsal_type_convert(attr.type);

  access_mode = 0;

  if (parg->arg_access3.access & ACCESS3_READ)
    access_mode |= FSAL_R_OK;

  if (parg->arg_access3.access & (ACCESS3_MODIFY | ACCESS3_EXTEND))
    access_mode |= FSAL_W_OK;

  if (filetype == REGULAR_FILE)
    {
      if (parg->arg_access3.access & ACCESS3_EXECUTE)
        access_mode |= FSAL_X_OK;
    }
  else if (parg->arg_access3.access & ACCESS3_LOOKUP)
    access_mode |= X_OK;

  /* Perform the 'access' call */
  if (cache_inode_access(pentry,
                         access_mode,
                         ht, pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
    {
      /* In Unix, delete permission only applies to directories */

      if (filetype == DIR_BEGINNING || filetype == DIR_CONTINUE)
        pres->res_access3.ACCESS3res_u.resok.access = parg->arg_access3.access;
      else
        pres->res_access3.ACCESS3res_u.resok.access =
            (parg->arg_access3.access & ~ACCESS3_DELETE);

      /* Build Post Op Attributes */
      nfs_SetPostOpAttr(pcontext,
                        pexport,
                        pentry,
                        &attr, &(pres->res_access3.ACCESS3res_u.resok.obj_attributes));

      pres->res_access3.status = NFS3_OK;
      return NFS_REQ_OK;
    }

  if (cache_status == CACHE_INODE_FSAL_EACCESS)
    {
      /*
       * We have to determine which access bits are good one by one 
       */
      pres->res_access3.ACCESS3res_u.resok.access = 0;

      if (cache_inode_access(pentry,
                             FSAL_R_OK,
                             ht, pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
        pres->res_access3.ACCESS3res_u.resok.access |= ACCESS3_READ;

      if (cache_inode_access(pentry,
                             FSAL_W_OK,
                             ht, pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
        pres->res_access3.ACCESS3res_u.resok.access |= (ACCESS3_MODIFY | ACCESS3_EXTEND);

      if (filetype == REGULAR_FILE)
        {
          if (cache_inode_access(pentry,
                                 FSAL_X_OK,
                                 ht,
                                 pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
            pres->res_access3.ACCESS3res_u.resok.access |= ACCESS3_EXECUTE;
        }
      else
        {
          if (cache_inode_access(pentry,
                                 FSAL_X_OK,
                                 ht,
                                 pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
            pres->res_access3.ACCESS3res_u.resok.access |= ACCESS3_LOOKUP;
        }

      pres->res_access3.status = NFS3_OK;
      return NFS_REQ_OK;
    }

  /* If we are here, there was an error */
  if (nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;
    }

  nfs_SetFailedStatus(pcontext, pexport,
                      NFS_V3,
                      cache_status,
                      NULL,
                      &pres->res_access3.status,
                      pentry,
                      &(pres->res_access3.ACCESS3res_u.resfail.obj_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);
  return NFS_REQ_OK;

}                               /* nfs3_Access */

/**
 * nfs3_Access_Free: Frees the result structure allocated for nfs3_Access.
 * 
 * Frees the result structure allocated for nfs3_Access.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Access_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* nfs3_Access_Free */
