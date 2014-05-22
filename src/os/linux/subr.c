/*
 * contributeur : Sachin Bhamare   sbhamare@panasas.com
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
 * ---------------------------------------
 */

/**
 * @file os/linux/subr.c
 * @author Sachin Bhamare <sbhamare@panasas.com>
 * @brief Platform dependant subroutines for Linux
 *
 */

#include "fsal.h"
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/fsuid.h>
#include <sys/syscall.h>
#include "os/subr.h"

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
	int retval = 0;

	retval = syscall(SYS_getdents64, fd, buf, bcount);
	if (retval >= 0)
		*basepp += retval;
	return retval;
}

/**
 * @brief Mash a FreeBSD directory entry into the generic form
 *
 * @param buf  [in] pointer into buffer read by vfs_readents
 * @param bpos [in] byte offset into buf to decode
 * @param vd   [in] pointer to the generic struct
 * @param base [in] base file offset for this buffer - not used
 *
 * @return true. Linux entries are never empty.
 */

bool to_vfs_dirent(char *buf, int bpos, struct vfs_dirent *vd, off_t base)
{
	struct dirent64 *dp = (struct dirent64 *)(buf + bpos);
	char type;

	vd->vd_ino = dp->d_ino;
	vd->vd_reclen = dp->d_reclen;
	type = buf[dp->d_reclen - 1];
	vd->vd_type = type;
	vd->vd_offset = dp->d_off;
	vd->vd_name = dp->d_name;
	return true;
}

/**
 * @brief Platform specific wrapper for utimensat().
 *
 * @param[in] fd    File descriptor
 * @param[in] path  Name of a file
 * @param[in] ts    Array of struct timespec
 * @param[in] flags Flags
 *
 * @return 0 on success, -1 on error (errno set to indicate the error).
 */
int vfs_utimesat(int fd, const char *path, const struct timespec ts[2],
		 int flags)
{
	return utimensat(fd, path, ts, flags);
}

/**
 * @brief Platform specific wrapper fro futimens().
 *
 * @param[in] fd File descriptor
 * @param[in] ts Array of struct timespec
 *
 * @return 0 on success, -1 on error (errno set to indicate the error).
 */
int vfs_utimes(int fd, const struct timespec *ts)
{
	return futimens(fd, ts);
}

uid_t setuser(uid_t uid)
{
	uid_t orig_uid = setfsuid(uid);
	if (uid != setfsuid(uid))
		LogCrit(COMPONENT_FSAL, "Could not set user identity");

	return orig_uid;
}

gid_t setgroup(gid_t gid)
{
	gid_t orig_gid = setfsgid(gid);
	if (gid != setfsgid(gid))
		LogCrit(COMPONENT_FSAL, "Could not set group identity");

	return orig_gid;
}

int set_threadgroups(size_t size, const gid_t *list)
{
	return syscall(__NR_setgroups, size, list);
}
