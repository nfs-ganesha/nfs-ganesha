// SPDX-License-Identifier: LGPL-3.0-or-later
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

#include "config.h"

#include "FSAL/fsal_localfs.h"
#ifdef LINUX
#include <sys/sysmacros.h> /* for major(3), minor(3) */
#endif
#include <fcntl.h>
#if __FreeBSD__
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include "gsh_config.h"
#ifdef USE_BLKID
#include <blkid/blkid.h>
#include <uuid/uuid.h>
#endif
#include "FSAL/fsal_commonlib.h"
#include "fsal_convert.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "server_stats_private.h"

#ifdef USE_BTRFSUTIL
#include "btrfsutil.h"
#endif

#ifdef USE_BLKID
static struct blkid_struct_cache *cache;
#endif

int open_dir_by_path_walk(int first_fd, const char *path, struct stat *stat)
{
	char *name, *rest, *p;
	int fd, len, rc, err;

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

static struct glist_head posix_file_systems = {
	&posix_file_systems, &posix_file_systems
};

static bool fs_initialized;
static struct avltree avl_fsid;
static struct avltree avl_dev;

static inline int
fsal_fs_cmpf_fsid(const struct avltree_node *lhs,
		  const struct avltree_node *rhs)
{
	struct fsal_filesystem *lk, *rk;

	lk = avltree_container_of(lhs, struct fsal_filesystem, avl_fsid);
	rk = avltree_container_of(rhs, struct fsal_filesystem, avl_fsid);

	return fsal_fs_compare_fsid(lk->fsid_type, &lk->fsid,
				    rk->fsid_type, &rk->fsid);
}

static inline struct fsal_filesystem *
avltree_inline_fsid_lookup(const struct avltree_node *key)
{
	struct avltree_node *node = avltree_inline_lookup(key, &avl_fsid,
							  fsal_fs_cmpf_fsid);

	if (node != NULL)
		return avltree_container_of(node, struct fsal_filesystem,
					    avl_fsid);
	else
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
	struct avltree_node *node = avltree_inline_lookup(key, &avl_dev,
							  fsal_fs_cmpf_dev);

	if (node != NULL)
		return avltree_container_of(node, struct fsal_filesystem,
					    avl_dev);
	else
		return NULL;
}

static void remove_fs(struct fsal_filesystem *fs)
{
	if (fs->in_fsid_avl)
		avltree_remove(&fs->avl_fsid, &avl_fsid);

	if (fs->in_dev_avl)
		avltree_remove(&fs->avl_dev, &avl_dev);

	glist_del(&fs->siblings);
	glist_del(&fs->filesystems);
}

static void free_fs(struct fsal_filesystem *fs)
{
	gsh_free(fs->path);
	gsh_free(fs->device);
	gsh_free(fs->type);
	gsh_free(fs);
}

int re_index_fs_fsid(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type,
		     struct fsal_fsid__ *fsid)
{
	struct avltree_node *node;
	struct fsal_fsid__ old_fsid = fs->fsid;
	enum fsid_type old_fsid_type = fs->fsid_type;

	LogDebug(COMPONENT_FSAL,
		 "Reindex %s from 0x%016"PRIx64".0x%016"PRIx64
		 " to 0x%016"PRIx64".0x%016"PRIx64,
		 fs->path,
		 fs->fsid.major, fs->fsid.minor,
		 fsid->major, fsid->minor);

	/* It is not valid to use this routine to
	 * remove fs from index.
	 */
	if (fsid_type == FSID_NO_TYPE)
		return -EINVAL;

	if (fs->in_fsid_avl)
		avltree_remove(&fs->avl_fsid, &avl_fsid);

	fs->fsid.major = fsid->major;
	fs->fsid.minor = fsid->minor;
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
	struct fsal_fsid__ fsid = {0};
	bool valid = false;

	if (fs->fsid_type == fsid_type)
		return 0;

	switch (fsid_type) {
	case FSID_ONE_UINT64:
		if (fs->fsid_type == FSID_TWO_UINT64) {
			/* Use the same compression we use for NFS v3 fsid */
			fsid.major = squash_fsid(&fs->fsid);
			valid = true;
		} else if (fs->fsid_type == FSID_TWO_UINT32) {
			/* Put major in the high order 32 bits and minor
			 * in the low order 32 bits.
			 */
			fsid.major = fs->fsid.major << 32 |
				     fs->fsid.minor;
			valid = true;
		}
		fsid.minor = 0;
		break;

	case FSID_MAJOR_64:
		/* Nothing to do, will ignore fsid.minor in index */
		valid = true;
		fsid.major = fs->fsid.major;
		fsid.minor = fs->fsid.minor;
		break;

	case FSID_TWO_UINT64:
		if (fs->fsid_type == FSID_MAJOR_64) {
			/* Must re-index since minor was not indexed
			 * previously.
			 */
			fsid.major = fs->fsid.major;
			fsid.minor = fs->fsid.minor;
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
		fsid.major = fs->dev.major;
		fsid.minor = fs->dev.minor;
		valid = true;
		/* fallthrough */

	case FSID_TWO_UINT32:
		if (fs->fsid_type == FSID_TWO_UINT64) {
			/* Shrink each 64 bit quantity to 32 bits by xoring the
			 * two halves.
			 */
			fsid.major = (fs->fsid.major & MASK_32) ^
				     (fs->fsid.major >> 32);
			fsid.minor = (fs->fsid.minor & MASK_32) ^
				     (fs->fsid.minor >> 32);
			valid = true;
		} else if (fs->fsid_type == FSID_ONE_UINT64) {
			/* Split 64 bit that is in major into two 32 bit using
			 * the high order 32 bits as major.
			 */
			fsid.major = fs->fsid.major >> 32;
			fsid.minor = fs->fsid.major & MASK_32;
			valid = true;
		}

		break;

	case FSID_NO_TYPE:
		/* It is not valid to use this routine to remove an fs */
		break;
	}

	if (!valid)
		return -EINVAL;

	return re_index_fs_fsid(fs, fsid_type, &fsid);
}

static bool posix_get_fsid(struct fsal_filesystem *fs, struct stat *mnt_stat)
{
	struct statfs stat_fs;
#ifdef USE_BLKID
	char *dev_name;
	char *uuid_str;
#endif

	LogFullDebug(COMPONENT_FSAL, "statfs of %s pathlen %d", fs->path,
		     fs->pathlen);

	if (statfs(fs->path, &stat_fs) != 0)
		LogCrit(COMPONENT_FSAL,
			"stat_fs of %s resulted in error %s(%d)",
			fs->path, strerror(errno), errno);

#if __FreeBSD__
	fs->namelen = stat_fs.f_namemax;
#else
	fs->namelen = stat_fs.f_namelen;
#endif

	fs->dev = posix2fsal_devt(mnt_stat->st_dev);

	if (nfs_param.core_param.fsid_device) {
		fs->fsid_type = FSID_DEVICE;
		fs->fsid.major = fs->dev.major;
		fs->fsid.minor = fs->dev.minor;
		return true;
	}

#ifdef USE_BLKID
	if (cache == NULL)
		goto out;

	dev_name = blkid_devno_to_devname(mnt_stat->st_dev);

	if (dev_name == NULL) {
		LogDebug(COMPONENT_FSAL,
			 "blkid_devno_to_devname of %s failed for dev %d.%d",
			 fs->path, major(mnt_stat->st_dev),
			 minor(mnt_stat->st_dev));
		goto out;
	}

	if (blkid_get_dev(cache, dev_name, BLKID_DEV_NORMAL) == NULL) {
		LogInfo(COMPONENT_FSAL,
			"blkid_get_dev of %s failed for devname %s",
			fs->path, dev_name);
		free(dev_name);
		goto out;
	}

	uuid_str = blkid_get_tag_value(cache, "UUID", dev_name);
	free(dev_name);

	if  (uuid_str == NULL) {
		LogInfo(COMPONENT_FSAL, "blkid_get_tag_value of %s failed",
			fs->path);
		goto out;
	}

	if (uuid_parse(uuid_str, (char *) &fs->fsid) == -1) {
		LogInfo(COMPONENT_FSAL, "uuid_parse of %s failed for uuid %s",
			fs->path, uuid_str);
		free(uuid_str);
		goto out;
	}

	free(uuid_str);
	fs->fsid_type = FSID_TWO_UINT64;

	return true;

out:
#endif
	fs->fsid_type = FSID_TWO_UINT32;
#if __FreeBSD__
	fs->fsid.major = (unsigned int) stat_fs.f_fsid.val[0];
	fs->fsid.minor = (unsigned int) stat_fs.f_fsid.val[1];
#else
	fs->fsid.major = (unsigned int) stat_fs.f_fsid.__val[0];
	fs->fsid.minor = (unsigned int) stat_fs.f_fsid.__val[1];
#endif
	if ((fs->fsid.major == 0) && (fs->fsid.minor == 0)) {
		fs->fsid.major = fs->dev.major;
		fs->fsid.minor = fs->dev.minor;
	}

	return true;
}

static void posix_create_file_system(struct mntent *mnt, struct stat *mnt_stat);

static void posix_create_fs_btrfs_subvols(struct fsal_filesystem *fs)
{
#ifdef USE_BTRFSUTIL
	struct mntent mnt;
	struct stat st;
	struct btrfs_util_subvolume_iterator *iter;
	enum btrfs_util_error err = BTRFS_UTIL_OK;
	uint64_t id, idsv;
	size_t lenp = strlen(fs->path), lens;
	char *path;

	LogFullDebug(COMPONENT_FSAL,
		     "Attempting to add subvols for btfs filesystem %s",
		     fs->path);

	/* Setup common fields for fake mntent, many are NULL or 0.
	 * type is set to fake btrfs_sv otherwise we would recurse into here.
	 * fsname (device) is set to the same as the parent (fs->device only
	 * gets used for quota manipulation.
	 */
	memset(&mnt, 0, sizeof(mnt));
	mnt.mnt_fsname = fs->device;
	mnt.mnt_type = "btrfs_sv";

	err = btrfs_util_subvolume_id(fs->path, &id);

	if (err != 0) {
		LogCrit(COMPONENT_FSAL,
			"btrfs_util_subvolume_id err %s",
			btrfs_util_strerror(err));
		return;
	}

	err = btrfs_util_create_subvolume_iterator(fs->path, id, 0, &iter);

	if (err != 0) {
		LogCrit(COMPONENT_FSAL,
			"btrfs_util_create_subvolume_iterator err %s",
			btrfs_util_strerror(err));
		return;
	}

	err = btrfs_util_sync_fd(btrfs_util_subvolume_iterator_fd(iter));


	if (err != 0) {
		LogCrit(COMPONENT_FSAL,
			"btrfs_util_sync_fd err %s",
			btrfs_util_strerror(err));
		goto out;
	}

	while (err == BTRFS_UTIL_OK) {
		err = btrfs_util_subvolume_iterator_next(iter, &path, &idsv);

		if (err == BTRFS_UTIL_OK) {
			/* Construct fully qualified path */
			lens = strlen(path);
			mnt.mnt_dir = gsh_malloc(lenp + lens + 2);
			memcpy(mnt.mnt_dir, fs->path, lenp);
			mnt.mnt_dir[lenp] = '/';
			memcpy(mnt.mnt_dir + lenp + 1, path, lens + 1);

			if (stat(mnt.mnt_dir, &st) >= 0) {
				LogInfo(COMPONENT_FSAL,
					"Adding btrfs subvol %s",
					mnt.mnt_dir);

				posix_create_file_system(&mnt, &st);
			} else {
				int err = errno;

				LogCrit(COMPONENT_FSAL,
					"Could not stat btrfs subvol %s err = %s",
					mnt.mnt_dir, strerror(err));
			}

			/* Free the path from the iterator and the fully
			 * qualified path.
			 */
			free(path);
			gsh_free(mnt.mnt_dir);
		} else if (err != BTRFS_UTIL_ERROR_STOP_ITERATION) {
			LogCrit(COMPONENT_FSAL,
				"btrfs_util_subvolume_iterator_next err %s",
				btrfs_util_strerror(err));
		}
	}

out:

	btrfs_util_destroy_subvolume_iterator(iter);
#else
	LogWarn(COMPONENT_FSAL,
		"btfs filesystem %s may have unsupported subvols",
		fs->path);
#endif
}

static void posix_create_file_system(struct mntent *mnt, struct stat *mnt_stat)
{
	struct fsal_filesystem *fs;
	struct avltree_node *node;

	fs = gsh_calloc(1, sizeof(*fs));

	fs->path = gsh_strdup(mnt->mnt_dir);
	fs->device = gsh_strdup(mnt->mnt_fsname);
	fs->type = gsh_strdup(mnt->mnt_type);
	glist_init(&fs->exports);

	if (!posix_get_fsid(fs, mnt_stat)) {
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
			 "Skipped duplicate %s namelen=%d fsid=0x%016"PRIx64
			 ".0x%016"PRIx64" %"PRIu64".%"PRIu64" type=%s",
			 fs->path, (int) fs->namelen,
			 fs->fsid.major, fs->fsid.minor,
			 fs->fsid.major, fs->fsid.minor, fs->type);

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
			 PRIu64".%"PRIu64" type=%s",
			 fs->path, (int) fs->namelen,
			 fs->dev.major, fs->dev.minor, fs->type);

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
		"Added filesystem %p %s namelen=%d dev=%"PRIu64".%"PRIu64
		" fsid=0x%016"PRIx64".0x%016"PRIx64" %"PRIu64".%"PRIu64
		" type=%s",
		fs, fs->path, (int) fs->namelen,
		fs->dev.major, fs->dev.minor,
		fs->fsid.major, fs->fsid.minor,
		fs->fsid.major, fs->fsid.minor, fs->type);

	if (strcasecmp(mnt->mnt_type, "btrfs") == 0)
		posix_create_fs_btrfs_subvols(fs);
}

static void posix_find_parent(struct fsal_filesystem *this)
{
	struct glist_head *glist;
	struct fsal_filesystem *fs;
	int plen = 0;

	/* Check if it already has parent */
	if (this->parent != NULL)
		return;

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

static bool path_is_subset(const char *path, const char *of)
{
	int len_path = strlen(path);
	int len_of = strlen(of);
	int len_cmp = MIN(len_path, len_of);

	/* We can handle special case of "/" trivially, so check for it */
	if ((len_path == 1 && path[0] == '/') ||
	    (len_of == 1 && of[0] == '/')) {
		/* One of the paths is "/" so subset MUST be true */
		return true;
	}


	if (len_path != len_of &&
	    ((len_cmp != len_path && path[len_cmp] != '/') ||
	     (len_cmp != len_of && of[len_cmp] != '/'))) {
		/* The character in the longer path just past the length of the
		 * shorter path must be '/' in order to be a valid subset path.
		 */
		return false;
	}

	/* Compare the two strings to the length of the shorter one */
	if (strncmp(path, of, len_cmp) != 0) {
		/* Since the shortest doesn't match to the start of the longer
		 * neither path is a subset of the other.
		 */
		return false;
	}

	return true;
}

int populate_posix_file_systems(const char *path)
{
	FILE *fp;
	struct mntent *mnt;
	struct stat st;
	int retval = 0;
	struct glist_head *glist, *glistn;
	struct fsal_filesystem *fs, *fsn;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	if (!fs_initialized) {
		LogDebug(COMPONENT_FSAL, "Initializing posix file systems");
		avltree_init(&avl_fsid, fsal_fs_cmpf_fsid, 0);
		avltree_init(&avl_dev, fsal_fs_cmpf_dev, 0);
		fs_initialized = true;
	}

	/* We are about to rescan mtab, remove unclaimed file systems.
	 * release_posix_file_system will actually do the depth first
	 * search for unclaimed file systems.
	 */

	glist = posix_file_systems.next;

	/* Scan the list for the first top level file system that is not
	 * claimed.
	 *
	 * NOTE: the following two loops are similar to glist_for_each_safe but
	 *       instead of just taking the next list entry, we take the next
	 *       list entry that is a top level file system. This way even if
	 *       the glist->next was a descendant file system that was going to
	 *       be released, before we release a file system or it's
	 *       descendants, we look for the next top level file system, since
	 *       it's a top level file system it WILL NOT be released when we
	 *       release fs or it's descendants.
	 */
	while (glist != &posix_file_systems) {
		fs = glist_entry(glist,
				 struct fsal_filesystem,
				 filesystems);

		if (fs->parent == NULL) {
			/* Top level file system done */
			break;
		}

		glist = glist->next;
	}

	/* Now, while we still have a top level file system to process
	 */
	while (glist != &posix_file_systems) {
		/* First, find the NEXT top level file system. */
		glistn = glist->next;

		while (glistn != &posix_file_systems) {
			fsn = glist_entry(glistn,
					  struct fsal_filesystem,
					  filesystems);
			if (fsn->parent == NULL) {
				/* Top level file system done */
				break;
			}

			glistn = glistn->next;
		}

		/* Now glistn/fsn is either the next top level file
		 * system or glistn is &posix_file_systems.
		 *
		 * fs is the file system to try releasing on, try to
		 * release this file system or it's descendants.
		 */

		(void) release_posix_file_system(fs, UNCLAIM_SKIP);

		/* Now ready to start processing the next top level
		 * file system that is not claimed.
		 */
		glist = glistn;
		fs = fsn;
	}

	/* start looking for the mount point */
	fp = setmntent(MOUNTED, "r");

	if (fp == NULL) {
		retval = errno;
		LogCrit(COMPONENT_FSAL, "Error %d in setmntent(%s): %s", retval,
			MOUNTED, strerror(retval));
		goto out;
	}

#ifdef USE_BLKID
	if (blkid_get_cache(&cache, NULL) != 0)
		LogInfo(COMPONENT_FSAL, "blkid_get_cache failed");
#endif

	while ((mnt = getmntent(fp)) != NULL) {
		if (mnt->mnt_dir == NULL)
			continue;

		if (!path_is_subset(path, mnt->mnt_dir)) {
			LogDebug(COMPONENT_FSAL,
				 "Ignoring %s because it is not a subset or superset of path %s",
				 mnt->mnt_dir, path);
			continue;
		}

		/* stat() on NFS mount points is prone to get stuck in
		 * kernel due to unavailable NFS servers. Since we don't
		 * support them anyway, check this early and avoid
		 * hangs!
		 *
		 * Also skip some other filesystem types that we would never
		 * export.
		 */
		if (strncasecmp(mnt->mnt_type, "nfs", 3) == 0 ||
		    strcasecmp(mnt->mnt_type, "autofs") == 0 ||
		    strcasecmp(mnt->mnt_type, "sysfs") == 0 ||
		    strcasecmp(mnt->mnt_type, "proc") == 0 ||
		    strcasecmp(mnt->mnt_type, "devtmpfs") == 0 ||
		    strcasecmp(mnt->mnt_type, "securityfs") == 0 ||
		    strcasecmp(mnt->mnt_type, "cgroup") == 0 ||
		    strcasecmp(mnt->mnt_type, "selinuxfs") == 0 ||
		    strcasecmp(mnt->mnt_type, "debugfs") == 0 ||
		    strcasecmp(mnt->mnt_type, "hugetlbfs") == 0 ||
		    strcasecmp(mnt->mnt_type, "mqueue") == 0 ||
		    strcasecmp(mnt->mnt_type, "pstore") == 0 ||
		    strcasecmp(mnt->mnt_type, "devpts") == 0 ||
		    strcasecmp(mnt->mnt_type, "configfs") == 0 ||
		    strcasecmp(mnt->mnt_type, "binfmt_misc") == 0 ||
		    strcasecmp(mnt->mnt_type, "rpc_pipefs") == 0 ||
		    strcasecmp(mnt->mnt_type, "vboxsf") == 0) {
			LogDebug(COMPONENT_FSAL,
				 "Ignoring %s because type %s",
				 mnt->mnt_dir,
				 mnt->mnt_type);
			continue;
		}

		if (stat(mnt->mnt_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}

		posix_create_file_system(mnt, &st);
	}

#ifdef USE_BLKID
	if (cache) {
		blkid_put_cache(cache);
		cache = NULL;
	}
#endif

	endmntent(fp);

	/* build tree of POSIX file systems */
	glist_for_each(glist, &posix_file_systems)
		posix_find_parent(glist_entry(glist, struct fsal_filesystem,
					      filesystems));

 out:
	PTHREAD_RWLOCK_unlock(&fs_lock);
	return retval;
}

int resolve_posix_filesystem(const char *path,
			     struct fsal_module *fsal,
			     struct fsal_export *exp,
			     claim_filesystem_cb claimfs,
			     unclaim_filesystem_cb unclaim,
			     struct fsal_filesystem **root_fs)
{
	int retval = EAGAIN;
	struct stat statbuf;

	while (retval == EAGAIN) {
		/* Need to retry stat on path until we don't get EAGAIN in case
		 * autofs needed to mount the file system.
		 */
		retval = stat(path, &statbuf);
		if (retval != 0) {
			retval = errno;
			LogDebug(COMPONENT_FSAL,
				 "stat returned %s (%d) while resolving export path %s %s",
				 strerror(retval), retval, path,
				 retval == EAGAIN ? "(may retry)" : "(failed)");
		}
	}

	if (retval != 0) {
		/* Since we failed a stat on the path, we might as well bail
		 * now...
		 */
		LogCrit(COMPONENT_FSAL,
			"stat returned %s (%d) while resolving export path %s",
			strerror(retval), retval, path);
		return retval;
	}

	retval = populate_posix_file_systems(path);

	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"populate_posix_file_systems returned %s (%d)",
			strerror(retval), retval);
		return retval;
	}

	retval = claim_posix_filesystems(path, fsal, exp,
					 claimfs, unclaim, root_fs, &statbuf);

	return retval;
}

/**
 * @brief release a POSIX file system and all it's descendants
 *
 * @param[in] fs             the file system to release
 * @param[in] release_claims what to do about claimed file systems
 *
 * @returns true if a descendant was not released because it was a claimed
 *          file system or a descendant underneath was a claimed file system.
 *
 */
bool release_posix_file_system(struct fsal_filesystem *fs,
			       enum release_claims release_claims)
{
	bool claimed = false; /* Assume no claimed children. */
	struct glist_head *glist, *glistn;

	LogFilesystem("TRY RELEASE", "", fs);

	/* Note: Check this file system AFTER we check the descendants, we will
	 *       thus release any descendants that are not claimed.
	 */

	glist_for_each_safe(glist, glistn, &fs->children) {
		struct fsal_filesystem *child_fs;

		child_fs = glist_entry(glist, struct fsal_filesystem, siblings);

		/* If a child or child underneath was not released because it
		 * was claimed, propagate that up.
		 */
		claimed |= release_posix_file_system(child_fs, release_claims);
	}

	if (fs->unclaim != NULL) {
		if (release_claims == UNCLAIM_WARN)
			LogWarn(COMPONENT_FSAL,
				"Filesystem %s is still claimed",
				fs->path);
		else
			LogDebug(COMPONENT_FSAL,
				 "Filesystem %s is still claimed",
				 fs->path);
		return true;
	}

	if (claimed) {
		if (release_claims == UNCLAIM_WARN)
			LogWarn(COMPONENT_FSAL,
				"Filesystem %s had at least one child still claimed",
				fs->path);
		else
			LogDebug(COMPONENT_FSAL,
				 "Filesystem %s had at least one child still claimed",
				 fs->path);
		return true;
	}

	LogFilesystem("REMOVE", "", fs);

	LogInfo(COMPONENT_FSAL,
		"Removed filesystem %p %s namelen=%d dev=%"PRIu64".%"PRIu64
		" fsid=0x%016"PRIx64".0x%016"PRIx64" %"PRIu64".%"PRIu64
		" type=%s",
		fs, fs->path, (int) fs->namelen, fs->dev.major, fs->dev.minor,
		fs->fsid.major, fs->fsid.minor, fs->fsid.major, fs->fsid.minor,
		fs->type);
	remove_fs(fs);
	free_fs(fs);

	return false;
}

void release_posix_file_systems(void)
{
	struct fsal_filesystem *fs;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	while ((fs = glist_first_entry(&posix_file_systems,
				       struct fsal_filesystem, filesystems))) {
		(void) release_posix_file_system(fs, UNCLAIM_WARN);
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

const char *str_claim_type(enum claim_type claim_type)
{
	switch (claim_type) {
	case CLAIM_ALL:
		return "CLAIM_ALL";
	case CLAIM_ROOT:
		return "CLAIM_ROOT";
	case CLAIM_SUBTREE:
		return "CLAIM_SUBTREE";
	case CLAIM_CHILD:
		return "CLAIM_CHILD";
	case CLAIM_TEMP:
		return "CLAIM_TEMP";
	case CLAIM_NUM:
		return "CLAIM_NUM";
	}

	return "unknown claim type";
}

void unclaim_child_map(struct fsal_filesystem_export_map *this)
{
	LogFilesystem("UNCLAIM ", "(BEFORE)", this->fs);

	/* Unclaim any child maps */
	while (!glist_empty(&this->child_maps)) {
		struct fsal_filesystem_export_map *map;

		map = glist_first_entry(&this->child_maps,
					struct fsal_filesystem_export_map,
					on_parent);

		unclaim_child_map(map);
	}

	LogFilesystem("Unclaim Child Map for Claim Type ",
		      str_claim_type(this->claim_type),
		      this->fs);

	/* Remove this file system from mapping */
	glist_del(&this->on_filesystems);
	glist_del(&this->on_exports);
	glist_del(&this->on_parent);

	/* Reduce the claims on the filesystem */
	--this->fs->claims[this->claim_type];
	--this->fs->claims[CLAIM_ALL];

	/* Don't actually unclaim from the FSAL if there are claims remaining or
	 * a temporary claim.
	 */
	if (this->fs->claims[CLAIM_ALL] == 0 &&
	    this->fs->claims[CLAIM_TEMP] == 0) {
		/* This was the last claim on the filesystem */
		assert(this->fs->claims[CLAIM_ROOT] == 0);
		assert(this->fs->claims[CLAIM_SUBTREE] == 0);
		assert(this->fs->claims[CLAIM_CHILD] == 0);

		if (this->fs->unclaim != NULL) {
			LogDebug(COMPONENT_FSAL,
				 "Have FSAL %s unclaim filesystem %s",
				 this->fs->fsal->name, this->fs->path);
			this->fs->unclaim(this->fs);
		}

		this->fs->fsal = NULL;
		this->fs->unclaim = NULL;
		this->fs->private_data = NULL;
	}

	LogFilesystem("UNCLAIM ", "(AFTER)", this->fs);

	/* And free this map */
	gsh_free(this);
}

void unclaim_all_filesystem_maps(struct fsal_filesystem *this)
{
	while (!glist_empty(&this->exports)) {
		struct fsal_filesystem_export_map *map;

		map = glist_first_entry(&this->exports,
					struct fsal_filesystem_export_map,
					on_exports);

		unclaim_child_map(map);
	}
}

void unclaim_all_export_maps(struct fsal_export *exp)
{
	PTHREAD_RWLOCK_wrlock(&fs_lock);

	while (!glist_empty(&exp->filesystems)) {
		struct fsal_filesystem_export_map *map;

		map = glist_first_entry(&exp->filesystems,
					struct fsal_filesystem_export_map,
					on_filesystems);

		unclaim_child_map(map);
	}

	if (exp->root_fs != NULL) {
		LogFilesystem("ROOT FS", "", exp->root_fs);

		/* Now that we've unclaimed all fsal_fileststem objects, see if
		 * we can release any. Once we're done with this, any unclaimed
		 * file systems should be able to be unmounted by the sysadmin
		 * (though note that if they are sub-mounted in another VFS
		 * export, they could become claimed by navigation into them).
		 * If there are any nested exports, the file systems they export
		 * will still be claimed. The nested exports will at least still
		 * be mountable via NFS v3.
		 */
		(void) release_posix_file_system(exp->root_fs, UNCLAIM_SKIP);
	}

	PTHREAD_RWLOCK_unlock(&fs_lock);
}

#define HAS_CHILD_CLAIMS(this) (this->claims[CLAIM_CHILD])
#define HAS_NON_CHILD_CLAIMS(this) \
	(this->claims[CLAIM_ROOT] != 0 || this->claims[CLAIM_SUBTREE] != 0)

static inline bool is_path_child(const char *possible_path,
				 int possible_pathlen,
				 const char *compare_path,
				 int compare_pathlen) {
	/* For a possible_path to represent a child of compare_path:
	 * possible_pathlen MUST be longer (otherwise it can't be a child
	 * the portion of possible_path up to compare_pathlen must be the same
	 * AND the portion of possible_path that compares MUST end with a '/'
	 *
	 * Thus /short is NOT a child of /short/longer
	 * and /some/path is NOT a child of /some/other/path
	 * and /some/path2 is NOT a child of /some/path
	 *
	 * Since the '/' check is simple, check it before comparing strings
	 */
	return possible_pathlen > compare_pathlen &&
	       possible_path[compare_pathlen] == '/' &&
	       strncmp(possible_path, compare_path, compare_pathlen) == 0;
}

static inline bool is_filesystem_child(struct fsal_filesystem *fs,
				       const char *path, int pathlen)
{
	return is_path_child(fs->path, fs->pathlen, path, pathlen);
}

/**
 * @brief Validate that fs is exported by exp
 *
 * @note Must hold fs_lock
 */
bool is_filesystem_exported(struct fsal_filesystem *fs,
			    struct fsal_export *exp)
{
	struct glist_head *glist, *glistn;
	struct fsal_filesystem_export_map *map;

	LogFullDebug(COMPONENT_FSAL,
		     "Checking if FileSystem %s belongs to export %"PRIu16,
		     fs->path, exp->export_id);

	glist_for_each_safe(glist, glistn, &fs->exports) {
		map = glist_entry(glist,
				  struct fsal_filesystem_export_map,
				  on_exports);

		if (map->exp == exp) {
			/* We found a match! */
			return true;
		}
	}

	LogInfo(COMPONENT_FSAL,
		 "FileSystem %s does not belong to export %"PRIu16,
		 fs->path, exp->export_id);

	return false;
}

static int process_claim(const char *path,
			 int pathlen,
			 struct fsal_filesystem_export_map *parent_map,
			 struct fsal_filesystem *this,
			 struct fsal_module *fsal,
			 struct fsal_export *exp,
			 claim_filesystem_cb claimfs,
			 unclaim_filesystem_cb unclaim)
{
	struct glist_head *export_glist, *child_glist;
	struct fsal_filesystem_export_map *map;
	int retval = 0;
	bool already_claimed = this->fsal == fsal;
	enum claim_type claim_type;
	void *private_data;

	LogFilesystem("PROCESS CLAIM", "", this);

	if (path == NULL)
		claim_type = CLAIM_CHILD;
	else if (strcmp(path, this->path) == 0)
		claim_type = CLAIM_ROOT;
	else
		claim_type = CLAIM_SUBTREE;

	/* Either this filesystem must be claimed by a FSAL OR it must not have
	 * any claims at all.
	 */
	assert(this->fsal != NULL || this->claims[CLAIM_ALL] == 0);

	/* Check if the filesystem is already directly exported by some other
	 * FSAL - note we can only get here is this is the root filesystem for
	 * the export, once we start processing nested filesystems, we skip
	 * any that are directly exported.
	 */
	if (this->fsal != fsal && HAS_NON_CHILD_CLAIMS(this)) {
		LogCrit(COMPONENT_FSAL,
			"Filesystem %s already exported by FSAL %s for export path %s",
			this->path, this->fsal->name, path);
		return EINVAL;
	}

	/* Now claim the file system (we may call claim multiple times) */
	retval = claimfs(this, exp, &private_data);

	if (retval == ENXIO) {
		if (claim_type != CLAIM_CHILD) {
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

	/* Take a temporary claim to prevent call to unclaim */
	this->claims[CLAIM_TEMP]++;

	if (already_claimed) {
		LogDebug(COMPONENT_FSAL,
			 "FSAL %s Repeat Claiming %p %s",
			 fsal->name, this, this->path);
	} else {
		LogInfo(COMPONENT_FSAL,
			"FSAL %s Claiming %p %s",
			fsal->name, this, this->path);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Attempting claim type %s by FSAL %s on filesystem %s",
		     str_claim_type(claim_type), fsal->name, this->path);

	/* Check for another FSAL holding child claims on this filesystem or
	 * any child claims held by this FSAL when the new claim is a root claim
	 */
	if (HAS_CHILD_CLAIMS(this) &&
	    (this->fsal != fsal || claim_type == CLAIM_ROOT)) {
		LogFullDebug(COMPONENT_FSAL,
			     "FSAL %s trying to claim filesystem %s from FSAL %s",
			     fsal->name, this->path, this->fsal->name);

		if (claim_type == CLAIM_SUBTREE) {
			/* Warn about situation where the claim directory
			 * structure would suggest the other FSAL's child
			 * claim should co-exist with a subtree claim.
			 *
			 * NOTE: this warning could be spurious depending on
			 *       order of exports. For example, assume two
			 *       filesystems:
			 *           /export/fs1
			 *           /export/fs1/some/path/fs2
			 *       And the following exports:
			 *       FSAL_XFS /export/fs1
			 *       FSAL_VFS /export/fs1/some/path/fs2/another
			 *       FSAL_VFS /export/fs1/some/path/fs2
			 *
			 *       Initially /export/fs1/some/path/fs2 will be
			 *       claimed for FSAL_XFS as part of the /export/fs1
			 *       export, but then the FSAL_VFS export
			 *       /export/fs1/some/path/fs2/another will take it
			 *       away, but at that point,
			 *       /export/fs1/some/path/fs2 would still appear to
			 *       be part of the XFS export, so the LogWarn fires
			 *       but in fact, the final VFS export of
			 *       /export/fs1/some/path/fs2 makes it all right.
			 *       If this bothers someone, put the exports in
			 *       order of longest/deepest path first...
			 */
			LogWarn(COMPONENT_FSAL,
				"FSAL %s export path %s includes filesystem %s which had a subtree export from FSAL %s - unclaiming filesystem from FSAL %s",
				fsal->name, path, this->path,
				this->fsal->name, this->fsal->name);
		}

		/* Walk the child maps and unclaim them all */
		unclaim_all_filesystem_maps(this);

		assert(!HAS_CHILD_CLAIMS(this));
	}

	/* If this is a root claim and there are any child claims (which must
	 * belong to this FSAL since we already disposed of child claims by
	 * another FSAL above), unclaim all of them.
	 */
	if (claim_type == CLAIM_ROOT && HAS_CHILD_CLAIMS(this)) {
		/* Walk the child maps and unclaim them all */
		unclaim_all_filesystem_maps(this);
	}

	/* At this point, the claims that remain on this filesystem belong
	 * to this FSAL, the following claims are allowed based on this claim:
	 *
	 * root claim: subtree and root claims are allowed
	 * subtree claim: root, subtree, and child claims are allowed
	 * child claim: subtree and child claims are allowed
	 *
	 * Note that two (or more) root claims ARE allowed as long as the
	 * exports are differentiated. Multiple subtree claims ARE allowed
	 * whether they are distinct or are the same directory (in which case
	 * the exports MUST be differentiated). Multiple parent claims may
	 * result in multiple child claims however the parent claims MUST be
	 * the same directory (root or subtree) otherwise the parent claim with
	 * the shorter path would have the parent claim with the longer path as
	 * a sub-export, and only the sub-export would export the child
	 * filesystems.
	 *
	 * Because of this, we need to check for overlapping subtree claims
	 * below and either unclaim the child claims from the shorter path
	 * or not make child claims when there are child claims from a longer
	 * path already in existence.
	 */

	/* Complete the claim */
	this->fsal = fsal;
	this->unclaim = unclaim;
	this->private_data = private_data;

	map = gsh_calloc(1, sizeof(*map));
	map->exp = exp;
	map->fs = this;
	map->claim_type = claim_type;
	glist_init(&map->child_maps);

	if (claim_type == CLAIM_ROOT)
		exp->root_fs = this;

	/* If this has no children, done */
	if (glist_empty(&this->children))
		goto account;

	/* Now we may need to clean out some child claims on this filesystems
	 * children. This happens when this export claim is a subtree with a
	 * longer path that an already existing export claim which could be a
	 * root or subtree claim.
	 */
	if (claim_type == CLAIM_SUBTREE) {
		/* Look for other claims on this filesystem where this claim is
		 * a subtree of the other claim. On such claims, look for any
		 * child filesystems that are mapped by the other claim that
		 * are children of our subtree. Such child claims must be
		 * removed (they will become child claims of this claim).
		 */
		glist_for_each(export_glist, &this->exports) {
			struct glist_head *glist, *glistn;
			struct fsal_filesystem_export_map *other_map;
			struct gsh_refstr *map_fullpath;
			size_t map_pathlen;
			bool child;

			other_map = glist_entry(
					export_glist,
					struct fsal_filesystem_export_map,
					on_exports);

			if (glist_empty(&other_map->child_maps)) {
				/* This map has no child claims under it, so
				 * skip it.
				 */
				continue;
			}

			map_fullpath = gsh_refstr_get(rcu_dereference(
				other_map->exp->owning_export->fullpath));

			map_pathlen = strlen(map_fullpath->gr_val);

			/* We're interested in when this claim is a subtree of
			 * an existing claim, in which case we will take any
			 * child claims that are without our subtree.
			 *
			 * NOTE the order of paths passed to is_path_child is
			 *      reversed from other uses because we are checking
			 *      if this claim is a subtree of the map claim.
			 */
			child = is_path_child(path, pathlen,
					      map_fullpath->gr_val,
					      map_pathlen);

			/* And we're done with the refstr. */
			gsh_refstr_put(map_fullpath);

			if (!child) {
				/* Since the map claim is not a subtree of this
				 * claim, we don't care about it's children. Its
				 * mapping some other portion of this filesystem
				 */
				continue;
			}

			glist_for_each_safe(glist, glistn,
					    &other_map->child_maps) {
				struct fsal_filesystem_export_map *child_map;

				child_map = glist_entry(
					glist,
					struct fsal_filesystem_export_map,
					on_parent);

				if (is_path_child(child_map->fs->path,
						  child_map->fs->pathlen,
						  path, pathlen)) {
					/* filesystem is a child of our
					 * subtree, now we need to remove this
					 * map's child_maps on the filesystem.
					 */
					unclaim_child_map(child_map);
				}
			}
		}
	}

	/* Claim the children now */
	glist_for_each(child_glist, &this->children) {
		struct fsal_filesystem *child_fs;

		child_fs = glist_entry(
			child_glist, struct fsal_filesystem, siblings);

		/* Any child filesystem can not have child claims from another
		 * FSAL since that FSAL would have to have a claim on this
		 * filesystem which would conflict with our claim.
		 *
		 * Child claims from our FSAL are fine.
		 */
		assert(!HAS_NON_CHILD_CLAIMS(child_fs) ||
		       child_fs->fsal == fsal);

		/* For subtree claims, only consider children that are
		 * children of the provided directory. This handles the
		 * case of an export of something other than the root
		 * of a file system.
		 */
		if (claim_type == CLAIM_SUBTREE &&
		    !is_filesystem_child(child_fs, path, pathlen))
			continue;

		/* Test if the root of this fs is exported, if so, skip it. It
		 * doesn't matter if the claim is for our FSAL or not.
		 */
		if (child_fs->claims[CLAIM_ROOT])
			continue;

		/* Test if there are subtree claims from a different FSAL */
		if (child_fs->claims[CLAIM_SUBTREE] && child_fs->fsal != fsal) {
			LogWarn(COMPONENT_FSAL,
				"FSAL %s export path %s includes filesystem %s which has subtree exports from FSAL %s - not exporting it as a child filesystem",
				fsal->name, path,
				child_fs->path, child_fs->fsal->name);
			continue;
		}

		/* Now we need to check if there is a claim deeper into this
		 * filesystem that has a child claim on the child filesystem.
		 * If so, the filesystem isn't a candidate.
		 */
		if (child_fs->claims[CLAIM_CHILD] != 0) {
			bool skip = false;

			/* Examine the child claims of the child filesystem to
			 * determinate if they are held by an export that has
			 * the same path as ours (in which case we can also
			 * make child claims) or not (in which case we know
			 * that export is a subtree of this export and thus
			 * that export gets the child claims).
			 */
			glist_for_each(export_glist, &child_fs->exports) {
				struct fsal_filesystem_export_map *other_map;
				struct gsh_refstr *map_fullpath;
				size_t map_pathlen;

				other_map = glist_entry(
					export_glist,
					struct fsal_filesystem_export_map,
					on_exports);

				if (other_map->claim_type == CLAIM_SUBTREE) {
					/* A subtree claim doesn't block us
					 * from taking a child claim.
					 */
					continue;
				}

				/* We already skipped the child filesystem if it
				 * had any root claims on it, so we better not
				 * find any in the map now... And therefore
				 * the claim we are now looking at MUST be a
				 * child claim.
				 */
				assert(other_map->claim_type == CLAIM_CHILD);

				map_fullpath = gsh_refstr_get(rcu_dereference(
				      other_map->exp->owning_export->fullpath));

				map_pathlen = strlen(map_fullpath->gr_val);

				/* Check if the child claim is from an export
				 * with a matching path or not. If from a path
				 * that doesn't match, it MUST be a subtree of
				 * this export's path and therefore we can not
				 * take child claims. Otherwise since it matches
				 * child claims will be ok.
				 */
				if (map_pathlen != pathlen ||
				    strcmp(map_fullpath->gr_val, path) != 0) {
					/* Child claim is from an export that is
					 * a subtree of this export so indicate
					 * that we must skip the filesystem.
					 */
					skip = true;
				}

				/* And we're done with the refstr. */
				gsh_refstr_put(map_fullpath);

				/* Since we've hit a child claim and all child
				 * claims must be from exports with the same
				 * path, we know if the path was the same as
				 * ours (in which case we can take a claim) or
				 * not.
				 */
				break;
			} /* end of glist_for_each */

			if (skip) {
				/* There is one or more exports with a path
				 * that is a subtree of our path that have
				 * child claims on the filesystem under
				 * consideration, so we can't claim it.
				 */
				continue;
			}
		} /* end of if (child_fs->claims[CLAIM_CHILD] != 0) */

		/* Try to claim this child, we dont' care about the return
		 * it might be a child filesystem this FSAL can't export or
		 * there might be some other problem, but it shouldn't cause
		 * failure of the export as a whole.
		 */
		(void) process_claim(NULL, 0, map, child_fs, fsal,
				     exp, claimfs, unclaim);
	}

account:

	/* Account for the claim */
	this->claims[claim_type]++;
	this->claims[CLAIM_ALL]++;

	/* Release the temporary claim */
	this->claims[CLAIM_TEMP]--;

	LogFullDebug(COMPONENT_FSAL,
		     "Completing claim type %s by FSAL %s on filesystem %s",
		     str_claim_type(claim_type), fsal->name, this->path);

	/* Now that we are done with all that, add this map into this
	 * filesystem and export (doing this late like this saves looking at it
	 * in loops above).
	 */
	glist_add_tail(&this->exports, &map->on_exports);
	glist_add_tail(&exp->filesystems, &map->on_filesystems);
	if (parent_map != NULL)
		glist_add_tail(&parent_map->child_maps, &map->on_parent);

	LogFilesystem("PROCESS CLAIM FINISHED", "", this);

	return retval;
}

int claim_posix_filesystems(const char *path,
			    struct fsal_module *fsal,
			    struct fsal_export *exp,
			    claim_filesystem_cb claimfs,
			    unclaim_filesystem_cb unclaim,
			    struct fsal_filesystem **root_fs,
			    struct stat *statbuf)
{
	int retval = 0;
	struct fsal_filesystem *fs, *root = NULL;
	struct glist_head *glist;
	struct fsal_dev__ dev;

	PTHREAD_RWLOCK_wrlock(&fs_lock);

	dev = posix2fsal_devt(statbuf->st_dev);

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
		retval = ENOENT;
		goto out;
	}

	/* Claim this file system and it's children */
	retval = process_claim(path, strlen(path), NULL, root, fsal,
			       exp, claimfs, unclaim);

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

#ifdef USE_DBUS

 /**
 *@brief Dbus method for showing dev ids of mounted POSIX filesystems
 *
 *@param[in]  args
 *@param[out] reply
 **/
static bool posix_showfs(DBusMessageIter *args,
			 DBusMessage *reply,
			 DBusError *error)
{
	struct fsal_filesystem *fs;
	struct glist_head *glist;
	DBusMessageIter iter, sub_iter, fs_iter;
	struct timespec timestamp;
	uint64_t val;
	char *path;

	dbus_message_iter_init_append(reply, &iter);
	now(&timestamp);
	gsh_dbus_append_timestamp(&iter, &timestamp);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 "(stt)",
					 &sub_iter);

	PTHREAD_RWLOCK_rdlock(&fs_lock);
	/* Traverse POSIX file systems to display dev ids */
	glist_for_each(glist, &posix_file_systems) {
		fs = glist_entry(glist, struct fsal_filesystem, filesystems);

		dbus_message_iter_open_container(&sub_iter,
			DBUS_TYPE_STRUCT, NULL, &fs_iter);

		path = (fs->path != NULL) ? fs->path : "";
		dbus_message_iter_append_basic(&fs_iter,
			DBUS_TYPE_STRING, &path);

		val = fs->dev.major;
		dbus_message_iter_append_basic(&fs_iter, DBUS_TYPE_UINT64,
			&val);

		val = fs->dev.minor;
		dbus_message_iter_append_basic(&fs_iter, DBUS_TYPE_UINT64,
			&val);

		dbus_message_iter_close_container(&sub_iter,
			&fs_iter);
	}
	PTHREAD_RWLOCK_unlock(&fs_lock);
	dbus_message_iter_close_container(&iter, &sub_iter);
	return true;
}

static struct gsh_dbus_method cachemgr_show_fs = {
	.name = "showfs",
	.method = posix_showfs,
	.args = {TIMESTAMP_REPLY,
		{
		  .name = "fss",
		  .type = "a(stt)",
		  .direction = "out"},
		END_ARG_LIST}
};

static struct gsh_dbus_method *cachemgr_methods[] = {
	&cachemgr_show_fs,
	&cachemgr_show_idmapper,
	NULL
};

static struct gsh_dbus_interface cachemgr_table = {
	.name = "org.ganesha.nfsd.cachemgr",
	.props = NULL,
	.methods = cachemgr_methods,
	.signals = NULL
};

/* DBUS list of interfaces on /org/ganesha/nfsd/CacheMgr
 * Intended for showing different caches
 */

static struct gsh_dbus_interface *cachemgr_interfaces[] = {
	&cachemgr_table,
	NULL
};

void dbus_cache_init(void)
{
	gsh_dbus_register_path("CacheMgr", cachemgr_interfaces);
}

#endif                          /* USE_DBUS */
