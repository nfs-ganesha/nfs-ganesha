/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Venkateswararao Jujjuri   jujjuri@gmail.com
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
 * \file    nfs_tcb.c
 * \author  $Author: leibovic $
 * \brief   The file that contain thread control block related code
 *
 * nfs_tcb.c : The file that contain thread control block related code
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
#include "nfs_core.h"
#include "nfs_tcb.h"
#include "nlm_list.h"


pthread_mutex_t   tcb_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct glist_head tcb_head;

pthread_mutex_t active_workers_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  active_workers_cond  = PTHREAD_COND_INITIALIZER;
unsigned int    num_active_workers   = 0;
unsigned int    num_existing_workers = 0;
awaken_reason_t awaken_reason        = AWAKEN_STARTUP;
int             num_pauses           = 0; /* Number of things trying to pause - do not awaken until this goes to 0 */
pause_state_t   pause_state          = STATE_STARTUP;

const char *pause_reason_str[] =
{
  "PAUSE_RELOAD_EXPORTS",
  "PAUSE_SHUTDOWN",
};

const char *awaken_reason_str[] =
{
  "AWAKEN_STARTUP",
  "AWAKEN_RELOAD_EXPORTS",
};

const char *pause_state_str[] =
{
  "STATE_STARTUP",
  "STATE_AWAKEN",
  "STATE_AWAKE",
  "STATE_PAUSE",
  "STATE_PAUSED",
  "STATE_EXIT",
};

const char *pause_rc_str[] =
{
  "PAUSE_OK",
  "PAUSE_AWAKE",
  "PAUSE_PAUSE",
  "PAUSE_EXIT",
};

void tcb_head_init(void)
{
  init_glist(&tcb_head);
}

void tcb_insert(nfs_tcb_t *element)
{
  P(tcb_mutex);
  glist_add_tail(&tcb_head, &element->tcb_list);
  V(tcb_mutex);
}

void tcb_remove(nfs_tcb_t *element)
{
  P(tcb_mutex);
  glist_del(&element->tcb_list);
  V(tcb_mutex);
}


/**
 * wait_for_workers_to_exit: Wait for workers to exit
 *
 */
void wait_for_workers_to_exit()
{
  struct timespec timeout;
  int rc;
  int t1, t2;
  unsigned int original_existing = num_existing_workers;

  LogDebug(COMPONENT_THREAD,
           "Waiting for worker threads to exit");

  t1 = time(NULL);
  while(num_existing_workers > 0)
    {
      LogDebug(COMPONENT_THREAD,
               "Waiting one second for threads to exit, still existing: %u",
               num_existing_workers);
      timeout.tv_sec  = time(NULL) + 1;
      timeout.tv_nsec = 0;
      rc = pthread_cond_timedwait(&active_workers_cond, &active_workers_mutex, &timeout);
    }
  t2 = time(NULL);
  LogInfo(COMPONENT_THREAD,
          "%u threads exited out of %u after %d seconds",
          original_existing - num_existing_workers,
          original_existing,
          t2 - t1);
}

/**
 * _wait_for_workers_to_pause: Wait for workers to become innactive
 *
 */
pause_rc _wait_for_workers_to_pause()
{
  struct timespec timeout;
  int rc;
  int t1, t2;

  LogDebug(COMPONENT_THREAD,
           "Waiting for worker threads to sleep");

  t1 = time(NULL);
  while(num_active_workers > 0)
    {
      /* If we are now trying to exit, just shortcut, let our caller deal with exiting. */
      if(pause_state == STATE_EXIT)
        {
          t2 = time(NULL);
          LogInfo(COMPONENT_THREAD,
                  "%u threads asleep of %u after %d seconds before interruption for shutdown",
                  nfs_param.core_param.nb_worker - num_active_workers,
                  nfs_param.core_param.nb_worker,
                  t2 - t1);
          return PAUSE_EXIT;
        }

      timeout.tv_sec  = time(NULL) + 1;
      timeout.tv_nsec = 0;
      rc = pthread_cond_timedwait(&active_workers_cond, &active_workers_mutex, &timeout);
    }
  t2 = time(NULL);
  LogInfo(COMPONENT_THREAD,
          "%u threads asleep out of %u after %d seconds",
          nfs_param.core_param.nb_worker - num_active_workers,
          nfs_param.core_param.nb_worker,
          t2 - t1);
  return PAUSE_OK;
}

pause_rc wait_for_workers_to_pause()
{
  pause_rc rc;

  P(active_workers_mutex);

  rc = _wait_for_workers_to_pause();

  V(active_workers_mutex);

  return rc;
}

/**
 * wait_for_workers_to_awaken: Wait for workers to become active
 *
 */
pause_rc _wait_for_workers_to_awaken()
{
  struct timespec timeout;
  int rc;
  int t1, t2;

  LogDebug(COMPONENT_THREAD,
           "Waiting for worker threads to awaken");

  t1 = time(NULL);
  while(num_active_workers < nfs_param.core_param.nb_worker)
    {
      /* If trying to exit, don't bother waiting */
      if(pause_state == STATE_EXIT)
        {
          t2 = time(NULL);
          LogInfo(COMPONENT_THREAD,
                  "%u threads awake of %u after %d seconds before interruption for shutdown",
                  num_active_workers,
                  nfs_param.core_param.nb_worker,
                  t2 - t1);
          return PAUSE_EXIT;
        }

       /* If trying to pause, don't bother waiting */
     if(pause_state == STATE_PAUSE || pause_state == STATE_PAUSED)
        {
          t2 = time(NULL);
          LogInfo(COMPONENT_THREAD,
                  "%u threads awake of %u after %d seconds before interruption for pause",
                  num_active_workers,
                  nfs_param.core_param.nb_worker,
                  t2 - t1);
          return PAUSE_PAUSE;
        }

      timeout.tv_sec  = time(NULL) + 10;
      timeout.tv_nsec = 0;
      rc = pthread_cond_timedwait(&active_workers_cond, &active_workers_mutex, &timeout);
    }
  t2 = time(NULL);
  LogInfo(COMPONENT_THREAD,
          "%u threads awake out of %u after %d seconds",
          num_active_workers, nfs_param.core_param.nb_worker, t2 - t1);
  return PAUSE_OK;
}

pause_rc wait_for_workers_to_awaken()
{
  pause_rc rc;

  P(active_workers_mutex);

  rc = _wait_for_workers_to_awaken();

  V(active_workers_mutex);

  return rc;
}

/**
 * mark_thread_asleep: Mark a thread as asleep
 *
 */
void mark_thread_asleep(nfs_worker_data_t *pmydata)
{
  P(active_workers_mutex);
  P(pmydata->wcb.tcb_mutex);

  if(pmydata->wcb.tcb_state == STATE_PAUSE)
    {
      pmydata->wcb.tcb_state = STATE_PAUSED;
      num_active_workers--;
      pmydata->wcb.tcb_ready = FALSE;
    }

  pthread_cond_signal(&active_workers_cond);
  LogDebug(COMPONENT_THREAD,
           "Worker thread #%u asleep",
           pmydata->worker_index);


  V(pmydata->wcb.tcb_mutex);
  V(active_workers_mutex);
}

/**
 * mark_thread_done: Mark a thread as done
 *
 */
void mark_thread_done(nfs_worker_data_t *pmydata)
{
  P(active_workers_mutex);
  P(pmydata->wcb.tcb_mutex);

  if(pmydata->wcb.tcb_ready)
    {
      num_active_workers--;
      pmydata->wcb.tcb_ready = FALSE;
    }

  num_existing_workers--;

  pthread_cond_signal(&active_workers_cond);
  LogDebug(COMPONENT_THREAD,
           "Worker thread #%u exiting",
           pmydata->worker_index);


  V(pmydata->wcb.tcb_mutex);
  V(active_workers_mutex);
  tcb_remove(&pmydata->wcb);
}

/**
 * mark_thread_exist: Mark a thread as existing
 *
 */
pause_rc mark_thread_existing(nfs_worker_data_t *pmydata)
{
  pause_rc rc;

  P(active_workers_mutex);
  P(pmydata->wcb.tcb_mutex);

  /* Increment count of existing (even if we are about to die,
   * mark_thread_done will be called in that case).
   */
  num_existing_workers++;

  if(pause_state == STATE_EXIT)
    rc = PAUSE_EXIT;
  else
    rc = PAUSE_OK;

  pthread_cond_signal(&active_workers_cond);
  LogDebug(COMPONENT_THREAD,
           "Worker thread #%u exists",
           pmydata->worker_index);

  V(pmydata->wcb.tcb_mutex);
  V(active_workers_mutex);

  return rc;
}

/**
 * mark_thread_awake: Mark a thread as awake
 *
 */
void mark_thread_awake(nfs_worker_data_t *pmydata)
{
  P(active_workers_mutex);
  P(pmydata->wcb.tcb_mutex);

  if(pmydata->wcb.tcb_state == STATE_STARTUP || pmydata->wcb.tcb_state == STATE_AWAKEN)
    {
      pmydata->wcb.tcb_state = STATE_AWAKE;
      num_active_workers++;
      pmydata->wcb.tcb_ready = TRUE;
    }

  pthread_cond_signal(&active_workers_cond);
  LogDebug(COMPONENT_THREAD,
           "Worker thread #%u active",
           pmydata->worker_index);

  V(pmydata->wcb.tcb_mutex);
  V(active_workers_mutex);
}

void notify_threads_of_new_state()
{
  unsigned int worker_index;
  for(worker_index = 0; worker_index  < nfs_param.core_param.nb_worker; worker_index++)
    {
      P(workers_data[worker_index].wcb.tcb_mutex);
      LogDebug(COMPONENT_THREAD,
               "Changing state of thread #%u from %s to %s",
               worker_index,
               pause_state_str[workers_data[worker_index].wcb.tcb_state],
               pause_state_str[pause_state]);
      workers_data[worker_index].wcb.tcb_state = pause_state;
      if(pthread_cond_signal(&(workers_data[worker_index].wcb.tcb_condvar)) == -1)
        {
          V(workers_data[worker_index].wcb.tcb_mutex);
          LogMajor(COMPONENT_THREAD,
                   "Error %d (%s) while signalling Worker Thread #%u... Exiting",
                   errno, strerror(errno), worker_index);
          Fatal();
        }
      V(workers_data[worker_index].wcb.tcb_mutex);
    }
}

/**
 * pause_workers: Pause the worker threads.
 *
 */
pause_rc pause_workers(pause_reason_t reason)
{
  pause_rc rc = PAUSE_OK;
  bool_t new_state = FALSE;
  bool_t wait = TRUE;

  P(active_workers_mutex);

  LogDebug(COMPONENT_THREAD,
           "Pause workers for reason: %s pause_state: %s",
           pause_reason_str[reason], pause_state_str[pause_state]);

  switch(reason)
    {
      case PAUSE_RELOAD_EXPORTS:
        num_pauses++;
        switch(pause_state)
          {
            case STATE_STARTUP:
              /* We need to wait for all threads to come up the first time
               * before we can think of trying to pause them.
               */
              rc = _wait_for_workers_to_awaken();
              if(rc != PAUSE_OK)
                {
                  LogDebug(COMPONENT_THREAD,
                           "pause workers for %s interrupted for shutdown",
                           pause_reason_str[reason]);
                  V(active_workers_mutex);
                  return rc;
                }
              /* fall through */

            case STATE_AWAKEN:
            case STATE_AWAKE:
              pause_state = STATE_PAUSE;
              new_state = TRUE;
              break;

            case STATE_PAUSE:
              break;

            case STATE_PAUSED:
              /* Already paused, nothing to do. */
              wait = FALSE;
              break;

            case STATE_EXIT:
              /* Ganesha is trying to exit, the caller should exit also */
              V(active_workers_mutex);
              return PAUSE_EXIT;
          }
        break;

      case PAUSE_SHUTDOWN:
        num_pauses++;
        if(pause_state == STATE_EXIT)
          {
            /* Already paused, nothing more to do. */
            wait = FALSE;
            rc = PAUSE_EXIT;
          }
        else
          {
            /* Otherwise don't care about current state, startup will handle need to exit. */
            pause_state = STATE_EXIT;
            new_state = TRUE;
          }
        break;
    }

  if(new_state)
    notify_threads_of_new_state();

  /* Wait for all worker threads to pause or exit */
  if(pause_state == STATE_EXIT && wait)
    {
      wait_for_workers_to_exit();
      rc = PAUSE_EXIT;
    }
  else if(wait)
    {
      rc = _wait_for_workers_to_pause();
      if(rc == PAUSE_OK && pause_state == STATE_PAUSE)
        pause_state = STATE_PAUSED;
    }

  V(active_workers_mutex);

  return rc;
}

/**
 * wake_workers: Wake up worker threads.
 *
 */
pause_rc wake_workers(awaken_reason_t reason)
{
  pause_rc rc;
  bool_t new_state = FALSE;
  bool_t wait = TRUE;

  P(active_workers_mutex);

  LogDebug(COMPONENT_THREAD,
           "Wake workers for reason: %s pause_state: %s",
           awaken_reason_str[reason], pause_state_str[pause_state]);

  switch(reason)
    {
      case AWAKEN_STARTUP:
        /* Just wait. */
        break;

      case AWAKEN_RELOAD_EXPORTS:
        num_pauses--;
        switch(pause_state)
          {
            case STATE_STARTUP:
            case STATE_AWAKEN:
              /* Already trying to awaken, just wait. */
              break;

            case STATE_PAUSE:
            case STATE_PAUSED:
              if(num_pauses != 0)
                {
                  /* Don't actually wake up yet. */
                  V(active_workers_mutex);
                  return PAUSE_PAUSE;
                }
              pause_state = STATE_AWAKEN;
              new_state = TRUE;
              break;

            case STATE_AWAKE:
              /* Already awake, nothing to do. */
              wait = FALSE;
              rc = PAUSE_OK;
              break;

            case STATE_EXIT:
              /* Ganesha is trying to exit, the caller should exit also */
              V(active_workers_mutex);
              return PAUSE_EXIT;
          }
    }

  if(new_state)
    notify_threads_of_new_state();

  /* Wait for all worker threads to wake up */
  if(wait)
    {
      rc = _wait_for_workers_to_awaken();
      if(rc == PAUSE_OK)
        pause_state = STATE_AWAKE;
    }

  V(active_workers_mutex);

  return rc;
}

