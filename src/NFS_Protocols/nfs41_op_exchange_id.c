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
 * \file    nfs41_op_exchange_id.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 16:02:52 $
 * \brief   Routines used for managing the NFS4_OP_EXCHANGE_ID operation.
 *
 * nfs4_op_exchange_id.c :  Routines used for managing the EXCHANGE_ID operation.
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

extern time_t ServerBootTime ;

/**
 *
 * nfs41_op_exchange_id:  The NFS4_OP_EXCHANGE_ID operation.
 *
 * Gets the currentFH for the current compound requests.
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

int nfs41_op_exchange_id(  struct nfs_argop4 * op ,    
                           compound_data_t   * data,
                           struct nfs_resop4 * resp)
{
  char __attribute__(( __unused__ )) funcname[] = "nfs4_op_setclientid" ;
  char str_verifier[MAXNAMLEN] ;
  char str_client[MAXNAMLEN] ;

#define arg_EXCHANGE_ID4  op->nfs_argop4_u.opexchange_id
#define res_EXCHANGE_ID4  resp->nfs_resop4_u.opexchange_id

  clientid4             clientid ;
  nfs_client_id_t       nfs_clientid ;
  nfs_worker_data_t   * pworker = NULL ;

  pworker = (nfs_worker_data_t *)data->pclient->pworker ;

  strncpy( str_verifier, arg_EXCHANGE_ID4.eia_clientowner.co_verifier, MAXNAMLEN ) ;
  strncpy( str_client, arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val, arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len )  ;
  str_client[arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len] = '\0' ;

  DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID Client id len = %u",arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len ) ;
  DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID Client name = #%s#", str_client ) ;
  //DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID Verifier = #%s#", str_verifier ) ; 

  /* There was no pb, returns the clientid */
  resp->resop = NFS4_OP_EXCHANGE_ID ;
  res_EXCHANGE_ID4.eir_status = NFS4_OK ;

  /* Compute the client id */  
  if( nfs_client_id_basic_compute( str_client, &clientid ) != CLIENT_ID_SUCCESS ) 
    {
	res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT ;
	return res_EXCHANGE_ID4.eir_status  ;
    }
  DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID computed clientid4=%llx for name='%s'", clientid, str_client ) ;

  /* Does this id already exists ? */
  if( nfs_client_id_get( clientid, &nfs_clientid ) == CLIENT_ID_SUCCESS )
    {
	/* Client id already in use */
	DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID ClientId %llx already in use for client '%s', check if same", clientid, nfs_clientid.client_name ) ;

        /* Principals are the same, check content of the setclientid request */
        if( nfs_clientid.confirmed == CONFIRMED_CLIENT_ID )
	  {
#ifdef _NFSV4_COMPARE_CRED_IN_EXCHANGE_ID
                /* Check if client id has same credentials */
                if( nfs_compare_clientcred(  &(nfs_clientid.credential), &(data->credential ) ) == FALSE )
                  {
                    DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID Confirmed ClientId %llx -> '%s': Credential do not match... Return NFS4ERR_CLID_INUSE",
                                     clientid, nfs_clientid.client_name ) ;

                    res_EXCHANGE_ID4.eir_status = NFS4ERR_CLID_INUSE ;
#ifdef _USE_NFS4_1
                    res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.na_r_netid = nfs_clientid.client_r_netid ;
                    res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.na_r_addr = nfs_clientid.client_r_addr ;
#else
                    res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.r_netid = nfs_clientid.client_r_netid ;
                    res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.r_addr = nfs_clientid.client_r_addr ;
#endif
                    return res_EXCHANGE_ID4.eir_status ;
                  }
                 else
	          DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID ClientId %llx is set again by same principal", clientid ) ;
#endif

		/* Ask for a different client with the same client id... returns an error if different client*/
	        DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID Confirmed ClientId %llx already in use for client '%s'", clientid, nfs_clientid.client_name ) ;
		
		if( strncmp( nfs_clientid.incoming_verifier, arg_EXCHANGE_ID4.eia_clientowner.co_verifier, NFS4_VERIFIER_SIZE ) )
		  {
	               DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID Confirmed ClientId %llx already in use for client '%s', verifier do not match...", 
					clientid, nfs_clientid.client_name ) ;

                       /* A client has rebooted and rebuilds its state */
	               DisplayLogLevel( NIV_DEBUG, "Probably something to be done here: a client has rebooted and try recovering its state. Update the record for this client" ) ;

                       /* Update the record, but set it as REBOOTED */
                       strncpy(  nfs_clientid.client_name,  arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val, arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len) ;
                       nfs_clientid.client_name[arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len] = '\0' ;

                       strncpy( nfs_clientid.incoming_verifier, arg_EXCHANGE_ID4.eia_clientowner.co_verifier, NFS4_VERIFIER_SIZE ) ;
                       snprintf( nfs_clientid.verifier, NFS4_VERIFIER_SIZE, "%u", (unsigned int)ServerBootTime ) ;
                       nfs_clientid.confirmed  = REBOOTED_CLIENT_ID ;
                       nfs_clientid.clientid   = clientid ;
                       nfs_clientid.last_renew = 0 ;

                       if( nfs_client_id_set( clientid, nfs_clientid, pworker->clientid_pool ) != CLIENT_ID_SUCCESS )
                        {
	                        res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT ;
	                        return res_EXCHANGE_ID4.eir_status  ;
                        }

		  }
		else
                  {
	            DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID Confirmed ClientId %llx already in use for client '%s', verifier matches. Now check callback", 
		 		     clientid, nfs_clientid.client_name ) ;
                  }
	  }
	else
         {
	    DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID ClientId %llx already in use for client '%s', but unconfirmed", clientid, nfs_clientid.client_name ) ;
	    DisplayLog( "Reuse of a formerly obtained clientid that is not yet confirmed. Code needs to be improved here" ) ;
         }
    }
  else
    {
       /* Build the client record */
       strncpy(  nfs_clientid.client_name, 
	         arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val, arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len) ;
       nfs_clientid.client_name[arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len] = '\0' ;
       
       strncpy( nfs_clientid.incoming_verifier, arg_EXCHANGE_ID4.eia_clientowner.co_verifier, NFS4_VERIFIER_SIZE ) ;
       snprintf( nfs_clientid.verifier, NFS4_VERIFIER_SIZE, "%u", (unsigned int)ServerBootTime ) ;
       nfs_clientid.confirmed  = UNCONFIRMED_CLIENT_ID ;
       nfs_clientid.cb_program = 0 ; /* to be set at create_session time */
       nfs_clientid.clientid   = clientid ;
       nfs_clientid.last_renew = 0 ;
       nfs_clientid.nb_session = 0 ;
       nfs_clientid.create_session_sequence = 1 ;
       nfs_clientid.credential = data->credential ;

       if( gethostname( nfs_clientid.server_owner, MAXNAMLEN ) == -1 )
         {
	     res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT ;
	     return res_EXCHANGE_ID4.eir_status  ;
         }
        strncpy( nfs_clientid.server_scope, nfs_clientid.server_owner, MAXNAMLEN ) ;
 
       if( nfs_client_id_add( clientid, nfs_clientid, pworker->clientid_pool ) != CLIENT_ID_SUCCESS )
         {
	     res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT ;
	     return res_EXCHANGE_ID4.eir_status  ;
         }
    }

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_clientid = clientid ; 
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_sequenceid = nfs_clientid.create_session_sequence ;
  /* No pNFS for the moment (this will come later), but referrals are supported */
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_flags = EXCHGID4_FLAG_USE_NON_PNFS | EXCHGID4_FLAG_SUPP_MOVED_REFER ;
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_state_protect.spr_how = SP4_NONE ;

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.so_major_id_len = strlen( nfs_clientid.server_owner ) ;
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.so_major_id_val = Mem_Alloc( strlen( nfs_clientid.server_owner ) ) ;
  memcpy( res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.so_major_id_val,
	  nfs_clientid.server_owner, 
	  strlen( nfs_clientid.server_owner ) ) ;
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_minor_id = 0 ;

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_len = strlen( nfs_clientid.server_scope ) ;
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_val = Mem_Alloc( strlen( nfs_clientid.server_scope ) ) ;
  memcpy( res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_val,
	  nfs_clientid.server_owner, 
	  strlen( nfs_clientid.server_owner ) ) ;

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id.eir_server_impl_id_len = 0 ;
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id.eir_server_impl_id_val = NULL ;

  DisplayLogLevel( NIV_DEBUG, "EXCHANGE_ID reply :ClientId=%llx", res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_clientid ) ;
 
  res_EXCHANGE_ID4.eir_status = NFS4_OK ; 
  return res_EXCHANGE_ID4.eir_status ;
} /* nfs41_op_exchange_id */



/**
 * nfs4_op_setclientid_Free: frees what was allocared to handle nfs4_op_setclientid.
 * 
 * Frees what was allocared to handle nfs4_op_setclientid.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */ 
void nfs41_op_exchange_id_Free( EXCHANGE_ID4res * resp )
{
  Mem_Free( resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_val ) ;
  Mem_Free( resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.so_major_id_val ) ;
  return ;
} /* nfs41_op_exchange_id_Free */
