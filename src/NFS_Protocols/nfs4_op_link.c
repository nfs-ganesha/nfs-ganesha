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
 * \file    nfs4_op_link.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.11 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_link.c : Routines used for managing the NFS4 COMPOUND functions.
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

/**
 * nfs4_op_link: The NFS4_OP_LINK operation.
 * 
 * This functions handles the NFS4_OP_LINK operation in NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

#define arg_LINK4 op->nfs_argop4_u.oplink
#define res_LINK4 resp->nfs_resop4_u.oplink

int nfs4_op_link(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_entry_t *dir_pentry = NULL;
  cache_entry_t *file_pentry = NULL;

  cache_inode_status_t cache_status;

  int error;
  fsal_attrib_list_t attr;
  fsal_name_t newname;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_link";

  resp->resop = NFS4_OP_LINK;
  res_LINK4.status = NFS4_OK;

  /* If there is no FH */
  if (nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LINK4.status = NFS4ERR_NOFILEHANDLE;
      return res_LINK4.status;
    }

  /* If the filehandle is invalid */
  if (nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LINK4.status = NFS4ERR_BADHANDLE;
      return res_LINK4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if (nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LINK4.status = NFS4ERR_FHEXPIRED;
      return res_LINK4.status;
    }

  /* If there is no FH */
  if (nfs4_Is_Fh_Empty(&(data->savedFH)))
    {
      res_LINK4.status = NFS4ERR_NOFILEHANDLE;
      return res_LINK4.status;
    }

  /* If the filehandle is invalid */
  if (nfs4_Is_Fh_Invalid(&(data->savedFH)))
    {
      res_LINK4.status = NFS4ERR_BADHANDLE;
      return res_LINK4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if (nfs4_Is_Fh_Expired(&(data->savedFH)))
    {
      res_LINK4.status = NFS4ERR_FHEXPIRED;
      return res_LINK4.status;
    }

  /* Pseudo Fs is explictely a Read-Only File system */
  if (nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_LINK4.status = NFS4ERR_ROFS;
      return res_LINK4.status;
    }

  /* If data->exportp is null, a junction from pseudo fs was traversed, credp and exportp have to be updated */
  if (data->pexport == NULL)
    {
      if ((error = nfs4_SetCompoundExport(data)) != NFS4_OK)
        {
          res_LINK4.status = error;
          return res_LINK4.status;
        }
    }

  /*
   * This operation creates a hard link, for the file represented by the saved FH, in directory represented by currentFH under the 
   * name arg_LINK4.target 
   */

  /* Crossing device is not allowed */
  if (((file_handle_v4_t *) (data->currentFH.nfs_fh4_val))->exportid !=
      ((file_handle_v4_t *) (data->savedFH.nfs_fh4_val))->exportid)
    {
      res_LINK4.status = NFS4ERR_XDEV;
      return res_LINK4.status;
    }

  /* If name is empty, return EINVAL */
  if (arg_LINK4.newname.utf8string_len == 0)
    {
      res_LINK4.status = NFS4ERR_INVAL;
      return res_LINK4.status;
    }

  /* Check for name to long */
  if (arg_LINK4.newname.utf8string_len > FSAL_MAX_NAME_LEN)
    {
      res_LINK4.status = NFS4ERR_NAMETOOLONG;
      return res_LINK4.status;
    }

  /* Convert the UFT8 objname to a regular string */
  if ((cache_status =
       cache_inode_error_convert(FSAL_buffdesc2name
                                 ((fsal_buffdesc_t *) & arg_LINK4.newname,
                                  &newname))) != CACHE_INODE_SUCCESS)
    {
      res_LINK4.status = nfs4_Errno(cache_status);
      return res_LINK4.status;
    }

  /* Sanity check: never create a link named '.' or '..' */
  if (!FSAL_namecmp(&newname, &FSAL_DOT) || !FSAL_namecmp(&newname, &FSAL_DOT_DOT))
    {
      res_LINK4.status = NFS4ERR_BADNAME;
      return res_LINK4.status;
    }

  /* get info from compound data */
  dir_pentry = data->current_entry;

  /* Destination FH (the currentFH) must be a directory */
  if (data->current_filetype != DIR_BEGINNING && data->current_filetype != DIR_CONTINUE)
    {
      res_LINK4.status = NFS4ERR_NOTDIR;
      return res_LINK4.status;
    }

  /* Target object (the savedFH) must not be a directory */
  if (data->saved_filetype == DIR_BEGINNING || data->saved_filetype == DIR_CONTINUE)
    {
      res_LINK4.status = NFS4ERR_ISDIR;
      return res_LINK4.status;
    }

  /* We have to keep track of the 'change' file attribute for reply structure */
  if ((cache_status = cache_inode_getattr(dir_pentry,
                                          &attr,
                                          data->ht,
                                          data->pclient,
                                          data->pcontext,
                                          &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_LINK4.status = nfs4_Errno(cache_status);
      return res_LINK4.status;
    }
  memset(&(res_LINK4.LINK4res_u.resok4.cinfo.before), 0, sizeof(changeid4));
  res_LINK4.LINK4res_u.resok4.cinfo.before = (changeid4) dir_pentry->internal_md.mod_time;

  /* Convert savedFH into a vnode */
  file_pentry = data->saved_entry;

  /* make the link */
  if (cache_inode_link(file_pentry,
                       dir_pentry,
                       &newname,
                       &attr,
                       data->ht,
                       data->pclient,
                       data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_LINK4.status = nfs4_Errno(cache_status);
      return res_LINK4.status;
    }

  memset(&(res_LINK4.LINK4res_u.resok4.cinfo.after), 0, sizeof(changeid4));
  res_LINK4.LINK4res_u.resok4.cinfo.after = (changeid4) dir_pentry->internal_md.mod_time;
  res_LINK4.LINK4res_u.resok4.cinfo.atomic = TRUE;

  res_LINK4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_link */

/**
 * nfs4_op_link_Free: frees what was allocared to handle nfs4_op_link.
 * 
 * Frees what was allocared to handle nfs4_op_link.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_link_Free(LINK4res * resp)
{
  /* Nothing to Mem_Free */
  return;
}                               /* nfs4_op_link_Free */
