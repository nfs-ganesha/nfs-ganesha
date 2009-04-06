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
 * \file    nfs4_op_readlink.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.15 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_readlink.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_file_handle.h"

/**
 * 
 * nfs4_op_readlink: The NFS4_OP_READLINK operation. 
 *
 * This function implements the NFS4_OP_READLINK operation.
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

#define arg_READLINK4 op->nfs_argop4_u.opreadlink
#define res_READLINK4 resp->nfs_resop4_u.opreadlink

int nfs4_op_readlink(  struct nfs_argop4 * op ,   
                       compound_data_t   * data,
                       struct nfs_resop4 * resp)
{
  cache_inode_status_t   cache_status ;
  fsal_path_t            symlink_path ;

  char            __attribute__(( __unused__ )) funcname[] = "nfs4_op_readlink" ;
  
  resp->resop = NFS4_OP_READLINK ;
  res_READLINK4.status =  NFS4_OK  ;
  
  /* If there is no FH */
  if( nfs4_Is_Fh_Empty( &(data->currentFH  ) ) )
    {
      res_READLINK4.status = NFS4ERR_NOFILEHANDLE ;
      return NFS4ERR_NOFILEHANDLE ;
    }
  
  /* If the filehandle is invalid */
  if( nfs4_Is_Fh_Invalid( &(data->currentFH ) ) )
    {
      res_READLINK4.status = NFS4ERR_BADHANDLE ;
      return NFS4ERR_BADHANDLE ;
    }
  
  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if( nfs4_Is_Fh_Expired( &(data->currentFH) ) )
    {
      res_READLINK4.status = NFS4ERR_FHEXPIRED ;
      return NFS4ERR_FHEXPIRED ;
    }
  
  /* You can readlink only on a link ... */
  if( data->current_filetype != SYMBOLIC_LINK )
    {
      /* As said on page 194 of RFC3530, return NFS4ERR_INVAL in this case */
      res_READLINK4.status = NFS4ERR_INVAL ;
      return res_READLINK4.status ;
    }
 
  /* Using cache_inode_readlink */
  if( cache_inode_readlink( data->current_entry,
                            &symlink_path, 
                            data->ht, 
                            data->pclient, 
                            data->pcontext, 
                            &cache_status ) ==  CACHE_INODE_SUCCESS )
    {
      /* Alloc read link */
#ifdef _DEBUG_MEMLEAKS
      /* For debugging memory leaks */
      BuddySetDebugLabel( "nfs4_op_readlink" ) ;
#endif

      if( ( res_READLINK4.READLINK4res_u.resok4.link.utf8string_val = (char *)Mem_Alloc( symlink_path.len ) ) == NULL )
        {
          res_READLINK4.status = NFS4ERR_INVAL ;
          return res_READLINK4.status ;
        }

#ifdef _DEBUG_MEMLEAKS
      /* For debugging memory leaks */
      BuddySetDebugLabel( "N/A" ) ;
#endif

      /* convert the fsal path to a utf8 string */
      if( str2utf8( (char *)symlink_path.path, &res_READLINK4.READLINK4res_u.resok4.link ) == -1 )
        {
          res_READLINK4.status = NFS4ERR_INVAL ;
          return res_READLINK4.status ;
        }

      res_READLINK4.status = NFS4_OK ;
      return res_READLINK4.status ;
    }
  
  res_READLINK4.status = nfs4_Errno( cache_status ) ;
  return res_READLINK4.status;
} /* nfs4_op_readlink */

    
/**
 * nfs4_op_readlink_Free: frees what was allocared to handle nfs4_op_readlink.
 * 
 * Frees what was allocared to handle nfs4_op_readlink.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_readlink_Free( READLINK4res * resp )
{
  if( resp->status == NFS4_OK && resp->READLINK4res_u.resok4.link.utf8string_len > 0 )
    Mem_Free( (char *)resp->READLINK4res_u.resok4.link.utf8string_val ) ;
  
  return ;
} /* nfs4_op_readlink_Free */
