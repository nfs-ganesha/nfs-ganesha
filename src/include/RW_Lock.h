/*
 * RW_Lock.h
 *
 * This file contains the defintions of the functions and types for the RW lock management
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/RW_Lock/RW_Lock.h,v 1.1 2004/08/16 09:35:08 deniel Exp $
 *
 * $Log: RW_Lock.h,v $
 * Revision 1.1  2004/08/16 09:35:08  deniel
 * Population de la repository avec les Hashtables et les RW_Lock
 *
 * Revision 1.5  2003/12/19 16:31:52  deniel
 * Resolution du pb de deadlock avec des variables de conditions
 *
 * Revision 1.4  2003/12/19 13:18:57  deniel
 * Identification du pb de deadlock: on ne peux pas unlocker deux fois de suite
 *
 * Revision 1.3  2003/12/18 14:15:37  deniel
 * Correction d'un probleme lors de la declaration des init de threads
 *
 * Revision 1.1.1.1  2003/12/17 10:29:49  deniel
 * Recreation de la base 
 *
 *
 * Revision 1.1  2003/12/17 09:34:43  deniel
 * Header file added to the repository
 *
 *
 */

#ifndef _RW_LOCK_H
#define _RW_LOCK_H

#include <pthread.h>
#include "log_macros.h"

/* My habit with mutex */

#define P( mutex )                                                          \
  do { int rc ;                                                             \
    if( ( rc = pthread_mutex_lock( &mutex ) ) != 0 )                        \
      LogFullDebug(COMPONENT_RW_LOCK, "  --> Error P: %d %d", rc, errno );  \
  } while (0)

#define V( mutex )                                                          \
  do { int rc ;                                                             \
    if( ( rc = pthread_mutex_unlock( &mutex ) ) != 0 )                      \
      LogFullDebug(COMPONENT_RW_LOCK, "  --> Error V: %d %d", rc, errno );  \
  } while (0)

/* Type representing the lock itself */
typedef struct _RW_LOCK
{
  unsigned int nbr_active;
  unsigned int nbr_waiting;
  unsigned int nbw_active;
  unsigned int nbw_waiting;
  pthread_mutex_t mutexProtect;
  pthread_cond_t condWrite;
  pthread_cond_t condRead;
  pthread_mutex_t mcond;
} rw_lock_t;

int rw_lock_init(rw_lock_t * plock);
int rw_lock_destroy(rw_lock_t * plock);
int P_w(rw_lock_t * plock);
int V_w(rw_lock_t * plock);
int P_r(rw_lock_t * plock);
int V_r(rw_lock_t * plock);
int rw_lock_downgrade(rw_lock_t * plock);
int rw_lock_upgrade(rw_lock_t * plock);

#endif                          /* _RW_LOCK */
