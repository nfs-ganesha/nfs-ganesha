/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Sachin Bhamare sbhamare@panasas.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/**
 * @file    syscalls.h
 * @brief   platform dependant syscalls
 */

#ifndef _SYSCALLS_FREEBSD_H
#define _SYSCALLS_FREEBSD_H

#include <sys/mount.h>
#include <sys/syscall.h>

#ifndef AT_FDCWD
#define AT_FDCWD               -100

#define AT_SYMLINK_NOFOLLOW     0x200	/* Do not follow symbolic links */
#define AT_SYMLINK_FOLLOW       0x400	/* Follow symbolic link */
#define AT_REMOVEDIR            0x800	/* Remove directory instead of file */

#endif				/* AT_FDCWD */

#if __FreeBSD_cc_version  >= 800001
/* getfhat() is not implemented in FreeBSD kernel yet */
int getfhat(int fd, const char *path, fhandle_t *fhp);
int fhlink(struct fhandle *fhp, int tofd, const char *to);
int fhreadlink(struct fhandle *fhp, char *buf, size_t bufsize);
#endif				/* __FreeBSD_cc_version */

#ifndef SYS_openat
int openat(int dir_fd, const char *file, int oflag, mode_t mode);
int fchownat(int dir_fd, const char *file, uid_t owner, gid_t group, int flag);
int futimesat(int dir_fd, char *filename, struct timeval *utimes);
int fstatat(int dir_fd, const char *file, struct stat *st, int flag);
int getfhat(int dir_fd, char *fname, struct fhandle *fhp, int flag);
int fhopenat(int dir_fd, const struct fhandle *u_fhp, int flags);
int fchmodat(int dir_fd, const char *filename, mode_t mode, int flags);
int faccessat(int dir_fd, char *filename, int mode, int flags);
int linkat(int fromfd, const char *from, int tofd, const char *to, int flags);
int mkdirat(int dir_fd, const char *file, mode_t mode);
int mkfifoat(int dir_fd, char *file, mode_t mode);
int mknodat(int dir_fd, const char *file, mode_t mode, dev_t dev);
int unlinkat(int dir_fd, const char *file, int flag);
int readlinkat(int fd, const char *path, char *buf, size_t len);
int symlinkat(const char *from, int tofd, const char *to);
int renameat(int oldfd, const char *old, int newfd, const char *new);
int utimensat(int dir_fd, char *path, struct timespec *times, int flags);
int fhlink(struct fhandle *fhp, int tofd, const char *to);
int fhreadlink(struct fhandle *fhp, char *buf, size_t bufsize);
#endif				/* SYS_openat */

#endif				/* _SYSCALLS_FREEBSD_H */
