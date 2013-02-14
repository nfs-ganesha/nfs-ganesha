/*
 *
 * contributeur : Sachin Bhamare   sbhamare@panasas.com
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    subr.c
 * \author  $Author: Sachin Bhamare $
 * \version $Revision: 1.0 $
 * \brief   platform dependant subroutines for Linux
 *
 */

#include "fsal.h"
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "os/subr.h"

/* not defined in linux headers so we do it here
 */
struct linux_dirent {
	unsigned long  d_ino;     /* Inode number */
	unsigned long  d_off;     /* Offset to next linux_dirent */
	unsigned short d_reclen;  /* Length of this linux_dirent */
	char           d_name[];  /* Filename (null-terminated) */
	/* length is actually (d_reclen - 2 -
	 * offsetof(struct linux_dirent, d_name)
	 */
	/*
	  char           pad;       // Zero padding byte
	  char           d_type;    // File type (only since Linux 2.6.4;
	  // offset is (d_reclen - 1))
	  */
};

/**
 * @brief Read system directory entries into the buffer
 *
 * @param buf    [in] pointer to the buffer
 * @param bcount [in] buffer size
 * @param basepp [in/out] pointer to offset into "file" after this read
 */

int vfs_readents(int fd, char *buf, unsigned int bcount, off_t *basepp)
{
	int retval = 0;

	retval = syscall(SYS_getdents, fd, buf, bcount);
	if(retval >= 0)
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
	struct linux_dirent *dp = (struct linux_dirent *)(buf + bpos);
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
 * @brief Platform specific wrapper fro utimensat().
 *
 * @param fd       [in] File descriptor
 * @param path     [in] name of a file
 * @param timespec [in] pointer to array of struct timespec
 * @param flags    [in] flags
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
 * @param fd       [in] File descriptor
 * @param timespec [in] pointer to array of struct timespec
 *
 * @return 0 on success, -1 on error (errno set to indicate the error).
 */
int vfs_utimes(int fd, const struct timespec *ts)
{
	return futimens(fd, ts);
}
