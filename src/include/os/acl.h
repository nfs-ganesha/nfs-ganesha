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
 * @file    os/acl.h
 * @brief   Non-standard POSIX ACL functions
 */

#ifndef _ACL_OS_H
#define _ACL_OS_H

#include "config.h"

#ifdef LINUX
#include <os/linux/acl.h>
#endif

#endif /* _ACL_OS_H */
