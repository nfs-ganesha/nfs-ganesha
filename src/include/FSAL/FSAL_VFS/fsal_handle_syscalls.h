/*
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef HANDLE_H
#define HANDLE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#ifndef __NR_name_to_handle_at
#if defined(__i386__)
#define __NR_name_to_handle_at  341
#define __NR_open_by_handle_at  342
#elif defined(__x86_64__)
#define __NR_name_to_handle_at  303
#define __NR_open_by_handle_at  304
#endif
#endif

#define AT_FDCWD		-100
#define AT_SYMLINK_FOLLOW	0x400
#define AT_EMPTY_PATH           0x1000
#define O_NOACCESS     00000003
#define __O_PATH       010000000
#define O_PATH         (__O_PATH | O_NOACCESS)

#define VFS_HANDLE_LEN 10
typedef struct vfs_file_handle {
        unsigned int handle_bytes;
        int handle_type;
        unsigned char handle[VFS_HANDLE_LEN];
} vfs_file_handle_t ;

#define AT_FDCWD		-100
#define AT_SYMLINK_FOLLOW	0x400
#define AT_EMPTY_PATH           0x1000
#define O_NOACCESS     00000003
#define __O_PATH       010000000
#define O_PATH         (__O_PATH | O_NOACCESS)

static inline int vfs_name_to_handle(const char *name, vfs_file_handle_t *fh, int *mnt_id)
{
  return syscall( __NR_name_to_handle_at, AT_FDCWD, name, fh, mnt_id, AT_SYMLINK_FOLLOW);
}

static inline int vfs_lname_to_handle(const char *name, vfs_file_handle_t *fh, int *mnt_id )
{
  return syscall( __NR_name_to_handle_at, AT_FDCWD, name, fh, mnt_id, 0);
}

static inline int vfs_fd_to_handle(int fd, vfs_file_handle_t * fh, int *mnt_id)
{
  return syscall( __NR_name_to_handle_at, fd, "", fh, mnt_id, AT_EMPTY_PATH);
}

static inline int vfs_open_by_handle(int mountfd, vfs_file_handle_t * fh, int flags)
{
  return syscall(__NR_open_by_handle_at, mountfd, fh, flags);
}

static inline int vfs_name_by_handle_at(int atfd, char * name, vfs_file_handle_t * fh)
{
  int mnt_id ;

  return syscall( __NR_name_to_handle_at, atfd, name, fh, &mnt_id, 0);
}

static inline ssize_t vfs_readlink_by_handle(int mountfd, vfs_file_handle_t *fh, char *buf, size_t bufsize)
{
        int fd, ret;

        fd = vfs_open_by_handle(mountfd, fh, O_PATH);
        if (fd < 0)
                return fd;
        ret = readlinkat(fd, "", buf, bufsize);
        close(fd);
        return ret;
}

static inline int vfs_stat_by_handle(int mountfd, vfs_file_handle_t *fh, struct stat *buf)
{
        int fd, ret;
        fd = vfs_open_by_handle(mountfd, fh, O_PATH);
        if (fd < 0)
                return fd;
        ret = fstatat(fd, "", buf, AT_EMPTY_PATH);
        close(fd);
        return ret;
}

static inline int vfs_link_by_handle(int mountfd, vfs_file_handle_t *fh, int newdirfd, char *newname)
{
        int fd, ret;
        fd = vfs_open_by_handle(mountfd, fh, O_PATH);
        if (fd < 0)
                return fd;
        ret = linkat(fd, "", newdirfd, newname, AT_EMPTY_PATH);
        close(fd);
        return ret;
}

static inline int vfs_chown_by_handle(int mountfd, vfs_file_handle_t *fh, uid_t owner, gid_t group)
{
        int fd, ret;
        fd = vfs_open_by_handle(mountfd, fh, O_PATH);
        if (fd < 0)
                return fd;
        ret = fchownat(fd, "", owner, group, AT_EMPTY_PATH);
        close(fd);
        return ret;
}

#endif
