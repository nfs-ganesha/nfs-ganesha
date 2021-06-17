/*
 *
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

#ifndef FSAL_LOCAL_FS_H
#define FSAL_LOCAL_FS_H

#include "fsal_api.h"

#if !GSH_CAN_HOST_LOCAL_FS

static inline void release_posix_file_systems(void) {}
#ifdef USE_DBUS
static inline void dbus_cache_init(void) {}
#endif

#else

int open_dir_by_path_walk(int first_fd, const char *path, struct stat *stat);

static inline int fsal_fs_compare_fsid(enum fsid_type left_fsid_type,
				       const struct fsal_fsid__ *left_fsid,
				       enum fsid_type right_fsid_type,
				       const struct fsal_fsid__ *right_fsid)
{
	if (left_fsid_type < right_fsid_type)
		return -1;

	if (left_fsid_type > right_fsid_type)
		return 1;

	if (left_fsid->major < right_fsid->major)
		return -1;

	if (left_fsid->major > right_fsid->major)
		return 1;

	/* No need to compare minors as they should be
	 * zeros if the type is FSID_MAJOR_64
	 */
	if (left_fsid_type == FSID_MAJOR_64) {
		assert(right_fsid_type == FSID_MAJOR_64);
		return 0;
	}

	if (left_fsid->minor < right_fsid->minor)
		return -1;

	if (left_fsid->minor > right_fsid->minor)
		return 1;

	return 0;
}

extern pthread_rwlock_t fs_lock;

#define LogFilesystem(cmt, cmt2, fs) \
	LogFullDebug(COMPONENT_FSAL, \
		     "%s%s FS %p %s parent %p %s children? %s siblings? %s " \
		     "FSAL %s exports? %s private %p " \
		     "claims ALL %d ROOT %d SUBTREE %d CHILD %d TEMP %d", \
		     (cmt), (cmt2), (fs), (fs)->path, (fs)->parent, \
		     (fs)->parent ? (fs)->parent->path : "NONE", \
		     glist_empty(&(fs)->children) ? "NO" : "YES", \
		     glist_null(&(fs)->siblings) ? "NO" : "YES", \
		     (fs)->fsal ? (fs)->fsal->name : "NONE", \
		     glist_empty(&(fs)->exports) ? "NO" : "YES", \
		     (fs)->private_data, \
		     (fs)->claims[CLAIM_ALL], \
		     (fs)->claims[CLAIM_ROOT], \
		     (fs)->claims[CLAIM_SUBTREE], \
		     (fs)->claims[CLAIM_CHILD], \
		     (fs)->claims[CLAIM_TEMP])

int populate_posix_file_systems(const char *path);

int resolve_posix_filesystem(const char *path,
			     struct fsal_module *fsal,
			     struct fsal_export *exp,
			     claim_filesystem_cb claimfs,
			     unclaim_filesystem_cb unclaim,
			     struct fsal_filesystem **root_fs);

void release_posix_file_systems(void);

enum release_claims {
	UNCLAIM_WARN,
	UNCLAIM_SKIP,
};

bool release_posix_file_system(struct fsal_filesystem *fs,
			       enum release_claims release_claims);

int re_index_fs_fsid(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type,
		     struct fsal_fsid__ *fsid);

int re_index_fs_dev(struct fsal_filesystem *fs,
		    struct fsal_dev__ *dev);

int change_fsid_type(struct fsal_filesystem *fs,
		     enum fsid_type fsid_type);

struct fsal_filesystem *lookup_fsid_locked(struct fsal_fsid__ *fsid,
					   enum fsid_type fsid_type);
struct fsal_filesystem *lookup_dev_locked(struct fsal_dev__ *dev);
struct fsal_filesystem *lookup_fsid(struct fsal_fsid__ *fsid,
				    enum fsid_type fsid_type);
struct fsal_filesystem *lookup_dev(struct fsal_dev__ *dev);

int claim_posix_filesystems(const char *path,
			    struct fsal_module *fsal,
			    struct fsal_export *exp,
			    claim_filesystem_cb claimfs,
			    unclaim_filesystem_cb unclaim,
			    struct fsal_filesystem **root_fs,
			    struct stat *statbuf);

bool is_filesystem_exported(struct fsal_filesystem *fs,
			    struct fsal_export *exp);

void unclaim_all_export_maps(struct fsal_export *exp);

#ifdef USE_DBUS
void dbus_cache_init(void);
#endif

#endif		/* GSH_CAN_HOST_LOCAL_FS */

#endif				/* FSAL_LOCAL_FS_H */
