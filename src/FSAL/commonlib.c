/*
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
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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

/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file fsal_commonlib.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Common functions for and private to FSAL modules.
 *
 * The prime requirement for functions to be here is that they operate only
 * on the public part of the FSAL api and are therefore sharable by all fsal
 * implementations.
 */
#include "config.h"

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <os/quota.h>

#include "common_utils.h"
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include "gsh_list.h"
#ifdef USE_BLKID
#include <blkid/blkid.h>
#include <uuid/uuid.h>
#endif
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_private.h"
#include "fsal_convert.h"

/* fsal_module to fsal_export helpers
 */

/* fsal_attach_export
 * called from the FSAL's create_export method with a reference on the fsal.
 */

int fsal_attach_export(struct fsal_module *fsal_hdl,
		       struct glist_head *obj_link)
{
	int retval = 0;

	if (atomic_fetch_int32_t(&fsal_hdl->refcount) > 0) {
		glist_add(&fsal_hdl->exports, obj_link);
	} else {
		LogCrit(COMPONENT_CONFIG,
			"Attaching export with out holding a reference!. hdl= = 0x%p",
			fsal_hdl);
		retval = EINVAL;
	}
	return retval;
}

/* fsal_detach_export
 * called by an export when it is releasing itself.
 * does not require a reference to be taken.  The list has
 * kept the fsal "busy".
 */

void fsal_detach_export(struct fsal_module *fsal_hdl,
			struct glist_head *obj_link)
{
	PTHREAD_RWLOCK_wrlock(&fsal_hdl->lock);
	glist_del(obj_link);
	PTHREAD_RWLOCK_unlock(&fsal_hdl->lock);
}

/**
 * @brief Initialize export ops vectors
 *
 * @param[in] exp Export handle
 *
 */

int fsal_export_init(struct fsal_export *exp)
{
	memcpy(&exp->exp_ops, &def_export_ops, sizeof(struct export_ops));
	return 0;
}

/**
 * @brief Free export ops vectors
 *
 * Free the memory allocated by init_export_ops. Poison pointers.
 *
 * @param[in] exp_hdl Export handle
 *
 */

void free_export_ops(struct fsal_export *exp_hdl)
{
	memset(&exp_hdl->exp_ops, 0, sizeof(exp_hdl->exp_ops));	/* poison */
}

/* fsal_export to fsal_obj_handle helpers
 */

void fsal_obj_handle_init(struct fsal_obj_handle *obj, struct fsal_export *exp,
			  object_file_type_t type)
{
	pthread_rwlockattr_t attrs;

	memcpy(&obj->obj_ops, &def_handle_ops, sizeof(struct fsal_obj_ops));
	obj->fsal = exp->fsal;
	obj->type = type;
	obj->attributes.expire_time_attr = 0;
	pthread_rwlockattr_init(&attrs);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&attrs,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	PTHREAD_RWLOCK_init(&obj->lock, &attrs);

	PTHREAD_RWLOCK_wrlock(&obj->fsal->lock);
	glist_add(&obj->fsal->handles, &obj->handles);
	PTHREAD_RWLOCK_unlock(&obj->fsal->lock);
}

void fsal_obj_handle_fini(struct fsal_obj_handle *obj)
{
	PTHREAD_RWLOCK_wrlock(&obj->fsal->lock);
	glist_del(&obj->handles);
	PTHREAD_RWLOCK_unlock(&obj->fsal->lock);
	PTHREAD_RWLOCK_destroy(&obj->lock);
	memset(&obj->obj_ops, 0, sizeof(obj->obj_ops));	/* poison myself */
	obj->fsal = NULL;
}

/* fsal_module to fsal_pnfs_ds helpers
 */

void fsal_pnfs_ds_init(struct fsal_pnfs_ds *pds, struct fsal_module *fsal)
{
	pthread_rwlockattr_t attrs;

	pds->refcount = 1;	/* we start out with a reference */
	fsal->m_ops.fsal_pnfs_ds_ops(&pds->s_ops);
	pds->fsal = fsal;

	pthread_rwlockattr_init(&attrs);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&attrs,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	PTHREAD_RWLOCK_init(&pds->lock, &attrs);
	glist_init(&pds->ds_handles);

	PTHREAD_RWLOCK_wrlock(&fsal->lock);
	glist_add(&fsal->servers, &pds->server);
	PTHREAD_RWLOCK_unlock(&fsal->lock);
}

void fsal_pnfs_ds_fini(struct fsal_pnfs_ds *pds)
{
	PTHREAD_RWLOCK_wrlock(&pds->fsal->lock);
	glist_del(&pds->server);
	PTHREAD_RWLOCK_unlock(&pds->fsal->lock);
	PTHREAD_RWLOCK_destroy(&pds->lock);
	memset(&pds->s_ops, 0, sizeof(pds->s_ops));	/* poison myself */
	pds->fsal = NULL;
}

/* fsal_pnfs_ds to fsal_ds_handle helpers
 */

void fsal_ds_handle_init(struct fsal_ds_handle *dsh, struct fsal_pnfs_ds *pds)
{
	dsh->refcount = 1;	/* we start out with a reference */
	pds->s_ops.fsal_dsh_ops(&dsh->dsh_ops);
	dsh->pds = pds;

	PTHREAD_RWLOCK_wrlock(&pds->lock);
	glist_add(&pds->ds_handles, &dsh->ds_handle);
	PTHREAD_RWLOCK_unlock(&pds->lock);
}

void fsal_ds_handle_fini(struct fsal_ds_handle *dsh)
{
	PTHREAD_RWLOCK_wrlock(&dsh->pds->lock);
	glist_del(&dsh->ds_handle);
	PTHREAD_RWLOCK_unlock(&dsh->pds->lock);

	memset(&dsh->dsh_ops, 0, sizeof(dsh->dsh_ops));	/* poison myself */
	dsh->pds = NULL;
}

/**
 * @brief FSAL error code to error message
 *
 * @param[in] fsal_err Error code
 *
 * @return Error message, empty string if not found.
 */

const char *msg_fsal_err(fsal_errors_t fsal_err)
{
	switch (fsal_err) {
	case ERR_FSAL_NO_ERROR:
		return "No error";
	case ERR_FSAL_PERM:
		return "Forbidden action";
	case ERR_FSAL_NOENT:
		return "No such file or directory";
	case ERR_FSAL_IO:
		return "I/O error";
	case ERR_FSAL_NXIO:
		return "No such device or address";
	case ERR_FSAL_NOMEM:
		return "Not enough memory";
	case ERR_FSAL_ACCESS:
		return "Permission denied";
	case ERR_FSAL_FAULT:
		return "Bad address";
	case ERR_FSAL_EXIST:
		return "This object already exists";
	case ERR_FSAL_XDEV:
		return "This operation can't cross filesystems";
	case ERR_FSAL_NOTDIR:
		return "This object is not a directory";
	case ERR_FSAL_ISDIR:
		return "Directory used in a nondirectory operation";
	case ERR_FSAL_INVAL:
		return "Invalid object type";
	case ERR_FSAL_FBIG:
		return "File exceeds max file size";
	case ERR_FSAL_NOSPC:
		return "No space left on filesystem";
	case ERR_FSAL_ROFS:
		return "Read-only filesystem";
	case ERR_FSAL_MLINK:
		return "Too many hard links";
	case ERR_FSAL_DQUOT:
		return "Quota exceeded";
	case ERR_FSAL_NAMETOOLONG:
		return "Max name length exceeded";
	case ERR_FSAL_NOTEMPTY:
		return "The directory is not empty";
	case ERR_FSAL_STALE:
		return "The file no longer exists";
	case ERR_FSAL_BADHANDLE:
		return "Illegal filehandle";
	case ERR_FSAL_BADCOOKIE:
		return "Invalid cookie";
	case ERR_FSAL_NOTSUPP:
		return "Operation not supported";
	case ERR_FSAL_TOOSMALL:
		return "Output buffer too small";
	case ERR_FSAL_SERVERFAULT:
		return "Undefined server error";
	case ERR_FSAL_BADTYPE:
		return "Invalid type for create operation";
	case ERR_FSAL_DELAY:
		return "File busy, retry";
	case ERR_FSAL_FHEXPIRED:
		return "Filehandle expired";
	case ERR_FSAL_SYMLINK:
		return "This is a symbolic link, should be file/directory";
	case ERR_FSAL_ATTRNOTSUPP:
		return "Attribute not supported";
	case ERR_FSAL_NOT_INIT:
		return "Filesystem not initialized";
	case ERR_FSAL_ALREADY_INIT:
		return "Filesystem already initialised";
	case ERR_FSAL_BAD_INIT:
		return "Filesystem initialisation error";
	case ERR_FSAL_SEC:
		return "Security context error";
	case ERR_FSAL_NO_QUOTA:
		return "No Quota available";
	case ERR_FSAL_NOT_OPENED:
		return "File/directory not opened";
	case ERR_FSAL_DEADLOCK:
		return "Deadlock";
	case ERR_FSAL_OVERFLOW:
		return "Overflow";
	case ERR_FSAL_INTERRUPT:
		return "Operation Interrupted";
	case ERR_FSAL_BLOCKED:
		return "Lock Blocked";
	case ERR_FSAL_SHARE_DENIED:
		return "Share Denied";
	case ERR_FSAL_TIMEOUT:
		return "Timeout";
	case ERR_FSAL_FILE_OPEN:
		return "File Open";
	case ERR_FSAL_UNION_NOTSUPP:
		return "Union Not Supported";
	case ERR_FSAL_IN_GRACE:
		return "Server in Grace";
        case ERR_FSAL_BAD_RANGE:
                return "Lock not in allowable range";
	}

	return "Unknown FSAL error";
}

/**
 * @brief Dump and fsal_staticfsinfo_t to a log
 *
 * This is used for debugging
 *
 * @param[in] info The info to dump
 */
void display_fsinfo(struct fsal_staticfsinfo_t *info)
{
	LogDebug(COMPONENT_FSAL, "FileSystem info: {");
	LogDebug(COMPONENT_FSAL, "  maxfilesize  = %" PRIX64 "    ",
		 (uint64_t) info->maxfilesize);
	LogDebug(COMPONENT_FSAL, "  maxlink  = %" PRIu32, info->maxlink);
	LogDebug(COMPONENT_FSAL, "  maxnamelen  = %" PRIu32, info->maxnamelen);
	LogDebug(COMPONENT_FSAL, "  maxpathlen  = %" PRIu32, info->maxpathlen);
	LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ", info->no_trunc);
	LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
		 info->chown_restricted);
	LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
		 info->case_insensitive);
	LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
		 info->case_preserving);
	LogDebug(COMPONENT_FSAL, "  link_support  = %d  ", info->link_support);
	LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
		 info->symlink_support);
	LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ", info->lock_support);
	LogDebug(COMPONENT_FSAL, "  lock_support_owner  = %d  ",
		 info->lock_support_owner);
	LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
		 info->lock_support_async_block);
	LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ", info->named_attr);
	LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
		 info->unique_handles);
	LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ", info->acl_support);
	LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ", info->cansettime);
	LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ", info->homogenous);
	LogDebug(COMPONENT_FSAL, "  supported_attrs  = %" PRIX64,
		 info->supported_attrs);
	LogDebug(COMPONENT_FSAL, "  maxread  = %" PRIu64, info->maxread);
	LogDebug(COMPONENT_FSAL, "  maxwrite  = %" PRIu64, info->maxwrite);
	LogDebug(COMPONENT_FSAL, "  umask  = %X ", info->umask);
	LogDebug(COMPONENT_FSAL, "  auth_exportpath_xdev  = %d  ",
		 info->auth_exportpath_xdev);
	LogDebug(COMPONENT_FSAL, "  xattr_access_rights = %#o ",
		 info->xattr_access_rights);
	LogDebug(COMPONENT_FSAL, "  accesscheck_support  = %d  ",
		 info->accesscheck_support);
	LogDebug(COMPONENT_FSAL, "  share_support  = %d  ",
		 info->share_support);
	LogDebug(COMPONENT_FSAL, "  share_support_owner  = %d  ",
		 info->share_support_owner);
	LogDebug(COMPONENT_FSAL, "  delegations = %d  ",
		 info->delegations);
	LogDebug(COMPONENT_FSAL, "  pnfs_mds = %d  ",
		 info->pnfs_mds);
	LogDebug(COMPONENT_FSAL, "  pnfs_ds = %d  ",
		 info->pnfs_ds);
	LogDebug(COMPONENT_FSAL, "  fsal_trace = %d  ",
		 info->fsal_trace);
	LogDebug(COMPONENT_FSAL, "  fsal_grace = %d  ",
		 info->fsal_grace);
	LogDebug(COMPONENT_FSAL, "}");
}

int open_dir_by_path_walk(int first_fd, const char *path, struct stat *stat)
{
	char *name, *rest, *p;
	int fd = first_fd, len, rc, err;

	/* Get length of the path */
	len = strlen(path);

	/* Strip terminating '/' by shrinking length */
	while (path[len-1] == '/' && len > 1)
		len--;

	/* Allocate space for duplicate */
	name = alloca(len + 1);

	/* Copy the string */
	memcpy(name, path, len);
	/* Terminate it */

	name[len] = '\0';

	/* Determine if this is a relative path off some directory
	 * or an absolute path. If absolute path, open root dir.
	 */
	if (first_fd == -1) {
		if (name[0] != '/') {
			LogInfo(COMPONENT_FSAL,
				"Absolute path %s must start with '/'",
				path);
			return -EINVAL;
		}
		rest = name + 1;
		fd = open("/", O_RDONLY | O_NOFOLLOW);
	} else {
		rest = name;
		fd = dup(first_fd);
	}

	if (fd == -1) {
		err = errno;
		LogCrit(COMPONENT_FSAL,
			"Failed initial directory open for path %s with %s",
			path, strerror(err));
		return -err;
	}

	while (rest[0] != '\0') {
		/* Find the end of this path element */
		p = index(rest, '/');

		/* NUL terminate element (if not at end of string */
		if (p != NULL)
			*p = '\0';

		/* Skip extra '/' */
		if (rest[0] == '\0') {
			rest++;
			continue;
		}

		/* Disallow .. elements... */
		if (strcmp(rest, "..") == 0) {
			close(fd);
			LogInfo(COMPONENT_FSAL,
				"Failed due to '..' element in path %s",
				path);
			return -EACCES;
		}

		/* Open the next directory in the path */
		rc = openat(fd, rest, O_RDONLY | O_NOFOLLOW);
		err = errno;

		close(fd);

		if (rc == -1) {
			LogDebug(COMPONENT_FSAL,
				 "openat(%s) in path %s failed with %s",
				 rest, path, strerror(err));
			return -err;
		}

		fd = rc;

		/* Done, break out */
		if (p == NULL)
			break;

		/* Skip the '/' */
		rest = p+1;
	}

	rc = fstat(fd, stat);
	err = errno;

	if (rc == -1) {
		close(fd);
		LogDebug(COMPONENT_FSAL,
			 "fstat %s failed with %s",
			 path, strerror(err));
		return -err;
	}

	if (!S_ISDIR(stat->st_mode)) {
		close(fd);
		LogInfo(COMPONENT_FSAL,
			"Path %s is not a directory",
			path);
		return -ENOTDIR;
	}

	return fd;
}

pthread_rwlock_t fs_lock = PTHREAD_RWLOCK_INITIALIZER;

struct glist_head posix_file_systems = {
	&posix_file_systems, &posix_file_systems
};

struct avltree avl_fsid;
struct avltree avl_dev;

static inline int
fsal_fs_cmpf_fsid(const struct avltree_node *lhs,
		  const struct avltree_node *rhs)
{
	struct fsal_filesystem *lk, *rk;

	lk = avltree_container_of(lhs, struct fsal_filesystem, avl_fsid);
	rk = avltree_container_of(rhs, struct fsal_filesystem, avl_fsid);

	if (lk->fsid.major < rk->fsid.major)
		return -1;

	if (lk->fsid.major > rk->fsid.major)
		return 1;

	if (lk->fsid_type == FSID_MAJOR_64 &&
	    rk->fsid_type == FSID_MAJOR_64)
		return 0;

	/* Treat no minor as strictly less than any minor */
	if (lk->fsid_type == FSID_MAJOR_64)
		return -1;

	/* Treat no minor as strictly less than any minor */
	if (rk->fsid_type == FSID_MAJOR_64)
		return 1;

	if (lk->fsid.minor < rk->fsid.minor)
		return -1;

	if (lk->fsid.minor > rk->fsid.minor)
		return 1;

	return 0;
}

static inline struct fsal_filesystem *
avltree_inline_fsid_lookup(const struct avltree_node *key)
{
	struct avltree_node *node = avl_fsid.root;
	int res = 0;

	while (node) {
		res = fsal_fs_cmpf_fsid(node, key);
		if (res == 0)
			return avltree_container_of(node,
						    struct fsal_filesystem,
						    avl_fsid);
		if (res > 0)
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

static inline int
fsal_fs_cmpf_dev(const struct avltree_node *lhs,
		 const struct avltree_node *rhs)
{
	struct fsal_filesystem *lk, *rk;

	lk = avltree_container_of(lhs, struct fsal_filesystem, avl_dev);
	rk = avltree_container_of(rhs, struct fsal_filesystem, avl_dev);

	if (lk->dev.major < rk->dev.major)
		return -1;

	if (lk->dev.major > rk->dev.major)
		return 1;

	if (lk->dev.minor < rk->dev.minor)
		return -1;

	if (lk->dev.minor > rk->dev.minor)
		return 1;

	return 0;
}

static inline struct fsal_filesystem *
avltree_inline_dev_lookup(const struct avltree_node *key)
{
	struct avltree_node *node = avl_dev.root;
	int res = 0;

	while (node) {
		res = fsal_fs_cmpf_dev(node, key);
		if (res == 0)
			return avltree_container_of(node,
						    struct fsal_filesystem,
						    avl_dev);
		if (res > 0)
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

void remove_fs(struct fsal_filesystem *fs)
{
	if (fs->in_fsid_avl)
		avltree_remove(&fs->avl_fsid, &avl_fsid);

	if (fs->in_dev_avl)
		avltree_remove(&fs->avl_dev, &avl_dev);

	glist_del(&fs->siblings);
	glist_del(&fs->filesystems);
}

void free_fs(struct fsal_filesystem *fs)
{
	if (fs->path != NULL)
		gsh_free(fs->path);

	if (fs->device != NULL)
		gsh_free(fs->device);

	if (fs->type != NULL)
		gsh_free(fs->type);

	gsh_free(fs);
}

int re_index_fs_fsid(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type,
		     uint64_t major,
		     uint64_t minor)
{
	struct avltree_node *node;
	struct fsal_fsid__ old_fsid = fs->fsid;
	enum fsid_type old_fsid_type = fs->fsid_type;

	LogDebug(COMPONENT_FSAL,
		 "Reindex %s from 0x%016"PRIx64".0x%016"PRIx64
		 " to 0x%016"PRIx64".0x%016"PRIx64,
		 fs->path,
		 fs->fsid.major, fs->fsid.minor,
		 major, minor);

	/* It is not valid to use this routine to
	 * remove fs from index.
	 */
	if (fsid_type == FSID_NO_TYPE)
		return -EINVAL;

	if (fs->in_fsid_avl)
		avltree_remove(&fs->avl_fsid, &avl_fsid);

	fs->fsid.major = major;
	fs->fsid.minor = minor;
	fs->fsid_type = fsid_type;

	node = avltree_insert(&fs->avl_fsid, &avl_fsid);

	if (node != NULL) {
		/* This is a duplicate file system. */
		fs->fsid = old_fsid;
		fs->fsid_type = old_fsid_type;
		if (fs->in_fsid_avl) {
			/* Put it back where it was */
			node = avltree_insert(&fs->avl_fsid, &avl_fsid);
			if (node != NULL) {
				LogFatal(COMPONENT_FSAL,
					 "Could not re-insert filesystem %s",
					 fs->path);
			}
		}
		return -EEXIST;
	}

	fs->in_fsid_avl = true;

	return 0;
}

int re_index_fs_dev(struct fsal_filesystem *fs,
		    struct fsal_dev__ *dev)
{
	struct avltree_node *node;
	struct fsal_dev__ old_dev = fs->dev;

	/* It is not valid to use this routine to
	 * remove fs from index.
	 */
	if (dev == NULL)
		return -EINVAL;

	if (fs->in_dev_avl)
		avltree_remove(&fs->avl_dev, &avl_dev);

	fs->dev = *dev;

	node = avltree_insert(&fs->avl_dev, &avl_dev);

	if (node != NULL) {
		/* This is a duplicate file system. */
		fs->dev = old_dev;
		if (fs->in_dev_avl) {
			/* Put it back where it was */
			node = avltree_insert(&fs->avl_dev, &avl_dev);
			if (node != NULL) {
				LogFatal(COMPONENT_FSAL,
					 "Could not re-insert filesystem %s",
					 fs->path);
			}
		}
		return -EEXIST;
	}

	fs->in_dev_avl = true;

	return 0;
}

#define MASK_32 ((uint64_t) UINT32_MAX)

int change_fsid_type(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type)
{
	uint64_t major = 0, minor = 0;
	bool valid = false;

	if (fs->fsid_type == fsid_type)
		return 0;

	switch (fsid_type) {
	case FSID_ONE_UINT64:
		if (fs->fsid_type == FSID_TWO_UINT64) {
			/* Use the same compression we use for NFS v3 fsid */
			major = squash_fsid(&fs->fsid);
			valid = true;
		} else if (fs->fsid_type == FSID_TWO_UINT32) {
			/* Put major in the high order 32 bits and minor
			 * in the low order 32 bits.
			 */
			major = fs->fsid.major << 32 |
				fs->fsid.minor;
			valid = true;
		}
		minor = 0;
		break;

	case FSID_MAJOR_64:
		/* Nothing to do, will ignore fsid.minor in index */
		valid = true;
		major = fs->fsid.major;
		minor = fs->fsid.minor;
		break;

	case FSID_TWO_UINT64:
		if (fs->fsid_type == FSID_MAJOR_64) {
			/* Must re-index since minor was not indexed
			 * previously.
			 */
			major = fs->fsid.major;
			minor = fs->fsid.minor;
			valid = true;
		} else {
			/* Nothing to do, FSID_TWO_UINT32 will just have high
			 * order zero bits while FSID_ONE_UINT64 will have
			 * minor = 0, without changing the actual value.
			 */
			fs->fsid_type = fsid_type;
			return 0;
		}
		break;

	case FSID_DEVICE:
		major = fs->dev.major;
		minor = fs->dev.minor;
		valid = true;

	case FSID_TWO_UINT32:
		if (fs->fsid_type == FSID_TWO_UINT64) {
			/* Shrink each 64 bit quantity to 32 bits by xoring the
			 * two halves.
			 */
			major = (fs->fsid.major & MASK_32) ^
				(fs->fsid.major >> 32);
			minor = (fs->fsid.minor & MASK_32) ^
				(fs->fsid.minor >> 32);
			valid = true;
		} else if (fs->fsid_type == FSID_ONE_UINT64) {
			/* Split 64 bit that is in major into two 32 bit using
			 * the high order 32 bits as major.
			 */
			major = fs->fsid.major >> 32;
			minor = fs->fsid.major & MASK_32;
			valid = true;
		}

		break;

	case FSID_NO_TYPE:
		/* It is not valid to use this routine to remove an fs */
		break;
	}

	if (!valid)
		return -EINVAL;

	return re_index_fs_fsid(fs, fsid_type, major, minor);
}

static bool posix_get_fsid(struct fsal_filesystem *fs)
{
	struct statfs stat_fs;
	struct stat mnt_stat;
#ifdef USE_BLKID
	char *dev_name = NULL, *uuid_str;
	static struct blkid_struct_cache *cache;
	struct blkid_struct_dev *dev;
#endif

	if (statfs(fs->path, &stat_fs) != 0) {
		LogCrit(COMPONENT_FSAL,
			"stat_fs of %s resulted in error %s(%d)",
			fs->path, strerror(errno), errno);
	}

	fs->namelen = stat_fs.f_namelen;

	if (stat(fs->path, &mnt_stat) != 0) {
		LogCrit(COMPONENT_FSAL,
			"stat of %s resulted in error %s(%d)",
			fs->path, strerror(errno), errno);
		return false;
	}

	fs->dev = posix2fsal_devt(mnt_stat.st_dev);

#ifdef USE_BLKID
	dev_name = blkid_devno_to_devname(mnt_stat.st_dev);

	if (dev_name == NULL) {
		LogInfo(COMPONENT_FSAL,
			"blkid_devno_to_devname of %s failed for dev %d.%d",
			fs->path,
			major(mnt_stat.st_dev),
			minor(mnt_stat.st_dev));
		goto no_uuid_no_dev_name;
	}

	if (cache == NULL && blkid_get_cache(&cache, NULL) != 0) {
		LogInfo(COMPONENT_FSAL,
			"blkid_get_cache of %s failed",
			fs->path);
		goto no_uuid;
	}

	dev = (struct blkid_struct_dev *)blkid_get_dev(cache,
						       dev_name,
						       BLKID_DEV_NORMAL);

	if (dev == NULL) {
		LogInfo(COMPONENT_FSAL,
			"blkid_get_dev of %s failed for devname %s",
			fs->path, dev_name);
		goto no_uuid;
	}

	uuid_str = blkid_get_tag_value(cache, "UUID", dev_name);

	if  (uuid_str == NULL) {
		LogInfo(COMPONENT_FSAL,
			"blkid_get_tag_value of %s failed",
			fs->path);
		goto no_uuid;
	}

	if (uuid_parse(uuid_str, (char *) &fs->fsid) == -1) {
		LogInfo(COMPONENT_FSAL,
			"uuid_parse of %s failed for uuid %s",
			fs->path, uuid_str);
		goto no_uuid;
	}

	fs->fsid_type = FSID_TWO_UINT64;
	free(dev_name);

	return true;

 no_uuid:

	free(dev_name);

 no_uuid_no_dev_name:

#endif

	fs->fsid_type = FSID_TWO_UINT32;
	fs->fsid.major = (unsigned) stat_fs.f_fsid.__val[0];
	fs->fsid.minor = (unsigned) stat_fs.f_fsid.__val[1];

	if ((fs->fsid.major == 0) && (fs->fsid.minor == 0)) {
		fs->fsid.major = fs->dev.major;
		fs->fsid.minor = fs->dev.minor;
	}

	return true;
}

static void posix_create_file_system(struct mntent *mnt)
{
	struct fsal_filesystem *fs;
	struct avltree_node *node;

	if (strncasecmp(mnt->mnt_type, "nfs", 3) == 0) {
		LogDebug(COMPONENT_FSAL,
			 "Ignoring %s because type %s",
			 mnt->mnt_dir,
			 mnt->mnt_type);
		return;
	}

	fs = gsh_calloc(1, sizeof(*fs));

	if (fs == NULL) {
		LogFatal(COMPONENT_FSAL,
			 "mem alloc for %s failed",
			 mnt->mnt_dir);
	}

	fs->path = gsh_strdup(mnt->mnt_dir);
	fs->device = gsh_strdup(mnt->mnt_fsname);
	fs->type = gsh_strdup(mnt->mnt_type);

	if (fs->path == NULL) {
		LogFatal(COMPONENT_FSAL,
			 "mem alloc for %s failed",
			 mnt->mnt_dir);
	}

	if (!posix_get_fsid(fs)) {
		free_fs(fs);
		return;
	}

	fs->pathlen = strlen(mnt->mnt_dir);

	node = avltree_insert(&fs->avl_fsid, &avl_fsid);

	if (node != NULL) {
		/* This is a duplicate file system. */
		struct fsal_filesystem *fs1;

		fs1 = avltree_container_of(node,
					   struct fsal_filesystem,
					   avl_fsid);

		LogDebug(COMPONENT_FSAL,
			 "Skipped duplicate %s namelen=%d "
			 "fsid=0x%016"PRIx64".0x%016"PRIx64
			 " %"PRIu64".%"PRIu64,
			 fs->path, (int) fs->namelen,
			 fs->fsid.major, fs->fsid.minor,
			 fs->fsid.major, fs->fsid.minor);

		if (fs1->device[0] != '/' && fs->device[0] == '/') {
			LogDebug(COMPONENT_FSAL,
				 "Switching device for %s from %s to %s type from %s to %s",
				 fs->path, fs1->device, fs->device,
				 fs1->type, fs->type);
			gsh_free(fs1->device);
			fs1->device = fs->device;
			fs->device = NULL;
			gsh_free(fs1->type);
			fs1->type = fs->type;
			fs->type = NULL;
		}

		free_fs(fs);
		return;
	}

	fs->in_fsid_avl = true;

	node = avltree_insert(&fs->avl_dev, &avl_dev);

	if (node != NULL) {
		/* This is a duplicate file system. */
		struct fsal_filesystem *fs1;

		fs1 = avltree_container_of(node,
					   struct fsal_filesystem,
					   avl_dev);

		LogDebug(COMPONENT_FSAL,
			 "Skipped duplicate %s namelen=%d dev=%"
			 PRIu64".%"PRIu64,
			 fs->path, (int) fs->namelen,
			 fs->dev.major, fs->dev.minor);

		if (fs1->device[0] != '/' && fs->device[0] == '/') {
			LogDebug(COMPONENT_FSAL,
				 "Switching device for %s from %s to %s type from %s to %s",
				 fs->path, fs1->device, fs->device,
				 fs1->type, fs->type);
			gsh_free(fs1->device);
			fs1->device = fs->device;
			fs->device = NULL;
			gsh_free(fs1->type);
			fs1->type = fs->type;
			fs->type = NULL;
		}

		remove_fs(fs);
		free_fs(fs);
		return;
	}

	fs->in_dev_avl = true;

	glist_add_tail(&posix_file_systems, &fs->filesystems);
	glist_init(&fs->children);

	LogInfo(COMPONENT_FSAL,
		"Added filesystem %s namelen=%d dev=%"PRIu64".%"PRIu64
		" fsid=0x%016"PRIx64".0x%016"PRIx64" %"PRIu64".%"PRIu64,
		fs->path, (int) fs->namelen,
		fs->dev.major, fs->dev.minor,
		fs->fsid.major, fs->fsid.minor,
		fs->fsid.major, fs->fsid.minor);
}

static void posix_find_parent(struct fsal_filesystem *this)
{
	struct glist_head *glist;
	struct fsal_filesystem *fs;
	int plen = 0;

	/* Check for root fs, it has no parent */
	if (this->pathlen == 1 && this->path[0] == '/')
		return;

	glist_for_each(glist, &posix_file_systems) {
		fs = glist_entry(glist, struct fsal_filesystem, filesystems);

		/* If this path is longer than this path, then it
		 * can't be a parent, or if it's shorter than the
		 * current match;
		 */
		if (fs->pathlen >= this->pathlen ||
		    fs->pathlen < plen)
			continue;

		/* Check for sub-string match */
		if (strncmp(fs->path, this->path, fs->pathlen) != 0)
			continue;

		/* Differentiate between /fs1 and /fs10 for parent of
		 * /fs10/fs2, however, if fs->path is "/", we need to
		 * special case.
		 */
		if (fs->pathlen != 1 &&
		    this->path[fs->pathlen] != '/')
			continue;

		this->parent = fs;
		plen = fs->pathlen;
	}

	if (this->parent == NULL) {
		LogInfo(COMPONENT_FSAL,
			"Unattached file system %s",
			this->path);
		return;
	}

	/* Add to parent's list of children */
	glist_add_tail(&this->parent->children, &this->siblings);
	LogInfo(COMPONENT_FSAL,
		"File system %s is a child of %s",
		this->path, this->parent->path);
}

void show_tree(struct fsal_filesystem *this, int nest)
{
	struct glist_head *glist;
	char blanks[1024];
	memset(blanks, ' ', nest * 2);
	blanks[nest * 2] = '\0';

	LogInfo(COMPONENT_FSAL,
		"%s%s",
		blanks, this->path);

	/* Claim the children now */
	glist_for_each(glist, &this->children) {
		show_tree(glist_entry(glist,
				      struct fsal_filesystem,
				      siblings),
			  nest + 1);
	}
}

int populate_posix_file_systems(void)
{
	FILE *fp;
	struct mntent *mnt;
	int retval = 0;
	struct glist_head *glist;
	struct fsal_filesystem *fs;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	if (!glist_empty(&posix_file_systems))
		goto out;

	avltree_init(&avl_fsid, fsal_fs_cmpf_fsid, 0);
	avltree_init(&avl_dev, fsal_fs_cmpf_dev, 0);

	/* start looking for the mount point */
	fp = setmntent(MOUNTED, "r");

	if (fp == NULL) {
		retval = errno;
		LogCrit(COMPONENT_FSAL, "Error %d in setmntent(%s): %s", retval,
			MOUNTED, strerror(retval));
		goto out;
	}

	while ((mnt = getmntent(fp)) != NULL) {
		if (mnt->mnt_dir == NULL)
			continue;

		posix_create_file_system(mnt);
	}

	endmntent(fp);

	/* build tree of POSIX file systems */
	glist_for_each(glist, &posix_file_systems) {
		posix_find_parent(glist_entry(glist,
					      struct fsal_filesystem,
					      filesystems));
	}

	/* show tree */
	glist_for_each(glist, &posix_file_systems) {
		fs = glist_entry(glist, struct fsal_filesystem, filesystems);
		if (fs->parent == NULL)
			show_tree(fs, 0);
	}

 out:

	PTHREAD_RWLOCK_unlock(&fs_lock);
	return retval;
}

void release_posix_file_systems(void)
{
	struct glist_head *glist, *glistn;
	struct fsal_filesystem *fs;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	glist_for_each_safe(glist, glistn, &posix_file_systems) {
		fs = glist_entry(glist, struct fsal_filesystem, filesystems);
		if (fs->unclaim != NULL) {
			LogWarn(COMPONENT_FSAL,
				"Fileystem %s is still claimed",
				fs->path);
			unclaim_fs(fs);
		}
		LogDebug(COMPONENT_FSAL,
			 "Releasing filesystem %s",
			 fs->path);
		remove_fs(fs);
		free_fs(fs);
	}

	PTHREAD_RWLOCK_unlock(&fs_lock);
}

struct fsal_filesystem *lookup_fsid_locked(struct fsal_fsid__ *fsid,
					   enum fsid_type fsid_type)
{
	struct fsal_filesystem key;
	key.fsid = *fsid;
	key.fsid_type = fsid_type;
	return avltree_inline_fsid_lookup(&key.avl_fsid);
}

struct fsal_filesystem *lookup_dev_locked(struct fsal_dev__ *dev)
{
	struct fsal_filesystem key;
	key.dev = *dev;
	return avltree_inline_dev_lookup(&key.avl_dev);
}

struct fsal_filesystem *lookup_fsid(struct fsal_fsid__ *fsid,
				    enum fsid_type fsid_type)
{
	struct fsal_filesystem *fs;

	PTHREAD_RWLOCK_rdlock(&fs_lock);

	fs = lookup_fsid_locked(fsid, fsid_type);

	PTHREAD_RWLOCK_unlock(&fs_lock);
	return fs;
}

struct fsal_filesystem *lookup_dev(struct fsal_dev__ *dev)
{
	struct fsal_filesystem *fs;

	PTHREAD_RWLOCK_rdlock(&fs_lock);

	fs = lookup_dev_locked(dev);

	PTHREAD_RWLOCK_unlock(&fs_lock);

	return fs;
}

void unclaim_fs(struct fsal_filesystem *this)
{
	/* One call to unclaim resolves all claims to the filesystem */
	if (this->unclaim != NULL) {
		LogDebug(COMPONENT_FSAL,
			 "Have FSAL %s unclaim filesystem %s",
			 this->fsal->name, this->path);
		this->unclaim(this);
	}

	this->fsal = NULL;
	this->unclaim = NULL;
	this->exported = false;
}

int process_claim(const char *path,
		  int pathlen,
		  struct fsal_filesystem *this,
		  struct fsal_module *fsal,
		  struct fsal_export *exp,
		  claim_filesystem_cb claim,
		  unclaim_filesystem_cb unclaim)
{
	struct glist_head *glist;
	struct fsal_filesystem *fs;
	int retval = 0;

	/* Check if the filesystem is already directly exported by some other
	 * FSAL - note we can only get here is this is the root filesystem for
	 * the export, once we start processing nested filesystems, we skip
	 * any that are directly exported.
	 */
	if (this->fsal != NULL && this->fsal != fsal && this->exported) {
		LogCrit(COMPONENT_FSAL,
			"Filesystem %s already exported by FSAL %s for export path %s",
			this->path, this->fsal->name, path);
		return EINVAL;
	}

	/* Check if another FSAL had claimed this file system as a sub-mounted
	 * file system.
	 */
	if (this->fsal != fsal)
		unclaim_fs(this);

	/* Now claim the file system (we may call claim multiple times */
	retval = claim(this, exp);

	if (retval == ENXIO) {
		if (path != NULL) {
			LogCrit(COMPONENT_FSAL,
				"FSAL %s could not to claim root file system %s for export %s",
				fsal->name, this->path, path);
			return EINVAL;
		} else {
			LogInfo(COMPONENT_FSAL,
				"FSAL %s could not to claim file system %s",
				fsal->name, this->path);
			return 0;
		}
	}

	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"FSAL %s failed to claim file system %s error %s",
			fsal->name, this->path, strerror(retval));
		return retval;
	}

	LogDebug(COMPONENT_FSAL,
		 "FSAL %s Claiming %s",
		 fsal->name, this->path);

	/* Complete the claim */
	this->fsal = fsal;
	this->unclaim = unclaim;

	/* If this was the root of the export, indicate this filesystem is
	 * directly exported.
	 */
	if (path != NULL)
		this->exported = true;

	/* If this has no children, done */
	if (glist_empty(&this->children))
		return 0;

	/* Claim the children now */
	glist_for_each(glist, &this->children) {
		fs = glist_entry(glist, struct fsal_filesystem, siblings);
		/* If path is provided, only consider children that are
		 * children of the provided directory. This handles the
		 * case of an export of something other than the root
		 * of a file system.
		 */
		if (path != NULL && (fs->pathlen < pathlen ||
		    (strncmp(fs->path, path, pathlen) != 0)))
			continue;

		/* Test if this fs is directly exported, if so, no more
		 * sub-mounted exports.
		 */
		if (fs->exported)
			continue;

		/* Try to claim this child */
		retval = process_claim(NULL, 0, fs, fsal,
				       exp, claim, unclaim);

		if (retval != 0)
			break;
	}

	return retval;
}

int claim_posix_filesystems(const char *path,
			    struct fsal_module *fsal,
			    struct fsal_export *exp,
			    claim_filesystem_cb claim,
			    unclaim_filesystem_cb unclaim,
			    struct fsal_filesystem **root_fs)
{
	int retval = 0;
	struct fsal_filesystem *fs, *root = NULL;
	struct glist_head *glist;
	struct stat statbuf;
	struct fsal_dev__ dev;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	if (stat(path, &statbuf) != 0) {
		retval = errno;
		LogCrit(COMPONENT_FSAL,
			"Could not stat directory for path %s", path);
		goto out;
	}
	dev = posix2fsal_devt(statbuf.st_dev);

	/* Scan POSIX file systems to find export root fs */
	glist_for_each(glist, &posix_file_systems) {
		fs = glist_entry(glist, struct fsal_filesystem, filesystems);
		if (fs->dev.major == dev.major && fs->dev.minor == dev.minor) {
			root = fs;
			break;
		}
	}

	/* Check if we found a filesystem */
	if (root == NULL) {
		LogCrit(COMPONENT_FSAL,
			"No file system for export path %s",
			path);
		retval = ENOENT;
		goto out;
	}

	/* Claim this file system and it's children */
	retval = process_claim(path, strlen(path), root, fsal,
			       exp, claim, unclaim);

	if (retval == 0) {
		LogInfo(COMPONENT_FSAL,
			"Root fs for export %s is %s",
			path, root->path);
		*root_fs = root;
	}

out:

	PTHREAD_RWLOCK_unlock(&fs_lock);
	return retval;
}

int encode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type)
{
	uint32_t u32;

	if (sizeof_fsid(fsid_type) > max)
		return -1;

	/* Pack fsid into the bytes */
	switch (fsid_type) {
	case FSID_NO_TYPE:
		break;

	case FSID_ONE_UINT64:
	case FSID_MAJOR_64:
		memcpy(buf,
		       &fsid->major,
		       sizeof(fsid->major));
		break;

	case FSID_TWO_UINT64:
		memcpy(buf,
		       fsid,
		       sizeof(*fsid));
		break;

	case FSID_TWO_UINT32:
	case FSID_DEVICE:
		u32 = fsid->major;
		memcpy(buf,
		       &u32,
		       sizeof(u32));
		u32 = fsid->minor;
		memcpy(buf + sizeof(u32),
		       &u32,
		       sizeof(u32));
	}

	return sizeof_fsid(fsid_type);
}

int decode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type)
{
	uint32_t u32;

	if (sizeof_fsid(fsid_type) > max)
		return -1;

	switch (fsid_type) {
	case FSID_NO_TYPE:
		memset(fsid, 0, sizeof(*fsid));
		break;

	case FSID_ONE_UINT64:
	case FSID_MAJOR_64:
		memcpy(&fsid->major,
		       buf,
		       sizeof(fsid->major));
		fsid->minor = 0;
		break;

	case FSID_TWO_UINT64:
		memcpy(fsid,
		       buf,
		       sizeof(*fsid));
		break;

	case FSID_TWO_UINT32:
	case FSID_DEVICE:
		memcpy(&u32,
		       buf,
		       sizeof(u32));
		fsid->major = u32;
		memcpy(&u32,
		       buf + sizeof(u32),
		       sizeof(u32));
		fsid->minor = u32;
		break;
	}

	return sizeof_fsid(fsid_type);
}

/** @} */
