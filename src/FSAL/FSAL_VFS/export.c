/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* export.c
 * VFS FSAL export object
 */

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <sys/quota.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "vfs_methods.h"

/*
 * VFS internal export
 */

struct vfs_fsal_export {
	struct fsal_export export;
	char *mntdir;
	char *fs_spec;
	char *fstype;
	int root_fd;
	dev_t root_dev;
	struct file_handle *root_handle;
};

/* helpers to/from other VFS objects
 */

struct fsal_staticfsinfo_t *vfs_staticinfo(struct fsal_module *hdl);

int vfs_get_root_fd(struct fsal_export *exp_hdl) {
	struct vfs_fsal_export *myself;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	return myself->root_fd;
}

/* export object methods
 */

static fsal_status_t release(struct fsal_export *exp_hdl)
{
	struct vfs_fsal_export *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);

	pthread_mutex_lock(&exp_hdl->lock);
	if(exp_hdl->refs > 0 || !glist_empty(&exp_hdl->handles)) {
		LogMajor(COMPONENT_FSAL,
			 "VFS release: export (0x%p)busy",
			 exp_hdl);
		fsal_error = posix2fsal_error(EBUSY);
		retval = EBUSY;
		goto errout;
	}
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	if(myself->root_fd >= 0)
		close(myself->root_fd);
	if(myself->root_handle != NULL)
		free(myself->root_handle);
	if(myself->fstype != NULL)
		free(myself->fstype);
	if(myself->mntdir != NULL)
		free(myself->mntdir);
	if(myself->fs_spec != NULL)
		free(myself->fs_spec);
	myself->export.ops = NULL; /* poison myself */
	pthread_mutex_unlock(&exp_hdl->lock);

	pthread_mutex_destroy(&exp_hdl->lock);
	free(myself);  /* elvis has left the building */
	ReturnCode(fsal_error, retval);

errout:
	pthread_mutex_unlock(&exp_hdl->lock);
	ReturnCode(fsal_error, retval);
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
					 fsal_dynamicfsinfo_t *infop)
{
	struct vfs_fsal_export *myself;
	struct statvfs buffstatvfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if( !infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	retval = fstatvfs(myself->root_fd, &buffstatvfs);
	if(retval < 0) {
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
	infop->time_delta.seconds = 1;
	infop->time_delta.nseconds = 0;

out:
	ReturnCode(fsal_error, retval);	
}

static fsal_boolean_t fs_supports(struct fsal_export *exp_hdl,
				  fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static fsal_size_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static fsal_size_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static fsal_size_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static fsal_count_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static fsal_mdsize_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static fsal_mdsize_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static fsal_fhexptype_t fs_fh_expire_type(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_fh_expire_type(info);
}

static fsal_time_t fs_lease_time(struct fsal_export *exp_hdl)
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

static fsal_attrib_mask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static fsal_accessmode_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static fsal_accessmode_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_xattr_access_rights(info);
}

#ifdef _USE_FSALMDS
static fattr4_fs_layout_types fs_layout_types(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_fs_layout_types(info);
}

static fsal_size_t layout_blksize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = vfs_staticinfo(exp_hdl->fsal);
	return fsal_layout_blksize(info);
}
#endif

/* check_quota
 * return happiness for now.
 */

static fsal_status_t check_quota(struct fsal_export *exp_hdl,
				 fsal_path_t * pfsal_path,
				 int quota_type,
				 struct user_cred *creds)
{
	ReturnCode(ERR_FSAL_NO_ERROR, 0) ;
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
			       fsal_path_t * pfsal_path,
			       int quota_type,
			       struct user_cred *creds,
			       fsal_quota_t *pquota)
{
	struct vfs_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	retval = stat(pfsal_path->path, &path_stat);
	if(retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "VFS get_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if(path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "VFS get_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, pfsal_path->path);
		fsal_error = ERR_FSAL_FAULT; /* maybe a better error? */
		retval = 0;
		goto out;
	}
	id = (quota_type == USRQUOTA) ? creds->caller_uid : creds->caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));
	retval = quotactl(QCMD(Q_GETQUOTA, quota_type),
			  myself->fs_spec,
			  id,
			  (caddr_t) &fs_quota);
	if(retval < 0) {
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
	ReturnCode(fsal_error, retval);	
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       fsal_path_t * pfsal_path,
			       int quota_type,
			       struct user_cred *creds,
			       fsal_quota_t * pquota,
			       fsal_quota_t * presquota)
{
	struct vfs_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct vfs_fsal_export, export);
	retval = stat(pfsal_path->path, &path_stat);
	if(retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "VFS set_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if(path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "VFS set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, pfsal_path->path);
		fsal_error = ERR_FSAL_FAULT; /* maybe a better error? */
		retval = 0;
		goto err;
	}
	id = (quota_type == USRQUOTA) ? creds->caller_uid : creds->caller_gid;
	memset((char *)&fs_quota, 0, sizeof(struct dqblk));
	if(pquota->bhardlimit != 0) {
		fs_quota.dqb_bhardlimit = pquota->bhardlimit;
		fs_quota.dqb_valid |= QIF_BLIMITS;
	}
	if(pquota->bsoftlimit != 0) {
		fs_quota.dqb_bsoftlimit = pquota->bsoftlimit;
		fs_quota.dqb_valid |= QIF_BLIMITS;
	}
	if(pquota->fhardlimit != 0) {
		fs_quota.dqb_ihardlimit = pquota->fhardlimit;
		fs_quota.dqb_valid |= QIF_ILIMITS;
	}
	if(pquota->btimeleft != 0) {
		fs_quota.dqb_btime = pquota->btimeleft;
		fs_quota.dqb_valid |= QIF_BTIME;
	}
	if(pquota->ftimeleft != 0) {
		fs_quota.dqb_itime = pquota->ftimeleft;
		fs_quota.dqb_valid |= QIF_ITIME;
	}
	retval = quotactl(QCMD(Q_SETQUOTA, quota_type),
			  myself->fs_spec,
			  id,
			  (caddr_t) &fs_quota);
	if(retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if(presquota != NULL) {
		return exp_hdl->ops->get_quota(exp_hdl, pfsal_path, quota_type,
					       creds, presquota);
	}
err:
	ReturnCode(fsal_error, retval);	
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

fsal_status_t extract_handle(struct fsal_export *exp_hdl,
					fsal_digesttype_t in_type,
					struct fsal_handle_desc *fh_desc)
{
	struct file_handle *hdl;
	size_t fh_size;

	/* sanity checks */
	if( !fh_desc || !fh_desc->start)
		ReturnCode(ERR_FSAL_FAULT, 0);

	hdl = (struct file_handle *)fh_desc->start;
	fh_size = vfs_sizeof_handle(hdl);
	if(in_type == FSAL_DIGEST_NFSV2) {
		if(fh_desc->len < fh_size) {
			LogMajor(COMPONENT_FSAL,
				 "V2 size too small for handle.  should be %lu, got %lu",
				 fh_size, fh_desc->len);
			ReturnCode(ERR_FSAL_SERVERFAULT, 0);
		}
	} else if(in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		ReturnCode(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;  /* pass back the actual size */
	ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/* NOP methods
 */

fsal_status_t lookup_junction(struct fsal_export *exp_hdl,
			      struct fsal_obj_handle *junction,
			      struct fsal_obj_handle **handle)
{
	ReturnCode(ERR_FSAL_NO_ERROR, 0);	
}

static struct export_ops exp_ops = {
	.get = fsal_export_get,
	.put = fsal_export_put,
	.release = release,
	.lookup_path = vfs_lookup_path,
	.lookup_junction = lookup_junction,
	.extract_handle = extract_handle,
	.create_handle = vfs_create_handle,
	.get_fs_dynamic_info = get_dynamic_info,
	.fs_supports = fs_supports,
	.fs_maxfilesize = fs_maxfilesize,
	.fs_maxread = fs_maxread,
	.fs_maxwrite = fs_maxwrite,
	.fs_maxlink = fs_maxlink,
	.fs_maxnamelen = fs_maxnamelen,
	.fs_maxpathlen = fs_maxpathlen,
	.fs_fh_expire_type = fs_fh_expire_type,
	.fs_lease_time = fs_lease_time,
	.fs_acl_support = fs_acl_support,
	.fs_supported_attrs = fs_supported_attrs,
	.fs_umask = fs_umask,
	.fs_xattr_access_rights = fs_xattr_access_rights,
#ifdef _USE_FSALMDS
	.fs_layout_types = fs_layout_types,
	.layout_blksize = layout_blksize,
#endif
	.check_quota = check_quota,
	.get_quota = get_quota,
	.set_quota = set_quota
};

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t vfs_create_export(struct fsal_module *fsal_hdl,
				fsal_path_t *export_path,
				char *fs_options,
				struct exportlist__ *exp_entry,
				struct fsal_module *next_fsal,
				struct fsal_export **export)
{
	pthread_mutexattr_t attrs;
	struct vfs_fsal_export *myself;
	FILE *fp;
	struct mntent *p_mnt;
	size_t pathlen, outlen = 0;
	char mntdir[MAXPATHLEN];  /* there has got to be a better way... */
	char fs_spec[MAXPATHLEN];
	char type[MAXNAMLEN];
	int retval = 0;
	fsal_errors_t fsal_error;

	*export = NULL; /* poison it first */
	if(export_path == NULL
	   || strlen(export_path->path) == 0
	   || strlen(export_path->path) > MAXPATHLEN) {
		LogMajor(COMPONENT_FSAL,
			 "vfs_create_export: export path empty or too big");
		ReturnCode(ERR_FSAL_INVAL, 0);
	}
	if(next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL,
			"This module is not stackable");
		ReturnCode(ERR_FSAL_INVAL, 0);
	}

	myself = malloc(sizeof(struct vfs_fsal_export));
	if(myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "vfs_fsal_create: out of memory for object");
		ReturnCode(posix2fsal_error(errno), errno);
	}
	memset(myself, 0, sizeof(struct vfs_fsal_export));
	myself->root_fd = -1;
	init_glist(&myself->export.exports);
	pthread_mutexattr_init(&attrs);
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
	pthread_mutex_init(&myself->export.lock, &attrs);

	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */

	pthread_mutex_lock(&myself->export.lock);
	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if(retval != 0)
		goto errout; /* seriously bad */
	myself->export.fsal = fsal_hdl;

	/* start looking for the mount point */
	fp = setmntent(MOUNTED, "r");
	if(fp == NULL) {
		retval = errno;
		LogCrit(COMPONENT_FSAL,
			"Error %d in setmntent(%s): %s", retval, MOUNTED,
			strerror(retval));
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	while((p_mnt = getmntent(fp)) != NULL) {
		if(p_mnt->mnt_dir != NULL) {
			pathlen = strlen(p_mnt->mnt_dir);
			if(pathlen > outlen) {
				if(strcmp(p_mnt->mnt_dir, "/") == 0) {
					outlen = pathlen;
					strncpy(mntdir,
						p_mnt->mnt_dir, MAXPATHLEN);
					strncpy(type,
						p_mnt->mnt_type, MAXNAMLEN);
					strncpy(fs_spec,
						p_mnt->mnt_fsname, MAXPATHLEN);
				} else if((strncmp(export_path->path,
						  p_mnt->mnt_dir,
						  pathlen) == 0) &&
					  ((export_path->path[pathlen] == '/') ||
					   (export_path->path[pathlen] == '\0'))) {
					if(strcasecmp(p_mnt->mnt_type, "xfs") == 0) {
						LogDebug(COMPONENT_FSAL,
							 "Mount (%s) is XFS, skipping",
							 p_mnt->mnt_dir);
						continue;
					}
					outlen = pathlen;
					strncpy(mntdir,
						p_mnt->mnt_dir, MAXPATHLEN);
					strncpy(type,
						p_mnt->mnt_type, MAXNAMLEN);
					strncpy(fs_spec,
						p_mnt->mnt_fsname, MAXPATHLEN);
				}
			}
		}
	}
	endmntent(fp);
	if(outlen <= 0) {
		LogCrit(COMPONENT_FSAL,
			"No mount entry matches '%s' in %s",
			export_path->path, MOUNTED);
		fsal_error = ERR_FSAL_NOENT;
		goto errout;
        }
	myself->root_fd = open(mntdir,  O_RDONLY|O_DIRECTORY);
	if(myself->root_fd < 0) {
		LogMajor(COMPONENT_FSAL,
			 "Could not open VFS mount point %s: rc = %d",
			 mntdir, errno);
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto errout;
	} else {
		struct stat root_stat;
		int mnt_id = 0;
		struct file_handle *fh = alloca(sizeof(struct file_handle)
					       + MAX_HANDLE_SZ);

		memset(fh, 0, sizeof(struct file_handle) + MAX_HANDLE_SZ);
		fh->handle_bytes = MAX_HANDLE_SZ;
		retval = fstat(myself->root_fd, &root_stat);
		if(retval < 0) {
			LogMajor(COMPONENT_FSAL,
				 "fstat: root_path: %s, fd=%d, errno=(%d) %s",
				 mntdir, myself->root_fd, errno, strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		myself->root_dev = root_stat.st_dev;
		retval = name_to_handle_at(myself->root_fd, "", fh,
					   &mnt_id, AT_EMPTY_PATH);
		if(retval != 0) {
			LogMajor(COMPONENT_FSAL,
				 "name_to_handle: root_path: %s, root_fd=%d, errno=(%d) %s",
				 mntdir, myself->root_fd, errno, strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		myself->root_handle = malloc(sizeof(struct file_handle) + fh->handle_bytes);
		if(myself->root_handle == NULL) {
			LogMajor(COMPONENT_FSAL,
				 "memory for root handle, errno=(%d) %s",
				 errno, strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		memcpy(myself->root_handle, fh, sizeof(struct file_handle) + fh->handle_bytes);
	}
	myself->fstype = strdup(type);
	myself->fs_spec = strdup(fs_spec);
	myself->mntdir = strdup(mntdir);
	myself->export.refs = 1;  /* we exit with a reference held */
	myself->export.exp_entry = exp_entry;
	myself->export.ops = &exp_ops;
	*export = &myself->export;
	pthread_mutex_unlock(&myself->export.lock);
	ReturnCode(ERR_FSAL_NO_ERROR, 0);

errout:
	if(myself->root_fd >= 0)
		close(myself->root_fd);
	if(myself->root_handle != NULL)
		free(myself->root_handle);
	if(myself->fstype != NULL)
		free(myself->fstype);
	if(myself->mntdir != NULL)
		free(myself->mntdir);
	if(myself->fs_spec != NULL)
		free(myself->fs_spec);
	myself->export.ops = NULL; /* poison myself */
	pthread_mutex_unlock(&myself->export.lock);
	pthread_mutex_destroy(&myself->export.lock);
	free(myself);  /* elvis has left the building */
	ReturnCode(fsal_error, retval);
}


