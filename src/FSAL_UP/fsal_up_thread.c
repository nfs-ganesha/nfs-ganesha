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

static int fsal_up_thread_exists(exportlist_t *entry);
static struct glist_head       *fsal_up_process_queue;
nfs_tcb_t                      fsal_up_process_tcb;

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
      pthread_mutex_lock(&nfs_param.fsal_up_param.event_pool_lock);
      pool_free(nfs_param.fsal_up_param.event_pool, arg);
      pthread_mutex_unlock(&nfs_param.fsal_up_param.event_pool_lock);
      return ret;
    }

  if(arg->event_type == FSAL_UP_EVENT_INVALIDATE)
    {
      arg->event_process_func(&arg->event_data);
      /* Step2 where we perform close; which could be expensive operation
         so deffer it to the separate thread. */
      arg->event_process_func = dumb_fsal_up_invalidate_step2;
    }

  /* Now queue them for further process. */
  LogFullDebug(COMPONENT_FSAL_UP, "Schedule %p", arg);

  P(fsal_up_process_tcb.tcb_mutex);
  glist_add_tail(fsal_up_process_queue, &arg->event_list);
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

  if ((fsal_up_process_queue =
       gsh_calloc(1, sizeof (struct glist_head))) == NULL)
    {
       LogFatal(COMPONENT_INIT,
                "Error while allocating FSAL UP process queue.");
    }

  init_glist(fsal_up_process_queue);
  tcb_new(&fsal_up_process_tcb, "FSAL_UP Process Thread");

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
          glist_empty(fsal_up_process_queue))
        {
          while(1)
            {
              P(fsal_up_process_tcb.tcb_mutex);
              if ((fsal_up_process_tcb.tcb_state == STATE_AWAKE) &&
                  !glist_empty(fsal_up_process_queue))
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
                  if (glist_empty(fsal_up_process_queue))
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
        fupevent = glist_first_entry(fsal_up_process_queue,
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
            pthread_mutex_lock(&nfs_param.fsal_up_param.event_pool_lock);
            pool_free(nfs_param.fsal_up_param.event_pool, fupevent);
            pthread_mutex_unlock(&nfs_param.fsal_up_param.event_pool_lock);

            continue;
          }
        V(fsal_up_process_tcb.tcb_mutex);
    }
  tcb_remove(&fsal_up_process_tcb);
}

void create_fsal_up_threads()
{
  int rc, id;
  pthread_attr_t attr_thr;
  fsal_up_arg_t *fsal_up_args;
  exportlist_t *pcurrent;
  struct glist_head * glist;

  memset(&attr_thr, 0, sizeof(attr_thr));

  /* Initialization of thread attrinbutes borrowed from nfs_init.c */
  if(pthread_attr_init(&attr_thr) != 0)
    LogCrit(COMPONENT_THREAD, "can't init pthread's attributes");

  if(pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
    LogCrit(COMPONENT_THREAD, "can't set pthread's scope");

  if(pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE) != 0)
    LogCrit(COMPONENT_THREAD, "can't set pthread's join state");

  if(pthread_attr_setstacksize(&attr_thr, THREAD_STACK_SIZE) != 0)
    LogCrit(COMPONENT_THREAD, "can't set pthread's stack size");

  /* The admin thread is the only other thread that may be
   * messing around with the export entries. */

  //  LOCK EXPORT LIST
  glist_for_each(glist, nfs_param.pexportlist)
    {
      pcurrent = glist_entry(glist, exportlist_t, exp_list);

      if (pcurrent->use_fsal_up == FALSE)
        continue;

      /* Make sure there are not multiple fsal_up_threads handling multiple
       * exports on the same filesystem. This could potentially cause issues. */
      LogMidDebug(COMPONENT_INIT, "Checking if export id %d with filesystem "
               "id %llu.%llu already has an assigned FSAL_UP thread.",
               pcurrent->id, pcurrent->filesystem_id.major,
               pcurrent->filesystem_id.minor);

      id = fsal_up_thread_exists(pcurrent);
      if (id)
        {
          LogDebug(COMPONENT_INIT, "Filesystem %llu.%llu already has an "
                   "assigned FSAL_UP with export id %d so export w/ id %d"
                   " is not being assigned a new FSAL_UP thread.",
                   pcurrent->filesystem_id.major,
                   pcurrent->filesystem_id.minor, id, pcurrent->id);
          continue;
        }
      else
        {
          LogDebug(COMPONENT_INIT, "Filesystem %llu.%llu export id %d does not"
                   " have an FSAL_UP thread yet, creating a thread now.",
                   pcurrent->filesystem_id.major, pcurrent->filesystem_id.minor,
                   pcurrent->id);

          if((fsal_up_args =
              gsh_calloc(1, sizeof(fsal_up_arg_t))) == NULL)
            {
              LogCrit(COMPONENT_INIT,
                      "Error while allocating FSAL UP args.");
              Fatal();
            }

          fsal_up_args->export_entry = pcurrent;

          if( ( rc = pthread_create( &pcurrent->fsal_up_thr, &attr_thr,
                                     fsal_up_thread,(void *)fsal_up_args)) != 0)
            {
              gsh_free(fsal_up_args);
              LogFatal(COMPONENT_THREAD,
                       "Could not create fsal_up_thread, error = %d (%s)",
                       errno, strerror(errno));
              Fatal();
            }
        }
    }
}

/* Given to pool_init() to be used as a constructor of
 * preallocated memory */
void constructor_fsal_up_event_t(void *ptr,
                                 void *parameter)
{
  return;
}

/* One pool can be used for all FSAL_UP used for exports. */
void nfs_Init_FSAL_UP()
{
  memset(&nfs_param.fsal_up_param, 0, sizeof(nfs_param.fsal_up_param));

  /* DEBUGGING */
  LogFullDebug(COMPONENT_INIT,
               "FSAL_UP: Initializing FSAL UP data pool");
  /* Allocation of the FSAL UP pool */
  nfs_param.fsal_up_param.event_pool
       = pool_init("FSAL UP Data Pool", sizeof(fsal_up_event_t),
                   pool_basic_substrate, NULL,
                   constructor_fsal_up_event_t, NULL);
  if(!(nfs_param.fsal_up_param.event_pool))
    {
      LogCrit(COMPONENT_INIT,
              "Error while allocating FSAL UP event pool");
      Fatal();
    }

  if(pthread_mutex_init(&nfs_param.fsal_up_param.event_pool_lock, NULL) != 0)
      LogCrit(COMPONENT_FSAL_UP, "FSAL_UP: Could not initialize event pool"
              " mutex.");

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

      pthread_mutex_lock(&nfs_param.fsal_up_param.event_pool_lock);
      pool_free(nfs_param.fsal_up_param.event_pool, myevent);
      pthread_mutex_unlock(&nfs_param.fsal_up_param.event_pool_lock);

      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  status = schedule_fsal_up_event_process(myevent);
  return status;
}

/* The admin exportlist must be locked before calling this function!! */
static int fsal_up_thread_exists(exportlist_t *entry)
{
  exportlist_t *pcurrent;
  struct glist_head * glist;
  pthread_t zeros;
  memset(&zeros, 0, sizeof(zeros));

  /* Loop through all export entries and if any have the same
   * filesystem id, assume they really do export a directory
   * from the same filesystem. In this case, check if there is
   * already an FSAL UP thread running. If there is, return TRUE
   * so we don't create multiple threads. */
  glist_for_each(glist, nfs_param.pexportlist)
    {
      pcurrent = glist_entry(glist, exportlist_t, exp_list);

      if (pcurrent == entry)
        continue;

      if (pcurrent->use_fsal_up == FALSE)
        continue;

      /* Only check if major is different */
      if (pcurrent->filesystem_id.major != entry->filesystem_id.major)
        continue;

      /* Is this the right wayt to check if a pthread reference is valid? */
      if (memcmp(&pcurrent->fsal_up_thr, &zeros, sizeof(pthread_t)) != 0)
        return pcurrent->id; /* Meaning a thread exists, so exit this thread.*/
      else
        continue;
    }
 return 0; /* Meaning no there weren't other threads and we set ours. */
}

fsal_up_event_functions_t *get_fsal_up_functions(char *fsal_up_type)
{
  if (strncmp(fsal_up_type, FSAL_UP_DUMB_TYPE, sizeof(FSAL_UP_DUMB_TYPE)) == 0)
    {
      LogEvent(COMPONENT_FSAL_UP, "Using the FSAL UP DUMB functions to handle"
               " FSAL UP events.");
      return get_fsal_up_dumb_functions();
    }
  else
    {
      return NULL;
    }
}

fsal_up_event_bus_filter_t *find_filter(char filtername[MAX_FILTER_NAMELEN])
{
  return NULL;
}

void *fsal_up_thread(void *Arg)
{
  fsal_status_t status;
  fsal_up_arg_t *fsal_up_args = (fsal_up_arg_t *)Arg;
  fsal_up_event_bus_context_t fsal_up_context;
  fsal_up_event_bus_parameter_t fsal_up_bus_param;
  fsal_up_event_bus_filter_t * pupebfilter = NULL;
  fsal_up_filter_list_t *filter = NULL;
  fsal_up_event_t *event;
  struct glist_head pevent_head;
  fsal_up_event_functions_t *event_func;
  fsal_count_t nb_events_found, event_nb;
  fsal_time_t timeout;
  char thr_name[40];

  memset(&fsal_up_bus_param, 0, sizeof(fsal_up_event_bus_parameter_t));
  memset(&fsal_up_context, 0, sizeof(fsal_up_event_bus_context_t));

  snprintf(thr_name, sizeof(thr_name), "FSAL UP Thread for filesystem %llu.%llu",
           fsal_up_args->export_entry->filesystem_id.major,
           fsal_up_args->export_entry->filesystem_id.minor);
  SetNameFunction(thr_name);

  /* Set the FSAL UP functions that will be used to process events. */
  event_func = get_fsal_up_functions(fsal_up_args->export_entry->fsal_up_type);
  if (event_func == NULL)
    {
      LogCrit(COMPONENT_FSAL_UP, "Error: FSAL UP TYPE: %s does not exist. "
              "Exiting FSAL UP thread.", fsal_up_args->export_entry->fsal_up_type);
      gsh_free(Arg);
      return NULL;
    }

  /* Get fsal up context from FSAL */
  /* It is expected that the export entry and event_pool will be referenced
   * in the returned callback context structure. */
  memcpy(&fsal_up_context.FS_export_context,
         &fsal_up_args->export_entry->FS_export_context,
         sizeof(fsal_export_context_t));

  fsal_up_context.event_pool = nfs_param.fsal_up_param.event_pool;
  fsal_up_context.event_pool_lock = &nfs_param.fsal_up_param.event_pool_lock;

  LogFullDebug(COMPONENT_FSAL_UP, "Initializing FSAL Callback context.");
  status = FSAL_UP_Init(&fsal_up_bus_param, &fsal_up_context);
  if (FSAL_IS_ERROR(status))
    {
      LogCrit(COMPONENT_FSAL_UP, "Error: Could not initialize FSAL UP for"
              " filesystem %llu.%llu export %d. Exiting FSAL UP thread.",
              fsal_up_args->export_entry->filesystem_id.major,
              fsal_up_args->export_entry->filesystem_id.minor,
              fsal_up_args->export_entry->id);
      gsh_free(Arg);
      return NULL;
    }

  /* Add filters ... later if needed we could add arguments to filters
   * configurable from configuration files. */
  for(filter = fsal_up_args->export_entry->fsal_up_filter_list;
      filter != NULL; filter = filter->next)
    {
      LogMidDebug(COMPONENT_FSAL_UP, "Applying filter \"%s\" to FSAL UP thread "
                  "for filesystem id %llu.%llu export id %d.", filter->name,
                  fsal_up_args->export_entry->filesystem_id.major,
                  fsal_up_args->export_entry->filesystem_id.minor,
                  fsal_up_args->export_entry->id);

      /* Find predefined filter */
      pupebfilter = find_filter(filter->name);
      if (pupebfilter == NULL)
        {
          LogCrit(COMPONENT_FSAL_UP, "Error: Could not find filter named \"%s\".",
                   filter->name);
        }

      else
        {
          /* Applying filter */
          FSAL_UP_AddFilter(pupebfilter, &fsal_up_context);
        }
    }

  /* Set the timeout for getting events. */
  timeout = fsal_up_args->export_entry->fsal_up_timeout;

  /* Start querying for events and processing. */
  while(1)
    {
      /* pevent is passed in as a single empty node, it's expected the
       * FSAL will use the event_pool in the bus_context to populate
       * this array by adding to the pevent_head. */
      event_nb = 0;
      nb_events_found = 0;
      init_glist(&pevent_head);
      LogFullDebug(COMPONENT_FSAL_UP,
                   "Requesting event from FSAL Callback interface.");
      status = FSAL_UP_GetEvents(&pevent_head,     /* out */
                                 &event_nb,        /* in/out */
                                 timeout,          /* in */
                                 &nb_events_found, /* out */
                                 &fsal_up_context);/* in */
      if (FSAL_IS_ERROR(status))
        {
          if (status.major == ERR_FSAL_TIMEOUT)
            LogDebug(COMPONENT_FSAL_UP, "FSAL_UP_EB_GetEvents() hit the timeout"
                     " limit of %u.%u seconds for filesystem id %llu.%llu export id"
                     " %d.", timeout.seconds, timeout.nseconds,
                     fsal_up_args->export_entry->filesystem_id.major,
                     fsal_up_args->export_entry->filesystem_id.minor,
                     fsal_up_args->export_entry->id);
          else if (status.major == ERR_FSAL_NOTSUPP)
            {
              LogCrit(COMPONENT_FSAL_UP, "Exiting FSAL UP Thread for filesystem"
                      " id %llu.%llu export id %u because the FSAL Callback"
                      " Interface is not supported for this FSAL type.",
                      fsal_up_args->export_entry->filesystem_id.major,
                      fsal_up_args->export_entry->filesystem_id.minor,
                      fsal_up_args->export_entry->id);
              return NULL;
            }
          else if (status.major == ERR_FSAL_BAD_INIT)
            {
              LogCrit(COMPONENT_FSAL_UP, "Exiting FSAL UP Thread for filesystem"
                      " id %llu.%llu export id %u because the FSAL Callback"
                      " reports that the GPFS export has gone away.",
                      fsal_up_args->export_entry->filesystem_id.major,
                      fsal_up_args->export_entry->filesystem_id.minor,
                      fsal_up_args->export_entry->id);
              return NULL;
            }
          else
            {
              LogWarn(COMPONENT_FSAL_UP, "Error: FSAL_UP_EB_GetEvents() "
                       "failed");
              continue;
            }
        }

      LogMidDebug(COMPONENT_FSAL_UP,
                  "Received %lu events to process for filesystem"
                  " id %llu.%llu export id %u.",
                  event_nb,
                  fsal_up_args->export_entry->filesystem_id.major,
                  fsal_up_args->export_entry->filesystem_id.minor,
                  fsal_up_args->export_entry->id);

      /* process the list of events */
      while(!glist_empty(&pevent_head))
        {
          event = glist_first_entry(&pevent_head, fsal_up_event_t, event_list);
          glist_del(&event->event_list);
          status = process_event(event, event_func);
          if (FSAL_IS_ERROR(status))
            {
              LogWarn(COMPONENT_FSAL_UP, "Error: Event could not be processed "
                       "for filesystem %llu.%llu export id %u.",
                       fsal_up_args->export_entry->filesystem_id.major,
                       fsal_up_args->export_entry->filesystem_id.minor,
                       fsal_up_args->export_entry->id);
            }
        }
    }

  gsh_free(Arg);
  return NULL;
}                               /* fsal_up_thread */

