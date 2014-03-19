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

/**
 * posix2fsal_error :
 * Convert POSIX error codes to FSAL error codes.
 *
 * \param posix_errorcode (input):
 *        The error code returned from POSIX.
 *
 * \return The FSAL error code associated
 *         to posix_errorcode.
 *
 */
int posix2fsal_error(int posix_errorcode)
{

	switch (posix_errorcode) {
	case 0:
		return ERR_FSAL_NO_ERROR;

	case EPERM:
		return ERR_FSAL_PERM;

	case ENOENT:
		return ERR_FSAL_NOENT;

		/* connection error */
#ifdef _AIX_5
	case ENOCONNECT:
#elif defined _LINUX
	case ECONNREFUSED:
	case ECONNABORTED:
	case ECONNRESET:
#endif

		/* IO error */
	case EIO:

		/* too many open files */
	case ENFILE:
	case EMFILE:

		/* broken pipe */
	case EPIPE:

		/* all shown as IO errors */
		return ERR_FSAL_IO;

		/* no such device */
	case ENODEV:
	case ENXIO:
		return ERR_FSAL_NXIO;

		/* invalid file descriptor : */
	case EBADF:
		/* we suppose it was not opened... */

      /**
       * @todo: The EBADF error also happens when file
       *        is opened for reading, and we try writting in it.
       *        In this case, we return ERR_FSAL_NOT_OPENED,
       *        but it doesn't seems to be a correct error translation.
       */

		return ERR_FSAL_NOT_OPENED;

	case ENOMEM:
	case ENOLCK:
		return ERR_FSAL_NOMEM;

	case EACCES:
		return ERR_FSAL_ACCESS;

	case EFAULT:
		return ERR_FSAL_FAULT;

	case EEXIST:
		return ERR_FSAL_EXIST;

	case EXDEV:
		return ERR_FSAL_XDEV;

	case ENOTDIR:
		return ERR_FSAL_NOTDIR;

	case EISDIR:
		return ERR_FSAL_ISDIR;

	case EINVAL:
		return ERR_FSAL_INVAL;

	case EFBIG:
		return ERR_FSAL_FBIG;

	case ENOSPC:
		return ERR_FSAL_NOSPC;

	case EMLINK:
		return ERR_FSAL_MLINK;

	case EDQUOT:
		return ERR_FSAL_DQUOT;

	case ENAMETOOLONG:
		return ERR_FSAL_NAMETOOLONG;

/**
 * @warning
 * AIX returns EEXIST where BSD uses ENOTEMPTY;
 * We want ENOTEMPTY to be interpreted anyway on AIX plateforms.
 * Thus, we explicitely write its value (87).
 */
#ifdef _AIX
	case 87:
#else
	case ENOTEMPTY:
	case -ENOTEMPTY:
#endif
		return ERR_FSAL_NOTEMPTY;

	case ESTALE:
		return ERR_FSAL_STALE;

		/* Error code that needs a retry */
	case EAGAIN:
	case EBUSY:

		return ERR_FSAL_DELAY;

	case ENOTSUP:
		return ERR_FSAL_NOTSUPP;

	case EOVERFLOW:
		return ERR_FSAL_OVERFLOW;

	case EDEADLK:
		return ERR_FSAL_DEADLOCK;

	case EINTR:
		return ERR_FSAL_INTERRUPT;

	case EROFS:
		return ERR_FSAL_ROFS;

	case ESRCH:		/* Returned by quotaclt */
		return ERR_FSAL_NO_QUOTA;

	default:

		/* other unexpected errors */
		return ERR_FSAL_SERVERFAULT;

	}

}

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
