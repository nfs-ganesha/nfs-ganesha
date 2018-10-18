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

/* xattr.c
 * VFS FSAL xattr support on FreeBSD platform
 */

#include <os/freebsd/xattr.h>
#include <limits.h>
#include <sys/extattr.h>

#ifndef EXTATTR_MAXNAMELEN
#define EXTATTR_MAXNAMELEN 255
#endif

ssize_t fgetxattr(int fd, const char *name, void *value, size_t size)
{
	return extattr_get_fd(fd, EXTATTR_NAMESPACE_SYSTEM, name, value, size);
}

ssize_t fsetxattr(int fd, const char *name, void *value, size_t size, int flags)
{
	char buff[EXTATTR_MAXNAMELEN];
	ssize_t attr_size = 0;

	errno = 0;

	attr_size =
	    extattr_get_fd(fd, EXTATTR_NAMESPACE_SYSTEM, name, buff, size);
	if (attr_size != size && errno == ENOATTR) {
		/* attr we are trying to set doesn't exist. check if
		 * XATTR_REPLACE was set */
		if (flags & XATTR_REPLACE)
			return ENOATTR;
	} else {
		if (flags & XATTR_CREATE)
			return EEXIST;
	}
	return extattr_set_fd(fd, EXTATTR_NAMESPACE_SYSTEM, name, value, size);
}

ssize_t flistxattr(int fd, const char *list, size_t size)
{
	return extattr_list_fd(fd, EXTATTR_NAMESPACE_SYSTEM, (void *)list,
			       size);
}

ssize_t fremovexattr(int fd, const char *name)
{
	return extattr_delete_fd(fd, EXTATTR_NAMESPACE_SYSTEM, name);
}
