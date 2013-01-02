/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file  state_async.c
 * @brief Management of SAL asynchronous processing
 */

#include "config.h"

#ifdef _SOLARIS
#include "solaris_port.h"
#endif /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#include "log.h"
#include "HashTable.h"
#include "fsal.h"
#include "sal_functions.h"
#include "nfs_tcb.h"

/**
 * @brief Thread ID of SAL async handler
 */
static pthread_t state_async_thread_id;

/**
 * @brief SAL asynchronous operation queue
 */
static struct glist_head state_async_queue;

/**
 * @brief Thread control block for SAL asynchronous handler thread
 */
nfs_tcb_t state_async_tcb;

/**
 * @brief Execute functions from the async queue
 *
 * @param[in] UnusedArg Unused
 *
 * @return NULL.
 */
void *state_async_thread(void *UnusedArg)
{
  state_async_queue_t * entry;
  struct timeval        now;
  struct timespec       timeout;
  state_block_data_t  * pblock;
  struct req_op_context req_ctx;

  SetNameFunction("state_async_thread");

  if(mark_thread_existing(&state_async_tcb) == PAUSE_EXIT)
    {
      /* Oops, that didn't last long... exit. */
      mark_thread_done(&state_async_tcb);
      LogDebug(COMPONENT_STATE,
               "State Async Thread: exiting before initialization");
      return NULL;
    }
  LogFullDebug(COMPONENT_STATE,
               "State Async Thread: my pthread id is %p",
               (caddr_t) pthread_self());

  while(1)
    {
      /* Check without tcb lock*/
      if((state_async_tcb.tcb_state != STATE_AWAKE) ||
          (glist_empty(&state_async_queue) &&
           glist_empty(&state_notified_locks)))
        {
          while(1)
            {
              P(state_async_tcb.tcb_mutex);
              if((state_async_tcb.tcb_state == STATE_AWAKE) &&
                  (!glist_empty(&state_async_queue) ||
                   !glist_empty(&state_notified_locks)))
                {
                  V(state_async_tcb.tcb_mutex);
                  break;
                }
              switch(thread_sm_locked(&state_async_tcb))
                {
                  case THREAD_SM_RECHECK:
                    V(state_async_tcb.tcb_mutex);
                    continue;

                  case THREAD_SM_BREAK:
                    if(glist_empty(&state_async_queue))
                      {
                        gettimeofday(&now, NULL);
                        timeout.tv_sec = 10 + now.tv_sec;
                        timeout.tv_nsec = 0;
                        pthread_cond_timedwait(&state_async_tcb.tcb_condvar,
                                               &state_async_tcb.tcb_mutex,
                                               &timeout);
                      }
                    V(state_async_tcb.tcb_mutex);
                    continue;

                  case THREAD_SM_EXIT:
                    V(state_async_tcb.tcb_mutex);
                    return NULL;
                }
            }
        }

      /* Check for async blocking lock notifications first */
      P(blocked_locks_mutex);

      /* Handle one async blocking lock notification and loop back */
      pblock = glist_first_entry(&state_notified_locks,
                                 state_block_data_t,
                                 sbd_list);

      if(pblock != NULL)
        {
          /* Pull block off list */
          glist_del(&pblock->sbd_list);

          /* Block is off list, no need to hold mutex any more */
          V(blocked_locks_mutex);

/** @TODO@ this is obviously wrong.  we need to fill in the context
 *  from somewhere!
 */
	  memset(&req_ctx, 0, sizeof(req_ctx));
          process_blocked_lock_upcall(pblock, &req_ctx);

          continue;
        }

      V(blocked_locks_mutex);

      /* Process one request if available */
      P(state_async_tcb.tcb_mutex);

      entry = glist_first_entry(&state_async_queue,
                                state_async_queue_t,
                                state_async_glist);
      if(entry != NULL)
        {
          /* Pull entry off list */
          glist_del(&entry->state_async_glist);

          /* Entry is off list, no need to hold mutex any more */
          V(state_async_tcb.tcb_mutex);

          /* Process async queue entry */
          entry->state_async_func(entry, &req_ctx);

          continue;
        }

      V(state_async_tcb.tcb_mutex);
    }
  tcb_remove(&state_async_tcb);
}

/**
 * @brief Schedule an asynchronous action
 *
 * @param[in] arg Request to schedule
 *
 * @return State status.
 */
state_status_t state_async_schedule(state_async_queue_t *arg)
{
  int rc;

  LogFullDebug(COMPONENT_STATE, "Schedule %p", arg);

  P(state_async_tcb.tcb_mutex);

  glist_add_tail(&state_async_queue, &arg->state_async_glist);

  rc = pthread_cond_signal(&state_async_tcb.tcb_condvar);

  if(rc == -1)
    {
      LogFullDebug(COMPONENT_STATE,
                   "Unable to signal State Async Thread");
      glist_del(&arg->state_async_glist);
    }

  V(state_async_tcb.tcb_mutex);

  return rc != -1 ? STATE_SUCCESS : STATE_SIGNAL_ERROR;
}

/**
 * @brief Signal thread that work is available
 */
void signal_async_work(void)
{
  int rc;

  P(state_async_tcb.tcb_mutex);

  rc = pthread_cond_signal(&state_async_tcb.tcb_condvar);

  if(rc == -1)
    LogFatal(COMPONENT_STATE,
             "Unable to signal State Async Thread");

  V(state_async_tcb.tcb_mutex);
}

/**
 * @brief Initialize asynchronous request system
 *
 * @return State status.
 */
state_status_t state_async_init(void)
{
  init_glist(&state_async_queue);
  tcb_new(&state_async_tcb, "State Async Thread");
  return STATE_SUCCESS;
}

/**
 * @Start the asynchronous request thread
 */
void state_async_thread_start(void)
{
  if(pthread_create(&state_async_thread_id, NULL, state_async_thread, NULL) != 0)
    LogFatal(COMPONENT_STATE, "Could not start State Async Thread");
}
/** @} */
