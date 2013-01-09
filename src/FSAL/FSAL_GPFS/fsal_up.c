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

struct glist_head gpfs_fsal_up_ctx_list;

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
  /* GPFS no longer uses the API for FSAL UP */
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_UP_getevents);
}

/** @todo FSF: there are lots of assumptions in here that must be fixed when we
 *             support unexport. The thread may go away when all exports are
 *             removed and must clean itself up. Also, it must make sure it gets
 *             mount_root_fd from a living export.
 */
void *GPFSFSAL_UP_Thread(void *Arg)
{
  fsal_status_t               status;
  fsal_up_event_t           * pevent;
  fsal_up_event_functions_t * event_func;
  char                        thr_name[80];
  gpfs_fsal_up_ctx_t        * gpfs_fsal_up_ctx = (gpfs_fsal_up_ctx_t *) Arg;
  gpfsfsal_export_context_t * export_ctx;
  int                         rc = 0;
  struct stat                 buf;
  struct glock                fl;
  struct callback_arg         callback;
  gpfsfsal_handle_t         * phandle;
  int                         reason = 0;
  int                         flags = 0;
  unsigned int              * fhP;
  cache_inode_fsal_data_t   * event_fsal_data;
  int                         retry = 0;

  snprintf(thr_name, sizeof(thr_name), "fsal_up_%d.%d",
           gpfs_fsal_up_ctx->gf_fsid[0], gpfs_fsal_up_ctx->gf_fsid[1]);
  SetNameFunction(thr_name);

  /* Set the FSAL UP functions that will be used to process events. */
  event_func = get_fsal_up_dumb_functions();

  if(event_func == NULL)
    {
      LogFatal(COMPONENT_FSAL_UP,
               "FSAL UP TYPE: %s does not exist. Can not continue.",
               FSAL_UP_DUMB_TYPE);
      gsh_free(Arg);
      return NULL;
    }
  rc = fsal_internal_version();
  LogFullDebug(COMPONENT_FSAL_UP,
               "Initializing FSAL Callback context for %s. GPFS get version is %d",
               gpfs_fsal_up_ctx->gf_fs, rc);

  /* Start querying for events and processing. */
  while(1)
    {
      /* Make sure we have at least one export. */
      if(glist_empty(&gpfs_fsal_up_ctx->gf_exports))
        {
          /** @todo FSF: should properly clean up if we have unexported all
           *             exports on this file system.
           */
          LogCrit(COMPONENT_FSAL_UP,
                  "All exports for file system %s have gone away",
                  gpfs_fsal_up_ctx->gf_fs);
          gsh_free(Arg);
          return NULL;
        }

      /* Use the mount_root_fd from the first export on this file system */
      export_ctx = glist_first_entry(&gpfs_fsal_up_ctx->gf_exports,
                                     gpfsfsal_export_context_t,
                                     fe_list);

      /* pevent is passed in as a single empty node, it's expected the
       * FSAL will use the event_pool in the bus_context to populate
       * this array by adding to the pevent_head. */

      /* Get a new event structure */
      pevent = pool_alloc(fsal_up_event_pool, NULL);

      if(pevent == NULL)
        {
          LogFatal(COMPONENT_FSAL_UP,
                   "Out of memory, can not continue.");
        }

      phandle = gsh_malloc(sizeof(fsal_handle_t));

      if(phandle == NULL)
        {
          LogFatal(COMPONENT_FSAL_UP,
                   "Out of memory, can not continue.");
        }

      LogFullDebug(COMPONENT_FSAL_UP,
                   "Requesting event from FSAL Callback interface for %s.",
                   gpfs_fsal_up_ctx->gf_fs);

      phandle->data.handle.handle_size = OPENHANDLE_HANDLE_LEN;
      phandle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
      phandle->data.handle.handle_version = OPENHANDLE_VERSION;
    
      callback.interface_version = GPFS_INTERFACE_VERSION + GPFS_INTERFACE_SUB_VER; 

      callback.mountdirfd = export_ctx->mount_root_fd;
      callback.handle     = (struct gpfs_file_handle *) &phandle->data.handle;
      callback.reason     = &reason;
      callback.flags      = &flags;
      callback.buf        = &buf;
      callback.fl         = &fl;

#ifdef _VALGRIND_MEMCHECK
      memset(callback.handle->f_handle, 0, callback.handle->handle_size);
#endif

      rc = gpfs_ganesha(OPENHANDLE_INODE_UPDATE, &callback);

      if(rc != 0)
        {
          LogCrit(COMPONENT_FSAL_UP,
                  "OPENHANDLE_INODE_UPDATE failed for %s."
                  " rc %d, errno %d (%s) reason %d",
                  gpfs_fsal_up_ctx->gf_fs, rc, errno, strerror(errno), reason);

          gsh_free(phandle);

          if(retry < 100)
            {
              retry++;
              continue;
            }

          if(errno == EUNATCH)
            LogFatal(COMPONENT_FSAL,
                     "GPFS file system %s has gone away.",
                     gpfs_fsal_up_ctx->gf_fs);

          continue;
        }

      retry = 0;

      LogDebug(COMPONENT_FSAL_UP,
               "inode update: rc %d reason %d update ino %ld",
               rc, reason, callback.buf->st_ino);

      LogFullDebug(COMPONENT_FSAL_UP,
                   "inode update: phandle:%p flags:%x callback.handle:%p"
                   " handle size = %u handle_type:%d handle_version:%d"
                   " key_size = %u handle_fsid=%d.%d f_handle:%p",
                   phandle, *callback.flags, callback.handle,
                   callback.handle->handle_size,
                   callback.handle->handle_type,
                   callback.handle->handle_version,
                   callback.handle->handle_key_size,
                   callback.handle->handle_fsid[0],
                   callback.handle->handle_fsid[1],
                   callback.handle->f_handle);

      callback.handle->handle_version = OPENHANDLE_VERSION;

      fhP = (int *)&(callback.handle->f_handle[0]);
      LogFullDebug(COMPONENT_FSAL_UP,
                   " inode update: handle %08x %08x %08x %08x %08x %08x %08x",
                   fhP[0],fhP[1],fhP[2],fhP[3],fhP[4],fhP[5],fhP[6]);

      /* Here is where we decide what type of event this is
       * ... open,close,read,...,invalidate? */

      event_fsal_data = &pevent->event_data.event_context.fsal_data;
      event_fsal_data->fh_desc.start = (caddr_t)phandle;
      event_fsal_data->fh_desc.len   = sizeof(*phandle);

      GPFSFSAL_ExpandHandle(NULL, FSAL_DIGEST_SIZEOF, &(event_fsal_data->fh_desc));

      switch(reason)
        {
          case INODE_LOCK_GRANTED: /* Lock Event */
            LogMidDebug(COMPONENT_FSAL_UP,
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
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode lock again: owner %p pid %d type %d start %lld len %lld",
                        fl.lock_owner, fl.flock.l_pid, fl.flock.l_type,
                        (long long) fl.flock.l_start, (long long) fl.flock.l_len);
            pevent->event_data.type.lock_grant.lock_owner = fl.lock_owner;
            pevent->event_data.type.lock_grant.lock_param.lock_length = fl.flock.l_len;
            pevent->event_data.type.lock_grant.lock_param.lock_start = fl.flock.l_start;
            pevent->event_data.type.lock_grant.lock_param.lock_type = fl.flock.l_type;
            pevent->event_type = FSAL_UP_EVENT_LOCK_AVAIL;
            break;

          case INODE_UPDATE: /* Update Event */
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode update: flags:%x update ino %ld n_link:%d",
                        flags, callback.buf->st_ino, (int)callback.buf->st_nlink);
            pevent->event_data.type.update.upu_flags = 0;
            pevent->event_data.type.update.upu_stat_buf = buf;
            if (flags & UP_NLINK)
              pevent->event_data.type.update.upu_flags |= FSAL_UP_NLINK;
            pevent->event_type = FSAL_UP_EVENT_UPDATE;
            break;

          case THREAD_STOP: /* GPFS export no longer available */
            LogWarn(COMPONENT_FSAL,
                    "GPFS file system %s is no longer available",
                    gpfs_fsal_up_ctx->gf_fs);
            gsh_free(phandle);
            return NULL;

          default: /* Invalidate Event - Default */
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode invalidate: flags:%x update ino %ld",
                        flags, callback.buf->st_ino);
            pevent->event_type = FSAL_UP_EVENT_INVALIDATE;
        }

      LogDebug(COMPONENT_FSAL_UP,
               "Received event to process for %s",
               gpfs_fsal_up_ctx->gf_fs);

      /* process the event */
      status = process_event(pevent, event_func);

      if(FSAL_IS_ERROR(status))
        {
          LogWarn(COMPONENT_FSAL_UP,
                  "Event could not be processed for %s.",
                  gpfs_fsal_up_ctx->gf_fs);
        }
    }

  return NULL;
}                               /* GPFSFSAL_UP_Thread */

gpfs_fsal_up_ctx_t * gpfsfsal_find_fsal_up_context(gpfsfsal_export_context_t * export_ctx)
{
  struct glist_head * glist;

  glist_for_each(glist, &gpfs_fsal_up_ctx_list)
    {
      gpfs_fsal_up_ctx_t * gpfs_fsal_up_ctx;

      gpfs_fsal_up_ctx = glist_entry(glist, gpfs_fsal_up_ctx_t, gf_list);

      if((gpfs_fsal_up_ctx->gf_fsid[0] == export_ctx->fsid[0]) &&
         (gpfs_fsal_up_ctx->gf_fsid[1] == export_ctx->fsid[1]))
        return gpfs_fsal_up_ctx;
    }

  return NULL;
}

#endif /* _USE_FSAL_UP */
