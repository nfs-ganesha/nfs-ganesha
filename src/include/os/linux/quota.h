/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 *   Copyright (C) Panasas, Inc. 2011
 *   Author(s): Sachin Bhamare <sbhamare@panasas.com>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later
 *   version.
 *
 *   This library can be distributed with a BSD license as well, just
 *   ask.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free
 *   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *   02111-1307 USA
 */

#ifndef QUOTA_LINUX_H
#define QUOTA_LINUX_H

#include <sys/quota.h>

#define QUOTACTL(cmd, path, id, addr) \
	quotactl((cmd), path, id, addr)

#endif /* QUOTA_LINUX_H */
