/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * VFS object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "gpfs_methods.h"

/* helpers
 */

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */


static struct gpfs_fsal_obj_handle *alloc_handle(struct gpfs_file_handle *fh,
                                                struct attrlist *attributes,
                                                const char *link_content,
                                                struct gpfs_file_handle *dir_fh,
                                                const char *unopenable_name,
                                                struct fsal_export *exp_hdl)
{
	struct gpfs_fsal_obj_handle *hdl;

	hdl = malloc(sizeof(struct gpfs_fsal_obj_handle) +
		     sizeof(struct gpfs_file_handle));
	if(hdl == NULL)
		return NULL;
	memset(hdl, 0, (sizeof(struct gpfs_fsal_obj_handle) +
			sizeof(struct gpfs_file_handle)));
	hdl->handle = (struct gpfs_file_handle *)&hdl[1];
	memcpy(hdl->handle, fh, sizeof(struct gpfs_file_handle));
	hdl->obj_handle.type = attributes->type;
	if(hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.fd = -1;  /* no open on this yet */
		hdl->u.file.openflags = FSAL_O_CLOSED;
	} else if(hdl->obj_handle.type == SYMBOLIC_LINK
	   && link_content != NULL) {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = malloc(len);
		if(hdl->u.symlink.link_content == NULL) {
			goto spcerr;
		}
		memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
	} else if(gpfs_unopenable_type(hdl->obj_handle.type)
		  && dir_fh != NULL
		  && unopenable_name != NULL) {
		hdl->u.unopenable.dir = malloc(sizeof(struct gpfs_file_handle));
		if(hdl->u.unopenable.dir == NULL)
			goto spcerr;
		memcpy(hdl->u.unopenable.dir,
		       dir_fh,
		       sizeof(struct gpfs_file_handle));
		hdl->u.unopenable.name = malloc(strlen(unopenable_name) + 1);
		if(hdl->u.unopenable.name == NULL)
			goto spcerr;
		strcpy(hdl->u.unopenable.name, unopenable_name);
	}
	hdl->obj_handle.export = exp_hdl;
	hdl->obj_handle.attributes.mask
		= exp_hdl->ops->fs_supported_attrs(exp_hdl);
	memcpy(&hdl->obj_handle.attributes, attributes, sizeof(struct attrlist));

	if(!fsal_obj_handle_init(&hdl->obj_handle,
				 exp_hdl,
	                         attributes->type))
                return hdl;

	hdl->obj_handle.ops = NULL;
	pthread_mutex_unlock(&hdl->obj_handle.lock);
	pthread_mutex_destroy(&hdl->obj_handle.lock);
spcerr:
	if(hdl->obj_handle.type == SYMBOLIC_LINK) {
		if(hdl->u.symlink.link_content != NULL)
			free(hdl->u.symlink.link_content);
        } else if(gpfs_unopenable_type(hdl->obj_handle.type)) {
		if(hdl->u.unopenable.name != NULL)
			free(hdl->u.unopenable.name);
		if(hdl->u.unopenable.dir != NULL)
			free(hdl->u.unopenable.dir);
	}
	free(hdl);  /* elvis has left the building */
	return NULL;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
                            const struct req_op_context *opctx,
			    const char *path,
			    struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        fsal_status_t status;
	struct gpfs_fsal_obj_handle /* *parent_hdl, */ *hdl;
/* 	int mount_fd; */
        struct attrlist attrib;
	struct gpfs_file_handle *fh
		= alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL; /* poison it first */
	if( !path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;
/* 	mount_fd = gpfs_get_root_fd(parent->export); */
/* 	parent_hdl = container_of(parent, struct gpfs_fsal_obj_handle, obj_handle); */
	if( !parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
        attrib.mask = parent->attributes.mask;
        status = GPFSFSAL_lookup(opctx, parent, path, &attrib, fh);
	if(FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &attrib, NULL, NULL, NULL, parent->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto hdlerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

hdlerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
                            const struct req_op_context *opctx,
                            const char *name,
                            struct attrlist *attrib,
                            struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle /* *myself, */ *hdl;
/* 	int mount_fd; */
        fsal_status_t status;

	struct gpfs_file_handle *fh
		= alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;
/* 	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle); */
/* 	mount_fd = gpfs_get_root_fd(dir_hdl->export); */

        attrib->mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
	status = GPFSFSAL_create(dir_hdl, name, opctx, attrib->mode, fh, attrib);
	if(FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, attrib, NULL, NULL, NULL, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
                             const struct req_op_context *opctx,
			     const char *name,
			     struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle /* *myself, */ *hdl;
        fsal_status_t status;
	struct gpfs_file_handle *fh
		= alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;
/* 	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle); */

        attrib->mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
	status = GPFSFSAL_mkdir(dir_hdl, name, opctx, attrib->mode, fh, attrib);
	if(FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, attrib, NULL, NULL, NULL, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto fileerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
fileerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
                              const struct req_op_context *opctx,
                              const char *name,
                              object_file_type_t nodetype,  /* IN */
                              fsal_dev_t *dev,  /* IN */
                              struct attrlist *attrib,
                              struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        fsal_status_t status;
	struct gpfs_fsal_obj_handle /* *myself, */ *hdl;
	struct gpfs_file_handle *dir_fh = NULL;
	struct gpfs_file_handle *fh
		= alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);

		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;
/* 	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle); */

        attrib->mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
        status = GPFSFSAL_mknode(dir_hdl, name, opctx, attrib->mode, nodetype, dev, fh, attrib);
	if(FSAL_IS_ERROR(status))
		return status;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, attrib, NULL, dir_fh, NULL, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto nodeerr;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
nodeerr:
	fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
                                 const struct req_op_context *opctx,
                                 const char *name,
                                 const char *link_path,
                                 struct attrlist *attrib,
                                 struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        fsal_status_t status;
	struct gpfs_fsal_obj_handle /* *myself, */ *hdl;
	struct gpfs_file_handle *fh
		= alloca(sizeof(struct gpfs_file_handle));

	*handle = NULL; /* poison it first */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;
/* 	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle); */

        attrib->mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
        status = GPFSFSAL_symlink(dir_hdl, name, link_path, opctx,
                           attrib->mode, fh, attrib);
        if(FSAL_IS_ERROR(status))
          return(status);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, attrib, link_path, NULL, NULL, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

errout:
	if(retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);

	return fsalstat(fsal_error, retval);	
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
                                 const struct req_op_context *opctx,
                                 struct gsh_buffdesc *link_content,
                                 bool refresh)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *myself = NULL;
/* 	int mntfd; */
        fsal_status_t status;

	if(obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if(refresh) { /* lazy load or LRU'd storage */
		size_t retlink;
		char link_buff[PATH_MAX + 1];

		retlink = PATH_MAX;

		if(myself->u.symlink.link_content != NULL) {
			free(myself->u.symlink.link_content);
			myself->u.symlink.link_content = NULL;
			myself->u.symlink.link_size = 0;
		}
/* 		mntfd = gpfs_get_root_fd(obj_hdl->export); */

                status =  GPFSFSAL_readlink(obj_hdl, opctx, link_buff,
                                            &retlink, NULL);
		if(FSAL_IS_ERROR(status))
			return(status);
			
		myself->u.symlink.link_content = malloc(retlink + 1);
		if(myself->u.symlink.link_content == NULL) {
			fsal_error = ERR_FSAL_NOMEM;
			goto out;
		}
		memcpy(myself->u.symlink.link_content, link_buff, retlink);
		myself->u.symlink.link_content[retlink] = '\0';
		myself->u.symlink.link_size = retlink + 1;
	}
	if(myself->u.symlink.link_content == NULL) {
		fsal_error = ERR_FSAL_FAULT; /* probably a better error?? */
		goto out;
	}
	link_content->len = myself->u.symlink.link_size;
	link_content->addr = gsh_malloc(link_content->len);
	if (link_content->addr == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		link_content->len = 0;
		goto out;
	}
	memcpy(link_content->addr,
	       myself->u.symlink.link_content,
	       link_content->len);

out:

	return fsalstat(fsal_error, retval);	
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
        fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

        status = GPFSFSAL_link(destdir_hdl, myself->handle, name, opctx, NULL);


	return(status);
}

/* not defined in linux headers so we do it here
 */

struct linux_dirent {
	unsigned long  d_ino;     /* Inode number */
	unsigned long  d_off;     /* Offset to next linux_dirent */
	unsigned short d_reclen;  /* Length of this linux_dirent */
	char           d_name[];  /* Filename (null-terminated) */
	/* length is actually (d_reclen - 2 -
	 * offsetof(struct linux_dirent, d_name)
	 */
	/*
	  char           pad;       // Zero padding byte
	  char           d_type;    // File type (only since Linux 2.6.4;
	  // offset is (d_reclen - 1))
	  */
};

#define BUF_SIZE 1024
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
                                  const struct req_op_context *opctx,
				  fsal_cookie_t *whence,
				  void *dir_state,
				  fsal_readdir_cb cb,
                                  bool *eof)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct gpfs_fsal_obj_handle *myself;
	int dirfd, mntfd;
	fsal_status_t status;
	off_t seekloc = 0;
	int bpos, cnt, nread;
	struct linux_dirent *dentry;
	char buf[BUF_SIZE];

	if(whence != NULL) {
		seekloc = (off_t)*whence;
	}
	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	mntfd = gpfs_get_root_fd(dir_hdl->export);
	status = fsal_internal_handle2fd_at(mntfd, myself->handle, &dirfd,
                                                        (O_RDONLY|O_DIRECTORY));
	if(dirfd < 0)
		return status;
	
	seekloc = lseek(dirfd, seekloc, SEEK_SET);
	if(seekloc < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto done;
	}
	cnt = 0;
	do {
		nread = syscall(SYS_getdents, dirfd, buf, BUF_SIZE);
		if(nread < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto done;
		}
		if(nread == 0)
			break;
		for(bpos = 0; bpos < nread;) {
			dentry = (struct linux_dirent *)(buf + bpos);
			if(strcmp(dentry->d_name, ".") == 0 ||
			   strcmp(dentry->d_name, "..") == 0)
				goto skip; /* must skip '.' and '..' */

			/* callback to cache inode */
			if (!cb(opctx,
			        dentry->d_name,
			        dir_state,
			        (fsal_cookie_t)dentry->d_off)) {
				goto done;
			}
skip:
			bpos += dentry->d_reclen;
			cnt++;
		}
	} while(nread > 0);

	*eof = nread == 0 ? true : false;
done:
	close(dirfd);
	
	return fsalstat(fsal_error, retval);	
}


static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
                                const struct req_op_context *opctx,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	fsal_status_t status;

	status = GPFSFSAL_rename(olddir_hdl, old_name,
	                         newdir_hdl, new_name,
	                         opctx);
	return(status);	
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx)
{
	struct gpfs_fsal_obj_handle *myself;
/* 	int mntfd; */
	fsal_status_t status;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
/* 	mntfd = gpfs_get_root_fd(obj_hdl->export); */

        obj_hdl->attributes.mask =
                      obj_hdl->export->ops->fs_supported_attrs(obj_hdl->export);
        status = GPFSFSAL_getattrs(obj_hdl->export, opctx, myself->handle,
                                   &obj_hdl->attributes);
	if(FSAL_IS_ERROR(status)) {
                FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
		FSAL_SET_MASK(obj_hdl->attributes.mask,
                              ATTR_RDATTR_ERR);
	}
	return(status);
}

/*
 * NOTE: this is done under protection of the attributes rwlock in the cache entry.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
			      struct attrlist *attrs)
{
        fsal_status_t status;

        status =  GPFSFSAL_setattrs(obj_hdl, opctx, attrs);

	return(status);
}

/* gpfs_compare
 * compare two handles.
 * return true for equal, false for anything else
 */
bool gpfs_compare(struct fsal_obj_handle *obj_hdl,
		  struct fsal_obj_handle *other_hdl)
{
	struct gpfs_fsal_obj_handle *myself, *other;

	if(obj_hdl == other_hdl)
		return true;
	if( !other_hdl)
		return false;
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	other = container_of(other_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	if((obj_hdl->type != other_hdl->type) ||
	   (myself->handle->handle_type != other->handle->handle_type) ||
	   (myself->handle->handle_size != other->handle->handle_size))
		return false;
	return memcmp(myself->handle->f_handle,
		      other->handle->f_handle,
		      myself->handle->handle_size) ? false : true;
}

/* /\* file_truncate */
/*  * truncate a file to the size specified. */
/*  * size should really be off_t... */
/*  *\/ */

/* static fsal_status_t file_truncate(struct fsal_obj_handle *obj_hdl, */
/*                                    const struct req_op_context *opctx, */
/* 				   uint64_t length) */
/* { */
/* 	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR; */
/*         fsal_status_t status; */
/* 	struct gpfs_fsal_obj_handle *myself; */
/* /\* 	int mount_fd; *\/ */
/* 	int retval = 0; */

/* 	if(obj_hdl->type != REGULAR_FILE) { */
/* 		fsal_error = ERR_FSAL_INVAL; */
/* 		goto errout; */
/* 	} */
/* 	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle); */
/* /\* 	mount_fd = gpfs_get_root_fd(obj_hdl->export); *\/ */
	
/*         status = GPFSFSAL_truncate(obj_hdl->export, myself->handle, opctx, length, NULL); */
/*         return (status); */

/* errout: */
/* 	return fsalstat(fsal_error, retval);	 */
/* } */

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
                                 const struct req_op_context *opctx,
				 const char *name)
{
        fsal_status_t status;

        status = GPFSFSAL_unlink(dir_hdl, name, opctx, NULL);

	return(status);	
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
	uint32_t ino32;
	uint64_t ino64;
	const struct gpfs_fsal_obj_handle *myself;
	const struct gpfs_file_handle *fh;
	size_t fh_size;

	/* sanity checks */
        if( !fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself = container_of(obj_hdl, const struct gpfs_fsal_obj_handle, obj_handle);
	fh = myself->handle;

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = gpfs_sizeof_handle(fh);
                if(fh_desc->len < fh_size)
                        goto errout;
                memcpy(fh_desc->addr, fh, fh_size);
		break;
	case FSAL_DIGEST_FILEID2:
		fh_size = FSAL_DIGEST_SIZE_FILEID2;
		if(fh_desc->len < fh_size)
			goto errout;
                ino32 = obj_hdl->attributes.fileid;
		memcpy(fh_desc->addr, &ino32, fh_size);
		break;
	case FSAL_DIGEST_FILEID3:
		fh_size = FSAL_DIGEST_SIZE_FILEID3;
		if(fh_desc->len < fh_size)
			goto errout;
                ino64 = obj_hdl->attributes.fileid;
		memcpy(fh_desc->addr, &ino64, fh_size);
		break;
	case FSAL_DIGEST_FILEID4:
		fh_size = FSAL_DIGEST_SIZE_FILEID4;
		if(fh_desc->len < fh_size)
			goto errout;
                ino64 = obj_hdl->attributes.fileid;
		memcpy(fh_desc->addr, &ino64, fh_size);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

errout:
	LogMajor(COMPONENT_FSAL,
		 "Space too small for handle.  need %lu, have %lu",
		 fh_size, fh_desc->len);

	return fsalstat(ERR_FSAL_TOOSMALL, 0);
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
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = myself->handle->handle_key_size;
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct gpfs_fsal_obj_handle *myself;
	int retval = 0;
	object_file_type_t type = obj_hdl->type;

	if(type == REGULAR_FILE) {
		gpfs_close(obj_hdl);
	}
	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

        if(type == REGULAR_FILE &&
           (myself->u.file.fd >=0 || myself->u.file.openflags != FSAL_O_CLOSED)) {
                LogCrit(COMPONENT_FSAL,
                        "Tried to release busy handle, "
                        "hdl = 0x%p, fd = %d, openflags = 0x%x",
                        obj_hdl,
                        myself->u.file.fd, myself->u.file.openflags);
                return fsalstat(posix2fsal_error(EINVAL), EINVAL);
        }

        retval = fsal_obj_handle_uninit(obj_hdl);
        if (retval != 0) {
                LogCrit(COMPONENT_FSAL,
                        "Tried to release busy handle, "
                        "hdl = 0x%p->refs = %d",
                        obj_hdl, obj_hdl->refs);
                return fsalstat(posix2fsal_error(retval), retval);
        }

	if(type == SYMBOLIC_LINK) {
		if(myself->u.symlink.link_content != NULL)
			free(myself->u.symlink.link_content);
	} else if(gpfs_unopenable_type(type)) {
		if(myself->u.unopenable.name != NULL)
			free(myself->u.unopenable.name);
		if(myself->u.unopenable.dir != NULL)
			free(myself->u.unopenable.dir);
	}
	free(myself);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* gpfs_share_op
 */

static fsal_status_t share_op(struct fsal_obj_handle *obj_hdl,
                             void *p_owner,
                             fsal_share_param_t  request_share)
{
	fsal_status_t status;
	int fd, mntfd;
	struct gpfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	mntfd = fd = myself->u.file.fd;

	status = GPFSFSAL_share_op(mntfd, fd, p_owner, request_share);

	return (status);
}

void gpfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = lookup;
	ops->readdir = read_dirents;
	ops->create = create;
	ops->mkdir = makedir;
	ops->mknode = makenode;
	ops->symlink = makesymlink;
	ops->readlink = readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->link = linkfile;
	ops->rename = renamefile;
	ops->unlink = file_unlink;
	ops->open = gpfs_open;
	ops->status = gpfs_status;
	ops->read = gpfs_read;
	ops->write = gpfs_write;
	ops->commit = gpfs_commit;
	ops->lock_op = gpfs_lock_op;
	ops->share_op = share_op;
	ops->close = gpfs_close;
	ops->lru_cleanup = gpfs_lru_cleanup;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
        handle_ops_pnfs(ops);
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 */

fsal_status_t gpfs_lookup_path(struct fsal_export *exp_hdl,
                              const struct req_op_context *opctx,
			      const char *path,
			      struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        fsal_status_t fsal_status;
	int retval = 0;
	int dir_fd;
	struct stat stat;
	struct gpfs_fsal_obj_handle *hdl;
	char *basepart;
	char *link_content = NULL;
	ssize_t retlink;
	struct gpfs_file_handle *dir_fh = NULL;
        struct attrlist attributes;
	struct gpfs_file_handle *fh = alloca(sizeof(struct gpfs_file_handle));

	memset(fh, 0, sizeof(struct gpfs_file_handle));
	fh->handle_size = OPENHANDLE_HANDLE_LEN;
	if(path == NULL
	   || path[0] != '/'
	   || strlen(path) > PATH_MAX
	   || strlen(path) < 2) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	basepart = rindex(path, '/');
	if(basepart[1] == '\0') {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	fsal_status = fsal_internal_get_handle(path, fh);
	if(FSAL_IS_ERROR(fsal_status))
		return fsal_status;

	if(basepart == path) {
		dir_fd = open("/", O_RDONLY);
	} else {
		char *dirpart = alloca(basepart - path + 1);

		memcpy(dirpart, path, basepart - path);
		dirpart[basepart - path] = '\0';
		dir_fd = open(dirpart, O_RDONLY, 0600);
	}
	if(dir_fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = fstat(dir_fd, &stat);
	if( !S_ISDIR(stat.st_mode)) {  /* this had better be a DIR! */
		goto fileerr;
	}
	basepart++;
	fsal_status = fsal_internal_get_handle_at(dir_fd, basepart, fh);
	if(FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	/* what about the file? Do no symlink chasing here. */
	retval = fstatat(dir_fd, basepart, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}
        attributes.mask = exp_hdl->ops->fs_supported_attrs(exp_hdl);
	fsal_status = posix2fsal_attributes(&stat, &attributes);
	if(FSAL_IS_ERROR(fsal_status))
		goto fileerr;

	if(S_ISLNK(stat.st_mode)) {
		link_content = malloc(PATH_MAX + 1);
		retlink = readlinkat(dir_fd, basepart,
				     link_content, PATH_MAX);
		if(retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if(retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			goto linkerr;
		}
		link_content[retlink] = '\0';
	}
	close(dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &attributes, NULL, NULL, NULL, exp_hdl);
	if(link_content != NULL)
		free(link_content);
	if(dir_fh != NULL)
		free(dir_fh);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL; /* poison it */
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	retval = errno;
linkerr:
	if(link_content != NULL)
		free(link_content);
	if(dir_fh != NULL)
		free(dir_fh);
	close(dir_fd);
	fsal_error = posix2fsal_error(retval);

errout:
	return fsalstat(fsal_error, retval);	
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

fsal_status_t gpfs_create_handle(struct fsal_export *exp_hdl,
                                const struct req_op_context *opctx,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle)
{
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *hdl;
	struct gpfs_file_handle  *fh;
        struct attrlist attrib;
	int mount_fd = gpfs_get_root_fd(exp_hdl);
	char *link_content = NULL;
	ssize_t retlink = PATH_MAX;
	char link_buff[PATH_MAX + 1];

	*handle = NULL; /* poison it first */
	if((hdl_desc->len > (sizeof(struct gpfs_file_handle))))
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len);  /* struct aligned copy */

        attrib.mask = exp_hdl->ops->fs_supported_attrs(exp_hdl);
        status = GPFSFSAL_getattrs(exp_hdl, opctx, fh, &attrib);
        if(FSAL_IS_ERROR(status))
          return(status);

	if(attrib.type == SYMBOLIC_LINK) { /* I could lazy eval this... */

                status = fsal_readlink_by_handle(mount_fd, fh,
                                                 link_buff, &retlink);
		if(FSAL_IS_ERROR(status))
			return(status);
			
		if(retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if(retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = link_buff;
	}
	hdl = alloc_handle(fh, &attrib, link_content, NULL, NULL, exp_hdl);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	
errout:
	return fsalstat(fsal_error, retval);	
}

