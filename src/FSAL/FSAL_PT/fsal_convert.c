// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_convert.c
// Description: FSAL convert operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_convert.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pt_ganesha.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include "nfs4_acls.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define MAX_2( x, y )    ( (x) > (y) ? (x) : (y) )
#define MAX_3( x, y, z ) ( (x) > (y) ? MAX_2((x),(z)) : MAX_2((y),(z)) )

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
		LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_NOTEMPTY",
			posix_errorcode);
		return ERR_FSAL_NOTEMPTY;

	case ESTALE:
		return ERR_FSAL_STALE;

		/* Error code that needs a retry */
	case EAGAIN:
	case EBUSY:
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
 * fsal2posix_openflags:
 * Convert FSAL open flags to Posix open flags.
 *
 * \param fsal_flags (input):
 *        The FSAL open flags to be translated.
 * \param p_hpss_flags (output):
 *        Pointer to the POSIX open flags.
 *
 * \return - ERR_FSAL_NO_ERROR (no error).
 *         - ERR_FSAL_FAULT    (p_hpss_flags is a NULL pointer).
 *         - ERR_FSAL_INVAL    (invalid or incompatible input flags).
 */
int fsal2posix_openflags(fsal_openflags_t fsal_flags, int *p_posix_flags)
{
	int cpt;

	if (!p_posix_flags)
		return ERR_FSAL_FAULT;

	/* check that all used flags exist */

	if (fsal_flags &
	    ~(FSAL_O_READ | FSAL_O_RDWR | FSAL_O_WRITE | FSAL_O_SYNC)) {
		return ERR_FSAL_INVAL;
	}

	/* Check for flags compatibility */

	/* O_RDONLY O_WRONLY O_RDWR cannot be used together */

	cpt = 0;
	if (fsal_flags & O_RDONLY)
		cpt++;
	if (fsal_flags & O_RDWR)
		cpt++;
	if (fsal_flags & O_WRONLY)
		cpt++;

	if (cpt > 1)
		return ERR_FSAL_INVAL;

	/* O_APPEND et O_TRUNC cannot be used together */

	if ((fsal_flags & O_APPEND) && (fsal_flags & O_TRUNC))
		return ERR_FSAL_INVAL;

	/* O_TRUNC without O_WRONLY or O_RDWR */

	if ((fsal_flags & O_TRUNC)
	    && !(fsal_flags & (O_WRONLY | O_RDWR))) {
		return ERR_FSAL_INVAL;
	}

	/* conversion */
	*p_posix_flags = 0;

	if (fsal_flags & O_RDONLY)
		*p_posix_flags |= O_RDONLY;
	if (fsal_flags & O_WRONLY)
		*p_posix_flags |= O_WRONLY;
	if (fsal_flags & O_RDWR)
		*p_posix_flags |= O_RDWR;
	if (fsal_flags & O_SYNC)
		*p_posix_flags |= O_SYNC;

	return ERR_FSAL_NO_ERROR;

}

fsal_status_t posix2fsal_attributes(struct stat * p_buffstat,
				    struct attrlist * p_fsalattr_out)
{

	FSI_TRACE(FSI_DEBUG, "FSI - posix2fsal_attributes\n");

	/* sanity checks */
	if (!p_buffstat || !p_fsalattr_out)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* Initialize ACL regardless of whether ACL was asked or not.
	 * This is needed to make sure ACL attribute is initialized. */
	p_fsalattr_out->acl = NULL;

	/* Fills the output struct

	   supported_attributes is set by the caller.

	   if(FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SUPPATTR))
	   {
	   p_fsalattr_out->supported_attributes = PT_SUPPORTED_ATTRIBUTES;
	   }
	 */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE)) {
		p_fsalattr_out->type = posix2fsal_type(p_buffstat->st_mode);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SIZE)) {
		p_fsalattr_out->filesize = p_buffstat->st_size;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FSID)) {
		p_fsalattr_out->fsid = posix2fsal_fsid(p_buffstat->st_dev);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ACL)) {
		p_fsalattr_out->acl = NULL;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID)) {
		p_fsalattr_out->fileid = (uint64_t) (p_buffstat->st_ino);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MODE)) {
		p_fsalattr_out->mode = unix2fsal_mode(p_buffstat->st_mode);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_NUMLINKS)) {
		p_fsalattr_out->numlinks = p_buffstat->st_nlink;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_OWNER)) {
		p_fsalattr_out->owner = p_buffstat->st_uid;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_GROUP)) {
		p_fsalattr_out->group = p_buffstat->st_gid;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME)) {
		p_fsalattr_out->atime =
		    posix2fsal_time(p_buffstat->st_atime, 0);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME)) {
		p_fsalattr_out->ctime =
		    posix2fsal_time(p_buffstat->st_ctime, 0);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME)) {
		p_fsalattr_out->mtime =
		    posix2fsal_time(p_buffstat->st_mtime, 0);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		p_fsalattr_out->chgtime
		    = posix2fsal_time(MAX_2(p_buffstat->st_mtime,
					    p_buffstat->st_ctime), 0);
		p_fsalattr_out->change =
		    (uint64_t) p_fsalattr_out->chgtime.tv_sec;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED)) {
		p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_RAWDEV)) {
		p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);
	}

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t
posixstat64_2_fsal_attributes(struct stat * p_buffstat,
			      struct attrlist * p_fsalattr_out)
{

	/* sanity checks */
	if (!p_buffstat || !p_fsalattr_out)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* Initialize ACL regardless of whether ACL was asked or not.
	 * This is needed to make sure ACL attribute is initialized. */
	p_fsalattr_out->acl = NULL;

	/* Fills the output struct */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE)) {
		p_fsalattr_out->type = posix2fsal_type(p_buffstat->st_mode);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SIZE)) {
		p_fsalattr_out->filesize = p_buffstat->st_size;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FSID)) {
		p_fsalattr_out->fsid = posix2fsal_fsid(p_buffstat->st_dev);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ACL)) {
		p_fsalattr_out->acl = NULL;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID)) {
		p_fsalattr_out->fileid = (uint64_t) (p_buffstat->st_ino);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MODE)) {
		p_fsalattr_out->mode = unix2fsal_mode(p_buffstat->st_mode);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_NUMLINKS)) {
		p_fsalattr_out->numlinks = p_buffstat->st_nlink;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_OWNER)) {
		p_fsalattr_out->owner = p_buffstat->st_uid;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_GROUP)) {
		p_fsalattr_out->group = p_buffstat->st_gid;
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME)) {
		p_fsalattr_out->atime =
		    posix2fsal_time(p_buffstat->st_atime,
				    p_buffstat->st_atim.tv_nsec);

	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME)) {
		p_fsalattr_out->ctime = posix2fsal_time(p_buffstat->st_ctime,
							p_buffstat->st_ctim.
							tv_nsec);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME)) {
		p_fsalattr_out->mtime = posix2fsal_time(p_buffstat->st_mtime,
							p_buffstat->st_mtim.
							tv_nsec);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		p_fsalattr_out->chgtime
		    = posix2fsal_time(MAX_2(p_buffstat->st_mtime,
					    p_buffstat->st_ctime), 0);
		p_fsalattr_out->change =
		    (uint64_t) p_fsalattr_out->chgtime.tv_sec;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED)) {
		p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_RAWDEV)) {
		p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);
	}

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
