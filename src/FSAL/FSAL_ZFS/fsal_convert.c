/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_convert.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \brief   FS-FSAL type translation functions.
 *
 */
#include "config.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>

/* THOSE FUNCTIONS CAN BE USED FROM OUTSIDE THE MODULE : */

fsal_status_t posix2fsal_attributes(const struct stat *buffstat,
				    struct attrlist *fsalattr)
{
	FSAL_CLEAR_MASK(fsalattr->mask);
	/* sanity checks */
	if (!buffstat || !fsalattr)
		return fsalstat(ERR_FSAL_FAULT, 0);

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

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
