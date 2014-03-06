/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) IBM Inc., 2013
 *
 * Contributors: Jim Lieb jlieb@panasas.com
 *               Allison Henderson        achender@linux.vnet.ibm.com
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
 * PT FSAL export object
 */

#include "config.h"

#include <fcntl.h>
#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <sys/quota.h>
#include "nlm_list.h"
#include "config_parsing.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "pt_methods.h"
#include "fsal_types.h"

/* helpers to/from other PT objects
 */

struct fsal_staticfsinfo_t *pt_staticinfo(struct fsal_module *hdl);

/* export object methods
 */

static fsal_status_t release(struct fsal_export *exp_hdl)
{
	struct pt_fsal_export *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(exp_hdl, struct pt_fsal_export, export);

	pthread_mutex_lock(&exp_hdl->lock);
	if (exp_hdl->refs > 0) {
		LogMajor(COMPONENT_FSAL, "PT release: export (0x%p)busy",
			 exp_hdl);
		fsal_error = posix2fsal_error(EBUSY);
		retval = EBUSY;
		goto errout;
	}
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);
	if (myself->root_fd >= 0)
		PTFSAL_close(myself->root_fd);
	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);
	if (myself->fstype != NULL)
		gsh_free(myself->fstype);
	if (myself->mntdir != NULL)
		gsh_free(myself->mntdir);
	if (myself->fs_spec != NULL)
		gsh_free(myself->fs_spec);
	pthread_mutex_unlock(&exp_hdl->lock);

	pthread_mutex_destroy(&exp_hdl->lock);
	gsh_free(myself);	/* elvis has left the building */
	return fsalstat(fsal_error, retval);

 errout:
	pthread_mutex_unlock(&exp_hdl->lock);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      const struct req_op_context *opctx,
				      fsal_dynamicfsinfo_t *infop)
{
	struct pt_fsal_export *myself;
	struct statvfs buffstatpt;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if (!infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(exp_hdl, struct pt_fsal_export, export);
	retval = fstatvfs(myself->root_fd, &buffstatpt);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	infop->total_bytes = buffstatpt.f_frsize * buffstatpt.f_blocks;
	infop->free_bytes = buffstatpt.f_frsize * buffstatpt.f_bfree;
	infop->avail_bytes = buffstatpt.f_frsize * buffstatpt.f_bavail;
	infop->total_files = buffstatpt.f_files;
	infop->free_files = buffstatpt.f_ffree;
	infop->avail_files = buffstatpt.f_favail;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

 out:
	return fsalstat(fsal_error, retval);
}

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pt_staticinfo(exp_hdl->fsal);
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
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota)
{
	struct pt_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct pt_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "PT get_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno,
			 strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if (path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "PT get_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto out;
	}
	id = (quota_type ==
	      USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->
	    caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));
	retval =
	    quotactl(QCMD(Q_GETQUOTA, quota_type), myself->fs_spec, id,
		     (caddr_t) &fs_quota);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	pquota->bhardlimit = fs_quota.dqb_bhardlimit;
	pquota->bsoftlimit = fs_quota.dqb_bsoftlimit;
	pquota->curblocks = fs_quota.dqb_curspace;
	pquota->fhardlimit = fs_quota.dqb_ihardlimit;
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
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	struct pt_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct pt_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "PT set_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno,
			 strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "PT set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto err;
	}
	id = (quota_type ==
	      USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->
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
	if (pquota->btimeleft != 0) {
		fs_quota.dqb_btime = pquota->btimeleft;
		fs_quota.dqb_valid |= QIF_BTIME;
	}
	if (pquota->ftimeleft != 0) {
		fs_quota.dqb_itime = pquota->ftimeleft;
		fs_quota.dqb_valid |= QIF_ITIME;
	}
	retval =
	    quotactl(QCMD(Q_SETQUOTA, quota_type), myself->fs_spec, id,
		     (caddr_t) &fs_quota);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (presquota != NULL) {
		return get_quota(exp_hdl, filepath, quota_type, req_ctx,
				 presquota);
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

static fsal_status_t pt_extract_handle(struct fsal_export *exp_hdl,
				       fsal_digesttype_t in_type,
				       struct gsh_buffdesc *fh_desc)
{
	ptfsal_handle_t *hdl;
	size_t fh_size = 0;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (ptfsal_handle_t *) fh_desc->addr;
	fh_size = pt_sizeof_handle(hdl);
	if (fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* pt_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void pt_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = pt_lookup_path;
	ops->extract_handle = pt_extract_handle;
	ops->create_handle = pt_create_handle;
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

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_I64("pt_export_id", LLONG_MIN, LLONG_MAX, 1,
		       pt_fsal_export, pt_export_id),
	CONFIG_EOL
};

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.pt-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t pt_create_export(struct fsal_module *fsal_hdl,
			       const char *export_path,
			       void *parse_node,
			       struct exportlist *exp_entry,
			       struct fsal_module *next_fsal,
			       const struct fsal_up_vector *up_ops,
			       struct fsal_export **export)
{
	struct pt_fsal_export *myself;
	FILE *fp;
	struct mntent *p_mnt;
	size_t pathlen, outlen = 0;
	char mntdir[MAXPATHLEN + 1];	/* there has got to be a better way */
	char fs_spec[MAXPATHLEN + 1];
	char type[MAXNAMLEN + 1];
	int retval = 0;
	fsal_status_t status;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	*export = NULL;		/* poison it first */
	if (export_path == NULL || strlen(export_path) == 0
	    || strlen(export_path) > MAXPATHLEN) {
		LogMajor(COMPONENT_FSAL,
			 "pt_create_export: export path empty or too big");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	if (next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL, "This module is not stackable");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	myself = gsh_malloc(sizeof(struct pt_fsal_export));
	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "pt_fsal_create: out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}
	memset(myself, 0, sizeof(struct pt_fsal_export));
	myself->root_fd = -1;

	retval = fsal_export_init(&myself->export, exp_entry);
	if (retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "pt_fsal_create: out of memory for object");
		return fsalstat(posix2fsal_error(retval), retval);
	}
	pt_export_ops_init(myself->export.ops);
	pt_handle_ops_init(myself->export.obj_ops);
	myself->export.up_ops = up_ops;

	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */

	pthread_mutex_lock(&myself->export.lock);
	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if (retval != 0)
		goto errout;	/* seriously bad */
	myself->export.fsal = fsal_hdl;

	/* start looking for the mount point */
	fp = setmntent(MOUNTED, "r");
	if (fp == NULL) {
		retval = errno;
		LogCrit(COMPONENT_FSAL, "Error %d in setmntent(%s): %s", retval,
			MOUNTED, strerror(retval));
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	while ((p_mnt = getmntent(fp)) != NULL) {
		if (p_mnt->mnt_dir != NULL) {
			pathlen = strlen(p_mnt->mnt_dir);
			if (pathlen > outlen) {
				if (strcmp(p_mnt->mnt_dir, "/") == 0) {
					outlen = pathlen;
					strncpy(mntdir, p_mnt->mnt_dir,
						MAXPATHLEN);
					strncpy(type, p_mnt->mnt_type,
						MAXNAMLEN);
					strncpy(fs_spec, p_mnt->mnt_fsname,
						MAXPATHLEN);
				} else
				    if ((strncmp
					 (export_path, p_mnt->mnt_dir,
					  pathlen) == 0)
					&& ((export_path[pathlen] == '/')
					|| (export_path[pathlen] == '\0'))) {
					if (strcasecmp(p_mnt->mnt_type, "xfs")
					    == 0) {
						LogDebug(COMPONENT_FSAL,
							 "Mount (%s) is XFS, skipping",
							 p_mnt->mnt_dir);
						continue;
					}
					outlen = pathlen;
					strncpy(mntdir, p_mnt->mnt_dir,
						MAXPATHLEN);
					strncpy(type, p_mnt->mnt_type,
						MAXNAMLEN);
					strncpy(fs_spec, p_mnt->mnt_fsname,
						MAXPATHLEN);
				}
			}
		}
	}
	endmntent(fp);
	if (outlen <= 0) {
		LogCrit(COMPONENT_FSAL, "No mount entry matches '%s' in %s",
			export_path, MOUNTED);
		fsal_error = ERR_FSAL_NOENT;
		goto errout;
	}
	myself->root_fd = open(mntdir, O_RDONLY | O_DIRECTORY);
	if (myself->root_fd < 0) {
		LogMajor(COMPONENT_FSAL,
			 "Could not open PT mount point %s: rc = %d", mntdir,
			 errno);
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto errout;
	} else {
		struct stat root_stat;
		ptfsal_handle_t *fh = alloca(sizeof(ptfsal_handle_t));

		memset(fh, 0, sizeof(ptfsal_handle_t));
		fh->data.handle.handle_size = OPENHANDLE_HANDLE_LEN;
		retval = fstat(myself->root_fd, &root_stat);
		if (retval < 0) {
			LogMajor(COMPONENT_FSAL,
				 "fstat: root_path: %s, fd=%d, errno=(%d) %s",
				 mntdir, myself->root_fd, errno,
				 strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}

		myself->root_dev = root_stat.st_dev;
		status =
		    fsal_internal_get_handle_at(NULL, &myself->export,
						myself->root_fd, export_path,
						fh);
		if (FSAL_IS_ERROR(status)) {
			fsal_error = retval = status.major;
			retval = errno;
			LogMajor(COMPONENT_FSAL,
				 "name_to_handle: root_path: %s, root_fd=%d, retval=%d",
				 mntdir, myself->root_fd, retval);
			goto errout;
		}
		myself->root_handle = gsh_malloc(sizeof(ptfsal_handle_t));
		if (myself->root_handle == NULL) {
			LogMajor(COMPONENT_FSAL,
				 "memory for root handle, errno=(%d) %s", errno,
				 strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		memcpy(myself->root_handle, &fh, sizeof(ptfsal_handle_t));
	}
	myself->fstype = gsh_strdup(type);
	myself->fs_spec = gsh_strdup(fs_spec);
	myself->mntdir = gsh_strdup(mntdir);
	retval = load_config_from_node(parse_node,
				       &export_param,
				       myself,
				       true);
	if (retval != 0) {
		retval = EINVAL;
		goto errout;
	}
	*export = &myself->export;

	pthread_mutex_unlock(&myself->export.lock);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	if (myself->root_fd >= 0)
		PTFSAL_close(myself->root_fd);
	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);
	if (myself->fstype != NULL)
		gsh_free(myself->fstype);
	if (myself->mntdir != NULL)
		gsh_free(myself->mntdir);
	if (myself->fs_spec != NULL)
		gsh_free(myself->fs_spec);
	free_export_ops(&myself->export);
	pthread_mutex_unlock(&myself->export.lock);
	pthread_mutex_destroy(&myself->export.lock);
	gsh_free(myself);	/* elvis has left the building */
	return fsalstat(fsal_error, retval);
}
