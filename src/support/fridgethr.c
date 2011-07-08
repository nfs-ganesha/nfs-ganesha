/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    fridgethr.c
 * \brief   A small module for thread management.
 *
 * fridgethr.c : A small module for thread management.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "HashData.h"
#include "HashTable.h"
#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

static pthread_mutex_t fridge_mutex ;
static fridge_entry_t * fridge_content = NULL ;
static pthread_attr_t attr_thr ;

static void fridgethr_remove( fridge_entry_t * pfe ) 
{
   if( pfe == NULL )
	return ;

   P( fridge_mutex ) ;
 
   if( pfe->pprev != NULL ) pfe->pprev->pnext = pfe->pnext ;
   if( pfe->pnext != NULL ) pfe->pnext->pprev = pfe->pprev ;
 
   if( pfe->pnext == NULL && pfe->pprev == NULL ) /* Is the fridge empty ? */
     fridge_content = NULL ;  

   V( fridge_mutex ) ;

   Mem_Free( pfe ) ;

   return ;
 } /* fridgethr_remove */

int fridgethr_get( pthread_t * pthrid, void *(*thrfunc)(void*), void * thrarg )
{
  fridge_entry_t * pfe = NULL ;

  P( fridge_mutex ) ;
  if( fridge_content == NULL )
   {
    V( fridge_mutex ) ;
    return pthread_create( pthrid, &attr_thr, thrfunc, thrarg ) ;
   }

  pfe = fridge_content ;
  pfe->frozen = FALSE ;
  if( pfe->pprev != NULL )
      pfe->pprev->pnext = pfe->pnext ;

  if( pfe->pnext != NULL )
      pfe->pnext->pprev = pfe->pprev ;

  fridge_content = fridge_content->pprev ;

  pfe->arg = thrarg ;
  if( pthread_cond_signal( &pfe->condvar ) )
   {
     V( fridge_mutex ) ;
     return -1 ;
   }
    
  *pthrid = pfe->thrid ;
 
  V( fridge_mutex ) ;
  return 0 ;
} /* fridgethr_get */

fridge_entry_t * fridgethr_freeze( )
{
  fridge_entry_t * pfe = NULL ;
  struct timespec timeout ;
  struct timeval    tp;
  int rc = 0 ;


  if( ( rc = gettimeofday( &tp, NULL ) ) != 0 )
    return NULL ;

  /* Be careful : pthread_cond_timedwait take an *absolute* time as time specification, not a duration */ 
  timeout.tv_sec = tp.tv_sec + nfs_param.core_param.tcp_fridge_expiration_delay ;
  timeout.tv_nsec = 0 ; 

  if( ( pfe = (fridge_entry_t *)Mem_Alloc( sizeof( fridge_entry_t ) ) ) == NULL )
    return NULL ;

  pfe->thrid = pthread_self() ;
  pthread_mutex_init( &(pfe->condmutex), NULL ) ;
  pthread_cond_init( &(pfe->condvar), NULL ) ;
  pfe->pprev = NULL ; 
  pfe->pnext = NULL ; 
  pfe->frozen = TRUE ;

  P( fridge_mutex ) ;
  if( fridge_content == NULL )
   {
     pfe->pprev = NULL ;
     pfe->pnext = NULL ;
   }
  else
   {
     pfe->pprev = fridge_content ;
     pfe->pnext = NULL ;
     fridge_content->pnext = pfe ;
   }
  fridge_content = pfe ;
  V( fridge_mutex ) ;

  P( pfe->condmutex ) ;
  while( pfe->frozen == TRUE && rc == 0 ) 
    if( nfs_param.core_param.tcp_fridge_expiration_delay > 0 )
       rc = pthread_cond_timedwait( &pfe->condvar, &pfe->condmutex, &timeout ) ;
    else
       rc = pthread_cond_wait( &pfe->condvar, &pfe->condmutex ) ;

  if( rc == ETIMEDOUT )
    fridgethr_remove( pfe );  
  else
    V( pfe->condmutex ) ;

  return (rc == 0 )?pfe:NULL ; 
} /* fridgethr_freeze */

int fridgethr_init( )
{
  /* Spawns a new thread to handle the connection */
  pthread_attr_init(&attr_thr) ; 
  pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_DETACHED);      /* If not, the conn mgr will be "defunct" threads */

  fridge_content = NULL ;

  if( pthread_mutex_init( &fridge_mutex, NULL ) != 0 )
    return -1 ;

  return 0 ;
} /* fridgethr_init */
