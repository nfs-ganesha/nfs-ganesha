/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_convert.h"

/* fsal_obj_handle common methods
 */

/* handle_release
 * default case is to throw a fault error.
 * creating an handle is not supported so getting here is bad
 */

static fsal_status_t handle_release(struct fsal_obj_handle *obj_hdl)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	rc = fsal_obj_handle_uninit(&objhandle->handle);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	if (objhandle->glfd) {
		rc = glfs_close(objhandle->glfd);
		if (rc) {
			status = gluster2fsal_error(errno);
			/* FIXME: cleanup as much as possible, or error out? */
			goto out;
		}
	}

	if (objhandle->glhandle) {
		rc = glfs_h_close(objhandle->glhandle);
		if (rc) {
			status = gluster2fsal_error(errno);
			goto out;
		}
	}

	gsh_free(objhandle);

out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_handle_release);
#endif

	return status;
}

/* lookup
 * default case not supported
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const struct req_op_context *opctx,
			    const char *path,
			    struct fsal_obj_handle **handle)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct stat              sb;
	struct glfs_object      *glhandle = NULL;
	unsigned char            globjhdl[GLAPI_HANDLE_LENGTH];
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export = 
		container_of(parent->export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle = 
		container_of(parent, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	glhandle = glfs_h_lookupat(glfs_export->gl_fs, parenthandle->glhandle, 
				   path, &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GLAPI_HANDLE_LENGTH);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle);
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

/* read_dirents
 * default case not supported
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  const struct req_op_context *opctx,
				  fsal_cookie_t *whence,
				  void *dir_state,
				  fsal_readdir_cb cb,
				  bool *eof)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glfs_fd          *glfd = NULL;
	long                     offset = 0;
	struct dirent           *pde = NULL;
	struct glusterfs_export *glfs_export = 
		container_of(dir_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle = 
		container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

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
			if((strcmp(de.d_name, ".") == 0) || 
				 (strcmp(de.d_name, "..") == 0)) {
				continue;
			}

			if (!cb(opctx, de.d_name, dir_state, 
				 glfs_telldir(glfd))) {
				goto out;
			}
		} else if (rc == 0 && pde == NULL) {
			*eof = true;
		} else if (rc != 0) {
			status = gluster2fsal_error(errno);
			goto out;
		} else {
			/* Can't happen */
			abort();
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

/* create
 * default case not supported
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
			    const struct req_op_context *opctx,
			    const char *name,
			    struct attrlist *attrib,
			    struct fsal_obj_handle **handle)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct stat              sb;
	struct glfs_object      *glhandle = NULL;
	unsigned char            globjhdl[GLAPI_HANDLE_LENGTH];
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export = 
		container_of(dir_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle = 
		container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;
		
	now(&s_time);
#endif

	/* FIXME: Is it enough to convert the mode as is? 
	 * what else from attrib should we use? */
	glhandle = glfs_h_creat(glfs_export->gl_fs, parenthandle->glhandle, 
				name, O_CREAT, fsal2unix_mode(attrib->mode), 
				&sb);

	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GLAPI_HANDLE_LENGTH);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl, 
			      GLAPI_HANDLE_LENGTH, &objhandle);
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

/* makedir
 * default case not supported
 */

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const struct req_op_context *opctx,
			     const char *name,
			     struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct stat              sb;
	struct glfs_object      *glhandle = NULL;
	unsigned char            globjhdl[GLAPI_HANDLE_LENGTH];
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export = 
		container_of(dir_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle = 
		container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	/* FIXME: We need to handle the UID/GID for creat */
	/* FIXME: Is it enough to convert the mode as is? 
	 * what else from attrib should we use? */
	glhandle = glfs_h_mkdir(glfs_export->gl_fs, parenthandle->glhandle, 
				name, fsal2unix_mode(attrib->mode), &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GLAPI_HANDLE_LENGTH);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle);
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

/* makenode
 * default case not supported
 */

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const struct req_op_context *opctx,
			      const char *name,
			      object_file_type_t nodetype,
			      fsal_dev_t *dev,
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct stat              sb;
	struct glfs_object      *glhandle = NULL;
	unsigned char            globjhdl[GLAPI_HANDLE_LENGTH];
	struct glusterfs_handle *objhandle = NULL;
	dev_t                    ndev = {0, };
	struct glusterfs_export *glfs_export = 
		container_of(dir_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle = 
		container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	switch (nodetype) {
		case BLOCK_FILE:
			if (!dev)
				return fsalstat (ERR_FSAL_INVAL, 0);
			/* FIXME: This needs a feature flag test? */
			ndev = makedev(dev->major, dev->minor);
			break;
		case CHARACTER_FILE:
			if (!dev)
				return fsalstat (ERR_FSAL_INVAL, 0);
			ndev = makedev(dev->major, dev->minor);
			break;
		case FIFO_FILE:
		case SOCKET_FILE:
			break;
		default:
			LogMajor (COMPONENT_FSAL, 
				  "Invalid node type in FSAL_mknode: %d", 
				  nodetype);
			return fsalstat (ERR_FSAL_INVAL, 0);
	}

	/* FIXME: We need to handle the UID/GID for creat */
	/* FIXME: Is it enough to convert the mode as is? 
	 * what else from attrib should we use? */
	glhandle = glfs_h_mknod(glfs_export->gl_fs, parenthandle->glhandle, 
				name, fsal2unix_mode(attrib->mode), ndev, &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GLAPI_HANDLE_LENGTH);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle);
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

/* makesymlink
 * default case not supported
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const struct req_op_context *opctx,
				 const char *name,
				 const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle)
{
#ifdef GLTIMING
	latency_dump();
#endif
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* readsymlink
 * default case not supported
 */

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 const struct req_op_context *opctx,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getattrs
 * default case not supported
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			      const struct req_op_context *opctx)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct stat              sb;
	struct glusterfs_export *glfs_export = 
		container_of(obj_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	rc = glfs_h_stat(glfs_export->gl_fs, objhandle->glhandle, &sb);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	stat2fsal_attributes(&sb, &objhandle->handle.attributes);

out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_getattrs);
#endif

	return status;
}

/* setattrs
 * default case not supported
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      const struct req_op_context *opctx,
			      struct attrlist *attrs)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct stat              sb;
	int                      mask = 0;
	struct glusterfs_export *glfs_export = 
		container_of(obj_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	memset(&sb, 0, sizeof(struct stat));

	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		/* TODO: we need a glfs_h_truncate */
	}
	
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		mask |= GLAPI_SET_ATTR_MODE;
		sb.st_mode = fsal2unix_mode(attrs->mode);
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)) {
		mask |= GLAPI_SET_ATTR_UID;
		sb.st_uid = attrs->owner;
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)) {
		mask |= GLAPI_SET_ATTR_GID;
		sb.st_gid = attrs->group;
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
		mask |= GLAPI_SET_ATTR_ATIME;
		sb.st_atim = attrs->atime;
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
		mask |= GLAPI_SET_ATTR_ATIME;
		struct timespec timestamp;
		
		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if(rc != 0) {
			status = gluster2fsal_error(errno);
			goto out;
		}
		sb.st_atim = timestamp;
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
		mask |= GLAPI_SET_ATTR_MTIME;
		sb.st_mtim = attrs->mtime;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
		mask |= GLAPI_SET_ATTR_MTIME;
		struct timespec timestamp;
		
		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if(rc != 0) {
			status = gluster2fsal_error(rc);
			goto out;
		}
		sb.st_mtim = timestamp;
	}

	rc = glfs_h_setattrs(glfs_export->gl_fs, objhandle->glhandle, &sb, 
			     mask);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_setattrs);
#endif
	return status;
}

/* linkfile
 * default case not supported
 */

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      const struct req_op_context *opctx,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* renamefile
 * default case not supported
 */

static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
				const struct req_op_context *opctx,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_unlink
 * default case not supported
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const struct req_op_context *opctx,
				 const char *name)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glusterfs_export *glfs_export = 
		container_of(dir_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *parenthandle = 
		container_of(dir_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	rc =  glfs_h_unlink (glfs_export->gl_fs, parenthandle->glhandle, name);
	if (rc != 0) {
		status = gluster2fsal_error(errno);
	}

#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_file_unlink);
#endif
	return status;
}


/* file_open
 * default case not supported
 */

static fsal_status_t file_open(struct fsal_obj_handle *obj_hdl,
			       const struct req_op_context *opctx,
			       fsal_openflags_t openflags)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glfs_fd          *glfd = NULL;
	int                      p_flags = 0;
	struct glusterfs_export *glfs_export = 
		container_of(obj_hdl->export, struct glusterfs_export, export);
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	if (objhandle->openflags != FSAL_O_CLOSED) {
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	/* TODO: Do we take O_SYNC as an in flag? */
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

/* file_status
 * default case file always closed
 */

static fsal_openflags_t file_status(struct fsal_obj_handle *obj_hdl)
{
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);

	return objhandle->openflags;
}

/* file_read
 * default case not supported
 */

static fsal_status_t file_read(struct fsal_obj_handle *obj_hdl,
			       const struct req_op_context *opctx,
			       uint64_t seek_descriptor,
			       size_t buffer_size,
			       void *buffer,
			       size_t *read_amount,
			       bool *end_of_file)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	rc = glfs_pread(objhandle->glfd, buffer, buffer_size, seek_descriptor, 
			0 /*TODO: flags is unused, so pass in something */);
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

/* file_write
 * default case not supported
 */

static fsal_status_t file_write(struct fsal_obj_handle *obj_hdl,
				const struct req_op_context *opctx,
				uint64_t seek_descriptor,
				size_t buffer_size,
				void *buffer,
				size_t *write_amount,
				bool *fsal_stable)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	/* FIXME: Handle fsal_stable (as O_SYNC?) here */
	rc = glfs_pwrite(objhandle->glfd, buffer, buffer_size, seek_descriptor, 
			0 /*TODO: flags is USED, so pass in appropriately */);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	*write_amount = rc;
	*fsal_stable = true;

out:
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_file_write);
#endif
	return status;
}

/* commit
 * default case not supported
 */

static fsal_status_t commit(struct fsal_obj_handle *obj_hdl, /* sync */
			    off_t offset,
			    size_t len)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

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

/* lock_op
 * default case not supported
 */
/*
static fsal_status_t lock_op(struct fsal_obj_handle *obj_hdl,
			     const struct req_op_context *opctx,
			     void * p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/* share_op
 * default case not supported
 */
/*
static fsal_status_t share_op(struct fsal_obj_handle *obj_hdl,
			      void *p_owner,
			      fsal_share_param_t  request_share)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/* file_close
 * default case not supported
 */

static fsal_status_t file_close(struct fsal_obj_handle *obj_hdl)
{
	int                      rc = 0;
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

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

/* list_ext_attrs
 * default case not supported
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
/* getextattr_id_by_name
 * default case not supported
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
/* getextattr_value_by_name
 * default case not supported
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
/* getextattr_value_by_id
 * default case not supported
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
/* setextattr_value
 * default case not supported
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
/* setextattr_value_by_id
 * default case not supported
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
/* getextattr_attrs
 * default case not supported
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
/* remove_extattr_by_id
 * default case not supported
 */
/*
static fsal_status_t remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  const struct req_op_context *opctx,
					  unsigned int xattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/* remove_extattr_by_name
 * default case not supported
 */
/*
static fsal_status_t remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const struct req_op_context *opctx,
					    const char *xattr_name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
*/
/* lru_cleanup
 * default case always be happy
 */

fsal_status_t lru_cleanup(struct fsal_obj_handle *obj_hdl,
			  lru_actions_t requests)
{
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	struct glusterfs_handle *objhandle = 
		container_of(obj_hdl, struct glusterfs_handle, handle);
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	/* TODO: need to determine what else is safely clean-able so that we 
	 * can free those up as well */
	if (objhandle->glfd != NULL) {
		status = file_close(obj_hdl);
	}

#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_lru_cleanup);
#endif
	return status;
}

/* handle_digest
 * default case server fault
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   fsal_digesttype_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	fsal_status_t            status = {ERR_FSAL_NO_ERROR, 0};
	size_t                   fh_size;
	struct glusterfs_handle *objhandle;
#ifdef GLTIMING
	struct timespec          s_time, e_time;

	now(&s_time);
#endif

	if( !fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	objhandle = container_of(obj_hdl, struct glusterfs_handle, handle);

	switch(output_type) {
		case FSAL_DIGEST_NFSV2:
		case FSAL_DIGEST_NFSV3:
		case FSAL_DIGEST_NFSV4:
			fh_size = GLAPI_HANDLE_LENGTH;
			if(fh_desc->len < fh_size) {
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
 * handle_digest
 * default case return a safe empty key
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	struct glusterfs_handle *objhandle;
#ifdef GLTIMING
	struct timespec          s_time, e_time;

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
 * @brief Fail to grant a layout segment.
 *
 * @param[in]     obj_hdl  The handle of the file on which the layout is
 *                         requested.
 * @param[in]     req_ctx  Request context
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return NFS4ERR_LAYOUTUNAVAILABLE
 */
/*
static nfsstat4
layoutget(struct fsal_obj_handle *obj_hdl,
	  struct req_op_context *req_ctx,
	  XDR *loc_body,
	  const struct fsal_layoutget_arg *arg,
	  struct fsal_layoutget_res *res)
{
	return NFS4ERR_LAYOUTUNAVAILABLE;
}
*/

/**
 * @brief Don't return a layout segment
 *
 * @param[in] obj_hdl  The object on which a segment is to be returned
 * @param[in] req_ctx  Request context
 * @param[in] lrf_body In the case of a non-synthetic return, this is
 *                     an XDR stream corresponding to the layout
 *                     type-specific argument to LAYOUTRETURN.  In
 *                     the case of a synthetic or bulk return,
 *                     this is a NULL pointer.
 * @param[in] arg      Input arguments of the function
 *
 * @return NFS4ERR_NOTSUPP
 */
/*
static nfsstat4
layoutreturn(struct fsal_obj_handle *obj_hdl,
	     struct req_op_context *req_ctx,
	     XDR *lrf_body,
	     const struct fsal_layoutreturn_arg *arg)
{
	return NFS4ERR_NOTSUPP;
}
*/

/**
 * @brief Fail to commit a segment of a layout
 *
 * @param[in]     obj_hdl  The object on which to commit
 * @param[in]     req_ctx  Request context
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */
/*
static nfsstat4
layoutcommit(struct fsal_obj_handle *obj_hdl,
	     struct req_op_context *req_ctx,
	     XDR *lou_body,
	     const struct fsal_layoutcommit_arg *arg,
	     struct fsal_layoutcommit_res *res)
{
	return NFS4ERR_NOTSUPP;
}
*/


/* Default fsal handle object method vector.
 * copied to allocated vector at register time
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
	ops->close = file_close;
	ops->lru_cleanup = lru_cleanup;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
}
