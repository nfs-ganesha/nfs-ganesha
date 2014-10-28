/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file  FSAL/fsal_convert.c
 * @brief FSAL type translation functions.
 */

#include "config.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/resource.h>

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
	struct rlimit rlim = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY
	};

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
		if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
			LogInfo(COMPONENT_FSAL,
				"Mapping %d to ERR_FSAL_IO, open_fd_count=%ld getrlimit failed",
				posix_errorcode,
				(long) atomic_fetch_size_t(&open_fd_count));
		} else {
			LogInfo(COMPONENT_FSAL,
				"Mapping %d to ERR_FSAL_IO, open_fd_count=%ld rlim_cur=%ld rlim_max=%ld",
				+posix_errorcode,
				(long) atomic_fetch_size_t(&open_fd_count),
				rlim.rlim_cur,
				rlim.rlim_max);
		}
		return ERR_FSAL_IO;

		/* no such device */
	case ENOTTY:
	case ENODEV:
	case ENXIO:
		LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_NXIO",
			posix_errorcode);
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
		LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_NOMEM",
			posix_errorcode);
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

	case EROFS:
		return ERR_FSAL_ROFS;

	case ETXTBSY:
		return ERR_FSAL_SHARE_DENIED;

	case EFBIG:
		return ERR_FSAL_FBIG;

	case ENOSPC:
		return ERR_FSAL_NOSPC;

	case EMLINK:
		return ERR_FSAL_MLINK;

	case EDQUOT:
		return ERR_FSAL_DQUOT;

	case ESRCH:		/* Returned by quotaclt */
		return ERR_FSAL_NO_QUOTA;

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
	case ETIME:
		LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_DELAY",
			posix_errorcode);
		return ERR_FSAL_DELAY;

	case ENOTSUP:
		return ERR_FSAL_NOTSUPP;

	case EOVERFLOW:
		return ERR_FSAL_OVERFLOW;

	case EDEADLK:
		return ERR_FSAL_DEADLOCK;

	case EINTR:
		return ERR_FSAL_INTERRUPT;

	default:
		LogCrit(COMPONENT_FSAL,
			"Mapping %d(default) to ERR_FSAL_SERVERFAULT",
			posix_errorcode);
		/* other unexpected errors */
		return ERR_FSAL_SERVERFAULT;

	}

}

/**
 * @brief Convert FSAL permission flags to Posix permission flags.
 *
 * @param[in] testperm FSAL permission flags to be tested
 *
 * @return POSIX permission flags to be tested.
 */
int fsal2posix_testperm(fsal_accessflags_t testperm)
{

	int posix_testperm = 0;

	if (testperm & FSAL_R_OK)
		posix_testperm |= R_OK;
	if (testperm & FSAL_W_OK)
		posix_testperm |= W_OK;
	if (testperm & FSAL_X_OK)
		posix_testperm |= X_OK;

	return posix_testperm;

}

/* mode bits are a uint16_t and chmod masks off type */

#define S_IALLUGO (~S_IFMT & 0xFFFF)

/**
 * @brief Convert FSAL mode to POSIX mode
 *
 * @param[in] fsal_mode FSAL mode to be translated
 *
 * @return The POSIX mode associated to fsal_mode.
 */
mode_t fsal2unix_mode(uint32_t fsal_mode)
{
	return fsal_mode & S_IALLUGO;
}

/**
 * @brief Convert POSIX mode to FSAL mode
 *
 * @param[in] unix_mode POSIX mode to be translated
 *
 * @return FSAL mode associated with @c unix_mode
 */

uint32_t unix2fsal_mode(mode_t unix_mode)
{
	return unix_mode & S_IALLUGO;
}

/**
 * @brief Convert POSIX object type to an FSAL object type
 *
 * @param[in] posix_type_in POSIX object type
 *
 * @retval The FSAL node type associated to @c posix_type_in.
 * @retval -1 if the input type is unknown.
 */

object_file_type_t posix2fsal_type(mode_t posix_type_in)
{

	switch (posix_type_in & S_IFMT) {
	case S_IFIFO:
		return FIFO_FILE;

	case S_IFCHR:
		return CHARACTER_FILE;

	case S_IFDIR:
		return DIRECTORY;

	case S_IFBLK:
		return BLOCK_FILE;

	case S_IFREG:
	case S_IFMT:
		return REGULAR_FILE;

	case S_IFLNK:
		return SYMBOLIC_LINK;

	case S_IFSOCK:
		return SOCKET_FILE;

	default:
		LogWarn(COMPONENT_FSAL, "Unknown object type: %d",
			posix_type_in);
		return -1;
	}

}

/**
 * @brief Convert a stat(2) style dev_t to an FSAL fsid
 *
 * @param[in] posix_devid The device id
 *
 * @return The FSAL fsid.
 */

fsal_fsid_t posix2fsal_fsid(dev_t posix_devid)
{

	fsal_fsid_t fsid;

	fsid.major = major(posix_devid);
	fsid.minor = minor(posix_devid);

	return fsid;

}

/**
 * @brief Convert a stat(2) style dev_t to an fsal_dev_t
 *
 * @param[in] posix_devid The device id
 *
 * @return The FSAL device.
 */

fsal_dev_t posix2fsal_devt(dev_t posix_devid)
{

	fsal_dev_t dev;

	dev.major = major(posix_devid);
	dev.minor = minor(posix_devid);

	return dev;
}

/**
 * @brief Convert FSAL open flags to POSIX open flags
 *
 * @param[in]  fsal_flags    FSAL open flags to be translated
 * @param[out] p_posix_flags POSIX open flags.
 *
 * @retval ERR_FSAL_NO_ERROR, no error.
 * @retval ERR_FSAL_INVAL, invalid or incompatible input flags.
 */

int fsal2posix_openflags(fsal_openflags_t fsal_flags, int *p_posix_flags)
{
	/* check that all used flags exist */
	if (fsal_flags &
	    ~(FSAL_O_READ | FSAL_O_RDWR | FSAL_O_WRITE | FSAL_O_SYNC))
		return ERR_FSAL_INVAL;

	/* conversion */
	*p_posix_flags = 0;

	if ((fsal_flags & FSAL_O_RDWR) == FSAL_O_RDWR)
		*p_posix_flags |= O_RDWR;
	else if ((fsal_flags & FSAL_O_RDWR) == FSAL_O_READ)
		*p_posix_flags |= O_RDONLY;
	else if ((fsal_flags & FSAL_O_RDWR) == FSAL_O_WRITE)
		*p_posix_flags |= O_WRONLY;

	if (fsal_flags & FSAL_O_SYNC)
		*p_posix_flags |= O_SYNC;

	return ERR_FSAL_NO_ERROR;
}

/**
 * @brief Return string for object type
 *
 * @param[in] type The FSAL object type
 *
 * @return A string naming the type or "unexpected type".
 */

const char *object_file_type_to_str(object_file_type_t type)
{
	switch (type) {
	case NO_FILE_TYPE:
		return "NO_FILE_TYPE";
	case REGULAR_FILE:
		return "REGULAR_FILE";
	case CHARACTER_FILE:
		return "CHARACTER_FILE";
	case BLOCK_FILE:
		return "BLOCK_FILE";
	case SYMBOLIC_LINK:
		return "SYMBOLIC_LINK";
	case SOCKET_FILE:
		return "SOCKET_FILE";
	case FIFO_FILE:
		return "FIFO_FILE";
	case DIRECTORY:
		return "DIRECTORY";
	case EXTENDED_ATTR:
		return "EXTENDED_ATTR";
	}
	return "unexpected type";
}

fsal_status_t posix2fsal_attributes(const struct stat *buffstat,
				    struct attrlist *fsalattr)
{
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

/** @} */
