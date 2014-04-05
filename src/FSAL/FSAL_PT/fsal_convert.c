/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_convert.c
 * Description: FSAL convert operations implementation
 * Author:      FSI IPC dev team
 * ----------------------------------------------------------------------------
 */
/*
 * vim:noexpandtab:shiftwidth=4:tabstop=4:
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

fsal_status_t pt_posix2fsal_attributes(struct stat *p_buffstat,
				    struct attrlist *p_fsalattr_out)
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
	      p_fsalattr_out->supported_attributes = PT_SUPPORTED_ATTRIBUTES;
	 */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE))
		p_fsalattr_out->type = posix2fsal_type(p_buffstat->st_mode);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SIZE))
		p_fsalattr_out->filesize = p_buffstat->st_size;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FSID))
		p_fsalattr_out->fsid = posix2fsal_fsid(p_buffstat->st_dev);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ACL))
		p_fsalattr_out->acl = NULL;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID))
		p_fsalattr_out->fileid = (uint64_t) (p_buffstat->st_ino);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MODE))
		p_fsalattr_out->mode = unix2fsal_mode(p_buffstat->st_mode);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_NUMLINKS))
		p_fsalattr_out->numlinks = p_buffstat->st_nlink;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_OWNER))
		p_fsalattr_out->owner = p_buffstat->st_uid;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_GROUP))
		p_fsalattr_out->group = p_buffstat->st_gid;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME))
		p_fsalattr_out->atime =
		    posix2fsal_time(p_buffstat->st_atime, 0);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME))
		p_fsalattr_out->ctime =
		    posix2fsal_time(p_buffstat->st_ctime, 0);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME))
		p_fsalattr_out->mtime =
		    posix2fsal_time(p_buffstat->st_mtime, 0);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		p_fsalattr_out->chgtime =
		    posix2fsal_time(MAX
				    (p_buffstat->st_mtime,
				     p_buffstat->st_ctime), 0);
		p_fsalattr_out->change =
		    (uint64_t) p_fsalattr_out->chgtime.tv_sec;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED))
		p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_RAWDEV))
		p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t posixstat64_2_fsal_attributes(struct stat *p_buffstat,
					    struct attrlist *p_fsalattr_out)
{

	/* sanity checks */
	if (!p_buffstat || !p_fsalattr_out)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* Initialize ACL regardless of whether ACL was asked or not.
	 * This is needed to make sure ACL attribute is initialized. */
	p_fsalattr_out->acl = NULL;

	/* Fills the output struct */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE))
		p_fsalattr_out->type = posix2fsal_type(p_buffstat->st_mode);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SIZE))
		p_fsalattr_out->filesize = p_buffstat->st_size;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FSID))
		p_fsalattr_out->fsid = posix2fsal_fsid(p_buffstat->st_dev);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ACL))
		p_fsalattr_out->acl = NULL;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID))
		p_fsalattr_out->fileid = (uint64_t) (p_buffstat->st_ino);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MODE))
		p_fsalattr_out->mode = unix2fsal_mode(p_buffstat->st_mode);
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_NUMLINKS))
		p_fsalattr_out->numlinks = p_buffstat->st_nlink;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_OWNER))
		p_fsalattr_out->owner = p_buffstat->st_uid;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_GROUP))
		p_fsalattr_out->group = p_buffstat->st_gid;
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME)) {
		p_fsalattr_out->atime =
		    posix2fsal_time(p_buffstat->st_atime,
				    p_buffstat->st_atim.tv_nsec);

	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME)) {
		p_fsalattr_out->ctime =
		    posix2fsal_time(p_buffstat->st_ctime,
				    p_buffstat->st_ctim.tv_nsec);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME)) {
		p_fsalattr_out->mtime =
		    posix2fsal_time(p_buffstat->st_mtime,
				    p_buffstat->st_mtim.tv_nsec);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		p_fsalattr_out->chgtime =
		    posix2fsal_time(MAX
				    (p_buffstat->st_mtime,
				     p_buffstat->st_ctime), 0);
		p_fsalattr_out->change =
		    (uint64_t) p_fsalattr_out->chgtime.tv_sec;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED))
		p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_RAWDEV))
		p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
