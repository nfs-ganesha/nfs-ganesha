/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * @file include/extended_types.h
 * @brief Extended types, platform dependant.
 *
 */

#ifndef _EXTENDED_TYPES_H
#define _EXTENDED_TYPES_H

#include "config.h"
#include <sys/types.h>

#ifdef LINUX
#include <os/linux/extended_types.h>
#elif FREEBSD
#include <os/freebsd/extended_types.h>
#endif

/* Added extended types, often missing */
typedef long long longlong_t;
typedef unsigned long long u_longlong_t;

typedef unsigned int uint_t;

/* conflict between sys/xattr.h and attr/xattr.h
 * this comes from xfs/linux.h.  Very bad form but we are
 * stuck with it.  If we didn't pick it up somewhere else
 * make is so here.
 * Danger Will Robinson!! this is an overlay of another errno...
 */

#ifndef ENOATTR
/* ENOATTR is a BSD-ism, it does not exist on Linux. ENODATA is used instead */
#define ENOATTR ENODATA		/* No such attribute */
#endif

#endif				/* _EXTENDED_TYPES_H */
