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
 * \file    RW_Lock.c
 * \author  $Author: deniel $
 * \brief   This file contains the functions for the RW lock management
 *
 * RW_Lock.c : this file contains the functions for the RW lock management.
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
static void print_lock(char *s, rw_lock_t * plock)
{
  
  LogFullDebug(COMPONENT_RW_LOCK,
               "%s: id = %u:  Lock State: nbr_active = %d, nbr_waiting = %d, nbw_active = %d, nbw_waiting = %d",
               s, (unsigned int)pthread_self(), plock->nbr_active, plock->nbr_waiting,
               plock->nbw_active, plock->nbw_waiting);
}                               /* print_lock */

/* 
 * Take the lock for reading 
 */
int P_r(rw_lock_t * plock)
{
  P(plock->mutexProtect);

  print_lock("P_r.1", plock);

  plock->nbr_waiting++;

  /* no new read lock is granted if writters are waiting or active */
  if(plock->nbw_active > 0 || plock->nbw_waiting > 0)
    pthread_cond_wait(&(plock->condRead), &(plock->mutexProtect));

  /* There is no active or waiting writters, readers can go ... */
  plock->nbr_waiting--;
  plock->nbr_active++;

  V(plock->mutexProtect);

  print_lock("P_r.end", plock);

  return 0;
}                               /* P_r */

/*
 * Release the lock after reading 
 */
int V_r(rw_lock_t * plock)
{
  P(plock->mutexProtect);

  print_lock("V_r.1", plock);

  /* I am a reader that is no more active */
  plock->nbr_active--;

  /* I was the last active reader, and there are some waiting writters, I let one of them go */
  if(plock->nbr_active == 0 && plock->nbw_waiting > 0)
    {
      print_lock("V_r.2 lecteur libere un redacteur", plock);
      pthread_cond_signal(&plock->condWrite);
    }

  print_lock("V_r.end", plock);

  V(plock->mutexProtect);

  return 0;
}                               /* V_r */

/*
 * Take the lock for writting 
 */
int P_w(rw_lock_t * plock)
{
  P(plock->mutexProtect);

  print_lock("P_w.1", plock);

  plock->nbw_waiting++;

  /* nobody must be active obtain exclusive lock */
  while(plock->nbr_active > 0 || plock->nbw_active > 0)
    pthread_cond_wait(&plock->condWrite, &plock->mutexProtect);

  /* I become active and no more waiting */
  plock->nbw_waiting--;
  plock->nbw_active++;

  V(plock->mutexProtect);

  print_lock("P_w.end", plock);
  return 0;
}                               /* P_w */

/*
 * Release the lock after writting 
 */
int V_w(rw_lock_t * plock)
{
  P(plock->mutexProtect);

  print_lock("V_w.1", plock);

  /* I was the active writter, I am not it any more */
  plock->nbw_active--;

  if(plock->nbw_waiting > 0)
    {

      print_lock("V_w.4 redacteur libere un lecteur", plock);

      /* There are waiting writters, but no waiting readers, I let a writter go */
      pthread_cond_signal(&(plock->condWrite));

      print_lock("V_w.5", plock);

    }
  else if(plock->nbr_waiting > 0)
    {
      /* if readers are waiting, let them go */
      print_lock("V_w.2 redacteur libere les lecteurs", plock);
      pthread_cond_broadcast(&(plock->condRead));

      print_lock("V_w.3", plock);

    }
  V(plock->mutexProtect);

  print_lock("V_w.end", plock);

  return 0;
}                               /* V_w */

/* Roughly, downgrading a writer lock is making a V_w atomically followed by a P_r */
int rw_lock_downgrade(rw_lock_t * plock)
{
  P(plock->mutexProtect);

  print_lock("downgrade.1", plock);

  /* I was the active writter, I am not it any more */
  plock->nbw_active--;

  if(plock->nbr_waiting > 0)
    {

      /* there are waiting readers, I let all the readers go */
      print_lock("downgrade.2 libere les lecteurs", plock);
      pthread_cond_broadcast(&(plock->condRead));

    }

  /* nobody must break caller's read lock, so don't consider or unlock writers */

  /* caller is also a reader, now */
  plock->nbr_active++;

  V(plock->mutexProtect);

  print_lock("downgrade.end", plock);

  return 0;

}                               /* rw_lock_downgrade */

/*
 * Routine for initializing a lock
 */
int rw_lock_init(rw_lock_t * plock)
{
  int rc = 0;
  pthread_mutexattr_t mutex_attr;
  pthread_condattr_t cond_attr;

  if((rc = pthread_mutexattr_init(&mutex_attr) != 0))
    return 1;
  if((rc = pthread_condattr_init(&cond_attr) != 0))
    return 1;

  if((rc = pthread_mutex_init(&(plock->mutexProtect), &mutex_attr)) != 0)
    return 1;

  if((rc = pthread_cond_init(&(plock->condRead), &cond_attr)) != 0)
    return 1;
  if((rc = pthread_cond_init(&(plock->condWrite), &cond_attr)) != 0)
    return 1;

  plock->nbr_waiting = 0;
  plock->nbr_active = 0;

  plock->nbw_waiting = 0;
  plock->nbw_active = 0;

  return 0;
}                               /* rw_lock_init */

/*
 * Routine for destroying a lock
 */
int rw_lock_destroy(rw_lock_t * plock)
{
  int rc = 0;

  if((rc = pthread_mutex_destroy(&(plock->mutexProtect))) != 0)
    return 1;

  if((rc = pthread_cond_destroy(&(plock->condWrite))) != 0)
    return 1;
  if((rc = pthread_cond_destroy(&(plock->condRead))) != 0)
    return 1;

  memset(plock, 0, sizeof(rw_lock_t));

  return 0;
}                               /* rw_lock_init */
