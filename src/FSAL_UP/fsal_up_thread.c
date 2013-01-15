/*
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
 * \file    fsal_up_thread.c
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
#include "nfs_core.h"
#include "log.h"
#include "fsal_up.h"
#include "err_fsal.h"
#include "nfs_tcb.h"

extern fsal_status_t dumb_fsal_up_invalidate_step2(fsal_up_event_data_t *);

static struct glist_head       fsal_up_process_queue;
nfs_tcb_t                      fsal_up_process_tcb;
pool_t                       * fsal_up_event_pool;

fsal_status_t  schedule_fsal_up_event_process(fsal_up_event_t *arg)
{
  int rc;
  fsal_status_t ret = {0, 0};

  /* Events which needs quick response, and locking events wich
     has its own queue gets processed here, rest will be queued. */
  if (arg->event_type == FSAL_UP_EVENT_LOCK_GRANT ||
      arg->event_type == FSAL_UP_EVENT_LOCK_AVAIL)
    {
      arg->event_process_func(&arg->event_data);

      gsh_free(arg->event_data.event_context.fsal_data.fh_desc.start);
      pool_free(fsal_up_event_pool, arg);
      return ret;
    }

  if(arg->event_type == FSAL_UP_EVENT_INVALIDATE)
    {
      arg->event_process_func(&arg->event_data);
      /* Step2 where we perform close; which could be expensive operation
         so deffer it to the separate thread. */
      arg->event_process_func = dumb_fsal_up_invalidate_step2;
    }
  if(arg->event_type == FSAL_UP_EVENT_UPDATE)
  {

   /* Invalidate first without scheduling */
    arg->event_process_func(&arg->event_data);

    if ((arg->event_data.type.update.upu_flags & FSAL_UP_NLINK) &&
        (arg->event_data.type.update.upu_stat_buf.st_nlink == 0) )
    {
      /* upu_flags must be set or st_nlink is unreliable. */
      /* Step2 where we perform close; which could be expensive operation
         so defer it to the separate thread. */
      arg->event_process_func = dumb_fsal_up_invalidate_step2;
    } else
            return ret;
  }

  /* Now queue them for further process. */
  LogFullDebug(COMPONENT_FSAL_UP, "Schedule %p", arg);

  P(fsal_up_process_tcb.tcb_mutex);
  glist_add_tail(&fsal_up_process_queue, &arg->event_list);
  rc = pthread_cond_signal(&fsal_up_process_tcb.tcb_condvar);
  LogFullDebug(COMPONENT_FSAL_UP,"Signaling tcb_condvar");
  if (rc == -1)
    {
      LogDebug(COMPONENT_FSAL_UP,
                   "Unable to signal FSAL_UP Process Thread");
      glist_del(&arg->event_list);
      ret.major = ERR_FSAL_FAULT;
    }
  V(fsal_up_process_tcb.tcb_mutex);
  return ret;
}

/* This thread processes FSAL UP events. */
void *fsal_up_process_thread(void *UnUsedArg)
{
  struct timeval             now;
  struct timespec            timeout;
  fsal_up_event_t          * fupevent;
  int                        rc;

  SetNameFunction("fsal_up_process_thread");

  if (mark_thread_existing(&fsal_up_process_tcb) == PAUSE_EXIT)
    {
      /* Oops, that didn't last long... exit. */
      mark_thread_done(&fsal_up_process_tcb);
      LogDebug(COMPONENT_INIT,
               "FSAL_UP Process Thread: Exiting before initialization");
      return NULL;
    }

  LogFullDebug(COMPONENT_INIT,
               "FSAL_UP Process Thread: my pthread id is %p",
               (caddr_t) pthread_self());

  while(1)
    {
      /* Check without tcb lock*/
      if ((fsal_up_process_tcb.tcb_state != STATE_AWAKE) ||
          glist_empty(&fsal_up_process_queue))
        {
          while(1)
            {
              P(fsal_up_process_tcb.tcb_mutex);
              if ((fsal_up_process_tcb.tcb_state == STATE_AWAKE) &&
                  !glist_empty(&fsal_up_process_queue))
                {
                  V(fsal_up_process_tcb.tcb_mutex);
                  break;
                }
              switch(thread_sm_locked(&fsal_up_process_tcb))
                {
                  case THREAD_SM_RECHECK:
                  V(fsal_up_process_tcb.tcb_mutex);
                  continue;

                  case THREAD_SM_BREAK:
                  if (glist_empty(&fsal_up_process_queue))
                    {
                      gettimeofday(&now, NULL);
                      timeout.tv_sec = 10 + now.tv_sec;
                      timeout.tv_nsec = 0;
                      rc = pthread_cond_timedwait(&fsal_up_process_tcb.tcb_condvar,
                                                  &fsal_up_process_tcb.tcb_mutex,
                                                  &timeout);
                    }
                  V(fsal_up_process_tcb.tcb_mutex);
                  continue;

                  case THREAD_SM_EXIT:
                  V(fsal_up_process_tcb.tcb_mutex);
                  return NULL;
                }
             }
          }
        P(fsal_up_process_tcb.tcb_mutex);
        fupevent = glist_first_entry(&fsal_up_process_queue,
                                     fsal_up_event_t,
                                     event_list);
        if(fupevent != NULL)
          {
            /* Pull the event off of the list */
            glist_del(&fupevent->event_list);

            /* Release the mutex */
            V(fsal_up_process_tcb.tcb_mutex);
            fupevent->event_process_func(&fupevent->event_data);
            gsh_free(fupevent->event_data.event_context.fsal_data.fh_desc.start);
            pool_free(fsal_up_event_pool, fupevent);

            continue;
          }
        V(fsal_up_process_tcb.tcb_mutex);
    }
  tcb_remove(&fsal_up_process_tcb);
}

/* One pool can be used for all FSAL_UP used for exports. */
void nfs_Init_FSAL_UP()
{
  /* DEBUGGING */
  LogFullDebug(COMPONENT_INIT,
               "FSAL_UP: Initializing FSAL UP data pool");
  /* Allocation of the FSAL UP pool */
  fsal_up_event_pool = pool_init("FSAL UP Data Pool",
                                 sizeof(fsal_up_event_t),
                                 pool_basic_substrate,
                                 NULL,
                                 NULL,
                                 NULL);
  if(fsal_up_event_pool == NULL)
    {
      LogFatal(COMPONENT_INIT,
               "Error while allocating FSAL UP event pool");
    }

  init_glist(&fsal_up_process_queue);
  tcb_new(&fsal_up_process_tcb, "FSAL_UP Process Thread");

  return;
}

fsal_status_t process_event(fsal_up_event_t *myevent, fsal_up_event_functions_t *event_func)
{
  fsal_status_t status = {0, 0};

  switch(myevent->event_type)
    {
    case FSAL_UP_EVENT_CREATE:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process CREATE event");
      myevent->event_process_func = event_func->fsal_up_create;
      break;
    case FSAL_UP_EVENT_UNLINK:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process UNLINK event");
      myevent->event_process_func = event_func->fsal_up_unlink;
      break;
    case FSAL_UP_EVENT_RENAME:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process RENAME event");
      myevent->event_process_func = event_func->fsal_up_rename;
      break;
    case FSAL_UP_EVENT_COMMIT:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process COMMIT event");
      myevent->event_process_func = event_func->fsal_up_commit;
      break;
    case FSAL_UP_EVENT_WRITE:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process WRITE event");
      myevent->event_process_func = event_func->fsal_up_write;
      break;
    case FSAL_UP_EVENT_LINK:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process LINK event");
      myevent->event_process_func = event_func->fsal_up_link;
      break;
    case FSAL_UP_EVENT_LOCK_GRANT:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process LOCK GRANT event");
      myevent->event_process_func = event_func->fsal_up_lock_grant;
      break;
    case FSAL_UP_EVENT_LOCK_AVAIL:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process LOCK AVAIL event");
      myevent->event_process_func = event_func->fsal_up_lock_avail;
      break;
    case FSAL_UP_EVENT_OPEN:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process OPEN event");
      myevent->event_process_func = event_func->fsal_up_open;
      break;
    case FSAL_UP_EVENT_CLOSE:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process CLOSE event");
      myevent->event_process_func = event_func->fsal_up_close;
      break;
    case FSAL_UP_EVENT_SETATTR:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process SETATTR event");
      myevent->event_process_func = event_func->fsal_up_setattr;
      break;
    case FSAL_UP_EVENT_UPDATE:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process UPDATE event");
      myevent->event_process_func = event_func->fsal_up_update;
      break;
    case FSAL_UP_EVENT_INVALIDATE:
      LogFullDebug(COMPONENT_FSAL_UP, "FSAL_UP: Process INVALIDATE event");
      myevent->event_process_func = event_func->fsal_up_invalidate;
      break;
    default:
      LogWarn(COMPONENT_FSAL_UP, "Unknown FSAL UP event type found: %d",
              myevent->event_type);
      gsh_free(myevent->event_data.event_context.fsal_data.fh_desc.start);

      pool_free(fsal_up_event_pool, myevent);

      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  status = schedule_fsal_up_event_process(myevent);
  return status;
}
