// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017 VMware, Inc.
 * Copyright 2018 Red Hat, Inc.
 * Author: Sriram Patil sriramp@vmware.com
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

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "fsal_api.h"
#include "vfs_methods.h"
#include "fsal_types.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_convert.h"
#include "nfs_core.h"
#include "nfs_proto_tools.h"

fsal_status_t vfs_get_fs_locations(struct vfs_fsal_obj_handle *hdl,
				   int fd,
				   struct fsal_attrlist *attrs_out)
{
	char *xattr_content = NULL;
	size_t attrsize = 0;
	char proclnk[MAXPATHLEN];
	char readlink_buf[MAXPATHLEN];
	char *spath;
	ssize_t r;
	fsal_status_t st = {ERR_FSAL_NO_ERROR, 0};
	int local_fd = fd;

	/* the real path of the referral directory is needed.
	 * it get's stored in attrs_out->fs_locations->path
	 */

	if (fd < 0) {
		local_fd = vfs_fsal_open(hdl, O_DIRECTORY, &st.major);
		if (local_fd < 0) {
			st.minor = -local_fd;
			return st;
		}
	}

	(void) snprintf(proclnk, sizeof(proclnk),
			"/proc/self/fd/%d", local_fd);
	r = readlink(proclnk, readlink_buf, sizeof(readlink_buf) - 1);
	if (r < 0) {
		st = posix2fsal_status(errno);
		LogEvent(COMPONENT_FSAL, "failed to readlink");
		goto out;
	}

	readlink_buf[r] = '\0';
	LogDebug(COMPONENT_FSAL, "fd -> path: %d -> %s", local_fd,
		 readlink_buf);

	// Release old fs locations if any
	nfs4_fs_locations_release(attrs_out->fs_locations);

	spath = readlink_buf;

	/* If Path and Pseudo path are not equal replace path with
	 * pseudo path.
	 */
	if (strcmp(CTX_FULLPATH(op_ctx), CTX_PSEUDOPATH(op_ctx)) != 0) {
		size_t pseudo_length = strlen(CTX_PSEUDOPATH(op_ctx));
		size_t fullpath_length = strlen(CTX_FULLPATH(op_ctx));
		size_t dirpath_len = r - fullpath_length;
		size_t total_length = pseudo_length + dirpath_len;
		char *dirpath = spath + fullpath_length;

		if (total_length >= sizeof(proclnk)) {
			st = posix2fsal_status(EINVAL);
			LogCrit(COMPONENT_FSAL,
				"Fixed up referral path %s%s too long",
				CTX_PSEUDOPATH(op_ctx), dirpath);
			goto out;
		}

		memcpy(proclnk, CTX_PSEUDOPATH(op_ctx), pseudo_length);
		memcpy(proclnk + pseudo_length, dirpath, dirpath_len + 1);
		spath = proclnk;
	}

	/* referral configuration is in a xattr "user.fs_location"
	 * on the directory in the form
	 * server:/path/to/referred/directory.
	 * It gets storeded in attrs_out->fs_locations->locations
	 */

	xattr_content = gsh_calloc(XATTR_BUFFERSIZE, sizeof(char));

	st = vfs_getextattr_value(hdl, local_fd, "user.fs_location",
				  xattr_content, XATTR_BUFFERSIZE, &attrsize);

	if (!FSAL_IS_ERROR(st)) {
		char *path = xattr_content;
		char *server = strsep(&path, ":");

		LogDebug(COMPONENT_FSAL, "user.fs_location: %s", xattr_content);

		if (!path) {
			attrs_out->fs_locations = NULL;
		} else {
			attrs_out->fs_locations =
				nfs4_fs_locations_new(spath, path, 1);

			attrs_out->fs_locations->nservers = 1;
			utf8string_dup(&attrs_out->fs_locations->server[0],
					server, path - server - 1);

			FSAL_SET_MASK(attrs_out->valid_mask,
				      ATTR4_FS_LOCATIONS);
		}
	}

out:
	gsh_free(xattr_content);

	// Close the local_fd only if no fd was passed into the function and we
	// opened the file in this function explicitly.
	if (fd < 0 && local_fd > 0) {
		close(local_fd);
	}

	return st;
}
