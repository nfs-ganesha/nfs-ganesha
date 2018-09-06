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
				   struct attrlist *attrs_out)
{
	char *xattr_content;
	size_t attrsize = 0;
	char proclnk[MAXPATHLEN];
	char readlink_buf[MAXPATHLEN];
	char *spath;
	ssize_t r;
	fsal_status_t st;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int fd;

	/* the real path of the referral directory is needed.
	 * it get's stored in attrs_out->fs_locations->path
	 */

	fd = vfs_fsal_open(hdl, O_DIRECTORY, &fsal_error);
	if (fd < 0) {
		return fsalstat(fsal_error, -fd);
	}

	sprintf(proclnk, "/proc/self/fd/%d", fd);
	r = readlink(proclnk, readlink_buf, MAXPATHLEN - 1);
	if (r < 0) {
		fsal_error = posix2fsal_error(errno);
		r = errno;
		LogEvent(COMPONENT_FSAL, "failed to readlink");
		close(fd);
		return fsalstat(fsal_error, r);
	}
	readlink_buf[r] = '\0';
	LogDebug(COMPONENT_FSAL, "fd -> path: %d -> %s",
			fd, readlink_buf);

	// Release old fs locations if any
	nfs4_fs_locations_release(attrs_out->fs_locations);

	spath = readlink_buf;

	/* If Path and Pseudo path are not equal replace path with
	 * pseudo path.
	 */
	if (strcmp(op_ctx->ctx_export->fullpath,
				op_ctx->ctx_export->pseudopath) != 0) {
		int pseudo_length = strlen(
				op_ctx->ctx_export->pseudopath);
		int fullpath_length = strlen(
				op_ctx->ctx_export->fullpath);
		char *dirpath = spath + fullpath_length;

		memcpy(proclnk, op_ctx->ctx_export->pseudopath,
				pseudo_length);
		memcpy(proclnk + pseudo_length, dirpath,
				r - fullpath_length);
		proclnk[pseudo_length + (r - fullpath_length)] = '\0';
		spath = proclnk;
	}

	/* referral configuration is in a xattr "user.fs_location"
	 * on the directory in the form
	 * server:/path/to/referred/directory.
	 * It gets storeded in attrs_out->fs_locations->locations
	 */

	xattr_content = gsh_calloc(XATTR_BUFFERSIZE, sizeof(char));

	st = vfs_getextattr_value_by_name((struct fsal_obj_handle *)hdl,
			"user.fs_location",
			xattr_content,
			XATTR_BUFFERSIZE,
			&attrsize);

	if (!FSAL_IS_ERROR(st)) {
		size_t len;
		char *path = xattr_content;
		char *server = strsep(&path, ":");

		LogDebug(COMPONENT_FSAL, "user.fs_location: %s",
				xattr_content);

		attrs_out->fs_locations = nfs4_fs_locations_new(spath, path, 1);
		len = strlen(server);
		attrs_out->fs_locations->server[0].utf8string_len = len;
		attrs_out->fs_locations->server[0].utf8string_val =
							gsh_memdup(server, len);
		attrs_out->fs_locations->nservers = 1;
		FSAL_SET_MASK(attrs_out->valid_mask, ATTR4_FS_LOCATIONS);
	}
	gsh_free(xattr_content);
	close(fd);

	return st;
}
