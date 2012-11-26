/*
 * Copyright (C) Paul Sheer, 2012
 * Author: Paul Sheer paulsheer@gmail.com
 *
 * contributeur : Jim Lieb          jlieb@panasas.com
 *                Philippe DENIEL   philippe.deniel@cea.fr
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#ifdef SUPPORT_LINUX_QUOTAS
#include <mntent.h>
#endif
#include <sys/statvfs.h>
#include <sys/quota.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include <FSAL/FSAL_POSIX/fsal_handle_syscalls.h>
#include "posix_methods.h"
#include <stdbool.h>

#include "scanmount.h"

#include "redblack.h"
#include "sockbuf.h"
#include "nodedb.h"


struct fsal_staticfsinfo_t *posix_staticinfo (struct fsal_module *hdl);

/* export object methods
 */

static fsal_status_t release (struct fsal_export *exp_hdl)
{
    struct posix_fsal_export *myself;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval = 0;

    myself = container_of (exp_hdl, struct posix_fsal_export, export);

    pthread_mutex_lock (&exp_hdl->lock);
    if (exp_hdl->refs > 0 || !glist_empty (&exp_hdl->handles)) {
        LogMajor (COMPONENT_FSAL, "POSIX release: export (0x%p)busy", exp_hdl);
        fsal_error = posix2fsal_error (EBUSY);
        retval = EBUSY;
        goto errout;
    }
    fsal_detach_export (exp_hdl->fsal, &exp_hdl->exports);
    free_export_ops(exp_hdl);
    if (myself->mntdir != NULL)
        free (myself->mntdir);
#ifdef SUPPORT_LINUX_QUOTAS
    if (myself->fstype != NULL)
        free (myself->fstype);
    if (myself->fs_spec != NULL)
        free (myself->fs_spec);
#endif
    pthread_mutex_unlock (&exp_hdl->lock);

    pthread_mutex_destroy (&exp_hdl->lock);
    free (myself);              /* elvis has left the building */
    return fsalstat (fsal_error, retval);

  errout:
    pthread_mutex_unlock (&exp_hdl->lock);
    return fsalstat (fsal_error, retval);
}

static fsal_status_t get_dynamic_info (struct fsal_export *exp_hdl,
				       const struct req_op_context *opctx,
				       fsal_dynamicfsinfo_t * infop)
{
    struct posix_fsal_export *myself;
    struct statvfs buffstatvfs;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval = 0;

    if (!infop) {
        fsal_error = ERR_FSAL_FAULT;
        goto out;
    }
    myself = container_of (exp_hdl, struct posix_fsal_export, export);
    assert (myself->mntdir);
    retval = statvfs (myself->mntdir, &buffstatvfs);
    if (retval < 0) {
        fsal_error = posix2fsal_error (errno);
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
    return fsalstat (fsal_error, retval);
}

static bool fs_supports (struct fsal_export *exp_hdl, fsal_fsinfo_options_t option)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_supports (info, option);
}

static uint64_t fs_maxfilesize (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_maxfilesize (info);
}

static uint32_t fs_maxread (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_maxread (info);
}

static uint32_t fs_maxwrite (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_maxwrite (info);
}

static uint32_t fs_maxlink (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_maxlink (info);
}

static uint32_t fs_maxnamelen (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_maxnamelen (info);
}

static uint32_t fs_maxpathlen (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_maxpathlen (info);
}

static fsal_fhexptype_t fs_fh_expire_type (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_fh_expire_type (info);
}

static gsh_time_t fs_lease_time (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_lease_time (info);
}

static fsal_aclsupp_t fs_acl_support (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_acl_support (info);
}

static attrmask_t fs_supported_attrs (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_supported_attrs (info);
}

static uint32_t fs_umask (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_umask (info);
}

static uint32_t fs_xattr_access_rights (struct fsal_export *exp_hdl)
{
    struct fsal_staticfsinfo_t *info;

    info = posix_staticinfo (exp_hdl->fsal);
    return fsal_xattr_access_rights (info);
}

#ifdef SUPPORT_LINUX_QUOTAS
/* get_quota
 * return quotas for this export.
 * path could cross a lower mount boundary which could
 * mask lower mount values with those of the export root
 * if this is a real issue, we can scan each time with setmntent()
 * better yet, compare st_dev of the file with st_dev of root_fd.
 * on linux, can map st_dev -> /proc/partitions name -> /dev/<name>
 */

static fsal_status_t get_quota (struct fsal_export *exp_hdl,
                                const char *filepath,
                                int quota_type, struct req_op_context *req_ctx, fsal_quota_t * pquota)
{
    struct posix_fsal_export *myself;
    struct dqblk fs_quota;
    struct stat path_stat;
    uid_t id;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval;

    myself = container_of (exp_hdl, struct posix_fsal_export, export);
    retval = stat (filepath, &path_stat);
    if (retval < 0) {
        LogMajor (COMPONENT_FSAL,
                  "POSIX get_quota, fstat: root_path: %s, errno=(%d) %s",
                  myself->mntdir, errno, strerror (errno));
        fsal_error = posix2fsal_error (errno);
        retval = errno;
        goto out;
    }
    if (path_stat.st_dev != myself->root_dev) {
        LogMajor (COMPONENT_FSAL,
                  "POSIX get_quota: crossed mount boundary! root_path: %s, quota path: %s", myself->mntdir, filepath);
        fsal_error = ERR_FSAL_FAULT;    /* maybe a better error? */
        retval = 0;
        goto out;
    }
    id = (quota_type == USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->caller_gid;
    memset ((char *) &fs_quota, 0, sizeof (struct dqblk));
    retval = quotactl (QCMD (Q_GETQUOTA, quota_type), myself->fs_spec, id, (caddr_t) & fs_quota);
    if (retval < 0) {
        fsal_error = posix2fsal_error (errno);
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
    return fsalstat (fsal_error, retval);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota (struct fsal_export *exp_hdl,
                                const char *filepath,
                                int quota_type,
                                struct req_op_context *req_ctx, fsal_quota_t * pquota, fsal_quota_t * presquota)
{
    struct posix_fsal_export *myself;
    struct dqblk fs_quota;
    struct stat path_stat;
    uid_t id;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval;

    myself = container_of (exp_hdl, struct posix_fsal_export, export);
    retval = stat (filepath, &path_stat);
    if (retval < 0) {
        LogMajor (COMPONENT_FSAL,
                  "POSIX set_quota, fstat: root_path: %s, errno=(%d) %s",
                  myself->mntdir, errno, strerror (errno));
        fsal_error = posix2fsal_error (errno);
        retval = errno;
        goto err;
    }
    if (path_stat.st_dev != myself->root_dev) {
        LogMajor (COMPONENT_FSAL,
                  "POSIX set_quota: crossed mount boundary! root_path: %s, quota path: %s", myself->mntdir, filepath);
        fsal_error = ERR_FSAL_FAULT;    /* maybe a better error? */
        retval = 0;
        goto err;
    }
    id = (quota_type == USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->caller_gid;
    memset ((char *) &fs_quota, 0, sizeof (struct dqblk));
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
    retval = quotactl (QCMD (Q_SETQUOTA, quota_type), myself->fs_spec, id, (caddr_t) & fs_quota);
    if (retval < 0) {
        fsal_error = posix2fsal_error (errno);
        retval = errno;
        goto err;
    }
    if (presquota != NULL) {
        return get_quota (exp_hdl, filepath, quota_type, req_ctx, presquota);
    }
  err:
    return fsalstat (fsal_error, retval);
}
#endif

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t extract_handle (struct fsal_export *exp_hdl, fsal_digesttype_t in_type, struct gsh_buffdesc *fh_desc)
{
    size_t fh_size;

    /* sanity checks */
    if (!fh_desc || !fh_desc->addr)
        return fsalstat (ERR_FSAL_FAULT, 0);

    fh_size = sizeof (struct handle_data);
    if (in_type == FSAL_DIGEST_NFSV2) {
        if (fh_desc->len < fh_size) {
            LogMajor (COMPONENT_FSAL, "V2 size too small for handle.  should be %lu, got %lu", fh_size, fh_desc->len);
            return fsalstat (ERR_FSAL_SERVERFAULT, 0);
        }
    } else if (in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size) {
        LogMajor (COMPONENT_FSAL, "Size mismatch for handle.  should be %lu, got %lu", fh_size, fh_desc->len);
        return fsalstat (ERR_FSAL_SERVERFAULT, 0);
    }
    fh_desc->len = fh_size;     /* pass back the actual size */
    return fsalstat (ERR_FSAL_NO_ERROR, 0);
}

/* posix_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void posix_export_ops_init (struct export_ops *ops)
{
    ops->release = release;
    ops->lookup_path = posix_lookup_path;
    ops->extract_handle = extract_handle;
    ops->create_handle = posix_create_handle;
    ops->get_fs_dynamic_info = get_dynamic_info;
    ops->fs_supports = fs_supports;
    ops->fs_maxfilesize = fs_maxfilesize;
    ops->fs_maxread = fs_maxread;
    ops->fs_maxwrite = fs_maxwrite;
    ops->fs_maxlink = fs_maxlink;
    ops->fs_maxnamelen = fs_maxnamelen;
    ops->fs_maxpathlen = fs_maxpathlen;
    ops->fs_fh_expire_type = fs_fh_expire_type;
    ops->fs_lease_time = fs_lease_time;
    ops->fs_acl_support = fs_acl_support;
    ops->fs_supported_attrs = fs_supported_attrs;
    ops->fs_umask = fs_umask;
    ops->fs_xattr_access_rights = fs_xattr_access_rights;
#ifdef SUPPORT_LINUX_QUOTAS
    ops->get_quota = get_quota;
    ops->set_quota = set_quota;
#endif
}

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

void posix_export_ops_init (struct export_ops *ops);
void posix_handle_ops_init (struct fsal_obj_ops *ops);

fsal_status_t posix_create_export (struct fsal_module *fsal_hdl,
                                   const char *export_path,
                                   const char *fs_options,
                                   struct exportlist__ *exp_entry,
                                   struct fsal_module *next_fsal,
                                   const struct fsal_up_vector *up_ops,
                                   struct fsal_export **export)
{
    struct posix_fsal_export *myself;
#ifdef SUPPORT_LINUX_QUOTAS
    FILE *fp;
    struct mntent *p_mnt;
    size_t pathlen, outlen = 0;
    char mntdir[MAXPATHLEN];    /* there has got to be a better way... */
    char fs_spec[MAXPATHLEN];
    char type[MAXNAMLEN];
#endif
    int retval = 0;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

    *export = NULL;             /* poison it first */
    if (export_path == NULL || strlen (export_path) == 0 || strlen (export_path) > MAXPATHLEN) {
        LogMajor (COMPONENT_FSAL, "nposix_create_export: export path empty or too big");
        return fsalstat (ERR_FSAL_INVAL, 0);
    }
    if (next_fsal != NULL) {
        LogCrit (COMPONENT_FSAL, "This module is not stackable");
        return fsalstat (ERR_FSAL_INVAL, 0);
    }

    myself = malloc (sizeof (struct posix_fsal_export));
    if (myself == NULL) {
        LogMajor (COMPONENT_FSAL, "posix_fsal_create: out of memory for object");
        return fsalstat (posix2fsal_error (errno), errno);
    }
    memset (myself, 0, sizeof (struct posix_fsal_export));
    myself->magic = POSIX_FSAL_EXPORT_MAGIC;

    retval = fsal_export_init (&myself->export, exp_entry);
    if (retval != 0)
        goto errout;            /* seriously bad */
    posix_export_ops_init (myself->export.ops);
    posix_handle_ops_init (myself->export.obj_ops);
    myself->export.up_ops = up_ops;

    /* lock myself before attaching to the fsal.
     * keep myself locked until done with creating myself.
     */

    pthread_mutex_lock (&myself->export.lock);
    retval = fsal_attach_export (fsal_hdl, &myself->export.exports);
    if (retval != 0)
        goto errout;            /* seriously bad */
    myself->export.fsal = fsal_hdl;

#ifdef SUPPORT_LINUX_QUOTAS
    /* start looking for the mount point */
    fp = setmntent (MOUNTED, "r");
    if (fp == NULL) {
        retval = errno;
        LogCrit (COMPONENT_FSAL, "Error %d in setmntent(%s): %s", retval, MOUNTED, strerror (retval));
        fsal_error = posix2fsal_error (retval);
        goto errout;
    }
    while ((p_mnt = getmntent (fp)) != NULL) {
        if (p_mnt->mnt_dir != NULL) {
            pathlen = strlen (p_mnt->mnt_dir);
            if (pathlen > outlen) {
                if (strcmp (p_mnt->mnt_dir, "/") == 0) {
                    outlen = pathlen;
                    strncpy (mntdir, p_mnt->mnt_dir, MAXPATHLEN);
                    strncpy (type, p_mnt->mnt_type, MAXNAMLEN);
                    strncpy (fs_spec, p_mnt->mnt_fsname, MAXPATHLEN);
                } else if ((strncmp (export_path,
                                     p_mnt->mnt_dir,
                                     pathlen) == 0) &&
                           ((export_path[pathlen] == '/') || (export_path[pathlen] == '\0'))) {
                    outlen = pathlen;
                    strncpy (mntdir, p_mnt->mnt_dir, MAXPATHLEN);
                    strncpy (type, p_mnt->mnt_type, MAXNAMLEN);
                    strncpy (fs_spec, p_mnt->mnt_fsname, MAXPATHLEN);
                }
            }
        }
    }
    endmntent (fp);
    if (outlen <= 0) {
        LogCrit (COMPONENT_FSAL, "No mount entry matches '%s' in %s", export_path, MOUNTED);
        fsal_error = ERR_FSAL_NOENT;
        goto errout;
    }

    {
        struct stat root_stat;
        struct file_handle *fh = alloca (sizeof (struct file_handle)
                                         + HANDLE_SIZE);

        memset (fh, 0, sizeof (struct file_handle) + HANDLE_SIZE);
        fh->handle_bytes = HANDLE_SIZE;
        retval = lstat (mntdir, &root_stat);
        if (retval < 0) {
            LogMajor (COMPONENT_FSAL,
                      "fstat: root_path: %s, errno=(%d) %s", mntdir, errno, strerror (errno));
            fsal_error = posix2fsal_error (errno);
            retval = errno;
            goto errout;
        }
        myself->root_dev = root_stat.st_dev;
    }
    myself->fstype = strdup (type);
    myself->fs_spec = strdup (fs_spec);
#endif

#ifdef SUPPORT_LINUX_QUOTAS
    myself->mntdir = strdup (mntdir);
#else
    myself->mntdir = strdup (export_path);
#endif
    *export = &myself->export;
    pthread_mutex_unlock (&myself->export.lock);
    return fsalstat (ERR_FSAL_NO_ERROR, 0);

  errout:
    if (myself->mntdir != NULL)
        free (myself->mntdir);
#ifdef SUPPORT_LINUX_QUOTAS
    if (myself->fstype != NULL)
        free (myself->fstype);
    if (myself->fs_spec != NULL)
        free (myself->fs_spec);
#endif
    free_export_ops(&myself->export);
    pthread_mutex_unlock (&myself->export.lock);
    pthread_mutex_destroy (&myself->export.lock);
    free (myself);              /* elvis has left the building */
    return fsalstat (fsal_error, retval);
}


