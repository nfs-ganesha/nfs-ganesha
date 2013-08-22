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
 * GPFS FSAL export object
 */

#include "config.h"

#include <fcntl.h>
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
#include "gpfs_methods.h"

/*
 * GPFS internal export
 */

struct gpfs_fsal_export {
	struct fsal_export export;
	char *mntdir;
	char *fs_spec;
	char *fstype;
	int root_fd;
	dev_t root_dev;
	struct gpfs_file_handle *root_handle;
	bool pnfs_enabled;
};

/* helpers to/from other GPFS objects
 */

struct fsal_staticfsinfo_t *gpfs_staticinfo(struct fsal_module *hdl);

int gpfs_get_root_fd(struct fsal_export *exp_hdl) {
	struct gpfs_fsal_export *myself;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);
	return myself->root_fd;
}

/* export object methods
 */

static fsal_status_t release(struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_export *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);

	pthread_mutex_lock(&exp_hdl->lock);
	if(exp_hdl->refs > 0 || !glist_empty(&exp_hdl->handles)) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS release: export (0x%p)busy",
			 exp_hdl);
		fsal_error = posix2fsal_error(EBUSY);
		retval = EBUSY;
		goto errout;
	}
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);
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
	pthread_mutex_unlock(&exp_hdl->lock);

	pthread_mutex_destroy(&exp_hdl->lock);
	free(myself);  /* elvis has left the building */
	return fsalstat(fsal_error, retval);

errout:
	pthread_mutex_unlock(&exp_hdl->lock);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
                                      const struct req_op_context *opctx,
			              fsal_dynamicfsinfo_t *infop)
{
	struct gpfs_fsal_export *myself;
	struct statvfs buffstatgpfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if( !infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);
	retval = fstatvfs(myself->root_fd, &buffstatgpfs);
	if(retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	infop->total_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_blocks;
	infop->free_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bfree;
	infop->avail_bytes = buffstatgpfs.f_frsize * buffstatgpfs.f_bavail;
	infop->total_files = buffstatgpfs.f_files;
	infop->free_files = buffstatgpfs.f_ffree;
	infop->avail_files = buffstatgpfs.f_favail;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

out:
	return fsalstat(fsal_error, retval);
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
			       const char * filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
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
	if(retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS get_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if(path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS get_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, filepath);
		fsal_error = ERR_FSAL_FAULT; /* maybe a better error? */
		retval = 0;
		goto out;
	}
	id = (quota_type == USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->caller_gid;
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
	return fsalstat(fsal_error, retval);	
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t * pquota,
			       fsal_quota_t * presquota)
{
	struct gpfs_fsal_export *myself;
	struct dqblk fs_quota;
	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(exp_hdl, struct gpfs_fsal_export, export);
	retval = stat(filepath, &path_stat);
	if(retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS set_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno, strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if(path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "GPFS set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, filepath);
		fsal_error = ERR_FSAL_FAULT; /* maybe a better error? */
		retval = 0;
		goto err;
	}
	id = (quota_type == USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->caller_gid;
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
		return get_quota(exp_hdl, filepath, quota_type,
				 req_ctx, presquota);
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
	if( !fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct gpfs_file_handle *)fh_desc->addr;
	fh_size = gpfs_sizeof_handle(hdl);
	if(in_type == FSAL_DIGEST_NFSV2) {
		if(fh_desc->len < fh_size) {
			LogMajor(COMPONENT_FSAL,
				 "V2 size too small for handle.  should be %lu, got %lu",
				 fh_size, fh_desc->len);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	} else if(in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = hdl->handle_key_size;  /* pass back the key size */
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
nfsstat4
gpfs_create_ds_handle(struct fsal_export *const export_pub,
                      const struct gsh_buffdesc *const desc,
                      struct fsal_ds_handle **const ds_pub)
{
        /* Handle to be created */
        struct gpfs_ds *ds = NULL;

        *ds_pub = NULL;

        if (desc->len != sizeof( struct gpfs_file_handle)) {
                return NFS4ERR_BADHANDLE;
        }
        ds = gsh_calloc(1, sizeof(struct gpfs_ds));

        if (ds == NULL) {
                return NFS4ERR_SERVERFAULT;
        }
        /* Connect lazily when a FILE_SYNC4 write forces us to, not
           here. */

        ds->connected = false;

        memcpy(&ds->wire, desc->addr, desc->len);

        if (fsal_ds_handle_init(&ds->ds,
                                export_pub->ds_ops,
                                export_pub)) {
                gsh_free(ds);
                return NFS4ERR_SERVERFAULT;
        }
        *ds_pub = &ds->ds;

        return NFS4_OK;
}

verifier4 GPFS_write_verifier;  /* NFS V4 write verifier */

static void
gpfs_verifier(struct gsh_buffdesc *verf_desc)
{
        memcpy(verf_desc->addr, &GPFS_write_verifier, verf_desc->len);
}

void
set_gpfs_verifier(verifier4 *verifier)
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

void gpfs_handle_ops_init(struct fsal_obj_ops *ops);

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t gpfs_create_export(struct fsal_module *fsal_hdl,
				const char *export_path,
				const char *fs_options,
				struct exportlist *exp_entry,
				struct fsal_module *next_fsal,
                                const struct fsal_up_vector *up_ops,
				struct fsal_export **export)
{
	struct gpfs_fsal_export *myself;
	FILE *fp;
	struct mntent *p_mnt;
	size_t pathlen, outlen = 0;
	char mntdir[MAXPATHLEN + 1];  /* there has got to be a better way... */
	char fs_spec[MAXPATHLEN + 1];
	char type[MAXNAMLEN + 1];
	int retval = 0;
	fsal_status_t status;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        struct gpfs_fsal_up_ctx *gpfs_fsal_up_ctx;
        struct gpfs_fsal_up_ctx up_ctx;
        bool_t start_fsal_up_thread = FALSE;

	*export = NULL; /* poison it first */
	if(export_path == NULL
	   || strlen(export_path) == 0
	   || strlen(export_path) > MAXPATHLEN) {
		LogMajor(COMPONENT_FSAL,
			 "gpfs_create_export: export path empty or too big");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	if(next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL,
			"This module is not stackable");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	myself = malloc(sizeof(struct gpfs_fsal_export));
	if(myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "gpfs_fsal_create: out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}
	memset(myself, 0, sizeof(struct gpfs_fsal_export));
	myself->root_fd = -1;

        retval = fsal_internal_version();
        LogInfo(COMPONENT_FSAL, "GPFS get version is %d options 0x%X",
                               retval, exp_entry->export_perms.options);

        retval = fsal_export_init(&myself->export,
				  exp_entry);
        if(retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "gpfs_fsal_create: out of memory for object");
		return fsalstat(posix2fsal_error(retval), retval);
	}
	gpfs_export_ops_init(myself->export.ops);
	gpfs_handle_ops_init(myself->export.obj_ops);
        myself->export.up_ops = up_ops;

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
				} else if((strncmp(export_path,
						  p_mnt->mnt_dir,
						  pathlen) == 0) &&
					  ((export_path[pathlen] == '/') ||
					   (export_path[pathlen] == '\0'))) {
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
			export_path, MOUNTED);
		fsal_error = ERR_FSAL_NOENT;
		goto errout;
        }
	myself->root_fd = open(mntdir, O_RDONLY|O_DIRECTORY);
	if(myself->root_fd < 0) {
		LogMajor(COMPONENT_FSAL,
			 "Could not open GPFS mount point %s: rc = %d",
			 mntdir, errno);
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto errout;
	} else {
		struct stat root_stat;
		struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));

		memset(fh, 0, sizeof(struct gpfs_file_handle));
		fh->handle_size = OPENHANDLE_HANDLE_LEN;
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
		status = fsal_internal_get_handle_at(myself->root_fd, NULL, fh);
                if(FSAL_IS_ERROR(status)) {
			fsal_error = retval = status.major;
			retval = errno;
			LogMajor(COMPONENT_FSAL,
				 "name_to_handle: root_path: %s, root_fd=%d, retval=%d",
				 mntdir, myself->root_fd, retval);
			goto errout;
		}
		myself->root_handle = malloc(sizeof(struct gpfs_file_handle));
		if(myself->root_handle == NULL) {
			LogMajor(COMPONENT_FSAL,
				 "memory for root handle, errno=(%d) %s",
				 errno, strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		memcpy(myself->root_handle, fh, sizeof(struct gpfs_file_handle));
	}
	myself->fstype = strdup(type);
	myself->fs_spec = strdup(fs_spec);
	myself->mntdir = strdup(mntdir);
	*export = &myself->export;

       /* Make sure the FSAL UP context list is initialized */
       if(glist_null(&gpfs_fsal_up_ctx_list))
         glist_init(&gpfs_fsal_up_ctx_list);

       up_ctx.gf_fsid[0] = myself->root_handle->handle_fsid[0];
       up_ctx.gf_fsid[1] = myself->root_handle->handle_fsid[1];
       gpfs_fsal_up_ctx = gpfsfsal_find_fsal_up_context(&up_ctx);

       if(gpfs_fsal_up_ctx == NULL)
         {
           gpfs_fsal_up_ctx = gsh_calloc(1, sizeof(struct gpfs_fsal_up_ctx));

           if(gpfs_fsal_up_ctx == NULL)
             {
               LogFatal(COMPONENT_FSAL,
                        "Out of memory can not continue.");
             }

           /* Initialize the gpfs_fsal_up_ctx */
           glist_init(&gpfs_fsal_up_ctx->gf_exports);
           gpfs_fsal_up_ctx->gf_export = &myself->export;
           gpfs_fsal_up_ctx->gf_fd = myself->root_fd;
           gpfs_fsal_up_ctx->gf_fsid[0] = myself->root_handle->handle_fsid[0];
           gpfs_fsal_up_ctx->gf_fsid[1] = myself->root_handle->handle_fsid[1];
           gpfs_fsal_up_ctx->gf_exp_id = exp_entry->id;

           /* Add it to the list of contexts */
           glist_add_tail(&gpfs_fsal_up_ctx_list, &gpfs_fsal_up_ctx->gf_list);

           start_fsal_up_thread = TRUE;
         }

         if(start_fsal_up_thread)
           {
             pthread_attr_t attr_thr;

             memset(&attr_thr, 0, sizeof(attr_thr));

             /* Initialization of thread attributes borrowed from nfs_init.c */
             if(pthread_attr_init(&attr_thr) != 0)
               LogCrit(COMPONENT_THREAD, "can't init pthread's attributes");

             if(pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
               LogCrit(COMPONENT_THREAD, "can't set pthread's scope");

             if(pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE) != 0)
               LogCrit(COMPONENT_THREAD, "can't set pthread's join state");

             if(pthread_attr_setstacksize(&attr_thr, 2116488) != 0)
               LogCrit(COMPONENT_THREAD, "can't set pthread's stack size");

             retval = pthread_create(&gpfs_fsal_up_ctx->gf_thread,
                                 &attr_thr,
                                 GPFSFSAL_UP_Thread,
                                 gpfs_fsal_up_ctx);

             if(retval != 0)
               {
                 LogFatal(COMPONENT_THREAD,
                          "Could not create GPFSFSAL_UP_Thread, error = %d (%s)",
                          errno, strerror(errno));
                       fsal_error = posix2fsal_error(errno);
                       goto errout;
               }
           }

	pthread_mutex_unlock(&myself->export.lock);

        gpfs_ganesha(OPENHANDLE_GET_VERIFIER, &GPFS_write_verifier);

	myself->pnfs_enabled = myself->export.ops->fs_supports(&myself->export,
	                                                fso_pnfs_ds_supported);
	if (myself->pnfs_enabled) {
		LogInfo(COMPONENT_FSAL,
			"gpfs_fsal_create: pnfs was enabled for [%s]",
			export_path);
		export_ops_pnfs(myself->export.ops);
		handle_ops_pnfs(myself->export.obj_ops);
                ds_ops_init(myself->export.ds_ops);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

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
	free_export_ops(&myself->export);
	pthread_mutex_unlock(&myself->export.lock);
	pthread_mutex_destroy(&myself->export.lock);
	free(myself);  /* elvis has left the building */
	return fsalstat(fsal_error, retval);
}


