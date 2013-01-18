/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Sachin Bhamare sbhamare@panasas.com
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
 * -------------
 */

/**
 * \file    subr.c
 * \author  $Author: Sachin Bhamare $
 * \version $Revision: 1.0 $
 * \brief   platform dependant subroutines for FreeBSD
 *
 */

#include <string.h>
#include <os/subr.h>
#include <sys/dirent.h>

void to_vfs_dirent(char *buf, struct vfs_dirent *vd)
{
	struct dirent *dp = (struct dirent *)buf;
	vd->vd_ino = dp->d_fileno;
	vd->vd_reclen = dp->d_reclen;
	vd->vd_type = dp->d_type;
	strncpy(vd->vd_name, dp->d_name, sizeof(dp->d_name));
}
