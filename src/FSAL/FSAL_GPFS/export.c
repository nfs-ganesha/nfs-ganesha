/*
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

/* export.c
 * GPFS FSAL export object
 */

#include "config.h"

#include <fcntl.h>
#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statfs.h>
#include <sys/quota.h>
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "gpfs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_export *myself;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);

	gpfs_unexport_filesystems(myself);
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself);		/* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	fsal_status_t status;
	struct statfs buffstatgpfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct gpfs_filesystem *gpfs_fs;

	if (!infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	gpfs_fs = obj_hdl->fs->private;

	status = GPFSFSAL_statfs(gpfs_fs->root_fd, obj_hdl, &buffstatgpfs);
	if (FSAL_IS_ERROR(status))
		return status;

	infop->total_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_blocks;
	infop->free_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bfree;
	infop->avail_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bavail;
	infop->total_files = buffstatgpfs.f_files;
	infop->free_files = buffstatgpfs.f_ffree;
	infop->avail_files = buffstatgpfs.f_ffree;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

 out:
	return fsalstat(fsal_error, 0);
}

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gpfs_staticinfo(exp_hdl->fsal);
	return fsal_xattr_access_rights(info);
}

/* get_quota
 * return quotas for this export.
 * path could cross a lower mount boundary which could
 * mask lower mount values with those of the export root
 * if this is a real issue, we can scan each time with setmntent()
 * better yet, compare st_dev of the file with st_dev of root_fd.
 * on linux, can map st_dev -> /proc/partitions name -> /dev/<name>
 */

static fsal_status_t get_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       fsal_quota_t *pquota)
{
	struct gpfs_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS get_quota, fstat: root_path: %s, errno=(%d) %s",
			 myself->root_fs->path,
			 errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if ((major(path_stat.st_dev) != myself->root_fs->dev.major) ||
	    (minor(path_stat.st_dev) != myself->root_fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS get_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->root_fs->path, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto out;
	}
	id = (quota_type ==
	      USRQUOTA) ? op_ctx->creds->caller_uid : op_ctx->creds->
	    caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));
	retval = quotactl(QCMD(Q_GETQUOTA, quota_type), myself->root_fs->device,
			  id, (caddr_t) &fs_quota);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	pquota->bhardlimit = fs_quota.dqb_bhardlimit;
	pquota->bsoftlimit = fs_quota.dqb_bsoftlimit;
	pquota->curblocks = fs_quota.dqb_curspace;
	pquota->fhardlimit = fs_quota.dqb_ihardlimit;
	pquota->fsoftlimit = fs_quota.dqb_isoftlimit;
	pquota->curfiles = fs_quota.dqb_curinodes;
	pquota->btimeleft = fs_quota.dqb_btime;
	pquota->ftimeleft = fs_quota.dqb_itime;
	pquota->bsize = DEV_BSIZE;

 out:
	return fsalstat(fsal_error, retval);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	struct gpfs_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS set_quota, fstat: root_path: %s, errno=(%d) %s",
			 myself->root_fs->path,
			 errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if ((major(path_stat.st_dev) != myself->root_fs->dev.major) ||
	    (minor(path_stat.st_dev) != myself->root_fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->root_fs->path, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto err;
	}
	id = (quota_type ==
	      USRQUOTA) ? op_ctx->creds->caller_uid : op_ctx->creds->
	    caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));
	if (pquota->bhardlimit != 0) {
		fs_quota.dqb_bhardlimit = pquota->bhardlimit;
		fs_quota.dqb_valid |= QIF_BLIMITS;
	}
	if (pquota->bsoftlimit != 0) {
		fs_quota.dqb_bsoftlimit = pquota->bsoftlimit;
		fs_quota.dqb_valid |= QIF_BLIMITS;
	}
	if (pquota->fhardlimit != 0) {
		fs_quota.dqb_ihardlimit = pquota->fhardlimit;
		fs_quota.dqb_valid |= QIF_ILIMITS;
	}
	if (pquota->fsoftlimit != 0) {
		fs_quota.dqb_isoftlimit = pquota->fsoftlimit;
		fs_quota.dqb_valid |= QIF_ILIMITS;
	}
	if (pquota->btimeleft != 0) {
		fs_quota.dqb_btime = pquota->btimeleft;
		fs_quota.dqb_valid |= QIF_BTIME;
	}
	if (pquota->ftimeleft != 0) {
		fs_quota.dqb_itime = pquota->ftimeleft;
		fs_quota.dqb_valid |= QIF_ITIME;
	}
	retval = quotactl(QCMD(Q_SETQUOTA, quota_type), myself->root_fs->device,
			  id, (caddr_t) &fs_quota);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (presquota != NULL) {
		return get_quota(exp_hdl, filepath, quota_type, presquota);
	}
 err:
	return fsalstat(fsal_error, retval);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t gpfs_extract_handle(struct fsal_export *exp_hdl,
					 fsal_digesttype_t in_type,
					 struct gsh_buffdesc *fh_desc)
{
	struct gpfs_file_handle *hdl;
	size_t fh_size = 0;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct gpfs_file_handle *)fh_desc->addr;
	fh_size = gpfs_sizeof_handle(hdl);
	if (fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = hdl->handle_key_size;	/* pass back the key size */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.  This is also where validation gets done,
 * since PUTFH is the only operation that can return
 * NFS4ERR_BADHANDLE.
 *
 * @param[in]  export_pub The export in which to create the handle
 * @param[in]  desc       Buffer from which to create the file
 * @param[out] ds_pub     FSAL data server handle
 *
 * @return NFSv4.1 error codes.
 */
nfsstat4 gpfs_create_ds_handle(struct fsal_export * const export_pub,
			       const struct gsh_buffdesc * const desc,
			       struct fsal_ds_handle ** const ds_pub)
{
	/* Handle to be created */
	struct gpfs_ds *ds = NULL;
	struct fsal_filesystem *fs;
	enum fsid_type fsid_type;
	struct fsal_fsid__ fsid;
	struct gpfs_file_handle *fh;

	*ds_pub = NULL;

	if (desc->len != sizeof(struct gpfs_file_handle))
		return NFS4ERR_BADHANDLE;

	fh = (struct gpfs_file_handle *) desc->addr;

	gpfs_extract_fsid(fh, &fsid_type, &fsid);

	fs = lookup_fsid(&fsid, fsid_type);

	if (fs == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Could not find filesystem for "
			"fsid=0x%016"PRIx64".0x%016"PRIx64
			" from handle",
			fsid.major, fsid.minor);
		return NFS4ERR_STALE;
	}

	if (fs->fsal != export_pub->fsal) {
		LogInfo(COMPONENT_FSAL,
			"Non GPFS filesystem "
			"fsid=0x%016"PRIx64".0x%016"PRIx64
			" from handle",
			fsid.major, fsid.minor);
		return NFS4ERR_STALE;
	}

	ds = gsh_calloc(1, sizeof(struct gpfs_ds));

	if (ds == NULL)
		return NFS4ERR_SERVERFAULT;

	/* Connect lazily when a FILE_SYNC4 write forces us to, not
	   here. */

	ds->connected = false;
	ds->gpfs_fs = fs->private;

	memcpy(&ds->wire, desc->addr, desc->len);

	fsal_ds_handle_init(&ds->ds, export_pub->ds_ops, export_pub->fsal);

	*ds_pub = &ds->ds;

	return NFS4_OK;
}

verifier4 GPFS_write_verifier;	/* NFS V4 write verifier */

static void gpfs_verifier(struct gsh_buffdesc *verf_desc)
{
	memcpy(verf_desc->addr, &GPFS_write_verifier, verf_desc->len);
}

void set_gpfs_verifier(verifier4 *verifier)
{
	memcpy(&GPFS_write_verifier, verifier, sizeof(verifier4));
}

/* gpfs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void gpfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = gpfs_lookup_path;
	ops->extract_handle = gpfs_extract_handle;
	ops->create_handle = gpfs_create_handle;
	ops->create_ds_handle = gpfs_create_ds_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_lease_time = fs_lease_time;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->fs_xattr_access_rights = fs_xattr_access_rights;
	ops->get_quota = get_quota;
	ops->set_quota = set_quota;
	ops->get_write_verifier = gpfs_verifier;
}

void free_gpfs_filesystem(struct gpfs_filesystem *gpfs_fs)
{
	if (gpfs_fs->root_fd >= 0)
		close(gpfs_fs->root_fd);
	gsh_free(gpfs_fs);
}

void gpfs_extract_fsid(struct gpfs_file_handle *fh,
		       enum fsid_type *fsid_type,
		       struct fsal_fsid__ *fsid)
{
	*fsid_type = FSID_MAJOR_64;
	memcpy(&fsid->major, fh->handle_fsid, sizeof(fsid->major));
	fsid->minor = 0;
}

int open_root_fd(struct gpfs_filesystem *gpfs_fs)
{
	enum fsid_type fsid_type;
	struct fsal_fsid__ fsid;
	int retval;
	fsal_status_t status;
	struct gpfs_file_handle *fh;

	gpfs_alloc_handle(fh);

	gpfs_fs->root_fd = open(gpfs_fs->fs->path, O_RDONLY | O_DIRECTORY);

	if (gpfs_fs->root_fd < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Could not open GPFS mount point %s: rc = %s (%d)",
			 gpfs_fs->fs->path, strerror(retval), retval);
		return retval;
	}

	status = fsal_internal_get_handle_at(gpfs_fs->root_fd,
					     gpfs_fs->fs->path, fh);

	if (FSAL_IS_ERROR(status)) {
		retval = status.minor;
		LogMajor(COMPONENT_FSAL,
			 "Get root handle for %s failed with %s (%d)",
			 gpfs_fs->fs->path, strerror(retval), retval);
		goto errout;
	}

	gpfs_extract_fsid(fh, &fsid_type, &fsid);

	retval = re_index_fs_fsid(gpfs_fs->fs, fsid_type,
				  fsid.major, fsid.minor);

	if (retval < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not re-index GPFS file system fsid for %s",
			gpfs_fs->fs->path);
		retval = -retval;
	}

	return retval;

errout:

	close(gpfs_fs->root_fd);
	gpfs_fs->root_fd = -1;

	return retval;
}

int gpfs_claim_filesystem(struct fsal_filesystem *fs, struct fsal_export *exp)
{
	struct gpfs_filesystem *gpfs_fs = NULL;
	int retval;
	struct gpfs_fsal_export *myself;
	struct gpfs_filesystem_export_map *map = NULL;

	myself = container_of(exp, struct gpfs_fsal_export, export);

	if (strcmp(fs->type, "gpfs") != 0) {
		LogInfo(COMPONENT_FSAL,
			"Attempt to claim non-GPFS filesystem %s",
			fs->path);
		retval = ENXIO;
		return 0;
	}

	map = gsh_calloc(1, sizeof(*map));

	if (map == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Out of memory to claim file system %s",
			fs->path);
		retval = ENOMEM;
		goto errout;
	}

	if (fs->fsal != NULL) {
		gpfs_fs = fs->private;
		if (gpfs_fs == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Something wrong with export, fs %s appears already claimed but doesn't have private data",
				fs->path);
			retval = EINVAL;
			goto errout;
		}

		goto already_claimed;
	}

	if (fs->private != NULL) {
			LogCrit(COMPONENT_FSAL,
				"Something wrong with export, fs %s was not claimed but had non-NULL private",
				fs->path);
	}

	gpfs_fs = gsh_calloc(1, sizeof(*gpfs_fs));

	if (gpfs_fs == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Out of memory to claim file system %s",
			fs->path);
		retval = ENOMEM;
		goto errout;
	}

	glist_init(&gpfs_fs->exports);
	gpfs_fs->root_fd = -1;

	gpfs_fs->fs = fs;

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

	if (gpfs_fs->up_ops == NULL) {
		pthread_attr_t attr_thr;

		memset(&attr_thr, 0, sizeof(attr_thr));

		/* Initialization of thread attributes from nfs_init.c */
		if (pthread_attr_init(&attr_thr) != 0)
			LogCrit(COMPONENT_THREAD,
				"can't init pthread's attributes");

		if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
			LogCrit(COMPONENT_THREAD, "can't set pthread's scope");

		if (pthread_attr_setdetachstate(&attr_thr,
						PTHREAD_CREATE_JOINABLE) != 0)
			LogCrit(COMPONENT_THREAD,
				"can't set pthread's join state");

		if (pthread_attr_setstacksize(&attr_thr, 2116488) != 0)
			LogCrit(COMPONENT_THREAD,
				"can't set pthread's stack size");

		gpfs_fs->up_ops = exp->up_ops;
		retval = pthread_create(&gpfs_fs->up_thread,
					&attr_thr,
					GPFSFSAL_UP_Thread,
					gpfs_fs);

		if (retval != 0) {
			retval = errno;
			LogCrit(COMPONENT_THREAD,
				"Could not create GPFSFSAL_UP_Thread, error = %d (%s)",
				retval, strerror(retval));
			goto errout;
		}
	}

	fs->private = gpfs_fs;

already_claimed:

	/* Now map the file system and export */
	map->fs = gpfs_fs;
	map->exp = myself;
	glist_add_tail(&gpfs_fs->exports, &map->on_exports);
	glist_add_tail(&myself->filesystems, &map->on_filesystems);

	return 0;

errout:

	if (map != NULL)
		gsh_free(map);

	if (gpfs_fs != NULL)
		free_gpfs_filesystem(gpfs_fs);

	return retval;
}

void gpfs_unclaim_filesystem(struct fsal_filesystem *fs)
{
	struct gpfs_filesystem *gpfs_fs = fs->private;
	struct glist_head *glist, *glistn;
	struct gpfs_filesystem_export_map *map;

	if (gpfs_fs != NULL) {
		glist_for_each_safe(glist, glistn, &gpfs_fs->exports) {
			map = glist_entry(glist,
					  struct gpfs_filesystem_export_map,
					  on_exports);

			/* Remove this file system from mapping */
			glist_del(&map->on_filesystems);
			glist_del(&map->on_exports);

			if (map->exp->root_fs == fs) {
				LogInfo(COMPONENT_FSAL,
					"Removing root_fs %s from GPFS export",
					fs->path);
			}

			/* And free it */
			gsh_free(map);
		}

		free_gpfs_filesystem(gpfs_fs);

		fs->private = NULL;
	}

	LogInfo(COMPONENT_FSAL,
		"GPFS Unclaiming %s",
		fs->path);
}

void gpfs_unexport_filesystems(struct gpfs_fsal_export *exp)
{
	struct glist_head *glist, *glistn;
	struct gpfs_filesystem_export_map *map;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	glist_for_each_safe(glist, glistn, &exp->filesystems) {
		map = glist_entry(glist,
				  struct gpfs_filesystem_export_map,
				  on_filesystems);

		/* Remove this export from mapping */
		glist_del(&map->on_filesystems);
		glist_del(&map->on_exports);

		if (glist_empty(&map->fs->exports)) {
			LogInfo(COMPONENT_FSAL,
				"GPFS is no longer exporting filesystem %s",
				map->fs->fs->path);
			unclaim_fs(map->fs->fs);
		}

		/* And free it */
		gsh_free(map);
	}

	PTHREAD_RWLOCK_unlock(&fs_lock);
}

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t gpfs_create_export(struct fsal_module *fsal_hdl,
				 void *parse_node,
				 const struct fsal_up_vector *up_ops)
{
	struct gpfs_fsal_export *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	myself = gsh_malloc(sizeof(struct gpfs_fsal_export));
	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}
	memset(myself, 0, sizeof(struct gpfs_fsal_export));
	glist_init(&myself->filesystems);

	retval = fsal_internal_version();
	LogInfo(COMPONENT_FSAL, "GPFS get version is %d options 0x%X", retval,
		op_ctx->export->export_perms.options);

	retval = fsal_export_init(&myself->export);
	if (retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "out of memory for object");
		gsh_free(myself);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	gpfs_export_ops_init(myself->export.ops);
	gpfs_handle_ops_init(myself->export.obj_ops);
	myself->export.up_ops = up_ops;

	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if (retval != 0)
		goto errout;	/* seriously bad */
	myself->export.fsal = fsal_hdl;

	retval = populate_posix_file_systems();

	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"populate_posix_file_systems returned %s (%d)",
			strerror(retval), retval);
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	retval = claim_posix_filesystems(op_ctx->export->fullpath,
					 fsal_hdl,
					 &myself->export,
					 gpfs_claim_filesystem,
					 gpfs_unclaim_filesystem,
					 &myself->root_fs);

	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"claim_posix_filesystems(%s) returned %s (%d)",
			op_ctx->export->fullpath,
			strerror(retval), retval);
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	op_ctx->fsal_export = &myself->export;

	gpfs_ganesha(OPENHANDLE_GET_VERIFIER, &GPFS_write_verifier);

	myself->pnfs_enabled =
	    myself->export.ops->fs_supports(&myself->export,
					    fso_pnfs_ds_supported);
	if (myself->pnfs_enabled) {
		LogInfo(COMPONENT_FSAL,
			"gpfs_fsal_create: pnfs was enabled for [%s]",
			op_ctx->export->fullpath);
		export_ops_pnfs(myself->export.ops);
		handle_ops_pnfs(myself->export.obj_ops);
		ds_ops_init(myself->export.ds_ops);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	free_export_ops(&myself->export);
	gsh_free(myself);		/* elvis has left the building */
	return fsalstat(fsal_error, retval);
}
