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
 * @brief Convert a struct stat from Ceph to a struct attrlist
 *
 * This function writes the content of the supplied struct stat to the
 * struct fsalsattr.
 *
 * @param[in]  buffstat Stat structure
 * @param[out] fsalattr FSAL attributes
 */

void ceph2fsal_attributes(const struct stat *buffstat,
			    struct attrlist *fsalattr)
{
	FSAL_CLEAR_MASK(fsalattr->mask);

	/* Fills the output struct */
	fsalattr->type = posix2fsal_type(buffstat->st_mode);
	FSAL_SET_MASK(fsalattr->mask, ATTR_TYPE);

	fsalattr->filesize = buffstat->st_size;
	FSAL_SET_MASK(fsalattr->mask, ATTR_SIZE);

	fsalattr->fsid = posix2fsal_fsid(buffstat->st_dev);
	FSAL_SET_MASK(fsalattr->mask, ATTR_FSID);

	fsalattr->fileid = buffstat->st_ino;
	FSAL_SET_MASK(fsalattr->mask, ATTR_FILEID);

	fsalattr->mode = unix2fsal_mode(buffstat->st_mode);
	FSAL_SET_MASK(fsalattr->mask, ATTR_MODE);

	fsalattr->numlinks = buffstat->st_nlink;
	FSAL_SET_MASK(fsalattr->mask, ATTR_NUMLINKS);

	fsalattr->owner = buffstat->st_uid;
	FSAL_SET_MASK(fsalattr->mask, ATTR_OWNER);

	fsalattr->group = buffstat->st_gid;
	FSAL_SET_MASK(fsalattr->mask, ATTR_GROUP);

	fsalattr->atime = posix2fsal_time(buffstat->st_atime, 0);
	FSAL_SET_MASK(fsalattr->mask, ATTR_ATIME);

	fsalattr->ctime = posix2fsal_time(buffstat->st_ctime, 0);
	FSAL_SET_MASK(fsalattr->mask, ATTR_CTIME);

	fsalattr->mtime = posix2fsal_time(buffstat->st_mtime, 0);
	FSAL_SET_MASK(fsalattr->mask, ATTR_MTIME);

	fsalattr->chgtime =
	    posix2fsal_time(MAX(buffstat->st_mtime, buffstat->st_ctime), 0);
	fsalattr->change = fsalattr->chgtime.tv_sec;
	FSAL_SET_MASK(fsalattr->mask, ATTR_CHGTIME);

	fsalattr->spaceused = buffstat->st_blocks * S_BLKSIZE;
	FSAL_SET_MASK(fsalattr->mask, ATTR_SPACEUSED);

	fsalattr->rawdev = posix2fsal_devt(buffstat->st_rdev);
	FSAL_SET_MASK(fsalattr->mask, ATTR_RAWDEV);
}

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

int construct_handle(const struct stat *st, struct Inode *i,
		     struct export *export, struct handle **obj)
{
	/* Pointer to the handle under construction */
	struct handle *constructing = NULL;

	assert(i);
	*obj = NULL;

	constructing = gsh_calloc(1, sizeof(struct handle));
	if (constructing == NULL)
		return -ENOMEM;

	constructing->vi.ino.val = st->st_ino;
#ifdef CEPH_NOSNAP
	constructing->vi.snapid.val = st->st_dev;
#endif /* CEPH_NOSNAP */
	constructing->i = i;
	constructing->up_ops = export->export.up_ops;

	ceph2fsal_attributes(st, &constructing->handle.attributes);

	fsal_obj_handle_init(&constructing->handle, &export->export,
			     constructing->handle.attributes.type);
	handle_ops_init(&constructing->handle.obj_ops);

	constructing->export = export;

	*obj = constructing;

	return 0;
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
