/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2013
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

#include <fcntl.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "gluster_internal.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_convert.h"

/* fsal_obj_handle common methods
 */

/**
 * @brief Implements GLUSTER FSAL objectoperation handle_release
 *
 * Free up the GLUSTER handle and associated data if any
 * Typically free up any members of the struct glusterfs_handle
 */

static void handle_release(struct fsal_obj_handle *obj_hdl)
{
	int rc = 0;
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	fsal_obj_handle_uninit(&objhandle->handle);

	if (objhandle->glfd) {
		rc = glfs_close(objhandle->glfd);
		if (rc) {
			LogCrit(COMPONENT_FSAL,
				"glfs_close returned %s(%d)",
				strerror(errno), errno);
			/* cleanup as much as possible */
		}
	}

	if (objhandle->glhandle) {
		rc = glfs_h_close(objhandle->glhandle);
		if (rc) {
			LogCrit(COMPONENT_FSAL,
				"glfs_h_close returned error %s(%d)",
				strerror(errno), errno);
		}
	}

	gsh_free(objhandle);

#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_handle_release);
#endif
}

/**
 * @brief Implements GLUSTER FSAL objectoperation lookup
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
        char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle =
	    container_of(parent, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	glhandle =
	    glfs_h_lookupat(glfs_export->gl_fs, parenthandle->glhandle, path,
			    &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs, vol_uuid, GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle, vol_uuid);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*handle = &objhandle->handle;

 out:
	if (status.major != ERR_FSAL_NO_ERROR) {
		gluster_cleanup_vars(glhandle);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_lookup);
#endif

	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation readdir
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t * whence, void *dir_state,
				  fsal_readdir_cb cb, bool * eof)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glfs_fd *glfd = NULL;
	long offset = 0;
	struct dirent *pde = NULL;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle =
	    container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	glfd = glfs_h_opendir(glfs_export->gl_fs, objhandle->glhandle);
	if (glfd == NULL) {
		return gluster2fsal_error(errno);
	}

	if (whence != NULL) {
		offset = *whence;
	}

	glfs_seekdir(glfd, offset);

	while (!(*eof)) {
		struct dirent de;

		rc = glfs_readdir_r(glfd, &de, &pde);
		if (rc == 0 && pde != NULL) {
			/* skip . and .. */
			if ((strcmp(de.d_name, ".") == 0)
			    || (strcmp(de.d_name, "..") == 0)) {
				continue;
			}

			if (!cb(de.d_name, dir_state, glfs_telldir(glfd))) {
				goto out;
			}
		} else if (rc == 0 && pde == NULL) {
			*eof = true;
		} else {
			status = gluster2fsal_error(errno);
			goto out;
		}
	}

 out:
	rc = glfs_closedir(glfd);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_read_dirents);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation create
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
			    const char *name, struct attrlist *attrib,
			    struct fsal_obj_handle **handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
        char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle =
	    container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	rc = setglustercreds(glfs_export, &op_ctx->creds->caller_uid,
			     &op_ctx->creds->caller_gid,
			     op_ctx->creds->caller_glen,
			     op_ctx->creds->caller_garray);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	/* FIXME: what else from attrib should we use? */
	glhandle =
	    glfs_h_creat(glfs_export->gl_fs, parenthandle->glhandle, name,
			 O_CREAT, fsal2unix_mode(attrib->mode), &sb);

	rc = setglustercreds(glfs_export, NULL, NULL, 0, NULL);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs, vol_uuid, GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle, vol_uuid);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*handle = &objhandle->handle;
	*attrib = objhandle->handle.attributes;

 out:
	if (status.major != ERR_FSAL_NO_ERROR) {
		gluster_cleanup_vars(glhandle);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_create);
#endif

	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation mkdir
 */

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
        char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle =
	    container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	rc = setglustercreds(glfs_export, &op_ctx->creds->caller_uid,
			     &op_ctx->creds->caller_gid,
			     op_ctx->creds->caller_glen,
			     op_ctx->creds->caller_garray);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	/* FIXME: what else from attrib should we use? */
	glhandle =
	    glfs_h_mkdir(glfs_export->gl_fs, parenthandle->glhandle, name,
			 fsal2unix_mode(attrib->mode), &sb);

	rc = setglustercreds(glfs_export, NULL, NULL, 0, NULL);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs, vol_uuid, GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle, vol_uuid);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*handle = &objhandle->handle;
	*attrib = objhandle->handle.attributes;

 out:
	if (status.major != ERR_FSAL_NO_ERROR) {
		gluster_cleanup_vars(glhandle);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_makedir);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation mknode
 */

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      fsal_dev_t * dev, struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
        char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	dev_t ndev = { 0, };
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle =
	    container_of(dir_hdl, struct glusterfs_handle, handle);
	mode_t create_mode;
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	switch (nodetype) {
	case BLOCK_FILE:
		if (!dev)
			return fsalstat(ERR_FSAL_INVAL, 0);
		/* FIXME: This needs a feature flag test? */
		ndev = makedev(dev->major, dev->minor);
		create_mode = S_IFBLK;
		break;
	case CHARACTER_FILE:
		if (!dev)
			return fsalstat(ERR_FSAL_INVAL, 0);
		ndev = makedev(dev->major, dev->minor);
		create_mode = S_IFCHR;
		break;
	case FIFO_FILE:
		create_mode = S_IFIFO;
		break;
	case SOCKET_FILE:
		create_mode = S_IFSOCK;
		break;
	default:
		LogMajor(COMPONENT_FSAL, "Invalid node type in FSAL_mknode: %d",
			 nodetype);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	rc = setglustercreds(glfs_export, &op_ctx->creds->caller_uid,
			     &op_ctx->creds->caller_gid,
			     op_ctx->creds->caller_glen,
			     op_ctx->creds->caller_garray);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	/* FIXME: what else from attrib should we use? */
	glhandle =
	    glfs_h_mknod(glfs_export->gl_fs, parenthandle->glhandle, name,
			 create_mode | fsal2unix_mode(attrib->mode), ndev, &sb);

	rc = setglustercreds(glfs_export, NULL, NULL, 0, NULL);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs, vol_uuid, GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle, vol_uuid);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*handle = &objhandle->handle;
	*attrib = objhandle->handle.attributes;

 out:
	if (status.major != ERR_FSAL_NO_ERROR) {
		gluster_cleanup_vars(glhandle);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_makenode);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation symlink
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
        char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle =
	    container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	rc = setglustercreds(glfs_export, &op_ctx->creds->caller_uid,
			     &op_ctx->creds->caller_gid,
			     op_ctx->creds->caller_glen,
			     op_ctx->creds->caller_garray);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	/* FIXME: what else from attrib should we use? */
	glhandle =
	    glfs_h_symlink(glfs_export->gl_fs, parenthandle->glhandle, name,
			   link_path, &sb);

	rc = setglustercreds(glfs_export, NULL, NULL, 0, NULL);
	if (rc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs, vol_uuid, GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle, vol_uuid);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*handle = &objhandle->handle;
	*attrib = objhandle->handle.attributes;

 out:
	if (status.major != ERR_FSAL_NO_ERROR) {
		gluster_cleanup_vars(glhandle);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_makesymlink);
#endif

	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation readlink
 */

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	link_content->len = 1024;	// bad bad!!! need to determine size
	link_content->addr = gsh_malloc(link_content->len);
	if (link_content->addr == NULL) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = glfs_h_readlink(glfs_export->gl_fs, objhandle->glhandle,
			     link_content->addr, link_content->len);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	/* Check if return buffer overflowed, it is still '\0' terminated */
	link_content->len = (strlen(link_content->addr) + 1);

 out:
	if (status.major != ERR_FSAL_NO_ERROR) {
		gsh_free(link_content->addr);
		link_content->addr = NULL;
		link_content->len = 0;
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_readsymlink);
#endif

	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation getattrs
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	glusterfs_fsal_xstat_t buffxstat;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
	struct attrlist *fsalattr;
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	/* FIXME: Should we hold the fd so that any async op does
	 * not close it */
	if (objhandle->openflags != FSAL_O_CLOSED) {
		rc = glfs_fstat(objhandle->glfd, &buffxstat.buffstat);
	} else {
		rc = glfs_h_stat(glfs_export->gl_fs, objhandle->glhandle, &buffxstat.buffstat);
	}

	if (rc != 0) {
		if (errno == ENOENT)
			status = gluster2fsal_error(ESTALE);
		else
			status = gluster2fsal_error(errno);

		goto out;
	}

	fsalattr = &objhandle->handle.attributes;
	stat2fsal_attributes(&buffxstat.buffstat, fsalattr);

	status = glusterfs_get_acl(glfs_export, objhandle->glhandle,
				   &buffxstat, fsalattr);

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_getattrs);
#endif

	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation setattrs
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	glusterfs_fsal_xstat_t buffxstat;
	int mask = 0;
	int attr_valid = 0;
	bool is_dir = 0;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif
	memset(&buffxstat, 0, sizeof(glusterfs_fsal_xstat_t));

	/* sanity checks.
	 * note : object_attributes is optional.
	 */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		rc = glfs_h_truncate(glfs_export->gl_fs, objhandle->glhandle,
				     attrs->filesize);
		if (rc != 0) {
			status = gluster2fsal_error(errno);
			goto out;
		}
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		mask |= GLAPI_SET_ATTR_MODE;
		buffxstat.buffstat.st_mode = fsal2unix_mode(attrs->mode);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)) {
		mask |= GLAPI_SET_ATTR_UID;
		buffxstat.buffstat.st_uid = attrs->owner;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)) {
		mask |= GLAPI_SET_ATTR_GID;
		buffxstat.buffstat.st_gid = attrs->group;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
		mask |= GLAPI_SET_ATTR_ATIME;
		buffxstat.buffstat.st_atim = attrs->atime;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
		mask |= GLAPI_SET_ATTR_ATIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			status = gluster2fsal_error(errno);
			goto out;
		}
		buffxstat.buffstat.st_atim = timestamp;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
		mask |= GLAPI_SET_ATTR_MTIME;
		buffxstat.buffstat.st_mtim = attrs->mtime;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
		mask |= GLAPI_SET_ATTR_MTIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			status = gluster2fsal_error(rc);
			goto out;
		}
		buffxstat.buffstat.st_mtim = timestamp;
	}

	// TODO: Check for attributes not supported and return
	// EATTRNOTSUPP error.
	
	if (NFSv4_ACL_SUPPORT) {
		if (FSAL_TEST_MASK(attrs->mask, ATTR_ACL)) {
			attr_valid |= XATTR_ACL;
			status = 
			  glusterfs_process_acl(glfs_export->gl_fs,
					        objhandle->glhandle,
						attrs, &buffxstat);

			if (FSAL_IS_ERROR(status))
				goto out;
			/* setting the ACL will set the mode-bits too if not already passed */
			mask |= GLAPI_SET_ATTR_MODE;
		} else if (mask & GLAPI_SET_ATTR_MODE) {
			switch (obj_hdl->type) {
				case REGULAR_FILE:
					is_dir = 0; break;
				case DIRECTORY:
					is_dir = 1; break;
				default :
					break;
			}
			status =
			 mode_bits_to_acl(glfs_export->gl_fs, objhandle,
					  attrs, &attr_valid,
					  &buffxstat, is_dir);

			if (FSAL_IS_ERROR(status))
				goto out;
		}
	} else if (FSAL_TEST_MASK(attrs->mask, ATTR_ACL)) { 
		status = fsalstat(ERR_FSAL_ATTRNOTSUPP, 0);
		goto out;
	}

	/* If any stat changed, indicate that */
	if (mask != 0) {
		attr_valid |= XATTR_STAT;
	}

	if (attr_valid & XATTR_STAT) {
		// Only if there is any change in attributes send them down to fs
		rc = glfs_h_setattrs(glfs_export->gl_fs,
				     objhandle->glhandle,
				     &buffxstat.buffstat,
				     mask);
		GLUSTER_VALIDATE_RETURN_STATUS (rc);
	}

	if (attr_valid & XATTR_ACL) {
		status = glusterfs_set_acl(glfs_export,
				           objhandle, &buffxstat);
		if (FSAL_IS_ERROR(status))
			goto out;
	}
out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_setattrs);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation link
 */

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	int rc = 0, credrc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
	struct glusterfs_handle *dstparenthandle =
	    container_of(destdir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	credrc =
	    setglustercreds(glfs_export, &op_ctx->creds->caller_uid,
			    &op_ctx->creds->caller_gid,
			    op_ctx->creds->caller_glen,
			    op_ctx->creds->caller_garray);
	if (credrc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	rc = glfs_h_link(glfs_export->gl_fs, objhandle->glhandle,
			 dstparenthandle->glhandle, name);

	credrc = setglustercreds(glfs_export, NULL, NULL, 0, NULL);
	if (credrc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_linkfile);
#endif

	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation rename
 */

static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	int rc = 0, credrc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export,
			 export);
	struct glusterfs_handle *srcparenthandle =
	    container_of(olddir_hdl, struct glusterfs_handle, handle);
	struct glusterfs_handle *dstparenthandle =
	    container_of(newdir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	credrc =
	    setglustercreds(glfs_export, &op_ctx->creds->caller_uid,
			    &op_ctx->creds->caller_gid,
			    op_ctx->creds->caller_glen,
			    op_ctx->creds->caller_garray);
	if (credrc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	rc = glfs_h_rename(glfs_export->gl_fs, srcparenthandle->glhandle,
			   old_name, dstparenthandle->glhandle, new_name);

	credrc = setglustercreds(glfs_export, NULL, NULL, 0, NULL);
	if (credrc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_renamefile);
#endif

	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation unlink
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	int rc = 0, credrc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle =
	    container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	credrc =
	    setglustercreds(glfs_export, &op_ctx->creds->caller_uid,
			    &op_ctx->creds->caller_gid,
			    op_ctx->creds->caller_glen,
			    op_ctx->creds->caller_garray);
	if (credrc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	rc = glfs_h_unlink(glfs_export->gl_fs, parenthandle->glhandle, name);

	credrc = setglustercreds(glfs_export, NULL, NULL, 0, NULL);
	if (credrc != 0) {
		status = gluster2fsal_error(EPERM);
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
		goto out;
	}

	if (rc != 0) {
		status = gluster2fsal_error(errno);
	}

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_file_unlink);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation open
 */

static fsal_status_t file_open(struct fsal_obj_handle *obj_hdl,
			       fsal_openflags_t openflags)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glfs_fd *glfd = NULL;
	int p_flags = 0;
	struct glusterfs_export *glfs_export =
	    container_of(op_ctx->fsal_export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	if (objhandle->openflags != FSAL_O_CLOSED) {
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	rc = fsal2posix_openflags(openflags, &p_flags);
	if (rc != 0) {
		status.major = rc;
		goto out;
	}

	glfd = glfs_h_open(glfs_export->gl_fs, objhandle->glhandle, p_flags);
	if (glfd == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	objhandle->openflags = openflags;
	objhandle->glfd = glfd;

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_file_open);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation status
 */

static fsal_openflags_t file_status(struct fsal_obj_handle *obj_hdl)
{
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);

	return objhandle->openflags;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation read
 */

static fsal_status_t file_read(struct fsal_obj_handle *obj_hdl,
			       uint64_t seek_descriptor, size_t buffer_size,
			       void *buffer, size_t * read_amount,
			       bool * end_of_file)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	rc = glfs_pread(objhandle->glfd, buffer, buffer_size, seek_descriptor,
			0 /*TODO: flags is unused, so pass in something */ );
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	if (rc < buffer_size) {
		*end_of_file = true;
	}

	*read_amount = rc;

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_file_read);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation write
 */

static fsal_status_t file_write(struct fsal_obj_handle *obj_hdl,
				uint64_t seek_descriptor, size_t buffer_size,
				void *buffer, size_t * write_amount,
				bool * fsal_stable)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	rc = glfs_pwrite(objhandle->glfd, buffer, buffer_size, seek_descriptor,
			 ((*fsal_stable) ? O_SYNC : 0));
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	*write_amount = rc;
	if (objhandle->openflags & FSAL_O_SYNC)
		*fsal_stable = true;

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_file_write);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation commit
 *
 * This function commits the entire file and ignores the range provided
 */

static fsal_status_t commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	/* TODO: Everybody pretty much ignores the range sent */
	rc = glfs_fsync(objhandle->glfd);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_commit);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation lock_op
 *
 * The lock operations do not yet support blocking locks, as cancel is probably
 * needed and the current implementation would block a thread that seems
 * excessive.
 */

static fsal_status_t lock_op(struct fsal_obj_handle *obj_hdl,
			     void * p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
	struct flock flock;
	int cmd;
	int saverrno = 0;
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	if (objhandle->openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Attempting to lock with no file descriptor open");
		status.major = ERR_FSAL_FAULT;
		goto out;
	}

	if (lock_op == FSAL_OP_LOCKT) {
		cmd = F_GETLK;
	} else if (lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_UNLOCK) {
		cmd = F_SETLK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Unsupported lock operation %d\n", lock_op);
		status.major = ERR_FSAL_NOTSUPP;
		goto out;
	}

	if (request_lock->lock_type == FSAL_LOCK_R) {
		flock.l_type = F_RDLCK;
	} else if (request_lock->lock_type == FSAL_LOCK_W) {
		flock.l_type = F_WRLCK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		status.major = ERR_FSAL_NOTSUPP;
		goto out;
	}

	/* TODO: Override R/W and just provide U? */
	if (lock_op == FSAL_OP_UNLOCK)
		flock.l_type = F_UNLCK;

	flock.l_len = request_lock->lock_length;
	flock.l_start = request_lock->lock_start;
	flock.l_whence = SEEK_SET;

	rc = glfs_posix_lock (objhandle->glfd, cmd, &flock);
	if (rc != 0 && lock_op == FSAL_OP_LOCK
	    && conflicting_lock && (errno == EACCES || errno == EAGAIN)) {
		/* process conflicting lock */
		saverrno = errno;
		cmd = F_GETLK;
		rc = glfs_posix_lock (objhandle->glfd, cmd, &flock);
		if (rc) {
			LogCrit(COMPONENT_FSAL,
				"Failed to get conflicting lock post lock"
				" failure");
			status = gluster2fsal_error(errno);
			goto out;
		}

		conflicting_lock->lock_length = flock.l_len;
		conflicting_lock->lock_start = flock.l_start;
		conflicting_lock->lock_type = flock.l_type;

		status = gluster2fsal_error(saverrno);
		goto out;
	} else if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	if (conflicting_lock != NULL) {
		if (lock_op == FSAL_OP_LOCKT && flock.l_type != F_UNLCK) {
			conflicting_lock->lock_length = flock.l_len;
			conflicting_lock->lock_start = flock.l_start;
			conflicting_lock->lock_type = flock.l_type;
		} else {
			conflicting_lock->lock_length = 0;
			conflicting_lock->lock_start = 0;
			conflicting_lock->lock_type = FSAL_NO_LOCK;
		}
	}

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_lock_op);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation share_op
 */
/*
static fsal_status_t share_op(struct fsal_obj_handle *obj_hdl,
			      void *p_owner,
			      fsal_share_param_t  request_share)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation close
 */

static fsal_status_t file_close(struct fsal_obj_handle *obj_hdl)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	rc = glfs_close(objhandle->glfd);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	objhandle->glfd = NULL;
	objhandle->openflags = FSAL_O_CLOSED;

 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_file_close);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation list_ext_attrs
 */
/*
static fsal_status_t list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    const struct req_op_context *opctx,
				    unsigned int cookie,
				    fsal_xattrent_t * xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation getextattr_id_by_name
 */
/*
static fsal_status_t getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const struct req_op_context *opctx,
					   const char *xattr_name,
					   unsigned int *pxattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation getextattr_value_by_name
 */
/*
static fsal_status_t getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const struct req_op_context *opctx,
					      const char *xattr_name,
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t * p_output_size)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation getextattr_value_by_id
 */
/*
static fsal_status_t getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    const struct req_op_context *opctx,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation setextattr_value
 */
/*
static fsal_status_t setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const struct req_op_context *opctx,
				      const char *xattr_name,
				      caddr_t buffer_addr,
				      size_t buffer_size,
				      int create)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation setextattr_value_by_id
 */
/*
static fsal_status_t setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    const struct req_op_context *opctx,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation getextattr_attrs
 */
/*
static fsal_status_t getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				      const struct req_op_context *opctx,
				      unsigned int xattr_id,
				      struct attrlist* p_attrs)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation remove_extattr_by_id
 */
/*
static fsal_status_t remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  const struct req_op_context *opctx,
					  unsigned int xattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation remove_extattr_by_name
 */
/*
static fsal_status_t remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const struct req_op_context *opctx,
					    const char *xattr_name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/**
 * @brief Implements GLUSTER FSAL objectoperation lru_cleanup
 *
 * For now this function closed the fd if open as a part of the lru_cleanup.
 */

fsal_status_t lru_cleanup(struct fsal_obj_handle * obj_hdl,
			  lru_actions_t requests)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_handle *objhandle =
	    container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	if (objhandle->glfd != NULL) {
		status = file_close(obj_hdl);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_lru_cleanup);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation handle_digest
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   fsal_digesttype_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	size_t fh_size;
	struct glusterfs_handle *objhandle;
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	if (!fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);

	objhandle = container_of(obj_hdl, struct glusterfs_handle, handle);

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = GLAPI_HANDLE_LENGTH;
		if (fh_desc->len < fh_size) {
			LogMajor(COMPONENT_FSAL,
				 "Space too small for handle.  need %lu, have %lu",
				 fh_size, fh_desc->len);
			status.major = ERR_FSAL_TOOSMALL;
			goto out;
		}
		memcpy(fh_desc->addr, objhandle->globjhdl, fh_size);
		break;
	default:
		status.major = ERR_FSAL_SERVERFAULT;
		goto out;
	}

	fh_desc->len = fh_size;
 out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_handle_digest);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL objectoperation handle_to_key
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	struct glusterfs_handle *objhandle;
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	objhandle = container_of(obj_hdl, struct glusterfs_handle, handle);
	fh_desc->addr = objhandle->globjhdl;
	fh_desc->len = GLAPI_HANDLE_LENGTH;

#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_handle_to_key);
#endif

	return;
}

/**
 * @brief Registers GLUSTER FSAL objectoperation vector
 */

void handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = handle_release;
	ops->lookup = lookup;
	ops->create = create;
	ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->readdir = read_dirents;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->open = file_open;
	ops->status = file_status;
	ops->read = file_read;
	ops->write = file_write;
	ops->commit = commit;
	ops->lock_op = lock_op;
	ops->close = file_close;
	ops->lru_cleanup = lru_cleanup;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
}
