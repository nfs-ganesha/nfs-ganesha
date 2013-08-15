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
#include "config.h"

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
  const struct fsal_up_vector * event_func;
  char                       thr_name[80];
  struct gpfs_fsal_up_ctx   * gpfs_fsal_up_ctx = (struct gpfs_fsal_up_ctx *) Arg;
  int                         rc = 0;
  struct nfsd4_pnfs_deviceid  dev_id;
  struct stat                 buf;
  struct glock                fl;
  struct callback_arg         callback;
  struct gpfs_file_handle    handle;
  int                        reason = 0;
  int                        flags = 0;
  unsigned int              * fhP;
  int                        retry = 0;
  struct gsh_buffdesc        key;

  snprintf(thr_name, sizeof(thr_name), "gpfs_up_%d.%d",
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

      LogFullDebug(COMPONENT_FSAL_UP,
                   "Requesting event from FSAL Callback interface for %d.",
                   gpfs_fsal_up_ctx->gf_fd);

      handle.handle_size = OPENHANDLE_HANDLE_LEN;
      handle.handle_key_size = OPENHANDLE_KEY_LEN;
      handle.handle_version = OPENHANDLE_VERSION;

      callback.interface_version = GPFS_INTERFACE_VERSION +
                                   GPFS_INTERFACE_SUB_VER;

      callback.mountdirfd = gpfs_fsal_up_ctx->gf_fd;
      callback.handle     = &handle;
      callback.reason     = &reason;
      callback.flags      = &flags;
      callback.buf        = &buf;
      callback.fl         = &fl;
      callback.dev_id     = &dev_id;

#ifdef _VALGRIND_MEMCHECK
      memset(callback.handle->f_handle, 0, callback.handle->handle_size);
#endif

      rc = gpfs_ganesha(OPENHANDLE_INODE_UPDATE, &callback);

      if(rc != 0)
        {
          if(rc == ENOSYS)
          {
            LogFatal(COMPONENT_FSAL_UP,
                    "GPFS was not found, rc ENOSYS");
            return NULL;
          }
          LogCrit(COMPONENT_FSAL_UP,
                  "OPENHANDLE_INODE_UPDATE failed for %d."
                  " rc %d, errno %d (%s) reason %d",
                  gpfs_fsal_up_ctx->gf_fd, rc, errno, strerror(errno), reason);

          rc = -(rc);
          if(rc > GPFS_INTERFACE_VERSION)
          {
            LogFatal(COMPONENT_FSAL_UP,
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
            LogFatal(COMPONENT_FSAL_UP,
                     "GPFS file system %d has gone away.",
                     gpfs_fsal_up_ctx->gf_fd);

          continue;
        }

      retry = 0;

      LogDebug(COMPONENT_FSAL_UP,
               "inode update: rc %d reason %d update ino %ld",
               rc, reason, callback.buf->st_ino);

      LogFullDebug(COMPONENT_FSAL_UP,
                   "inode update: flags:%x callback.handle:%p"
                   " handle size = %u handle_type:%d handle_version:%d"
                   " key_size = %u handle_fsid=%d.%d f_handle:%p",
                   *callback.flags, callback.handle,
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
      key.addr = &handle;
      key.len = handle.handle_key_size;

      LogDebug(COMPONENT_FSAL_UP,
               "Received event to process for %d",
               gpfs_fsal_up_ctx->gf_fd);

      switch(reason)
      {
          case INODE_LOCK_GRANTED: /* Lock Event */
          case INODE_LOCK_AGAIN: /* Lock Event */
	  {
            LogMidDebug(COMPONENT_FSAL_UP,
                        "%s: owner %p pid %d type %d start %lld len %lld",
			reason == INODE_LOCK_GRANTED ?
			"inode lock granted" :
			"inode lock again",
                        fl.lock_owner, fl.flock.l_pid, fl.flock.l_type,
                        (long long) fl.flock.l_start, (long long) fl.flock.l_len);

	    fsal_lock_param_t lockdesc = {
	      .lock_sle_type = FSAL_POSIX_LOCK,
	      .lock_type = fl.flock.l_type,
	      .lock_start = fl.flock.l_start,
	      .lock_length = fl.flock.l_len
	    };
	    rc = up_async_lock_grant(general_fridge,
				     gpfs_fsal_up_ctx->gf_export,
				     &key,
				     fl.lock_owner,
				     &lockdesc,
				     NULL, NULL);
	  }
            break;

          case BREAK_DELEGATION: /* Delegation Event */
            LogDebug(COMPONENT_FSAL_UP,
                     "delegation recall: flags:%x ino %ld",
                     flags, callback.buf->st_ino);
	    rc = up_async_delegrecall(general_fridge,
				      gpfs_fsal_up_ctx->gf_export,
				      &key,
				      NULL, NULL);
            break;

          case LAYOUT_FILE_RECALL: /* Layout file recall Event */
	  {
	    struct pnfs_segment segment =
	      {
		.offset = 0,
		.length = UINT64_MAX,
		.io_mode = LAYOUTIOMODE4_ANY
	      };
            LogDebug(COMPONENT_FSAL_UP,
                     "layout file recall: flags:%x ino %ld",
                     flags, callback.buf->st_ino);

	    rc = up_async_layoutrecall(general_fridge,
				       gpfs_fsal_up_ctx->gf_export,
				       &key,
				       LAYOUT4_NFSV4_1_FILES,
				       false,
				       &segment,
				       NULL, NULL, NULL, NULL);
	  }
            break;

          case LAYOUT_RECALL_ANY: /* Recall all layouts Event */
            LogDebug(COMPONENT_FSAL_UP,
                     "layout recall any: flags:%x ino %ld",
                     flags, callback.buf->st_ino);

	    /**
	     * @todo This functionality needs to be implemented as a
	     * bulk FSID CB_LAYOUTRECALL.  RECALL_ANY isn't suitable
	     * since it can't be restricted to just one FSAL.  Also
	     * an FSID LAYOUTRECALL lets you have multiplke
	     * filesystems exported from one FSAL and not yank layouts
	     * on all of them when you only need to recall them for one.
	     */
            break;

          case LAYOUT_NOTIFY_DEVICEID: /* Device update Event */
            dev_id.sbid = gpfs_fsal_up_ctx->gf_exp_id;/* override with export id */
            LogDebug(COMPONENT_FSAL_UP,
                     "layout device update: flags:%x ino %ld devid %ld-%016lx",
                     flags, callback.buf->st_ino, dev_id.sbid, dev_id.devid);

	    rc = up_async_notify_device(general_fridge,
					gpfs_fsal_up_ctx->gf_export,
					NOTIFY_DEVICEID4_DELETE_MASK,
					LAYOUT4_NFSV4_1_FILES,
					dev_id.devid,
					true,
				 	NULL, NULL);
            break;

          case INODE_UPDATE: /* Update Event */
	  {
	    struct attrlist attr;

            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode update: flags:%x update ino %ld n_link:%d",
                        flags, callback.buf->st_ino, (int)callback.buf->st_nlink);

            /* Check for accepted flags, any other changes just invalidate. */
            if (flags & (UP_SIZE|UP_NLINK|UP_MODE|UP_OWN|UP_TIMES|UP_ATIME))
	      {
		uint32_t upflags = 0;
		attr.mask = 0;
		if (flags & UP_NLINK)
		  upflags |= fsal_up_nlink;
		if (flags & UP_SIZE)
		  attr.mask |= ATTR_CHGTIME|ATTR_CHANGE|ATTR_SIZE|ATTR_SPACEUSED;
		if (flags & UP_MODE)
		  attr.mask |= ATTR_CHGTIME|ATTR_CHANGE|ATTR_MODE;
		if (flags & UP_OWN)
		  attr.mask |= ATTR_CHGTIME|ATTR_CHANGE|ATTR_OWNER;
		if (flags & UP_TIMES)
		  attr.mask |= ATTR_CHGTIME|ATTR_CHANGE|ATTR_ATIME|ATTR_CTIME|ATTR_MTIME;
		if (flags & UP_ATIME)
		  attr.mask |= ATTR_CHGTIME|ATTR_CHANGE|ATTR_ATIME;

	        posix2fsal_attributes(&buf, &attr);
		rc = event_func->update(gpfs_fsal_up_ctx->gf_export,
					&key,
					&attr,
					upflags);
	      }
	    else
	      {
		rc = event_func->invalidate(gpfs_fsal_up_ctx->gf_export,
					    &key,
					    CACHE_INODE_INVALIDATE_ATTRS |
					    CACHE_INODE_INVALIDATE_CONTENT);
	      }

	  }
            break;

          case THREAD_STOP: /* GPFS export no longer available */
            LogWarn(COMPONENT_FSAL_UP,
                    "GPFS file system %d is no longer available",
                    gpfs_fsal_up_ctx->gf_fd);
            return NULL;

          case INODE_INVALIDATE:
            LogMidDebug(COMPONENT_FSAL_UP,
                        "inode invalidate: flags:%x update ino %ld",
                        flags, callback.buf->st_ino);
	    rc = event_func->invalidate(gpfs_fsal_up_ctx->gf_export,
					&key,
					CACHE_INODE_INVALIDATE_ATTRS |
					CACHE_INODE_INVALIDATE_CONTENT);
            break;

          default:
            LogWarn(COMPONENT_FSAL_UP,
                        "Unknown event: %d", reason);
            continue;
      }

      if(rc && rc != CACHE_INODE_NOT_FOUND)
        {
          LogWarn(COMPONENT_FSAL_UP,
                  "Event %d could not be processed for fd %d rc %d",
                   reason, gpfs_fsal_up_ctx->gf_fd, rc);
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
