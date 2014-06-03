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

/* handle.c
 * VFS object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include "fsal_handle_syscalls.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"
#include <os/subr.h>
#include "pnfs_panfs/mds.h"

int vfs_readlink(struct vfs_fsal_obj_handle *myself,
		 fsal_errors_t *fsal_error)
{
	int retval = 0;
	int fd;
	ssize_t retlink;
	struct stat st;
	int flags = O_PATH | O_NOACCESS | O_NOFOLLOW;

	if (myself->u.symlink.link_content != NULL) {
		gsh_free(myself->u.symlink.link_content);
		myself->u.symlink.link_content = NULL;
		myself->u.symlink.link_size = 0;
	}
	fd = vfs_fsal_open(myself, flags, fsal_error);
	if (fd < 0)
		return fd;

	retval = vfs_stat_by_handle(fd, myself->handle, &st, flags);
	if (retval < 0)
		goto error;

	myself->u.symlink.link_size = st.st_size + 1;
	myself->u.symlink.link_content =
	    gsh_malloc(myself->u.symlink.link_size);
	if (myself->u.symlink.link_content == NULL)
		goto error;

	retlink =
	    vfs_readlink_by_handle(myself->handle, fd, "",
				   myself->u.symlink.link_content,
				   myself->u.symlink.link_size);
	if (retlink < 0)
		goto error;
	myself->u.symlink.link_content[retlink] = '\0';
	close(fd);

	return retval;

 error:
	retval = -errno;
	*fsal_error = posix2fsal_error(errno);
	close(fd);
	if (myself->u.symlink.link_content != NULL) {
		gsh_free(myself->u.symlink.link_content);
		myself->u.symlink.link_content = NULL;
		myself->u.symlink.link_size = 0;
	}
	return retval;
}

int vfs_get_root_handle(struct vfs_filesystem *vfs_fs,
			struct vfs_fsal_export *exp)
{
	int retval = 0;

	vfs_fs->root_fd = open(vfs_fs->fs->path, O_RDONLY | O_DIRECTORY);

	if (vfs_fs->root_fd < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Could not open VFS mount point %s: rc = %s (%d)",
			 vfs_fs->fs->path, strerror(retval), retval);
		return retval;
	}

	/* Check if we have to re-index the fsid based on config */
	if (exp->fsid_type != -1 &&
	    exp->fsid_type != vfs_fs->fs->fsid_type) {
		retval = -change_fsid_type(vfs_fs->fs, exp->fsid_type);
		if (retval != 0) {
			LogCrit(COMPONENT_FSAL,
				"Can not change fsid type of %s to %d, error %s",
				vfs_fs->fs->path, exp->fsid_type,
				strerror(retval));
			return retval;
		}

		LogInfo(COMPONENT_FSAL,
			"Reindexed filesystem %s to "
			"fsid=0x%016"PRIx64".0x%016"PRIx64,
			vfs_fs->fs->path,
			vfs_fs->fs->fsid.major,
			vfs_fs->fs->fsid.minor);
	}

	if (retval != 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Get root handle for %s failed with %s (%d)",
			 vfs_fs->fs->path, strerror(retval), retval);
	} else {
		/* May reindex for some platforms */
		retval = vfs_re_index(vfs_fs, exp);
	}

	return retval;
}

void vfs_fini(struct vfs_fsal_export *myself)
{
	pnfs_panfs_fini(myself->pnfs_data);
}

void vfs_init_export_ops(struct vfs_fsal_export *myself,
			 const char *export_path)
{
	if (myself->pnfs_panfs_enabled) {
		LogInfo(COMPONENT_FSAL,
			"pnfs_panfs was enabled for [%s]",
			export_path);
		export_ops_pnfs(myself->export.ops);
		handle_ops_pnfs(myself->export.obj_ops);
	}
}

int vfs_init_export_pnfs(struct vfs_fsal_export *myself)
{
	int retval = 0;

	if (myself->pnfs_panfs_enabled) {
		retval = pnfs_panfs_init(vfs_get_root_fd(&myself->export),
							 &myself->pnfs_data);
		if (retval) {
			LogCrit(COMPONENT_FSAL,
				"vfs export_ops_pnfs failed => %d [%s]",
				retval, strerror(retval));
		}
	}

	return retval;
}
