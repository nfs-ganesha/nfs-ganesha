/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * RW_Lock.c
 *
 * This file contains the functions for the RW lock management
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/RW_Lock/RW_Lock.c,v 1.2 2004/08/16 12:49:09 deniel Exp $
 *
 * $Log: RW_Lock.c,v $
 * Revision 1.2  2004/08/16 12:49:09  deniel
 * Mise a plat ok pour HashTable et RWLock
 *
 * Revision 1.1  2004/08/16 09:35:08  deniel
 * Population de la repository avec les Hashtables et les RW_Lock
 *
 * Revision 1.1  2004/02/18 12:27:50  deniel
 * Ajout dans la repository des fichiers relatifs au RW_Lock et au tables de Hash
 *
 * Revision 1.5  2003/12/19 16:31:51  deniel
 * Resolution du pb de deadlock avec des variables de conditions
 *
 * Revision 1.4  2003/12/19 13:18:57  deniel
 * Identification du pb de deadlock: on ne peux pas unlocker deux fois de suite
 *
 * Revision 1.3  2003/12/18 14:15:37  deniel
 * Correction d'un probleme lors de la declaration des init de threads
 *
 * Revision 1.2  2003/12/17 13:43:32  deniel
 * Premiere version. Des problemes de deadlock
 *
 * Revision 1.1.1.1  2003/12/17 10:29:49  deniel
 * Recreation de la base 
 *
 *
 * Revision 1.1  2003/12/17 09:33:57  deniel
 * First version of the lock management functions
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "RW_Lock.h"

/*
 * Debugging function
 */
static void print_lock( char * s, rw_lock_t * plock )
{
#ifdef _DEBUG
  printf( "%s: id = %u:  Lock State: nbr_active = %d, nbr_waiting = %d, nbw_active = %d, nbw_waiting = %d\n", 
          s, (unsigned int)pthread_self(), plock->nbr_active, plock->nbr_waiting, plock->nbw_active, plock->nbw_waiting ) ;
#else
  return ;
#endif
} /* print_lock */

/* 
 * Take the lock for reading 
 */
int P_r( rw_lock_t * plock )
{
  P( plock->mutexProtect ) ;

  /* print_lock( "P_r.1", plock ) ; */

  plock->nbr_waiting ++ ;

  /* no new read lock is granted if writters are waiting or active */
  while( plock->nbw_active > 0  || plock->nbw_waiting > 0 )
        pthread_cond_wait( &(plock->condRead), &(plock->mutexProtect) );

  /* There is no active or waiting writters, readers can go ... */
  plock->nbr_waiting -- ;
  plock->nbr_active ++ ;

  V( plock->mutexProtect ) ;

  //print_lock( "P_r.end", plock ) ;

  return 0 ;
} /* P_r */

/*
 * Release the lock after reading 
 */
int V_r( rw_lock_t * plock )
{
  P( plock->mutexProtect ) ;

  //print_lock( "V_r.1", plock ) ;

  /* I am a reader that is no more active */
  plock->nbr_active -- ;
  
  /* I was the last active reader, and there are some waiting writters, I let one of them go */
  if( plock->nbr_active == 0 && plock->nbw_waiting > 0 )
    {
      //print_lock( "V_r.2 lecteur libere un redacteur", plock ) ;
      pthread_cond_signal( &plock->condWrite ) ;
    }

  //print_lock( "V_r.end", plock ) ;

  V( plock->mutexProtect ) ;
  
  return 0 ;
} /* V_r */


/*
 * Take the lock for writting 
 */
int P_w( rw_lock_t * plock )
{
  P( plock->mutexProtect ) ;

  //print_lock( "P_w.1", plock ) ;

  plock->nbw_waiting ++ ;

  /* nobody must be active obtain exclusive lock */
  while( plock->nbr_active > 0 || plock->nbw_active > 0 ) 
        pthread_cond_wait( &plock->condWrite, &plock->mutexProtect );
        
  /* I become active and no more waiting */ 
  plock->nbw_waiting -- ;
  plock->nbw_active  ++ ;

  V( plock->mutexProtect ) ;

  //print_lock( "P_w.end", plock ) ;
  return 0 ;
} /* P_w */

/*
 * Release the lock after writting 
 */
int V_w( rw_lock_t * plock )
{
  P( plock->mutexProtect ) ;

  //print_lock( "V_w.1", plock ) ;

  /* I was the active writter, I am not it any more */
  plock->nbw_active -- ;

  if( plock->nbr_waiting > 0 )
    {
      /* if readers are waiting, let them go */
      //print_lock( "V_w.2 redacteur libere les lecteurs", plock ) ;
      pthread_cond_broadcast( &(plock->condRead) ) ;

      //print_lock( "V_w.3", plock ) ;

    }
  else if ( plock->nbw_waiting > 0 )
    {

      //print_lock( "V_w.4 redacteur libere un lecteur", plock ) ;

      /* There are waiting writters, but no waiting readers, I let a writter go */
      pthread_cond_signal( &(plock->condWrite) ) ;

      //print_lock( "V_w.5", plock ) ;

    }
  V( plock->mutexProtect ) ;


  //print_lock( "V_w.end", plock ) ;

  return 0 ;
} /* V_w */

/* Roughly, downgrading a writer lock is making a V_w atomically followed by a P_r */
int rw_lock_downgrade( rw_lock_t * plock )
{
  P( plock->mutexProtect ) ;

  /* I was the active writter, I am not it any more */
  plock->nbw_active -- ;

  if( plock->nbr_waiting > 0 )
    {

      /* there are waiting readers, I let all the readers go */
      pthread_cond_broadcast( &(plock->condRead) ) ;

    }

  /* nobody must break caller's read lock, so don't consider or unlock writers */

  /* caller is also a reader, now */
  plock->nbr_active ++;

  V( plock->mutexProtect );

} /* rw_lock_downgrade */

/*
 * Routine for initializing a lock
 */
int rw_lock_init( rw_lock_t * plock )
{ 
  int rc = 0 ;
  pthread_mutexattr_t mutex_attr ;
  pthread_condattr_t cond_attr ;
  
  if( ( rc = pthread_mutexattr_init( &mutex_attr ) != 0 ) ) return 1 ;
  if( ( rc = pthread_condattr_init( &cond_attr ) != 0 ) ) return 1 ;
  
  if( ( rc = pthread_mutex_init( &(plock->mutexProtect), &mutex_attr ) ) != 0 ) return 1 ;

  if( ( rc = pthread_cond_init( &(plock->condRead), &cond_attr ) ) != 0 ) return 1 ;
  if( ( rc = pthread_cond_init( &(plock->condWrite), &cond_attr ) ) != 0 ) return 1 ;
  
  plock->nbr_waiting = 0 ;
  plock->nbr_active = 0 ;

  plock->nbw_waiting = 0 ;  
  plock->nbw_active = 0 ;

  return 0 ;
} /* rw_lock_init */

/*
 * Routine for destroying a lock
 */
int rw_lock_destroy( rw_lock_t * plock )
{ 
  int rc = 0 ;
  
  
  if( ( rc = pthread_mutex_destroy( &(plock->mutexProtect) ) ) != 0 ) return 1 ;

  if( ( rc = pthread_cond_destroy( &(plock->condWrite) ) ) != 0 ) return 1 ;
  if( ( rc = pthread_cond_destroy( &(plock->condRead) ) ) != 0 ) return 1 ;

  memset( plock, 0, sizeof( rw_lock_t ) );
  
  return 0 ;
} /* rw_lock_init */


  
  
  
