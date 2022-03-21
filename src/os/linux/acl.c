// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright: DataDirect Networks, 2022
 * Author: Martin Schwenke <mschwenke@ddn.com>
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
 */

/**
 * @file    os/linux/acl.c
 * @brief   Non-standard POSIX ACL functions
 */

#include <errno.h>
#include <stdio.h>

#include "os/acl.h"

#ifndef HAVE_ACL_GET_FD_NP
/*
 * @brief Get POSIX ACL, including default ACL, via file descriptor
 *
 * @param[in]  fd	File descriptor
 * @param[in]  type	ACL type - ACL_TYPE_ACCESS, ACL_TYPE_DEFAULT
 *
 * @return Posix ACL on success, NULL on failure.
 */

acl_t
acl_get_fd_np(int fd, acl_type_t type)
{
	char fname[PATH_MAX];
	int num;

	if (type == ACL_TYPE_ACCESS)
		return acl_get_fd(fd);

	if (fd < 0) {
		errno = EINVAL;
		return NULL;
	}

	num = snprintf(fname, sizeof(fname), "/proc/self/fd/%d", fd);
	if (num >= sizeof(fname)) {
		errno = EINVAL;
		return NULL;
	}

	return acl_get_file(fname, type);
}
#endif

#ifndef HAVE_ACL_SET_FD_NP
/*
 * @brief Set POSIX ACL, including default ACL, via file descriptor
 *
 * @param[in]  fd	Extented attribute
 * @param[in]  acl	Posix ACL
 * @param[in]  type	ACL type - ACL_TYPE_ACCESS, ACL_TYPE_DEFAULT
 *
 * @return 0 on success, non-zero on failure, with errno set.
 */

int
acl_set_fd_np(int fd, acl_t acl, acl_type_t type)
{
	char fname[PATH_MAX];
	int num;

	if (type == ACL_TYPE_ACCESS)
		return acl_set_fd(fd, acl);

	if (fd < 0) {
		errno = EINVAL;
		return -1;
	}

	num = snprintf(fname, sizeof(fname), "/proc/self/fd/%d", fd);
	if (num >= sizeof(fname)) {
		errno = EINVAL;
		return -1;
	}

	return acl_set_file(fname, type, acl);
}
#endif
