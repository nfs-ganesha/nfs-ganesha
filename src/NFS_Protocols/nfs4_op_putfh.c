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
 * \file    nfs4_op_putfh.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.11 $
 * \brief   Routines used for managing the NFS4_OP_PUTFH operation.
 *
 * nfs4_op_putfh.c : Routines used for managing the NFS4_OP_PUTFH operation.
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

#define arg_PUTFH4 op->nfs_argop4_u.opputfh  
#define res_PUTFH4 resp->nfs_resop4_u.opputfh 

/**
 *
 * nfs4_op_putfh: The NFS4_OP_PUTFH operation
 *
 * Sets the current FH with the value given in argument. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs4_op_<*> function
 * @see nfs4_Compound
 *
 */

int nfs4_op_putfh(  struct nfs_argop4 * op ,   
                    compound_data_t   * data,
                    struct nfs_resop4 * resp)
{
  int                rc ;
  int                error  ;
  fsal_attrib_list_t attr ;
#ifdef _DEBUG_NFS_V4
  char               outstr[1024] ;
#endif
  char               __attribute__(( __unused__ )) funcname[] = "nfs4_op_putfh" ;


  resp->resop = NFS4_OP_PUTFH ;
  res_PUTFH4.status = NFS4_OK ;

  /* If there is no FH */
  if( nfs4_Is_Fh_Empty( &(arg_PUTFH4.object) ) )
    {
      res_PUTFH4.status = NFS4ERR_NOFILEHANDLE ;
      return res_PUTFH4.status ;
    }
  
  /* If the filehandle is invalid */
  if( nfs4_Is_Fh_Invalid( &(arg_PUTFH4.object) ) )
    {
      res_PUTFH4.status = NFS4ERR_BADHANDLE ;
      return res_PUTFH4.status ;
    }
  
  /* Tests if teh Filehandle is expired (for volatile filehandle) */
  if( nfs4_Is_Fh_Expired( &(arg_PUTFH4.object) ) )
    {
      res_PUTFH4.status = NFS4ERR_FHEXPIRED ;
      return res_PUTFH4.status ;
    }
  
  /* If no currentFH were set, allocate one */
  if( data->currentFH.nfs_fh4_len == 0 )
    {
      if( ( error = nfs4_AllocateFH( &(data->currentFH) ) ) != NFS4_OK )
        {
          res_PUTFH4.status = error ;
          return res_PUTFH4.status ;
        }
    }

  /* The same is to be done with mounted_on_FH */
  if( data->mounted_on_FH.nfs_fh4_len == 0 )
    {
      if( ( error = nfs4_AllocateFH( &(data->mounted_on_FH) ) ) != NFS4_OK )
        {
          res_PUTFH4.status = error ;
          return res_PUTFH4.status ;
        }
    }

  
  /* Copy the filehandle from the reply structure */
  data->currentFH.nfs_fh4_len = arg_PUTFH4.object.nfs_fh4_len ;
  data->mounted_on_FH.nfs_fh4_len = arg_PUTFH4.object.nfs_fh4_len ;

  /* Put the data in place */
  memcpy( data->currentFH.nfs_fh4_val,     arg_PUTFH4.object.nfs_fh4_val, arg_PUTFH4.object.nfs_fh4_len ) ;
  memcpy( data->mounted_on_FH.nfs_fh4_val, arg_PUTFH4.object.nfs_fh4_val, arg_PUTFH4.object.nfs_fh4_len ) ;


#ifdef _DEBUG_NFS_V4
  nfs4_sprint_fhandle( &arg_PUTFH4.object, outstr ) ;
  DisplayLog( "NFS4_OP_PUTFH CURRENTFH BEFORE: File handle = %s", outstr );
#endif
  /* If the filehandle is not pseudo hs file handle, get the entry related to it, otherwise use fake values */
  if( nfs4_Is_Fh_Pseudo( &(data->currentFH) ) )
    {
      data->current_entry = NULL ;
      data->current_filetype = DIR_BEGINNING ;
      data->pexport = NULL ; /* No exportlist is related to pseudo fs */
    }
  else
    { 
      /* If data->exportp is null, a junction from pseudo fs was traversed, credp and exportp have to be updated */
      if( data->pexport == NULL )
        {
          if( ( error = nfs4_SetCompoundExport( data ) ) != NFS4_OK )
            {
              res_PUTFH4.status = error ;
              return res_PUTFH4.status ;
            }
        }
      
      /* Build the pentry */
      if( ( data->current_entry = nfs_FhandleToCache( NFS_V4, 
                                                      NULL, 
                                                      NULL, 
                                                      &(data->currentFH),
                                                      NULL,
                                                      NULL,
                                                      &(res_PUTFH4.status),
                                                      &attr,
                                                      data->pcontext, 
                                                      data->pclient,
                                                      data->ht, 
                                                      &rc ) ) == NULL )
      {
          res_PUTFH4.status = NFS4ERR_BADHANDLE ;
          return res_PUTFH4.status ;
        }

      /* Extract the filetype */
      data->current_filetype = cache_inode_fsal_type_convert( attr.type ) ; 
      
    }
  
  /* Trace */

  return NFS4_OK ;
} /* nfs4_op_putfh */


/**
 * nfs4_op_create_Free: frees what was allocared to handle nfs4_op_create.
 * 
 * Frees what was allocared to handle nfs4_op_create.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_putfh_Free( PUTFH4res * resp )
{
  /* Nothing to be freed */
  return ;
} /* nfs4_op_create_Free */

