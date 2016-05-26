/*
 * Copyright © 2012-2014, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * contributeur : William Allen Simpson <bill@cohortfs.com>
 *		  Marcus Watts <mdw@cohortfs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @file   internal.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @author William Allen Simpson <bill@cohortfs.com>
 * @date Wed Oct 22 13:24:33 2014
 *
 * @brief Internal definitions for the Ceph FSAL
 *
 * This file includes internal function definitions, constants, and
 * variable declarations used to impelment the Ceph FSAL, but not
 * exposed as part of the API.
 */

#include <sys/stat.h>
#include "fsal_types.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"

#define CEPH_INTERNAL_C
#include "internal.h"

/**
 * The attributes tis FSAL can interpret or supply.
 */

const attrmask_t supported_attributes = (
	ATTR_TYPE      | ATTR_SIZE     | ATTR_FSID  | ATTR_FILEID |
	ATTR_MODE      | ATTR_NUMLINKS | ATTR_OWNER | ATTR_GROUP  |
	ATTR_ATIME     | ATTR_RAWDEV   | ATTR_CTIME | ATTR_MTIME  |
	ATTR_SPACEUSED | ATTR_CHGTIME);

/**
 * The attributes this FSAL can set.
 */

const attrmask_t settable_attributes = (
	ATTR_MODE  | ATTR_OWNER | ATTR_GROUP | ATTR_ATIME	 |
	ATTR_CTIME | ATTR_MTIME | ATTR_SIZE  | ATTR_MTIME_SERVER |
	ATTR_ATIME_SERVER);

/**
 * @brief Construct a new filehandle
 *
 * This function constructs a new Ceph FSAL object handle and attaches
 * it to the export.  After this call the attributes have been filled
 * in and the handdle is up-to-date and usable.
 *
 * @param[in]  st     Stat data for the file
 * @param[in]  export Export on which the object lives
 * @param[out] obj    Object created
 *
 * @return 0 on success, negative error codes on failure.
 */

void construct_handle(const struct stat *st, struct Inode *i,
		      struct export *export, struct handle **obj)
{
	/* Pointer to the handle under construction */
	struct handle *constructing = NULL;

	assert(i);

	constructing = gsh_calloc(1, sizeof(struct handle));

	constructing->vi.ino.val = st->st_ino;
#ifdef CEPH_NOSNAP
	constructing->vi.snapid.val = st->st_dev;
#endif /* CEPH_NOSNAP */
	constructing->i = i;
	constructing->up_ops = export->export.up_ops;

	fsal_obj_handle_init(&constructing->handle, &export->export,
			     posix2fsal_type(st->st_mode));
	handle_ops_init(&constructing->handle.obj_ops);
	constructing->handle.fsid = posix2fsal_fsid(st->st_dev);
	constructing->handle.fileid = st->st_ino;

	constructing->export = export;

	*obj = constructing;
}

/**
 * @brief Release all resrouces for a handle
 *
 * @param[in] obj Handle to release
 */

void deconstruct_handle(struct handle *obj)
{
	ceph_ll_put(obj->export->cmount, obj->i);
	fsal_obj_handle_fini(&obj->handle);
	gsh_free(obj);
}
