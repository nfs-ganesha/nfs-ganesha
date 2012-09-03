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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    fridgethr.c
 * \brief   A small pthread-based thread pool.
 *
 * fridgethr.c : A small pthread-based thread pool.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "fridgethr.h"

int
fridgethr_init(thr_fridge_t *fr)
{
    fr->nthreads = 0;
    fr->entry = NULL;
    fr->flags = FRIDGETHR_FLAG_NONE;

    pthread_attr_init(&fr->attr); 
    pthread_attr_setscope(&fr->attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setdetachstate(&fr->attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&fr->attr, fr->stacksize);

    if( pthread_mutex_init( &fr->mtx, NULL ) != 0 )
        return (-1);

    return (0);
} /* fridgethr_init */


static void
fridgethr_remove(thr_fridge_t *fr, fridge_entry_t *pfe) 
{
    /* XXX why would pfe be NULL? */
    if (pfe == NULL)
        return;

    pthread_mutex_lock(&fr->mtx);
 
    if (pfe->prev != NULL)
        pfe->prev->next = pfe->next;
    if (pfe->next != NULL)
        pfe->next->prev = pfe->prev;

    /* Is the fridge empty? */
    if ((pfe->next == NULL) &&
        (pfe->prev == NULL))
        fr->entry = NULL ;

    pthread_mutex_unlock(&fr->mtx);

    gsh_free(pfe) ;

    return;
} /* fridgethr_remove */

int
fridgethr_get(thr_fridge_t *fr, pthread_t *id, void *(*func)(void*),
              void *arg)
{

    fridge_entry_t *pfe = NULL ;

    pthread_mutex_lock(&fr->mtx);

    if ( fr->entry == NULL ) {
        pthread_mutex_unlock(&fr->mtx);
        return (pthread_create (id, &fr->attr, func, arg));
    }

    pfe = fr->entry;
    pfe->frozen = FALSE;

    /* XXX we should be using a TAILQ or whatever here */
    if(pfe->prev != NULL)
        pfe->prev->next = pfe->next;
    if (pfe->next != NULL)
        pfe->next->prev = pfe->prev;

    fr->entry = fr->entry->prev;

    pfe->arg = arg ;
    if (pthread_cond_signal(&pfe->cv)) {
        pthread_mutex_unlock(&fr->mtx);
        return (-1);
    }

    *id = pfe->id ;
 
    pthread_mutex_unlock(&fr->mtx);

    return (0);
} /* fridgethr_get */

void *
fridgethr_freeze(thr_fridge_t *fr)
{
  fridge_entry_t *pfe = NULL;
  int rc = 0;
  void *arg = NULL;

  if ((pfe = gsh_malloc(sizeof(fridge_entry_t))) == NULL)
      return (NULL);

  if ((rc = gettimeofday(&pfe->tp, NULL)) != 0)
      return (NULL);

  /* Be careful : pthread_cond_timedwait take an *absolute* time as time
   * specification, not a duration */ 
  pfe->timeout.tv_sec = pfe->tp.tv_sec + fr->expiration_delay_s;
  pfe->timeout.tv_nsec = 0; 

  pfe->id = pthread_self();
  pthread_mutex_init(&pfe->mtx, NULL);
  pthread_cond_init(&pfe->cv, NULL);
  pfe->prev = NULL;
  pfe->next = NULL;
  pfe->frozen = TRUE;

  pthread_mutex_lock(&fr->mtx);
  if (fr->entry == NULL) {
      pfe->prev = NULL ;
      pfe->next = NULL ;
  } else {
      pfe->prev = fr->entry;
      pfe->next = NULL ;
      fr->entry->next = pfe ;
  }
  fr->entry = pfe ;
  pthread_mutex_unlock(&fr->mtx);

  pthread_mutex_lock(&pfe->mtx);
  while ((pfe->frozen == TRUE) &&
         (rc == 0)) { 
      if (fr->expiration_delay_s > 0 )
          rc = pthread_cond_timedwait(&pfe->cv, &pfe->mtx, &pfe->timeout );
      else
          rc = pthread_cond_wait(&pfe->cv, &pfe->mtx);
  }

  if( rc != ETIMEDOUT )
      arg = pfe->arg;

  fridgethr_remove(fr, pfe);  
  return (arg);
} /* fridgethr_freeze */
