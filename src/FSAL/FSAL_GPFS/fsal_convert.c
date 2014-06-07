/*
 * vim:noexpandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_convert.c
 * \date    $Date: 2006/01/17 15:53:39 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
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

#ifdef _USE_NFS4_ACL
static int gpfs_acl_2_fsal_acl(struct attrlist *p_object_attributes,
			       gpfs_acl_t *p_gpfsacl);
#endif				/* _USE_NFS4_ACL */

fsal_status_t posix2fsal_attributes(const struct stat *p_buffstat,
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

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME))
		p_fsalattr_out->atime = p_buffstat->st_atim;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME))
		p_fsalattr_out->ctime = p_buffstat->st_ctim;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME))
		p_fsalattr_out->mtime = p_buffstat->st_mtim;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		p_fsalattr_out->chgtime =
		    (gsh_time_cmp(&p_buffstat->st_mtim, &p_buffstat->st_ctim) >
		     0) ? p_buffstat->st_mtim : p_buffstat->st_ctim;
		/* XXX */
		p_fsalattr_out->change =
		    p_fsalattr_out->chgtime.tv_sec +
		    p_fsalattr_out->chgtime.tv_nsec;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED))
		p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_RAWDEV))
		p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Same function as posixstat64_2_fsal_attributes. When NFS4 ACL support
 * is enabled, this will replace posixstat64_2_fsal_attributes. */
fsal_status_t gpfsfsal_xstat_2_fsal_attributes(gpfsfsal_xstat_t *p_buffxstat,
					       struct attrlist *p_fsalattr_out)
{
	struct stat *p_buffstat;

	/* sanity checks */
	if (!p_buffxstat || !p_fsalattr_out)
		return fsalstat(ERR_FSAL_FAULT, 0);

	p_buffstat = &p_buffxstat->buffstat;

	LogDebug(COMPONENT_FSAL, "inode %ld", p_buffstat->st_ino);

	/* Fills the output struct */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE)) {
		p_fsalattr_out->type = posix2fsal_type(p_buffstat->st_mode);
		LogFullDebug(COMPONENT_FSAL, "type = 0x%x",
			     p_fsalattr_out->type);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SIZE)) {
		p_fsalattr_out->filesize = p_buffstat->st_size;
		LogFullDebug(COMPONENT_FSAL, "filesize = %llu",
			     (unsigned long long)p_fsalattr_out->filesize);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FSID)) {
		p_fsalattr_out->fsid = p_buffxstat->fsal_fsid;
		LogFullDebug(COMPONENT_FSAL,
			     "fsid=0x%016"PRIx64".0x%016"PRIx64,
			     p_fsalattr_out->fsid.major,
			     p_fsalattr_out->fsid.minor);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ACL)) {
		p_fsalattr_out->acl = NULL;
#ifdef _USE_NFS4_ACL
		if (p_buffxstat->attr_valid & XATTR_ACL) {
			/* ACL is valid, so try to convert fsal acl. */
			gpfs_acl_2_fsal_acl(p_fsalattr_out,
					    (gpfs_acl_t *) p_buffxstat->
					    buffacl);
		}
#endif				/* _USE_NFS4_ACL */
		LogFullDebug(COMPONENT_FSAL, "acl = %p", p_fsalattr_out->acl);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID)) {
		p_fsalattr_out->fileid = (uint64_t) (p_buffstat->st_ino);
		LogFullDebug(COMPONENT_FSAL, "fileid = %lu",
			     p_fsalattr_out->fileid);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MODE)) {
		p_fsalattr_out->mode = unix2fsal_mode(p_buffstat->st_mode);
		LogFullDebug(COMPONENT_FSAL, "mode = %llu",
			     (long long unsigned int)p_fsalattr_out->mode);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_NUMLINKS)) {
		p_fsalattr_out->numlinks = p_buffstat->st_nlink;
		LogFullDebug(COMPONENT_FSAL, "numlinks = %u",
			     p_fsalattr_out->numlinks);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_OWNER)) {
		p_fsalattr_out->owner = p_buffstat->st_uid;
		LogFullDebug(COMPONENT_FSAL, "owner = %lu",
			     p_fsalattr_out->owner);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_GROUP)) {
		p_fsalattr_out->group = p_buffstat->st_gid;
		LogFullDebug(COMPONENT_FSAL, "group = %lu",
			     p_fsalattr_out->group);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME)) {
		p_fsalattr_out->atime =
		    posix2fsal_time(p_buffstat->st_atime,
				    p_buffstat->st_atim.tv_nsec);
		LogFullDebug(COMPONENT_FSAL, "atime = %lu",
			     p_fsalattr_out->atime.tv_sec);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME)) {
		p_fsalattr_out->ctime =
		    posix2fsal_time(p_buffstat->st_ctime,
				    p_buffstat->st_ctim.tv_nsec);
		LogFullDebug(COMPONENT_FSAL, "ctime = %lu",
			     p_fsalattr_out->ctime.tv_sec);
	}
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME)) {
		p_fsalattr_out->mtime =
		    posix2fsal_time(p_buffstat->st_mtime,
				    p_buffstat->st_mtim.tv_nsec);
		LogFullDebug(COMPONENT_FSAL, "mtime = %lu",
			     p_fsalattr_out->mtime.tv_sec);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		if (p_buffstat->st_mtime == p_buffstat->st_ctime) {
			if (p_buffstat->st_mtim.tv_nsec >
			    p_buffstat->st_ctim.tv_nsec)
				p_fsalattr_out->chgtime =
				    posix2fsal_time(p_buffstat->st_mtime,
						    p_buffstat->st_mtim.
						    tv_nsec);
			else
				p_fsalattr_out->chgtime =
				    posix2fsal_time(p_buffstat->st_ctime,
						    p_buffstat->st_ctim.
						    tv_nsec);
		} else if (p_buffstat->st_mtime > p_buffstat->st_ctime) {
			p_fsalattr_out->chgtime =
			    posix2fsal_time(p_buffstat->st_mtime,
					    p_buffstat->st_mtim.tv_nsec);
		} else {
			p_fsalattr_out->chgtime =
			    posix2fsal_time(p_buffstat->st_ctime,
					    p_buffstat->st_ctim.tv_nsec);
		}
		p_fsalattr_out->change =
		    (uint64_t) p_fsalattr_out->chgtime.tv_sec +
		    (uint64_t) p_fsalattr_out->chgtime.tv_nsec;
		LogFullDebug(COMPONENT_FSAL, "chgtime = %lu",
			     p_fsalattr_out->chgtime.tv_sec);

	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED)) {
		p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;
		LogFullDebug(COMPONENT_FSAL, "spaceused = %llu",
			     (unsigned long long)p_fsalattr_out->spaceused);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_RAWDEV)) {
		p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);
		LogFullDebug(COMPONENT_FSAL, "rawdev major = %u, minor = %u",
			     (unsigned int)p_fsalattr_out->rawdev.major,
			     (unsigned int)p_fsalattr_out->rawdev.minor);
	}

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

#ifdef _USE_NFS4_ACL
/* Covert GPFS NFS4 ACLs to FSAL ACLs, and set the ACL
 * pointer of attribute. */
static int gpfs_acl_2_fsal_acl(struct attrlist *p_object_attributes,
			       gpfs_acl_t *p_gpfsacl)
{
	fsal_acl_status_t status;
	fsal_acl_data_t acldata;
	fsal_ace_t *pace;
	fsal_acl_t *pacl;
	gpfs_ace_v4_t *pace_gpfs;

	/* sanity checks */
	if (!p_object_attributes || !p_gpfsacl)
		return ERR_FSAL_FAULT;

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
			 "gpfs_acl_2_fsal_acl: fsal ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
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
			"gpfs_acl_2_fsal_acl: failed to create a new acl entry");
		return ERR_FSAL_FAULT;
	}

	/* Add fsal acl to attribute. */
	p_object_attributes->acl = pacl;

	return ERR_FSAL_NO_ERROR;
}

/* Covert FSAL ACLs to GPFS NFS4 ACLs. */
fsal_status_t fsal_acl_2_gpfs_acl(fsal_acl_t *p_fsalacl,
				  gpfsfsal_xstat_t *p_buffxstat)
{
	int i;
	fsal_ace_t *pace;
	gpfs_acl_t *p_gpfsacl;

	p_gpfsacl = (gpfs_acl_t *) p_buffxstat->buffacl;

	p_gpfsacl->acl_level = 0;
	p_gpfsacl->acl_version = GPFS_ACL_VERSION_NFS4;
	p_gpfsacl->acl_type = GPFS_ACL_TYPE_NFS4;
	p_gpfsacl->acl_nace = p_fsalacl->naces;
	p_gpfsacl->acl_len =
	    ((int)(signed long)&(((gpfs_acl_t *) 0)->ace_v1)) +
	    p_gpfsacl->acl_nace * sizeof(gpfs_ace_v4_t);

	for (pace = p_fsalacl->aces, i = 0;
	     pace < p_fsalacl->aces + p_fsalacl->naces; pace++, i++) {
		p_gpfsacl->ace_v4[i].aceType = pace->type;
		p_gpfsacl->ace_v4[i].aceFlags = pace->flag;
		p_gpfsacl->ace_v4[i].aceIFlags = pace->iflag;
		p_gpfsacl->ace_v4[i].aceMask = pace->perm;

		if (IS_FSAL_ACE_SPECIAL_ID(*pace))
			p_gpfsacl->ace_v4[i].aceWho = pace->who.uid;
		else {
			if (IS_FSAL_ACE_GROUP_ID(*pace))
				p_gpfsacl->ace_v4[i].aceWho = pace->who.gid;
			else
				p_gpfsacl->ace_v4[i].aceWho = pace->who.uid;
		}

		LogMidDebug(COMPONENT_FSAL,
			 "fsal_acl_2_gpfs_acl: gpfs ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
			 p_gpfsacl->ace_v4[i].aceType,
			 p_gpfsacl->ace_v4[i].aceFlags,
			 p_gpfsacl->ace_v4[i].aceMask,
			 (p_gpfsacl->ace_v4[i].
			  aceIFlags & FSAL_ACE_IFLAG_SPECIAL_ID) ? 1 : 0,
			 (p_gpfsacl->ace_v4[i].
			  aceFlags & FSAL_ACE_FLAG_GROUP_ID) ? "gid" : "uid",
			 p_gpfsacl->ace_v4[i].aceWho);

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
#endif				/* _USE_NFS4_ACL */
