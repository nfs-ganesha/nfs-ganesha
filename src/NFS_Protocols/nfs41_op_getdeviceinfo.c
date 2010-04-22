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
 * \file    nfs41_op_getdeviceinfo.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 16:02:52 $
 * \brief   Routines used for managing the NFS4_OP_GETDEVICEINFO operation.
 *
 * nfs41_op_getdeviceinfo.c :  Routines used for managing the GETDEVICEINFO operation.
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
#include "nfs_file_handle.h"
#include "nfs_tools.h"

/**
 *
 * nfs41_op_getdeviceinfo:  The NFS4_OP_GETDEVICEINFO operation.
 *
 * Gets the list of pNFS devices
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see nfs4_Compound
 *
 */

#define arg_GETDEVICEINFO4  op->nfs_argop4_u.opgetdeviceinfo
#define res_GETDEVICEINFO4  resp->nfs_resop4_u.opgetdeviceinfo

int nfs41_op_getdeviceinfo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getdeviceinfo";

  resp->resop = NFS4_OP_GETDEVICEINFO;

#ifndef _USE_PNFS
  res_GETDEVICEINFO4.gdir_status = NFS4ERR_NOTSUPP;
  return res_res_GETDEVICEINFO4.gdir_status;
#else

  char * buff = NULL ;
  unsigned int lenbuff = 0 ;

  if( ( buff = Mem_Alloc( 1024 ) ) == NULL )
   {
     res_GETDEVICEINFO4.gdir_status = NFS4ERR_SERVERFAULT;
     return res_GETDEVICEINFO4.gdir_status;
   }
  
  /** @todo handle multiple DS here when this will be implemented (switch on deviceid arg) */
  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_notification.bitmap4_len = 0 ;  
  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_notification.bitmap4_val = NULL ;  

  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_layout_type = LAYOUT4_NFSV4_1_FILES ;

  pnfs_encode_getdeviceinfo( buff, &lenbuff ) ;
  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.da_addr_body_len =  lenbuff ;
  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.da_addr_body_val =  buff ;

  res_GETDEVICEINFO4.gdir_status = NFS4_OK;

  return res_GETDEVICEINFO4.gdir_status;
#endif /* _USE_PNFS */
}                               /* nfs41_op_exchange_id */

/**
 * nfs4_op_getdeviceinfo_Free: frees what was allocared to handle nfs4_op_getdeviceinfo.
 * 
 * Frees what was allocared to handle nfs4_op_getdeviceinfo.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_getdeviceinfo_Free(GETDEVICEINFO4res * resp)
{
  if( resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.da_addr_body_val != NULL )
    Mem_Free( resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.da_addr_body_val ) ;

  return;
}                               /* nfs41_op_exchange_id_Free */
