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
 * \file    nfs4_op_lock.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lock.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs4_op_lock: The NFS4_OP_LOCK operation. 
 *
 * This function implements the NFS4_OP_LOCK operation.
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

#define arg_LOCK4 op->nfs_argop4_u.oplock
#define res_LOCK4 resp->nfs_resop4_u.oplock

int nfs4_op_lock(  struct nfs_argop4 * op ,   
                     compound_data_t   * data,
                     struct nfs_resop4 * resp)
{
  char                       __attribute__(( __unused__ )) funcname[] = "nfs4_op_lock" ;
  cache_inode_status_t       cache_status ;
  lock_owner4                lockowner ;
  cache_inode_state_data_t   candidate_data ;
  cache_inode_state_type_t   candidate_type ;
  int                        rc = 0 ; 
  cache_inode_state_t      * file_state = NULL ;
  cache_inode_state_t      * pstate_found = NULL ;
  cache_inode_state_t      * pstate_previous_iterate = NULL ;
  cache_inode_state_t      * pstate_found_iterate = NULL ;
  uint64_t                   distance = 0LL ;
  nfs_client_id_t            nfs_client_id ;

  /* Lock are not supported */
  resp->resop = NFS4_OP_LOCK ;
  res_LOCK4.status =  NFS4ERR_LOCK_NOTSUPP ;

  /* If there is no FH */
  if( nfs4_Is_Fh_Empty( &(data->currentFH  ) ) )
    {
      res_LOCK4.status = NFS4ERR_NOFILEHANDLE ;
      return res_LOCK4.status ;
    }

  /* If the filehandle is invalid */
  if( nfs4_Is_Fh_Invalid( &(data->currentFH ) ) )
    {
      res_LOCK4.status = NFS4ERR_BADHANDLE ;
      return res_LOCK4.status ;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if( nfs4_Is_Fh_Expired( &(data->currentFH) ) )
    {
      res_LOCK4.status = NFS4ERR_FHEXPIRED ;
      return res_LOCK4.status ;
    }

  /* Commit is done only on a file */
  if( data->current_filetype != REGULAR_FILE )
    {
        /* Type of the entry is not correct */
        switch( data->current_filetype )
          {
             case DIR_BEGINNING:
             case DIR_CONTINUE:
                res_LOCK4.status = NFS4ERR_ISDIR ;
                break ;
             default:
                res_LOCK4.status = NFS4ERR_INVAL ;
                break ;
          }
    }

  /* Lock length should not be 0 */
  if( arg_LOCK4.length == 0LL )
    {
       res_LOCK4.status = NFS4ERR_INVAL ;
       return res_LOCK4.status ;
    }

  /* Check for range overflow 
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if( arg_LOCK4.length != 0xffffffffffffffffLL ) 
    {
        /* Comparing beyond 2^64 is not possible int 64 bits precision, but off+len > 2^64 is equivalent to len > 2^64 - off */
        if( arg_LOCK4.length > ( 0xffffffffffffffffLL - arg_LOCK4.offset ) )
          {
             res_LOCK4.status = NFS4ERR_INVAL ;
             return res_LOCK4.status ;
          }
    }

  /* loop into the states related to this pentry to find the related lock */
  pstate_found_iterate = NULL ;
  pstate_previous_iterate = pstate_found ;
  do
   {
     cache_inode_state_iterate( data->current_entry,
                                &pstate_found_iterate,
                                pstate_previous_iterate, 
                                data->pclient,
                                data->pcontext, 
                                &cache_status ) ;
      if( ( cache_status == CACHE_INODE_STATE_ERROR ) || ( cache_status == CACHE_INODE_INVALID_ARGUMENT ) )
       {
          res_LOCK4.status = NFS4ERR_INVAL ;
          return res_LOCK4.status ;
       }

      if( pstate_found_iterate != NULL ) 
       {
        if( pstate_found_iterate->state_type == CACHE_INODE_STATE_LOCK )
         {
            /* We found a lock, check is they overlap */
            if( arg_LOCK4.offset > pstate_found_iterate->state_data.lock.offset )
               distance = arg_LOCK4.offset -  pstate_found_iterate->state_data.lock.offset ;
            else
               distance = pstate_found_iterate->state_data.lock.offset -  arg_LOCK4.offset ;

            if( distance <= ( arg_LOCK4.length + pstate_found_iterate->state_data.lock.length ) )
             {
	       /* Locks are overlapping */
               

               /* If both lock are READ, this is not a case of error  */
               if(  ( arg_LOCK4.locktype != READ_LT ) || (  pstate_found_iterate->state_data.lock.lock_type != READ_LT ) )
                 {
                   /* Overlapping lock is found, if owner is different than the calling owner, return NFS4ERR_DENIED */
                   if( ( lockowner.owner.owner_len == pstate_found_iterate->state_owner.owner.owner_len ) &&
                       ( !memcmp( lockowner.owner.owner_val,
                                  pstate_found_iterate->state_owner.owner.owner_val, 
                                  pstate_found_iterate->state_owner.owner.owner_len ) ) )
                    {
                       /* The calling state owner is the same. There is a discussion on this case at page 161 of RFC3530. I choose to ignore this
                        * lock and continue iterating on the other states */        
                    }
                   else
                    {
                       /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
                       res_LOCK4.LOCK4res_u.denied.offset   = pstate_found_iterate->state_data.lock.offset ;
                       res_LOCK4.LOCK4res_u.denied.length   = pstate_found_iterate->state_data.lock.length ;
                       res_LOCK4.LOCK4res_u.denied.locktype = pstate_found_iterate->state_data.lock.lock_type ;
                       res_LOCK4.LOCK4res_u.denied.owner.owner.owner_len = pstate_found_iterate->state_owner.owner.owner_len ;
                       res_LOCK4.LOCK4res_u.denied.owner.owner.owner_val = pstate_found_iterate->state_owner.owner.owner_val ;
                       res_LOCK4.status = NFS4ERR_DENIED ;
                       return res_LOCK4.status ;
                    } 
                }
             }
         }
       }
     pstate_previous_iterate = pstate_found_iterate ;
   } while( pstate_found_iterate != NULL ) ;

  
  /* Is this a new lock owner or not ? */
  if( arg_LOCK4.locker.new_lock_owner == TRUE )
   {
      /* this is a new lockowner */
      lockowner = arg_LOCK4.locker.locker4_u.open_owner.lock_owner ;

      /* A lock owner is always associated with a previously made open
       * which has itself a previously made stateid */

      /* Check stateid correctness */
      if( ( rc = nfs4_Check_Stateid( &arg_LOCK4.locker.locker4_u.open_owner.open_stateid, data->current_entry ) )  != NFS4_OK )
       {
         res_LOCK4.status = rc ;
         return res_LOCK4.status ;
       }

      /* Check is the clientid is known or not */
      if( nfs_client_id_get( arg_LOCK4.locker.locker4_u.open_owner.lock_owner.clientid, 
                             &nfs_client_id ) == CLIENT_ID_NOT_FOUND )
       {
         res_LOCK4.status = NFS4ERR_STALE_CLIENTID ;
         return res_LOCK4.status ;
       }

      /* Try to get the related stateid */
      if( cache_inode_get_state( arg_LOCK4.locker.locker4_u.open_owner.open_stateid.other,
                                 &pstate_found,
                                 data->pclient,
                                 &cache_status ) != CACHE_INODE_SUCCESS )
       {
          res_LOCK4.status = nfs4_Errno( cache_status ) ;
          return res_LOCK4.status ;
       }

     /* An open state has been found. Check its type */
     if( pstate_found->state_type != CACHE_INODE_STATE_SHARE )
      {
         res_LOCK4.status = NFS4ERR_BAD_STATEID ;
         return res_LOCK4.status ;
      }

#ifdef _DEBUG_LOCK
     printf( "=== New Owner ===> %u %u %u\n", arg_LOCK4.locker.locker4_u.open_owner.open_stateid.seqid,
                                              arg_LOCK4.locker.locker4_u.open_owner.open_seqid, pstate_found->seqid ) ;
#endif

     /* check the stateid */
     if( arg_LOCK4.locker.locker4_u.open_owner.open_stateid.seqid < pstate_found->seqid )
       {
          res_LOCK4.status = NFS4ERR_OLD_STATEID ;
          return res_LOCK4.status ;
       } 

     /* Check validity of the seqid */
     if( ( arg_LOCK4.locker.locker4_u.open_owner.open_seqid < pstate_found->seqid ) ||
         ( arg_LOCK4.locker.locker4_u.open_owner.open_seqid > pstate_found->seqid +2 ) )
       {
          res_LOCK4.status = NFS4ERR_BAD_SEQID ;
          return res_LOCK4.status ;
       } 

     /* Sanity check : Is this the right file ? */
     if( pstate_found->pentry != data->current_entry )
      {
         res_LOCK4.status = NFS4ERR_BAD_STATEID ;
         return res_LOCK4.status ;
      }

     /* Lock seqid (seqid wanted for new lock) should be 0 (see newpynfs test LOCK8c)  */
     if(  arg_LOCK4.locker.locker4_u.open_owner.lock_seqid != 0 )
       {
          res_LOCK4.status = NFS4ERR_BAD_SEQID ;
          return res_LOCK4.status ;
       }

     /* No conflict found. Prepare state management structure */
     candidate_type = CACHE_INODE_STATE_LOCK ;
     candidate_data.lock.lock_type = arg_LOCK4.locktype ;
     candidate_data.lock.offset    = arg_LOCK4.offset ;
     candidate_data.lock.length    = arg_LOCK4.length ;
   }
  else
   {
      /* The owner already exists, use the provided owner to create a new state */
      /* Get the former state */
      if( cache_inode_get_state( arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.other, 
                                 &pstate_found, 
                                 data->pclient,
                                 &cache_status ) != CACHE_INODE_SUCCESS )
       {
          res_LOCK4.status = NFS4ERR_STALE_STATEID ;
          return res_LOCK4.status ;
       }

      /* An lock state has been found. Check its type */
      if( pstate_found->state_type != CACHE_INODE_STATE_LOCK )
       {
          res_LOCK4.status = NFS4ERR_BAD_STATEID ;
          return res_LOCK4.status ;
       }

#ifdef _DEBUG_LOCK
     printf( "=== Konwn LockOwner ===> %u %u %u\n", arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.seqid,
                                   arg_LOCK4.locker.locker4_u.lock_owner.lock_seqid, pstate_found->seqid ) ;
#endif

     /* Check if stateid is not too old */
     if( arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.seqid < pstate_found->seqid )
       {
          res_LOCK4.status = NFS4ERR_OLD_STATEID ;
          return res_LOCK4.status ;
       }

     /* Check good seq id */
     if( arg_LOCK4.locker.locker4_u.lock_owner.lock_seqid > arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.seqid )
       {
          res_LOCK4.status = NFS4ERR_BAD_SEQID ;
          return res_LOCK4.status ;
       }

     /* Check validity of the desired seqid */
     if( (  arg_LOCK4.locker.locker4_u.lock_owner.lock_seqid !=  pstate_found->seqid ) &&
         (  arg_LOCK4.locker.locker4_u.lock_owner.lock_seqid != pstate_found->seqid + 1 ) )
       {
          res_LOCK4.status = NFS4ERR_BAD_SEQID ;
          return res_LOCK4.status ;
       }

#ifdef _CONFORM_TO_TEST_LOCK8c
     /* Check validity of the seqid */
     if(  arg_LOCK4.locker.locker4_u.lock_owner.lock_seqid != 0 )
       {
          res_LOCK4.status = NFS4ERR_BAD_SEQID ;
          return res_LOCK4.status ;
       }
#endif

     /* Sanity check : Is this the right file ? */
     if( pstate_found->pentry != data->current_entry )
      {
         res_LOCK4.status = NFS4ERR_BAD_STATEID ;
         return res_LOCK4.status ;
      }

     memcpy( (char *)&lockowner, (char *)&(pstate_found->state_owner), sizeof( lock_owner4 ) ) ; 
   } 
 

   if( arg_LOCK4.locker.new_lock_owner == TRUE )
    { 
      /* Add the lock state to the lock table */
      if( cache_inode_add_state( data->current_entry,
                                 candidate_type,
                                 &candidate_data, 
                                 1, /* Lock have a initial seqid of 1 */
                                 ((open_owner4 *)&lockowner),
                                 data->pclient,
                                 data->pcontext,
                                 &file_state,
                                 &cache_status ) != CACHE_INODE_SUCCESS )
        {
          res_LOCK4.status = NFS4ERR_STALE_STATEID ;
          return res_LOCK4.status ;
        }

      /** @todo BUGAZOMEU: Manage the case if lock conflicts */
      res_LOCK4.LOCK4res_u.resok4.lock_stateid.seqid = file_state->seqid ;
      memcpy( res_LOCK4.LOCK4res_u.resok4.lock_stateid.other, file_state->stateid_other, 12 ) ;
    }
  else
    {
      /* Increment the seqid */
      pstate_found->seqid += 1 ;
      res_LOCK4.LOCK4res_u.resok4.lock_stateid.seqid = pstate_found->seqid ;
      memcpy( res_LOCK4.LOCK4res_u.resok4.lock_stateid.other, pstate_found->stateid_other, 12 ) ;
    }
#ifdef _DEBUG_STATES
  nfs_State_PrintAll(  ) ;
#endif

  res_LOCK4.status = NFS4_OK ;
  return res_LOCK4.status;
} /* nfs4_op_lock */

    
/**
 * nfs4_op_lock_Free: frees what was allocared to handle nfs4_op_lock.
 * 
 * Frees what was allocared to handle nfs4_op_lock.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_lock_Free( LOCK4res * resp )
{ 
  /* Nothing to Mem_Free */
  return ;
} /* nfs4_op_lock_Free */
