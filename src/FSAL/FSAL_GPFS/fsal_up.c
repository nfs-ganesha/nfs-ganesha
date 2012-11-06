/*
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
 * @file    fsal_up.c
 * @brief   FSAL Upcall Interface
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

struct glist_head gpfs_fsal_up_ctx_list;

/** @todo FSF: there are lots of assumptions in here that must be fixed when we
 *             support unexport. The thread may go away when all exports are
 *             removed and must clean itself up. Also, it must make sure it gets
 *             mount_root_fd from a living export.
 */
void *GPFSFSAL_UP_Thread(void *Arg)
{
  struct fsal_up_event       * pevent;
  const struct fsal_up_vector * event_func;
  char                       thr_name[80];
  struct gpfs_fsal_up_ctx   * gpfs_fsal_up_ctx = (struct gpfs_fsal_up_ctx *) Arg;
  int                         rc = 0;
  struct stat                 buf;
  struct glock                fl;
  struct callback_arg         callback;
  struct gpfs_file_handle   * phandle;
  int                        reason = 0;
  int                        flags = 0;
  unsigned int              * fhP;
  int                        retry = 0;
  fsal_status_t              st;

  snprintf(thr_name, sizeof(thr_name), "gpfs_fsal_up_%d.%d",
           gpfs_fsal_up_ctx->gf_fsid[0], gpfs_fsal_up_ctx->gf_fsid[1]);
  SetNameFunction(thr_name);

  /* Set the FSAL UP functions that will be used to process events. */
  event_func = gpfs_fsal_up_ctx->gf_export->up_ops;

  if(event_func == NULL)
    {
      LogFatal(COMPONENT_FSAL_UP,
               "FSAL up vector does not exist. Can not continue.");
      gsh_free(Arg);
      return NULL;
    }

  LogFullDebug(COMPONENT_FSAL_UP,
               "Initializing FSAL Callback context for %d.",
               gpfs_fsal_up_ctx->gf_fd);

  /* Start querying for events and processing. */
  while(1)
    {
      /* Make sure we have at least one export. */
      if(glist_empty(&gpfs_fsal_up_ctx_list))
        {
          /** @todo FSF: should properly clean up if we have unexported all
           *             exports on this file system.
           */
          LogCrit(COMPONENT_FSAL_UP,
                  "All exports for file system %d have gone away",
                  gpfs_fsal_up_ctx->gf_fd);
          gsh_free(Arg);
          return NULL;
        }

      /* pevent is passed in as a single empty node, it's expected the
       * FSAL will use the event_pool in the bus_context to populate
       * this array by adding to the pevent_head. */

      /* Get a new event structure */
      pevent = fsal_up_alloc_event();
      if(pevent == NULL)
        {
          if(retry < 100)   /* pool not be initialized */
            {
              sleep(1);
              retry++;
              continue;
            }
          LogFatal(COMPONENT_FSAL_UP,
                   "Out of memory, can not continue.");
        }

      phandle = gsh_malloc(sizeof(struct gpfs_file_handle));

      if(phandle == NULL)
        {
          LogFatal(COMPONENT_FSAL_UP,
                   "Out of memory, can not continue.");
        }

      LogFullDebug(COMPONENT_FSAL_UP,
                   "Requesting event from FSAL Callback interface for %d.",
                   gpfs_fsal_up_ctx->gf_fd);

      phandle->handle_size = OPENHANDLE_HANDLE_LEN;
      phandle->handle_key_size = OPENHANDLE_KEY_LEN;
      phandle->handle_version = OPENHANDLE_VERSION;

      callback.interface_version = GPFS_INTERFACE_VERSION +
                                   GPFS_INTERFACE_SUB_VER;

      callback.mountdirfd = gpfs_fsal_up_ctx->gf_fd;
      callback.handle     = phandle;
      callback.reason     = &reason;
      callback.flags      = &flags;
      callback.buf        = &buf;
      callback.fl         = &fl;

      rc = gpfs_ganesha(OPENHANDLE_INODE_UPDATE, &callback);

      if(rc != 0)
        {
          LogCrit(COMPONENT_FSAL_UP,
                  "OPENHANDLE_INODE_UPDATE failed for %d."
                  " rc %d, errno %d (%s) reason %d",
                  gpfs_fsal_up_ctx->gf_fd, rc, errno, strerror(errno), reason);

          gsh_free(phandle);

          rc = -(rc);
          if(rc > GPFS_INTERFACE_VERSION)
          {
            LogFatal(COMPONENT_FSAL,
                    "Ganesha version %d mismatch GPFS version %d.",
                     callback.interface_version, rc);
            return NULL;
          }
          if(retry < 100)
            {
              retry++;
              continue;
            }

          if(errno == EUNATCH)
            LogFatal(COMPONENT_FSAL,
                     "GPFS file system %d has gone away.",
                     gpfs_fsal_up_ctx->gf_fd);

          continue;
        }

      retry = 0;

      /* Fill in fsid portion of handle */
      callback.handle->handle_fsid[0] = gpfs_fsal_up_ctx->gf_fsid[0];
      callback.handle->handle_fsid[1] = gpfs_fsal_up_ctx->gf_fsid[1];

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
      pevent->file.export = gpfs_fsal_up_ctx->gf_export;
      pevent->file.export->ops->get(pevent->file.export);
      pevent->functions = event_func;
      pevent->file.key.addr = phandle;
      pevent->file.key.len = phandle->handle_key_size;

      switch(reason)
      {
          case INODE_LOCK_GRANTED: /* Lock Event */
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode lock granted: owner %p pid %d type %d start %lld len %lld",
                        fl.lock_owner, fl.flock.l_pid, fl.flock.l_type,
                        (long long) fl.flock.l_start, (long long) fl.flock.l_len);
            pevent->data.lock_grant.lock_owner = fl.lock_owner;
            pevent->data.lock_grant.lock_param.lock_length = fl.flock.l_len;
            pevent->data.lock_grant.lock_param.lock_start = fl.flock.l_start;
            pevent->data.lock_grant.lock_param.lock_type = fl.flock.l_type;
            pevent->type = FSAL_UP_EVENT_LOCK_GRANT;
            break;

          case INODE_LOCK_AGAIN: /* Lock Event */
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode lock again: owner %p pid %d type %d start %lld len %lld",
                        fl.lock_owner, fl.flock.l_pid, fl.flock.l_type,
                        (long long) fl.flock.l_start, (long long) fl.flock.l_len);
            pevent->data.lock_grant.lock_owner = fl.lock_owner;
            pevent->data.lock_grant.lock_param.lock_length = fl.flock.l_len;
            pevent->data.lock_grant.lock_param.lock_start = fl.flock.l_start;
            pevent->data.lock_grant.lock_param.lock_type = fl.flock.l_type;
            pevent->type = FSAL_UP_EVENT_LOCK_GRANT;
            break;

          case BREAK_DELEGATION: /* Delegation Event */
            LogDebug(COMPONENT_FSAL,
                     "delegation recall: flags:%x ino %ld",
                     flags, callback.buf->st_ino);
            pevent->data.delegrecall.flags = 0;
            pevent->type = FSAL_UP_EVENT_DELEGATION_RECALL;
            break;

          case INODE_UPDATE: /* Update Event */
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode update: flags:%x update ino %ld n_link:%d",
                        flags, callback.buf->st_ino, (int)callback.buf->st_nlink);

            pevent->data.update.flags = 0;
            /* convert attributes */

            pevent->data.update.attr.mask = 0;

            /* Check for accepted flags, any other changes or errors just invalidate. */
            if (!(flags & ~(UP_NLINK | UP_MODE | UP_OWN | UP_TIMES | UP_ATIME)))
            {
              if (flags & UP_NLINK)
                pevent->data.update.flags |= fsal_up_nlink;
              if (flags & UP_MODE)
                pevent->data.update.attr.mask |= ATTR_MODE;
              if (flags & UP_OWN)
                pevent->data.update.attr.mask |= ATTR_OWNER;
              if (flags & UP_TIMES)
                pevent->data.update.attr.mask |= ATTR_ATIME|ATTR_CTIME|ATTR_MTIME;
              if (flags & UP_ATIME)
                pevent->data.update.attr.mask |= ATTR_ATIME;

              st = posix2fsal_attributes(&buf, &pevent->data.update.attr);
              if(FSAL_IS_ERROR(st))
                pevent->type = FSAL_UP_EVENT_INVALIDATE;
              else
                pevent->type = FSAL_UP_EVENT_UPDATE;
            }
            else
              pevent->type = FSAL_UP_EVENT_INVALIDATE;

            break;

          case THREAD_STOP: /* GPFS export no longer available */
            LogWarn(COMPONENT_FSAL,
                    "GPFS file system %d is no longer available",
                    gpfs_fsal_up_ctx->gf_fd);
            fsal_up_free_event(pevent);
            return NULL;

          case INODE_INVALIDATE:
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode invalidate: flags:%x update ino %ld",
                        flags, callback.buf->st_ino);
            pevent->type = FSAL_UP_EVENT_INVALIDATE;
            break;

          default:
            LogWarn(COMPONENT_FSAL_UP,
                        "Unknown event: %d", reason);
            fsal_up_free_event(pevent);
            continue;
      }

      LogDebug(COMPONENT_FSAL_UP,
               "Received event to process for %d",
               gpfs_fsal_up_ctx->gf_fd);

      /* process the event */
      rc = fsal_up_submit(pevent);

      if(rc)
        {
          LogWarn(COMPONENT_FSAL_UP,
                  "Event could not be processed for %d rc %d.",
                  gpfs_fsal_up_ctx->gf_fd, rc);
        }
    }

  return NULL;
}                               /* GPFSFSAL_UP_Thread */

struct gpfs_fsal_up_ctx * gpfsfsal_find_fsal_up_context(
                                            struct gpfs_fsal_up_ctx *export_ctx)
{
  struct glist_head * glist;

  glist_for_each(glist, &gpfs_fsal_up_ctx_list)
    {
      struct gpfs_fsal_up_ctx * gpfs_fsal_up_ctx;

      gpfs_fsal_up_ctx = glist_entry(glist, struct gpfs_fsal_up_ctx, gf_list);

      if((gpfs_fsal_up_ctx->gf_fsid[0] == export_ctx->gf_fsid[0]) &&
         (gpfs_fsal_up_ctx->gf_fsid[1] == export_ctx->gf_fsid[1]))
        return gpfs_fsal_up_ctx;
    }

  return NULL;
}
