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
 * \file    nfs4_op_open_confirm.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_open_confirm.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs4_op_open_confirm: The NFS4_OP_OPEN_CONFIRM
 * 
 * Implements the NFS4_OP_OPEN_CONFIRM
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if ok, any other value show an error.
 *
 */
#define arg_OPEN_CONFIRM4 op->nfs_argop4_u.opopen_confirm
#define res_OPEN_CONFIRM4 resp->nfs_resop4_u.opopen_confirm

int nfs4_op_open_confirm(  struct nfs_argop4 * op ,   
                           compound_data_t   * data,
                           struct nfs_resop4 * resp)
{
    char                  __attribute__(( __unused__ )) funcname[] = "nfs4_op_open_confirm" ;
    int                   rc = 0 ;
   cache_inode_state_t  * pstate_found = NULL ;
   cache_inode_status_t   cache_status ; 

    resp->resop = NFS4_OP_OPEN_CONFIRM ;
    res_OPEN_CONFIRM4.status =  NFS4_OK ;
  
    
    /* If there is no FH */
    if( nfs4_Is_Fh_Empty( &(data->currentFH  ) ) )
      {
        res_OPEN_CONFIRM4.status = NFS4ERR_NOFILEHANDLE ;
        return res_OPEN_CONFIRM4.status ;
      }
  
    /* If the filehandle is invalid */
    if( nfs4_Is_Fh_Invalid( &(data->currentFH ) ) )
      {
        res_OPEN_CONFIRM4.status = NFS4ERR_BADHANDLE ;
        return res_OPEN_CONFIRM4.status ;
      }
    
    /* Tests if the Filehandle is expired (for volatile filehandle) */
    if( nfs4_Is_Fh_Expired( &(data->currentFH) ) )
      {
        res_OPEN_CONFIRM4.status = NFS4ERR_FHEXPIRED ;
        return res_OPEN_CONFIRM4.status ;
      }


    /* Should not operate on non-file objects */
    if( data->current_entry->internal_md.type != REGULAR_FILE )
     {
       switch( data->current_entry->internal_md.type )
         {  
	    case DIR_BEGINNING:
	    case DIR_CONTINUE:
         	res_OPEN_CONFIRM4.status = NFS4ERR_ISDIR ;
         	return res_OPEN_CONFIRM4.status ;
		break ;
	    default:
         	res_OPEN_CONFIRM4.status = NFS4ERR_INVAL ;
         	return res_OPEN_CONFIRM4.status ;
		break ;
		
         }
     }

    /* Does the stateid match ? */
    if( ( rc = nfs4_Check_Stateid( &arg_OPEN_CONFIRM4.open_stateid, data->current_entry ) )  != NFS4_OK )
     {
        res_OPEN_CONFIRM4.status = rc ;
        return res_OPEN_CONFIRM4.status ;
     }   

    /* Get the related state */
    if( cache_inode_get_state( arg_OPEN_CONFIRM4.open_stateid.other,
                               &pstate_found,
                               data->pclient,
                               &cache_status ) != CACHE_INODE_SUCCESS )
      {
         res_OPEN_CONFIRM4.status = nfs4_Errno( cache_status ) ;
         return res_OPEN_CONFIRM4.status ;
      }

   /* If opened file is already confirmed, retrun NFS4ERR_BAD_STATEID */
   if( pstate_found->state_data.share.confirmed == TRUE )
     {
        res_OPEN_CONFIRM4.status = NFS4ERR_BAD_STATEID ;
        return res_OPEN_CONFIRM4.status ;
     }

   if(  pstate_found->seqid != arg_OPEN_CONFIRM4.seqid )
     {
        if( pstate_found->seqid +1 != arg_OPEN_CONFIRM4.seqid )
         {
            res_OPEN_CONFIRM4.status = NFS4ERR_BAD_SEQID ;
            return res_OPEN_CONFIRM4.status ;
         }
     }


   /* Set the state as confirmed */
   pstate_found->state_data.share.confirmed = TRUE ;
   pstate_found->seqid =  arg_OPEN_CONFIRM4.seqid ;

   /* Update the state */
   if( cache_inode_update_state( pstate_found,
                                 data->pclient,
                                 &cache_status ) != CACHE_INODE_SUCCESS )
      {
         res_OPEN_CONFIRM4.status = nfs4_Errno( cache_status ) ;
         return res_OPEN_CONFIRM4.status ;
      }
 
   /* Return the stateid to the client */
   res_OPEN_CONFIRM4.OPEN_CONFIRM4res_u.resok4.open_stateid.seqid = pstate_found->seqid ;
   memcpy( res_OPEN_CONFIRM4.OPEN_CONFIRM4res_u.resok4.open_stateid.other, pstate_found->stateid_other, 12 ) ;
   
   return res_OPEN_CONFIRM4.status;
} /* nfs4_op_open_confirm */

    
/**
 * nfs4_op_open_confirm_Free: frees what was allocared to handle nfs4_op_open_confirm.
 * 
 * Frees what was allocared to handle nfs4_op_open_confirm.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_open_confirm_Free( OPEN_CONFIRM4res * resp )
{
  /* Nothing to be done */
  return ;
} /* nfs4_op_open_confirm_Free */
