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
 * VFS FSAL export object
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
#include "nlm_list.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_handle_syscalls.h"
#include "vfs_methods.h"

#include "pnfs_panfs/mds.h"

/* helpers to/from other VFS objects
 */

struct fsal_staticfsinfo_t *vfs_staticinfo(struct fsal_module *hdl);

int vfs_get_root_fd(struct fsal_export *exp_hdl)
{
	struct vfs_fsal_export *myself;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	return myself->root_fd;
}

static int vfs_fsal_open_exp(struct fsal_export *exp, vfs_file_handle_t *fh,
			     int openflags, fsal_errors_t *fsal_error)
{
	int mount_fd = vfs_get_root_fd(exp);
	int fd = vfs_open_by_handle(mount_fd, fh, openflags);
	if (fd < 0) {
		fd = -errno;
		if (fd == -ENOENT) {
			*fsal_error = ERR_FSAL_STALE;
			fd = -ESTALE;
		} else {
			*fsal_error = posix2fsal_error(-fd);
		}
		LogDebug(COMPONENT_FSAL, "Failed with %s", strerror(-fd));
	}
	return fd;
}

static int vfs_exp_fd_to_handle(int fd, vfs_file_handle_t *fh)
{
	int mntid;
	return vfs_fd_to_handle(fd, fh, &mntid);
}

static struct vfs_exp_handle_ops defops = {
	.vex_open_by_handle = vfs_fsal_open_exp,
	.vex_name_to_handle = vfs_name_to_handle_at,
	.vex_fd_to_handle = vfs_exp_fd_to_handle,
	.vex_readlink = vfs_fsal_readlink
};

/* export object methods
 */

static fsal_status_t release(struct fsal_export *exp_hdl)
{
	struct vfs_fsal_export *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);

	pnfs_panfs_fini(myself->pnfs_data);
	pthread_mutex_lock(&exp_hdl->lock);
	if (exp_hdl->refs > 0 || !glist_empty(&exp_hdl->handles)) {
		LogMajor(COMPONENT_FSAL, "VFS release: export (0x%p)busy",
			 exp_hdl);
		fsal_error = posix2fsal_error(EBUSY);
		retval = EBUSY;
		goto errout;
	}
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);
	if (myself->root_fd >= 0)
		close(myself->root_fd);
	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);
	if (myself->fstype != NULL)
		gsh_free(myself->fstype);
	if (myself->mntdir != NULL)
		gsh_free(myself->mntdir);
	if (myself->fs_spec != NULL)
		gsh_free(myself->fs_spec);
	if (myself->handle_lib != NULL)
		gsh_free(myself->handle_lib);
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
	struct vfs_fsal_export *myself;
	struct statvfs buffstatvfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if (!infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	retval = fstatvfs(myself->root_fd, &buffstatvfs);
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
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota)
{
	struct vfs_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "VFS get_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno,
			 strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if (path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "VFS get_quota: crossed mount boundary! root_path: %s, quota path: %s",
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
	    QUOTACTL(QCMD(Q_GETQUOTA, quota_type), myself->fs_spec, id,
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
	struct vfs_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "VFS set_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno,
			 strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "VFS set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto err;
	}
	id = (quota_type ==
	      USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->
	    caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));
	if (pquota->bhardlimit != 0)
		fs_quota.dqb_bhardlimit = pquota->bhardlimit;
	if (pquota->bsoftlimit != 0)
		fs_quota.dqb_bsoftlimit = pquota->bsoftlimit;
	if (pquota->fhardlimit != 0)
		fs_quota.dqb_ihardlimit = pquota->fhardlimit;
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
	retval =
	    QUOTACTL(QCMD(Q_SETQUOTA, quota_type), myself->fs_spec, id,
		     (caddr_t) &fs_quota);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (presquota != NULL)
		return get_quota(exp_hdl, filepath, quota_type, req_ctx,
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
	size_t fh_min;

	fh_min = VFS_FILE_HANDLE_MIN;
	if (in_type == FSAL_DIGEST_NFSV2) {
		if (fh_desc->len < fh_min) {
			LogMajor(COMPONENT_FSAL,
				 "V2 size too small for handle.  should be >= %lu, got %lu",
				 fh_min, fh_desc->len);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	} else if (in_type != FSAL_DIGEST_SIZEOF && fh_desc->len < fh_min) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be >= %lu, got %lu",
			 fh_min, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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

void vfs_handle_ops_init(struct fsal_obj_ops *ops);

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_BOOL("pnfs_panfs", false,
		       vfs_fsal_export, pnfs_panfs_enabled),
	CONF_ITEM_PATH("handle_lib", 1, MAXPATHLEN, NULL,
		       vfs_fsal_export, handle_lib),
	CONFIG_EOL
};

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.vfs-export%d",
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

fsal_status_t vfs_create_export(struct fsal_module *fsal_hdl,
				const char *export_path,
				const char *fs_specific,
				struct exportlist *exp_entry,
				struct fsal_module *next_fsal,
				const struct fsal_up_vector *up_ops,
				struct fsal_export **export)
{
	struct vfs_fsal_export *myself;
	FILE *fp;
	struct mntent *p_mnt;
	size_t pathlen, outlen = 0;
	char mntdir[MAXPATHLEN + 1]; /* there has got to be a better way... */
	char fs_spec[MAXPATHLEN + 1];
	struct vfs_exp_handle_ops *hops = &defops;
	char type[MAXNAMLEN + 1];
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	*export = NULL;		/* poison it first */
	if (export_path == NULL || strlen(export_path) == 0
	    || strlen(export_path) > MAXPATHLEN) {
		LogMajor(COMPONENT_FSAL,
			 "vfs_create_export: export path empty or too big");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	if (next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL, "This module is not stackable");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	myself = gsh_calloc(1, sizeof(struct vfs_fsal_export));
	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "vfs_fsal_create: out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}
	myself->root_fd = -1;

	retval = fsal_export_init(&myself->export, exp_entry);
	if (retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "vfs_fsal_create: out of memory for object");
		gsh_free(myself);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	vfs_export_ops_init(myself->export.ops);
	vfs_handle_ops_init(myself->export.obj_ops);
	myself->export.up_ops = up_ops;

	retval = load_config_from_node((void *)fs_specific,
				       &export_param,
				       myself,
				       true);
	if (retval != 0)
		return fsalstat(ERR_FSAL_INVAL, 0);
	if (myself->pnfs_panfs_enabled) {
		LogInfo(COMPONENT_FSAL,
			"vfs_fsal_create: pnfs_panfs was enabled for [%s]",
			export_path);
		export_ops_pnfs(myself->export.ops);
		handle_ops_pnfs(myself->export.obj_ops);
	}

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
					    || (export_path[pathlen] ==
						'\0'))) {
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
#ifdef LINUX
	if (myself->handle_lib != NULL) {
		void *dl;
		void *sym;

		dl = dlopen(myself->handle_lib, RTLD_NOW | RTLD_LOCAL);
		if (dl == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Could not load handle module '%s' for export '%s' - %s",
				myself->handle_lib, export_path,
				dlerror());
			fsal_error = ERR_FSAL_NOENT;
			goto errout;
		}

		sym = dlsym(dl, "get_handle_ops");
		if (sym == NULL) {
			LogCrit(COMPONENT_FSAL,
				"No bootstrap entry in handle module '%s'",
				myself->handle_lib);
			dlclose(dl);
			fsal_error = ERR_FSAL_NOENT;
			goto errout;
		}
		hops = ((struct vfs_exp_handle_ops * (*)(char *))sym) (mntdir);
		if (hops == NULL) {
			LogCrit(COMPONENT_FSAL,
				"%s - cannot bootstrap handle module '%s' with %s",
				export_path, myself->handle_lib, mntdir);
			dlclose(dl);
			fsal_error = ERR_FSAL_NOENT;
			goto errout;
		}
	}
#endif

	myself->root_fd = open(mntdir, O_RDONLY | O_DIRECTORY);
	if (myself->root_fd < 0) {
		LogMajor(COMPONENT_FSAL,
			 "Could not open VFS mount point %s: rc = %d", mntdir,
			 errno);
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto errout;
	} else {
		struct stat root_stat;
		vfs_file_handle_t *fh = NULL;
		vfs_alloc_handle(fh);
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
		retval = hops->vex_fd_to_handle(myself->root_fd, fh);
		if (retval != 0) {
			LogMajor(COMPONENT_FSAL,
				 "vfs_fd_to_handle: root_path: %s, root_fd=%d, errno=(%d) %s",
				 mntdir, myself->root_fd, errno,
				 strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		myself->root_handle = gsh_malloc(sizeof(vfs_file_handle_t));
		if (myself->root_handle == NULL) {
			LogMajor(COMPONENT_FSAL,
				 "memory for root handle, errno=(%d) %s", errno,
				 strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		memcpy(myself->root_handle, fh, sizeof(vfs_file_handle_t));
	}

	if (myself->pnfs_panfs_enabled) {
		retval = pnfs_panfs_init(myself->root_fd, &myself->pnfs_data);
		if (retval) {
			LogCrit(COMPONENT_FSAL,
				"vfs export_ops_pnfs faild => %d [%s]", retval,
				strerror(retval));
			goto errout;
		}
	}
	myself->fstype = gsh_strdup(type);
	myself->fs_spec = gsh_strdup(fs_spec);
	myself->mntdir = gsh_strdup(mntdir);
	myself->vex_ops = *hops;
	*export = &myself->export;
	pthread_mutex_unlock(&myself->export.lock);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	if (myself->root_fd >= 0)
		close(myself->root_fd);
	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);
	if (myself->fstype != NULL)
		gsh_free(myself->fstype);
	if (myself->mntdir != NULL)
		gsh_free(myself->mntdir);
	if (myself->fs_spec != NULL)
		gsh_free(myself->fs_spec);
	if (myself->handle_lib != NULL)
		gsh_free(myself->handle_lib);
	free_export_ops(&myself->export);
	pthread_mutex_unlock(&myself->export.lock);
	pthread_mutex_destroy(&myself->export.lock);
	gsh_free(myself);	/* elvis has left the building */
	return fsalstat(fsal_error, retval);
}
