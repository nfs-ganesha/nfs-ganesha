/*
 * contributeur : Sachin Bhamare   sbhamare@panasas.com
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
 * @file os/xattr.h
 * @author Sachin Bhamare <sbhamare@panasas.com>
 * @brief Platform dependant utils for xattr support
 */

#ifndef _XATTR_OS_H
#define _XATTR_OS_H

#ifdef LINUX
#include <sys/xattr.h>
#elif FREEBSD
#include <os/freebsd/xattr.h>
#endif

#endif				/* _XATTR_OS_H */
