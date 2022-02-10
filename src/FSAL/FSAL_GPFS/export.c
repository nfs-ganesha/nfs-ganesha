/** @file export.c
 *  @brief GPFS FSAL module export functions.
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#include "config.h"

#include <fcntl.h>
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statfs.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_localfs.h"
#include "gpfs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "pnfs_utils.h"
#include "include/gpfs.h"

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_export *myself =
	    container_of(exp_hdl, struct gpfs_fsal_export, export);

	gpfs_unexport_filesystems(myself);
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);
	close(myself->export_fd);

	gsh_free(myself);		/* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	fsal_status_t status;
	struct statfs buffstatgpfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	if (!infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	status = GPFSFSAL_statfs(export_fd, obj_hdl, &buffstatgpfs);
	if (FSAL_IS_ERROR(status))
		return status;

	infop->total_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_blocks;
	infop->free_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bfree;
	infop->avail_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bavail;
	infop->total_files = buffstatgpfs.f_files;
	infop->free_files = buffstatgpfs.f_ffree;
	infop->avail_files = buffstatgpfs.f_ffree;
	infop->maxread = buffstatgpfs.f_bsize;
	infop->maxwrite = buffstatgpfs.f_bsize;
	infop->time_delta.tv_sec = 0;
	infop->time_delta.tv_nsec = FSAL_DEFAULT_TIME_DELTA_NSEC;

 out:
	return fsalstat(fsal_error, 0);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	attrmask_t supported_mask;
	struct gpfs_fsal_export *gpfs_export;

	gpfs_export = container_of(exp_hdl, struct gpfs_fsal_export, export);
	supported_mask = fsal_supported_attrs(&exp_hdl->fsal->fs_info);

	/* Fixup supported_mask to indicate if ACL is actually supported for
	 * this export.
	 */
	if (gpfs_export->use_acl)
		supported_mask |= ATTR_ACL;
	else
		supported_mask &= ~ATTR_ACL;

	return supported_mask;
}

/* get_quota
 * return quotas for this export.
 * path could cross a lower mount boundary which could
 * mask lower mount values with those of the export root
 * if this is a real issue, we can scan each time with setmntent()
 * better yet, compare st_dev of the file with st_dev of root_fd.
 * on linux, can map st_dev -> /proc/partitions name -> /dev/<name>
 */

static fsal_status_t
get_quota(struct fsal_export *exp_hdl, const char *filepath, int quota_type,
	  int quota_id, fsal_quota_t *fsal_quota)
{
	gpfs_quotaInfo_t gpfs_quota = {0};
	struct stat path_stat;
	int retval = 0;
	struct quotactl_arg args;
	struct fsal_filesystem *fs = container_of(exp_hdl,
						  struct gpfs_fsal_export,
						  export)->root_fs;

	if (stat(filepath, &path_stat) < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "GPFS get quota, stat: root_path: %s, errno=(%d) %s",
			 fs->path, retval, strerror(retval));
		return fsalstat(posix2fsal_error(retval), retval);
	}

	if ((major(path_stat.st_dev) != fs->dev.major) ||
	    (minor(path_stat.st_dev) != fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS get quota: crossed mount boundary! root_path: %s, quota path: %s",
			 fs->path, filepath);
		return fsalstat(ERR_FSAL_FAULT, 0);  /* maybe a better error? */
	}

	args.pathname = filepath;
	args.cmd = GPFS_QCMD(Q_GETQUOTA, quota_type);
	args.qid = quota_id;
	args.bufferP = &gpfs_quota;
	if (op_ctx && op_ctx->client)
		args.cli_ip = op_ctx->client->hostaddr_str;

	fsal_set_credentials(&op_ctx->creds);
	if (gpfs_ganesha(OPENHANDLE_QUOTA, &args) < 0)
		retval = errno;
	fsal_restore_ganesha_credentials();

	if (retval)
		return fsalstat(posix2fsal_error(retval), retval);

	fsal_quota->bhardlimit = gpfs_quota.blockHardLimit;
	fsal_quota->bsoftlimit = gpfs_quota.blockSoftLimit;
	fsal_quota->curblocks = gpfs_quota.blockUsage;
	fsal_quota->fhardlimit = gpfs_quota.inodeHardLimit;
	fsal_quota->fsoftlimit = gpfs_quota.inodeSoftLimit;
	fsal_quota->curfiles = gpfs_quota.inodeUsage;
	fsal_quota->btimeleft = gpfs_quota.blockGraceTime;
	fsal_quota->ftimeleft = gpfs_quota.inodeGraceTime;
	fsal_quota->bsize = 1024;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t
set_quota(struct fsal_export *exp_hdl, const char *filepath, int quota_type,
	  int quota_id, fsal_quota_t *fsal_quota, fsal_quota_t *res_quota)
{
	gpfs_quotaInfo_t gpfs_quota = {0};
	struct stat path_stat;
	int retval = 0;
	struct quotactl_arg args;
	struct fsal_filesystem *fs = container_of(exp_hdl,
						  struct gpfs_fsal_export,
						  export)->root_fs;

	if (stat(filepath, &path_stat) < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "GPFS set quota, stat: root_path: %s, errno=(%d) %s",
			 fs->path, retval, strerror(retval));
		return fsalstat(posix2fsal_error(retval), retval);
	}

	if ((major(path_stat.st_dev) != fs->dev.major) ||
	    (minor(path_stat.st_dev) != fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS set quota: crossed mount boundary! root_path: %s, quota path: %s",
			 fs->path, filepath);
		return fsalstat(ERR_FSAL_FAULT, 0);  /* maybe a better error? */
	}

	gpfs_quota.blockHardLimit = fsal_quota->bhardlimit;
	gpfs_quota.blockSoftLimit = fsal_quota->bsoftlimit;
	gpfs_quota.inodeHardLimit = fsal_quota->fhardlimit;
	gpfs_quota.inodeSoftLimit = fsal_quota->fsoftlimit;
	gpfs_quota.blockGraceTime = fsal_quota->btimeleft;
	gpfs_quota.inodeGraceTime = fsal_quota->ftimeleft;

	args.pathname = filepath;
	args.cmd = GPFS_QCMD(Q_SETQUOTA, quota_type);
	args.qid = quota_id;
	args.bufferP = &gpfs_quota;
	if (op_ctx && op_ctx->client)
		args.cli_ip = op_ctx->client->hostaddr_str;

	fsal_set_credentials(&op_ctx->creds);
	if (gpfs_ganesha(OPENHANDLE_QUOTA, &args) < 0)
		retval = errno;
	fsal_restore_ganesha_credentials();

	if (retval)
		return fsalstat(posix2fsal_error(retval), retval);

	if (res_quota != NULL)
		return get_quota(exp_hdl, filepath,
				 quota_type, quota_id, res_quota);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.
 */

static fsal_status_t
gpfs_wire_to_host(struct fsal_export *exp_hdl, fsal_digesttype_t in_type,
		  struct gsh_buffdesc *fh_desc, int flags)
{
	struct gpfs_file_handle *hdl;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct gpfs_file_handle *)fh_desc->addr;
	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		hdl->handle_size = bswap_16(hdl->handle_size);
		hdl->handle_type = bswap_16(hdl->handle_type);
		hdl->handle_version = bswap_16(hdl->handle_version);
		hdl->handle_key_size = bswap_16(hdl->handle_key_size);
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		hdl->handle_size = bswap_16(hdl->handle_size);
		hdl->handle_type = bswap_16(hdl->handle_type);
		hdl->handle_version = bswap_16(hdl->handle_version);
		hdl->handle_key_size = bswap_16(hdl->handle_key_size);
#endif
	}
	fh_size = gpfs_sizeof_handle(hdl);
	LogFullDebug(COMPONENT_FSAL,
	  "flags 0x%X size %d type %d ver %d key_size %d FSID 0x%X:%X fh_size %zu",
	   flags, hdl->handle_size, hdl->handle_type, hdl->handle_version,
	   hdl->handle_key_size, hdl->handle_fsid[0], hdl->handle_fsid[1],
	   fh_size);

	/* Some older file handles include additional 16 bytes in fh_desc->len.
	 * Honor those as well.
	 */
	if (fh_desc->len != fh_size &&
	    fh_desc->len != fh_size + 16) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %zu, got %zu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = hdl->handle_size;	/* pass back the size */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Produce handle-key from a host-handle */
static fsal_status_t
gpfs_host_to_key(struct fsal_export *exp_hdl,
		   struct gsh_buffdesc *fh_desc)
{
	struct gpfs_file_handle *hdl;

	if (fh_desc->len < offsetof(struct gpfs_file_handle, f_handle))
		return fsalstat(ERR_FSAL_INVAL, 0);

	hdl = (struct gpfs_file_handle *)fh_desc->addr;
	fh_desc->len = hdl->handle_key_size;	/* pass back the key size */

	LogFullDebug(COMPONENT_FSAL,
		     "size %d type %d ver %d key_size %d FSID 0x%X:%X",
		     hdl->handle_size, hdl->handle_type, hdl->handle_version,
		     hdl->handle_key_size, hdl->handle_fsid[0],
		     hdl->handle_fsid[1]);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */
struct state_t *
gpfs_alloc_state(struct fsal_export *exp_hdl, enum state_type state_type,
		 struct state_t *related_state)
{
	struct state_t *state;
	struct gpfs_fd *my_fd;

	state = init_state(gsh_calloc(1, sizeof(struct gpfs_state_fd)),
			   exp_hdl, state_type, related_state);

	my_fd = &container_of(state, struct gpfs_state_fd, state)->gpfs_fd;

	my_fd->fd = -1;
	my_fd->openflags = FSAL_O_CLOSED;
	PTHREAD_RWLOCK_init(&my_fd->fdlock, NULL);

	return state;
}

/**
 * @brief free a gpfs_state_fd structure
 *
 * @param[in] exp_hdl  Export state_t will be associated with
 * @param[in] state    Related state if appropriate
 *
 */
void
gpfs_free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	struct gpfs_state_fd *state_fd = container_of(state,
						      struct gpfs_state_fd,
						      state);
	struct gpfs_fd *my_fd = &state_fd->gpfs_fd;

	PTHREAD_RWLOCK_destroy(&my_fd->fdlock);
	gsh_free(state_fd);
}

/**
 *  @brief overwrite vector entries with the methods that we support
 *  @param ops tpye of struct export_ops
 */
void gpfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = gpfs_lookup_path;
	ops->wire_to_host = gpfs_wire_to_host;
	ops->host_to_key = gpfs_host_to_key;
	ops->create_handle = gpfs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->get_quota = get_quota;
	ops->set_quota = set_quota;
	ops->alloc_state = gpfs_alloc_state;
	ops->free_state = gpfs_free_state;
}

static void free_gpfs_filesystem(struct gpfs_filesystem *gpfs_fs)
{
	if (gpfs_fs->root_fd >= 0)
		close(gpfs_fs->root_fd);
	PTHREAD_MUTEX_destroy(&gpfs_fs->upvector_mutex);
	gsh_free(gpfs_fs);
}

/**
 *  @brief Extract major from fsid
 *  @param fh GPFS file handle
 *  @param fsid FSAL ID
 */
void gpfs_extract_fsid(struct gpfs_file_handle *fh, struct fsal_fsid__ *fsid)
{
	memcpy(&fsid->major, fh->handle_fsid, sizeof(fsid->major));
	fsid->minor = 0;
}

/**
 *  @brief Open root fd
 *  @param gpfs_fs GPFS filesystem
 *  @return 0(zero) on success, otherwise error.
 */
int open_root_fd(struct gpfs_filesystem *gpfs_fs)
{
	struct fsal_fsid__ fsid;
	int retval;
	fsal_status_t status;
	struct gpfs_file_handle fh = {0};

	gpfs_fs->root_fd = open(gpfs_fs->fs->path, O_RDONLY | O_DIRECTORY);

	if (gpfs_fs->root_fd < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Could not open GPFS mount point %s: rc = %s (%d)",
			 gpfs_fs->fs->path, strerror(retval), retval);
		return retval;
	}
	LogFullDebug(COMPONENT_FSAL, "root export_fd %d path %s",
				gpfs_fs->root_fd, gpfs_fs->fs->path);

	status = fsal_internal_get_handle_at(gpfs_fs->root_fd,
					     gpfs_fs->fs->path, &fh,
					     gpfs_fs->root_fd);

	if (FSAL_IS_ERROR(status)) {
		retval = status.minor;
		LogMajor(COMPONENT_FSAL,
			 "Get root handle for %s failed with %s (%d)",
			 gpfs_fs->fs->path, strerror(retval), retval);
		goto errout;
	}

	gpfs_extract_fsid(&fh, &fsid);

	retval = re_index_fs_fsid(gpfs_fs->fs, GPFS_FSID_TYPE, &fsid);

	if (retval == 0)
		return 0;

	LogCrit(COMPONENT_FSAL,
		"Could not re-index GPFS file system fsid for %s, error:%d",
		gpfs_fs->fs->path, retval);
	assert(retval < 0);
	retval = -retval;

errout:
	close(gpfs_fs->root_fd);
	gpfs_fs->root_fd = -1;

	return retval;
}

/**
 *  @brief Claim GPFS filesystem
 *  @param fs FSAL filesystem
 *  @param exp FSAL export
 *  @return 0(zero) on success, otherwise error.
 */
int gpfs_claim_filesystem(struct fsal_filesystem *fs,
			  struct fsal_export *exp,
			  void **private_data)
{
	struct gpfs_filesystem *gpfs_fs;
	int retval;
	struct gpfs_filesystem_export_map *map;
	pthread_attr_t attr_thr;

	if (strcmp(fs->type, "gpfs") != 0) {
		LogEvent(COMPONENT_FSAL,
			"Attempt to claim non-GPFS filesystem %s", fs->path);
		return ENXIO;
	}

	if (fs->fsal != NULL && fs->private_data == NULL)
		LogFatal(COMPONENT_FSAL,
			"Something wrong with export, fs %s appears already claimed but doesn't have private data",
			fs->path);

	if (fs->fsal == NULL && fs->private_data != NULL)
		LogFatal(COMPONENT_FSAL,
			"Something wrong with export, fs %s was not claimed but had non-NULL private",
			fs->path);

	gpfs_fs = fs->private_data;
	if (!gpfs_fs) { /* first export */
		gpfs_fs = gsh_calloc(1, sizeof(*gpfs_fs));
		glist_init(&gpfs_fs->exports);
		gpfs_fs->root_fd = -1;
		gpfs_fs->fs = fs;
		PTHREAD_MUTEX_init(&gpfs_fs->upvector_mutex, NULL);
	}

	/* Now map the file system and export */
	map = gsh_calloc(1, sizeof(*map));
	map->fs = gpfs_fs;
	map->exp = container_of(exp, struct gpfs_fsal_export, export);
	PTHREAD_MUTEX_lock(&gpfs_fs->upvector_mutex);
	glist_add_tail(&gpfs_fs->exports, &map->on_exports);
	glist_add_tail(&map->exp->filesystems, &map->on_filesystems);
	PTHREAD_MUTEX_unlock(&gpfs_fs->upvector_mutex);

	map->exp->export_fd = open(CTX_FULLPATH(op_ctx),
				   O_RDONLY | O_DIRECTORY);
	if (map->exp->export_fd < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			"Could not open GPFS export point %s: rc = %s (%d)",
			CTX_FULLPATH(op_ctx), strerror(retval), retval);
		goto errout;
	}

	LogFullDebug(COMPONENT_FSAL, "export_fd %d path %s",
			map->exp->export_fd, CTX_FULLPATH(op_ctx));

	/* We have set up the export. If the file system is already claimed,
	 * we are done.
	 */
	if (fs->private_data) /* file system is already claimed */
		return 0;

	/* Get an fd for the root and create an upcall thread */
	retval = open_root_fd(gpfs_fs);
	if (retval != 0) {
		if (retval == ENOTTY) {
			LogInfo(COMPONENT_FSAL,
				"file system %s is not exportable with %s",
				fs->path, exp->fsal->name);
			retval = ENXIO;
		}
		goto errout;
	}

	gpfs_fs->stop_thread = false;

	if (pthread_attr_init(&attr_thr) != 0)
		LogCrit(COMPONENT_THREAD, "can't init pthread's attributes");

	if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
		LogCrit(COMPONENT_THREAD, "can't set pthread's scope");

	if (pthread_attr_setdetachstate(&attr_thr,
					PTHREAD_CREATE_JOINABLE) != 0)
		LogCrit(COMPONENT_THREAD, "can't set pthread's join state");

	if (pthread_attr_setstacksize(&attr_thr, 2116488) != 0)
		LogCrit(COMPONENT_THREAD, "can't set pthread's stack size");

	if (pthread_create(&gpfs_fs->up_thread, &attr_thr, GPFSFSAL_UP_Thread,
			   gpfs_fs)) {
		retval = errno;
		LogCrit(COMPONENT_THREAD,
			"Could not create GPFSFSAL_UP_Thread, error = %d (%s)",
			retval, strerror(retval));
		goto errout;
	}

	*private_data = gpfs_fs;

	return 0;

errout:
	if (map->exp->export_fd >= 0) {
		close(map->exp->export_fd);
		map->exp->export_fd = -1;
	}
	PTHREAD_MUTEX_lock(&gpfs_fs->upvector_mutex);
	glist_del(&map->on_filesystems);
	glist_del(&map->on_exports);
	PTHREAD_MUTEX_unlock(&gpfs_fs->upvector_mutex);
	gsh_free(map);
	if (!fs->private_data)
		free_gpfs_filesystem(gpfs_fs);

	return retval;
}

/**
 *  @brief Unclaim filesystem
 *  @param fs FSAL filesystem
 */
void gpfs_unclaim_filesystem(struct fsal_filesystem *fs)
{
	struct gpfs_filesystem *gpfs_fs = fs->private_data;
	struct glist_head *glist, *glistn;
	struct gpfs_filesystem_export_map *map;
	struct callback_arg callback = {0};
	int reason = THREAD_STOP;

	if (gpfs_fs == NULL)
		goto out;

	glist_for_each_safe(glist, glistn, &gpfs_fs->exports) {
		map = glist_entry(glist, struct gpfs_filesystem_export_map,
				  on_exports);

		/* Remove this file system from mapping */
		PTHREAD_MUTEX_lock(&map->fs->upvector_mutex);
		glist_del(&map->on_filesystems);
		glist_del(&map->on_exports);
		PTHREAD_MUTEX_unlock(&map->fs->upvector_mutex);

		if (map->exp->root_fs == fs)
			LogInfo(COMPONENT_FSAL,
				"Removing root_fs %s from GPFS export",
				fs->path);

		/* And free it */
		gsh_free(map);
	}

	/* Terminate GPFS upcall thread */
	callback.mountdirfd = gpfs_fs->root_fd;
	callback.reason = &reason;

	if (gpfs_ganesha(OPENHANDLE_THREAD_UPDATE, &callback))
		LogCrit(COMPONENT_FSAL,
			"Unable to stop upcall thread for %s, fd=%d, errno=%d",
			fs->path, gpfs_fs->root_fd, errno);
	else
		LogFullDebug(COMPONENT_FSAL, "Thread STOP successful");

	/* Prior to calling pthread_join(), we should set stop_thread=true.
	 * Its not being set atomically because the synchronization requirement
	 * is not critical enough.
	 */
	gpfs_fs->stop_thread = true;

	pthread_join(gpfs_fs->up_thread, NULL);
	free_gpfs_filesystem(gpfs_fs);
	fs->private_data = NULL;

out:
	LogInfo(COMPONENT_FSAL, "GPFS Unclaiming %s", fs->path);
}

/**
 *  @brief Unexport filesystem
 *  @param exp FSAL export
 */
void gpfs_unexport_filesystems(struct gpfs_fsal_export *exp)
{
	struct glist_head *glist, *glistn;
	struct gpfs_filesystem_export_map *map;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	glist_for_each_safe(glist, glistn, &exp->filesystems) {
		map = glist_entry(glist, struct gpfs_filesystem_export_map,
				  on_filesystems);

		/* Remove this export from mapping */
		PTHREAD_MUTEX_lock(&map->fs->upvector_mutex);
		glist_del(&map->on_filesystems);
		glist_del(&map->on_exports);
		PTHREAD_MUTEX_unlock(&map->fs->upvector_mutex);

		if (glist_empty(&map->fs->exports)) {
			LogInfo(COMPONENT_FSAL,
				"GPFS is no longer exporting filesystem %s",
				map->fs->fs->path);
			//unclaim_fs(map->fs->fs);
		}

		/* And free it */
		gsh_free(map);
	}

	PTHREAD_RWLOCK_unlock(&fs_lock);
}

/* GPFS FSAL export config */
static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_BOOL("ignore_mode_change", false,
		       gpfs_fsal_export, ignore_mode_change),
	CONFIG_EOL
};


static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.gpfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};


/**
 * @brief create_export
 *
 *  Create an export point and return a handle to it to be kept
 *  in the export list.
 *  First lookup the fsal, then create the export and then put the fsal back.
 *  returns the export with one reference taken.
 *
 *  @return FSAL status
 */
fsal_status_t
gpfs_create_export(struct fsal_module *fsal_hdl, void *parse_node,
		   struct config_error_type *err_type,
		   const struct fsal_up_vector *up_ops)
{
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct gpfs_fsal_export *gpfs_exp;
	struct fsal_export *exp;
	int rc;

	gpfs_exp = gsh_calloc(1, sizeof(struct gpfs_fsal_export));
	exp = &gpfs_exp->export;

	glist_init(&gpfs_exp->filesystems);

	status.minor = fsal_internal_version();
	LogInfo(COMPONENT_FSAL, "GPFS get version is %d options 0x%X id %d",
		status.minor, op_ctx->export_perms.options,
		op_ctx->ctx_export->export_id);

	fsal_export_init(exp);
	gpfs_export_ops_init(&exp->exp_ops);

	/* Load GPFS FSAL specific export config */
	rc = load_config_from_node(parse_node,
				   &export_param,
				   gpfs_exp,
				   true,
				   err_type);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Incorrect or missing parameters for export %s",
			CTX_FULLPATH(op_ctx));
		status.major = ERR_FSAL_INVAL;
		goto free;
	}

	status.minor = fsal_attach_export(fsal_hdl, &exp->exports);
	if (status.minor != 0) {
		status.major = posix2fsal_error(status.minor);
		goto free;	/* seriously bad */
	}

	exp->fsal = fsal_hdl;
	exp->up_ops = up_ops;
	op_ctx->fsal_export = exp;

	status.minor = resolve_posix_filesystem(CTX_FULLPATH(op_ctx),
						fsal_hdl, exp,
						gpfs_claim_filesystem,
						gpfs_unclaim_filesystem,
						&gpfs_exp->root_fs);

	if (status.minor != 0) {
		LogCrit(COMPONENT_FSAL,
			"resolve_posix_filesystem(%s) returned %s (%d)",
			CTX_FULLPATH(op_ctx),
			strerror(status.minor), status.minor);
		status.major = posix2fsal_error(status.minor);
		goto detach;
	}

	/* if the nodeid has not been obtained, get it now */
	if (!g_nodeid) {
		struct gpfs_filesystem *gpfs_fs =
						gpfs_exp->root_fs->private_data;
		struct grace_period_arg gpa;
		int nodeid;

		gpa.mountdirfd = gpfs_fs->root_fd;

		nodeid = gpfs_ganesha(OPENHANDLE_GET_NODEID, &gpa);
		if (nodeid > 0) {
			g_nodeid = nodeid;
			LogFullDebug(COMPONENT_FSAL, "nodeid %d", g_nodeid);
		} else
			LogCrit(COMPONENT_FSAL,
			    "OPENHANDLE_GET_NODEID failed rc %d", nodeid);
	}

	gpfs_exp->pnfs_ds_enabled =
	    exp->exp_ops.fs_supports(exp, fso_pnfs_ds_supported);

	gpfs_exp->pnfs_mds_enabled =
	    exp->exp_ops.fs_supports(exp, fso_pnfs_mds_supported);

	if (gpfs_exp->pnfs_ds_enabled) {
		struct fsal_pnfs_ds *pds = NULL;

		status = fsal_hdl->m_ops.create_fsal_pnfs_ds(fsal_hdl,
							     parse_node,
							     &pds);

		if (status.major != ERR_FSAL_NO_ERROR)
			goto unexport;

		/* special case: server_id matches export_id */
		pds->id_servers = op_ctx->ctx_export->export_id;
		pds->mds_export = op_ctx->ctx_export;
		pds->mds_fsal_export = op_ctx->fsal_export;

		if (!pnfs_ds_insert(pds)) {
			LogCrit(COMPONENT_CONFIG,
				"Server id %d already in use.",
				pds->id_servers);
			status.major = ERR_FSAL_EXIST;

			/* Return the ref taken by create_fsal_pnfs_ds */
			pnfs_ds_put(pds);
			goto unexport;
		}

		LogInfo(COMPONENT_FSAL,
			"gpfs_fsal_create: pnfs ds was enabled for [%s]",
			CTX_FULLPATH(op_ctx));
		export_ops_pnfs(&exp->exp_ops);
	}
	gpfs_exp->use_acl =
		!op_ctx_export_has_option(EXPORT_OPTION_DISABLE_ACL);

	return status;

unexport:
	gpfs_unexport_filesystems(gpfs_exp);
detach:
	fsal_detach_export(fsal_hdl, &exp->exports);
free:
	free_export_ops(exp);
	gsh_free(gpfs_exp);
	return status;
}
