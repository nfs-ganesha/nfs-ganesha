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

#include <stddef.h>
#include <string.h>
#include <os/subr.h>

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

void to_vfs_dirent(char *buf, struct vfs_dirent *vd)
{
	struct linux_dirent *dp = (struct linux_dirent *)buf;
	char type;

	vd->vd_ino = dp->d_ino;
	vd->vd_reclen = dp->d_reclen;
	type = buf[dp->d_reclen - 1];
	vd->vd_type = type;
	strncpy(vd->vd_name, dp->d_name,
		dp->d_reclen - 2 - offsetof(struct linux_dirent, d_name));
}
