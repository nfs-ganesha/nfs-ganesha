/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (C) 2010, Linux Box Corporation
 * All Rights Reserved
 *
 * Contributor: Matt Benjamin
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 *
 * \file gsh_intrinsic.h
 * \author Matt Benjamin
 * \brief Compiler intrinsics
 *
 * \section DESCRIPTION
 *
 * Compiler intrinsics.

 */

#ifndef _GSH_INTRINSIC_H
#define _GSH_INTRINSIC_H

#if __GLIBC__
#ifndef likely
#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)
#endif
#else
#ifndef likely
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif
#endif

#if defined(__PPC64__)
#define GSH_CACHE_LINE_SIZE 128
#else /* __x86_64__, __i386__ and others */
#define GSH_CACHE_LINE_SIZE 64
#endif
#define GSH_CACHE_PAD(_n) char __pad ## _n[GSH_CACHE_LINE_SIZE]


#endif				/* _GSH_INTRINSIC_H */
