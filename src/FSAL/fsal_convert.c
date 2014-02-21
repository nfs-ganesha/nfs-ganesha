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

#define MAX_2(x, y)    ((x) > (y) ? (x) : (y))
#define MAX_3(x, y, z) ((x) > (y) ? MAX_2((x), (z)) : MAX_2((y), (z)))

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

	fsid.major = posix_devid;
	fsid.minor = 0;

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
 * @retval ERR_FSAL_FAULT, p_posix_flags is a NULL pointer.
 * @retval ERR_FSAL_INVAL, invalid or incompatible input flags.
 */

int fsal2posix_openflags(fsal_openflags_t fsal_flags, int *p_posix_flags)
{
	if (!p_posix_flags)
		return ERR_FSAL_FAULT;

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
	case FS_JUNCTION:
		return "FS_JUNCTION";
	case EXTENDED_ATTR:
		return "EXTENDED_ATTR";
	}
	return "unexpected type";
}

/** @} */
