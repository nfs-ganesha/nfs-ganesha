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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#include <sys/types.h>
#include "os/xattr.h"
#include "gluster_internal.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include "nfs4_acls.h"
#include "FSAL/fsal_commonlib.h"
#include "posix_acls.h"

/**
 * @brief FSAL status mapping from GlusterFS errors
 *
 * This function returns a fsal_status_t with the FSAL error as the
 * major, and the posix error as minor. Please note that this routine
 * needs to be used only in case of failures.
 *
 * @param[in] gluster_errorcode Gluster error
 *
 * @return FSAL status.
 */

fsal_status_t gluster2fsal_error(const int err)
{
	fsal_status_t status;
	int g_err = err;

	if (!g_err) {
		LogWarn(COMPONENT_FSAL, "appropriate errno not set");
		g_err = EINVAL;
	}
	status.minor = g_err;
	status.major = posix2fsal_error(g_err);

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
	/* Indicate which atrributes we have set without affecting the
	 * other bits in the mask.
	 */
	fsalattr->valid_mask |= ATTRS_POSIX;
	fsalattr->supported = op_ctx->fsal_export->exp_ops.fs_supported_attrs(
							op_ctx->fsal_export);

	/* Fills the output struct */
	fsalattr->type = posix2fsal_type(buffstat->st_mode);

	fsalattr->filesize = buffstat->st_size;

	fsalattr->fsid = posix2fsal_fsid(buffstat->st_dev);

	fsalattr->fileid = buffstat->st_ino;

	fsalattr->mode = unix2fsal_mode(buffstat->st_mode);

	fsalattr->numlinks = buffstat->st_nlink;

	fsalattr->owner = buffstat->st_uid;

	fsalattr->group = buffstat->st_gid;

	/** @todo: gfapi currently only fills in the legacy time_t fields
	 *         when it supports the timespec fields calls to this
	 *         function should be replaced with calls to
	 *         posix2fsal_attributes rather than changing this code.
	 */
	fsalattr->atime = posix2fsal_time(buffstat->st_atime, 0);
	fsalattr->ctime = posix2fsal_time(buffstat->st_ctime, 0);
	fsalattr->mtime = posix2fsal_time(buffstat->st_mtime, 0);

	fsalattr->chgtime = posix2fsal_time(MAX(buffstat->st_mtime,
						buffstat->st_ctime), 0);
	fsalattr->change = fsalattr->chgtime.tv_sec;

	fsalattr->spaceused = buffstat->st_blocks * S_BLKSIZE;

	fsalattr->rawdev = posix2fsal_devt(buffstat->st_rdev);
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

void construct_handle(struct glusterfs_export *glexport, const struct stat *st,
		      struct glfs_object *glhandle, unsigned char *globjhdl,
		      struct glusterfs_handle **obj, const char *vol_uuid)
{
	struct glusterfs_handle *constructing = NULL;

	constructing = gsh_calloc(1, sizeof(struct glusterfs_handle));

	constructing->glhandle = glhandle;
	memcpy(constructing->globjhdl, vol_uuid, GLAPI_UUID_LENGTH);
	memcpy(constructing->globjhdl+GLAPI_UUID_LENGTH, globjhdl,
	       GFAPI_HANDLE_LENGTH);
	constructing->globalfd.glfd = NULL;

	fsal_obj_handle_init(&constructing->handle, &glexport->export,
			     posix2fsal_type(st->st_mode));
	constructing->handle.fsid = posix2fsal_fsid(st->st_dev);
	constructing->handle.fileid = st->st_ino;
	constructing->handle.obj_ops = &GlusterFS.handle_ops;

	*obj = constructing;
}

void gluster_cleanup_vars(struct glfs_object *glhandle)
{
	if (glhandle) {
		/* Error ignored, this is a cleanup operation, can't do much. */
		/** @todo: Useful point for logging? */
		glfs_h_close(glhandle);
	}
}

/* fs_specific_has() parses the fs_specific string for a particular key,
 * returns true if found, and optionally returns a val if the string is
 * of the form key=val.
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

	for (option = strtok_r(fso_dup, ",", &next_comma); option;
	     option = strtok_r(NULL, ",", &next_comma)) {
		char *k = option;
		char *v = k;

		strsep(&v, "=");
		if (strcmp(k, key) == 0) {
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

void setglustercreds(struct glusterfs_export *glfs_export, uid_t *uid,
		     gid_t *gid, unsigned int ngrps, gid_t *groups,
		     char *file, int line, char *function)
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

	if (ngrps != 0 && groups)
		rc = glfs_setfsgroups(ngrps, groups);
	else
		rc = glfs_setfsgroups(0, NULL);

 out:
	if (rc != 0) {
		DisplayLogComponentLevel(COMPONENT_FSAL, file, line, function,
			 NIV_FATAL,
			"Could not set Gluster credentials - uid(%d), gid(%d)",
			*uid, *gid);
	}
}

/*
 * Read the ACL in GlusterFS format and convert it into fsal ACL before
 * storing it in fsalattr
 */
fsal_status_t glusterfs_get_acl(struct glusterfs_export *glfs_export,
				struct glfs_object *glhandle,
				glusterfs_fsal_xstat_t *buffxstat,
				struct attrlist *fsalattr)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	fsal_acl_data_t acldata;
	fsal_acl_status_t aclstatus;
	fsal_ace_t *pace = NULL;
	int e_count = 0, i_count = 0, new_count = 0, new_i_count = 0;

	if (fsalattr->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's release the
		 * old ACL properly.
		 */
		int acl_status;

		acl_status = nfs4_acl_release_entry(fsalattr->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);

		fsalattr->acl = NULL;
	}

	if (NFSv4_ACL_SUPPORT) {

		buffxstat->e_acl = glfs_h_acl_get(glfs_export->gl_fs->fs,
						glhandle, ACL_TYPE_ACCESS);

		if (!buffxstat->e_acl) {
			status = gluster2fsal_error(errno);
			return status;
		}

		e_count = ace_count(buffxstat->e_acl);

		if (buffxstat->is_dir) {
			buffxstat->i_acl =
					 glfs_h_acl_get(glfs_export->gl_fs->fs,
						glhandle, ACL_TYPE_DEFAULT);
			i_count = ace_count(buffxstat->i_acl);
		}

		/* Allocating memory for both ALLOW and DENY entries */
		acldata.naces = 2 * (e_count  + i_count);

		LogDebug(COMPONENT_FSAL, "No of aces present in fsal_acl_t = %d"
					, acldata.naces);
		if (!acldata.naces)
			return status;

		FSAL_SET_MASK(buffxstat->attr_valid, XATTR_ACL);

		acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);
		pace = acldata.aces;

		new_count = posix_acl_2_fsal_acl(buffxstat->e_acl,
					buffxstat->is_dir, false, &pace);
		if (new_count < 0)
			return fsalstat(ERR_FSAL_NO_ACE, -1);

		if (i_count > 0) {
			new_i_count = posix_acl_2_fsal_acl(buffxstat->i_acl,
							true, true, &pace);
			if (new_i_count > 0)
				new_count += new_i_count;
			else
				LogDebug(COMPONENT_FSAL,
				"Inherit acl is not set for this directory");
		}

		/* Reallocating acldata into the required size */
		acldata.aces = (fsal_ace_t *) gsh_realloc(acldata.aces,
				new_count*sizeof(fsal_ace_t));
		acldata.naces = new_count;

		fsalattr->acl = nfs4_acl_new_entry(&acldata, &aclstatus);
		LogDebug(COMPONENT_FSAL, "fsal acl = %p, fsal_acl_status = %u",
				fsalattr->acl, aclstatus);
		if (fsalattr->acl == NULL) {
			LogCrit(COMPONENT_FSAL,
			"failed to create a new acl entry");
			return fsalstat(ERR_FSAL_NOMEM, -1);
		}

		fsalattr->valid_mask |= ATTR_ACL;
	} else {
		/* We were asked for ACL but do not support. */
		status = fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	return status;

}

/*
 * Store the Glusterfs ACL using setxattr call.
 */
fsal_status_t glusterfs_set_acl(struct glusterfs_export *glfs_export,
				struct glusterfs_handle *objhandle,
				glusterfs_fsal_xstat_t *buffxstat)
{
	int rc = 0;

	rc = glfs_h_acl_set(glfs_export->gl_fs->fs, objhandle->glhandle,
				ACL_TYPE_ACCESS, buffxstat->e_acl);
	if (rc < 0) {
		/** @todo: check if error is appropriate.*/
		LogMajor(COMPONENT_FSAL, "failed to set access type posix acl");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	/* For directories consider inherited acl too */
	if (buffxstat->is_dir && buffxstat->i_acl) {
		rc = glfs_h_acl_set(glfs_export->gl_fs->fs, objhandle->glhandle,
				ACL_TYPE_DEFAULT, buffxstat->i_acl);
		if (rc < 0) {
			LogMajor(COMPONENT_FSAL,
				 "failed to set default type posix acl");
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 *  Process NFSv4 ACLs passed in setattr call
 */
fsal_status_t glusterfs_process_acl(struct glfs *fs,
				    struct glfs_object *object,
				    struct attrlist *attrs,
				    glusterfs_fsal_xstat_t *buffxstat)
{
	if (attrs->acl) {
		LogDebug(COMPONENT_FSAL, "setattr acl = %p",
			 attrs->acl);

		/* Convert FSAL ACL to POSIX ACL */
		buffxstat->e_acl = fsal_acl_2_posix_acl(attrs->acl,
						ACL_TYPE_ACCESS);
		if (!buffxstat->e_acl) {
			LogMajor(COMPONENT_FSAL,
				 "failed to set access type posix acl");
			return fsalstat(ERR_FSAL_FAULT, 0);
		}
		/* For directories consider inherited acl too */
		if (buffxstat->is_dir) {
			buffxstat->i_acl = fsal_acl_2_posix_acl(attrs->acl,
							ACL_TYPE_DEFAULT);
			if (!buffxstat->i_acl)
				LogDebug(COMPONENT_FSAL,
				"inherited acl is not defined for directory");
		}

	} else {
		LogCrit(COMPONENT_FSAL, "setattr acl is NULL");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

int initiate_up_thread(struct glusterfs_fs *gl_fs)
{

	pthread_attr_t up_thr_attr;
	int retval  = -1;
	int err   = 0;
	int retries = 10;

	memset(&up_thr_attr, 0, sizeof(up_thr_attr));

	/* Initialization of thread attributes from nfs_init.c */
	err = pthread_attr_init(&up_thr_attr);
	if (err) {
		LogCrit(COMPONENT_THREAD,
			"can't init pthread's attributes (%s)",
			strerror(err));
		goto out;
	}

	err = pthread_attr_setscope(&up_thr_attr,
				      PTHREAD_SCOPE_SYSTEM);
	if (err) {
		LogCrit(COMPONENT_THREAD,
			"can't set pthread's scope (%s)",
			strerror(err));
		goto out;
	}

	err = pthread_attr_setdetachstate(&up_thr_attr,
					  PTHREAD_CREATE_JOINABLE);
	if (err) {
		LogCrit(COMPONENT_THREAD,
			"can't set pthread's join state (%s)",
			strerror(err));
		goto out;
	}

	err = pthread_attr_setstacksize(&up_thr_attr, 2116488);
	if (err) {
		LogCrit(COMPONENT_THREAD,
			"can't set pthread's stack size (%s)",
			strerror(err));
		goto out;
	}

	do {
		err = pthread_create(&gl_fs->up_thread,
				     &up_thr_attr,
				     GLUSTERFSAL_UP_Thread,
				     gl_fs);
		sleep(1);
	} while (err && (err == EAGAIN) && (retries-- > 0));

	if (err) {
		LogCrit(COMPONENT_THREAD,
			"can't create upcall pthread (%s)",
			strerror(err));
		goto out;
	}

	retval = 0;

out:
	err = pthread_attr_destroy(&up_thr_attr);
	if (err) {
		LogCrit(COMPONENT_THREAD,
			"can't destroy pthread's attributes (%s)",
			strerror(err));
	}

	return retval;
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
		LogCrit(COMPONENT_FSAL, "Op:%d:Count:%"PRIu64":nsecs:%"PRIu64,
			i, glfsal_latencies[i].count,
			glfsal_latencies[i].overall_time);
	}
}
#endif
