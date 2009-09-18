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
 * \file    nfs41_op_sequence.c
 * \author  $Author: deniel $
 * \brief   Routines used for managing the NFS4_OP_SEQUENCE operation.
 *
 * nfs41_op_sequence.c : Routines used for managing the NFS4_OP_SEQUENCE operation.
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
 * nfs41_op_sequence: the NFS4_OP_SEQUENCE operation
 *
 * This functions handles the NFS4_OP_SEQUENCE operation in NFSv4. This function can be called only from nfs4_Compound.
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
int nfs41_op_sequence(  struct nfs_argop4 * op ,   
                        compound_data_t   * data,
                        struct nfs_resop4 * resp)
{
#define arg_SEQUENCE4  op->nfs_argop4_u.opsequence
#define res_SEQUENCE4  resp->nfs_resop4_u.opsequence

   nfs41_session_t * psession ;
  
   resp->resop = NFS4_OP_SEQUENCE ;
   res_SEQUENCE4.sr_status = NFS4_OK ;

   /* OP_SEQUENCE is always the first operation of the request */
   if( data->oppos != 0 )
    {
        res_SEQUENCE4.sr_status = NFS4ERR_SEQUENCE_POS ;
        return res_SEQUENCE4.sr_status ;
    }

   if( !nfs41_Session_Get_Pointer( arg_SEQUENCE4.sa_sessionid, &psession ) )
    {
        res_SEQUENCE4.sr_status = NFS4ERR_BADSESSION ;
        return res_SEQUENCE4.sr_status ;
    } 

  /* Check is slot is compliant with ca_maxrequests */
  if( arg_SEQUENCE4.sa_slotid >= psession->fore_channel_attrs.ca_maxrequests )
    {
        res_SEQUENCE4.sr_status = NFS4ERR_BADSLOT ;
        return res_SEQUENCE4.sr_status ;
    } 

  /* By default, no DRC replay */
  data->use_drc = FALSE ;

  P( psession->slots[arg_SEQUENCE4.sa_slotid].lock ) ;
  if( psession->slots[arg_SEQUENCE4.sa_slotid].sequence +1 != arg_SEQUENCE4.sa_sequenceid )
    {
        if( psession->slots[arg_SEQUENCE4.sa_slotid].sequence == arg_SEQUENCE4.sa_sequenceid )
         {
            /* Replay operation through the DRC */
            data->use_drc = TRUE ;
	    data->pcached_res = psession->slots[arg_SEQUENCE4.sa_slotid].cached_result ;

            res_SEQUENCE4.sr_status = NFS4_OK ;
            return res_SEQUENCE4.sr_status ;
         }
        V( psession->slots[arg_SEQUENCE4.sa_slotid].lock ) ;
        res_SEQUENCE4.sr_status = NFS4ERR_SEQ_MISORDERED ;
        return res_SEQUENCE4.sr_status ;
    } 

  /* Keep memory of the session in the COMPOUND's data */
  data->psession = psession ; 

  /* Update the sequence id within the slot */
  psession->slots[arg_SEQUENCE4.sa_slotid].sequence += 1 ;

  memcpy( (char *)res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_sessionid, (char *)arg_SEQUENCE4.sa_sessionid, NFS4_SESSIONID_SIZE ) ;
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_sequenceid = psession->slots[arg_SEQUENCE4.sa_slotid].sequence ;
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_slotid = arg_SEQUENCE4.sa_slotid ;
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_highest_slotid = NFS41_NB_SLOTS -1 ;
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_target_highest_slotid = arg_SEQUENCE4.sa_slotid ; /* Maybe not the best choice */
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_status_flags = 0 ; /* What is to be set here ? */ 

  if( arg_SEQUENCE4.sa_cachethis == TRUE )
    data->pcached_res = psession->slots[arg_SEQUENCE4.sa_slotid].cached_result ;
  else
    data->pcached_res = NULL ;

  V( psession->slots[arg_SEQUENCE4.sa_slotid].lock ) ;

  res_SEQUENCE4.sr_status = NFS4_OK ; 
  return res_SEQUENCE4.sr_status ;
} /* nfs41_op_sequence */


/**
 * nfs41_op_sequence_Free: frees what was allocared to handle nfs41_op_sequence.
 * 
 * Frees what was allocared to handle nfs41_op_sequence.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_sequence_Free( SEQUENCE4res * resp )
{
  /* Nothing to be done */
  return ;
} /* nfs4_op_sequence_Free */
