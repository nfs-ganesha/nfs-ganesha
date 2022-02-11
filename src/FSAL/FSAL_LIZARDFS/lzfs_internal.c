// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright (C) 2019 Skytechnology sp. z o.o.
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "fsal.h"
#include "fsal_convert.h"
#include "pnfs_utils.h"

#include "lzfs_internal.h"

fsal_status_t lizardfs2fsal_error(int ec)
{
	fsal_status_t status;

	if (!ec) {
		LogWarn(COMPONENT_FSAL, "appropriate errno not set");
		ec = EINVAL;
	}

	status.minor = ec;
	status.major = posix2fsal_error(liz_error_conv(ec));

	return status;
}

nfsstat4 lizardfs2nfs4_error(int ec)
{
	if (!ec) {
		LogWarn(COMPONENT_FSAL, "appropriate errno not set");
		ec = EINVAL;
	}
	return posix2nfs4_error(liz_error_conv(ec));
}

fsal_status_t lzfs_fsal_last_err(void)
{
	return lizardfs2fsal_error(liz_last_err());
}

nfsstat4 lzfs_nfs4_last_err(void)
{
	return lizardfs2nfs4_error(liz_last_err());
}

liz_context_t *lzfs_fsal_create_context(liz_t *instance,
					struct user_cred *cred)
{
	static const int kLocalGArraySize = 64;

	if (cred == NULL) {
		liz_context_t *ctx = liz_create_user_context(0, 0, 0, 0);
		return ctx;
	}

	liz_context_t *ctx;
	uid_t uid = (cred->caller_uid == op_ctx->export_perms.anonymous_uid) ?
							0 : cred->caller_uid;
	gid_t gid = (cred->caller_gid == op_ctx->export_perms.anonymous_gid) ?
							0 : cred->caller_gid;

	ctx = liz_create_user_context(uid, gid, 0, 0);
	if (!ctx) {
		return NULL;
	}

	if (cred->caller_glen > 0) {
		if (cred->caller_glen > kLocalGArraySize) {
			gid_t *garray = malloc((cred->caller_glen + 1) *
							sizeof(gid_t));

			if (garray != NULL) {
				garray[0] = gid;
				memcpy(garray + 1,
				       cred->caller_garray,
				       sizeof(gid_t) * cred->caller_glen);
				liz_update_groups(instance, ctx, garray,
						  cred->caller_glen + 1);
				free(garray);
				return ctx;
			}
		}

		gid_t garray[kLocalGArraySize + 1];

		garray[0] = gid;
		int count = MIN(cred->caller_glen, kLocalGArraySize);

		memcpy(garray + 1, cred->caller_garray, sizeof(gid_t) * count);
		liz_update_groups(instance, ctx, garray, count + 1);
	}

	return ctx;
}

fsal_staticfsinfo_t *lzfs_fsal_staticinfo(struct fsal_module *module_hdl)
{
	struct lzfs_fsal_module *lzfs_module = container_of(
				module_hdl, struct lzfs_fsal_module, fsal);
	return &lzfs_module->fs_info;
}

struct lzfs_fsal_handle *lzfs_fsal_new_handle(
		const struct stat *attr, struct lzfs_fsal_export *lzfs_export)
{
	struct lzfs_fsal_handle *result = NULL;

	result = gsh_calloc(1, sizeof(struct lzfs_fsal_handle));

	result->inode = attr->st_ino;
	result->unique_key.module_id = FSAL_ID_LIZARDFS;
	result->unique_key.export_id = lzfs_export->export.export_id;
	result->unique_key.inode = attr->st_ino;

	fsal_obj_handle_init(&result->handle,
			     &lzfs_export->export,
			     posix2fsal_type(attr->st_mode));
	lzfs_fsal_handle_ops_init(lzfs_export, result->handle.obj_ops);
	result->handle.fsid = posix2fsal_fsid(attr->st_dev);
	result->handle.fileid = attr->st_ino;
	result->export = lzfs_export;

	return result;
}

void lzfs_fsal_delete_handle(struct lzfs_fsal_handle *obj)
{
	fsal_obj_handle_fini(&obj->handle);
	gsh_free(obj);
}
