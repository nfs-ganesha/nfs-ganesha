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

/* This is large enough for PanFS file handles embedded in a BSD fhandle */
#define VFS_HANDLE_LEN 48

/* A file system with handles smaller than this would be surprising */
#define VFS_HANDLE_MIN_INTERNAL 4

/*
 * The vfs_file_handle_t is similar to the Linux struct file_handle,
 * except the handle[] array is fixed size in this definition.
 * The BSD struct fhandle is a bit different (of course).
 * So the Linux code will typecast all of vfs_file_handle_t to
 * a struct file_handle, while the BSD code will cast the
 * handle subfield to struct fhandle.
 */

typedef struct vfs_file_handle {
	unsigned int handle_bytes;
	int handle_type;
	unsigned char handle[VFS_HANDLE_LEN];
} vfs_file_handle_t;

#define VFS_FILE_HANDLE_MIN \
	(offsetof(vfs_file_handle_t, handle) + VFS_HANDLE_MIN_INTERNAL)

#define vfs_file_handle_size(fh) \
	(offsetof(vfs_file_handle_t, handle) + fh->handle_bytes)

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
