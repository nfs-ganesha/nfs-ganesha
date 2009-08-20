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
 * \file    nfs41_op_locku.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_locku.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_file_handle.h"
#include "nfs_tools.h"


/**
 * 
 * nfs41_op_locku: The NFS4_OP_LOCKU operation. 
 *
 * This function implements the NFS4_OP_LOCKU operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs41_op_<*> function
 * @see nfs4_Compound
 *
 */ 


#define arg_LOCKU4 op->nfs_argop4_u.oplocku
#define res_LOCKU4 resp->nfs_resop4_u.oplocku

int nfs41_op_locku(  struct nfs_argop4 * op ,   
                     compound_data_t   * data,
                     struct nfs_resop4 * resp)
{
  char                       __attribute__(( __unused__ )) funcname[] = "nfs41_op_locku" ;
  cache_inode_status_t       cache_status ;
  cache_inode_state_t    *   pstate_found = NULL ;
  cache_inode_state_t    *   pstate_open  = NULL ;
  unsigned int               rc = 0 ;

  /* Lock are not supported */
  resp->resop = NFS4_OP_LOCKU ;

#ifndef _WITH_NFSV4_LOCKS
  res_LOCKU4.status =  NFS4ERR_LOCK_NOTSUPP ;
  return res_LOCKU4.status ;
#else
 
  /* If there is no FH */
  if( nfs4_Is_Fh_Empty( &(data->currentFH  ) ) )
    {
      res_LOCKU4.status = NFS4ERR_NOFILEHANDLE ;
      return res_LOCKU4.status ; 
    }
 
  /* If the filehandle is invalid */
  if( nfs4_Is_Fh_Invalid( &(data->currentFH ) ) )
    {
      res_LOCKU4.status = NFS4ERR_BADHANDLE ; 
      return res_LOCKU4.status ; 
    }
 
  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if( nfs4_Is_Fh_Expired( &(data->currentFH) ) )
    {
      res_LOCKU4.status = NFS4ERR_FHEXPIRED ; 
      return res_LOCKU4.status ; 
    }

  /* Commit is done only on a file */
  if( data->current_filetype != REGULAR_FILE )
    {
        /* Type of the entry is not correct */
        switch( data->current_filetype )
          {
             case DIR_BEGINNING:
             case DIR_CONTINUE:
                res_LOCKU4.status = NFS4ERR_ISDIR ; 
                break ;
             default:
                res_LOCKU4.status = NFS4ERR_INVAL ; 
                break ;
          }
    }
  
  /* Lock length should not be 0 */
  if( arg_LOCKU4.length == 0LL )
    {
       res_LOCKU4.status = NFS4ERR_INVAL ;
       return res_LOCKU4.status ;
    }

  /* Check for range overflow 
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if( arg_LOCKU4.length != 0xffffffffffffffffLL ) 
    {
        /* Comparing beyond 2^64 is not possible int 64 bits precision, but off+len > 2^64 is equivalent to len > 2^64 - off */
        if( arg_LOCKU4.length > ( 0xffffffffffffffffLL - arg_LOCKU4.offset ) )
          {
             res_LOCKU4.status = NFS4ERR_INVAL ;
             return res_LOCKU4.status ;
          }
    }

  /* Check for correctness of the provided stateid */
  if( ( rc = nfs4_Check_Stateid( &arg_LOCKU4.lock_stateid, data->current_entry ) ) != NFS4_OK )
   {
      res_LOCKU4.status = rc ;
      return res_LOCKU4.status ;
   }   
 
  /* Get the related state */
  if( cache_inode_get_state( arg_LOCKU4.lock_stateid.other,
                             &pstate_found,
                             data->pclient,
                             &cache_status ) != CACHE_INODE_SUCCESS )
    {
       if( cache_status == CACHE_INODE_NOT_FOUND )
         res_LOCKU4.status = NFS4ERR_LOCK_RANGE ;
       else
         res_LOCKU4.status = nfs4_Errno( cache_status ) ;

       return res_LOCKU4.status ;
    }

  pstate_open = (cache_inode_state_t *)(pstate_found->state_data.lock.popenstate) ;
  memcpy( res_LOCKU4.LOCKU4res_u.lock_stateid.other, pstate_found->stateid_other, 12 ) ;


  /* Remove the state associated with the lock */
  if( cache_inode_del_state( pstate_found,
                             data->pclient, 
                             &cache_status ) != CACHE_INODE_SUCCESS )
    {
       res_LOCKU4.status = nfs4_Errno( cache_status ) ;
       return res_LOCKU4.status ;
    }

#ifdef _DEBUG_STATES
  nfs_State_PrintAll(  ) ;
#endif

  /* Successful exit */
  res_LOCKU4.status = NFS4_OK ;
  return res_LOCKU4.status;
#endif
} /* nfs41_op_locku */


/**
 * nfs41_op_locku_Free: frees what was allocared to handle nfs41_op_locku.
 * 
 * Frees what was allocared to handle nfs41_op_locku.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_locku_Free( LOCKU4res * resp )
{ 
  /* Nothing to Mem_Free */
  return ;
} /* nfs41_op_locku_Free */


    
