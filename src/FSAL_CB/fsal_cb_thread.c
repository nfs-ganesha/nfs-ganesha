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
 * \file    fsal_cb_thread.c
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
#include "stuff_alloc.h"
#include "log_macros.h"
#include "fsal_cb.h"

void *fsal_cb_thread(void *Arg);
static int fsal_cb_thread_exists(exportlist_t *entry);

void create_fsal_cb_threads()
{
  int rc, id;
  pthread_attr_t attr_thr;
  fsal_cb_arg_t *fsal_cb_args;
  exportlist_t *pcurrent;

  /* Initialization of thread attrinbutes borrowed from nfs_init.c */
  if(pthread_attr_init(&attr_thr) != 0)
    LogDebug(COMPONENT_THREAD, "can't init pthread's attributes");

  if(pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's scope");

  if(pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's join state");

  if(pthread_attr_setstacksize(&attr_thr, THREAD_STACK_SIZE) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's stack size");

  /* The admin thread is the only other thread that may be
   * messing around with the export entries. */

  //  LOCK EXPORT LIST
  for(pcurrent = nfs_param.pexportlist;
      pcurrent != NULL;
      pcurrent = pcurrent->next)
    {
      if (pcurrent->use_fsal_cb == FALSE)
        continue;

      /* Make sure there are not multiple fsal_cb_threads handling multiple
       * exports on the same filesystem. This could potentially cause issues. */
      LogEvent(COMPONENT_INIT, "Checking if export id %d with filesystem "
               "id %llu.%llu already has an assigned FSAL_CB thread.",
               pcurrent->fsalid, pcurrent->filesystem_id.major,
               pcurrent->filesystem_id.minor);
      
      id = fsal_cb_thread_exists(pcurrent);
      if (id)
        {
          LogEvent(COMPONENT_INIT, "Filesystem %llu.%llu already has an "
                   "assigned FSAL_CB with export id %d so export w/ id %d"
                   " is not being assigned a new FSAL_CB thread.",
                   pcurrent->filesystem_id.major,
                   pcurrent->filesystem_id.minor, id, pcurrent->id);
          continue;
        }
      else
        {
          LogEvent(COMPONENT_INIT, "Filesystem %llu.%llu export id %d does not"
                   " have an FSAL_CB thread yet, creating a thread now.",
                   pcurrent->filesystem_id.major, pcurrent->filesystem_id.minor,
                   pcurrent->id);

          if((fsal_cb_args =
              (fsal_cb_arg_t *) Mem_Alloc(sizeof(fsal_cb_arg_t))) == NULL)
            {
              LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
              Fatal();
            }
          
          fsal_cb_args->export_entry = pcurrent;
          
          if( ( rc = pthread_create( &pcurrent->fsal_cb_thr, &attr_thr,
                                     fsal_cb_thread,(void *)fsal_cb_args)) != 0)
            {
              Mem_Free(fsal_cb_args);
              LogFatal(COMPONENT_THREAD,
                       "Could not create fsal_cb_thread, error = %d (%s)",
                       errno, strerror(errno));
              Fatal();
            }
        }          
    }
}

/* Given to MakePool() to be used as a constructor of 
 * preallocated memory */
void constructor_fsal_cb_event_t(void *ptr)
{
  return;
}

/* One pool can be used for all FSAL_CB used for exports. */
void nfs_Init_FSAL_CB()
{
  nfs_param.fsal_cb_param.nb_event_data_prealloc = 2;

  /* DEBUGGING */
  LogDebug(COMPONENT_INIT,
           "FSAL_CB: Initializing FSAL CB data pool");  
  /* Allocation of the FSAL CB pool */
  MakePool(&nfs_param.fsal_cb_param.event_pool,
           nfs_param.fsal_cb_param.nb_event_data_prealloc,
           fsal_cb_event_t,
           constructor_fsal_cb_event_t, NULL);
  NamePool(&nfs_param.fsal_cb_param.event_pool, "FSAL CB Data Pool");
  if(!IsPoolPreallocated(&nfs_param.fsal_cb_param.event_pool))
    {
      LogCrit(COMPONENT_INIT,
              "Error while allocating FSAL CB data pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      Fatal();
    }    

  return;
}

fsal_status_t process_event(fsal_cb_event_t *event, fsal_cb_event_functions_t *event_func)
{
  fsal_status_t status;
  /* Set the event data structure's cache inode hash table reference. */
  event->event_data.event_context.ht = nfs_param.fsal_cb_param.ht;  

  /* FullDebug, convert fhandle to file path and print. */
  
  /* DEBUGGING */
  switch(event->event_type)
    {
    case FSAL_CB_EVENT_CREATE:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process CREATE event");
      status = event_func->fsal_cb_create(&event->event_data);
      break;
    case FSAL_CB_EVENT_UNLINK:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process UNLINK event");
      status = event_func->fsal_cb_unlink(&event->event_data);
      break;
    case FSAL_CB_EVENT_RENAME:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process RENAME event");
      status = event_func->fsal_cb_rename(&event->event_data);
      break;
    case FSAL_CB_EVENT_COMMIT:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process COMMIT event");
      status = event_func->fsal_cb_commit(&event->event_data);
      break;
    case FSAL_CB_EVENT_WRITE:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process WRITE event");
      status = event_func->fsal_cb_write(&event->event_data);
      break;
    case FSAL_CB_EVENT_LINK:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process LINK event");
      status = event_func->fsal_cb_link(&event->event_data);
      break;
    case FSAL_CB_EVENT_LOCK:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process LOCK event");
      status = event_func->fsal_cb_lock(&event->event_data);
      break;
    case FSAL_CB_EVENT_LOCKU:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process LOCKU event");
      status = event_func->fsal_cb_locku(&event->event_data);
      break;
    case FSAL_CB_EVENT_OPEN:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process OPEN event");
      status = event_func->fsal_cb_open(&event->event_data);
      break;
    case FSAL_CB_EVENT_CLOSE:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process CLOSE event");
      status = event_func->fsal_cb_close(&event->event_data);
      break;
    case FSAL_CB_EVENT_SETATTR:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process SETATTR event");
      status = event_func->fsal_cb_setattr(&event->event_data);
      break;
    case FSAL_CB_EVENT_INVALIDATE:
      LogDebug(COMPONENT_FSAL_CB, "FSAL_CB: Process INVALIDATE event");
      status = event_func->fsal_cb_create(&event->event_data);
      break;
    default:
      LogDebug(COMPONENT_FSAL_CB, "Unknown FSAL CB event type found: %d",
              event->event_type);
    }

  if (FSAL_IS_ERROR(status))
    {
      LogDebug(COMPONENT_FSAL_CB,"Error: Failed to process event");
    }
  return status;
}

/* The admin exportlist must be locked before calling this function!! */
static int fsal_cb_thread_exists(exportlist_t *entry)
{
  exportlist_t *pcurrent;
  pthread_t zeros;
  memset(&zeros, 0, sizeof(zeros));
  
  /* Loop through all export entries and if any have the same
   * filesystem id, assume they really do export a directory
   * from the same filesystem. In this case, check if there is
   * already an FSAL CB thread running. If there is, return TRUE
   * so we don't create multiple threads. */
  for(pcurrent = nfs_param.pexportlist;
      pcurrent != NULL;
      pcurrent = pcurrent->next)
    {
      if (pcurrent == entry)
        continue;

      if (pcurrent->use_fsal_cb == FALSE)
        continue;

      /* Should I check if major as well as minor are different? */
      if (((pcurrent->filesystem_id.major != entry->filesystem_id.major)
           || (pcurrent->filesystem_id.minor != entry->filesystem_id.minor)))
        continue;

      /* Is this the right wayt to check if a pthread reference is valid? */
      if (memcmp(&pcurrent->fsal_cb_thr, &zeros, sizeof(pthread_t)) != 0)
        return pcurrent->id; /* Meaning a thread exists, so exit this thread.*/
      else
        continue;
    }  
 return 0; /* Meaning no there weren't other threads and we set ours. */
}

fsal_cb_event_functions_t *get_fsal_cb_functions(char *fsal_cb_type)
{
  if (strncmp(fsal_cb_type, FSAL_CB_DUMB_TYPE, sizeof(FSAL_CB_DUMB_TYPE)) == 0)
    {
      LogEvent(COMPONENT_FSAL_CB, "Using the FSAL CB DUMB functions to handle"
               " FSAL CB events.");
      return get_fsal_cb_dumb_functions();
    }  
  else
    {
      return NULL;
    }
}

fsal_cb_event_bus_filter_t *find_filter(char filtername[MAX_FILTER_NAMELEN])
{
  return NULL;
}

void *fsal_cb_thread(void *Arg)
{
  fsal_status_t status;
  int rc;
  fsal_cb_arg_t *fsal_cb_args = (fsal_cb_arg_t *)Arg;
  fsal_cb_event_bus_context_t fsal_cb_context;
  fsal_cb_event_bus_parameter_t fsal_cb_bus_param;
  fsal_cb_event_bus_filter_t * pcbebfilter = NULL;
  fsal_cb_filter_list_t *filter = NULL;
  fsal_cb_event_t *pevent_head, *event, *tmpevent;
  fsal_cb_event_functions_t *event_func;
  fsal_count_t nb_events_found, event_nb;
  fsal_time_t timeout;
  char thr_name[40];

  snprintf(thr_name, sizeof(thr_name), "FSAL CB Thread for filesystem %llu.%llu",
           fsal_cb_args->export_entry->filesystem_id.major,
           fsal_cb_args->export_entry->filesystem_id.minor);
  SetNameFunction(thr_name);
  
#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_fsal_cb)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogFatal(COMPONENT_FSAL_CB,
               "FSAL_CB: Memory manager could not be initialized");
      Fatal();
    }
  LogInfo(COMPONENT_FSAL_CB,
          "FSAL_CB: Memory manager for filesystem %llu.%llu export id %d"
          " successfully initialized",
          fsal_cb_args->export_entry->filesystem_id.major,
          fsal_cb_args->export_entry->filesystem_id.minor,
          fsal_cb_args->export_entry->id);
#endif
  
  /* Set the FSAL CB functions that will be used to process events. */
  event_func = get_fsal_cb_functions(fsal_cb_args->export_entry->fsal_cb_type);
  if (event_func == NULL)
    {
      LogCrit(COMPONENT_FSAL_CB, "Error: FSAL CB TYPE: %s does not exist. "
              "Exiting FSAL CB thread.", fsal_cb_args->export_entry->fsal_cb_type);
      Mem_Free(Arg);
      return NULL;
    }

  /* Get fsal cb context from FSAL */
  /* It is expected that the export entry and event_pool will be referenced
   * in the returned callback context structure. */
  memcpy(&fsal_cb_context.FS_export_context,
         &fsal_cb_args->export_entry->FS_export_context,
         sizeof(fsal_export_context_t));

  fsal_cb_context.event_pool = &nfs_param.fsal_cb_param.event_pool;

  LogDebug(COMPONENT_FSAL_CB, "Initializing FSAL Callback context.");
  status = FSAL_CB_Init(&fsal_cb_bus_param, &fsal_cb_context);
  if (FSAL_IS_ERROR(status))
    {
      LogCrit(COMPONENT_FSAL_CB, "Error: Could not initialize FSAL CB for"
              " filesystem %llu.%llu export %d. Exiting FSAL CB thread.",
              fsal_cb_args->export_entry->filesystem_id.major,
              fsal_cb_args->export_entry->filesystem_id.minor,
              fsal_cb_args->export_entry->id);
    }

  /* Add filters ... later if needed we could add arguments to filters
   * configurable from configuration files. */
  for(filter = fsal_cb_args->export_entry->fsal_cb_filter_list;
      filter != NULL; filter = filter->next)
    {
      LogEvent(COMPONENT_FSAL_CB, "Applying filter \"%s\" to FSAL CB thread "
               "for filesystem id %llu.%llu export id %d.", filter->name,
              fsal_cb_args->export_entry->filesystem_id.major,
              fsal_cb_args->export_entry->filesystem_id.minor,
              fsal_cb_args->export_entry->id);

      /* Find predefined filter */
      pcbebfilter = find_filter(filter->name);
      if (pcbebfilter == NULL)
        {
          LogCrit(COMPONENT_FSAL_CB, "Error: Could not find filter named \"%s\".",
                   filter->name);
        }

      /* Applying filter */
      FSAL_CB_AddFilter(pcbebfilter, &fsal_cb_context);
    }


  /* Set the timeout for getting events. */
  timeout = fsal_cb_args->export_entry->fsal_cb_timeout;

  /* Start querying for events and processing. */
  while(1)
    {
      /* pevent is passed in as a single empty node, it's expected the
       * FSAL will use the event_pool in the bus_context to populate
       * this array by adding to the pevent_head->next attribute. */
      event_nb = 0;
      nb_events_found = 0;
      pevent_head = NULL;
      LogDebug(COMPONENT_FSAL_CB, "Requesting event from FSAL Callback interface.");
      status = FSAL_CB_GetEvents(&pevent_head,     /* out */
                                 &event_nb,        /* in/out */
                                 timeout,          /* in */
                                 &nb_events_found, /* out */
                                 &fsal_cb_context);/* in */
      if (FSAL_IS_ERROR(status))
        {
          if (status.major == ERR_FSAL_TIMEOUT)
            LogDebug(COMPONENT_FSAL_CB, "FSAL_CB_EB_GetEvents() hit the timeout"
                     " limit of %u.%u seconds for filesystem id %llu.%llu export id"
                     " %d.", timeout.seconds, timeout.nseconds,
                     fsal_cb_args->export_entry->filesystem_id.major,
                     fsal_cb_args->export_entry->filesystem_id.minor,
                     fsal_cb_args->export_entry->id);
          else if (status.major == ERR_FSAL_NOTSUPP)
            {
              LogCrit(COMPONENT_FSAL_CB, "Exiting FSAL CB Thread for filesystem"
                      " id %llu.%llu export id %u because the FSAL Callback"
                      " Interface is not supported for this FSAL type.",
                      fsal_cb_args->export_entry->filesystem_id.major,
                      fsal_cb_args->export_entry->filesystem_id.minor,
                      fsal_cb_args->export_entry->id);
              return NULL;
            }
          else
            LogDebug(COMPONENT_FSAL_CB, "Error: FSAL_CB_EB_GetEvents() "
                     "failed");
        }

      LogDebug(COMPONENT_FSAL_CB, "Received %lu events to process for filesystem"
                     " id %llu.%llu export id %u.",
               event_nb,
               fsal_cb_args->export_entry->filesystem_id.major,
               fsal_cb_args->export_entry->filesystem_id.minor,
               fsal_cb_args->export_entry->id);
      
      /* process the list of events */
      for(event = pevent_head; event != NULL;)
        {
          status = process_event(event, event_func);
          if (FSAL_IS_ERROR(status))
            {
              LogDebug(COMPONENT_FSAL_CB, "Error: Event could not be processed "
                       "for filesystem %llu.%llu export id %u.",
                       fsal_cb_args->export_entry->filesystem_id.major,
                       fsal_cb_args->export_entry->filesystem_id.minor,
                       fsal_cb_args->export_entry->id);
            }
          tmpevent = event;
          event = event->next_event;
          ReleaseToPool(tmpevent, &nfs_param.fsal_cb_param.event_pool);
          event_nb--;
        }

      LogDebug(COMPONENT_FSAL_CB, "%lu events not found for filesystem"
               " %llu.%llu export id %u", event_nb,
               fsal_cb_args->export_entry->filesystem_id.major,
               fsal_cb_args->export_entry->filesystem_id.minor,
               fsal_cb_args->export_entry->id);
    }

  Mem_Free(Arg);
  return NULL;
}                               /* fsal_cb_thread */

