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
 * \file    nfs4_op_nverify.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:52 $
 * \version $Revision: 1.12 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_nverify.c : Routines used for managing the NFS4 COMPOUND functions.
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
 *
 * nfs4_op_nverify: Implemtation of NFS4_OP_NVERIFY
 * 
 * Implemtation of NFS4_OP_NVERIFY. This is usually made for cache validator implementation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

#define arg_NVERIFY4 op->nfs_argop4_u.opnverify
#define res_NVERIFY4 resp->nfs_resop4_u.opnverify

int nfs4_op_nverify(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_attrib_list_t file_attr;
  cache_inode_status_t cache_status;
  fattr4 file_attr4;
  int rc = 0;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_nverify";

  resp->resop = NFS4_OP_NVERIFY;
  res_NVERIFY4.status = NFS4_OK;

  /* If there is no FH */
  if (nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4ERR_NOFILEHANDLE;
      return NFS4ERR_NOFILEHANDLE;
    }

  /* If the filehandle is invalid */
  if (nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4ERR_BADHANDLE;
      return NFS4ERR_BADHANDLE;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if (nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4ERR_FHEXPIRED;
      return NFS4ERR_FHEXPIRED;
    }

  /* operation is always permitted on pseudofs */
  if (nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4_OK;
      return res_NVERIFY4.status;
    }

  /* Get only attributes that are allowed to be read */
  if (!nfs4_Fattr_Check_Access(&arg_NVERIFY4.obj_attributes, FATTR4_ATTR_READ))
    {
      res_NVERIFY4.status = NFS4ERR_INVAL;
      return res_NVERIFY4.status;
    }

  /* Ask only for supported attributes */
  if (!nfs4_Fattr_Supported(&arg_NVERIFY4.obj_attributes))
    {
      res_NVERIFY4.status = NFS4ERR_ATTRNOTSUPP;
      return res_NVERIFY4.status;
    }

  /* Get the cache inode attribute */
  if ((cache_status = cache_inode_getattr(data->current_entry,
                                          &file_attr,
                                          data->ht,
                                          data->pclient,
                                          data->pcontext,
                                          &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_NVERIFY4.status = NFS4ERR_INVAL;
      return res_NVERIFY4.status;
    }

  if (nfs4_FSALattr_To_Fattr(data->pexport,
                             &file_attr,
                             &file_attr4,
                             data,
                             &(data->currentFH),
                             &(arg_NVERIFY4.obj_attributes.attrmask)) != 0)
    {
      res_NVERIFY4.status = NFS4ERR_SERVERFAULT;
      return res_NVERIFY4.status;
    }

  if ((rc = nfs4_Fattr_cmp(&(arg_NVERIFY4.obj_attributes), &file_attr4)) == FALSE)
    res_NVERIFY4.status = NFS4_OK;
  else
    {
      if (rc == -1)
        res_NVERIFY4.status = NFS4ERR_INVAL;
      else
        res_NVERIFY4.status = NFS4ERR_SAME;
    }

  return res_NVERIFY4.status;
}                               /* nfs4_op_nverify */

/**
 * nfs4_op_nverify_Free: frees what was allocared to handle nfs4_op_nverify.
 * 
 * Frees what was allocared to handle nfs4_op_nverify.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_nverify_Free(NVERIFY4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_nverify_Free */
