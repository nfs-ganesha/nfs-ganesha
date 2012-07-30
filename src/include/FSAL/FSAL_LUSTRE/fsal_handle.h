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

#ifndef LUSTRE_HANDLE_H
#define LUSTRE_HANDLE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stddef.h> /* For having offsetof defined */

/* For llapi_quotactl */
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>

#ifndef AT_FDCWD
#error "Very old kernel and/or glibc"
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH           0x1000
#endif

#ifndef O_PATH
#define O_PATH 010000000
#endif

#ifndef O_NOACCESS
#define O_NOACCESS O_ACCMODE
#endif


typedef  struct lustre_file_handle 
{
    lustre_fid fid;
    /* used for FSAL_DIGEST_FILEID */
    unsigned long long inode;
} lustre_file_handle_t;  /**< FS object handle */

static inline int lustre_name_to_handle_at(int mdirfd, const char *name,
                                           struct lustre_file_handle * handle, int *mnt_id, int flags)
{
  abort() ;
  return 1 ;
}

static inline int lustre_open_by_handle_at(int mdirfd, struct lustre_file_handle * handle,
                                           int flags)
{
  abort() ;
  return 1 ;
}

static inline size_t lustre_sizeof_handle(struct lustre_file_handle *hdl)
{
	return (size_t)sizeof( struct lustre_file_handle ) ;
}

static inline int lustre_name_to_handle(const char *name, lustre_file_handle_t *fh, int *mnt_id)
{
  abort() ;
  return 1 ;
}

static inline int lustre_lname_to_handle(const char *name, lustre_file_handle_t *fh, int *mnt_id )
{
  abort() ;
  return 1 ;
}

static inline int lustre_fd_to_handle(int fd, lustre_file_handle_t * fh, int *mnt_id)
{
  abort() ;
  return 1 ;
}

static inline int lustre_open_by_handle(int mountfd, lustre_file_handle_t * fh, int flags)
{
  abort() ;
  return 1 ;
}

static inline int lustre_name_by_handle_at(int atfd, const char *name, lustre_file_handle_t *fh)
{
  abort() ;

  return 1 ;
}

static inline ssize_t lustre_readlink_by_handle(int mountfd, lustre_file_handle_t *fh, char *buf, size_t bufsize)
{
        int fd, ret;

        fd = lustre_open_by_handle(mountfd, fh, (O_PATH|O_NOACCESS));
        if (fd < 0)
                return fd;
        ret = readlinkat(fd, "", buf, bufsize);
        close(fd);
        return ret;
}

static inline int lustre_stat_by_handle(int mountfd, lustre_file_handle_t *fh, struct stat *buf)
{
        int fd, ret;
        fd = lustre_open_by_handle(mountfd, fh, (O_PATH|O_NOACCESS));
        if (fd < 0)
                return fd;
        ret = fstatat(fd, "", buf, AT_EMPTY_PATH);
        close(fd);
        return ret;
}

static inline int lustre_link_by_handle(int mountfd, lustre_file_handle_t *fh, int newdirfd, char *newname)
{
        int fd, ret;
        fd = lustre_open_by_handle(mountfd, fh, (O_PATH|O_NOACCESS));
        if (fd < 0)
                return fd;
        ret = linkat(fd, "", newdirfd, newname, AT_EMPTY_PATH);
        close(fd);
        return ret;
}

static inline int lustre_chown_by_handle(int mountfd, lustre_file_handle_t *fh, uid_t owner, gid_t group)
{
        int fd, ret;
        fd = lustre_open_by_handle(mountfd, fh, (O_PATH|O_NOACCESS));
        if (fd < 0)
                return fd;
        ret = fchownat(fd, "", owner, group, AT_EMPTY_PATH);
        close(fd);
        return ret;
}

#endif /* LUSTRE_HANDLE_H */
