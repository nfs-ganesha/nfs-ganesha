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
 * -------------
 */

/**
 * @file os/freebsd/subr.c
 * @author Sachin Bhamare <sbhamare@panasas.com>
 * @brief Platform dependant subroutines for FreeBSD
 */

#include <unistd.h>
#include <string.h>
#include <os/subr.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <log.h>
#include "syscalls.h"
#include <sys/module.h>

/**
 * @brief Read system directory entries into the buffer
 *
 * @param[in]     fd     File descriptor of open directory
 * @param[in]     buf    The buffer
 * @param[in]     bcount Buffer size
 * @param[in,out] basepp Offset into "file" after this read
 */
int vfs_readents(int fd, char *buf, unsigned int bcount, off_t *basepp)
{
	return getdirentries(fd, buf, bcount, basepp);
}

/**
 * @brief Mash a FreeBSD directory entry into the generic form
 *
 * @param[in] buf  Pointer into buffer read by vfs_readents
 * @param[in] bpos Offset into buffer
 * @param[in] vd   Pointer to the generic struct
 * @param[in] base File offset for this entry in buffer.
 *
 * @return true if entry valid, false if not (empty)
 */
bool to_vfs_dirent(char *buf, int bpos, struct vfs_dirent *vd, off_t base)
{
	struct dirent *dp = (struct dirent *)(buf + bpos);

	vd->vd_ino = dp->d_fileno;
	vd->vd_reclen = dp->d_reclen;
	vd->vd_type = dp->d_type;
	vd->vd_offset = base + bpos + dp->d_reclen;
	vd->vd_name = dp->d_name;
	return (dp->d_fileno != 0) ? true : false;
}

/**
 * @brief Following functions exist to compensate for lack of *times() APIs with
 * nanosecond granularity on FreeBSD platform. These functions also take care of
 * timespec <--> timeval conversion.
 */

#define TIMEVAL_TO_TIMESPEC(tv, ts)                                     \
	do {                                                            \
		(ts)->tv_sec = (tv)->tv_sec;                            \
		(ts)->tv_nsec = (tv)->tv_usec * 1000;                   \
	} while (0)

#define TIMESPEC_TO_TIMEVAL(tv, ts)                                     \
	do {                                                            \
		(tv)->tv_sec = (ts)->tv_sec;                            \
		(tv)->tv_usec = (ts)->tv_nsec / 1000;                   \
	} while (0)

int vfs_utimesat(int fd, const char *path, const struct timespec ts[2],
		 int flags)
{
	struct timeval tv[2];

	if (ts[0].tv_nsec == UTIME_OMIT || ts[1].tv_nsec == UTIME_OMIT) {
		/* nothing to do */
		return 0;
	} else if (ts[0].tv_nsec == UTIME_NOW || ts[1].tv_nsec == UTIME_NOW) {
		/* set to the current timestamp. achieve this by passing NULL
		 * timeval to kernel */
		return futimesat(fd, (char *)path, NULL);
	}
	TIMESPEC_TO_TIMEVAL(&tv[0], &ts[0]);
	TIMESPEC_TO_TIMEVAL(&tv[1], &ts[1]);
	return futimesat(fd, (char *)path, tv);
}

int vfs_utimes(int fd, const struct timespec *ts)
{
	struct timeval tv[2];

	if (ts[0].tv_nsec == UTIME_OMIT || ts[1].tv_nsec == UTIME_OMIT) {
		/* nothing to do */
		return 0;
	} else if (ts[0].tv_nsec == UTIME_NOW || ts[1].tv_nsec == UTIME_NOW) {
		/* set to the current timestamp. achieve this by passing NULL
		 * timeval to kernel */
		return futimes(fd, NULL);
	}
	TIMESPEC_TO_TIMEVAL(&tv[0], &ts[0]);
	TIMESPEC_TO_TIMEVAL(&tv[1], &ts[1]);
	return futimes(fd, tv);
}

static int setthreaduid(uid_t uid)
{
	int mod, err;
	int syscall_num;
	struct module_stat stat;

	stat.version = sizeof(stat);
	/* modstat will retrieve the module_stat structure for our module named
	 * syscall (see the SYSCALL_MODULE macro which sets the name to syscall)
	 */
	mod = modfind("sys/setthreaduid");
	if (mod == -1)
		return errno;

	err = modstat(mod, &stat);
	if (err)
		return errno;
	/* extract the slot (syscall) number */
	syscall_num = stat.data.intval;

	return syscall(syscall_num, uid);
}

static int setthreadgid(gid_t gid)
{
	int mod, err;
	int syscall_num;
	struct module_stat stat;

	stat.version = sizeof(stat);
	/* modstat will retrieve the module_stat structure for our module named
	* syscall (see the SYSCALL_MODULE macro which sets the name to syscall)
	*/
	mod = modfind("sys/setthreadgid");
	if (mod == -1)
		return errno;

	err = modstat(mod, &stat);
	if (err)
		return errno;

	/* extract the slot (syscall) number */
	syscall_num = stat.data.intval;

	return syscall(syscall_num, gid);
}

static int setthreadgroups(size_t size, const gid_t *list)
{
	int mod, err;
	int syscall_num;
	struct module_stat stat;

	stat.version = sizeof(stat);
	/* modstat will retrieve the module_stat structure for our module named
	* syscall (see the SYSCALL_MODULE macro which sets the name to syscall)
	*/
	mod = modfind("sys/setthreadgroups");
	if (mod == -1)
		return errno;

	err = modstat(mod, &stat);
	if (err)
		return errno;

	/* extract the slot (syscall) number */
	syscall_num = stat.data.intval;

	return syscall(syscall_num, size, list);
}

uid_t getuser(void)
{
	return geteuid();
}

gid_t getgroup(void)
{
	return getegid();
}

void setuser(uid_t uid)
{
	int rc = setthreaduid(uid);

	if (rc != 0)
		LogCrit(COMPONENT_FSAL,
			"Could not set user identity %s (%d)",
			strerror(errno), errno);
}

void setgroup(gid_t gid)
{
	int rc = setthreadgid(gid);

	if (rc != 0)
		LogCrit(COMPONENT_FSAL,
			"Could not set group identity %s (%d)",
			strerror(errno), errno);
}

int set_threadgroups(size_t size, const gid_t *list)
{
	return setthreadgroups(size, list);
}
