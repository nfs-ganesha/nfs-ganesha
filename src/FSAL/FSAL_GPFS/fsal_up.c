/**
 * @file    fsal_up.c
 * @brief   FSAL Upcall Interface
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

#include "config.h"
#include "fsal.h"
#include "fsal_up.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"
#include "nfs_init.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>
#include <urcu-bp.h>
#include "FSAL/fsal_localfs.h"

/* Setup up_vector. File system's upvector_mutex must be held */
static bool setup_up_vector(struct gpfs_filesystem *gpfs_fs)
{
	struct gpfs_filesystem_export_map *map;

	map = glist_first_entry(&gpfs_fs->exports,
				struct gpfs_filesystem_export_map, on_exports);
	if (!map)
		return false;

	gpfs_fs->up_vector = (struct fsal_up_vector *)map->exp->export.up_ops;

	/* wait for upcall readiness */
	up_ready_wait(gpfs_fs->up_vector);

	return true;
}

/**
 * @brief Up Thread
 *
 * @param Arg reference to void
 *
 */
void *GPFSFSAL_UP_Thread(void *Arg)
{
	struct gpfs_filesystem *gpfs_fs = Arg;
	struct fsal_up_vector *event_func;
	char thr_name[16];
	int rc = 0;
	struct pnfs_deviceid devid;
	struct stat buf;
	struct glock fl;
	struct callback_arg callback;
	struct gpfs_file_handle handle;
	int reason = 0;
	int flags = 0;
	unsigned int *fhP;
	int retry = 0;
	struct gsh_buffdesc key;
	uint32_t expire_time_attr = 0;
	uint32_t upflags;
	int errsv = 0;
	fsal_status_t fsal_status = {0,};
	struct req_op_context op_context;

	rcu_register_thread();

#ifdef _VALGRIND_MEMCHECK
		memset(&handle, 0, sizeof(handle));
		memset(&buf, 0, sizeof(buf));
		memset(&fl, 0, sizeof(fl));
		memset(&devid, 0, sizeof(devid));
#endif

	(void) snprintf(thr_name, sizeof(thr_name),
			"fsal_up_%"PRIu64".%"PRIu64,
			gpfs_fs->fs->dev.major, gpfs_fs->fs->dev.minor);
	SetNameFunction(thr_name);

	LogFullDebug(COMPONENT_FSAL_UP,
		     "Initializing FSAL Callback context for %d.",
		     gpfs_fs->root_fd);

	/* wait for nfs init completion to get general_fridge
	 * initialized which is needed for processing some upcall events
	 */
	while (1) {
		rc = nfs_init_wait_timeout(1);

		/* First check if the thread needs to be stopped */
		if (gpfs_fs->stop_thread)
			return NULL;
		if (rc == 0)
			break;
		else if (rc == ETIMEDOUT)
			continue;
		else {
			LogEvent(COMPONENT_FSAL_UP,
				 "nfs_init_wait_timeout() completed with rc %d",
				 rc);
			return NULL;
		}
	}

	/* Start querying for events and processing. */
	while (1) {
		LogFullDebug(COMPONENT_FSAL_UP,
			     "Requesting event from FSAL Callback interface for %d.",
			     gpfs_fs->root_fd);

		handle.handle_size = GPFS_MAX_FH_SIZE;
		handle.handle_key_size = OPENHANDLE_KEY_LEN;
		handle.handle_version = OPENHANDLE_VERSION;

		callback.interface_version =
		    GPFS_INTERFACE_VERSION + GPFS_INTERFACE_SUB_VER;

		callback.mountdirfd = gpfs_fs->root_fd;
		callback.handle = &handle;
		callback.reason = &reason;
		callback.flags = &flags;
		callback.buf = &buf;
		callback.fl = &fl;
		callback.dev_id = &devid;
		callback.expire_attr = &expire_time_attr;

		rc = gpfs_ganesha(OPENHANDLE_INODE_UPDATE, &callback);
		errsv = errno;

		if (rc != 0) {
			rc = -(rc);
			if (rc > GPFS_INTERFACE_VERSION) {
				LogFatal(COMPONENT_FSAL_UP,
					 "Ganesha version %d mismatch GPFS version %d.",
					 callback.interface_version, rc);
				goto out;
			}

			if (errsv == EINTR)
				continue;

			LogCrit(COMPONENT_FSAL_UP,
				"OPENHANDLE_INODE_UPDATE failed for %d. rc %d, errno %d (%s) reason %d",
				gpfs_fs->root_fd, rc, errsv,
				strerror(errsv), reason);

			/* @todo 1000 retry logic will go away once the
			 * OPENHANDLE_INODE_UPDATE ioctl separates EINTR
			 * and EUNATCH.
			 */
			if (errsv == EUNATCH && ++retry > 1000)
				LogFatal(COMPONENT_FSAL_UP,
					 "GPFS file system %d has gone away.",
					 gpfs_fs->root_fd);

			continue;
		}

		retry = 0;

		/* flags is int, but only the least significant 2 bytes
		 * are valid.  We are getting random bits into the upper
		 * 2 bytes! Workaround this until the kernel module
		 * gets fixed.
		 */
		flags = flags & 0xffff;

		LogDebug(COMPONENT_FSAL_UP,
			 "inode update: rc %d reason %d update ino %"
			 PRId64 " flags:%x",
			 rc, reason, callback.buf->st_ino, flags);

		LogFullDebug(COMPONENT_FSAL_UP,
			     "inode update: flags:%x callback.handle:%p handle size = %u handle_type:%d handle_version:%d key_size = %u handle_fsid=%X.%X f_handle:%p expire: %d",
			     *callback.flags, callback.handle,
			     callback.handle->handle_size,
			     callback.handle->handle_type,
			     callback.handle->handle_version,
			     callback.handle->handle_key_size,
			     callback.handle->handle_fsid[0],
			     callback.handle->handle_fsid[1],
			     callback.handle->f_handle, expire_time_attr);

		callback.handle->handle_version = OPENHANDLE_VERSION;

		fhP = (int *)&(callback.handle->f_handle[0]);
		LogFullDebug(COMPONENT_FSAL_UP,
			     " inode update: handle %08x %08x %08x %08x %08x %08x %08x",
			     fhP[0], fhP[1], fhP[2], fhP[3], fhP[4], fhP[5],
			     fhP[6]);

		/* Here is where we decide what type of event this is
		 * ... open,close,read,...,invalidate? */
		key.addr = &handle;
		key.len = handle.handle_key_size;

		LogDebug(COMPONENT_FSAL_UP, "Received event to process for %d",
			 gpfs_fs->root_fd);

		/* We need valid up_vector while processing some of the
		 * events below. Setup up vector and hold the mutex while
		 * processing the event for the entire duration.
		 */
		PTHREAD_MUTEX_lock(&gpfs_fs->upvector_mutex);
		if (!setup_up_vector(gpfs_fs)) {
			PTHREAD_MUTEX_unlock(&gpfs_fs->upvector_mutex);
			goto out;
		}

		/* Get a ref to the gpfs_fs->up_vector->up_gsh_export and
		 * initialize op_context for the thread.
		 */
		get_gsh_export_ref(gpfs_fs->up_vector->up_gsh_export);
		init_op_context_simple(&op_context,
				       gpfs_fs->up_vector->up_gsh_export,
				       gpfs_fs->up_vector->up_fsal_export);

		event_func = gpfs_fs->up_vector;

		switch (reason) {
		case INODE_LOCK_GRANTED:	/* Lock Event */
		case INODE_LOCK_AGAIN:	/* Lock Event */
			{
				LogMidDebug(COMPONENT_FSAL_UP,
					    "%s: owner %p pid %d type %d start %lld len %lld",
					    reason ==
					    INODE_LOCK_GRANTED ?
					    "inode lock granted" :
					    "inode lock again", fl.lock_owner,
					    fl.flock.l_pid, fl.flock.l_type,
					    (long long)fl.flock.l_start,
					    (long long)fl.flock.l_len);

				fsal_lock_param_t lockdesc = {
					.lock_sle_type = FSAL_POSIX_LOCK,
					.lock_type = fl.flock.l_type,
					.lock_start = fl.flock.l_start,
					.lock_length = fl.flock.l_len
				};
				if (reason == INODE_LOCK_AGAIN)
					fsal_status = up_async_lock_avail(
							 general_fridge,
							 event_func,
							 &key,
							 fl.lock_owner,
							 &lockdesc, NULL, NULL);
				else
					fsal_status = up_async_lock_grant(
							 general_fridge,
							 event_func,
							 &key,
							 fl.lock_owner,
							 &lockdesc, NULL, NULL);
			}
			break;

		case BREAK_DELEGATION:	/* Delegation Event */
			LogDebug(COMPONENT_FSAL_UP,
				 "delegation recall: flags:%x ino %" PRId64,
				 flags, callback.buf->st_ino);
			fsal_status = up_async_delegrecall(general_fridge,
						  event_func,
						  &key, NULL, NULL);
			break;

		case LAYOUT_FILE_RECALL:	/* Layout file recall Event */
			{
				struct pnfs_segment segment = {
					.offset = 0,
					.length = UINT64_MAX,
					.io_mode = LAYOUTIOMODE4_ANY
				};
				LogDebug(COMPONENT_FSAL_UP,
					 "layout file recall: flags:%x ino %"
					 PRId64, flags, callback.buf->st_ino);

				fsal_status = up_async_layoutrecall(
							general_fridge,
							event_func,
							&key,
							LAYOUT4_NFSV4_1_FILES,
							false, &segment,
							NULL, NULL, NULL,
							NULL);
			}
			break;

		case LAYOUT_RECALL_ANY:	/* Recall all layouts Event */
			LogDebug(COMPONENT_FSAL_UP,
				 "layout recall any: flags:%x ino %" PRId64,
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

		case LAYOUT_NOTIFY_DEVICEID:	/* Device update Event */
			LogDebug(COMPONENT_FSAL_UP,
				 "layout dev update: flags:%x ino %"
				 PRId64 " seq %d fd %d fsid 0x%" PRIx64,
				 flags,
				callback.buf->st_ino,
				devid.device_id2,
				devid.device_id4,
				devid.devid);

			memset(&devid, 0, sizeof(devid));
			devid.fsal_id = FSAL_ID_GPFS;

			fsal_status = up_async_notify_device(general_fridge,
						event_func,
						NOTIFY_DEVICEID4_DELETE_MASK,
						LAYOUT4_NFSV4_1_FILES,
						&devid,
						true, NULL,
						NULL);
			break;

		case INODE_UPDATE:	/* Update Event */
			{
				struct fsal_attrlist attr;

				LogMidDebug(COMPONENT_FSAL_UP,
					    "inode update: flags:%x update ino %"
					    PRId64 " n_link:%d",
					    flags, callback.buf->st_ino,
					    (int)callback.buf->st_nlink);

				/** @todo: This notification is completely
				 * asynchronous.  If we happen to change some
				 * of the attributes later, we end up over
				 * writing those with these possibly stale
				 * values as we don't know when we get to
				 * update with these up call values. We should
				 * probably use time stamp or let the up call
				 * always provide UP_TIMES flag in which case
				 * we can compare the current ctime vs up call
				 * provided ctime before updating the
				 * attributes.
				 *
				 * For now, we think size attribute is more
				 * important than others, so invalidate the
				 * attributes and let ganesha fetch attributes
				 * as needed if this update includes a size
				 * change. We are careless for other attribute
				 * changes, and we may end up with stale values
				 * until this gets fixed!
				 */
				if (flags & (UP_SIZE | UP_SIZE_BIG)) {
					fsal_status = event_func->invalidate(
						event_func, &key,
						FSAL_UP_INVALIDATE_CACHE);
					break;
				}

				/* Check for accepted flags, any other changes
				   just invalidate. */
				if (flags &
				    ~(UP_SIZE | UP_NLINK | UP_MODE | UP_OWN |
				     UP_TIMES | UP_ATIME | UP_SIZE_BIG)) {
					fsal_status = event_func->invalidate(
						event_func, &key,
						FSAL_UP_INVALIDATE_CACHE);
				} else {
					/* buf may not have all attributes set.
					 * Set the mask to what is changed
					 */
					attr.valid_mask = 0;
					attr.acl = NULL;
					upflags = 0;
					if (flags & UP_SIZE)
						attr.valid_mask |=
						   ATTR_CHANGE |
						   ATTR_SIZE | ATTR_SPACEUSED;
					if (flags & UP_SIZE_BIG) {
						attr.valid_mask |=
						   ATTR_CHANGE |
						   ATTR_SIZE | ATTR_SPACEUSED;
						upflags |=
						   fsal_up_update_filesize_inc |
						   fsal_up_update_spaceused_inc;
					}
					if (flags & UP_MODE)
						attr.valid_mask |=
						   ATTR_CHANGE |
						   ATTR_MODE;
					if (flags & UP_OWN)
						attr.valid_mask |=
						   ATTR_CHANGE |
						   ATTR_OWNER | ATTR_GROUP |
						   ATTR_MODE;
					if (flags & UP_TIMES)
						attr.valid_mask |=
						   ATTR_CHANGE |
						   ATTR_ATIME | ATTR_CTIME |
						    ATTR_MTIME;
					if (flags & UP_ATIME)
						attr.valid_mask |=
						   ATTR_CHANGE |
						   ATTR_ATIME;
					if (flags & UP_NLINK)
						attr.valid_mask |=
							ATTR_NUMLINKS;
					attr.request_mask = attr.valid_mask;

					attr.expire_time_attr =
					    expire_time_attr;

					posix2fsal_attributes(&buf, &attr);
					fsal_status = event_func->update(
							event_func, &key,
							&attr, upflags);

					if ((flags & UP_NLINK)
					    && (attr.numlinks == 0)) {
						upflags = fsal_up_nlink;
						attr.valid_mask = 0;
						attr.request_mask = 0;
						fsal_status = up_async_update
						    (general_fridge,
						     event_func,
						     &key, &attr,
						     upflags, NULL, NULL);
					}
				}
			}
			break;

		case THREAD_STOP:  /* We wanted to terminate this thread */
			LogDebug(COMPONENT_FSAL_UP,
				"Terminating the GPFS up call thread for %d",
				gpfs_fs->root_fd);
			release_op_context();
			PTHREAD_MUTEX_unlock(&gpfs_fs->upvector_mutex);
			goto out;

		case INODE_INVALIDATE:
			LogMidDebug(COMPONENT_FSAL_UP,
				    "inode invalidate: flags:%x update ino %"
				    PRId64, flags, callback.buf->st_ino);

			upflags = FSAL_UP_INVALIDATE_CACHE;
			fsal_status = event_func->invalidate_close(
						event_func,
						&key,
						upflags);
			break;

		case THREAD_PAUSE:
			/* File system image is probably going away, but
			 * we don't need to do anything here as we
			 * eventually get other errors that stop this
			 * thread.
			 */
			release_op_context();
			PTHREAD_MUTEX_unlock(&gpfs_fs->upvector_mutex);
			continue; /* get next event */

		default:
			release_op_context();
			PTHREAD_MUTEX_unlock(&gpfs_fs->upvector_mutex);
			LogWarn(COMPONENT_FSAL_UP, "Unknown event: %d", reason);
			continue;
		}

		release_op_context();
		PTHREAD_MUTEX_unlock(&gpfs_fs->upvector_mutex);

		if (FSAL_IS_ERROR(fsal_status) &&
		    fsal_status.major != ERR_FSAL_NOENT) {
			LogWarn(COMPONENT_FSAL_UP,
				"Event %d could not be processed for fd %d rc %s",
				reason, gpfs_fs->root_fd,
				fsal_err_txt(fsal_status));
		}
	}
out:
	rcu_unregister_thread();
	return NULL;
}				/* GPFSFSAL_UP_Thread */
