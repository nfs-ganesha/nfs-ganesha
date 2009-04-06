/*
 *
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
 * \file    mfsl_async_synclet.c
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"
#include "LRU_List.h"
#include "stuff_alloc.h"

#ifndef _USE_SWIG


pthread_t   mfsl_async_atd_thrid ;
pthread_t * mfsl_async_synclet_thrid ;

int once = 0 ;

extern mfsl_synclet_data_t  * synclet_data ;
extern mfsl_parameter_t       mfsl_param ;
extern unsigned int       end_of_mfsl ;

LRU_list_t         *  async_op_lru ;
pthread_mutex_t       mutex_async_list ;

/**
 *
 * MFSL_async_post: posts an asynchronous operation to the pending operations list.
 *
 * Posts an asynchronous operation to the pending operations list.
 *
 * @param popdesc [IN]    the asynchronous operation descriptor
 *
 */
fsal_status_t  MFSL_async_post( mfsl_async_op_desc_t  * popdesc )
{
   LRU_entry_t   * plru_entry      = NULL ;
   LRU_status_t    lru_status ;
/* Do not use RPCBIND by default */
#define  _RPCB_PROT_H_RPCGEN

   P( mutex_async_list ) ;
  
   if( ( plru_entry = LRU_new_entry( async_op_lru, &lru_status ) ) == NULL )
     {
	DisplayLog( "Impossible to post async operation in LRU dispatch list" ) ;
        MFSL_return( ERR_FSAL_SERVERFAULT, (int)lru_status ) ;
     }

   plru_entry->buffdata.pdata = (caddr_t)popdesc ;
   plru_entry->buffdata.len   = sizeof( mfsl_async_op_desc_t ) ;

   V( mutex_async_list ) ;

   MFSL_return( ERR_FSAL_NO_ERROR, 0 ) ;
} /* MFSL_async_post */

/**
 *
 * mfsl_async_process_async_op: processes an asynchronous operation.
 *
 * Processes an asynchronous operation that was taken from a synclet's pending queue 
 *
 * @param popdesc [IN]    the asynchronous operation descriptor
 *
 * @return the related fsal_status 
 *
 */
fsal_status_t  mfsl_async_process_async_op( mfsl_async_op_desc_t  * pasyncopdesc )
{
  fsal_status_t        fsal_status ;
  mfsl_context_t     * pmfsl_context ;

  if( pasyncopdesc == NULL )
   {
 	MFSL_return( ERR_FSAL_INVAL, 0 ) ;
   }

  /* Calling the function from async op */
  DisplayLogLevel( NIV_DEBUG, "op_type=%u %s", pasyncopdesc->op_type, mfsl_async_op_name[pasyncopdesc->op_type] ) ;

  fsal_status = (pasyncopdesc->op_func)( pasyncopdesc ) ;

  if( FSAL_IS_ERROR( fsal_status ) )
   {
     return fsal_status ; 
   }

  /* Free the previously allocated structures */
  pmfsl_context = (mfsl_context_t *)pasyncopdesc->ptr_mfsl_context ;

  P( pmfsl_context->lock ) ;
  RELEASE_PREALLOC( pasyncopdesc, pmfsl_context->pool_async_op, next_alloc ) ;
  V( pmfsl_context->lock ) ;

  /* Regular exit */
  MFSL_return( ERR_FSAL_NO_ERROR, 0 ) ;
} /* cache_inode_process_async_op */

/**
 *
 * mfsl_async_choose_synclet: Choose the synclet that will receive an asynchornous op to manage
 *
 * @param (none)
 * 
 * @return the index for the synclet to be used.
 *
 */
static unsigned int mfsl_async_choose_synclet( void )
{
#define NO_VALUE_CHOOSEN  1000000
  unsigned int synclet_chosen = NO_VALUE_CHOOSEN;
  unsigned int min_number_pending = NO_VALUE_CHOOSEN ;

  unsigned int i;
  static unsigned int last ;
  unsigned int cpt = 0 ;

  do 
   {
    /* chose the smallest queue */

    for( i = (last +1)% mfsl_param.nb_synclet , cpt = 0 ; cpt <  mfsl_param.nb_synclet ; cpt ++ , i = ( i + 1 ) % mfsl_param.nb_synclet   )
    {
        /* Choose only fully initialized workers and that does not gc */

        if ( synclet_data[i].op_lru->nb_entry < min_number_pending )
        {
          synclet_chosen = i;
          min_number_pending = synclet_data[i].op_lru->nb_entry;
        }
    }

  } while( synclet_chosen == NO_VALUE_CHOOSEN );

  last = synclet_chosen ;

  return synclet_chosen;
} /* mfsl_async_choose_synclet */

/**
 * mfsl_async_synclet_refresher_thread: thread used for asynchronous cache inode management.
 *
 * This thread is used for managing asynchrous inode management
 *
 * @param IndexArg the index for the thread 
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void * mfsl_async_synclet_refresher_thread( void * Arg )
{
  int                         rc = 0 ;
  unsigned int                i = 0 ;
  fsal_status_t               fsal_status ;
  fsal_export_context_t       fsal_export_context ;
  fsal_op_context_t           fsal_context ;
  mfsl_precreated_object_t  * pooldirs  = NULL ;
  mfsl_precreated_object_t  * poolfiles = NULL ;

  SetNameFunction( "MFSL_ASYNC Context refresher"  ) ;

  if ( ( rc = BuddyInit( NULL )) != BUDDY_SUCCESS )
    {
      /* Failed init */
      DisplayLog( "Memory manager could not be initialized, exiting..." ) ;
      exit( 1 ) ;
    }
  DisplayLog( "Memory manager successfully initialized" ) ;

  /* Init FSAL root fsal_op_context */
  if( FSAL_IS_ERROR( FSAL_BuildExportContext( &fsal_export_context,
					      NULL, 
					      NULL ) ) ) 	
   {
      /* Failed init */
      DisplayLog( "MFSL Synclet context could not build export context, exiting..." ) ;
      exit( 1 ) ;
   }

  if( FSAL_IS_ERROR( FSAL_InitClientContext(  &fsal_context ) ) ) 
   {
      /* Failed init */
      DisplayLog( "MFSL Synclet context could not build thread context, exiting..." ) ;
      exit( 1 ) ;
   }

  if( FSAL_IS_ERROR( FSAL_GetClientContext( &fsal_context, 
					    &fsal_export_context,
                                            0,
                                            0,
 					    NULL,
					    0 ) ) )
   {
      /* Failed init */
      DisplayLog( "could not build client context, exiting..." ) ;
      exit( 1 ) ;
   }

  /* Showtime... */
  DisplayLog( "Started..." ) ;
} /* mfsl_async_synclet_refresher_thread */

/**
 * mfsl_async_synclet_thread: thread used for asynchronous cache inode management.
 *
 * This thread is used for managing asynchrous inode management
 *
 * @param IndexArg the index for the thread 
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void * mfsl_async_synclet_thread( void * Arg )
{
  long                           index = 0 ;
  char                           namestr[64] ;
  int                            rc = 0 ;
  fsal_status_t                  fsal_status ;
  fsal_export_context_t          fsal_export_context ;
  int                            found = FALSE ;
  LRU_entry_t                  * pentry ;
  mfsl_async_op_desc_t         * pasyncopdesc = NULL ; 

  index = (long)Arg ;
  sprintf( namestr, "MFSL_ASYNC Synclet #%ld", index ) ;
  SetNameFunction( namestr ) ;

  if ( ( rc = BuddyInit( NULL )) != BUDDY_SUCCESS )
    {
      /* Failed init */
      DisplayLog( "Memory manager could not be initialized, exiting..." ) ;
      exit( 1 ) ;
    }
  DisplayLog( "Memory manager successfully initialized" ) ;

  /* Init FSAL root fsal_op_context */
  if( FSAL_IS_ERROR( FSAL_BuildExportContext( &fsal_export_context,
					      NULL, 
					      NULL ) ) ) 	
   {
      /* Failed init */
      DisplayLog( "MFSL Synclet context could not build export context, exiting..." ) ;
      exit( 1 ) ;
   }

  if( FSAL_IS_ERROR( FSAL_InitClientContext(  &synclet_data[index].root_fsal_context ) ) ) 
   {
      /* Failed init */
      DisplayLog( "MFSL Synclet context could not build thread context, exiting..." ) ;
      exit( 1 ) ;
   }

  if( FSAL_IS_ERROR( FSAL_GetClientContext( &synclet_data[index].root_fsal_context, 
					    &fsal_export_context,
                                            0,
                                            0,
 					    NULL,
					    0 ) ) )
   {
      /* Failed init */
      DisplayLog( "MFSL Synclet context could not build client context, exiting..." ) ;
      exit( 1 ) ;
   }


  /* Init synclet context */
  if( FSAL_IS_ERROR ( MFSL_ASYNC_GetSyncletContext( &synclet_data[index].synclet_context, 
		              	                    &synclet_data[index].root_fsal_context ) ) )
   {
      /* Failed init */
      DisplayLog( "MFSL Synclet context could not be initialized, exiting..." ) ;
      exit( 1 ) ;
   }

  /* Showtime... */
  DisplayLog( "Started..." ) ;

  while( !end_of_mfsl  ) 
   {
    P( synclet_data[index].mutex_op_condvar ) ;
    while( synclet_data[index].op_lru->nb_entry == synclet_data[index].op_lru->nb_invalid )
       pthread_cond_wait( &(synclet_data[index].op_condvar), &(synclet_data[index].mutex_op_condvar) )  ;
    V( synclet_data[index].mutex_op_condvar ) ;

    found = FALSE ;
    P( synclet_data[index].mutex_op_lru ) ;
    for( pentry = synclet_data[index].op_lru->LRU ; pentry != NULL ; pentry = pentry->next )
        {
          if( pentry->valid_state == LRU_ENTRY_VALID )
            {
              found = TRUE ;
              break ;
            }
        }
    V( synclet_data[index].mutex_op_lru ) ;

    if( !found ) 
     {
        DisplayLogLevel( NIV_MAJOR, "Error : I have been awaken when no pending async operation is available" ) ;
        continue ; /* return to main loop */
     }

    /* Get the async op to be proceeded */
    pasyncopdesc = (mfsl_async_op_desc_t *)(pentry->buffdata.pdata ) ;

    DisplayLogLevel( NIV_DEBUG, "I will proceed with asyncop %p", pasyncopdesc ) ;

    /* Execute the async op */
    fsal_status = mfsl_async_process_async_op( pasyncopdesc ) ;

    /* Now proceed with LRU gc management. First step is to increment the passcounter */
    synclet_data[index].passcounter += 1 ;


    /* Finalize my making the LRU entry invalid so that it is garbagged later */
    P( synclet_data[index].mutex_op_lru ) ;
    if( LRU_invalidate( synclet_data[index].op_lru , pentry ) != LRU_LIST_SUCCESS )
      {
        DisplayLog( "Incoherency: released entry for asyncopdesc could not be tagged invalid") ;
      }
    V( synclet_data[index].mutex_op_lru ) ;

    /* Init synclet context */
    if( FSAL_IS_ERROR ( MFSL_ASYNC_RefreshSyncletContext( &synclet_data[index].synclet_context, 
    		              	                        &synclet_data[index].root_fsal_context ) ) )
     {
        /* Failed init */
        DisplayLog( "MFSL Synclet context could not be initialized, exiting..." ) ;
        exit( 1 ) ;
     }

    /* Put the invalid entries back to pool (they have been managed */
    if( synclet_data[index].passcounter > mfsl_param.nb_before_gc )
     {
 
	if( LRU_gc_invalid( synclet_data[index].op_lru, NULL ) != LRU_LIST_SUCCESS )
	  DisplayLog( "/!\\ : Could not gc on LRU list for pending asynchronous operations" ) ;

   	synclet_data[index].passcounter = 0 ;
     }
 
    } /* while( 1 ) */

  
    DisplayLog( "Terminated..." ) ;

    return NULL ;
} /* mfsl_async_synclet_thread */

/**
 * mfsl_async_asynchronous_dispatcher_thread: this thread will assign asynchronous operation to the synclets.
 *
 *
 * @param Arg (unused)
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void * mfsl_async_asynchronous_dispatcher_thread( void * Arg ) 
{
  int                            rc = 0 ;
  LRU_entry_t                  * pentry_dispatch = NULL ;
  LRU_entry_t                  * pentry_synclet = NULL ;
  LRU_status_t                   lru_status ;
  unsigned int                   chosen_synclet = 0 ;  
  unsigned int                   passcounter = 0 ;
  struct timeval                 current ;
  mfsl_async_op_desc_t         * pasyncopdesc = NULL ; 

  SetNameFunction( "MFSL_ASYNC ADT" ) ;

  if ( ( rc = BuddyInit( NULL )) != BUDDY_SUCCESS )
    {
      /* Failed init */
      DisplayLog( "Memory manager could not be initialized, exiting..." ) ;
      exit( 1 ) ;
    }
  DisplayLog( "Memory manager successfully initialized" ) ;


  /* Structure initialisation */
  if( ( async_op_lru = LRU_Init( mfsl_param.lru_param, &lru_status ) ) == NULL )
   {
     DisplayLog( "Could not init LRU List" ) ;
     exit( 1 ) ;
   }

  if( ( rc = pthread_mutex_init( &mutex_async_list, NULL ) ) != 0 )
     return NULL ;

  DisplayLog( "Started..." ) ;
  while( !end_of_mfsl )
   {
      /* Sleep for a while */
      usleep( 60000 ) ;

      // sleep( mfsl_param.adt_sleeptime ) ;
      if( gettimeofday( &current, NULL ) != 0 )
       {
           /* Could'not get time of day... Stopping, this may need a major failure */
           DisplayLog( " cannot get time of day..." ) ;
           continue ;
       }
  
   
      P( mutex_async_list ) ;
      for( pentry_dispatch = async_op_lru->LRU ; pentry_dispatch != NULL ; pentry_dispatch = pentry_dispatch->next )
        {
           /* Entries are in chronological order, they should be managed in the asynchronous window only */
	   if( pentry_dispatch->valid_state == LRU_ENTRY_VALID )
            {
                /* Get the async op to be proceeded */
                pasyncopdesc = (mfsl_async_op_desc_t *)(pentry_dispatch->buffdata.pdata ) ;

		/* Manage ops that are older than the duration of the asynchronous window */
                if( current.tv_sec - pasyncopdesc->op_time.tv_sec < mfsl_param.adt_sleeptime   )
                   break ;

		/* Choose a synclet to operate on */
                chosen_synclet =  mfsl_async_choose_synclet( ) ;

		/* Insert async op to this synclet's LRU */
                P( synclet_data[chosen_synclet].mutex_op_lru ) ;
                if( ( pentry_synclet = LRU_new_entry( synclet_data[chosen_synclet].op_lru, &lru_status ) ) == NULL )
                 {
	           DisplayLogLevel( NIV_CRIT, "Impossible to post async operation in LRU synclet list" ) ;
                   V( synclet_data[chosen_synclet].mutex_op_lru ) ;
		   continue ;
                 }

		DisplayLogLevel( NIV_DEBUG, "Asyncop %p is to be managed by synclet %u", pentry_dispatch->buffdata.pdata, chosen_synclet ) ;

		pentry_synclet->buffdata.pdata = pentry_dispatch->buffdata.pdata ;
		pentry_synclet->buffdata.len   = pentry_dispatch->buffdata.len ;
             
                V( synclet_data[chosen_synclet].mutex_op_lru ) ;

                P( synclet_data[chosen_synclet].mutex_op_condvar ) ;
                if( pthread_cond_signal( &(synclet_data[chosen_synclet].op_condvar) ) == -1 )
                   DisplayLogLevel( NIV_EVENT, "Error : pthread_cond_signal failed on condvar for synclect %u", chosen_synclet ) ; 
                V( synclet_data[chosen_synclet].mutex_op_condvar ) ;

		/* Invalidate the entry in dispatch list */
		LRU_invalidate( async_op_lru,  pentry_dispatch ) ;
            }
        }
      
       /* Increment the passcounter */
       passcounter += 1 ;

       /* Put the invalid entries back to pool (they have been managed */
       if( passcounter > mfsl_param.nb_before_gc )
        {
           if( LRU_gc_invalid( async_op_lru, NULL ) != LRU_LIST_SUCCESS )
             DisplayLog( "/!\\ : Could not gc on LRU list for not dispatched asynchronous operations" ) ;

           passcounter = 0 ;
         }


      V( mutex_async_list ) ;
   }

  DisplayLog( "Terminated..." ) ;

  /* Should never occur (neverending loop) */
  return NULL ;
} /* mfsl_async_asynchronous_dispatcher_thread */


#endif /* ! _USE_SWIG */


