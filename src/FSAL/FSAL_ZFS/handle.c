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
#include "zfs_methods.h"
#include "FSAL/FSAL_ZFS/fsal_types.h"
#include <stdbool.h>


/* helpers
 */

/* alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */
extern size_t i_snapshots;
extern snapshot_t *p_snapshots;
extern pthread_rwlock_t vfs_lock;

libzfswrap_vfs_t *ZFSFSAL_GetVFS(zfs_file_handle_t *handle)
{
  /* This function must be called with the reader lock locked */
  assert(pthread_rwlock_trywrlock(&vfs_lock) != 0);
  /* Check for the zpool (index == 0) */
  if(handle->i_snap == 0)
    return p_snapshots[0].p_vfs;

  /* Handle the indirection */
  int i;
  for(i = 1; i < i_snapshots + 1; i++)
  {
    if(p_snapshots[i].index == handle->i_snap)
    {
      LogFullDebug(COMPONENT_FSAL, "Looking up inside the snapshot n°%d", handle->i_snap);
      return p_snapshots[i].p_vfs;
    }
  }

  LogMajor(COMPONENT_FSAL, "Unable to get the right VFS");
  return NULL;
}

static struct zfs_fsal_obj_handle *alloc_handle(struct zfs_file_handle *fh,
                                                struct zfs_file_handle *dir_fh,
                                                int type,
                                                struct fsal_export *exp_hdl)
{
	struct zfs_fsal_obj_handle *hdl;

	hdl = malloc(sizeof(struct zfs_fsal_obj_handle) +
		     sizeof(struct zfs_file_handle) ) ;
	if(hdl == NULL)
		return NULL;
	memset(hdl, 0, (sizeof(struct zfs_fsal_obj_handle) +
			sizeof(struct zfs_file_handle) ) ) ;
	hdl->handle = (struct zfs_file_handle *)&hdl[1];
	memcpy(hdl->handle, fh,
	       sizeof(struct zfs_file_handle) );
	if(hdl->obj_handle.type == REGULAR_FILE) {
		hdl->u.file.is_closed = true;  /* no open on this yet */
		hdl->u.file.openflags = FSAL_O_CLOSED;
	} 

	hdl->obj_handle.export = exp_hdl;
	hdl->obj_handle.attributes.mask
		= exp_hdl->ops->fs_supported_attrs(exp_hdl);
	hdl->obj_handle.attributes.supported_attributes
                = hdl->obj_handle.attributes.mask;
	if(!fsal_obj_handle_init(&hdl->obj_handle,
				 exp_hdl,
	                         type)) ;
                return hdl;

	hdl->obj_handle.ops = NULL;
	pthread_mutex_unlock(&hdl->obj_handle.lock);
	pthread_mutex_destroy(&hdl->obj_handle.lock);
	free(hdl);  /* elvis has left the building */
	return NULL;
}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t zfs_lookup(struct fsal_obj_handle *parent,
		                const struct req_op_context *opctx,
				const char *path,

				struct fsal_obj_handle **handle)
{
	struct zfs_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct stat stat;
	struct zfs_file_handle *dir_hdl = NULL;
	struct zfs_file_handle *fh
		= alloca(sizeof(struct zfs_file_handle));
       creden_t cred;

	if( !path)
		return fsalstat(ERR_FSAL_FAULT, 0);
	memset(fh, 0, sizeof(struct zfs_file_handle));
	parent_hdl = container_of(parent, struct zfs_fsal_obj_handle, obj_handle);
	if( !parent->ops->handle_is(parent, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

      /* >> Call your filesystem lookup function here << */
      /* >> Be carefull you don't traverse junction nor follow symlinks << */
      inogen_t object;
      int type;
      char i_snap = parent_hdl->handle->i_snap ;

      /* Hook to add the hability to go inside a .zfs directory inside the root dir */
      if(parent_hdl->handle->zfs_handle.inode == 3 &&
          !strcmp(path, ZFS_SNAP_DIR))
      {

        LogDebug(COMPONENT_FSAL, "Lookup for the .zfs/ pseudo-directory");

        object.inode = ZFS_SNAP_DIR_INODE;
        object.generation = 0;
        type = S_IFDIR;
        retval = 0;
      }

      /* Hook for the files inside the .zfs directory */
      else if(parent_hdl->handle->zfs_handle.inode == ZFS_SNAP_DIR_INODE)
      {
        LogDebug(COMPONENT_FSAL, "Lookup inside the .zfs/ pseudo-directory");

        ZFSFSAL_VFS_RDLock();
        int i;
        for(i = 1; i < i_snapshots + 1; i++)
          if(!strcmp(p_snapshots[i].psz_name, path))


        if(i == i_snapshots + 1)
        {
	  return fsalstat(ERR_FSAL_NOTDIR, 0);
        }

        libzfswrap_getroot(p_snapshots[i].p_vfs, &object);
        ZFSFSAL_VFS_Unlock();

        type = S_IFDIR;
        i_snap = i + 1;
        retval = 0;
      }
      else
      {
        /* Get the right VFS */
        ZFSFSAL_VFS_RDLock();
        libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS( parent_hdl->handle );
        if(!p_vfs) {
          retval = ENOENT;
        } else {
	
	  cred.uid = opctx->creds->caller_uid;
	  cred.gid = opctx->creds->caller_gid;
          retval = libzfswrap_lookup( p_vfs,
                                      &cred,
                                      parent_hdl->handle->zfs_handle, 
                                      path,
                                      &object, 
                                      &type );
	}
              
        ZFSFSAL_VFS_Unlock();

        //FIXME!!! Hook to remove the i_snap bit when going up from the .zfs directory
        if(object.inode == 3)
          i_snap = 0;
      }
        /* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh,
			   dir_hdl,
                           type,
			   parent->export);
	if(hdl != NULL) {
		*handle = &hdl->obj_handle ;

                hdl->handle->zfs_handle = object;
                hdl->handle->type = type ;
                hdl->handle->i_snap = 0;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL; /* poison it */
		goto errout;
	}
       cred.uid = opctx->creds->caller_uid;
       cred.gid = opctx->creds->caller_gid;

      retval = libzfswrap_getattr( ZFSFSAL_GetVFS( parent_hdl->handle ), 
                                   &cred,
                                   hdl->handle->zfs_handle, 
                                   &stat, 
                                   &type );

	if(retval ) {
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
errout:
	return fsalstat(fsal_error, retval);	
}

/* lookup_path
 * should not be used for "/" only is exported */

fsal_status_t zfs_lookup_path(struct fsal_export *exp_hdl,
		     	      const struct req_op_context *opctx,
                              const char *path,
			      struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t zfs_create( struct fsal_obj_handle *dir_hdl,
		                 const struct req_op_context *opctx,
                                 const char *name,
                                 struct attrlist *attrib,
                                 struct fsal_obj_handle **handle)
{
	struct zfs_fsal_obj_handle *myself, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct zfs_file_handle *fh
		= alloca(sizeof(struct zfs_file_handle) );
        creden_t cred;
        inogen_t object;

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct zfs_file_handle) );
	myself = container_of(dir_hdl, struct zfs_fsal_obj_handle, obj_handle);

	cred.uid = attrib->owner;
	cred.gid = attrib->group;

        retval = libzfswrap_create( zfs_get_root_pvfs( dir_hdl->export ),
                                    &cred,
                                    myself->handle->zfs_handle,
                                    name,
                                    fsal2unix_mode(attrib->mode),
                                    &object);

	if(retval ) {
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, NULL, REGULAR_FILE, dir_hdl->export);
	if(hdl != NULL)
        {
           /* >> set output handle << */
           hdl->handle->zfs_handle = object ;
           hdl->handle->type = REGULAR_FILE ;
           hdl->handle->i_snap= 0  ;
           *handle = &hdl->obj_handle;
	} 
        else 
        {
	   fsal_error = ERR_FSAL_NOMEM;
	   goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	fsal_error = posix2fsal_error(retval);
errout:
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t zfs_mkdir( struct fsal_obj_handle *dir_hdl,
	                        const struct req_op_context *opctx,
                                const char *name,
                                struct attrlist *attrib,
                                struct fsal_obj_handle **handle)
{
	struct zfs_fsal_obj_handle *myself, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	struct zfs_file_handle *fh
		= alloca(sizeof(struct zfs_file_handle) );
        creden_t cred;
        inogen_t object;

	*handle = NULL; /* poison it */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct zfs_file_handle) );
	myself = container_of(dir_hdl, struct zfs_fsal_obj_handle, obj_handle);

	cred.uid = attrib->owner;
	cred.gid = attrib->group;

        retval = libzfswrap_mkdir( zfs_get_root_pvfs( dir_hdl->export ),
                                   &cred,
                                   myself->handle->zfs_handle,
                                   name,
                                   fsal2unix_mode(attrib->mode),
                                   &object);

	if(retval ) {
		goto fileerr;
	}

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, NULL, DIRECTORY, dir_hdl->export);
	if(hdl != NULL)
        {
           /* >> set output handle << */
           hdl->handle->zfs_handle = object ;
           hdl->handle->type = DIRECTORY ;
           hdl->handle->i_snap= 0  ;
           *handle = &hdl->obj_handle;
	} 
        else 
        {
	   fsal_error = ERR_FSAL_NOMEM;
	   goto errout;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

fileerr:
	fsal_error = posix2fsal_error(retval);
errout:
	return fsalstat(fsal_error, retval);	
}


static fsal_status_t zfs_makenode( struct fsal_obj_handle *dir_hdl,
                                   const struct req_op_context *opctx,
                                   const char *name,
                                   object_file_type_t nodetype,  /* IN */
                                   fsal_dev_t *dev,              /* IN */
                                   struct attrlist *attrib,
                                   struct fsal_obj_handle **handle )
{
     return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/** makesymlink
 *  Note that we do not set mode bits on symlinks for Linux/POSIX
 *  They are not really settable in the kernel and are not checked
 *  anyway (default is 0777) because open uses that target's mode
 */

static fsal_status_t zfs_makesymlink(struct fsal_obj_handle *dir_hdl,
                                     const struct req_op_context *opctx,
                                     const char *name,
                                     const char *link_path,
                                     struct attrlist *attrib,
                                     struct fsal_obj_handle **handle)
{
	struct zfs_fsal_obj_handle *myself, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        creden_t cred;
        inogen_t object;

	struct zfs_file_handle *fh
		= alloca(sizeof(struct zfs_file_handle)  );

	*handle = NULL; /* poison it first */
	if( !dir_hdl->ops->handle_is(dir_hdl, DIRECTORY)) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			dir_hdl);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}
	memset(fh, 0, sizeof(struct zfs_file_handle) );
	myself = container_of(dir_hdl, struct zfs_fsal_obj_handle, obj_handle);
        cred.uid = attrib->owner ;
        cred.gid = attrib->group ;

        retval = libzfswrap_symlink( zfs_get_root_pvfs( dir_hdl->export ),
                                     &cred,
                                     myself->handle->zfs_handle,
                                     name,
                                     link_path,
                                     &object);
	if(retval ) 
		goto err;


	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, NULL, SYMBOLIC_LINK, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto err;
	}
	*handle = &hdl->obj_handle;
        hdl->handle->zfs_handle = object ;
        hdl->handle->type = SYMBOLIC_LINK ;
        hdl->handle->i_snap= 0  ;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

err:
	if(retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t zfs_readsymlink(struct fsal_obj_handle *obj_hdl,
                                 const struct req_op_context *opctx,
                                 char *link_content,
                                 size_t *link_len,
                                 bool refresh)
{
	struct zfs_fsal_obj_handle *myself = NULL;
	int retval = 0;
        int retlink = 0 ;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        creden_t cred;

	if(obj_hdl->type != SYMBOLIC_LINK) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);

        cred.uid = opctx->creds->caller_uid;
	cred.gid = opctx->creds->caller_gid;

        retlink = libzfswrap_readlink( zfs_get_root_pvfs( obj_hdl->export ),
                                       &cred,
                                       myself->handle->zfs_handle,
                                       link_content,
                                       *link_len ) ;

	if(retlink ) {
		fsal_error = posix2fsal_error(retlink);
                goto out ;        
	}

	*link_len = strlen( link_content ) ;
out:
	return fsalstat(fsal_error, retval);	
}

static fsal_status_t zfs_linkfile(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	struct zfs_fsal_obj_handle *myself, *destdir;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        creden_t cred;

	if( !obj_hdl->export->ops->fs_supports(obj_hdl->export,
					       fso_link_support)) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);

	destdir = container_of(destdir_hdl, struct zfs_fsal_obj_handle, obj_handle);

        cred.uid = opctx->creds->caller_uid;
	cred.gid = opctx->creds->caller_gid;

        retval = libzfswrap_link( zfs_get_root_pvfs( obj_hdl->export ),
                                  &cred,
                                  destdir->handle->zfs_handle, 
                                  myself->handle->zfs_handle, 
                                  name );


	if(retval) 
        {
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
 * @param eof [OUT] eof marker true == end of dir
 */
#if plustard
static fsal_status_t lustre_read_dirents(struct fsal_obj_handle *dir_hdl,
				  const struct req_op_context *opctx,
				  uint32_t entry_cnt,
				  struct fsal_cookie *whence,
				  void *dir_state,
				  fsal_status_t (*cb)(
					  const struct req_op_context *opctx,
					  const char *name,
					  unsigned int dtype,
					  struct fsal_obj_handle *dir_hdl,
					  void *dir_state,
					  struct fsal_cookie *cookie),
                                  bool *eof)
{
	struct zfs_fsal_obj_handle *myself;
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
	myself = container_of(dir_hdl, struct zfs_fsal_obj_handle, obj_handle);
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
			status = cb(opctx,
				    dentry->d_name,
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

        *eof = (nread == 0);
done:
	close(dirfd);
	
out:
	return fsalstat(fsal_error, retval);
}
#endif

static fsal_status_t zfs_renamefile( struct fsal_obj_handle *olddir_hdl,
                                     const struct req_op_context *opctx,
			             const char *old_name,
				     struct fsal_obj_handle *newdir_hdl,
				     const char *new_name)
{
	struct zfs_fsal_obj_handle *olddir, *newdir;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        creden_t cred;

	olddir = container_of(olddir_hdl, struct zfs_fsal_obj_handle, obj_handle);
	newdir = container_of(newdir_hdl, struct zfs_fsal_obj_handle, obj_handle);

        cred.uid = opctx->creds->caller_uid;
	cred.gid = opctx->creds->caller_gid;

        retval = libzfswrap_rename( zfs_get_root_pvfs( olddir_hdl->export ),
	        		    &cred,
                                    olddir->handle->zfs_handle,
			            old_name,
                                    newdir->handle->zfs_handle,
			            new_name);


	if(retval ) {
		fsal_error = posix2fsal_error(retval);
	}
	return fsalstat(fsal_error, retval);	
}

/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

static fsal_status_t zfs_getattrs(struct fsal_obj_handle *obj_hdl,
				  const struct req_op_context *opctx)
{
	struct zfs_fsal_obj_handle *myself;
	struct stat stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	fsal_status_t st;
	int retval = 0;
        int type = 0 ;
        creden_t cred;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);

        cred.uid = opctx->creds->caller_uid;
	cred.gid = opctx->creds->caller_gid;

         if(  myself->handle->zfs_handle.inode == ZFS_SNAP_DIR_INODE &&
              myself->handle->zfs_handle.generation == 0)
          {
            memset(&stat, 0, sizeof(stat));
            stat.st_mode = S_IFDIR | 0755;
            stat.st_ino = ZFS_SNAP_DIR_INODE;
            stat.st_nlink = 2;
            stat.st_ctime = time(NULL);
            stat.st_atime = stat.st_ctime;
            stat.st_mtime = stat.st_ctime;
            retval = 0;
          }
         else
          {
            retval = libzfswrap_getattr( zfs_get_root_pvfs( obj_hdl->export ),
                                         &cred,
                                         myself->handle->zfs_handle, 
                                         &stat, 
                                         &type);

	    if(retval )
		goto errout;
          }

	/* convert attributes */
	st = posix2fsal_attributes(&stat, &obj_hdl->attributes);
	if(FSAL_IS_ERROR(st)) {
		FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
		FSAL_SET_MASK(obj_hdl->attributes.mask,
			      ATTR_RDATTR_ERR);
		fsal_error = st.major;  retval = st.minor;
		goto out;
	}
	goto out;

errout:
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

static fsal_status_t zfs_setattrs( struct fsal_obj_handle *obj_hdl,
				   const struct req_op_context *opctx,
			           struct attrlist *attrs)
{
  struct zfs_fsal_obj_handle *myself;
  struct stat stats = { 0 } ;
  fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
  int retval = 0;
  int flags = 0;
  creden_t cred;
  struct stat new_stat = { 0 };

  /* apply umask, if mode attribute is to be changed */
  if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
     {
		attrs->mode
			&= ~obj_hdl->export->ops->fs_umask(obj_hdl->export);
     }
  myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);

  if(myself->handle->i_snap != 0)
  {
    LogDebug(COMPONENT_FSAL, "Trying to change the attributes of an object inside a snapshot");
    fsalstat(ERR_FSAL_ROFS, 0);
  }

  /* First, check that FSAL attributes */ 
  if(FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
  {
    flags |= LZFSW_ATTR_MODE;
    stats.st_mode = fsal2unix_mode(attrs->mode);
  }
  if(FSAL_TEST_MASK(attrs->mask, ATTR_OWNER))
  {
    flags |= LZFSW_ATTR_UID;
    stats.st_uid = attrs->owner;
  }
  if(FSAL_TEST_MASK(attrs->mask, ATTR_GROUP))
  {
    flags |= LZFSW_ATTR_GID;
    stats.st_gid = attrs->group;
  }
  if(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME))
  {
    flags |= LZFSW_ATTR_ATIME;
    stats.st_atime = attrs->atime.seconds;
  }
  if(FSAL_TEST_MASK(attrs->mask, ATTR_MTIME))
  {
    flags |= LZFSW_ATTR_MTIME;
    stats.st_mtime = attrs->mtime.seconds;
  }

  cred.uid = opctx->creds->caller_uid;
  cred.gid = opctx->creds->caller_gid;

  retval = libzfswrap_setattr( zfs_get_root_pvfs( obj_hdl->export ),
                               &cred,
                               myself->handle->zfs_handle, 
                               &stats, 
                               flags, 
                               &new_stat);

  fsal_error = posix2fsal_error(retval);
  return fsalstat(fsal_error, retval);
}

/* compare
 * compare two handles.
 * return true for equal, false for anything else
 */
static bool compare(struct fsal_obj_handle *obj_hdl,
                    struct fsal_obj_handle *other_hdl)
{
	struct zfs_fsal_obj_handle *myself, *other;

	if( !other_hdl)
		return false;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
	other = container_of(other_hdl, struct zfs_fsal_obj_handle, obj_handle);
	if( (obj_hdl->type                         != other_hdl->type)          ||
	    (myself->handle->i_snap                != other->handle->i_snap) ||
	    (myself->handle->type                  != other->handle->type) ||
	    (myself->handle->zfs_handle.inode      != other->handle->zfs_handle.inode) ||
	    (myself->handle->zfs_handle.generation != other->handle->zfs_handle.generation) )
		return false;

        return true;
}

/* file_truncate
 * truncate a file to the size specified.
 * size should really be off_t...
 */

static fsal_status_t zfs_file_truncate( struct fsal_obj_handle *obj_hdl,
					const struct req_op_context *opctx,
					uint64_t length)
{
  struct zfs_fsal_obj_handle *myself;
  fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
  int retval = 0;
  creden_t cred;

  cred.uid = opctx->creds->caller_uid;
  cred.gid = opctx->creds->caller_gid;

  if(obj_hdl->type != REGULAR_FILE) {
	fsal_error = ERR_FSAL_INVAL;
        return fsalstat(fsal_error, retval);	
  }
  myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
 
  retval = libzfswrap_truncate( zfs_get_root_pvfs( obj_hdl->export ),
 		                &cred,
                                myself->handle->zfs_handle, 
                                length);

  if(retval ) 
     fsal_error = posix2fsal_error(retval);
	
  return fsalstat(fsal_error, retval);	
}

/* file_unlink
 * unlink the named file in the directory
 */

static fsal_status_t zfs_file_unlink( struct fsal_obj_handle *dir_hdl,
				      const struct req_op_context *opctx,
				      const char *name)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        creden_t cred;

        cred.uid = opctx->creds->caller_uid;
        cred.gid = opctx->creds->caller_gid;

	myself = container_of(dir_hdl, struct zfs_fsal_obj_handle, obj_handle);

        if( myself->handle->type == DIRECTORY )
          retval = libzfswrap_rmdir( zfs_get_root_pvfs( dir_hdl->export ),
                                     &cred,
                                     myself->handle->zfs_handle,
                                     name);

        else
          retval = libzfswrap_unlink( zfs_get_root_pvfs( dir_hdl->export ),
                                      &cred,
                                      myself->handle->zfs_handle,
                                      name);
  
       if(retval ) 
          fsal_error = posix2fsal_error(retval);
	
  return fsalstat(fsal_error, retval);	
}


/* handle_digest
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 */

static fsal_status_t zfs_handle_digest( struct fsal_obj_handle *obj_hdl,
                                        fsal_digesttype_t output_type,
                                        struct gsh_buffdesc *fh_desc)
{
	uint32_t ino32;
	uint64_t ino64;
	struct zfs_fsal_obj_handle *myself;
	struct zfs_file_handle *fh;
	size_t fh_size;

	/* sanity checks */
        if( !fh_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);
	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
	fh = myself->handle;

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		fh_size = zfs_sizeof_handle(fh);
                if(fh_desc->len < fh_size)
                        goto errout;
                memcpy(fh_desc->addr, fh, fh_size);
		break;
	case FSAL_DIGEST_FILEID2:
		fh_size = FSAL_DIGEST_SIZE_FILEID2;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, &fh->zfs_handle.inode, fh_size);
		break;
	case FSAL_DIGEST_FILEID3:
		fh_size = FSAL_DIGEST_SIZE_FILEID3;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(&ino32, &fh->zfs_handle.inode, sizeof(ino32));
		ino64 = ino32;
		memcpy(fh_desc->addr, &ino64, fh_size);
		break;
	case FSAL_DIGEST_FILEID4:
		fh_size = FSAL_DIGEST_SIZE_FILEID4;
		if(fh_desc->len < fh_size)
			goto errout;
		memcpy(fh_desc->addr, &fh->zfs_handle.inode, fh_size);
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

static void zfs_handle_to_key( struct fsal_obj_handle *obj_hdl,
                               struct gsh_buffdesc *fh_desc )
{
	struct zfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
	fh_desc->addr = myself->handle;
	fh_desc->len = zfs_sizeof_handle(myself->handle);
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct fsal_export *exp = obj_hdl->export;
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
	pthread_mutex_lock(&obj_hdl->lock);
	obj_hdl->refs--;  /* subtract the reference when we were created */
	if(obj_hdl->refs != 0 || (obj_hdl->type == REGULAR_FILE
				  && myself->u.file.openflags != FSAL_O_CLOSED)) {
		pthread_mutex_unlock(&obj_hdl->lock);
		retval = obj_hdl->refs > 0 ? EBUSY : EINVAL;
		LogCrit(COMPONENT_FSAL,
			"Tried to release busy handle, "
			"hdl = 0x%p->refs = %d, openflags = 0x%x",
			obj_hdl, obj_hdl->refs,
			myself->u.file.openflags);
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
	} 
	free(myself);
	return fsalstat(fsal_error, 0);
}

void zfs_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = zfs_lookup;
	//////////////////////////////////////ops->readdir = zfs_read_dirents;
	ops->create = zfs_create;
	ops->mkdir = zfs_mkdir;
	ops->mknode = zfs_makenode;
	ops->symlink = zfs_makesymlink;
	ops->readlink = zfs_readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = zfs_getattrs;
	ops->setattrs = zfs_setattrs;
	ops->link = zfs_linkfile;
	ops->rename = zfs_renamefile;
	ops->unlink = zfs_file_unlink;
	ops->truncate = zfs_file_truncate;
	ops->open = zfs_open;
	ops->status = zfs_status;
	ops->read = zfs_read;
	ops->write = zfs_write;
	///////////////////////ops->commit = zfs_commit;
	//////////////////////ops->lock_op = zfs_lock_op;
	ops->close = zfs_close;
	ops->lru_cleanup = zfs_lru_cleanup;
	ops->compare = compare;
	ops->handle_digest = zfs_handle_digest;
	ops->handle_to_key = zfs_handle_to_key;
}

/* export methods that create object handles
 */

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

fsal_status_t zfs_create_handle( struct fsal_export *exp_hdl,
				 const struct req_op_context *opctx,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle)
{
	struct zfs_fsal_obj_handle *hdl;
        int type = 0 ;
	struct zfs_file_handle  *fh;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	*handle = NULL; /* poison it first */
	if( hdl_desc->len > sizeof(struct zfs_file_handle) ) 
		return fsalstat(ERR_FSAL_FAULT, 0);

        type = ((struct zfs_file_handle *)(hdl_desc->addr))->type ;

	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len);  /* struct aligned copy */

	hdl = alloc_handle(fh, NULL, type, exp_hdl);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
	        return fsalstat(fsal_error, 0 );	
	}
	*handle = &hdl->obj_handle;
	
	return fsalstat(fsal_error, 0 );	
}

