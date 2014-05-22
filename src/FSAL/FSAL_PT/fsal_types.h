/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_types.h
 * Description: FSAL common types declarations
 * Author:      FSI IPC dev team
 * ----------------------------------------------------------------------------
 */
/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * FS relative includes
 */

#include "config_parsing.h"
#include "fsal_types.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif				/* _GNU_SOURCE */

#ifndef _ATFILE_SOURCE
#define _ATFILE_SOURCE
#endif				/* _ATFILE_SOURCE */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "fsi_ipc_ccl.h"

/* -------------------------------------------
 *      POSIX FS dependant definitions
 * ------------------------------------------- */

#define OPENHANDLE_KEY_LEN 28
#define OPENHANDLE_VERSION 1

struct file_handle {
	u_int32_t handle_size;
	u_int32_t handle_type;
	u_int16_t handle_version;
	u_int16_t handle_key_size;
	/* file identifier */
	unsigned char f_handle[FSI_CCL_PERSISTENT_HANDLE_N_BYTES];
};

/** end of open by handle structures */

/* Allow aliasing of fsal_handle_t since FSALs will be
 * casting between pointer types
 */
typedef struct {
	struct {
		struct file_handle handle;
	} data;
} __attribute__ ((__may_alias__)) ptfsal_handle_t; /**< FS object handle */

/** Authentification context.    */

static inline size_t pt_sizeof_handle(ptfsal_handle_t *fh)
{
	return sizeof(ptfsal_handle_t);
}

/*
 * PT internal export
 */

struct pt_fsal_export {
	struct fsal_export export;
	uint64_t pt_export_id;	/* This is PT side FS export ID */
};

/**< directory cookie */
typedef union {
	struct {
		off_t cookie;
	} data;
#ifdef _BUILD_SHARED_FSAL
	char pad[FSAL_COOKIE_T_SIZE];
#endif
} ptfsal_cookie_t;

/* Directory stream descriptor. */

typedef struct {
	int fd;
	/* credential for accessing the directory */
	const struct req_op_context *context;
	char path[PATH_MAX];
	unsigned int dir_offset;
	ptfsal_handle_t *handle;
} ptfsal_dir_t;

typedef struct {
	int fd;
	int ro;                 /* read only file ? */
	uint64_t export_id;     /*  export id */
	uint64_t uid;           /* user id of the connecting user */
	uint64_t gid;           /* group id of the connecting user */
} ptfsal_file_t;

/* A set of buffers to retrieve multiple attributes at the same time. */
typedef struct fsal_xstat__ {
	int attr_valid;
	struct stat buffstat;
} ptfsal_xstat_t;

#endif				/* _FSAL_TYPES__SPECIFIC_H */
