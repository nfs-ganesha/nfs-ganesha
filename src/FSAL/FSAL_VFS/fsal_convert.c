/*
 * vim:noexpandtab:shiftwidth=4:tabstop=4:
 */

/**
 * @file  FSAL_VFS/fsal_convert.c
 * @brief VFS-FSAL type translation functions.
 */
#include "config.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

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

	/* Use full timer resolution */
#ifdef LINUX
	fsalattr->atime = buffstat->st_atim;
	fsalattr->ctime = buffstat->st_ctim;
	fsalattr->mtime = buffstat->st_mtim;
	fsalattr->chgtime =
	    (gsh_time_cmp(&buffstat->st_mtim, &buffstat->st_ctim) >
	     0) ? fsalattr->mtime : fsalattr->ctime;
#elif FREEBSD
	fsalattr->atime = buffstat->st_atimespec;
	fsalattr->ctime = buffstat->st_ctimespec;
	fsalattr->mtime = buffstat->st_mtimespec;
	fsalattr->chgtime =
	    (gsh_time_cmp(&buffstat->st_mtimespec, &buffstat->st_ctimespec) >
	     0) ? fsalattr->mtime : fsalattr->ctime;
#endif
	FSAL_SET_MASK(fsalattr->mask, ATTR_ATIME);
	FSAL_SET_MASK(fsalattr->mask, ATTR_CTIME);
	FSAL_SET_MASK(fsalattr->mask, ATTR_MTIME);

	fsalattr->change = timespec_to_nsecs(&fsalattr->chgtime);
	FSAL_SET_MASK(fsalattr->mask, ATTR_CHGTIME);

	fsalattr->spaceused = buffstat->st_blocks * S_BLKSIZE;
	FSAL_SET_MASK(fsalattr->mask, ATTR_SPACEUSED);

	fsalattr->rawdev = posix2fsal_devt(buffstat->st_rdev);
	FSAL_SET_MASK(fsalattr->mask, ATTR_RAWDEV);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
