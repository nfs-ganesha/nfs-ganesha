/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file fsal_errors.c
 * @brief Routines for handling errors.
 */

#include "fsal.h"

/**
 * @brief FSAL error code to error message
 *
 * @param[in] fsal_err Error code
 *
 * @return Error message, empty string if not found.
 */

const char *msg_fsal_err(fsal_errors_t fsal_err)
{
	switch (fsal_err) {
	case ERR_FSAL_NO_ERROR:
		return "No error";
	case ERR_FSAL_PERM:
		return "Forbidden action";
	case ERR_FSAL_NOENT:
		return "No such file or directory";
	case ERR_FSAL_IO:
		return "I/O error";
	case ERR_FSAL_NXIO:
		return "No such device or address";
	case ERR_FSAL_NOMEM:
		return "Not enough memory";
	case ERR_FSAL_ACCESS:
		return "Permission denied";
	case ERR_FSAL_FAULT:
		return "Bad address";
	case ERR_FSAL_EXIST:
		return "This object already exists";
	case ERR_FSAL_XDEV:
		return "This operation can't cross filesystems";
	case ERR_FSAL_NOTDIR:
		return "This object is not a directory";
	case ERR_FSAL_ISDIR:
		return "Directory used in a nondirectory operation";
	case ERR_FSAL_INVAL:
		return "Invalid object type";
	case ERR_FSAL_FBIG:
		return "File exceeds max file size";
	case ERR_FSAL_NOSPC:
		return "No space left on filesystem";
	case ERR_FSAL_ROFS:
		return "Read-only filesystem";
	case ERR_FSAL_MLINK:
		return "Too many hard links";
	case ERR_FSAL_DQUOT:
		return "Quota exceeded";
	case ERR_FSAL_NAMETOOLONG:
		return "Max name length exceeded";
	case ERR_FSAL_NOTEMPTY:
		return "The directory is not empty";
	case ERR_FSAL_STALE:
		return "The file no longer exists";
	case ERR_FSAL_BADHANDLE:
		return "Illegal filehandle";
	case ERR_FSAL_BADCOOKIE:
		return "Invalid cookie";
	case ERR_FSAL_NOTSUPP:
		return "Operation not supported";
	case ERR_FSAL_TOOSMALL:
		return "Output buffer too small";
	case ERR_FSAL_SERVERFAULT:
		return "Undefined server error";
	case ERR_FSAL_BADTYPE:
		return "Invalid type for create operation";
	case ERR_FSAL_DELAY:
		return "File busy, retry";
	case ERR_FSAL_FHEXPIRED:
		return "Filehandle expired";
	case ERR_FSAL_SYMLINK:
		return "This is a symbolic link, should be file/directory";
	case ERR_FSAL_ATTRNOTSUPP:
		return "Attribute not supported";
	case ERR_FSAL_NOT_INIT:
		return "Filesystem not initialized";
	case ERR_FSAL_ALREADY_INIT:
		return "Filesystem already initialised";
	case ERR_FSAL_BAD_INIT:
		return "Filesystem initialisation error";
	case ERR_FSAL_SEC:
		return "Security context error";
	case ERR_FSAL_NO_QUOTA:
		return "No Quota available";
	case ERR_FSAL_NOT_OPENED:
		return "File/directory not opened";
	case ERR_FSAL_DEADLOCK:
		return "Deadlock";
	case ERR_FSAL_OVERFLOW:
		return "Overflow";
	case ERR_FSAL_INTERRUPT:
		return "Operation Interrupted";
	case ERR_FSAL_BLOCKED:
		return "Lock Blocked";
	case ERR_FSAL_SHARE_DENIED:
		return "Share Denied";
	case ERR_FSAL_TIMEOUT:
		return "Timeout";
	case ERR_FSAL_FILE_OPEN:
		return "File Open";
	case ERR_FSAL_UNION_NOTSUPP:
		return "Union Not Supported";
	}

	return "Unknown FSAL error";
}

/** @} */
