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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* export.c
 * LUSTRE FSAL export object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include "ganesha_list.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_handle.h"
#include "lustre_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"

#ifdef HAVE_INCLUDE_LUSTREAPI_H
#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>
#else
#ifdef HAVE_INCLUDE_LIBLUSTREAPI_H
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>
#endif
#endif

static void lustre_release(struct fsal_export *exp_hdl)
{
	struct lustre_fsal_export *myself;

	myself = container_of(exp_hdl, struct lustre_fsal_export, export);

	lustre_unexport_filesystems(myself);
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself);		/* elvis has left the building */
}

static fsal_status_t lustre_get_dynamic_info(struct fsal_export *exp_hdl,
					     struct fsal_obj_handle *obj_hdl,
					     fsal_dynamicfsinfo_t *infop)
{
	struct statvfs buffstatvfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct lustre_filesystem *lustre_fs;

	if (!infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	lustre_fs = obj_hdl->fs->private;

	retval = statvfs(lustre_fs->fs->path, &buffstatvfs);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	infop->total_bytes = buffstatvfs.f_frsize * buffstatvfs.f_blocks;
	infop->free_bytes = buffstatvfs.f_frsize * buffstatvfs.f_bfree;
	infop->avail_bytes = buffstatvfs.f_frsize * buffstatvfs.f_bavail;
	infop->total_files = buffstatvfs.f_files;
	infop->free_files = buffstatvfs.f_ffree;
	infop->avail_files = buffstatvfs.f_favail;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

 out:
	return fsalstat(fsal_error, retval);
}

static bool lustre_fs_supports(struct fsal_export *exp_hdl,
			       fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t lustre_fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t lustre_fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t lustre_fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t lustre_fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t lustre_fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t lustre_fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec lustre_fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t lustre_fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t lustre_fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t lustre_fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t lustre_fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(exp_hdl->fsal);
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

static fsal_status_t lustre_get_quota(struct fsal_export *exp_hdl,
				      const char *filepath,
				      int quota_type,
				      fsal_quota_t *pquota)
{
	struct lustre_fsal_export *myself;
	struct if_quotactl dataquota;

	struct stat path_stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct lustre_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "LUSTRE get_quota, fstat: root_path: %s, errno=(%d) %s",
			 myself->root_fs->path, errno,
			 strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if ((major(path_stat.st_dev) != myself->root_fs->dev.major) ||
	    (minor(path_stat.st_dev) != myself->root_fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "LUSTRE get_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->root_fs->path, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto out;
	}
	memset((char *)&dataquota, 0, sizeof(struct if_quotactl));

	dataquota.qc_cmd = LUSTRE_Q_GETQUOTA;
	dataquota.qc_type = quota_type;
	dataquota.qc_id =
	    (quota_type ==
	     USRQUOTA) ? op_ctx->creds->caller_uid : op_ctx->creds->
	    caller_gid;

	retval = llapi_quotactl((char *)filepath, &dataquota);

	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	pquota->bsize = 1024;	/* LUSTRE has block of 1024 bytes */
	pquota->bhardlimit = dataquota.qc_dqblk.dqb_bhardlimit;
	pquota->bsoftlimit = dataquota.qc_dqblk.dqb_bsoftlimit;
	pquota->curblocks = dataquota.qc_dqblk.dqb_curspace / pquota->bsize;

	pquota->fhardlimit = dataquota.qc_dqblk.dqb_ihardlimit;
	pquota->fsoftlimit = dataquota.qc_dqblk.dqb_isoftlimit;
	pquota->curfiles = dataquota.qc_dqblk.dqb_isoftlimit;

	/* Times left are set only if used resource
	 * is in-between soft and hard limits */
	if ((pquota->curfiles > pquota->fsoftlimit)
	    && (pquota->curfiles < pquota->fhardlimit))
		pquota->ftimeleft = dataquota.qc_dqblk.dqb_itime;
	else
		pquota->ftimeleft = 0;

	if ((pquota->curblocks > pquota->bsoftlimit)
	    && (pquota->curblocks < pquota->bhardlimit))
		pquota->btimeleft = dataquota.qc_dqblk.dqb_btime;
	else
		pquota->btimeleft = 0;

 out:
	return fsalstat(fsal_error, retval);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t lustre_set_quota(struct fsal_export *exp_hdl,
			       const char *filepath,
			       int quota_type,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	struct lustre_fsal_export *myself;
	struct stat path_stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct if_quotactl dataquota;

	myself = container_of(exp_hdl, struct lustre_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "LUSTRE set_quota, fstat: root_path: %s, errno=(%d) %s",
			 myself->root_fs->path,
			 errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if ((major(path_stat.st_dev) != myself->root_fs->dev.major) ||
	    (minor(path_stat.st_dev) != myself->root_fs->dev.minor)) {
		LogMajor(COMPONENT_FSAL,
			 "LUSTRE set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->root_fs->path, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto err;
	}

	memset((char *)&dataquota, 0, sizeof(struct if_quotactl));
	dataquota.qc_cmd = LUSTRE_Q_GETQUOTA;
	dataquota.qc_type = quota_type;
	dataquota.qc_id =
	    (quota_type ==
	     USRQUOTA) ? op_ctx->creds->caller_uid : op_ctx->creds->
	    caller_gid;

	/* Convert FSAL structure to FS one */
	if (pquota->bhardlimit != 0) {
		dataquota.qc_dqblk.dqb_bhardlimit = pquota->bhardlimit;
		dataquota.qc_dqblk.dqb_valid |= QIF_BLIMITS;
	}

	if (pquota->bsoftlimit != 0) {
		dataquota.qc_dqblk.dqb_bsoftlimit = pquota->bsoftlimit;
		dataquota.qc_dqblk.dqb_valid |= QIF_BLIMITS;
	}

	if (pquota->fhardlimit != 0) {
		dataquota.qc_dqblk.dqb_ihardlimit = pquota->fhardlimit;
		dataquota.qc_dqblk.dqb_valid |= QIF_ILIMITS;
	}

	if (pquota->fsoftlimit != 0) {
		dataquota.qc_dqblk.dqb_isoftlimit = pquota->fsoftlimit;
		dataquota.qc_dqblk.dqb_valid |= QIF_ILIMITS;
	}

	if (pquota->btimeleft != 0) {
		dataquota.qc_dqblk.dqb_btime = pquota->btimeleft;
		dataquota.qc_dqblk.dqb_valid |= QIF_BTIME;
	}

	if (pquota->ftimeleft != 0) {
		dataquota.qc_dqblk.dqb_itime = pquota->ftimeleft;
		dataquota.qc_dqblk.dqb_valid |= QIF_ITIME;
	}

	retval = llapi_quotactl((char *)filepath, &dataquota);

	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (presquota != NULL)
		return lustre_get_quota(exp_hdl, filepath, quota_type,
					presquota);
 err:
	return fsalstat(fsal_error, retval);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t lustre_extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	struct lustre_file_handle *hdl;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct lustre_file_handle *)fh_desc->addr;
	fh_size = lustre_sizeof_handle(hdl);
	if (fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;	/* pass back the actual size */
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
nfsstat4 lustre_create_ds_handle(struct fsal_export *const export_pub,
				 const struct gsh_buffdesc *const desc,
				 struct fsal_ds_handle **const ds_pub)
{
	/* Handle to be created */
	struct lustre_ds *ds = NULL;

	*ds_pub = NULL;

	if (desc->len != sizeof(struct lustre_file_handle))
		return NFS4ERR_BADHANDLE;
	ds = gsh_calloc(1, sizeof(struct lustre_ds));

	if (ds == NULL)
		return NFS4ERR_SERVERFAULT;

	/* Connect lazily when a FILE_SYNC4 write forces us to, not
	 *            here. */

	ds->connected = false;

	memcpy(&ds->wire, desc->addr, desc->len);

	fsal_ds_handle_init(&ds->ds, export_pub->ds_ops, export_pub->fsal);

	*ds_pub = &ds->ds;

	return NFS4_OK;
}

/* lustre_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void lustre_export_ops_init(struct export_ops *ops)
{
	ops->release = lustre_release;
	ops->lookup_path = lustre_lookup_path;
	ops->extract_handle = lustre_extract_handle;
	ops->create_handle = lustre_create_handle;
	ops->create_ds_handle = lustre_create_ds_handle;
	ops->get_fs_dynamic_info = lustre_get_dynamic_info;
	ops->fs_supports = lustre_fs_supports;
	ops->fs_maxfilesize = lustre_fs_maxfilesize;
	ops->fs_maxread = lustre_fs_maxread;
	ops->fs_maxwrite = lustre_fs_maxwrite;
	ops->fs_maxlink = lustre_fs_maxlink;
	ops->fs_maxnamelen = lustre_fs_maxnamelen;
	ops->fs_maxpathlen = lustre_fs_maxpathlen;
	ops->fs_lease_time = lustre_fs_lease_time;
	ops->fs_acl_support = lustre_fs_acl_support;
	ops->fs_supported_attrs = lustre_fs_supported_attrs;
	ops->fs_umask = lustre_fs_umask;
	ops->fs_xattr_access_rights = lustre_fs_xattr_access_rights;
	ops->get_quota = lustre_get_quota;
	ops->set_quota = lustre_set_quota;
}

void free_lustre_filesystem(struct lustre_filesystem *lustre_fs)
{
	if (lustre_fs->fsname)
		gsh_free(lustre_fs->fsname);

	gsh_free(lustre_fs);
}

int lustre_claim_filesystem(struct fsal_filesystem *fs, struct fsal_export *exp)
{
	struct lustre_filesystem *lustre_fs = fs->private;
	int retval = 0;
	struct lustre_fsal_export *myself;
	struct lustre_filesystem_export_map *map = NULL;

	myself = container_of(exp, struct lustre_fsal_export, export);

	if (strcmp(fs->type, "lustre") != 0) {
		LogInfo(COMPONENT_FSAL,
			"Attempt to claim non-LUSTRE filesystem %s",
			fs->path);
		retval = ENXIO;
		goto errout;
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
		lustre_fs = fs->private;
		if (lustre_fs == NULL) {
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

	lustre_fs = gsh_calloc(1, sizeof(*lustre_fs));

	if (lustre_fs == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Out of memory to claim file system %s",
			fs->path);
		retval = ENOMEM;
		goto errout;
	}

	glist_init(&lustre_fs->exports);

	lustre_fs->fs = fs;

	/* Call llapi to get Lustre fs name
	 * This is not the fsname is the mntent */
	lustre_fs->fsname = gsh_malloc(MAXPATHLEN);
	if (lustre_fs->fsname == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Out of memory to claim file system %s",
			fs->path);
		retval = ENOMEM;
		goto errout;
	}

	/* Get information from llapi */
	retval = llapi_search_fsname(fs->path, lustre_fs->fsname);
	if (retval)
		goto errout;

	/* Lustre_fs is ready, store it in the FS */
	fs->private = lustre_fs;

already_claimed:

	/* Now map the file system and export */
	map->fs = lustre_fs;
	map->exp = myself;
	glist_add_tail(&lustre_fs->exports, &map->on_exports);
	glist_add_tail(&myself->filesystems, &map->on_filesystems);

	return 0;

errout:

	if (map != NULL)
		gsh_free(map);

	if (lustre_fs != NULL)
		free_lustre_filesystem(lustre_fs);

	return retval;
}

void lustre_unclaim_filesystem(struct fsal_filesystem *fs)
{
	struct lustre_filesystem *lustre_fs = fs->private;
	struct glist_head *glist, *glistn;
	struct lustre_filesystem_export_map *map;

	if (lustre_fs != NULL) {
		glist_for_each_safe(glist, glistn, &lustre_fs->exports) {
			map = glist_entry(glist,
					  struct lustre_filesystem_export_map,
					  on_exports);

			/* Remove this file system from mapping */
			glist_del(&map->on_filesystems);
			glist_del(&map->on_exports);

			if (map->exp->root_fs == fs) {
				LogInfo(COMPONENT_FSAL,
					"Removing root_fs %s from LUSTRE export",
					fs->path);
			}

			/* And free it */
			gsh_free(map);
		}

		free_lustre_filesystem(lustre_fs);

		fs->private = NULL;
	}

	LogInfo(COMPONENT_FSAL,
		"LUSTRE Unclaiming %s",
		fs->path);
}

void lustre_unexport_filesystems(struct lustre_fsal_export *exp)
{
	struct glist_head *glist, *glistn;
	struct lustre_filesystem_export_map *map;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	glist_for_each_safe(glist, glistn, &exp->filesystems) {
		map = glist_entry(glist,
				  struct lustre_filesystem_export_map,
				  on_filesystems);

		/* Remove this export from mapping */
		glist_del(&map->on_filesystems);
		glist_del(&map->on_exports);

		if (glist_empty(&map->fs->exports)) {
			LogInfo(COMPONENT_FSAL,
				"LUSTRE is no longer exporting filesystem %s",
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

fsal_status_t lustre_create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   const struct fsal_up_vector *up_ops)
{
	struct lustre_fsal_export *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	myself = gsh_malloc(sizeof(struct lustre_fsal_export));
	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "lustre_fsal_create: out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}
	memset(myself, 0, sizeof(struct lustre_fsal_export));
	glist_init(&myself->filesystems);

	retval = fsal_export_init(&myself->export);
	if (retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "out of memory for object");
		gsh_free(myself);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	lustre_export_ops_init(myself->export.ops);
	lustre_handle_ops_init(myself->export.obj_ops);
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
					 lustre_claim_filesystem,
					 lustre_unclaim_filesystem,
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

	myself->pnfs_enabled =
	    myself->export.ops->fs_supports(&myself->export,
					    fso_pnfs_ds_supported);
	if (myself->pnfs_enabled) {
		LogInfo(COMPONENT_FSAL,
			"lustre_fsal_create: pnfs was enabled for [%s]",
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
