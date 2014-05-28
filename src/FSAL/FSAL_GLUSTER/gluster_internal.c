/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2011
 * Author: Anand Subramanian anands@redhat.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* main.c
 * Module core functions
 */

#include "gluster_internal.h"
#include "fsal_convert.h"
#include "nfs4_acls.h"
#include "FSAL/fsal_commonlib.h"

/**
 * @brief FSAL status mapping from GlusterFS errors
 *
 * This function returns a fsal_status_t with the FSAL error as the
 * major, and the posix error as minor.
 *
 * @param[in] gluster_errorcode Gluster error
 *
 * @return FSAL status.
 */

fsal_status_t gluster2fsal_error(const int gluster_errorcode)
{
	fsal_status_t status;
	status.minor = gluster_errorcode;

	switch (gluster_errorcode) {

	case 0:
		status.major = ERR_FSAL_NO_ERROR;
		break;

	case EPERM:
		status.major = ERR_FSAL_PERM;
		break;

	case ENOENT:
		status.major = ERR_FSAL_NOENT;
		break;

	case ECONNREFUSED:
	case ECONNABORTED:
	case ECONNRESET:
	case EIO:
	case ENFILE:
	case EMFILE:
	case EPIPE:
		status.major = ERR_FSAL_IO;
		break;

	case ENODEV:
	case ENXIO:
		status.major = ERR_FSAL_NXIO;
		break;

	case EBADF:
			/**
			 * @todo: The EBADF error also happens when file is
			 *	  opened for reading, and we try writting in
			 *	  it.  In this case, we return
			 *	  ERR_FSAL_NOT_OPENED, but it doesn't seems to
			 *	  be a correct error translation.
			 */
		status.major = ERR_FSAL_NOT_OPENED;
		break;

	case ENOMEM:
		status.major = ERR_FSAL_NOMEM;
		break;

	case EACCES:
		status.major = ERR_FSAL_ACCESS;
		break;

	case EFAULT:
		status.major = ERR_FSAL_FAULT;
		break;

	case EEXIST:
		status.major = ERR_FSAL_EXIST;
		break;

	case EXDEV:
		status.major = ERR_FSAL_XDEV;
		break;

	case ENOTDIR:
		status.major = ERR_FSAL_NOTDIR;
		break;

	case EISDIR:
		status.major = ERR_FSAL_ISDIR;
		break;

	case EINVAL:
		status.major = ERR_FSAL_INVAL;
		break;

	case EFBIG:
		status.major = ERR_FSAL_FBIG;
		break;

	case ENOSPC:
		status.major = ERR_FSAL_NOSPC;
		break;

	case EMLINK:
		status.major = ERR_FSAL_MLINK;
		break;

	case EDQUOT:
		status.major = ERR_FSAL_DQUOT;
		break;

	case ENAMETOOLONG:
		status.major = ERR_FSAL_NAMETOOLONG;
		break;

	case ENOTEMPTY:
		status.major = ERR_FSAL_NOTEMPTY;
		break;

	case ESTALE:
		status.major = ERR_FSAL_STALE;
		break;

	case EAGAIN:
	case EBUSY:
		status.major = ERR_FSAL_DELAY;
		break;

	default:
		status.major = ERR_FSAL_SERVERFAULT;
		break;
	}

	return status;
}

/**
 * @brief Convert a struct stat from Gluster to a struct attrlist
 *
 * This function writes the content of the supplied struct stat to the
 * struct fsalsattr.
 *
 * @param[in]  buffstat Stat structure
 * @param[out] fsalattr FSAL attributes
 */

void stat2fsal_attributes(const struct stat *buffstat,
			  struct attrlist *fsalattr)
{
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
}

struct fsal_staticfsinfo_t *gluster_staticinfo(struct fsal_module *hdl)
{
	struct glusterfs_fsal_module *glfsal_module;

	glfsal_module = container_of(hdl, struct glusterfs_fsal_module, fsal);
	return &glfsal_module->fs_info;
}

/**
 * @brief Construct a new filehandle
 *
 * This function constructs a new Gluster FSAL object handle and attaches
 * it to the export.  After this call the attributes have been filled
 * in and the handdle is up-to-date and usable.
 *
 * @param[in]  st     Stat data for the file
 * @param[in]  export Export on which the object lives
 * @param[out] obj    Object created
 *
 * @return 0 on success, negative error codes on failure.
 */

int construct_handle(struct glusterfs_export *glexport, const struct stat *sb,
		     struct glfs_object *glhandle, unsigned char *globjhdl,
		     int len, struct glusterfs_handle **obj, const char *vol_uuid)
{
	struct glusterfs_handle *constructing = NULL;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	glusterfs_fsal_xstat_t buffxstat;

	*obj = NULL;
	memset(&buffxstat, 0, sizeof(glusterfs_fsal_xstat_t));

	constructing = gsh_calloc(1, sizeof(struct glusterfs_handle));
	if (constructing == NULL) {
		errno = ENOMEM;
		return -1;
	}

	stat2fsal_attributes(sb, &constructing->handle.attributes);

	status = glusterfs_get_acl(glexport, glhandle, &buffxstat,
				   &constructing->handle.attributes);

	if (FSAL_IS_ERROR(status)) {
		// TODO: Is the error appropriate
		errno = EINVAL;
                gsh_free(constructing);
		return -1;
	}

	constructing->glhandle = glhandle;
	memcpy(constructing->globjhdl, vol_uuid, GLAPI_UUID_LENGTH);
	memcpy(constructing->globjhdl+GLAPI_UUID_LENGTH, globjhdl, GFAPI_HANDLE_LENGTH);
	constructing->glfd = NULL;

	fsal_obj_handle_init(&constructing->handle, &glexport->export,
			     constructing->handle.attributes.type);

	*obj = constructing;

	return 0;
}

void gluster_cleanup_vars(struct glfs_object *glhandle)
{
	if (glhandle) {
		/* Error ignored, this is a cleanup operation, can't do much.
		 * TODO: Useful point for logging? */
		glfs_h_close(glhandle);
	}

	return;
}

/* fs_specific_has() parses the fs_specific string for a particular key, 
 *  returns true if found, and optionally returns a val if the string is
 *  of the form key=val.
 *
 * The fs_specific string is a comma (,) separated options where each option
 * can be of the form key=value or just key. Example:
 *	FS_specific = "foo=baz,enable_A";
 */
bool fs_specific_has(const char *fs_specific, const char *key, char *val,
		     int *max_val_bytes)
{
	char *next_comma, *option;
	bool ret;
	char *fso_dup = NULL;

	if (!fs_specific || !fs_specific[0])
		return false;

	fso_dup = gsh_strdup(fs_specific);
	if (!fso_dup) {
		LogCrit(COMPONENT_FSAL, "strdup(%s) failed", fs_specific);
		return false;
	}

	for (option = strtok_r(fso_dup, ",", &next_comma); option;
	     option = strtok_r(NULL, ",", &next_comma)) {
		char *k = option;
		char *v = k;

		strsep(&v, "=");
		if (0 == strcmp(k, key)) {
			if (val)
				strncpy(val, v, *max_val_bytes);
			if (max_val_bytes)
				*max_val_bytes = strlen(v) + 1;
			ret = true;
			goto cleanup;
		}
	}

	ret = false;
 cleanup:
	gsh_free(fso_dup);
	return ret;
}

int setglustercreds(struct glusterfs_export *glfs_export, uid_t * uid,
		    gid_t * gid, unsigned int ngrps, gid_t * groups)
{
	int rc = 0;

	if (uid) {
		if (*uid != glfs_export->saveduid)
			rc = glfs_setfsuid(*uid);
	} else {
		rc = glfs_setfsuid(glfs_export->saveduid);
	}
	if (rc)
		goto out;

	if (gid) {
		if (*gid != glfs_export->savedgid)
			rc = glfs_setfsgid(*gid);
	} else {
		rc = glfs_setfsgid(glfs_export->savedgid);
	}
	if (rc)
		goto out;

	if (ngrps != 0 && groups) {
		rc = glfs_setfsgroups(ngrps, groups);
	} else {
		rc = glfs_setfsgroups(0, NULL);
	}

 out:
	return rc;
}

#ifdef POSIX_ACL_CONVERSION
/*
 *  Given a FSAL ACL convert it into an equivalent POSIX ACL
 */
fsal_status_t fsal_acl_2_glusterfs_posix_acl(fsal_acl_t *p_fsalacl,
				  char *p_buffacl)
{
	int i;
	fsal_ace_t *pace;
	glusterfs_acl_t *p_glusterfsacl;

	p_glusterfsacl = (glusterfs_acl_t *) p_buffacl;

	p_glusterfsacl->acl_level = 0;
	p_glusterfsacl->acl_version = GLUSTERFS_ACL_VERSION_POSIX;
	p_glusterfsacl->acl_type = GLUSTERFS_ACL_TYPE_ACCESS;
	p_glusterfsacl->acl_nace = 0;

	for (pace = p_fsalacl->aces, i = 0;
	     pace < p_fsalacl->aces + p_fsalacl->naces; pace++, i++) {

		if (!IS_FSAL_ACE_ALLOW(*pace)) {
			continue;
		}

		p_glusterfsacl->acl_nace++;
		if (IS_FSAL_ACE_SPECIAL_ID(*pace)) {
			// POSIX ACLs do not contain IDs for the special ACEs
			p_glusterfsacl->ace_v1[i].ace_id = GLUSTERFS_ACL_UNDEFINED_ID;
			switch (pace->who.uid) {
				case FSAL_ACE_SPECIAL_OWNER:
					p_glusterfsacl->ace_v1[i].ace_tag = GLUSTERFS_ACL_USER_OBJ;
					break;
				case FSAL_ACE_SPECIAL_GROUP:
					p_glusterfsacl->ace_v1[i].ace_tag = GLUSTERFS_ACL_GROUP_OBJ;
					break;
				case FSAL_ACE_SPECIAL_EVERYONE: 
					p_glusterfsacl->ace_v1[i].ace_tag = GLUSTERFS_ACL_OTHER;
				break;
			}
		}
		else {
			/*
			 * TODO: POSIX ACLs do not support multiple USER/GROUP Aces with same
			 * UID/GID. What about duplicates
			 */
			if (IS_FSAL_ACE_GROUP_ID(*pace)) {
				p_glusterfsacl->ace_v1[i].ace_tag = GLUSTERFS_ACL_GROUP;
				p_glusterfsacl->ace_v1[i].ace_id = pace->who.gid;
			}
			else {
				p_glusterfsacl->ace_v1[i].ace_tag = GLUSTERFS_ACL_USER;
				p_glusterfsacl->ace_v1[i].ace_id = pace->who.uid;
			}
		}
		p_glusterfsacl->ace_v1[i].ace_perm = 0;
		p_glusterfsacl->ace_v1[i].ace_perm |= 
			((pace->perm & ACE4_MASK_READ_DATA) ? GLUSTERFS_ACL_READ : 0);
		p_glusterfsacl->ace_v1[i].ace_perm |= 
			((pace->perm & ACE4_MASK_WRITE_DATA) ? GLUSTERFS_ACL_WRITE : 0);
		p_glusterfsacl->ace_v1[i].ace_perm |= 
			((pace->perm & ACE4_MASK_EXECUTE) ? GLUSTERFS_ACL_EXECUTE : 0);

	}
	/* TODO: calculate appropriate aceMask */
	p_glusterfsacl->ace_v1[i].ace_tag = GLUSTERFS_ACL_MASK;
	p_glusterfsacl->ace_v1[i].ace_perm |=  GLUSTERFS_ACL_READ | GLUSTERFS_ACL_WRITE;
	p_glusterfsacl->acl_nace++;

	// One extra ace for mask
	p_glusterfsacl->acl_len =
	    ((int)(signed long)&(((glusterfs_acl_t *) 0)->ace_v1)) +
	    (p_glusterfsacl->acl_nace) * sizeof(glusterfs_ace_v1_t);

	/* TODO: Sort the aces in the order of OWNER, USER, GROUP & EVRYONE */
	
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
#endif

/*
 * Read the ACL in GlusterFS format and convert it into fsal ACL before
 * storing it in fsalattr
 */
fsal_status_t glusterfs_get_acl (struct glusterfs_export *glfs_export,
				struct glfs_object *glhandle,
				glusterfs_fsal_xstat_t *buffxstat,
				struct attrlist *fsalattr)
{
	int rc = 0;
	fsalattr->acl = NULL;
	char *acl_key = "user.nfsv4_acls";
	if (NFSv4_ACL_SUPPORT) {
		
		rc = glfs_h_getxattrs(glfs_export->gl_fs,
				      glhandle,
	                    	      acl_key, buffxstat->buffacl,
				      GLFS_ACL_BUF_SIZE);
		// TODO: Return error incase of errors other than
		// ENOTFOUND
		if (rc >= 0) {
			glusterfs_acl_2_fsal_acl(fsalattr,
			    	(glusterfs_acl_t *) buffxstat->buffacl);
		}
		LogFullDebug(COMPONENT_FSAL, "acl = %p", fsalattr->acl);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * Store the Glusterfs ACL using setxattr call.
 */
fsal_status_t glusterfs_set_acl (struct glusterfs_export *glfs_export,
				struct glusterfs_handle *objhandle,
				glusterfs_fsal_xstat_t *buffxstat)
{
	int rc = 0;
	char *acl_key = "user.nfsv4_acls";
	glusterfs_acl_t *acl_p;
	unsigned int acl_total_size = 0 ;

	if (!NFSv4_ACL_SUPPORT ) {
		return fsalstat(ERR_FSAL_ATTRNOTSUPP, 0);
	}

	acl_p = (glusterfs_acl_t *)(buffxstat->buffacl);
	acl_total_size = acl_p->acl_len;
	rc = glfs_h_setxattrs(glfs_export->gl_fs, objhandle->glhandle,
			      acl_key, buffxstat->buffacl,
			      acl_total_size, 0);

	if ( rc < 0 ) {
		// TODO: check if error is appropriate.
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 *  Given a FSAL ACL convert it into GLUSTERFS ACL format. 
 *  Also, compute mode-bits equivalent to the ACL set and
 *  store in st_mode.
 */
fsal_status_t fsal_acl_2_glusterfs_acl(fsal_acl_t *p_fsalacl,
				  char *p_buffacl, uint32_t *st_mode)
{
	int i;
	fsal_ace_t *pace;
	glusterfs_acl_t *p_glusterfsacl;
	
	/* sanity checks */
	if (!p_fsalacl || !p_buffacl || !st_mode)
		return fsalstat(ERR_FSAL_FAULT, 0);

	p_glusterfsacl = (glusterfs_acl_t *) p_buffacl;

	p_glusterfsacl->acl_level = 0;
	p_glusterfsacl->acl_version = GLUSTERFS_ACL_VERSION_NFS4;
	p_glusterfsacl->acl_type = GLUSTERFS_ACL_TYPE_NFS4;
	p_glusterfsacl->acl_nace = p_fsalacl->naces;
	p_glusterfsacl->acl_len =
	    ((int)(signed long)&(((glusterfs_acl_t *) 0)->ace_v1)) +
	    p_glusterfsacl->acl_nace * sizeof(glusterfs_ace_v4_t);

	for (pace = p_fsalacl->aces, i = 0;
	     pace < p_fsalacl->aces + p_fsalacl->naces; pace++, i++) {

		/* check for the unsupported ACE types. */
		if (!(IS_FSAL_ACE_ALLOW(*pace) ||
				 IS_FSAL_ACE_DENY(*pace))) {  
			return fsalstat(ERR_FSAL_ATTRNOTSUPP, 0);
		}

		/*
		 * Check for unsupported ACE flags
		 * Apart from 'ACE4_IDENTIFIER_GROUP', currently we do
		 * not support Inherit and AUDIT/ALARM ACE flags
		 */
		if (GET_FSAL_ACE_FLAG(*pace) & ~ACE4_FLAG_SUPPORTED) {
			return fsalstat(ERR_FSAL_ATTRNOTSUPP, 0);
		}

		p_glusterfsacl->ace_v4[i].aceType = pace->type;
		p_glusterfsacl->ace_v4[i].aceFlags = pace->flag;
		p_glusterfsacl->ace_v4[i].aceIFlags = pace->iflag;
		p_glusterfsacl->ace_v4[i].aceMask = pace->perm;

		if (IS_FSAL_ACE_SPECIAL_ID(*pace)) {
			p_glusterfsacl->ace_v4[i].aceWho = pace->who.uid;
			if (IS_FSAL_ACE_ALLOW(*pace)) 
				CHANGE_MODE_BITS(*pace);
		} else {
			if (IS_FSAL_ACE_GROUP_ID(*pace))
				p_glusterfsacl->ace_v4[i].aceWho = pace->who.gid;
			else
				p_glusterfsacl->ace_v4[i].aceWho = pace->who.uid;
		}

		LogMidDebug(COMPONENT_FSAL,
			 "fsal_acl_2_glusterfs_acl: glusterfs ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
			 p_glusterfsacl->ace_v4[i].aceType,
			 p_glusterfsacl->ace_v4[i].aceFlags,
			 p_glusterfsacl->ace_v4[i].aceMask,
			 (p_glusterfsacl->ace_v4[i].
			  aceIFlags & FSAL_ACE_IFLAG_SPECIAL_ID) ? 1 : 0,
			 (p_glusterfsacl->ace_v4[i].
			  aceFlags & FSAL_ACE_FLAG_GROUP_ID) ? "gid" : "uid",
			 p_glusterfsacl->ace_v4[i].aceWho);

	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 *  Given a GLUSTERFS ACL convert it into FSAL ACL format
 */
int glusterfs_acl_2_fsal_acl(struct attrlist *p_object_attributes,
			       glusterfs_acl_t *p_glusterfsacl)
{
	fsal_acl_status_t status;
	fsal_acl_data_t acldata;
	fsal_ace_t *pace = NULL;
	fsal_acl_t *pacl = NULL;
	glusterfs_ace_v4_t *pace_glusterfs;

	/* sanity checks */
	if (!p_object_attributes || !p_glusterfsacl)
		return ERR_FSAL_FAULT;

	/* Create fsal acl data. */
	acldata.naces = p_glusterfsacl->acl_nace;
	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);

	/* return if ACL not present */
	if (!acldata.naces) { 
		return ERR_FSAL_NO_ERROR;
	}

	/* Fill fsal acl data from glusterfs acl. */
	for (pace = acldata.aces, pace_glusterfs = p_glusterfsacl->ace_v4;
	     pace < acldata.aces + acldata.naces; pace++, pace_glusterfs++) {
		pace->type = pace_glusterfs->aceType;
		pace->flag = pace_glusterfs->aceFlags;
		pace->iflag = pace_glusterfs->aceIFlags;
		pace->perm = pace_glusterfs->aceMask;

		if (IS_FSAL_ACE_SPECIAL_ID(*pace)) { /* Record special user. */
			pace->who.uid = pace_glusterfs->aceWho;
		} else {
			if (IS_FSAL_ACE_GROUP_ID(*pace))
				pace->who.gid = pace_glusterfs->aceWho;
			else	/* Record user. */
				pace->who.uid = pace_glusterfs->aceWho;
		}

		LogMidDebug(COMPONENT_FSAL,
			 "glusterfs_acl_2_fsal_acl: fsal ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
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
			"glusterfs_acl_2_fsal_acl: failed to create a new acl entry");
		return ERR_FSAL_FAULT;
	}

	/* Add fsal acl to attribute. */
	p_object_attributes->acl = pacl;

	return ERR_FSAL_NO_ERROR;
}

/*
 *  Given mode-bits, first verify if the object already has an ACL set. 
 *  Only if there is an ACL present, modify it accordingly as per
 *  the mode-bits set.  
 */
fsal_status_t mode_bits_to_acl(struct glfs *fs, struct glusterfs_handle *objhandle,
			       struct attrlist *attrs, int *attrs_valid, 
			       glusterfs_fsal_xstat_t *buffxstat,
			       bool is_dir)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	int rc = 0;
	char *acl_key = "user.nfsv4_acls";
	glusterfs_acl_t *p_glusterfsacl;
	glusterfs_ace_v4_t *pace_Aowner = NULL;
	glusterfs_ace_v4_t *pace_Downer = NULL;
	glusterfs_ace_v4_t *pace_Agroup = NULL;
	glusterfs_ace_v4_t *pace_Dgroup = NULL;
	glusterfs_ace_v4_t *pace_Aeveryone = NULL;
	glusterfs_ace_v4_t *pace_Deveryone = NULL;
	glusterfs_ace_v4_t *pace = NULL;
	fsal_ace_t face;
	bool read_owner = false;
	bool write_owner = false;
	bool execute_owner = false;
	bool read_group = false;
	bool write_group = false;
	bool execute_group = false;
	bool read_everyone = false;
	bool write_everyone = false;
	bool execute_everyone = false;

	rc = glfs_h_getxattrs(fs, objhandle->glhandle,
		     	      acl_key, buffxstat->buffacl,
			      GLFS_ACL_BUF_SIZE);
	if (rc <= 0) {
		//No ACL found
		// TODO : Do we need to construct aces?
		// TODO: check for failure conditions
		goto out;
	}
	/* there is an existing acl. modify it */
	*attrs_valid |= XATTR_ACL;  

	p_glusterfsacl = (glusterfs_acl_t *)buffxstat->buffacl;

	read_owner = IS_READ_OWNER(attrs->mode); 
	write_owner = IS_WRITE_OWNER(attrs->mode); 
	execute_owner = IS_EXECUTE_OWNER(attrs->mode); 
	read_group = IS_READ_GROUP(attrs->mode); 
	write_group = IS_WRITE_GROUP(attrs->mode); 
	execute_group = IS_EXECUTE_GROUP(attrs->mode); 
	read_everyone = IS_READ_OTHERS(attrs->mode); 
	write_everyone = IS_WRITE_OTHERS(attrs->mode); 
	execute_everyone = IS_EXECUTE_OTHERS(attrs->mode); 
	
	for (pace = p_glusterfsacl->ace_v4; pace < p_glusterfsacl->ace_v4 + p_glusterfsacl->acl_nace; 
	     pace++) {
		/* TODO: try to avoid converting it to FSAL ACE format if poss */
		face.type = pace->aceType;			
		face.flag = pace->aceFlags;			
		face.iflag = pace->aceIFlags;			
		face.who.uid = pace->aceWho;
		
		if (IS_FSAL_ACE_ALLOW(face)) {
			if (IS_FSAL_ACE_SPECIAL_ID(face)) {
				// NULL out the mask
				pace->aceMask = 0;
				if (IS_FSAL_ACE_SPECIAL_OWNER(face)) {
					pace_Aowner = pace;
				} else if(IS_FSAL_ACE_SPECIAL_GROUP(face)) {
					pace_Agroup = pace;
				} else { //everyone
					pace_Aeveryone = pace;
				}
			}
			pace->aceMask |= ACE4_OTHERS_AUTOSET;
		} else { //deny ace
			if (IS_FSAL_ACE_SPECIAL_ID(face)) {
				if (IS_FSAL_ACE_SPECIAL_OWNER(face)) {
					pace_Downer = pace;
				} else if(IS_FSAL_ACE_SPECIAL_GROUP(face)) {
					pace_Dgroup = pace;
				} else { //everyone
					pace_Deveryone = pace;
				}
			}
			pace->aceMask &= ~(ACE4_OTHERS_AUTOSET);
		}	
	}

	/*
	 *  Now add the missing Allow aces (if any) at the end in the order of
	 *  OWNER@, GROUP@, EVERYONE@. Deny aces need not be added as the masks
	 *  will by default be denied if not present in the Allow ace.
	 */
	if (!pace_Aowner) {
		p_glusterfsacl->acl_nace++;
		p_glusterfsacl->acl_len += sizeof(glusterfs_ace_v4_t);
		pace->aceType = FSAL_ACE_TYPE_ALLOW;
		pace->aceIFlags = FSAL_ACE_IFLAG_SPECIAL_ID;
		pace->aceFlags = 0;
		pace->aceMask = 0;
		//No need to set aceFlags as its default ace
		pace->aceWho = FSAL_ACE_SPECIAL_OWNER;
		pace->aceMask |= ACE4_OTHERS_AUTOSET;
		pace_Aowner = pace;
		pace++;
	}
	if (!pace_Agroup) {
		p_glusterfsacl->acl_nace++;
		p_glusterfsacl->acl_len += sizeof(glusterfs_ace_v4_t);
		pace->aceType = FSAL_ACE_TYPE_ALLOW;
		pace->aceIFlags = FSAL_ACE_IFLAG_SPECIAL_ID;
		pace->aceFlags = 0;
		pace->aceMask = 0;
		//No need to set aceFlags as its default ace
		pace->aceWho = FSAL_ACE_SPECIAL_GROUP;
		pace->aceMask |= ACE4_OTHERS_AUTOSET;
		pace_Agroup = pace;
		pace++;
	}
	if (!pace_Aeveryone) {
		p_glusterfsacl->acl_nace++;
		p_glusterfsacl->acl_len += sizeof(glusterfs_ace_v4_t);
		pace->aceType = FSAL_ACE_TYPE_ALLOW;
		pace->aceIFlags = FSAL_ACE_IFLAG_SPECIAL_ID;
		pace->aceFlags = 0;
		pace->aceMask = 0;
		//No need to set aceFlags as its default ace
		pace->aceWho = FSAL_ACE_SPECIAL_EVERYONE;
		pace->aceMask |= ACE4_OTHERS_AUTOSET;
		pace_Aeveryone = pace;
		pace++;
	}

	/* Now adjust perms for special aces */
	if (pace_Aowner) {
		pace_Aowner->aceMask |= ACE4_OWNER_AUTOSET;
		if (read_owner) {
			pace_Aowner->aceMask |= is_dir ? ACE4_READ_DIR_ALL : ACE4_READ_ALL;
		}
		if (write_owner) {
			pace_Aowner->aceMask |= is_dir ? ACE4_WRITE_DIR_ALL : ACE4_WRITE_ALL;
		}
		if (execute_owner) {
			pace_Aowner->aceMask |= ACE4_EXECUTE_ALL;
		}
	}
	if (pace_Downer) {
		pace_Downer->aceMask &= ~(ACE4_OWNER_AUTOSET);
		if (read_owner) {
			pace_Downer->aceMask &= is_dir ? ~(ACE4_READ_DIR_ALL) : ~(ACE4_READ_ALL);
		}
		if (write_owner) {
			pace_Downer->aceMask &= is_dir ? ~(ACE4_WRITE_DIR_ALL) : ~(ACE4_WRITE_ALL);
		}
		if (execute_owner) {
			pace_Downer->aceMask &= is_dir ? : ~(ACE4_EXECUTE_ALL);
		}
	}
	if (pace_Agroup) {
		if (read_group) {
			pace_Agroup->aceMask |= is_dir ? ACE4_READ_DIR_ALL : ACE4_READ_ALL;
		}
		if (write_group) {
			pace_Agroup->aceMask |= is_dir ? ACE4_WRITE_DIR_ALL : ACE4_WRITE_ALL;
		}
		if (execute_group) {
			pace_Agroup->aceMask |= ACE4_EXECUTE_ALL;
		}
	}
	if (pace_Dgroup) {
		if (read_group) {
			pace_Dgroup->aceMask &= is_dir ? ~(ACE4_READ_DIR_ALL) : ~(ACE4_READ_ALL);
		}
		if (write_group) {
			pace_Dgroup->aceMask &= is_dir ? ~(ACE4_WRITE_DIR_ALL) : ~(ACE4_WRITE_ALL);
		}
		if (execute_group) {
			pace_Dgroup->aceMask &= is_dir ? : ~(ACE4_EXECUTE_ALL);
		}
	}
	if (pace_Aeveryone) {
		if (read_everyone) {
			pace_Aeveryone->aceMask |= is_dir ? ACE4_READ_DIR_ALL : ACE4_READ_ALL;
		}
		if (write_everyone) {
			pace_Aeveryone->aceMask |= is_dir ? ACE4_WRITE_DIR_ALL : ACE4_WRITE_ALL;
		}
		if (execute_everyone) {
			pace_Aeveryone->aceMask |= ACE4_EXECUTE_ALL;
		}
	}
	if (pace_Deveryone) {
		if (read_everyone) {
			pace_Deveryone->aceMask &= is_dir ? ~(ACE4_READ_DIR_ALL) : ~(ACE4_READ_ALL);
		}
		if (write_everyone) {
			pace_Deveryone->aceMask &= is_dir ? ~(ACE4_WRITE_DIR_ALL) : ~(ACE4_WRITE_ALL);
		}
		if (execute_everyone) {
			pace_Deveryone->aceMask &= is_dir ? : ~(ACE4_EXECUTE_ALL);
		}
	}

out :
	return status;
}

/*
 *  Process NFSv4 ACLs passed in setattr call
 */
fsal_status_t glusterfs_process_acl(struct glfs *fs,
				    struct glfs_object *object,
			  	    struct attrlist *attrs,
				    glusterfs_fsal_xstat_t *buffxstat)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	uint32_t fsal_mode;

	memset(&buffxstat->buffacl, 0, GLFS_ACL_BUF_SIZE);
	fsal_mode = unix2fsal_mode(buffxstat->buffstat.st_mode);

	if (attrs->acl) {
		LogDebug(COMPONENT_FSAL, "setattr acl = %p",
			 attrs->acl);

		/* Clear owner,group,everyone mode-bits */
		fsal_mode &= CLEAR_MODE_BITS;

		/* Convert FSAL ACL to GLUSTERFS NFS4 ACL and fill buffer. */
		status =
		    fsal_acl_2_glusterfs_acl(attrs->acl,
					buffxstat->buffacl, &fsal_mode);
		buffxstat->buffstat.st_mode = fsal2unix_mode(fsal_mode);

		if (FSAL_IS_ERROR(status))
			return status;
	} else {
		LogCrit(COMPONENT_FSAL, "setattr acl is NULL");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}
	return status;
}

#ifdef GLTIMING
void latency_update(struct timespec *s_time, struct timespec *e_time, int opnum)
{
	atomic_add_uint64_t(&glfsal_latencies[opnum].overall_time,
			    timespec_diff(s_time, e_time));
	atomic_add_uint64_t(&glfsal_latencies[opnum].count, 1);
}

void latency_dump(void)
{
	int i = 0;

	for (; i < LATENCY_SLOTS; i++) {
		LogCrit(COMPONENT_FSAL, "Op:%d:Count:%llu:nsecs:%llu", i,
			(long long unsigned int)glfsal_latencies[i].count,
			(long long unsigned int)glfsal_latencies[i].
			overall_time);
	}
}
#endif
