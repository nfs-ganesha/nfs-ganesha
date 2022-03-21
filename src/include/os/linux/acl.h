/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 * @file    os/linux/acl.h
 * @brief   Non-standard POSIX ACL functions
 */

#ifndef _ACL_LINUX_H
#define _ACL_LINUX_H

#include "config.h"

#include <limits.h>
#include <sys/types.h>
#include <sys/acl.h>

#ifndef HAVE_ACL_GET_FD_NP
acl_t
acl_get_fd_np(int fd, acl_type_t type);
#endif

#ifndef HAVE_ACL_SET_FD_NP
int
acl_set_fd_np(int fd, acl_t acl, acl_type_t type);
#endif

#endif /* _ACL_LINUX_H */
