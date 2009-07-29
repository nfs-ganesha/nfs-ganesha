         /* vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    cache_inode_state.c
 * \author  $Author: deniel $
 * \date    $Date$
 * \version $Revision$
 * \brief   This file contains functions used in state management.
 *
 * cache_inode_state.c : This file contains functions used in state management.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif /* _SOLARIS */

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"
#include "nfs_core.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>


/**
 *
 * cache_inode_state_conflict : checks for a conflict between an existing state and a candidate state.
 *
 * Checks for a conflict between an existing state and a candidate state.
 *
 * @param pstate      [IN] existing state
 * @param state_type  [IN] type of candidate state
 * @param pstate_data [IN] data for the candidate state
 *
 * @return TRUE if there is a conflict, FALSE if no conflict has been found
 *
 */
int cache_inode_state_conflict( cache_inode_state_t       * pstate, 
                                cache_inode_state_type_t    state_type,
                                cache_inode_state_data_t  * pstate_data ) 
{
  int rc = FALSE;

  if( pstate == NULL || pstate_data == NULL )
    return TRUE ;

  switch( state_type ) 
   {
      case CACHE_INODE_STATE_NONE:
         rc= FALSE ; /* STATE_NONE conflicts with nobody */
         break ;

      case CACHE_INODE_STATE_SHARE:
         if( pstate->state_type == CACHE_INODE_STATE_SHARE )
          {
             if( ( pstate->state_data.share.share_access & pstate_data->share.share_deny ) ||
                 ( pstate->state_data.share.share_deny & pstate_data->share.share_access ) )
                rc = TRUE ;
          }        

      case CACHE_INODE_STATE_LOCK:
         rc = FALSE ;
         break ; /* lock conflict is managed in the NFS request */
         
      case CACHE_INODE_STATE_DELEG:
      default:
         /* Not yet implemented for now, answer TRUE to 
          * avoid weird behavior */
         rc= TRUE ;
         break ;
   }

  return rc ;
} /* cache_inode_state_conflict */

/**
 *
 * cache_inode_add_state: adds a new state to a file pentry 
 *
 * Adds a new state to a file pentry 
 *
 * @param pentry        [INOUT] cache entry to operate on
 * @param state_type    [IN]    state to be defined
 * @param pstate_data   [IN]    data related to this state
 * @param powner_input  [IN]    related open_owner
 * @param pclient       [INOUT] cache inode client to be used
 * @param pcontext      [IN]    FSAL credentials
 * @param ppstate       [OUT]   pointer to a pointer to the new state
 * @param pstatus       [OUT]   returned status 
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_add_state( cache_entry_t              * pentry, 
                                            cache_inode_state_type_t     state_type,
                                            cache_inode_state_data_t   * pstate_data,
                                            cache_inode_open_owner_t   * powner_input,
                                            cache_inode_client_t       * pclient,
                                            fsal_op_context_t          * pcontext,
                                            cache_inode_state_t      * * ppstate,
                                            cache_inode_status_t       * pstatus )
{
  cache_inode_state_t           * phead_state   = NULL ;
  cache_inode_state_t           * pnew_state    = NULL ;
  cache_inode_state_t           * piter_state   = NULL ;
  cache_inode_state_t           * piter_saved   = NULL ;
  cache_inode_open_owner_t      * powner   = powner_input ;
  u_int64_t                       fileid_digest = 0 ;
  u_int16_t                       new_counter   = 0 ;
  char                            other_head[12] ;
  bool_t                          conflict_found = FALSE ;
#ifdef _DEBUG_STATES
  unsigned int                    i = 0 ;
#endif

  /* Sanity Check */
  if( pstatus == NULL ) 
    return CACHE_INODE_INVALID_ARGUMENT ;
 
  if( pentry == NULL || pstate_data == NULL || pclient == NULL || pcontext == NULL || powner_input == NULL || ppstate == NULL )
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
   }

  /* entry has to be a file */
  if( pentry->internal_md.type != REGULAR_FILE )
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
   }

  /* Acquire lock to enter critical section on this entry */
  P_w( &pentry->lock ) ;

  GET_PREALLOC( pnew_state,
                pclient->pool_state_v4,
                pclient->nb_pre_state_v4,
                cache_inode_state_t,
                next ) ;

  if( pnew_state == NULL )
    {
      DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't allocate a new file state from cache pool" ) ;
      *pstatus = CACHE_INODE_MALLOC_ERROR ;

       /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;
     
      V_w( &pentry->lock ) ;

      return *pstatus ;
    }

#ifdef _DEBUG_STATES
  printf( "         ----- Entering cache_inode_add_state: head_counter=%u current_counter=%u\n",
          pentry->object.file.state_head_counter, pentry->object.file.state_current_counter  ) ;
#endif

  if( pentry->object.file.state_current_counter == 0xFFFF )
     new_counter = 1 ;
  else
     new_counter = pentry->object.file.state_current_counter + 1 ;

  /* Here, we need to know the file id */
  if( FSAL_IS_ERROR( FSAL_DigestHandle( pcontext->export_context,
                                        FSAL_DIGEST_FILEID3,
                                        &(pentry->object.file.handle),
                                        (caddr_t)&fileid_digest ) ) )
     {
         DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (B)", pentry ) ;
         *pstatus = CACHE_INODE_STATE_ERROR ;

         /* stat */
         pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;

         V_w( &pentry->lock ) ;

         return *pstatus ;
     }


  /* If there already a state or not ? */
  if( pentry->object.file.state_head_counter == 0 )
   {
      /* The file has no state for now, accept this new state */
      pnew_state->next = NULL ;
      pnew_state->prev = NULL ;

      /* Add the stateid.other, this will increment pentry->object.file.state_current_counter */
      if( !nfs_BuildStateId_Other_Raw( fileid_digest, new_counter, pnew_state->stateid_other ) )
       {
         DisplayLogJd( pclient->log_outputs, "Can't create a new state id for the pentry %p (A)", pentry ) ;
         *pstatus = CACHE_INODE_STATE_ERROR ;

         /* stat */
         pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;

         V_w( &pentry->lock ) ;

         return *pstatus ;
       }
    
      /* Set the type and data for this state */
      pnew_state->state_type  = state_type ;
      memcpy( (char *)&(pnew_state->state_data), (char *)pstate_data, sizeof( cache_inode_state_data_t ) ) ;
      pnew_state->seqid = 0 ;
      pnew_state->pentry = pentry ;
      pnew_state->powner = powner ;

     /* Set the head state id */
     pentry->object.file.state_current_counter = new_counter ;
     pentry->object.file.state_head_counter = pentry->object.file.state_current_counter ;
     pnew_state->my_id = pentry->object.file.state_current_counter ;

   }
  else
   {
      /* Build the 'other' for head's state */
      if( !nfs_BuildStateId_Other_Raw( fileid_digest, pentry->object.file.state_head_counter, other_head ) )
       {
          DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (C)", pentry ) ;
          *pstatus = CACHE_INODE_STATE_ERROR ;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;

          V_w( &pentry->lock ) ;

          return *pstatus ;
       }

     /* Get the state from the hash */
     if( !nfs4_State_Get_Pointer( other_head, &phead_state ) )
      {
          DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (D)", pentry ) ;
          *pstatus = CACHE_INODE_STATE_ERROR ;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;

          V_w( &pentry->lock ) ;

          return *pstatus ;
      }
 
    /* Brwose the state's list */
    for( piter_state = phead_state ; piter_state != NULL ; piter_saved =  piter_state, piter_state = piter_state->next )
     {
        if( cache_inode_state_conflict( piter_state, state_type, pstate_data ) )
         {
           conflict_found = TRUE ;
           break ;
         }
     }

    /* An error is to be returned if a conflict is found */
    if( conflict_found == TRUE )
     {
          DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "new state conflicts with another state for pentry %p", pentry ) ;
          *pstatus = CACHE_INODE_STATE_CONFLICT ;

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;

          V_w( &pentry->lock ) ;

          return *pstatus ;
     }

    /* If this point is reached, then the state is to be added to the state list and piter_saved is the tail of the list  */
    pnew_state->next = NULL ;
    pnew_state->prev = piter_saved ;
    piter_saved->next = pnew_state ;


    /* Add the stateid.other, this will increment pentry->object.file.state_current_counter */
    if( !nfs_BuildStateId_Other_Raw( fileid_digest, new_counter, pnew_state->stateid_other ) )
     {
       DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (E)", pentry ) ;
       *pstatus = CACHE_INODE_STATE_ERROR ;

       /* stat */
       pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;

       V_w( &pentry->lock ) ;

       return *pstatus ;
     }
  
    /* Set the type and data for this state */
    pnew_state->state_type = state_type ;
    memcpy( (char *)&(pnew_state->state_data), (char *)pstate_data, sizeof( cache_inode_state_data_t ) ) ;
    pnew_state->seqid = 0;
    pnew_state->pentry = pentry ;
    pnew_state->powner = powner ;

    /* Set the head state id */
    pentry->object.file.state_current_counter = new_counter ;
    pnew_state->my_id = pentry->object.file.state_current_counter ;
   } /* else */

#ifdef _DEBUG_STATES 
  printf( "         ----- Exiting cache_inode_add_state: head_counter=%u current_counter=%u pnew_state=%p type=%u\n",
          pentry->object.file.state_head_counter, pentry->object.file.state_current_counter, pnew_state, state_type  ) ;
#endif

  /* Add the state to the related hashtable */
  if( !nfs4_State_Set( pnew_state->stateid_other, pnew_state ) )
   {
     DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (F)", pentry ) ;
     *pstatus = CACHE_INODE_STATE_ERROR ;

     /* stat */
     pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_ADD_STATE] += 1 ;

     V_w( &pentry->lock ) ;

     return *pstatus ;
   }

  /* Copy the result */
  *ppstate = pnew_state ;

  /* Regular exit */
  *pstatus = CACHE_INODE_SUCCESS ;
#ifdef _DEBUG_STATES
   printf( "         -----  cache_inode_add_state : " ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)pnew_state->stateid_other[i] ) ;
   printf( "\n" ) ;
#endif

  V_w( &pentry->lock ) ;

  return *pstatus ;
} /* cache_inode_add_state */

/**
 *
 * cache_inode_get_state: gets a state from the hash's state
 *
 * Gets a state from the hash's state
 *
 * @param other     [IN]    stateid.other used as hash key
 * @param ppstate   [OUT]   pointer to the pointer to the new state
 * @param pclient   [INOUT] related cache inode client
 * @param pstatus   [OUT]   returned status 
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_get_state( char                         other[12], 
                                            cache_inode_state_t     *  * ppstate,
                                            cache_inode_client_t       * pclient,
                                            cache_inode_status_t       * pstatus )
{
  if( pstatus == NULL ) 
    return CACHE_INODE_INVALID_ARGUMENT ;

  if( ppstate == NULL || pclient == NULL ) 
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
   }

  if( !nfs4_State_Get_Pointer( other, ppstate ) )
   {
      *pstatus = CACHE_INODE_NOT_FOUND ;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET_STATE] += 1 ;

      return *pstatus ;
  }

 /* Sanity check, mostly for debug */
 if( memcmp( other, (*ppstate)->stateid_other, 12 ) )
   printf( "-------------> Warning !!!! Stateid(other) differs !!!!!!\n" ) ;

 *pstatus = CACHE_INODE_SUCCESS ;
 return *pstatus ;
} /* cache_inode_get_state */

/**
 *
 * cache_inode_update_state: update a state from the hash's state
 *
 * Updates a state from the hash's state
 *
 * @param pstate   [OUT]   pointer to the new state
 * @param pclient  [INOUT] related cache inode client
 * @param pstatus  [OUT]   returned status 
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_update_state( cache_inode_state_t        * pstate,
                                               cache_inode_client_t       * pclient,
                                               cache_inode_status_t       * pstatus )
{
  if( pstatus == NULL ) 
    return CACHE_INODE_INVALID_ARGUMENT ;

  if( pstate == NULL || pclient == NULL ) 
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
   }
 
  if( !nfs4_State_Update( pstate->stateid_other, pstate ) )
   {
      *pstatus = CACHE_INODE_STATE_ERROR ;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_UPDATE_STATE] += 1 ;

      return *pstatus ;
  }

  *pstatus = CACHE_INODE_SUCCESS ;
  return *pstatus ;
} /* cache_inode_set_state */

/**
 *
 * cache_inode_del_state_by_key: deletes a state from the hash's state associated with a given stateid
 *
 * Deletes a state from the hash's state
 *
 * @param other    [IN]    stateid.other used as hash key
 * @param pstate   [OUT]   pointer to the new state
 * @param pclient  [INOUT] related cache inode client
 * @param pstatus  [OUT]   returned status 
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_del_state_by_key( char                         other[12], 
                                                   cache_inode_client_t       * pclient,
                                                   cache_inode_status_t       * pstatus )
{
  cache_inode_state_t  * pstate = NULL ;
  cache_entry_t        * pentry = NULL ;
  
  if( pstatus == NULL ) 
    return CACHE_INODE_INVALID_ARGUMENT ;

  if( pstatus == NULL || pclient == NULL ) 
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
   }


  /* Does this state exists ? */
  if( !nfs4_State_Get_Pointer( other, &pstate ) )
   {
      *pstatus = CACHE_INODE_NOT_FOUND ;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1 ;

      return *pstatus ;
   }

  /* The state exists, locks the related pentry before operating on it */
  pentry = pstate->pentry ;

  P_w( &pentry->lock ) ;

  /* Set the head counter */
  if( pstate->my_id == pentry->object.file.state_head_counter )
   {
     /* This is the first state managed */ 
     if( pstate->next == NULL )
      {
        /* I am the only remaining state, set the head counter to 0 in the pentry */
        pentry->object.file.state_head_counter    = 0 ;
      }
     else
      {
        /* The state that is next to me become the new head */
        pentry->object.file.state_head_counter = pstate->next->my_id ;
      }
   }

  /* redo the double chained list */
  if( pstate->next != NULL )
    pstate->next->prev = pstate->prev ;

  if( pstate->prev != NULL ) 
    pstate->prev->next = pstate->next ; 

  if( !memcmp(  (char *)pstate->stateid_other, other, 12 ) )
    {
  	/* Remove the entry from the HashTable */
        if( !nfs4_State_Del( pstate->stateid_other ) )
    	 {
      	    *pstatus = CACHE_INODE_STATE_ERROR ;

      	    /* stat */
      	    pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1 ;

      	    V_w( &pentry->lock ) ;

      	    return *pstatus ;
   	 }


        /* reset the pstate field to avoid later mistakes */
        memset( (char *)pstate->stateid_other, 0, 12 ) ;
        pstate->state_type                  = CACHE_INODE_STATE_NONE ;
        pstate->my_id                       = 0 ;
        pstate->next                        = NULL ;
        pstate->prev                        = NULL ;
        pstate->pentry                      = NULL ;

        RELEASE_PREALLOC( pstate,
                          pclient->pool_state_v4,
                          next ) ; 
     }

  *pstatus = CACHE_INODE_SUCCESS ;
 
 V_w( &pentry->lock ) ;

 return *pstatus ;
} /* cache_inode_del_state_by_key */

/**
 *
 * cache_inode_del_state: deletes a state from the hash's state
 *
 * Deletes a state from the hash's state
 *
 * @param pstate   [OUT]   pointer to the new state
 * @param pclient  [INOUT] related cache inode client
 * @param pstatus  [OUT]   returned status 
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_del_state( cache_inode_state_t        * pstate,
                                            cache_inode_client_t       * pclient,
                                            cache_inode_status_t       * pstatus )
{
  cache_inode_state_t  * ptest_state = NULL ;
  cache_entry_t        * pentry = NULL ;
  
  if( pstatus == NULL ) 
    return CACHE_INODE_INVALID_ARGUMENT ;

  if( pstate == NULL || pclient == NULL ) 
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
   }

#ifdef _DEBUG_STATES
   {
     unsigned int i = 0 ;

     printf( "         -----  cache_inode_del_state : " ) ;
     for( i = 0 ; i < 12 ; i++ )
       printf( "%02x", (unsigned char)pstate->stateid_other[i] ) ;
     printf( "\n" ) ;
   }
#endif


  /* Does this state exists ? */
  if( !nfs4_State_Get_Pointer( pstate->stateid_other, &ptest_state ) )
   {
      *pstatus = CACHE_INODE_NOT_FOUND ;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1 ;

      return *pstatus ;
   }

  /* The state exists, locks the related pentry before operating on it */
  pentry = pstate->pentry ;

  P_w( &pentry->lock ) ;

  /* Set the head counter */
  if( pstate->my_id == pentry->object.file.state_head_counter )
   {
     /* This is the first state managed */ 
     if( pstate->next == NULL )
      {
        /* I am the only remaining state, set the head counter to 0 in the pentry */
        pentry->object.file.state_head_counter    = 0 ;
      }
     else
      {
        /* The state that is next to me become the new head */
        pentry->object.file.state_head_counter = pstate->next->my_id ;
      }
   }

  /* redo the double chained list */
  if( pstate->next != NULL )
    pstate->next->prev = pstate->prev ;

  if( pstate->prev != NULL ) 
    pstate->prev->next = pstate->next ; 

  /* Remove the entry from the HashTable */
  if( !nfs4_State_Del( pstate->stateid_other ) )
   {
      *pstatus = CACHE_INODE_STATE_ERROR ;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_DEL_STATE] += 1 ;

      V_w( &pentry->lock ) ;

      return *pstatus ;
   }

  /* reset the pstate field to avoid later mistakes */
  memset( (char *)pstate->stateid_other, 0, 12 ) ;
  pstate->state_type                  = CACHE_INODE_STATE_NONE ;
  pstate->my_id                       = 0 ;
  pstate->next                        = NULL ;
  pstate->prev                        = NULL ;
  pstate->pentry                      = NULL ;

 

   RELEASE_PREALLOC( pstate,
                    pclient->pool_state_v4,
                    next ) ; 

  *pstatus = CACHE_INODE_SUCCESS ;
 
 V_w( &pentry->lock ) ;

 return *pstatus ;
} /* cache_inode_del_state */

/**
 *
 * cache_inode_get_state: gets a state from the hash's state
 *
 * Gets a state from the hash's state. If a state owned by that lockowner was found in a previous call,
 * starts looking at this entry
 *
 * @param other           [IN]    stateid.other used as hash key
 * @param powner          [IN]    the owner that is used as key to locate a state
 * @param ppstate         [OUT]   pointer to a pointer of state that will point to the result
 * @param previous_pstate [IN]    a pointer to be used to start search. Should be NULL at first call
 * @param pclient         [INOUT] related cache inode client
 * @param pcontext        [IN]    related FSAL operation context
 * @pstatus               [OUT]   status for the operation
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_find_state_by_owner( cache_entry_t           * pentry, 
                                                      open_owner4             * powner, 
                                                      cache_inode_state_t   * * ppstate,
                                                      cache_inode_state_t     * previous_pstate,
                                                      cache_inode_client_t    * pclient,
                                                      fsal_op_context_t       * pcontext,
                                                      cache_inode_status_t    * pstatus )
{
  u_int64_t             fileid_digest = 0 ;
  char                  other_head[12] ;
  cache_inode_state_t * piter_state       = NULL ;
  cache_inode_state_t * piter_state_start = NULL ;
  cache_inode_state_t   head_state  ;
  bool_t                found = FALSE ;

  if( pstatus == NULL ) 
   return CACHE_INODE_INVALID_ARGUMENT ;

  if( pentry == NULL || powner == NULL || ppstate == NULL || 
      pclient == NULL || pcontext == NULL )
   {
     *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
     return *pstatus ;
   }
 
  /* Here, we need to know the file id */
  if( FSAL_IS_ERROR( FSAL_DigestHandle( pcontext->export_context,
                                       FSAL_DIGEST_FILEID3,
                                       &(pentry->object.file.handle),
                                       (caddr_t)&fileid_digest ) ) )
    {
        DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (F)", pentry ) ;
        *pstatus = CACHE_INODE_STATE_ERROR ;

        return *pstatus ;
    }

  P_r( &pentry->lock ) ;

  /* The file already have at least one state, browse all of them, starting with the first state */
  /* Build the 'other' for head's state */
  if( !nfs_BuildStateId_Other_Raw( fileid_digest, pentry->object.file.state_head_counter, other_head ) )
   {
      *pstatus = CACHE_INODE_STATE_ERROR ;

      V_r( &pentry->lock ) ;

      return *pstatus ;
   }
  
  /* if this is the first call, used the data stored in pentry to get the state's chain head */ 
  if( previous_pstate == NULL )
   {
     /* Get the state from the hash */
     if( !nfs4_State_Get( other_head, &head_state ) )
      {
        DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (G)", pentry ) ;
        *pstatus = CACHE_INODE_STATE_ERROR ;

        V_r( &pentry->lock ) ;

        return *pstatus ;
      }

     piter_state = &head_state ;
   }
  else
   {
     /* Sanity check: make sure that this state is related to this pentry */
     if( previous_pstate->pentry != pentry )
      {
        DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Bad previous pstate: related to pentry %p, not to %p", 
                           previous_pstate->pentry, pentry ) ;

        *pstatus = CACHE_INODE_STATE_ERROR ;

        V_r( &pentry->lock ) ;

        return *pstatus ;
      }

     piter_state = previous_pstate ; 
   }
  
  /* Search loop */
  for( piter_state = piter_state_start ; piter_state != NULL ; piter_state = piter_state->next )
   {
      if( piter_state->powner->clientid == powner->clientid )
       {
          /* Client ids match, check the owner */
          if( piter_state->powner->owner_len == powner->owner.owner_len )
           if( !memcmp( piter_state->powner->owner_val, powner->owner.owner_val, powner->owner.owner_len ) )
            {
                found = TRUE ;
                break ;
            } 
       }
   } /* for... */
  

  if( found == FALSE )
    *pstatus = CACHE_INODE_NOT_FOUND ;
  else
    *pstatus = CACHE_INODE_SUCCESS ;

  *ppstate = piter_state ;
  
   V_r( &pentry->lock ) ; 

  return *pstatus ;
} /* cache_inode_find_state_by_owner */


/**
 *  
 * cache_inode_state_del_all: deletes all states associated with a pentry (e.g. when closing a file)
 *
 * @param pentry   [IN]    the pentry to be operated on
 * @param pclient  [INOUT] related cacjhe inode client
 * @param pcontext [IN]    related FSAL operation context
 * @param pstatus  [OUT]   returned status
 *
 * @return the same as *pstatus
 *
 */  
cache_inode_status_t cache_inode_state_del_all( cache_entry_t         * pentry,
                                                cache_inode_client_t  * pclient,
                                                fsal_op_context_t     * pcontext,
                                                cache_inode_status_t  * pstatus )
{
  u_int64_t             fileid_digest = 0 ;
  cache_inode_state_t * piter_state   = NULL ;
  cache_inode_state_t * pnext_state   = NULL ;
  char                  other_head[12] ;

  if( pstatus == NULL )
    return CACHE_INODE_INVALID_ARGUMENT ;

  if( pentry == NULL || pclient == NULL || pcontext == NULL )
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
   }
 
  /* Here, we need to know the file id */
  if( FSAL_IS_ERROR( FSAL_DigestHandle( pcontext->export_context,
                                       FSAL_DIGEST_FILEID3,
                                       &(pentry->object.file.handle),
                                       (caddr_t)&fileid_digest ) ) )
    {
        *pstatus = CACHE_INODE_STATE_ERROR ;

        return *pstatus ;
    }

  P_w( &pentry->lock ) ;

  /* Build the 'other' related to the head of the state chain to start iterating */  
  if( !nfs_BuildStateId_Other_Raw( fileid_digest, pentry->object.file.state_head_counter, other_head ) )
   {
      *pstatus = CACHE_INODE_STATE_ERROR ;

       V_w( &pentry->lock ) ;

       return *pstatus ;
   }

  /* Get the head state from the hash */
  if( !nfs4_State_Get_Pointer( other_head, &piter_state ) )
    {
      *pstatus = CACHE_INODE_STATE_ERROR ;

      V_w( &pentry->lock ) ;

      return *pstatus ;
    }

#ifdef _DEBUG_STATES
  nfs_State_PrintAll(  ) ;
#endif

  /* Release all the states within the chain step by step */
  do
   {
    pnext_state = piter_state->next ;

    /* Remove the entry from the HashTable */
     if( !nfs4_State_Del( piter_state->stateid_other ) )
      {
        *pstatus = CACHE_INODE_STATE_ERROR ;

        V_w( &pentry->lock ) ;

        return *pstatus ;
      }


    /* reset the pstate field to avoid later mistakes */
    memset( (char *)piter_state->stateid_other, 0, 12 ) ;
    piter_state->state_type                  = CACHE_INODE_STATE_NONE ;
    piter_state->my_id                       = 0 ;
    piter_state->next                        = NULL ;
    piter_state->prev                        = NULL ;
    piter_state->pentry                      = NULL ;

    RELEASE_PREALLOC( piter_state,
                      pclient->pool_state_v4,
                      next ) ; 

    /* Go on to the next one */
    piter_state = pnext_state ;
     
#ifdef _DEBUG_STATES
    nfs_State_PrintAll( ) ;
#endif
   } while( piter_state != NULL ) ;

#ifdef _DEBUG_STATES
  nfs_State_PrintAll( ) ;
#endif
  /* No more state associated with this pentry */
  pentry->object.file.state_head_counter    = 0 ;
  pentry->object.file.state_current_counter  = 0 ;

  V_w( &pentry->lock ) ;

  /* Function is not implemented know */
  *pstatus = CACHE_INODE_SUCCESS ;
 
  return *pstatus ;
} /* cache_inode_state_del_all */

/**
 *
 * cache_inode_state_iterate: iterates on the states's loop
 *
 * Iterates on the states's loop
 *
 * @param other           [IN]    stateid.other used as hash key
 * @param ppstate         [OUT]   pointer to a pointer of state that will point to the result
 * @param previous_pstate [IN]    a pointer to be used to start search. Should be NULL at first call
 * @param pclient         [INOUT] related cache inode client
 * @param pcontext        [IN]    related FSAL operation context
 * @pstatus               [OUT]   status for the operation
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_state_iterate( cache_entry_t           * pentry, 
                                                cache_inode_state_t   * * ppstate,
                                                cache_inode_state_t     * previous_pstate,
                                                cache_inode_client_t    * pclient,
                                                fsal_op_context_t       * pcontext,
                                                cache_inode_status_t    * pstatus )
{
  u_int64_t             fileid_digest = 0 ;
  cache_inode_state_t * piter_state   = NULL ;
  cache_inode_state_t * phead_state   = NULL ;
  char                  other_head[12] ;

  if( pstatus == NULL ) 
   return CACHE_INODE_INVALID_ARGUMENT ;

  if( pentry == NULL || ppstate == NULL || 
      pclient == NULL || pcontext == NULL )
   {
     *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
     return *pstatus ;
   }

  /* Here, we need to know the file id */
  if( FSAL_IS_ERROR( FSAL_DigestHandle( pcontext->export_context,
                                       FSAL_DIGEST_FILEID3,
                                       &(pentry->object.file.handle),
                                       (caddr_t)&fileid_digest ) ) )
    {
        DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (F)", pentry ) ;
        *pstatus = CACHE_INODE_STATE_ERROR ;

        return *pstatus ;
    }

  P_r( &pentry->lock ) ;

  /* if this is the first call, used the data stored in pentry to get the state's chain head */ 
  if( previous_pstate == NULL )
   {
     /* The file already have at least one state, browse all of them, starting with the first state */
     /* Build the 'other' for head's state */
     if( !nfs_BuildStateId_Other_Raw( fileid_digest, pentry->object.file.state_head_counter, other_head ) )
      {
         *pstatus = CACHE_INODE_STATE_ERROR ;

         V_r( &pentry->lock ) ;

         return *pstatus ;
      }

     /* Get the state from the hash */
     if( !nfs4_State_Get_Pointer( other_head, &phead_state ) )
      {
        DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Can't create a new state id for the pentry %p (G)", pentry ) ;
        *pstatus = CACHE_INODE_STATE_ERROR ;

        V_r( &pentry->lock ) ;

        return *pstatus ;
      }

     piter_state = phead_state ;
   }
  else
   {
     /* Sanity check: make sure that this state is related to this pentry */
     if( previous_pstate->pentry != pentry )
      {
        DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "Bad previous pstate: related to pentry %p, not to %p", 
                           previous_pstate->pentry, pentry ) ;

        *pstatus = CACHE_INODE_STATE_ERROR ;

        V_r( &pentry->lock ) ;

        return *pstatus ;
      }

     piter_state = previous_pstate->next ; 
   }
  
   
   *ppstate = piter_state ;
 
   V_r( &pentry->lock ) ; 

  return *pstatus ;
} /* cache_inode_state_iterate */

