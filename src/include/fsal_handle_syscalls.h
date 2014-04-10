/*
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file include/fsal_handle_syscalls.h
 * @brief System calls for the platform dependent handle calls
 *
 * @todo This file should be in FSAL_VFS, not in the top-level include
 * directory.
 */

#ifndef HANDLE_H
#define HANDLE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <sys/fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stddef.h>		/* For having offsetof defined */

#define VFS_HANDLE_LEN 59

typedef struct vfs_file_handle {
	uint8_t handle_len; /* does not go on the wire */
	uint8_t handle_data[VFS_HANDLE_LEN];
} vfs_file_handle_t;

static inline bool vfs_handle_invalid(struct gsh_buffdesc *desc)
{
	return desc->len > VFS_HANDLE_LEN;
}

#define vfs_alloc_handle(fh)						\
	do {								\
		(fh) = alloca(sizeof(struct vfs_file_handle));		\
		memset((fh), 0, (sizeof(struct vfs_file_handle)));	\
		(fh)->handle_len = VFS_HANDLE_LEN;			\
	} while (0)

#define vfs_malloc_handle(fh)						\
	do {								\
		(fh) = gsh_calloc(1, sizeof(struct vfs_file_handle));	\
		(fh)->handle_len = VFS_HANDLE_LEN;			\
	} while (0)

#ifdef LINUX
#include "os/linux/fsal_handle_syscalls.h"
#elif FREEBSD
#include "os/freebsd/fsal_handle_syscalls.h"
#else
#error "No by-handle syscalls defined on this platform."
#endif

#ifndef AT_FDCWD
#error "Very old kernel and/or glibc"
#endif

#endif
/** @} */
