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

#include "context_wrap.h"
#include "lzfs_internal.h"

int liz_cred_lookup(liz_t *instance, struct user_cred *cred,
		    liz_inode_t parent, const char *path,
		    struct liz_entry *entry)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_lookup(instance, ctx, parent, path, entry);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_mknod(liz_t *instance, struct user_cred *cred, liz_inode_t parent,
		   const char *path, mode_t mode, dev_t rdev,
		   struct liz_entry *entry)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_mknod(instance, ctx, parent, path, mode, rdev, entry);

	liz_destroy_context(ctx);
	return rc;
}

liz_fileinfo_t *liz_cred_open(liz_t *instance, struct user_cred *cred,
			      liz_inode_t inode, int flags)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return NULL;
	}
	liz_fileinfo_t *ret = liz_open(instance, ctx, inode, flags);

	liz_destroy_context(ctx);
	return ret;
}

ssize_t liz_cred_read(liz_t *instance, struct user_cred *cred,
		      liz_fileinfo_t *fileinfo, off_t offset, size_t size,
		      char *buffer)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	ssize_t ret = liz_read(instance, ctx, fileinfo, offset, size, buffer);

	liz_destroy_context(ctx);
	return ret;
}

ssize_t liz_cred_write(liz_t *instance, struct user_cred *cred,
		       liz_fileinfo_t *fileinfo, off_t offset, size_t size,
		       const char *buffer)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	ssize_t ret = liz_write(instance, ctx, fileinfo, offset, size, buffer);

	liz_destroy_context(ctx);
	return ret;
}

int liz_cred_flush(liz_t *instance, struct user_cred *cred,
		   liz_fileinfo_t *fileinfo)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_flush(instance, ctx, fileinfo);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_getattr(liz_t *instance, struct user_cred *cred,
		     liz_inode_t inode, struct liz_attr_reply *reply)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_getattr(instance, ctx, inode, reply);

	liz_destroy_context(ctx);
	return rc;
}

liz_fileinfo_t *liz_cred_opendir(liz_t *instance, struct user_cred *cred,
				 liz_inode_t inode)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return NULL;
	}
	liz_fileinfo_t *ret = liz_opendir(instance, ctx, inode);

	liz_destroy_context(ctx);
	return ret;
}

int liz_cred_readdir(liz_t *instance, struct user_cred *cred,
		     struct liz_fileinfo *fileinfo, off_t offset,
		     size_t max_entries, struct liz_direntry *buf,
		     size_t *num_entries)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_readdir(instance, ctx, fileinfo, offset, max_entries, buf,
			     num_entries);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_mkdir(liz_t *instance, struct user_cred *cred, liz_inode_t parent,
		   const char *name, mode_t mode, struct liz_entry *out_entry)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_mkdir(instance, ctx, parent, name, mode, out_entry);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_rmdir(liz_t *instance, struct user_cred *cred, liz_inode_t parent,
		   const char *name)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_rmdir(instance, ctx, parent, name);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_unlink(liz_t *instance, struct user_cred *cred,
		    liz_inode_t parent, const char *name)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_unlink(instance, ctx, parent, name);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_setattr(liz_t *instance, struct user_cred *cred,
		     liz_inode_t inode, struct stat *stbuf, int to_set,
		     struct liz_attr_reply *reply)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_setattr(instance, ctx, inode, stbuf, to_set, reply);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_fsync(liz_t *instance, struct user_cred *cred,
		   struct liz_fileinfo *fileinfo)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_fsync(instance, ctx, fileinfo);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_rename(liz_t *instance, struct user_cred *cred,
		    liz_inode_t parent, const char *name,
		    liz_inode_t new_parent, const char *new_name)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_rename(instance, ctx, parent, name, new_parent, new_name);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_symlink(liz_t *instance, struct user_cred *cred, const char *link,
		     liz_inode_t parent, const char *name,
		     struct liz_entry *entry)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_symlink(instance, ctx, link, parent, name, entry);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_readlink(liz_t *instance, struct user_cred *cred,
		      liz_inode_t inode, char *buf, size_t size)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_readlink(instance, ctx, inode, buf, size);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_link(liz_t *instance, struct user_cred *cred, liz_inode_t inode,
		  liz_inode_t parent, const char *name,
		  struct liz_entry *entry)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_link(instance, ctx, inode, parent, name, entry);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_get_chunks_info(liz_t *instance, struct user_cred *cred,
			     liz_inode_t inode, uint32_t chunk_index,
			     liz_chunk_info_t *buffer, uint32_t buffer_size,
			     uint32_t *reply_size)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc =
		liz_get_chunks_info(instance, ctx, inode, chunk_index, buffer,
				    buffer_size, reply_size);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_setacl(liz_t *instance, struct user_cred *cred, liz_inode_t inode,
		    const liz_acl_t *acl)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_setacl(instance, ctx, inode, acl);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_getacl(liz_t *instance, struct user_cred *cred, liz_inode_t inode,
		    liz_acl_t **acl)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_getacl(instance, ctx, inode, acl);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_setlk(liz_t *instance, struct user_cred *cred,
		   liz_fileinfo_t *fileinfo, const liz_lock_info_t *lock)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_setlk(instance, ctx, fileinfo, lock, NULL, NULL);

	liz_destroy_context(ctx);
	return rc;
}

int liz_cred_getlk(liz_t *instance, struct user_cred *cred,
		   liz_fileinfo_t *fileinfo, liz_lock_info_t *lock)
{
	liz_context_t *ctx = lzfs_fsal_create_context(instance, cred);

	if (ctx == NULL) {
		return -1;
	}
	int rc = liz_getlk(instance, ctx, fileinfo, lock);

	return rc;
}
