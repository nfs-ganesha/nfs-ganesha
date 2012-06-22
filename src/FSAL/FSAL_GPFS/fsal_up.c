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

fsal_status_t GPFSFSAL_UP_GetEvents( struct glist_head * pevent_head,             /* OUT */
                                     fsal_count_t * event_nb,                     /* IN */
                                     fsal_time_t timeout,                         /* IN */
                                     fsal_count_t * peventfound,                  /* OUT */
                                     fsal_up_event_bus_context_t * pupebcontext   /* IN */ )
{
  int rc = 0;
  struct stat buf;
  struct glock fl;
  struct callback_arg callback;
  cache_inode_fsal_data_t pfsal_data;
  fsal_handle_t *tmp_handlep = NULL;
  gpfsfsal_handle_t *phandle;
  int reason = 0;
  int flags = 0;
  unsigned int *fhP;
  cache_inode_fsal_data_t *event_fsal_data;
  fsal_up_event_t *pevent;

  tmp_handlep = gsh_malloc(sizeof(fsal_handle_t));
  if (tmp_handlep == NULL)
    {
      LogCrit(COMPONENT_FSAL, "Error: Could not malloc ... ENOMEM");
      Return(ERR_FSAL_NOMEM, ENOMEM, INDEX_FSAL_UP_getevents);
    }

  pfsal_data.fh_desc.start = (caddr_t)tmp_handlep;
  pfsal_data.fh_desc.len = sizeof(*tmp_handlep);
  phandle = (gpfsfsal_handle_t *) pfsal_data.fh_desc.start;

  if (pupebcontext == NULL || event_nb == NULL)
    {
      LogDebug(COMPONENT_FSAL, "Error: GPFSFSAL_UP_GetEvents() received"
               " unexpectedly NULL arguments.");
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_UP_getevents);
    }

  gpfsfsal_export_context_t *p_export_context =
    (gpfsfsal_export_context_t *)&pupebcontext->FS_export_context;

  phandle->data.handle.handle_size = OPENHANDLE_HANDLE_LEN;
  phandle->data.handle.handle_key_size = 0;
  callback.mountdirfd = p_export_context->mount_root_fd;
  callback.handle = (struct gpfs_file_handle *) &phandle->data.handle;
  callback.reason = &reason;
  callback.flags = &flags;
  callback.buf = &buf;
  callback.fl = &fl;

  rc = gpfs_ganesha(OPENHANDLE_INODE_UPDATE, &callback);
  if (rc != 0)
    {
      LogCrit(COMPONENT_FSAL,
        "Error: OPENHANDLE_INODE_UPDATE failed. rc %d, errno %d", rc, errno);
      gsh_free(tmp_handlep);
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_UP_getevents);
    }

  LogDebug(COMPONENT_FSAL,
           "inode update: rc %d reason %d update ino %ld",
           rc, reason, callback.buf->st_ino);
  LogDebug(COMPONENT_FSAL,
           "inode update: tmp_handlep:%p flags:%x callback.handle:%p  pfsal_data.fh_desc.start:%p handle size = %u handle_type:%d handle_version:%d key_size = %u f_handle:%p", tmp_handlep, *callback.flags, callback.handle,
           pfsal_data.fh_desc.start,
           callback.handle->handle_size,
           callback.handle->handle_type,
           callback.handle->handle_version,
           callback.handle->handle_key_size,
           callback.handle->f_handle);

  callback.handle->handle_version = OPENHANDLE_VERSION;

  // TODO: Workaround until we get new GPFS code.
  callback.handle->handle_type = 7;

  fhP = (int *)&(callback.handle->f_handle[0]);
  LogDebug(COMPONENT_FSAL,
           " inode update: handle %08x %08x %08x %08x %08x %08x %08x\n",
           fhP[0],fhP[1],fhP[2],fhP[3],fhP[4],fhP[5],fhP[6]);

  /* Here is where we decide what type of event this is
   * ... open,close,read,...,invalidate? */
  pthread_mutex_lock(pupebcontext->event_pool_lock);
  pevent = pool_alloc(pupebcontext->event_pool, NULL);
  pthread_mutex_unlock(pupebcontext->event_pool_lock);

  event_fsal_data = &pevent->event_data.event_context.fsal_data;
  event_fsal_data->fh_desc.start = (caddr_t)tmp_handlep;
  event_fsal_data->fh_desc.len = sizeof(*tmp_handlep);
  GPFSFSAL_ExpandHandle(NULL, FSAL_DIGEST_SIZEOF, &(event_fsal_data->fh_desc));
  switch (reason)
    {
      case INODE_LOCK_GRANTED: /* Lock Event */
        LogDebug(COMPONENT_FSAL,
                 "inode lock granted: owner %p pid %d type %d start %lld len %lld",
                 fl.lock_owner, fl.flock.l_pid, fl.flock.l_type,
                 (long long) fl.flock.l_start, (long long) fl.flock.l_len);
        pevent->event_data.type.lock_grant.lock_owner = fl.lock_owner;
        pevent->event_data.type.lock_grant.lock_param.lock_length = fl.flock.l_len;
        pevent->event_data.type.lock_grant.lock_param.lock_start = fl.flock.l_start;
        pevent->event_data.type.lock_grant.lock_param.lock_type = fl.flock.l_type;
        pevent->event_type = FSAL_UP_EVENT_LOCK_GRANT;
        break;
      case INODE_LOCK_AGAIN: /* Lock Event */
        LogDebug(COMPONENT_FSAL,
                 "inode lock again: owner %p pid %d type %d start %lld len %lld",
                 fl.lock_owner, fl.flock.l_pid, fl.flock.l_type,
                 (long long) fl.flock.l_start, (long long) fl.flock.l_len);
        pevent->event_data.type.lock_grant.lock_owner = fl.lock_owner;
        pevent->event_data.type.lock_grant.lock_param.lock_length = fl.flock.l_len;
        pevent->event_data.type.lock_grant.lock_param.lock_start = fl.flock.l_start;
        pevent->event_data.type.lock_grant.lock_param.lock_type = fl.flock.l_type;
        pevent->event_type = FSAL_UP_EVENT_LOCK_GRANT;
        break;
      case INODE_UPDATE: /* Update Event */
        LogDebug(COMPONENT_FSAL,
                 "inode update: flags:%x update ino %ld n_link:%d",
                 flags, callback.buf->st_ino, (int)callback.buf->st_nlink);
        pevent->event_data.type.update.upu_flags = 0;
        pevent->event_data.type.update.upu_stat_buf = buf;
        if (flags & UP_NLINK)
          pevent->event_data.type.update.upu_flags |= FSAL_UP_NLINK;
        pevent->event_type = FSAL_UP_EVENT_UPDATE;
        break;
      default: /* Invalidate Event - Default */
        pevent->event_type = FSAL_UP_EVENT_INVALIDATE;
    }
  glist_add_tail(pevent_head, &pevent->event_list);
  /* Increment the numebr of events we are returning.*/
  (*event_nb)++;

  /* Return() will increment statistics ... but that object is
   * allocated by different threads ... is that a memory leak? */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_UP_getevents);
}

#endif /* _USE_FSAL_UP */
