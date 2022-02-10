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
#include "FSAL/fsal_localfs.h"
#include "fsal_handle_syscalls.h"
#include "vfs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "subfsal.h"
#include "gsh_config.h"

/* helpers to/from other VFS objects
 */

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct vfs_fsal_export *myself;

	myself = EXPORT_VFS_FROM_FSAL(exp_hdl);

	if (op_ctx != NULL && op_ctx->ctx_export != NULL) {
		LogDebug(COMPONENT_FSAL, "Releasing VFS export %"PRIu16
			 " for %s",
			 exp_hdl->export_id,
			 ctx_export_path(op_ctx));
	} else {
		LogDebug(COMPONENT_FSAL, "Releasing VFS export %"PRIu16
			 " on filesystem %s",
			 exp_hdl->export_id,
			 exp_hdl->root_fs->path);
	}

	vfs_sub_fini(myself);

	unclaim_all_export_maps(exp_hdl);

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

	LogFullDebug(COMPONENT_FSAL, "About to check obj %p fs %p",
		     obj_hdl, obj_hdl->fs);

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
	infop->time_delta.tv_sec = 0;
	infop->time_delta.tv_nsec = FSAL_DEFAULT_TIME_DELTA_NSEC;

 out:
	return fsalstat(fsal_error, retval);
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
			       int quota_id,
			       fsal_quota_t *pquota)
{
	struct dqblk fs_quota;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	int errsv;

	/** @todo	if we later have a config to disallow crossmnt, check
	 *		that the quota is in the same file system as export.
	 *		Otherwise, the fact that the quota path will have
	 *		made the longest match means the path MUST be exported
	 *		by this export.
	 */

	memset((char *)&fs_quota, 0, sizeof(struct dqblk));

	if (!vfs_set_credentials(&op_ctx->creds, exp_hdl->fsal)) {
		fsal_error = ERR_FSAL_PERM;
		retval = EPERM;
		goto out;
	}

	/** @todo need to get the right file system... */
	retval = QUOTACTL(QCMD(Q_GETQUOTA, quota_type),
			  exp_hdl->root_fs->device,
			  quota_id, (caddr_t) &fs_quota);
	errsv = errno;
	vfs_restore_ganesha_credentials(exp_hdl->fsal);

	if (retval < 0) {
		fsal_error = posix2fsal_error(errsv);
		retval = errsv;
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
			       int quota_id,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	struct dqblk fs_quota;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	int errsv;

	/** @todo	if we later have a config to disallow crossmnt, check
	 *		that the quota is in the same file system as export.
	 *		Otherwise, the fact that the quota path will have
	 *		made the longest match means the path MUST be exported
	 *		by this export.
	 */

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

	if (!vfs_set_credentials(&op_ctx->creds, exp_hdl->fsal)) {
		fsal_error = ERR_FSAL_PERM;
		retval = EPERM;
		goto err;
	}

	/** @todo need to get the right file system... */
	retval = QUOTACTL(QCMD(Q_SETQUOTA, quota_type),
			  exp_hdl->root_fs->device,
			  quota_id, (caddr_t) &fs_quota);
	errsv = errno;
	vfs_restore_ganesha_credentials(exp_hdl->fsal);

	if (retval < 0) {
		fsal_error = posix2fsal_error(errsv);
		retval = errsv;
		goto err;
	}
	if (presquota != NULL)
		return get_quota(exp_hdl, filepath, quota_type,
				 quota_id, presquota);

 err:
	return fsalstat(fsal_error, retval);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.
 *
 * Setting the length to sizeof(vfs_file_handle_t) coerces all handles
 * to a value too large for some applications (e.g., ESXi), and much
 * larger than necessary.  (On my Linux system, I'm seeing 12 byte file
 * handles (EXT4).  Since this routine has no idea what the internal
 * length was, it should not set the value (the length comes from us
 * anyway, it's up to us to get it right elsewhere).
 */

static fsal_status_t wire_to_host(struct fsal_export *exp_hdl,
				  fsal_digesttype_t in_type,
				  struct gsh_buffdesc *fh_desc,
				  int flags)
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
	ops->wire_to_host = wire_to_host;
	ops->create_handle = vfs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->get_quota = get_quota;
	ops->set_quota = set_quota;
	ops->alloc_state = vfs_alloc_state;
	ops->free_state = vfs_free_state;
}

int vfs_claim_filesystem(struct fsal_filesystem *fs,
			 struct fsal_export *exp,
			 void **private_data)
{
	int retval = 0, fd = root_fd(fs);
	struct vfs_fsal_export *myself;

	LogFilesystem("VFS CLAIM FS", "", fs);

	myself = EXPORT_VFS_FROM_FSAL(exp);

	if (fs->fsal != NULL) {
		if (fd <= 0) {
			LogCrit(COMPONENT_FSAL,
				"Something wrong with export, fs %s appears already claimed but doesn't have private data",
				fs->path);
			retval = EINVAL;
			goto errout;
		}

		goto already_claimed;
	}

	retval = vfs_get_root_handle(fs, myself, &fd);

	if (retval != 0) {
		if (retval == ENOTTY) {
			LogInfo(COMPONENT_FSAL,
				"file system %s is not exportable with %s",
				fs->path, exp->fsal->name);
			retval = ENXIO;
		}
		goto errout;
	}

already_claimed:

	*private_data = (void *) (long) fd;

errout:

	return retval;
}

void vfs_unclaim_filesystem(struct fsal_filesystem *fs)
{
	LogFilesystem("VFS UNCLAIM FS", "", fs);

	if (root_fd(fs) > 0)
		close(root_fd(fs));

	LogInfo(COMPONENT_FSAL,
		"VFS Unclaiming %s",
		fs->path);
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
	fsal_status_t fsal_status = {0, 0};

	vfs_state_init();

	myself = gsh_calloc(1, sizeof(struct vfs_fsal_export));

	fsal_export_init(&myself->export);
	vfs_export_ops_init(&myself->export.exp_ops);

	retval = load_config_from_node(parse_node,
				       vfs_sub_export_param,
				       myself,
				       true,
				       err_type);
	if (retval != 0) {
		fsal_status = posix2fsal_status(EINVAL);
		goto err_free;
	}
	myself->export.fsal = fsal_hdl;
	vfs_sub_init_export_ops(myself, CTX_FULLPATH(op_ctx));

	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if (retval != 0) {
		fsal_status = posix2fsal_status(retval);
		goto err_free;	/* seriously bad */
	}

	retval = resolve_posix_filesystem(CTX_FULLPATH(op_ctx),
					  fsal_hdl, &myself->export,
					  vfs_claim_filesystem,
					  vfs_unclaim_filesystem,
					  &myself->export.root_fs);

	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"resolve_posix_filesystem(%s) returned %s (%d)",
			CTX_FULLPATH(op_ctx),
			strerror(retval), retval);
		fsal_status = posix2fsal_status(retval);
		goto err_cleanup;
	}

	retval = vfs_sub_init_export(myself);
	if (retval != 0) {
		fsal_status = posix2fsal_status(retval);
		goto err_cleanup;
	}

	op_ctx->fsal_export = &myself->export;

	myself->export.up_ops = up_ops;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

err_cleanup:
	unclaim_all_export_maps(&myself->export);
	fsal_detach_export(fsal_hdl, &myself->export.exports);
err_free:
	free_export_ops(&myself->export);
	gsh_free(myself);	/* elvis has left the building */
	return fsal_status;
}

/**
 * @brief Update an existing export
 *
 * This will result in a temporary fsal_export being created, and built into
 * a stacked export.
 *
 * On entry, op_ctx has the original gsh_export and no fsal_export.
 *
 * The caller passes the original fsal_export, as well as the new super_export's
 * FSAL when there is a stacked export. This will allow the underlying export to
 * validate that the stacking has not changed.
 *
 * This function does not actually create a new fsal_export, the only purpose is
 * to validate and update the config.
 *
 * @param[in]     fsal_hdl         FSAL module
 * @param[in]     parse_node       opaque pointer to parse tree node for
 *                                 export options to be passed to
 *                                 load_config_from_node
 * @param[out]    err_type         config proocessing error reporting
 * @param[in]     original         The original export that is being updated
 * @param[in]     updated_super    The updated super_export's FSAL
 *
 * @return FSAL status.
 */

fsal_status_t vfs_update_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				struct fsal_export *original,
				struct fsal_module *updated_super)
{
	struct vfs_fsal_export myself;
	int retval = 0;
	bool invalid = false;
	struct vfs_fsal_export *orig =
		container_of(original, struct vfs_fsal_export, export);
	fsal_status_t status;

	/* Check for changes in stacking by calling default update_export. */
	status = update_export(fsal_hdl, parse_node, err_type,
			       original, updated_super);

	if (FSAL_IS_ERROR(status))
		return status;

	memset(&myself, 0, sizeof(myself));

	retval = load_config_from_node(parse_node,
				       vfs_sub_export_param,
				       &myself,
				       true,
				       err_type);

	if (retval != 0) {
		return posix2fsal_status(EINVAL);
	}

	if (orig->fsid_type != myself.fsid_type) {
		LogCrit(COMPONENT_FSAL,
			"Can not change fsid_type without restart.");
		invalid = true;
	}

	if (orig->async_hsm_restore != myself.async_hsm_restore) {
		LogCrit(COMPONENT_FSAL,
			"Can not change async_hsm_restore without restart.");
		invalid = true;
	}

	return invalid
		? posix2fsal_status(EINVAL)
		: fsalstat(ERR_FSAL_NO_ERROR, 0);
}
