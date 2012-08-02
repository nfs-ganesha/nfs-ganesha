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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* handle.c
 * VFS object (file|dir) handle object
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <FSAL/FSAL_VFS/fsal_handle_syscalls.h>
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include <FSAL/FSAL_LUSTRE/fsal_handle.h>
#include "lustre_methods.h"


#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>


/* helpers
 */

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */

static struct lustre_fsal_obj_handle *alloc_handle(struct lustre_file_handle *fh,
                                                   struct stat *stat,
                                                   const char *link_content,
                                                   struct lustre_file_handle *dir_fh,
                                                   const char *sock_name,
                                                   struct fsal_export *exp_hdl)
{
	struct lustre_fsal_obj_handle *hdl;
	fsal_status_t st;

	hdl = malloc(sizeof(struct lustre_fsal_obj_handle) +
		     sizeof(struct lustre_file_handle) ) ;
	if(hdl == NULL)
		return NULL;
	memset(hdl, 0, (sizeof(struct lustre_fsal_obj_handle) +
			sizeof(struct lustre_file_handle) ) ) ;
	hdl->handle = (struct lustre_file_handle *)&hdl[1];
	memcpy(hdl->handle, fh,
	       sizeof(struct lustre_file_handle) );
	hdl->obj_handle.type = posix2fsal_type(stat->st_mode);
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
	} else if(hdl->obj_handle.type == SOCKET_FILE
		  && dir_fh != NULL
		  && sock_name != NULL) {
		hdl->u.sock.sock_dir = malloc(sizeof(struct lustre_file_handle)) ;
		if(hdl->u.sock.sock_dir == NULL)
			goto spcerr;
		memcpy(hdl->u.sock.sock_dir,
		       dir_fh,
		       sizeof(struct lustre_file_handle) );
		hdl->u.sock.sock_name = malloc(strlen(sock_name) + 1);
		if(hdl->u.sock.sock_name == NULL)
			goto spcerr;
		strcpy(hdl->u.sock.sock_name, sock_name);
	}
	hdl->obj_handle.export = exp_hdl;
	hdl->obj_handle.attributes.mask
		= exp_hdl->ops->fs_supported_attrs(exp_hdl);
	hdl->obj_handle.attributes.supported_attributes
                = hdl->obj_handle.attributes.mask;
	st = posix2fsal_attributes(stat, &hdl->obj_handle.attributes);
	if(FSAL_IS_ERROR(st))
		goto spcerr;
	if(!fsal_obj_handle_init(&hdl->obj_handle,
				 exp_hdl->fsal->obj_ops,
				 exp_hdl,
	                         posix2fsal_type(stat->st_mode)))
                return hdl;

	hdl->obj_handle.ops = NULL;
	pthread_mutex_unlock(&hdl->obj_handle.lock);
	pthread_mutex_destroy(&hdl->obj_handle.lock);
spcerr:
	if(hdl->obj_handle.type == SYMBOLIC_LINK) {
		if(hdl->u.symlink.link_content != NULL)
			free(hdl->u.symlink.link_content);
	} else if(hdl->obj_handle.type == SOCKET_FILE) {
		if(hdl->u.sock.sock_name != NULL)
			free(hdl->u.sock.sock_name);
		if(hdl->u.sock.sock_dir != NULL)
			free(hdl->u.sock.sock_dir);
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
			    const char *path,
			    struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct stat stat;
	char *link_content = NULL;
	struct lustre_file_handle *dir_hdl = NULL;
	const char *sock_name = NULL;
	ssize_t retlink;
        char fidpath[MAXPATHLEN] ;
	char link_buff[1024];
	struct lustre_file_handle *fh
		= alloca(sizeof(struct lustre_file_handle));

	if( !path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	memset(fh, 0, sizeof(struct lustre_file_handle));
	parent_hdl = container_of(parent, struct lustre_fsal_obj_handle, obj_handle);
	if( !parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

        retval = lustre_name_to_handle_at( lustre_get_root_path( parent->export ), parent_hdl->handle, path, fh,  0);
        if(retval < 0) {
                retval = errno;
                fsal_error = posix2fsal_error(retval);
                goto errout;
        }
 
        retval = lustre_handle_to_path( lustre_get_root_path( parent->export ), fh, fidpath ) ;
        if(retval < 0) {
                retval = errno;
                fsal_error = posix2fsal_error(retval);
                goto errout;
        }

	retval = lstat(fidpath, &stat );
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	if(S_ISLNK(stat.st_mode)) { /* I could lazy eval this... */
		retlink = readlink( fidpath, link_buff, 1024);
		if(retlink < 0 || retlink == 1024) {
			retval = errno;
			if(retlink == 1024)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = &link_buff[0];
	} else if(S_ISSOCK(stat.st_mode)) {
		dir_hdl = parent_hdl->handle;
		sock_name = path;
	}
	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat,
			   link_content,
			   dir_hdl,
			   sock_name,
			   parent->export);
	if(hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL; /* poison it */
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
errout:
	return fsalstat(fsal_error, retval);	
}


/* make_file_safe
 * the file/dir got created mode 0, uid root (me)
 * which leaves it inaccessible. Set ownership first
 * followed by mode.
 * could use setfsuid/gid around the mkdir/mknod/openat
 * but that only works on Linux and is more syscalls
 * 5 (set uid/gid, create, unset uid/gid) vs. 3
 * NOTE: this way escapes quotas however we do check quotas
 * first in cache_inode_*
 */

static inline
int make_file_safe( char * mntpath,
                    struct lustre_file_handle * infh,
		    const char *name,
		    mode_t unix_mode,
		    uid_t user,
		    gid_t group,
		    struct lustre_file_handle *fh,
		    struct stat *stat)
{
	int retval;
        char lustre_path[MAXPATHLEN] ;
        char path_name[MAXPATHLEN] ;

        retval = lustre_handle_to_path( mntpath, infh, lustre_path ) ;
	if(retval < 0) {
		goto fileerr;
	}
	
	retval = lchown( lustre_path, user, group ) ;
	if(retval < 0) {
		goto fileerr;
	}
        
	/* now that it is owned properly, set accessible mode */
	
	retval = chmod( lustre_path, unix_mode );
	if(retval < 0) {
                retval = errno ;
		goto fileerr;
	}

	retval = lustre_name_to_handle_at( mntpath, infh, name, fh, 0);
	if(retval < 0) {
		goto fileerr;
	}

        snprintf( path_name, MAXPATHLEN, "%s/%s", lustre_path, name ) ;

	retval = lstat( path_name, stat);
	if(retval < 0) {
                retval = errno ;
		goto fileerr;
	}

fileerr:
	retval = errno;
	return retval;
}
/* create
 * create a regular file and set its attributes
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
                            const char *name,
                            struct attrlist *attrib,
                            struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
        char newpath[MAXPATHLEN] ;
        char dirpath[MAXPATHLEN] ;
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        int fd ;
	uid_t user;
	gid_t group;
	struct lustre_file_handle *fh
		= alloca(sizeof(struct lustre_file_handle) );

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle) );
	myself = container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
        retval = lustre_handle_to_path( lustre_get_root_path( dir_hdl->export ), myself->handle, dirpath ) ;
	if(retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = lstat(dirpath, &stat);
	if(retval < 0) {
                retval = errno ;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */

	/* create it with no access because we are root when we do this
	 * we use openat because there is no creatat...
	 */
        snprintf( newpath, MAXPATHLEN, "%s/%s", dirpath, name ) ;
	fd = open( newpath, O_CREAT|O_WRONLY|O_TRUNC|O_EXCL, 0000);
	if(fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
        close( fd ) ; /* not needed anymore */

	retval = make_file_safe( lustre_get_root_path( dir_hdl->export), 
                                 myself->handle, name, unix_mode, user, group, fh, &stat);
	if(retval < 0) {
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, NULL, NULL, dir_hdl->export);
	if(hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	fsal_error = posix2fsal_error(retval);
	unlink( newpath ) ;  /* remove the evidence on errors */
errout:
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name,
			     struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
        char dirpath[MAXPATHLEN] ;
        char newpath[MAXPATHLEN] ;
	struct stat stat;
	mode_t unix_mode;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	struct lustre_file_handle *fh
		= alloca(sizeof(struct lustre_file_handle) );

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle) );
	myself = container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
        retval = lustre_handle_to_path( lustre_get_root_path( dir_hdl->export ), myself->handle, dirpath ) ;
	if( retval < 0) {
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = lstat( dirpath,  &stat );
	if(retval < 0) {
                retval = errno ;
		goto direrr;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */

	/* create it with no access because we are root when we do this */
        snprintf( newpath, MAXPATHLEN, "%s/%s", dirpath, name ) ;
	retval = mkdir( newpath, 0000);
	if(retval < 0) {
                retval = errno ;
		goto direrr;
	}
	retval = make_file_safe( lustre_get_root_path( dir_hdl->export), myself->handle, name, unix_mode, user, group, fh, &stat);
	if(retval < 0) {
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, NULL, NULL, dir_hdl->export);
	if(hdl != NULL) {
		*handle = &hdl->obj_handle;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
direrr:
	retval = errno;
	fsal_error = posix2fsal_error(retval);

	return fsalstat(fsal_error, retval);	

fileerr:
	fsal_error = posix2fsal_error(retval);
	rmdir( newpath );  /* remove the evidence on errors */
errout:
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
                              const char *name,
                              object_file_type_t nodetype,  /* IN */
                              fsal_dev_t *dev,  /* IN */
                              struct attrlist *attrib,
                              struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *myself, *hdl;
        char dirpath[MAXPATHLEN] ;
        char newpath[MAXPATHLEN] ;
	struct stat stat;
	mode_t unix_mode, create_mode = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	dev_t unix_dev = 0;
	struct lustre_file_handle *dir_fh = NULL;
	const char *sock_name = NULL;
	struct lustre_file_handle *fh
		= alloca(sizeof(struct lustre_file_handle) );

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);


		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle) );
	myself = container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
	unix_mode = fsal2unix_mode(attrib->mode)
		& ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
	switch (nodetype) {
	case BLOCK_FILE:
		if( !dev) {
			fsal_error = ERR_FSAL_FAULT;
			goto errout;
		}
		create_mode = S_IFBLK;
		unix_dev = makedev(dev->major, dev->minor);
		break;
	case CHARACTER_FILE:
		if( !dev) {
 			fsal_error = ERR_FSAL_FAULT;
			goto errout;


		}
		create_mode = S_IFCHR;
		unix_dev = makedev(dev->major, dev->minor);
		break;
	case FIFO_FILE:
		create_mode = S_IFIFO;
		break;
	case SOCKET_FILE:
		create_mode = S_IFSOCK;
		dir_fh = myself->handle;
                sock_name = name;
		break;
	default:
		LogMajor(COMPONENT_FSAL,
			 "Invalid node type in FSAL_mknode: %d",
			 nodetype);
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
        retval = lustre_handle_to_path( lustre_get_root_path( dir_hdl->export ), myself->handle, dirpath ) ;
	if( retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = lstat( dirpath, &stat);
	if(retval < 0) {
                retval = errno ;
		goto direrr;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */

	/* create it with no access because we are root when we do this */
        snprintf( newpath, MAXPATHLEN, "%s/%s", dirpath, name ) ;
	retval = mknod( newpath, create_mode, unix_dev);
	if(retval < 0) {
                retval = errno ;
		goto direrr;
	}
	retval = make_file_safe(lustre_get_root_path( dir_hdl->export),myself->handle, name, unix_mode, user, group, fh, &stat);
	if(retval < 0) {
		goto direrr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, dir_fh, sock_name, dir_hdl->export);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
	
direrr:
	fsal_error = posix2fsal_error(retval);
errout:
	unlink( newpath);
	return fsalstat(fsal_error, retval);	
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
	struct lustre_fsal_obj_handle *myself, *hdl;
        char dirpath[MAXPATHLEN] ;
        char newpath[MAXPATHLEN] ;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	uid_t user;
	gid_t group;
	struct lustre_file_handle *fh
		= alloca(sizeof(struct lustre_file_handle)  );

	*handle = NULL; /* poison it first */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct lustre_file_handle) );
	myself = container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	user = attrib->owner;
	group = attrib->group;
        retval = lustre_handle_to_path( lustre_get_root_path( dir_hdl->export ), myself->handle, dirpath ) ;
	if( retval < 0) {
		goto errout;
	}
	retval = lstat(dirpath, &stat );
	if(retval < 0) {
		goto direrr;
	}
	if(stat.st_mode & S_ISGID)
		group = -1; /*setgid bit on dir propagates dir group owner */
	
	/* create it with no access because we are root when we do this */
        snprintf( newpath, MAXPATHLEN, "%s/%s", dirpath, name ) ;
	retval = symlink(link_path, newpath );
	if(retval < 0) {
		goto direrr;
	}
	/* do this all by hand because we can't use fchmodat on symlinks...
	 */
	retval = lchown( newpath, user, group );
	if(retval < 0) {
		goto linkerr;
	}

	retval = lustre_name_to_handle_at( lustre_get_root_path( dir_hdl->export),myself->handle, name, fh, 0);
	if(retval < 0) {
		goto linkerr;
	}
	/* now get attributes info, being careful to get the link, not the target */
	retval = lstat(newpath, &stat);
	if(retval < 0) {
		goto linkerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, link_path, NULL, NULL, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

linkerr:
	retval = errno;
	unlink( newpath);
	goto errout;

direrr:
	retval = errno;
errout:
	if(retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
                                 char *link_content,
                                 size_t *link_len,
                                 bool_t refresh)
{
	struct lustre_fsal_obj_handle *myself = NULL;
        char mypath[MAXPATHLEN] ;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if(obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	if(refresh) { /* lazy load or LRU'd storage */
		ssize_t retlink;
		char link_buff[1024];

		if(myself->u.symlink.link_content != NULL) {
			free(myself->u.symlink.link_content);
			myself->u.symlink.link_content = NULL;
			myself->u.symlink.link_size = 0;
		}
                retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), myself->handle, mypath ) ;
		if( retval < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto out;
		}
		retlink = readlink( mypath, link_buff, 1024);
		if(retlink < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto out;
		}

		myself->u.symlink.link_content = malloc(retlink + 1);
		if(myself->u.symlink.link_content == NULL) {
			fsal_error = ERR_FSAL_NOMEM;
			goto out;
		}
		memcpy(myself->u.symlink.link_content, link_buff, retlink);
		myself->u.symlink.link_content[retlink] = '\0';
		myself->u.symlink.link_size = retlink + 1;
	}
	if(myself->u.symlink.link_content == NULL
	   || *link_len <= myself->u.symlink.link_size) {
		fsal_error = ERR_FSAL_FAULT; /* probably a better error?? */
		goto out;
	}
	memcpy(link_content,
	       myself->u.symlink.link_content,
	       myself->u.symlink.link_size);

out:
	*link_len = myself->u.symlink.link_size;
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	struct lustre_fsal_obj_handle *myself, *destdir;
        char srcpath[MAXPATHLEN] ;
        char destdirpath[MAXPATHLEN] ;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	if( !obj_hdl->export->ops->fs_supports(obj_hdl->export, link_support)) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
        retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), myself->handle, srcpath ) ;
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	destdir = container_of(destdir_hdl, struct lustre_fsal_obj_handle, obj_handle);
        retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), destdir->handle, destdirpath ) ;
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	retval = link(srcpath, destdirpath);
	if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
out:
	return fsalstat(fsal_error, retval);	
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
 * @param entry_cnt [IN] limit of entries. 0 implies no limit
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker TRUE == end of dir
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  uint32_t entry_cnt,
				  struct fsal_cookie *whence,
				  void *dir_state,
				  fsal_status_t (*cb)(
					  const char *name,
					  unsigned int dtype,
					  struct fsal_obj_handle *dir_hdl,
					  void *dir_state,
					  struct fsal_cookie *cookie),
                                  bool_t *eof)
{
	struct lustre_fsal_obj_handle *myself;
	int dirfd ;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t status;
	int retval = 0;
	off_t seekloc = 0;
	int bpos, cnt, nread;
	unsigned char d_type;
	struct linux_dirent *dentry;
	struct fsal_cookie *entry_cookie;
	char buf[BUF_SIZE];

	if(whence != NULL) {
		if(whence->size != sizeof(off_t)) {
			fsal_error = posix2fsal_error(EINVAL);
			retval = errno;
			goto out;
		}
		memcpy(&seekloc, whence->cookie, sizeof(off_t));
	}
	entry_cookie = alloca(sizeof(struct fsal_cookie) + sizeof(off_t));
	myself = container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
	dirfd = lustre_open_by_handle( lustre_get_root_path( dir_hdl->export),myself->handle, (O_RDONLY|O_DIRECTORY));
	if(dirfd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
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
			d_type = *(buf + bpos + dentry->d_reclen - 1);
			entry_cookie->size = sizeof(off_t);
			memcpy(&entry_cookie->cookie, &dentry->d_off, sizeof(off_t));

			/* callback to cache inode */
			status = cb(dentry->d_name,
				    d_type,
				    dir_hdl,
				    dir_state, entry_cookie);
			if(FSAL_IS_ERROR(status)) {
				fsal_error = status.major;
				retval = status.minor;
				goto done;
			}
		skip:
			bpos += dentry->d_reclen;
			cnt++;
			if(entry_cnt > 0 && cnt >= entry_cnt)
				goto done;
		}
	} while(nread > 0);

	*eof = nread == 0 ? TRUE : FALSE;
done:
	close(dirfd);
	
out:
	return fsalstat(fsal_error, retval);	
}


static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	struct lustre_fsal_obj_handle *olddir, *newdir;
        char olddirpath[MAXPATHLEN] ;
        char oldnamepath[MAXPATHLEN] ;
        char newdirpath[MAXPATHLEN] ;
        char newnamepath[MAXPATHLEN] ;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	olddir = container_of(olddir_hdl, struct lustre_fsal_obj_handle, obj_handle);
        retval = lustre_handle_to_path( lustre_get_root_path( olddir_hdl->export ), olddir->handle, olddirpath ) ;
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	newdir = container_of(newdir_hdl, struct lustre_fsal_obj_handle, obj_handle);
        retval = lustre_handle_to_path( lustre_get_root_path( newdir_hdl->export ), newdir->handle, newdirpath ) ;
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
        snprintf( oldnamepath, MAXPATHLEN, "%s/%s", olddirpath, new_name ) ;
        snprintf( newnamepath, MAXPATHLEN, "%s/%s", newdirpath, new_name ) ;
	retval = rename(oldnamepath, newnamepath);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
out:
	return fsalstat(fsal_error, retval);	
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
                              struct attrlist *obj_attr)
{
	struct lustre_fsal_obj_handle *myself;
        char mypath[MAXPATHLEN] ;
	int open_flags = O_RDONLY;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t st;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	if(obj_hdl->type == REGULAR_FILE) {
		if(myself->u.file.fd < 0) {
			goto open_file;  /* no file open at the moment */
		}
		fstat(myself->u.file.fd, &stat);
	} else if(obj_hdl->type == SOCKET_FILE) {
                retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), 
                                                myself->u.sock.sock_dir, mypath ) ;
		if(retval < 0) {
			goto errout;
		}
		retval = lstat( mypath, &stat ) ;
		if(retval < 0) {
			goto errout;
		}
	} else {
		if(obj_hdl->type == SYMBOLIC_LINK)
			open_flags |= O_PATH;
		else if(obj_hdl->type == FIFO_FILE)
			open_flags |= O_NONBLOCK;
	open_file:
                retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), myself->handle, mypath ) ;
		if(retval < 0) {
			goto errout;
		}
		retval = lstat( mypath, 
				&stat ) ;
		if(retval < 0) {
			goto errout;
		}
	}

	/* convert attributes */
	obj_hdl->attributes.mask = obj_attr->mask;
	st = posix2fsal_attributes(&stat, &obj_hdl->attributes);
	if(FSAL_IS_ERROR(st)) {
		FSAL_CLEAR_MASK(obj_attr->mask);
		FSAL_SET_MASK(obj_attr->mask,
			      ATTR_RDATTR_ERR);
		fsal_error = st.major;  retval = st.minor;
		goto out;
	}
	memcpy(obj_attr, &obj_hdl->attributes, sizeof(struct attrlist));
	goto out;

errout:
        retval = errno;
        if(retval == ENOENT)
                fsal_error = ERR_FSAL_STALE;
        else
                fsal_error = posix2fsal_error(retval);
out:
	return fsalstat(fsal_error, retval);	
}

/*
 * NOTE: this is done under protection of the attributes rwlock in the cache entry.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	struct lustre_fsal_obj_handle *myself;
        char mypath[MAXPATHLEN] ;
        char mysockpath[MAXPATHLEN] ;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	/* apply umask, if mode attribute is to be changed */
	if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		attrs->mode
			&= ~obj_hdl->export->ops->fs_umask(obj_hdl->export);
	}
	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

	/* This is yet another "you can't get there from here".  If this object
	 * is a socket (AF_UNIX), an fd on the socket s useless _period_.
	 * If it is for a symlink, without O_PATH, you will get an ELOOP error
	 * and (f)chmod doesn't work for a symlink anyway - not that it matters
	 * because access checking is not done on the symlink but the final target.
	 * AF_UNIX sockets are also ozone material.  If the socket is already active
	 * listeners et al, you can manipulate the mode etc.  If it is just sitting
	 * there as in you made it with a mknod (one of those leaky abstractions...)
	 * or the listener forgot to unlink it, it is lame duck.
	 */

	if(obj_hdl->type == SOCKET_FILE) {
                retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), myself->u.sock.sock_dir, mypath ) ;
		if(retval < 0) {
			retval = errno;
			if(retval == ENOENT)
				fsal_error = ERR_FSAL_STALE;
			else
				fsal_error = posix2fsal_error(retval);
			goto out;
		}
		retval = lstat( mypath,
				&stat ) ;
	} else {
                retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), myself->handle, mypath ) ;
		if( retval < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
			goto out;
		}
		retval = lstat( mypath, &stat);
	}
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	/** CHMOD **/
	if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		/* The POSIX chmod call doesn't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if(!S_ISLNK(stat.st_mode)) {
			if(obj_hdl->type == SOCKET_FILE)
                          {
                                snprintf( mysockpath, MAXPATHLEN, "%s/%s", mypath, myself->u.sock.sock_name ) ;
				retval = lchmod( mysockpath,
						 fsal2unix_mode(attrs->mode));
                          }   
			else
				retval = lchmod(mypath, fsal2unix_mode(attrs->mode));

			if(retval != 0) {
				goto fileerr;
			}
		}
	}
		
	/**  CHOWN  **/
	if(FSAL_TEST_MASK(attrs->mask,
			  ATTR_OWNER | ATTR_GROUP)) {
		uid_t user = FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)
                        ? (int)attrs->owner : -1;
		gid_t group = FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)
                        ? (int)attrs->group : -1;

		if(obj_hdl->type == SOCKET_FILE)
                  {
                     snprintf( mysockpath, MAXPATHLEN, "%s/%s", mypath, myself->u.sock.sock_name ) ;
		     retval = lchown( mysockpath,
				      user,
				      group ) ;
                  }
		else
			retval = lchown(mypath, user, group);

		if(retval) {
			goto fileerr;
		}
	}
		
	/**  UTIME  **/
	if(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME | ATTR_MTIME)) {
		struct timeval timebuf[2];

		/* Atime */
		timebuf[0].tv_sec =
			(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME) ?
                         (time_t) attrs->atime.seconds : stat.st_atime);
		timebuf[0].tv_usec = 0;

		/* Mtime */
		timebuf[1].tv_sec =
			(FSAL_TEST_MASK(attrs->mask, ATTR_MTIME) ?
			 (time_t) attrs->mtime.seconds : stat.st_mtime);
		timebuf[1].tv_usec = 0;
		if(obj_hdl->type == SOCKET_FILE)
                  {
                     snprintf( mysockpath, MAXPATHLEN, "%s/%s", mypath, myself->u.sock.sock_name ) ;
		     retval = utimes( mysockpath, timebuf ) ;
                  }
		else
			retval = utimes(mypath, timebuf);
		if(retval != 0) {
			goto fileerr;
		}
	}
	return fsalstat(fsal_error, retval);	

fileerr:
        retval = errno;
        fsal_error = posix2fsal_error(retval);
out:
	return fsalstat(fsal_error, retval);	
}

/* compare
 * compare two handles.
 * return TRUE for equal, FALSE for anything else
 */
static bool_t compare(struct fsal_obj_handle *obj_hdl,
                      struct fsal_obj_handle *other_hdl)
{
	struct lustre_fsal_obj_handle *myself, *other;

	if( !other_hdl)
		return FALSE;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	other = container_of(other_hdl, struct lustre_fsal_obj_handle, obj_handle);
	if( (obj_hdl->type             != other_hdl->type)          ||
	    (myself->handle->fid.f_seq != other->handle->fid.f_seq) ||
	    (myself->handle->fid.f_oid != other->handle->fid.f_oid) ||
	    (myself->handle->fid.f_ver != other->handle->fid.f_ver) ||
	    (myself->handle->inode     != other->handle->inode) )
		return FALSE;

        return TRUE ;
}

/* file_truncate
 * truncate a file to the size specified.
 * size should really be off_t...
 */

static fsal_status_t file_truncate(struct fsal_obj_handle *obj_hdl,
				   uint64_t length)
{
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        char mypath[MAXPATHLEN] ;
	int retval = 0;

	if(obj_hdl->type != REGULAR_FILE) {
		fsal_error = ERR_FSAL_INVAL;
		goto errout;
	}
	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
        retval = lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), myself->handle, mypath ) ;
	if(retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = truncate( mypath, length);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
errout:
	return fsalstat(fsal_error, retval);	
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        char dirpath[MAXPATHLEN] ;
        char filepath[MAXPATHLEN] ;
	struct stat stat;
	int retval = 0;

	myself = container_of(dir_hdl, struct lustre_fsal_obj_handle, obj_handle);
        retval = lustre_handle_to_path( lustre_get_root_path( dir_hdl->export ), myself->handle, dirpath ) ;
	if(retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto out;
	}
        snprintf( filepath, MAXPATHLEN, "%s/%s", dirpath, name ) ;
	retval = lstat( filepath, &stat );
	if(retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
		goto out;
	}
	retval = unlink( filepath ) ;
	if(retval < 0) {
		retval = errno;
		if(retval == ENOENT)
			fsal_error = ERR_FSAL_STALE;
		else
			fsal_error = posix2fsal_error(retval);
	}
	
out:
	return fsalstat(fsal_error, retval);	
}


/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t handle_digest(struct fsal_obj_handle *obj_hdl,
                                   fsal_digesttype_t output_type,
                                   struct gsh_buffdesc *fh_desc)
{
	uint32_t ino32;
	uint64_t ino64;
	struct lustre_fsal_obj_handle *myself;
	struct lustre_file_handle *fh;
	size_t fh_size;

	/* sanity checks */
        if( !fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	fh = myself->handle;

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = lustre_sizeof_handle(fh);
                if(fh_desc->len < fh_size)
                        goto errout;
                memcpy(fh_desc->addr, fh, fh_size);
		break;
	case FSAL_DIGEST_FILEID2:
		fh_size = FSAL_DIGEST_SIZE_FILEID2;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, fh, fh_size);
		break;
	case FSAL_DIGEST_FILEID3:
		fh_size = FSAL_DIGEST_SIZE_FILEID3;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(&ino32, fh, sizeof(ino32));
		ino64 = ino32;
		memcpy(fh_desc->addr, &ino64, fh_size);
		break;
	case FSAL_DIGEST_FILEID4:
		fh_size = FSAL_DIGEST_SIZE_FILEID4;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(&ino32, fh, sizeof(ino32));
		ino64 = ino32;
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
	struct lustre_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = lustre_sizeof_handle(myself->handle);
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct fsal_export *exp = obj_hdl->export;
	struct lustre_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
	pthread_mutex_lock(&obj_hdl->lock);
	obj_hdl->refs--;  /* subtract the reference when we were created */
	if(obj_hdl->refs != 0 || (obj_hdl->type == REGULAR_FILE
				  && (myself->u.file.fd >=0
				      || myself->u.file.openflags != FSAL_O_CLOSED))) {
		pthread_mutex_unlock(&obj_hdl->lock);
		retval = obj_hdl->refs > 0 ? EBUSY : EINVAL;
		LogCrit(COMPONENT_FSAL,
			"Tried to release busy handle, "
			"hdl = 0x%p->refs = %d, fd = %d, openflags = 0x%x",
			obj_hdl, obj_hdl->refs,
			myself->u.file.fd, myself->u.file.openflags);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	fsal_detach_handle(exp, &obj_hdl->handles);
	pthread_mutex_unlock(&obj_hdl->lock);
	pthread_mutex_destroy(&obj_hdl->lock);
	myself->obj_handle.ops = NULL; /*poison myself */
	myself->obj_handle.export = NULL;
	if(obj_hdl->type == SYMBOLIC_LINK) {
		if(myself->u.symlink.link_content != NULL)
			free(myself->u.symlink.link_content);
	} else if(obj_hdl->type == SOCKET_FILE) {
		if(myself->u.sock.sock_name != NULL)
			free(myself->u.sock.sock_name);
		if(myself->u.sock.sock_dir != NULL)
			free(myself->u.sock.sock_dir);
	}
	free(myself);
	return fsalstat(fsal_error, 0);
}

void lustre_handle_ops_init(struct fsal_obj_ops *ops)
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
	ops->truncate = file_truncate;
	ops->open = lustre_open;
	ops->status = lustre_status;
	ops->read = lustre_read;
	ops->write = lustre_write;
	ops->commit = lustre_commit;
	ops->lock_op = lustre_lock_op;
	ops->close = lustre_close;
	ops->lru_cleanup = lustre_lru_cleanup;
	ops->compare = compare;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
}

/* export methods that create object handles
 */

/* lookup_path
 * modeled on old api except we don't stuff attributes.
 * KISS
 * @todo : use of dirfd is no more needed with FSAL_LUSTRE
 */

fsal_status_t lustre_lookup_path(struct fsal_export *exp_hdl,
			      const char *path,
			      struct fsal_obj_handle **handle)
{
	int dir_fd;
	struct stat stat;
	struct lustre_fsal_obj_handle *hdl;
	char *basepart;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	char *link_content = NULL;
	ssize_t retlink;
	struct lustre_file_handle *dir_fh = NULL;
	char *sock_name = NULL;
        char dirpart[MAXPATHLEN] ;
        char dirfullpath[MAXPATHLEN] ;
	struct lustre_file_handle *fh
		= alloca(sizeof(struct lustre_file_handle) );

	memset(fh, 0, sizeof(struct lustre_file_handle) );
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
	if(basepart == path) {
		dir_fd = open("/", O_RDONLY);
	} else {
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
        snprintf( dirfullpath, MAXPATHLEN, "%s/%s", dirpart, basepart ) ;
	retval = lustre_path_to_handle(path, fh);
	if(retval < 0) {
		goto fileerr;
	}

	/* what about the file? Do no symlink chasing here. */
	retval = fstatat(dir_fd, basepart, &stat, AT_SYMLINK_NOFOLLOW);
	if(retval < 0) {
		goto fileerr;
	}
	if(S_ISLNK(stat.st_mode)) {
		link_content = malloc(PATH_MAX);
		retlink = readlinkat(dir_fd, basepart,
				     link_content, PATH_MAX);
		if(retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if(retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			goto linkerr;
		}
		link_content[retlink] = '\0';
	} else if(S_ISSOCK(stat.st_mode)) { /* AF_UNIX sockets require craziness */
		dir_fh = malloc(sizeof(struct lustre_file_handle) );
		memset(dir_fh, 0, sizeof(struct lustre_file_handle) );
		retval = lustre_path_to_handle( path,
					        dir_fh ) ;
		if(retval < 0) {
			goto fileerr;
		}
		sock_name = basepart;
	}
	close(dir_fd);

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, link_content, dir_fh, sock_name, exp_hdl);
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
 * we cannot get an fd on an AF_UNIX socket.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t lustre_create_handle(struct fsal_export *exp_hdl,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle)
{
	struct lustre_fsal_obj_handle *hdl;
	struct stat stat;
	struct lustre_file_handle  *fh;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	int fd;
	char *link_content = NULL;
	ssize_t retlink;
	char link_buff[PATH_MAX];

	

	*handle = NULL; /* poison it first */
	if( hdl_desc->len > sizeof(struct lustre_file_handle) ) 
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len);  /* struct aligned copy */
	fd = lustre_open_by_handle(lustre_get_root_path( exp_hdl ),fh, O_PATH|O_NOACCESS);
	if(fd < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	retval = fstatat(fd, "", &stat, AT_EMPTY_PATH);
	if(retval < 0) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		close(fd);
		goto errout;
	}
	if(S_ISLNK(stat.st_mode)) { /* I could lazy eval this... */
		retlink = readlinkat(fd, "", link_buff, PATH_MAX);
		if(retlink < 0 || retlink == PATH_MAX) {
			retval = errno;
			if(retlink == PATH_MAX)
				retval = ENAMETOOLONG;
			fsal_error = posix2fsal_error(retval);
			close(fd);
			goto errout;
		}
		link_buff[retlink] = '\0';
		link_content = &link_buff[0];
	}
	close(fd);

	hdl = alloc_handle(fh, &stat, link_content, NULL, NULL, exp_hdl);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
		goto errout;
	}
	*handle = &hdl->obj_handle;
	
errout:
	return fsalstat(fsal_error, retval);	
}

