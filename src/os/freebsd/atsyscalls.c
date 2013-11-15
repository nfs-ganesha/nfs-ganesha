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
 * @file    atsyscalls.c
 * @brief   platform dependent syscalls
 */

#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include "syscalls.h"

#if __FreeBSD_cc_version  >= 800001
/* Fllowing syscalls are not yet implemented in vanilla FreeBSD kernels  */
int getfhat(int fd, const char *path, fhandle_t *fhp)
{
	/* currently this is only a stub untill we implement getfhat() */
	/* in FreeBSD kernel */
	return ENOTSUP;
}

int fhlink(struct fhandle *fhp, int tofd, const char *to)
{
	return ENOTSUP;
}

int fhreadlink(struct fhandle *fhp, char *buf, size_t bufsize)
{
	return ENOTSUP;
}

#endif

#ifndef SYS_openat
/*
 *  * Allow compliation (only) on FreeBSD versions without these syscalls
 *   * These numbers match the modified FreeBSD 7.2 used by Panasas
 *    */

#define SYS_faccessat   512
#define SYS_fchmodat    513
#define SYS_fchownat    514
#define SYS_fstatat     515
#define SYS_futimesat   516
#define SYS_linkat      517
#define SYS_mkdirat     518
#define SYS_mkfifoat    519
#define SYS_mknodat     520
#define SYS_openat      521
#define SYS_readlinkat  522
#define SYS_renameat    523
#define SYS_symlinkat   524
#define SYS_unlinkat    525
#define SYS_getfhat     526
#define SYS_fhlink      527
#define SYS_fhreadlink  528

int openat(int dir_fd, const char *file, int oflag, mode_t mode)
{
	return syscall(SYS_openat, dir_fd, file, oflag, mode);
}

int mkdirat(int dir_fd, const char *file, mode_t mode)
{
	return syscall(SYS_mkdirat, dir_fd, file, mode);
}

int mknodat(int dir_fd, const char *file, mode_t mode, dev_t dev)
{
	return syscall(SYS_mknodat, dir_fd, file, mode, dev);
}

int fchownat(int dir_fd, const char *file, uid_t owner, gid_t group, int flag)
{
	return syscall(SYS_fchownat, dir_fd, file, owner, group, flag);
}

int futimesat(int dir_fd, char *filename, struct timeval *utimes)
{
	return syscall(SYS_futimesat, dir_fd, filename, utimes);
}

int fstatat(int dir_fd, const char *file, struct stat *st, int flag)
{
	return syscall(SYS_fstatat, dir_fd, file, st, flag);
}

int unlinkat(int dir_fd, const char *file, int flag)
{
	return syscall(SYS_unlinkat, dir_fd, file, flag);
}

int renameat(int oldfd, const char *old, int newfd, const char *new)
{
	return syscall(SYS_renameat, oldfd, old, newfd, new);
}

int linkat(int fromfd, const char *from, int tofd, const char *to, int flags)
{
	return syscall(SYS_linkat, fromfd, from, tofd, to, flags);
}

int symlinkat(const char *from, int tofd, const char *to)
{
	return syscall(SYS_symlinkat, from, tofd, to);
}

int readlinkat(int fd, const char *path, char *buf, size_t len)
{
	return syscall(SYS_readlinkat, fd, path, buf, len);
}

int fchmodat(int dir_fd, const char *filename, mode_t mode, int flags)
{
	return syscall(SYS_fchmodat, dir_fd, filename, mode, flags);
}

int faccessat(int dir_fd, char *filename, int mode, int flags)
{
	return syscall(SYS_faccessat, dir_fd, filename, mode, flags);
}

int getfhat(int dir_fd, char *fname, struct fhandle *fhp, int flag)
{
	return syscall(SYS_getfhat, dir_fd, fname, fhp, flag);
}

int fhlink(struct fhandle *fhp, int tofd, const char *to)
{
	return syscall(SYS_fhlink, fhp, tofd, to);
}

int fhreadlink(struct fhandle *fhp, char *buf, size_t bufsize)
{
	return syscall(SYS_fhreadlink, fhp, buf, bufsize);
}

#endif				/* SYS_openat */
