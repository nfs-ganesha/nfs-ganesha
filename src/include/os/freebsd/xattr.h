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
 * @file    os/freebsd/xattr.h
 * @brief   Platform dependant utils for xattr support on FreeBSD
 *
 */

#ifndef _XATTR_FREEBSD_H
#define _XATTR_FREEBSD_H

#include <sys/errno.h>
#include <sys/types.h>

#define XATTR_CREATE  0x1
#define XATTR_REPLACE 0x2

extern ssize_t fgetxattr(int fd, const char *name, void *value, size_t size);
extern ssize_t fsetxattr(int fd, const char *name, void *value, size_t size,
			 int flags);
extern ssize_t flistxattr(int fd, const char *list, size_t size);
extern ssize_t fremovexattr(int fd, const char *name);

#endif				/* _XATTR_FREEBSD_H */
