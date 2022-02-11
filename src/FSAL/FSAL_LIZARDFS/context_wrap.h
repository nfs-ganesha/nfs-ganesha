/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

#include <fsal_types.h>

#include "lizardfs/lizardfs_c_api.h"

int liz_cred_lookup(liz_t *instance, struct user_cred *cred,
		    liz_inode_t parent, const char *path,
		    struct liz_entry *entry);

int liz_cred_mknod(liz_t *instance, struct user_cred *cred, liz_inode_t parent,
		   const char *path, mode_t mode, dev_t rdev,
		   struct liz_entry *entry);

liz_fileinfo_t *liz_cred_open(liz_t *instance, struct user_cred *cred,
			      liz_inode_t inode, int flags);

ssize_t liz_cred_read(liz_t *instance, struct user_cred *cred,
		      liz_fileinfo_t *fileinfo, off_t offset, size_t size,
		      char *buffer);

ssize_t liz_cred_write(liz_t *instance, struct user_cred *cred,
		       liz_fileinfo_t *fileinfo, off_t offset, size_t size,
		       const char *buffer);

int liz_cred_flush(liz_t *instance, struct user_cred *cred,
		   liz_fileinfo_t *fileinfo);

int liz_cred_getattr(liz_t *instance, struct user_cred *cred,
		     liz_inode_t inode, struct liz_attr_reply *reply);

liz_fileinfo_t *liz_cred_opendir(liz_t *instance, struct user_cred *cred,
				 liz_inode_t inode);

int liz_cred_readdir(liz_t *instance, struct user_cred *cred,
		     struct liz_fileinfo *fileinfo, off_t offset,
		     size_t max_entries, struct liz_direntry *buf,
		     size_t *num_entries);

int liz_cred_mkdir(liz_t *instance, struct user_cred *cred, liz_inode_t parent,
		   const char *name, mode_t mode, struct liz_entry *out_entry);

int liz_cred_rmdir(liz_t *instance, struct user_cred *cred, liz_inode_t parent,
		   const char *name);

int liz_cred_unlink(liz_t *instance, struct user_cred *cred,
		    liz_inode_t parent, const char *name);

int liz_cred_setattr(liz_t *instance, struct user_cred *cred,
		     liz_inode_t inode, struct stat *stbuf, int to_set,
		     struct liz_attr_reply *reply);

int liz_cred_fsync(liz_t *instance, struct user_cred *cred,
		   struct liz_fileinfo *fileinfo);

int liz_cred_rename(liz_t *instance, struct user_cred *cred,
		    liz_inode_t parent, const char *name,
		    liz_inode_t new_parent, const char *new_name);

int liz_cred_symlink(liz_t *instance, struct user_cred *cred, const char *link,
		     liz_inode_t parent, const char *name,
		     struct liz_entry *entry);

int liz_cred_readlink(liz_t *instance, struct user_cred *cred,
		      liz_inode_t inode, char *buf, size_t size);

int liz_cred_link(liz_t *instance, struct user_cred *cred, liz_inode_t inode,
		  liz_inode_t parent, const char *name,
		  struct liz_entry *entry);

int liz_cred_get_chunks_info(liz_t *instance, struct user_cred *cred,
			     liz_inode_t inode, uint32_t chunk_index,
			     liz_chunk_info_t *buffer, uint32_t buffer_size,
			     uint32_t *reply_size);

int liz_cred_setacl(liz_t *instance, struct user_cred *cred, liz_inode_t inode,
		    const liz_acl_t *acl);

int liz_cred_getacl(liz_t *instance, struct user_cred *cred, liz_inode_t inode,
		    liz_acl_t **acl);

int liz_cred_setlk(liz_t *instance, struct user_cred *cred,
		   liz_fileinfo_t *fileinfo, const liz_lock_info_t *lock);

int liz_cred_getlk(liz_t *instance, struct user_cred *cred,
		   liz_fileinfo_t *fileinfo, liz_lock_info_t *lock);
