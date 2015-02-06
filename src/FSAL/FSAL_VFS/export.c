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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* export.c
 * VFS Super-FSAL export object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>
#include "gsh_list.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_handle_syscalls.h"
#include "vfs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "subfsal.h"

/* helpers to/from other VFS objects
 */

struct fsal_staticfsinfo_t *vfs_staticinfo(struct fsal_module *hdl);

int vfs_get_root_fd(struct fsal_export *exp_hdl)
{
	struct vfs_fsal_export *myself;
	struct vfs_filesystem *my_root_fs;

	myself = EXPORT_VFS_FROM_FSAL(exp_hdl);
	my_root_fs = myself->root_fs->private;
	return my_root_fs->root_fd;
}

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct vfs_fsal_export *myself;

	myself = EXPORT_VFS_FROM_FSAL(exp_hdl);

	vfs_sub_fini(myself);

	vfs_unexport_filesystems(myself);

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself);	/* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	struct statvfs buffstatvfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			 obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		retval = EXDEV;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}

	retval = statvfs(obj_hdl->fs->path, &buffstatvfs);

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

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
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
	struct vfs_fsal_export *myself;
	struct dqblk fs_quota;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = EXPORT_VFS_FROM_FSAL(exp_hdl);

	/** @todo	if we later have a config to disallow crossmnt, check
	 *		that the quota is in the same file system as export.
	 *		Otherwise, the fact that the quota path will have
	 *		made the longest match means the path MUST be exported
	 *		by this export.
	 */

	id = (quota_type ==
	      USRQUOTA) ? op_ctx->creds->caller_uid : op_ctx->creds->
	    caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));

	/** @todo need to get the right file system... */
	retval = QUOTACTL(QCMD(Q_GETQUOTA, quota_type), myself->root_fs->device,
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
	struct vfs_fsal_export *myself;
	struct dqblk fs_quota;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = EXPORT_VFS_FROM_FSAL(exp_hdl);

	/** @todo	if we later have a config to disallow crossmnt, check
	 *		that the quota is in the same file system as export.
	 *		Otherwise, the fact that the quota path will have
	 *		made the longest match means the path MUST be exported
	 *		by this export.
	 */

	id = (quota_type ==
	      USRQUOTA) ? op_ctx->creds->caller_uid : op_ctx->creds->
	    caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));
	if (pquota->bhardlimit != 0)
		fs_quota.dqb_bhardlimit = pquota->bhardlimit;
	if (pquota->bsoftlimit != 0)
		fs_quota.dqb_bsoftlimit = pquota->bsoftlimit;
	if (pquota->fhardlimit != 0)
		fs_quota.dqb_ihardlimit = pquota->fhardlimit;
	if (pquota->fsoftlimit != 0)
		fs_quota.dqb_isoftlimit = pquota->fsoftlimit;
	if (pquota->btimeleft != 0)
		fs_quota.dqb_btime = pquota->btimeleft;
	if (pquota->ftimeleft != 0)
		fs_quota.dqb_itime = pquota->ftimeleft;
#ifdef LINUX
	if (pquota->bhardlimit != 0)
		fs_quota.dqb_valid |= QIF_BLIMITS;
	if (pquota->bsoftlimit != 0)
		fs_quota.dqb_valid |= QIF_BLIMITS;
	if (pquota->fhardlimit != 0)
		fs_quota.dqb_valid |= QIF_ILIMITS;
	if (pquota->btimeleft != 0)
		fs_quota.dqb_valid |= QIF_BTIME;
	if (pquota->ftimeleft != 0)
		fs_quota.dqb_valid |= QIF_ITIME;
#endif

	/** @todo need to get the right file system... */
	retval = QUOTACTL(QCMD(Q_SETQUOTA, quota_type), myself->root_fs->device,
			  id, (caddr_t) &fs_quota);

	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (presquota != NULL)
		return get_quota(exp_hdl, filepath, quota_type,
				 presquota);

 err:
	return fsalstat(fsal_error, retval);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 *
 * So, adjust the start pointer, check.  But setting the length
 * to sizeof(vfs_file_handle_t) coerces all handles to a value
 * too large for some applications (e.g., ESXi), and much larger
 * than necessary.  (On my Linux system, I'm seeing 12 byte file
 * handles (EXT4).  Since this routine has no idea what the
 * internal length was, it should not set the value (the length
 * comes from us anyway, it's up to us to get it right elsewhere).
 */

static fsal_status_t extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	struct fsal_filesystem *fs;
	bool dummy;
	vfs_file_handle_t *fh = NULL;

	vfs_alloc_handle(fh);

	return vfs_check_handle(exp_hdl, fh_desc, &fs, fh, &dummy);
}

/* vfs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void vfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = vfs_lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = vfs_create_handle;
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
}

void free_vfs_filesystem(struct vfs_filesystem *vfs_fs)
{
	if (vfs_fs->root_fd >= 0)
		close(vfs_fs->root_fd);
	gsh_free(vfs_fs);
}

int vfs_claim_filesystem(struct fsal_filesystem *fs, struct fsal_export *exp)
{
	struct vfs_filesystem *vfs_fs = fs->private;
	int retval;
	struct vfs_fsal_export *myself;
	struct vfs_filesystem_export_map *map;

	myself = EXPORT_VFS_FROM_FSAL(exp);

	map = gsh_calloc(1, sizeof(*map));

	if (map == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Out of memory to claim file system %s",
			fs->path);
		retval = ENOMEM;
		goto errout;
	}

	if (fs->fsal != NULL) {
		vfs_fs = fs->private;
		if (vfs_fs == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Something wrong with export, fs %s appears already claimed but doesn't have private data",
				fs->path);
			retval = EINVAL;
			goto errout;
		}

		goto already_claimed;
	}

	vfs_fs = gsh_calloc(1, sizeof(*vfs_fs));

	if (vfs_fs == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Out of memory to claim file system %s",
			fs->path);
		retval = ENOMEM;
		goto errout;
	}

	glist_init(&vfs_fs->exports);
	vfs_fs->root_fd = -1;

	vfs_fs->fs = fs;

	retval = vfs_get_root_handle(vfs_fs, myself);

	if (retval != 0) {
		if (retval == ENOTTY) {
			LogInfo(COMPONENT_FSAL,
				"file system %s is not exportable with %s",
				fs->path, exp->fsal->name);
			retval = ENXIO;
		}
		goto errout;
	}

	fs->private = vfs_fs;

already_claimed:

	/* Now map the file system and export */
	map->fs = vfs_fs;
	map->exp = myself;
	glist_add_tail(&vfs_fs->exports, &map->on_exports);
	glist_add_tail(&myself->filesystems, &map->on_filesystems);

	return 0;

errout:

	if (map != NULL)
		gsh_free(map);

	if (vfs_fs != NULL)
		free_vfs_filesystem(vfs_fs);

	return retval;
}

void vfs_unclaim_filesystem(struct fsal_filesystem *fs)
{
	struct vfs_filesystem *vfs_fs = fs->private;
	struct glist_head *glist, *glistn;
	struct vfs_filesystem_export_map *map;

	if (vfs_fs != NULL) {
		glist_for_each_safe(glist, glistn, &vfs_fs->exports) {
			map = glist_entry(glist,
					  struct vfs_filesystem_export_map,
					  on_exports);

			/* Remove this file system from mapping */
			glist_del(&map->on_filesystems);
			glist_del(&map->on_exports);

			if (map->exp->root_fs == fs) {
				LogInfo(COMPONENT_FSAL,
					"Removing root_fs %s from VFS export",
					fs->path);
			}

			/* And free it */
			gsh_free(map);
		}

		free_vfs_filesystem(vfs_fs);

		fs->private = NULL;
	}

	LogInfo(COMPONENT_FSAL,
		"VFS Unclaiming %s",
		fs->path);
}

void vfs_unexport_filesystems(struct vfs_fsal_export *exp)
{
	struct glist_head *glist, *glistn;
	struct vfs_filesystem_export_map *map;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	glist_for_each_safe(glist, glistn, &exp->filesystems) {
		map = glist_entry(glist,
				  struct vfs_filesystem_export_map,
				  on_filesystems);

		/* Remove this export from mapping */
		glist_del(&map->on_filesystems);
		glist_del(&map->on_exports);

		if (glist_empty(&map->fs->exports)) {
			LogInfo(COMPONENT_FSAL,
				"VFS is no longer exporting filesystem %s",
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

fsal_status_t vfs_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops)
{
	struct vfs_fsal_export *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	myself = gsh_calloc(1, sizeof(struct vfs_fsal_export));
	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}

	glist_init(&myself->filesystems);

	retval = fsal_export_init(&myself->export);
	if (retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "out of memory for object");
		gsh_free(myself);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	vfs_export_ops_init(&myself->export.exp_ops);
	myself->export.up_ops = up_ops;

	retval = load_config_from_node(parse_node,
				       vfs_sub_export_param,
				       myself,
				       true,
				       err_type);
	if (retval != 0)
		return fsalstat(ERR_FSAL_INVAL, 0);
	myself->export.fsal = fsal_hdl;
	vfs_sub_init_export_ops(myself, op_ctx->export->fullpath);

	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if (retval != 0) {
		fsal_error = posix2fsal_error(retval);
		goto errout;	/* seriously bad */
	}

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
					 vfs_claim_filesystem,
					 vfs_unclaim_filesystem,
					 &myself->root_fs);

	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"claim_posix_filesystems(%s) returned %s (%d)",
			op_ctx->export->fullpath,
			strerror(retval), retval);
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	retval = vfs_sub_init_export(myself);
	if (retval != 0) {
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	op_ctx->fsal_export = &myself->export;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:

	vfs_unexport_filesystems(myself);

	free_export_ops(&myself->export);
	gsh_free(myself);	/* elvis has left the building */
	return fsalstat(fsal_error, retval);
}
