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
 * --------------------------------------- */
/**
 * \file    nfs4_op_getattr.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:52 $
 * \version $Revision: 1.17 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_getattr.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <sys/file.h>  /* for having FNDELAY */
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
#include "nfs_file_handle.h"


#define arg_GETATTR4 op->nfs_argop4_u.opgetattr
#define res_GETATTR4 resp->nfs_resop4_u.opgetattr
#define res_FATTR4  res_GETATTR4.GETATTR4res_u.resok4.obj_attributes 
 

/**
 *
 * nfs4_op_getattr: Gets attributes for an entry in the FSAL.
 * 
 * Gets attributes for an entry in the FSAL.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */
int nfs4_op_getattr(  struct nfs_argop4 * op ,   
                      compound_data_t   * data,
                      struct nfs_resop4 * resp)
{
  fsal_attrib_list_t   attr ;
  cache_inode_status_t cache_status ;
  
  char __attribute__(( __unused__ )) funcname[] = "nfs4_op_getattr" ;

  /* This is a NFS4_OP_GETTAR */
  resp->resop = NFS4_OP_GETATTR ;

  /* If there is no FH */
  if( nfs4_Is_Fh_Empty( &(data->currentFH  ) ) )
    {
      res_GETATTR4.status = NFS4ERR_NOFILEHANDLE ;
      return NFS4ERR_NOFILEHANDLE ;
    }
  
  /* If the filehandle is invalid */
  if( nfs4_Is_Fh_Invalid( &(data->currentFH ) ) )
    {
      res_GETATTR4.status = NFS4ERR_BADHANDLE ;
      return NFS4ERR_BADHANDLE ;
    }
  
  /* Tests if teh Filehandle is expired (for volatile filehandle) */
  if( nfs4_Is_Fh_Expired( &(data->currentFH) ) )
    {
      res_GETATTR4.status = NFS4ERR_FHEXPIRED ;
      return NFS4ERR_FHEXPIRED ;
    }

  /* Pseudo Fs management */
  if( nfs4_Is_Fh_Pseudo( &(data->currentFH) ) )
    return nfs4_op_getattr_pseudo( op, data, resp ) ;

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if( nfs4_Is_Fh_Xattr( &(data->currentFH ) ) ) 
    return nfs4_op_getattr_xattr( op, data, resp ) ;

#ifdef _DEBUG_NFS_V4
  printf( "CURRENT FH: NFS4_OP_GETATTR = { Length = %d  Val = ", data->currentFH.nfs_fh4_len) ;
  for( cpt = 0 ; cpt < data->currentFH.nfs_fh4_len ; cpt ++ )
    printf( "%02X", data->currentFH.nfs_fh4_val[cpt] );
  printf( " } \n" ) ;
#endif

  /* Sanity check: if no attributes are wanted, nothing is to be done.
   * In this case NFS4_OK is to be returned */
  if( arg_GETATTR4.attr_request.bitmap4_len == 0 )
    {
      res_GETATTR4.status = NFS4_OK ;
      return res_GETATTR4.status ;
    }

  /* Get only attributes that are allowed to be read */
  if( !nfs4_Fattr_Check_Access_Bitmap( &arg_GETATTR4.attr_request, FATTR4_ATTR_READ ) )
    {
      res_GETATTR4.status = NFS4ERR_INVAL ;
      return res_GETATTR4.status ;
    }

  /* Ask only for supported attributes */
  if( !nfs4_Fattr_Supported_Bitmap( &arg_GETATTR4.attr_request ) )
    {
       res_GETATTR4.status = NFS4ERR_ATTRNOTSUPP ;
       return res_GETATTR4.status ;
    }
  
  /*
   * Get attributes.
   */
  if( cache_inode_getattr( data->current_entry, 
                           &attr, 
                           data->ht,
                           data->pclient, 
                           data->pcontext, 
                           &cache_status ) == CACHE_INODE_SUCCESS )
    {
      if( nfs4_FSALattr_To_Fattr( data->pexport,
                                  &attr,
                                  &(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes), 
                                  data, 
                                  &(data->currentFH),
                                  &(arg_GETATTR4.attr_request) ) != 0 )
        res_GETATTR4.status = NFS4ERR_SERVERFAULT ;
      else
        res_GETATTR4.status = NFS4_OK ;
      
      return res_GETATTR4.status ;
    }
  res_GETATTR4.status = nfs4_Errno( cache_status ) ;
  
  return res_GETATTR4.status ;
} /* nfs4_op_getattr */


/**
 * nfs4_op_getattr_Free: frees what was allocared to handle nfs4_op_getattr.
 * 
 * Frees what was allocared to handle nfs4_op_getattr.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_getattr_Free( GETATTR4res * resp )
{ 
  if( resp->status == NFS4_OK )
    {
      if( resp->GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val != NULL ) 
        Mem_Free( (char *)resp->GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val ) ; 

      if( resp->GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val != NULL ) 
       Mem_Free( (char *)resp->GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val ) ; 
    }
  return ;
} /* nfs4_op_getattr_Free */
