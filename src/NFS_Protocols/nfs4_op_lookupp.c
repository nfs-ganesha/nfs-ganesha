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
 * \file    nfs4_op_lookupp.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.16 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lookupp.c : Routines used for managing the NFS4 COMPOUND functions.
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

/*==============================================================================
 *
 * Function:
 *	nfs4_op_lookupp - The NFS4_OP_LOOKUPP operation
 *
 * Synopsis:
 *   int nfs4_op_lookupp(  struct nfs_argop4 * op ,   
 *                         compound_data_t   * data,
 *                         struct nfs_resop4 * resp)
 *
 * Description:
 *      This functions handles the NFS4_OP_LOOKUPP operation in NFSv4. This function can be called only from nfs4_Compound
 *
 * Other Inputs:
 *
 * Outputs:
 *
  * Interfaces:
 *
 * Resources Used:
 *	memory
 *
 * Limitations:
 *
 * Assumptions:
 *  Notes:
 *
 *----------------------------------------------------------------------------*/

/**
 * nfs4_op_lookupp: looks up  for the parent into theFSAL.
 * 
 * looks up for the parent into the FSAL.In NFSv4 this is to be used instead of "LOOKUP( '..' )" .
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */
#define arg_LOOKUPP4 op->nfs_argop4_u.oplookupp
#define res_LOOKUPP4 resp->nfs_resop4_u.oplookupp

int nfs4_op_lookupp(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_name_t name;
  cache_entry_t *dir_pentry = NULL;
  cache_entry_t *file_pentry = NULL;
  fsal_attrib_list_t attrlookup;
  cache_inode_status_t cache_status;
  int error = 0;
  fsal_handle_t *pfsal_handle = NULL;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lookupp";

  resp->resop = NFS4_OP_LOOKUPP;
  resp->nfs_resop4_u.oplookupp.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LOOKUPP4.status = NFS4ERR_NOFILEHANDLE;
      return res_LOOKUPP4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LOOKUPP4.status = NFS4ERR_BADHANDLE;
      return res_LOOKUPP4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LOOKUPP4.status = NFS4ERR_FHEXPIRED;
      return res_LOOKUPP4.status;
    }

  /* looking up for parent directory from ROOTFH return NFS4ERR_NOENT (RFC3530, page 166) */
  if(data->currentFH.nfs_fh4_len == data->rootFH.nfs_fh4_len
     && memcmp(data->currentFH.nfs_fh4_val, data->rootFH.nfs_fh4_val,
               data->currentFH.nfs_fh4_len) == 0)
    {
      /* Nothing to do, just reply with success */
      res_LOOKUPP4.status = NFS4ERR_NOENT;
      return res_LOOKUPP4.status;
    }

  /* If in pseudoFS, proceed with pseudoFS specific functions */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_lookupp_pseudo(op, data, resp);

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_lookupp_xattr(op, data, resp);

  /* If data->exportp is null, a junction from pseudo fs was traversed, credp and exportp have to be updated */
  if(data->pexport == NULL)
    {
      if((error = nfs4_SetCompoundExport(data)) != NFS4_OK)
        {
          res_LOOKUPP4.status = error;
          return res_LOOKUPP4.status;
        }
    }

  /* Preparying for cache_inode_lookup ".." */
  file_pentry = NULL;
  dir_pentry = data->current_entry;
  name = FSAL_DOT_DOT;

  /* BUGAZOMEU: Faire la gestion des cross junction traverse */
  if((file_pentry = cache_inode_lookup(dir_pentry,
                                       &name,
                                       &attrlookup,
                                       data->ht,
                                       data->pclient,
                                       data->pcontext, &cache_status)) != NULL)
    {
      /* Extract the fsal attributes from the cache inode pentry */
      pfsal_handle = cache_inode_get_fsal_handle(file_pentry, &cache_status);
      if(cache_status != CACHE_INODE_SUCCESS)
        {
          res_LOOKUPP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUPP4.status;
        }

      /* Convert it to a file handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, pfsal_handle, data))
        {
          res_LOOKUPP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUPP4.status;
        }

      /* Copy this to the mounted on FH (if no junction is traversed */
      memcpy((char *)(data->mounted_on_FH.nfs_fh4_val),
             (char *)(data->currentFH.nfs_fh4_val), data->currentFH.nfs_fh4_len);
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

      /* Keep the pointer within the compound data */
      data->current_entry = file_pentry;
      data->current_filetype = file_pentry->internal_md.type;

      /* Return successfully */
      res_LOOKUPP4.status = NFS4_OK;
      return NFS4_OK;

    }

  /* If the part of the code is reached, then something wrong occured in the lookup process, status is not HPSS_E_NOERROR 
   * and contains the code for the error */

  /* If NFS4ERR_SYMLINK should be returned for a symlink instead of ENOTDIR */
  if((cache_status == CACHE_INODE_NOT_A_DIRECTORY) &&
     (dir_pentry->internal_md.type == SYMBOLIC_LINK))
    res_LOOKUPP4.status = NFS4ERR_SYMLINK;
  else
    res_LOOKUPP4.status = nfs4_Errno(cache_status);

  return res_LOOKUPP4.status;
}                               /* nfs4_op_lookupp */

/**
 * nfs4_op_lookupp_Free: frees what was allocared to handle nfs4_op_lookupp.
 * 
 * Frees what was allocared to handle nfs4_op_lookupp.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_lookupp_Free(LOOKUPP4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_lookupp_Free */
