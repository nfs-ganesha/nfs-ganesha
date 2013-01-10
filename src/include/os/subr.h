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
 * \file    types.h
 * \author  $Author: Sachin Bhamare $
 * \version $Revision: 1.0 $
 * \brief   platform dependant subroutine type definitions
 *
 */

#ifndef _SUBR_OS_H
#define _SUBR_OS_H

#include <extended_types.h>

struct vfs_dirent {
	uint64_t vd_ino;
	uint32_t vd_reclen;
	uint32_t vd_type;
	char     vd_name[255];
};

void to_vfs_dirent(char *buf, struct vfs_dirent *vd);

#endif                          /* _SUBR_OS_H */
