// SPDX-License-Identifier: LGPL-3.0-or-later
/*
   Copyright 2017 Skytechnology sp. z o.o.
   Copyright 2023 Leil Storage OÜ

   This file is part of SaunaFS.

   SaunaFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   SaunaFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with SaunaFS. If not, see <http://www.gnu.org/licenses/>.
 */

#include "context_wrap.h"
#include "saunafs_internal.h"

int saunafs_lookup(sau_t *instance, struct user_cred *cred, sau_inode_t parent,
		   const char *path, struct sau_entry *entry)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_lookup(instance, context, parent, path, entry);
}

int saunafs_mknode(sau_t *instance, struct user_cred *cred, sau_inode_t parent,
		   const char *path, mode_t mode, dev_t rdev,
		   struct sau_entry *entry)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_mknod(instance, context, parent, path, mode, rdev, entry);
}

fileinfo_t *saunafs_open(sau_t *instance, struct user_cred *cred,
			 sau_inode_t inode, int flags)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return NULL;

	return sau_open(instance, context, inode, flags);
}

ssize_t saunafs_read(sau_t *instance, struct user_cred *cred,
		     fileinfo_t *fileinfo, off_t offset, size_t size,
		     char *buffer)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_read(instance, context, fileinfo, offset, size, buffer);
}

ssize_t saunafs_write(sau_t *instance, struct user_cred *cred,
		      fileinfo_t *fileinfo, off_t offset, size_t size,
		      const char *buffer)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_write(instance, context, fileinfo, offset, size, buffer);
}

int saunafs_flush(sau_t *instance, struct user_cred *cred, fileinfo_t *fileinfo)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_flush(instance, context, fileinfo);
}

int saunafs_getattr(sau_t *instance, struct user_cred *cred, sau_inode_t inode,
		    struct sau_attr_reply *reply)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_getattr(instance, context, inode, reply);
}

fileinfo_t *saunafs_opendir(sau_t *instance, struct user_cred *cred,
			    sau_inode_t inode)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return NULL;

	return sau_opendir(instance, context, inode);
}

int saunafs_readdir(sau_t *instance, struct user_cred *cred,
		    struct sau_fileinfo *fileinfo, off_t offset,
		    size_t max_entries, struct sau_direntry *buf,
		    size_t *num_entries)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_readdir(instance, context, fileinfo, offset, max_entries,
			   buf, num_entries);
}

int saunafs_mkdir(sau_t *instance, struct user_cred *cred, sau_inode_t parent,
		  const char *name, mode_t mode, struct sau_entry *out_entry)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_mkdir(instance, context, parent, name, mode, out_entry);
}

int saunafs_rmdir(sau_t *instance, struct user_cred *cred, sau_inode_t parent,
		  const char *name)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_rmdir(instance, context, parent, name);
}

int saunafs_unlink(sau_t *instance, struct user_cred *cred, sau_inode_t parent,
		   const char *name)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_unlink(instance, context, parent, name);
}

int saunafs_setattr(sau_t *instance, struct user_cred *cred, sau_inode_t inode,
		    struct stat *stbuf, int to_set,
		    struct sau_attr_reply *reply)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_setattr(instance, context, inode, stbuf, to_set, reply);
}

int saunafs_fsync(sau_t *instance, struct user_cred *cred,
		  struct sau_fileinfo *fileinfo)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_fsync(instance, context, fileinfo);
}

int saunafs_rename(sau_t *instance, struct user_cred *cred, sau_inode_t parent,
		   const char *name, sau_inode_t new_parent,
		   const char *new_name)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_rename(instance, context, parent, name, new_parent,
			  new_name);
}

int saunafs_symlink(sau_t *instance, struct user_cred *cred, const char *link,
		    sau_inode_t parent, const char *name,
		    struct sau_entry *entry)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_symlink(instance, context, link, parent, name, entry);
}

int saunafs_readlink(sau_t *instance, struct user_cred *cred, sau_inode_t inode,
		     char *buf, size_t size)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_readlink(instance, context, inode, buf, size);
}

int saunafs_link(sau_t *instance, struct user_cred *cred, sau_inode_t inode,
		 sau_inode_t parent, const char *name, struct sau_entry *entry)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_link(instance, context, inode, parent, name, entry);
}

int saunafs_get_chunks_info(sau_t *instance, struct user_cred *cred,
			    sau_inode_t inode, uint32_t chunk_index,
			    sau_chunk_info_t *buff, uint32_t buffer_size,
			    uint32_t *reply_size)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_get_chunks_info(instance, context, inode, chunk_index, buff,
				   buffer_size, reply_size);
}

int saunafs_setacl(sau_t *instance, struct user_cred *cred, sau_inode_t inode,
		   sau_acl_t *acl)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_setacl(instance, context, inode, acl);
}

int saunafs_getacl(sau_t *instance, struct user_cred *cred, sau_inode_t inode,
		   sau_acl_t **acl)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_getacl(instance, context, inode, acl);
}

int saunafs_setlock(sau_t *instance, struct user_cred *cred,
		    fileinfo_t *fileinfo, const sau_lock_info_t *lock)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_setlk(instance, context, fileinfo, lock, NULL, NULL);
}

int saunafs_getlock(sau_t *instance, struct user_cred *cred,
		    fileinfo_t *fileinfo, sau_lock_info_t *lock)
{
	sau_context_t *context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_getlk(instance, context, fileinfo, lock);
}

int saunafs_getxattr(sau_t *instance, struct user_cred *cred, sau_inode_t ino,
		     const char *name, size_t size, size_t *out_size,
		     uint8_t *buf)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_getxattr(instance, context, ino, name, size, out_size, buf);
}

int saunafs_setxattr(sau_t *instance, struct user_cred *cred, sau_inode_t ino,
		     const char *name, const uint8_t *value, size_t size,
		     int flags)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_setxattr(instance, context, ino, name, value, size, flags);
}

int saunafs_listxattr(sau_t *instance, struct user_cred *cred, sau_inode_t ino,
		      size_t size, size_t *out_size, char *buf)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_listxattr(instance, context, ino, size, out_size, buf);
}

int saunafs_removexattr(sau_t *instance, struct user_cred *cred,
			sau_inode_t ino, const char *name)
{
	sau_context_t *context
		__attribute__((cleanup(sau_destroy_context))) = NULL;
	context = createContext(instance, cred);

	if (context == NULL)
		return -1;

	return sau_removexattr(instance, context, ino, name);
}
