/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
 * vim:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* handle.c
 */

#include "config.h"

#include "scality_methods.h"
#include "city.h"
#include "nfs_file_handle.h"
#include "display.h"

#include "dbd_rest_client.h"
#include "sproxyd_client.h"
#include "redis_client.h"

#define EXPIRE_TIME_ATTR 1
/* from GNU coreutils documentation
 * On most systems, if a directory’s set-group-ID bit is set, newly created
 * subfiles inherit the same group as the directory, and newly created
 * subdirectories inherit the set-group-ID bit of the parent directory. On a
 * few systems, a directory’s set-user-ID bit has a similar effect on the
 * ownership of new subfiles and the set-user-ID bits of new subdirectories.
 * These mechanisms let users share files more easily, by lessening the need
 * to use chmod or chown to share new files.
 */
#define DEFAULT_MODE_DIRECTORY 06777
#define DEFAULT_MODE_REGULAR 0666

/* Atomic uint64_t that is used to generate inode numbers in the Pseudo FS */

static int
name_to_object(struct scality_fsal_obj_handle *parent,
	       const char *name,
	       char *buf, int buf_sz)
{
	int ret;
	if ( parent && parent->object ) {
		size_t parent_len = strlen(parent->object);
		if ( parent_len > 0 )
			ret = snprintf(buf, buf_sz, "%s"S3_DELIMITER"%s", parent->object, name);
		else
			ret = snprintf(buf, buf_sz, "%s", name);
	}
	else {
		buf[0]='\0';
		ret = 0;
	}
	if ( ret >= buf_sz ) {
		LogCrit(COMPONENT_FSAL, "name_to_object: buffer too small");
		ret = -1;
	}

	return ret;
}

static bool
not_backed_lookup(struct scality_fsal_obj_handle *myself, const char *name)
{
	struct scality_not_backed_dir *child;
	for ( child = myself->not_backed_children ;
	      child ;
	      child = child->next ) {
		if ( 0 == strcmp(name, child->name ) )
			return true;
	}
	return false;
}

static void
not_backed_insert(struct scality_fsal_obj_handle *myself, const char *name)
{
	struct scality_not_backed_dir *child;
	child = gsh_malloc(sizeof(*child));
	child->name = gsh_strdup(name);
	child->next = myself->not_backed_children;
	myself->not_backed_children = child;
}

static void
not_backed_remove(struct scality_fsal_obj_handle *myself, const char *name)
{
	struct scality_not_backed_dir **childp;
	for ( childp = &myself->not_backed_children ;
	      *childp ;
	      childp = &(*childp)->next ) {
		if ( 0 == strcmp(name, (*childp)->name ) ) {
			struct scality_not_backed_dir *child;
			child = *childp;
			*childp = child->next;
			gsh_free(child->name);
			gsh_free(child);
			break;
		}
	}
}

static void
not_backed_readdir(struct scality_fsal_obj_handle *myself,
		   fsal_readdir_cb cb,
		   void *dir_state,
		   int *countp,
		   bool *eof)
{
	struct scality_not_backed_dir *child, *next;
	for ( child = myself->not_backed_children ;
	      child ;
	      child = next ) {
		++(*countp);
		/* child may be free'd by lookup during callback */
		next = child->next;
		char name[MAXPATHLEN];
		snprintf(name, sizeof name, "%s", child->name);
		if ( !cb(name, dir_state, *countp)) {
			*eof = false;
			break;
		}
	}
}



static void release(struct fsal_obj_handle *obj_hdl);

/* alloc_handle
 * allocate and fill in a handle
 */

static struct scality_fsal_obj_handle
*alloc_handle(const char *object,
	      const char *handle_key,
	      struct fsal_export *exp_hdl,
	      dbd_dtype_t dtype)
{
	struct scality_fsal_obj_handle *hdl;
	struct scality_fsal_export *export;

	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);

	hdl = gsh_calloc(1, sizeof(struct scality_fsal_obj_handle) +
			    V4_FH_OPAQUE_SIZE);

	hdl->obj_handle.attrs = &hdl->attributes;

	/* Create the handle */
	hdl->handle = (char *) &hdl[1];

	hdl->object = gsh_strdup(object);

	hdl->openflags = FSAL_O_CLOSED;
	hdl->locations = NULL;
	hdl->n_locations = 0;

	if ( NULL == handle_key )
		redis_create_handle_key(hdl->object, hdl->handle, V4_FH_OPAQUE_SIZE);
	else
		memcpy(hdl->handle, handle_key, V4_FH_OPAQUE_SIZE);

	hdl->obj_handle.type = (dtype == DBD_DTYPE_DIRECTORY) ? DIRECTORY : REGULAR_FILE;

	/* Fills the output struct */
	hdl->attributes.type = (dtype == DBD_DTYPE_DIRECTORY) ? DIRECTORY : REGULAR_FILE;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_TYPE);

	hdl->attributes.filesize = 0;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_SIZE);

	/* fsid will be supplied later */
	hdl->attributes.fsid.major = 0;
	hdl->attributes.fsid.minor = 0;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_FSID);

	hdl->attributes.fileid = *(uint64_t*)hdl->handle;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_FILEID);

        mode_t unix_mode =  (dtype == DBD_DTYPE_DIRECTORY)
                ? DEFAULT_MODE_DIRECTORY
                : DEFAULT_MODE_REGULAR;
        unix_mode = unix_mode & ~export->umask;
	hdl->attributes.mode = unix2fsal_mode(unix_mode);
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_MODE);

	hdl->attributes.numlinks = 1;
	hdl->numlinks = 1;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_NUMLINKS);

        // not using op_ctx->creds->caller_uid because caller is not the owner
        // and user is squashed
	hdl->attributes.owner = op_ctx->export->export_perms.anonymous_uid;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_OWNER);

        // not using op_ctx->creds->caller_gid because caller is not the owner
        // and group is squashed
	hdl->attributes.group = op_ctx->export->export_perms.anonymous_gid;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_GROUP);

	/* Use full timer resolution */
	if ( DBD_DTYPE_DIRECTORY == dtype )
		hdl->attributes.atime = (struct timespec){0, 0};
	else
		now(&hdl->attributes.atime);
	hdl->attributes.ctime = hdl->attributes.atime;
	hdl->attributes.mtime = hdl->attributes.atime;
	hdl->attributes.chgtime = hdl->attributes.atime;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_ATIME);
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_CTIME);
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_MTIME);

	hdl->attributes.change =
		timespec_to_nsecs(&hdl->attributes.chgtime);
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_CHGTIME);

	hdl->attributes.spaceused = 0;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_SPACEUSED);

	hdl->attributes.rawdev.major = 0;
	hdl->attributes.rawdev.minor = 0;
	FSAL_SET_MASK(hdl->attributes.mask, ATTR_RAWDEV);

	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl,  (dtype == DBD_DTYPE_DIRECTORY) ? DIRECTORY : REGULAR_FILE);

	hdl->attributes.expire_time_attr = EXPIRE_TIME_ATTR;

	scality_handle_ops_init(&hdl->obj_handle.obj_ops);

	hdl->not_backed = false;
	hdl->not_backed_children = NULL;

	hdl->state = SCALITY_FSAL_OBJ_STATE_CLEAN;
	hdl->part_size = 0;
	hdl->memory_used = 0;
	hdl->delete_on_commit = NULL;
	hdl->delete_on_rollback = NULL;

	return hdl;
}

static fsal_status_t
test_access(struct fsal_obj_handle *obj_hdl,
                 fsal_accessflags_t access_type,
                 fsal_accessflags_t *allowed,
                 fsal_accessflags_t *denied)
{
        fsal_status_t status;
        status = fsal_test_access(obj_hdl, access_type, allowed, denied);
        LogDebug(COMPONENT_FSAL, "fsal_test_access returned %d", status.major);
        return status;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path,
			    struct fsal_obj_handle **handle)
{
	struct scality_fsal_obj_handle *myself, *hdl;
	fsal_errors_t error = ERR_FSAL_NOENT;

	myself = container_of(parent,
			      struct scality_fsal_obj_handle,
			      obj_handle);

	LogDebug(COMPONENT_FSAL,
		 "lookup(%s)",
		 path);


	if (strcmp(path, "..") == 0) {
		/* lookup parent - lookupp */
		assert(!"Not Impl");
		goto out;
	}

	/* Check if this context already holds the lock on
	 * this directory.
	 */

	//dbd stuffs!
	dbd_dtype_t dtype;
	int ret;
	char object[MAX_URL_SIZE];
	ret = name_to_object(myself, path, object, sizeof object);

	if ( ret < 0 ) {
		error = ERR_FSAL_SERVERFAULT;
		goto out;
	}

	bool found_in_not_backed_children =
		not_backed_lookup(myself, path);

	ret = dbd_lookup(container_of(op_ctx->fsal_export,
				      struct scality_fsal_export,
				      export),
			 myself,
			 path,
			 &dtype);
	
	if ( 0 == ret ) {
		char handle_key[V4_FH_OPAQUE_SIZE];
		char *handle_keyp = NULL;
		ret = redis_get_handle_key(object, handle_key, sizeof handle_key);
		if ( 0 == ret )
			handle_keyp = handle_key;

		switch (dtype) {
		case DBD_DTYPE_REGULAR:
			hdl = alloc_handle(object,
					   handle_keyp,
					   op_ctx->fsal_export, dtype);
			*handle = &hdl->obj_handle;
			error = ERR_FSAL_NO_ERROR;
			break;
		case DBD_DTYPE_ENOENT:
			if ( !found_in_not_backed_children )
				break;
			//fallthrough
		case DBD_DTYPE_DIRECTORY:
			hdl = alloc_handle(object,
					   handle_keyp,
					   op_ctx->fsal_export, DBD_DTYPE_DIRECTORY);
			*handle = &hdl->obj_handle;
			error = ERR_FSAL_NO_ERROR;
			if ( found_in_not_backed_children &&
			     DBD_DTYPE_DIRECTORY == dtype ) {
				not_backed_remove(myself, path);
			}
			break;			
		default:break;
		}
	}

out:

	return fsalstat(error, 0);
}

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
			    const char *name,
			    struct attrlist *attrib,
			    struct fsal_obj_handle **handle)
{
	struct scality_fsal_obj_handle *myself, *hdl;
	struct scality_fsal_export *export;
	
	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);
	myself = container_of(dir_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);
	char object[MAX_URL_SIZE];
	if ( '\0' == myself->object[0] )
		snprintf(object, sizeof object, "%s", name);
	else
		snprintf(object, sizeof object, "%s"S3_DELIMITER"%s", myself->object, name);
	LogDebug(COMPONENT_FSAL, "create %s", object);
	char handle_key[V4_FH_OPAQUE_SIZE];
	char *handle_keyp = NULL;
	int ret;
	ret = redis_get_handle_key(object, handle_key, sizeof handle_key);
	if ( 0 == ret )
		handle_keyp = handle_key;

	hdl = alloc_handle(object,
			   handle_keyp,
			   op_ctx->fsal_export, DBD_DTYPE_REGULAR);
	*handle = &hdl->obj_handle;

	ret = dbd_post(export, hdl);
	if ( ret < 0 ) {
		LogCrit(COMPONENT_FSAL, "create of %s"S3_DELIMITER"%s failed",
			myself->object, name);
		release(*handle);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	myself->not_backed = false;
	
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name,
			     struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	struct scality_fsal_obj_handle *myself, *hdl;
        fsal_accessflags_t access_type;
        fsal_status_t status;
	fsal_errors_t error = ERR_FSAL_NO_ERROR;

	myself = container_of(dir_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);
	if ( not_backed_lookup(myself, name) )
		return fsalstat(ERR_FSAL_EXIST, EEXIST);

	access_type = FSAL_MODE_MASK_SET(FSAL_W_OK) |
		FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_SUBDIRECTORY);
	status = test_access(dir_hdl, access_type, NULL, NULL);
	if (FSAL_IS_ERROR(status))
		return status;

	char object[MAX_URL_SIZE];
	if ( '\0' == myself->object[0] )
		snprintf(object, sizeof object, "%s", name);
	else
		snprintf(object, sizeof object, "%s"S3_DELIMITER"%s", myself->object, name);
	LogDebug(COMPONENT_FSAL, "makedir %s", object);

	char handle_key[V4_FH_OPAQUE_SIZE];
	char *handle_keyp = NULL;
	int ret;
	ret = redis_get_handle_key(object, handle_key, sizeof handle_key);
	if ( 0 == ret )
		handle_keyp = handle_key;
	
	hdl = alloc_handle(object,
			   handle_keyp,
			   op_ctx->fsal_export, DBD_DTYPE_DIRECTORY);
	hdl->not_backed = true;
	*handle = &hdl->obj_handle;

	not_backed_insert(myself, name);

	return fsalstat(error, 0);
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name,
			      object_file_type_t nodetype,
			      fsal_dev_t *dev,
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	/* SCALITY doesn't support non-directory inodes */
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name,
				 const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle)
{
	/* SCALITY doesn't support non-directory inodes */
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	/* SCALITY doesn't support non-directory inodes */
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	/* SCALITY doesn't support non-directory inodes */
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/**
 * read_dirents
 * read the directory and call through the callback function for
 * each entry.
 * @param dir_hdl [IN] the directory to read
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker true == end of dir
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence,
				  void *dir_state,
				  fsal_readdir_cb cb,
				  bool *eof)
{
	struct scality_fsal_obj_handle *myself;
	fsal_cookie_t seekloc;

	if (whence != NULL)
		seekloc = *whence;
	else
		seekloc = 0;

	*eof = true;

	myself = container_of(dir_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);

	LogDebug(COMPONENT_FSAL,
		"readdir: hdl=%p, name=%s",
		myself, myself->object);

	PTHREAD_RWLOCK_rdlock(&dir_hdl->lock);

	/* Use fsal_private to signal to lookup that we hold
	 * the lock.
	 */
	op_ctx->fsal_private = dir_hdl;


	struct scality_fsal_export *export = container_of(op_ctx->fsal_export,
							 struct scality_fsal_export,
							 export);

	int count = -1;

	if ( !myself->not_backed) {
		dbd_readdir(export, myself, seekloc, dir_state, cb, &count, eof);
	}
	if ( -1 != count )
		myself->not_backed = false;

	not_backed_readdir(myself, cb, dir_state, &count, eof);

	op_ctx->fsal_private = NULL;

	PTHREAD_RWLOCK_unlock(&dir_hdl->lock);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t renamefile(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	/* SCALITY doesn't support non-directory inodes */
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl)
{
	struct scality_fsal_obj_handle *myself;
	struct scality_fsal_export *export;

	myself = container_of(obj_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);
	export = container_of(op_ctx->fsal_export, struct scality_fsal_export, export);
	LogDebug(COMPONENT_FSAL,
		"getattrs(%s)",myself->object);

	/* We need to update the numlinks under attr lock. */
	myself->attributes.numlinks = atomic_fetch_uint32_t(&myself->numlinks);

	if ( DIRECTORY != myself->attributes.type &&
	     !myself->not_backed &&
	     SCALITY_FSAL_OBJ_STATE_CLEAN == myself->state) {
	
		int ret;
		ret = dbd_getattr(export, myself);
		if ( ret < 0 ) {
			LogDebug(COMPONENT_FSAL,
				 "Requesting attributes for non existing object name=%s",
				 myself->object);
			return fsalstat(ERR_FSAL_STALE, ESTALE);		
		}
		
	}

	if ( DIRECTORY == myself->attributes.type ) {
		cache_inode_status_t status;
		struct gsh_buffdesc handle_key;
		uint32_t flags = CACHE_INODE_INVALIDATE_CONTENT|CACHE_INODE_INVALIDATE_GOT_LOCK;
		obj_hdl->obj_ops.handle_to_key(obj_hdl, &handle_key);
		status = export->export.up_ops->invalidate(&export->module->fsal, &handle_key, flags);
		if ( CACHE_INODE_SUCCESS != status ) {
			LogWarn(COMPONENT_FSAL, "Unable to invalidate directory content");
		}
	}
	
	LogFullDebug(COMPONENT_FSAL,
		     "hdl=%p, name=%s numlinks %"PRIu32,
		     myself,
		     myself->object,
		     myself->attributes.numlinks);
	
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*
 * NOTE: this is done under protection of the attributes rwlock
 *       in the cache entry.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	struct scality_fsal_obj_handle *myself;
	struct scality_fsal_export *export;
	
	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);
	myself = container_of(obj_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);

	if ( REGULAR_FILE != myself->attributes.type ) {
		LogCrit(COMPONENT_FSAL,
			"Invoking unsupported FSAL operation, setattrs on directory: %s",
			myself->object);
		return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
		myself->attributes.atime = (struct timespec) {
			.tv_sec = time(NULL),
			.tv_nsec = 0
		};
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
		myself->attributes.mtime = (struct timespec) {
			.tv_sec = time(NULL),
			.tv_nsec = 0
		};
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
		myself->attributes.mtime = attrs->mtime;
	}
	
	if (FSAL_TEST_MASK(attrs->mask, ATTR_CTIME)) {
		myself->attributes.ctime = attrs->ctime;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
		myself->attributes.atime = attrs->atime;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		if ( 0 != attrs->filesize )
			return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
		size_t i;
		for ( i = 0 ; i < myself->n_locations ; ++i ) {
			struct scality_location *location = &myself->locations[i];
			if ( location->key )
				scality_add_to_free_list(&myself->delete_on_commit, location->key);
			else if ( location->content )
				gsh_free(location->content);
		}
		myself->attributes.filesize = 0;
		gsh_free(myself->locations);
		myself->memory_used = 0;
		myself->locations = NULL;
		myself->n_locations = 0;
		myself->state = SCALITY_FSAL_OBJ_STATE_DIRTY;
	}

	if ( SCALITY_FSAL_OBJ_STATE_CLEAN == myself->state ) {
		int ret = dbd_post(export, myself);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL,
				"Unable to setattr(%s)", myself->object);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	int ret;
	dbd_dtype_t dtype;
	struct scality_fsal_export *export;
	struct scality_fsal_obj_handle *myself;
	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);
	myself = container_of(dir_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);

	LogDebug(COMPONENT_FSAL,
		 "unlink(%s)",
		 name);

	bool found_in_not_backed_children =
		not_backed_lookup(myself, name);

	ret = dbd_lookup(export, myself, name, &dtype);
	if ( 0 != ret )
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);

	switch(dtype) {
	case DBD_DTYPE_REGULAR: {
		struct fsal_obj_handle *hdl;
		fsal_status_t status = lookup(dir_hdl, name, &hdl);
		if ( ERR_FSAL_NO_ERROR == status.major ) {
			struct scality_fsal_obj_handle *obj_hdl;
			obj_hdl = container_of(hdl,
					       struct scality_fsal_obj_handle,
					       obj_handle);
			size_t i;
			int ret;
			ret = dbd_delete(export, obj_hdl->object);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL, "Unable to de-index %s", obj_hdl->object);
				release(hdl);
				return fsalstat(ERR_FSAL_SERVERFAULT, 0);
			}
			for ( i = 0 ; i < obj_hdl->n_locations ; ++i ) {
				ret = sproxyd_delete(export,
					       obj_hdl->locations[i].key);
				if ( ret < 0 ) {
					LogWarn(COMPONENT_FSAL, "Unable to delete key %s from %s", obj_hdl->locations[i].key, obj_hdl->object);
				}
			}
			scality_cleanup(export, myself, SCALITY_FSAL_CLEANUP_COMMIT|SCALITY_FSAL_CLEANUP_ROLLBACK);
			release(hdl);
		}

		/*
		 * if not marked as "not backed" last rm in a dir complains
		 */
		ret = dbd_lookup_object(export, myself->object, &dtype);
		if ( 0 != ret )
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		if ( DBD_DTYPE_ENOENT == dtype ) {
			myself->not_backed = true;
		}
		
		break;
	}
	case DBD_DTYPE_DIRECTORY:
		return fsalstat(ERR_FSAL_NOTEMPTY, 0);
	case DBD_DTYPE_ENOENT:
		if ( found_in_not_backed_children) {
			not_backed_remove(myself, name);
		} else {
			return fsalstat(ERR_FSAL_NOENT, ENOENT);
		}
		break;
	default:break;
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	char object[MAX_URL_SIZE];
	ret = snprintf(object, sizeof object, "%s"S3_DELIMITER"%s", myself->object, name);
	if ( ret < sizeof(object) ) {
		redis_remove(object);
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   fsal_digesttype_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	const struct scality_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      const struct scality_fsal_obj_handle,
			      obj_handle);

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < V4_FH_OPAQUE_SIZE) {
			LogMajor(COMPONENT_FSAL,
				 "Space too small for handle.  need %lu, have %zu",
				 ((unsigned long) V4_FH_OPAQUE_SIZE),
				 fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		}

		memcpy(fh_desc->addr, myself->handle, V4_FH_OPAQUE_SIZE);
		fh_desc->len = V4_FH_OPAQUE_SIZE;
		break;

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * handle_to_key
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	struct scality_fsal_obj_handle *myself;

	myself = container_of(obj_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);

	fh_desc->addr = myself->handle;
	fh_desc->len = V4_FH_OPAQUE_SIZE;
}

/*
 * release
 * release our export first so they know we are gone
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	struct scality_fsal_obj_handle *myself;
	int i;
	
	myself = container_of(obj_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);

	if ( myself->not_backed )
		LogCrit(COMPONENT_FSAL, "cleanup of a not_backed dir %s %p", myself->object, myself);

	if ( SCALITY_FSAL_OBJ_STATE_DIRTY == myself->state ) {
		fsal_status_t status;
		status = scality_commit(obj_hdl, 0, myself->attributes.filesize);
		if ( ERR_FSAL_NO_ERROR != status.major ) {
			LogCrit(COMPONENT_FSAL, "Failed to flush file content at release");
		}
	}

	
	fsal_obj_handle_fini(obj_hdl);

	LogDebug(COMPONENT_FSAL,
		 "Releasing hdl=%p, name=%s",
		 myself, myself->object);

	if (myself->object != NULL)
		gsh_free(myself->object);

	for ( i = 0 ; i < myself->n_locations ; ++i )
		gsh_free(myself->locations[i].key);
	if (myself->locations)
		gsh_free(myself->locations);
	
	gsh_free(myself);
}

void scality_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->create = create;
	ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->test_access = test_access;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->open = scality_open;
	ops->status = scality_status;
	ops->read = scality_read;
	ops->write = scality_write;
	ops->commit = scality_commit;
	ops->lock_op = scality_lock_op;
	ops->close = scality_close;
	ops->lru_cleanup = scality_lru_cleanup;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;

	/* xattr related functions */
	ops->list_ext_attrs = scality_list_ext_attrs;
	ops->getextattr_id_by_name = scality_getextattr_id_by_name;
	ops->getextattr_value_by_name = scality_getextattr_value_by_name;
	ops->getextattr_value_by_id = scality_getextattr_value_by_id;
	ops->setextattr_value = scality_setextattr_value;
	ops->setextattr_value_by_id = scality_setextattr_value_by_id;
	ops->getextattr_attrs = scality_getextattr_attrs;
	ops->remove_extattr_by_id = scality_remove_extattr_by_id;
	ops->remove_extattr_by_name = scality_remove_extattr_by_name;

}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t scality_lookup_path(struct fsal_export *exp_hdl,
				 const char *path,
				 struct fsal_obj_handle **handle)
{
	struct scality_fsal_export *myself;

	myself = container_of(exp_hdl, struct scality_fsal_export, export);

	if (strcmp(path, myself->export_path) != 0) {
		/* Lookup of a path other than the export's root. */
		LogCrit(COMPONENT_FSAL,
			"Attempt to lookup non-root path %s",
			path);
		return fsalstat(ERR_FSAL_NOENT, ENOENT);
	}

	if (myself->root_handle == NULL) {
		int ret;
		char object[MAX_URL_SIZE];
		ret = name_to_object(NULL, myself->export_path, object, sizeof object);
		if ( ret < 0 ) {
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
		char handle_key[V4_FH_OPAQUE_SIZE];
		char *handle_keyp = NULL;
		ret = redis_get_handle_key(object, handle_key, sizeof handle_key);
		if ( 0 == ret )
			handle_keyp = handle_key;

                ret = dbd_collect_bucket_attributes(myself);
                if ( 0 != ret ) {
                        LogCrit(COMPONENT_FSAL,
                                "Cannot collect bucket attributes for %s",
                                myself->export_path);
                        return fsalstat(ERR_FSAL_NOENT, ENOENT);
                }
		myself->root_handle =
			alloc_handle(object,
				     handle_keyp,
				     exp_hdl,
				     DBD_DTYPE_DIRECTORY);
	}

	*handle = &myself->root_handle->obj_handle;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in cache_inode etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket, nor reliably on block or
 * character special devices.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t scality_create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle)
{
	struct scality_fsal_obj_handle *hdl;
	struct scality_fsal_export *export;

	export = container_of(exp_hdl, struct scality_fsal_export, export);

	*handle = NULL;

	if (hdl_desc->len != V4_FH_OPAQUE_SIZE) {
		LogCrit(COMPONENT_FSAL,
			"Invalid handle size %zu expected %lu",
			hdl_desc->len,
			((unsigned long) V4_FH_OPAQUE_SIZE));

		return fsalstat(ERR_FSAL_BADHANDLE, 0);
	}

	char object[MAX_URL_SIZE];
	int ret;
	ret = redis_get_object(hdl_desc->addr, V4_FH_OPAQUE_SIZE,
			       object, sizeof object);
	if ( ret < 0 ) {
		LogDebug(COMPONENT_FSAL, "missed handle");
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	}

	LogDebug(COMPONENT_FSAL, "handle match for %s", object);
	
	
	dbd_dtype_t dtype = DBD_DTYPE_DIRECTORY;

	if ( '\0' != object[0]  ) {
		ret = dbd_lookup_object(export, object, &dtype);
		if ( ret < 0 ) {
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	}
	switch (dtype) {
	case DBD_DTYPE_REGULAR:
	case DBD_DTYPE_DIRECTORY:
		hdl = alloc_handle(object,
				   hdl_desc->addr,
				   op_ctx->fsal_export, dtype);
		*handle = &hdl->obj_handle;
		return fsalstat(ERR_FSAL_NO_ERROR,0);
	case DBD_DTYPE_ENOENT:
		return fsalstat(ERR_FSAL_STALE, ESTALE);
	default:break;
		return fsalstat(ERR_FSAL_BADHANDLE, 0);
	}

	return fsalstat(ERR_FSAL_STALE, ESTALE);
}
