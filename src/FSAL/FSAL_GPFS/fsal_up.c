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
 *
 * \file    fsal_up.c
 * \brief   FSAL Upcall Interface
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_up.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>

#ifdef _USE_FSAL_UP

fsal_status_t GPFSFSAL_UP_Init( fsal_up_event_bus_parameter_t * pebparam,      /* IN */
                                   fsal_up_event_bus_context_t * pupebcontext     /* OUT */)
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_UP_init);
}

fsal_status_t GPFSFSAL_UP_AddFilter( fsal_up_event_bus_filter_t * pupebfilter,  /* IN */
                                        fsal_up_event_bus_context_t * pupebcontext /* INOUT */ )
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_UP_addfilter);
}

fsal_status_t GPFSFSAL_UP_GetEvents( fsal_up_event_t ** pevents,                  /* OUT */
                                     fsal_count_t * event_nb,                     /* IN */
                                     fsal_time_t timeout,                         /* IN */
                                     fsal_count_t * peventfound,                  /* OUT */
                                     fsal_up_event_bus_context_t * pupebcontext   /* IN */ )
{
  int rc = 0;
  struct stat64 buf;
  struct flock fl;
  struct callback_arg callback;
  cache_inode_fsal_data_t pfsal_data;
  gpfsfsal_handle_t *phandle = (gpfsfsal_handle_t *) &pfsal_data.handle;
  int reason = 0;
  unsigned int *fhP;

  if (pupebcontext == NULL || event_nb == NULL)
    {
      LogDebug(COMPONENT_FSAL, "Error: GPFSFSAL_UP_GetEvents() received"
               " unexpectedly NULL arguments.");
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_UP_getevents);
    }

  gpfsfsal_export_context_t *p_export_context =
    (gpfsfsal_export_context_t *)&pupebcontext->FS_export_context;

  pfsal_data.cookie = 0;
  phandle->data.handle.handle_size = OPENHANDLE_HANDLE_LEN;
  phandle->data.handle.handle_key_size = 0;
  callback.mountdirfd = p_export_context->mount_root_fd;
  callback.handle = &phandle->data.handle;
  callback.reason = &reason;
  callback.buf = &buf;
  callback.fl = (struct glock *) &fl;

  rc = gpfs_ganesha(OPENHANDLE_INODE_UPDATE, &callback);
  LogDebug(COMPONENT_FSAL,
           "inode update: rc %d reason %d update ino %ld",
           rc, reason, callback.buf->st_ino);
  LogDebug(COMPONENT_FSAL,
           "inode update: handle size = %u key_size = %u",
           callback.handle->handle_size,
           callback.handle->handle_key_size);
  fhP = (int *)&(callback.handle->f_handle[0]);
  LogDebug(COMPONENT_FSAL,
           " inode update: handle %08x %08x %08x %08x %08x %08x %08x\n",
           fhP[0],fhP[1],fhP[2],fhP[3],fhP[4],fhP[5],fhP[6]);

  /* Here is where we decide what type of event this is
   * ... open,close,read,...,invalidate? */
  if (*pevents == NULL)
    GetFromPool(*pevents, pupebcontext->event_pool, fsal_up_event_t);
  memset(&(*pevents)->event_data.event_context.fsal_data, 0,
         sizeof(cache_inode_fsal_data_t));
  memcpy(&(*pevents)->event_data.event_context.fsal_data, &pfsal_data,
         sizeof(cache_inode_fsal_data_t));

  if (reason == INODE_LOCK_GRANTED) /* Lock Event */
    {
      LogDebug(COMPONENT_FSAL,
               "inode update: lock pid %d type %d start %lld len %lld",
               fl.l_pid, fl.l_type, (long long) fl.l_start,
               (long long) fl.l_len);
      (*pevents)->event_data.type.lock.lock_param.lock_owner = fl.l_pid;
      (*pevents)->event_data.type.lock.lock_param.lock_length = fl.l_len;
      (*pevents)->event_data.type.lock.lock_param.lock_start = fl.l_start;
      (*pevents)->event_data.type.lock.lock_param.lock_type = fl.l_type;
      (*pevents)->event_type = FSAL_UP_EVENT_LOCK;
    }
  else /* Invalidate Event - Default */
    {
      (*pevents)->event_type = FSAL_UP_EVENT_INVALIDATE;
    }

  /* Increment the numebr of events we are returning.*/
  (*event_nb)++;

  /* Return() will increment statistics ... but that object is
   * allocated by different threads ... is that a memory leak? */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_UP_getevents);
}

#endif /* _USE_FSAL_UP */
