// SPDX-License-Identifier: LGPL-3.0-or-later
/** @file fsal_convert.c
 *  @brief GPFS FSAL module convert functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * GPFS-FSAL type translation functions.
 */

#include "config.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include "nfs4_acls.h"
#include "include/gpfs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

static fsal_status_t
gpfs_acl_2_fsal_acl(struct fsal_attrlist *p_object_attributes,
		    gpfs_acl_t *p_gpfsacl);

/**
 *  @brief convert GPFS xstat to FSAl attributes
 *
 *  @param gpfs_buf Reference to GPFS stat buffer
 *  @param fsal_attr Reference to attribute list
 *  @param use_acl Bool whether ACL are used
 *  @return FSAL status
 *
 *  Same function as posixstat64_2_fsal_attributes. When NFS4 ACL support
 *  is enabled, this will replace posixstat64_2_fsal_attributes.
 */
fsal_status_t
gpfsfsal_xstat_2_fsal_attributes(gpfsfsal_xstat_t *gpfs_buf,
				 struct fsal_attrlist *fsal_attr,
				 gpfs_acl_t *acl_buf, bool use_acl)
{
	struct stat *p_buffstat;

	/* sanity checks */
	if (!gpfs_buf || !fsal_attr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	fsal_attr->supported = GPFS_SUPPORTED_ATTRIBUTES;

	p_buffstat = &gpfs_buf->buffstat;

	LogDebug(COMPONENT_FSAL, "inode %" PRId64, p_buffstat->st_ino);

	/* Fills the output struct */
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_TYPE)) {
		fsal_attr->type = posix2fsal_type(p_buffstat->st_mode);
		fsal_attr->valid_mask |= ATTR_TYPE;
		LogFullDebug(COMPONENT_FSAL, "type = 0x%x",
			     fsal_attr->type);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_SIZE)) {
		fsal_attr->filesize = p_buffstat->st_size;
		fsal_attr->valid_mask |= ATTR_SIZE;
		LogFullDebug(COMPONENT_FSAL, "filesize = %llu",
			     (unsigned long long)fsal_attr->filesize);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_FSID)) {
		fsal_attr->fsid = gpfs_buf->fsal_fsid;
		fsal_attr->valid_mask |= ATTR_FSID;
		LogFullDebug(COMPONENT_FSAL,
			     "fsid=0x%016"PRIx64".0x%016"PRIx64,
			     fsal_attr->fsid.major,
			     fsal_attr->fsid.minor);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_ACL)) {
		if (fsal_attr->acl != NULL) {
			/* We should never be passed attributes that have an
			 * ACL attached, but just in case some future code
			 * path changes that assumption, let's release the
			 * old ACL properly.
			 */
			int acl_status;

			LogCrit(COMPONENT_FSAL,
				"attrs passed in with acl, shouldn't happen");

			acl_status = nfs4_acl_release_entry(fsal_attr->acl);

			if (acl_status != NFS_V4_ACL_SUCCESS)
				LogCrit(COMPONENT_FSAL,
					"Failed to release old acl, status=%d",
					acl_status);

			fsal_attr->acl = NULL;
		}

		if (use_acl && gpfs_buf->attr_valid & XATTR_ACL) {
			/* ACL is valid, so try to convert fsal acl. */
			fsal_status_t status = gpfs_acl_2_fsal_acl(fsal_attr,
								   acl_buf);
			if (!FSAL_IS_ERROR(status)) {
				/* Only mark ACL valid if we actually provide
				 * one in fsal_attr.
				 */
				fsal_attr->valid_mask |= ATTR_ACL;
			} else {
				/* Otherwise, we were asked for ACL and could
				 * not provide one, so we must fail.
				 */
				return status;
			}
		}
		LogFullDebug(COMPONENT_FSAL, "acl = %p", fsal_attr->acl);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_FILEID)) {
		fsal_attr->fileid = (uint64_t) (p_buffstat->st_ino);
		fsal_attr->valid_mask |= ATTR_FILEID;
		LogFullDebug(COMPONENT_FSAL, "fileid = %" PRIu64,
			     fsal_attr->fileid);
	}

	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_MODE)) {
		fsal_attr->mode = unix2fsal_mode(p_buffstat->st_mode);
		fsal_attr->valid_mask |= ATTR_MODE;
		LogFullDebug(COMPONENT_FSAL, "mode = %"PRIu32,
			     fsal_attr->mode);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_NUMLINKS)) {
		fsal_attr->numlinks = p_buffstat->st_nlink;
		fsal_attr->valid_mask |= ATTR_NUMLINKS;
		LogFullDebug(COMPONENT_FSAL, "numlinks = %u",
			     fsal_attr->numlinks);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_OWNER)) {
		fsal_attr->owner = p_buffstat->st_uid;
		fsal_attr->valid_mask |= ATTR_OWNER;
		LogFullDebug(COMPONENT_FSAL, "owner = %" PRIu64,
			     fsal_attr->owner);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_GROUP)) {
		fsal_attr->group = p_buffstat->st_gid;
		fsal_attr->valid_mask |= ATTR_GROUP;
		LogFullDebug(COMPONENT_FSAL, "group = %" PRIu64,
			     fsal_attr->group);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_ATIME)) {
		fsal_attr->atime =
		    posix2fsal_time(p_buffstat->st_atime,
				    p_buffstat->st_atim.tv_nsec);
		fsal_attr->valid_mask |= ATTR_ATIME;
		LogFullDebug(COMPONENT_FSAL, "atime = %lu",
			     fsal_attr->atime.tv_sec);
	}

	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_CTIME)) {
		fsal_attr->ctime =
		    posix2fsal_time(p_buffstat->st_ctime,
				    p_buffstat->st_ctim.tv_nsec);
		fsal_attr->valid_mask |= ATTR_CTIME;
		LogFullDebug(COMPONENT_FSAL, "ctime = %lu",
			     fsal_attr->ctime.tv_sec);
	}
	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_MTIME)) {
		fsal_attr->mtime =
		    posix2fsal_time(p_buffstat->st_mtime,
				    p_buffstat->st_mtim.tv_nsec);
		fsal_attr->valid_mask |= ATTR_MTIME;
		LogFullDebug(COMPONENT_FSAL, "mtime = %lu",
			     fsal_attr->mtime.tv_sec);
	}

	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_CHANGE)) {
		if (p_buffstat->st_mtime == p_buffstat->st_ctime) {
			if (p_buffstat->st_mtim.tv_nsec >
			    p_buffstat->st_ctim.tv_nsec) {
				fsal_attr->change =
					(uint64_t) p_buffstat->st_mtim.tv_sec +
					(uint64_t) p_buffstat->st_mtim.tv_nsec;
			} else {
				fsal_attr->change =
					(uint64_t) p_buffstat->st_ctim.tv_sec +
					(uint64_t) p_buffstat->st_ctim.tv_nsec;
			}
		} else if (p_buffstat->st_mtime > p_buffstat->st_ctime) {
			fsal_attr->change =
				(uint64_t) p_buffstat->st_mtim.tv_sec +
				(uint64_t) p_buffstat->st_mtim.tv_nsec;
		} else {
			fsal_attr->change =
				(uint64_t) p_buffstat->st_ctim.tv_sec +
				(uint64_t) p_buffstat->st_ctim.tv_nsec;
		}

		fsal_attr->valid_mask |= ATTR_CHANGE;
		LogFullDebug(COMPONENT_FSAL, "change = %"PRIu64,
			     fsal_attr->change);

	}

	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_SPACEUSED)) {
		fsal_attr->spaceused = p_buffstat->st_blocks * S_BLKSIZE;
		fsal_attr->valid_mask |= ATTR_SPACEUSED;
		LogFullDebug(COMPONENT_FSAL, "spaceused = %llu",
			     (unsigned long long)fsal_attr->spaceused);
	}

	if (FSAL_TEST_MASK(fsal_attr->request_mask, ATTR_RAWDEV)) {
		fsal_attr->rawdev = posix2fsal_devt(p_buffstat->st_rdev);
		fsal_attr->valid_mask |= ATTR_RAWDEV;
		LogFullDebug(COMPONENT_FSAL, "rawdev major = %u, minor = %u",
			     (unsigned int)fsal_attr->rawdev.major,
			     (unsigned int)fsal_attr->rawdev.minor);
	}

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Covert GPFS NFS4 ACLs to FSAL ACLs, and set the ACL
 * pointer of attribute. */
static fsal_status_t
gpfs_acl_2_fsal_acl(struct fsal_attrlist *p_object_attributes,
		    gpfs_acl_t *p_gpfsacl)
{
	fsal_acl_status_t status;
	fsal_acl_data_t acldata;
	fsal_ace_t *pace;
	fsal_acl_t *pacl;
	gpfs_ace_v4_t *pace_gpfs;

	/* sanity checks */
	if (!p_object_attributes || !p_gpfsacl)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* Create fsal acl data. */
	acldata.naces = p_gpfsacl->acl_nace;
	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);

	/* Fill fsal acl data from gpfs acl. */
	for (pace = acldata.aces, pace_gpfs = p_gpfsacl->ace_v4;
	     pace < acldata.aces + acldata.naces; pace++, pace_gpfs++) {
		pace->type = pace_gpfs->aceType;
		pace->flag = pace_gpfs->aceFlags;
		pace->iflag = pace_gpfs->aceIFlags;
		pace->perm = pace_gpfs->aceMask;

		if (IS_FSAL_ACE_SPECIAL_ID(*pace)) { /* Record special user. */
			pace->who.uid = pace_gpfs->aceWho;
		} else {
			if (IS_FSAL_ACE_GROUP_ID(*pace))
				pace->who.gid = pace_gpfs->aceWho;
			else	/* Record user. */
				pace->who.uid = pace_gpfs->aceWho;
		}

		LogMidDebug(COMPONENT_FSAL,
			 "fsal ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
			 pace->type, pace->flag, pace->perm,
			 IS_FSAL_ACE_SPECIAL_ID(*pace),
			 GET_FSAL_ACE_WHO_TYPE(*pace), GET_FSAL_ACE_WHO(*pace));
	}

	/* Create a new hash table entry for fsal acl. */
	pacl = nfs4_acl_new_entry(&acldata, &status);
	LogMidDebug(COMPONENT_FSAL, "fsal acl = %p, fsal_acl_status = %u", pacl,
		 status);

	if (pacl == NULL) {
		LogCrit(COMPONENT_FSAL,
			"failed to create a new acl entry");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	/* Add fsal acl to attribute. */
	p_object_attributes->acl = pacl;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/** @fn fsal_status_t
 *     fsal_acl_2_gpfs_acl(struct fsal_obj_handle *dir_hdl,
 *                         fsal_acl_t *fsal_acl,
 *                         gpfsfsal_xstat_t *gpfs_buf)
 *  @param dir_hdl Object handle
 *  @param fsal_acl GPFS access control list
 *  @param gpfs_buf Reference to GPFS stat buffer
 *  @return FSAL status
 *
 *  @brief Covert FSAL ACLs to GPFS NFS4 ACLs.
 */
fsal_status_t
fsal_acl_2_gpfs_acl(struct fsal_obj_handle *dir_hdl, fsal_acl_t *fsal_acl,
		    gpfsfsal_xstat_t *gpfs_buf, gpfs_acl_t *acl_buf,
		    unsigned int acl_buflen)
{
	int i;
	fsal_ace_t *pace;

	acl_buf->acl_level = 0;
	acl_buf->acl_version = GPFS_ACL_VERSION_NFS4;
	acl_buf->acl_type = GPFS_ACL_TYPE_NFS4;
	acl_buf->acl_nace = fsal_acl->naces;
	acl_buf->acl_len = acl_buflen;

	/* GPFS can support max 638 entries */
	if (fsal_acl->naces > GPFS_ACL_MAX_NACES) {
		LogInfo(COMPONENT_FSAL,
			"No. of ACE's:%d higher than supported by GPFS",
			fsal_acl->naces);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	for (pace = fsal_acl->aces, i = 0;
	     pace < fsal_acl->aces + fsal_acl->naces; pace++, i++) {
		acl_buf->ace_v4[i].aceType = pace->type;
		acl_buf->ace_v4[i].aceFlags = pace->flag;
		acl_buf->ace_v4[i].aceIFlags = pace->iflag;
		acl_buf->ace_v4[i].aceMask = pace->perm;

		if (IS_FSAL_ACE_SPECIAL_ID(*pace))
			acl_buf->ace_v4[i].aceWho = pace->who.uid;
		else {
			if (IS_FSAL_ACE_GROUP_ID(*pace))
				acl_buf->ace_v4[i].aceWho = pace->who.gid;
			else
				acl_buf->ace_v4[i].aceWho = pace->who.uid;
		}

		LogMidDebug(COMPONENT_FSAL,
			 "gpfs ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
			 acl_buf->ace_v4[i].aceType,
			 acl_buf->ace_v4[i].aceFlags,
			 acl_buf->ace_v4[i].aceMask,
			 (acl_buf->ace_v4[i].aceIFlags &
					FSAL_ACE_IFLAG_SPECIAL_ID) ? 1 : 0,
			 (acl_buf->ace_v4[i].aceFlags & FSAL_ACE_FLAG_GROUP_ID)
				? "gid" : "uid",
			 acl_buf->ace_v4[i].aceWho);

		/* It is invalid to set inherit flags on non dir objects */
		if (dir_hdl->type != DIRECTORY &&
		    (acl_buf->ace_v4[i].aceFlags &
		    FSAL_ACE_FLAG_INHERIT) != 0) {
			LogMidDebug(COMPONENT_FSAL,
			   "attempt to set inherit flag to non dir object");
			return fsalstat(ERR_FSAL_INVAL, 0);
		}

		/* It is invalid to set inherit only with
		 * out an actual inherit flag */
		if ((acl_buf->ace_v4[i].aceFlags & FSAL_ACE_FLAG_INHERIT) ==
			FSAL_ACE_FLAG_INHERIT_ONLY) {
			LogMidDebug(COMPONENT_FSAL,
			   "attempt to set inherit only without an inherit flag");
			return fsalstat(ERR_FSAL_INVAL, 0);
		}

	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t fsal_cred_2_gpfs_cred(struct user_cred *p_fsalcred,
				    struct xstat_cred_t *p_gpfscred)
{
	int i;

	if (!p_fsalcred || !p_gpfscred)
		return fsalstat(ERR_FSAL_FAULT, 0);

	p_gpfscred->principal = p_fsalcred->caller_uid;
	p_gpfscred->group = p_fsalcred->caller_gid;
	p_gpfscred->num_groups = p_fsalcred->caller_glen;

	for (i = 0; i < p_fsalcred->caller_glen; i++)
		p_gpfscred->eGroups[i] = p_fsalcred->caller_garray[i];

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t fsal_mode_2_gpfs_mode(mode_t fsal_mode, fsal_accessflags_t v4mask,
				    unsigned int *p_gpfsmode, bool is_dir)
{
	int gpfs_mode = 0;

	if (!p_gpfsmode)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* If mode is zero, translate v4mask to posix mode. */
	if (fsal_mode == 0) {
		if (!is_dir) {
			if (v4mask & FSAL_ACE_PERM_READ_DATA)
				gpfs_mode |= FSAL_R_OK;
		} else {
			if (v4mask & FSAL_ACE_PERM_LIST_DIR) {
				gpfs_mode |= FSAL_R_OK;
				gpfs_mode |= FSAL_X_OK;
			}
		}

		if (!is_dir) {
			if (v4mask & FSAL_ACE_PERM_WRITE_DATA)
				gpfs_mode |= FSAL_W_OK;
		} else {
			if (v4mask & FSAL_ACE_PERM_ADD_FILE) {
				gpfs_mode |= FSAL_W_OK;
				gpfs_mode |= FSAL_X_OK;
			}
		}

		if (!is_dir) {
			if (v4mask & FSAL_ACE_PERM_APPEND_DATA)
				gpfs_mode |= FSAL_W_OK;
		} else {
			if (v4mask & FSAL_ACE_PERM_ADD_SUBDIRECTORY) {
				gpfs_mode |= FSAL_W_OK;
				gpfs_mode |= FSAL_X_OK;
			}
		}

		if (!is_dir) {
			if (v4mask & FSAL_ACE_PERM_EXECUTE)
				gpfs_mode |= FSAL_X_OK;
		} else {
			if (v4mask & FSAL_ACE_PERM_DELETE_CHILD) {
				gpfs_mode |= FSAL_W_OK;
				gpfs_mode |= FSAL_X_OK;
			}
		}

		if (v4mask & FSAL_ACE_PERM_DELETE)
			gpfs_mode |= FSAL_W_OK;

		gpfs_mode = gpfs_mode >> 24;
	} else {
		gpfs_mode = fsal_mode >> 24;
	}

	LogMidDebug(COMPONENT_FSAL,
		 "fsal_mode 0x%x, v4mask 0x%x, is_dir %d converted to gpfs_mode 0x%x",
		 fsal_mode, v4mask, is_dir, gpfs_mode);

	*p_gpfsmode = gpfs_mode;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
