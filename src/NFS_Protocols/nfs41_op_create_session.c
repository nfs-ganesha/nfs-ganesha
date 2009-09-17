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
 * \file    nfs4_op_create_session.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 17:02:52 $
 * \brief   Routines used for managing the NFS4_OP_CREATE_SESSION operation.
 *
 * nfs4_op_create_session.c :  Routines used for managing the NFS4_OP_CREATE_SESSION operation.
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
 * nfs4_op_create_session:  The NFS4_OP_CREATE_SESSION operation.
 *
 * The NFS4_OP_CREATE_SESSION operation.
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

int nfs41_op_create_session(  struct nfs_argop4 * op ,    
                              compound_data_t * data,
                              struct nfs_resop4 * resp)
{
  nfs_client_id_t       nfs_clientid ;
  nfs41_session_t     * pnfs41_session = NULL ;
  clientid4             clientid = 0 ;
  nfs_worker_data_t   * pworker = NULL ;

  pworker = (nfs_worker_data_t *)data->pclient->pworker ;


#define arg_CREATE_SESSION4 op->nfs_argop4_u.opcreate_session
#define res_CREATE_SESSION4 resp->nfs_resop4_u.opcreate_session

  resp->resop = NFS4_OP_CREATE_SESSION ;
  res_CREATE_SESSION4.csr_status = NFS4_OK ;
  clientid =  arg_CREATE_SESSION4.csa_clientid ;

  DisplayLogLevel( NIV_DEBUG, "CREATE_SESSION clientid = %llx", clientid ) ;

  
  /* Does this id already exists ? */
  if( nfs_client_id_get( clientid , &nfs_clientid ) != CLIENT_ID_SUCCESS )
    {
       /* The client id does not exist: stale client id */
       res_CREATE_SESSION4.csr_status = NFS4ERR_STALE_CLIENTID ;
       return res_CREATE_SESSION4.csr_status ;
    }

  nfs_clientid.confirmed = CONFIRMED_CLIENT_ID ;
  nfs_clientid.cb_program = arg_CREATE_SESSION4.csa_cb_program ;
  /** @todo: BUGAZOMEU Gerer les parametres de secu */

  /* Record session related information at the right place */
  GET_PREALLOC( pnfs41_session,
                data->pclient->pool_session,
                data->pclient->nb_pre_state_v4,
		nfs41_session_t,
 		next_alloc ) ;

  if( pnfs41_session == NULL )
   {
       res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT ;
       return res_CREATE_SESSION4.csr_status ;
   }

  memset( (char *)pnfs41_session, 0, sizeof( nfs41_session_t ) ) ;
  pnfs41_session->clientid = clientid ;
  pnfs41_session->sequence = 1 ;
  pnfs41_session->session_flags = CREATE_SESSION4_FLAG_CONN_BACK_CHAN ;
  pnfs41_session->fore_channel_attrs = arg_CREATE_SESSION4.csa_fore_chan_attrs ;
  pnfs41_session->back_channel_attrs = arg_CREATE_SESSION4.csa_back_chan_attrs ;

  /* Set ca_maxrequests */
  pnfs41_session->fore_channel_attrs.ca_maxrequests = NFS41_NB_SLOTS ;
  pnfs41_session->fore_channel_attrs.ca_maxrequests = NFS41_NB_SLOTS ;

  if( nfs41_Build_sessionid( &clientid, pnfs41_session->session_id ) != 1 )
    {
       res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT ;
       return res_CREATE_SESSION4.csr_status ;
    }

  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_sequence = 1 ;
  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_flags = CREATE_SESSION4_FLAG_CONN_BACK_CHAN ;

  /* return the input for wantinf of something better (will change in later versions) */
  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_fore_chan_attrs = pnfs41_session->fore_channel_attrs ;
  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_back_chan_attrs = pnfs41_session->back_channel_attrs ;
 
  memcpy( res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_sessionid, pnfs41_session->session_id, NFS4_SESSIONID_SIZE ) ;

  if( !nfs41_Session_Set(  pnfs41_session->session_id, pnfs41_session ) )
    {
       res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT ; /* Maybe a more precise status would be better */
       return res_CREATE_SESSION4.csr_status ;
    }

  /* Successful exit */
  res_CREATE_SESSION4.csr_status = NFS4_OK ;
  return res_CREATE_SESSION4.csr_status ;
} /* nfs41_op_create_session */



/**
 * nfs41_op_create_session_Free: frees what was allocared to handle nfs41_op_create_session.
 * 
 * Frees what was allocared to handle nfs41_op_create_session.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */ 
void nfs41_op_create_session_Free( CREATE_SESSION4res * resp )
{
  /* To be completed */
  return ;
} /* nfs41_op_create_session_Free */
