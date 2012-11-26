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
                                                struct stat *stat,
                                                const char *link_content,
                                                struct fsal_export *exp_hdl)
{
	struct zfs_fsal_obj_handle *hdl;
        fsal_status_t st;

	hdl = malloc(sizeof(struct zfs_fsal_obj_handle) +
		     sizeof(struct zfs_file_handle) ) ;
	if(hdl == NULL)
		return NULL;
	memset(hdl, 0, (sizeof(struct zfs_fsal_obj_handle) +
			sizeof(struct zfs_file_handle) ) ) ;
	hdl->handle = (struct zfs_file_handle *)&hdl[1];
	memcpy(hdl->handle, fh,
	       sizeof(struct zfs_file_handle) );

        hdl->obj_handle.type = posix2fsal_type(stat->st_mode);

	if( (hdl->obj_handle.type == SYMBOLIC_LINK) &&
            (link_content != NULL)  )
        {
		size_t len = strlen(link_content) + 1;

		hdl->u.symlink.link_content = gsh_malloc(len);
		if(hdl->u.symlink.link_content == NULL) {
			goto spcerr;
	        }

                memcpy(hdl->u.symlink.link_content, link_content, len);
		hdl->u.symlink.link_size = len;
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
				 exp_hdl,
	                         posix2fsal_type(stat->st_mode))) ;
                return hdl;

spcerr:
	hdl->obj_handle.ops = NULL;
	pthread_mutex_unlock(&hdl->obj_handle.lock);
	pthread_mutex_destroy(&hdl->obj_handle.lock);
	if(hdl->obj_handle.type == SYMBOLIC_LINK) {
		if(hdl->u.symlink.link_content != NULL)
			gsh_free(hdl->u.symlink.link_content);
        } 
	gsh_free(hdl);  /* elvis has left the building */
	return NULL;

}

/* handle methods
 */

/* lookup
 * deprecated NULL parent && NULL path implies root handle
 */

static fsal_status_t tank_lookup(struct fsal_obj_handle *parent,
		                const struct req_op_context *opctx,
				const char *path,
				struct fsal_obj_handle **handle)
{
	struct zfs_fsal_obj_handle *parent_hdl, *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct stat stat;
	struct zfs_file_handle *fh
		= alloca(sizeof(struct zfs_file_handle));
        creden_t cred;
        libzfswrap_vfs_t *p_vfs = NULL ;

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

      ZFSFSAL_VFS_RDLock();
      p_vfs = ZFSFSAL_GetVFS( parent_hdl->handle );
      ZFSFSAL_VFS_Unlock();

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

        ZFSFSAL_VFS_RDLock();
        libzfswrap_getroot(p_snapshots[i].p_vfs, &object);
        p_vfs = p_snapshots[i].p_vfs ;
        ZFSFSAL_VFS_Unlock();

        type = S_IFDIR;
        i_snap = i + 1;
        retval = 0;
      }
      else
      {
        /* Get the right VFS */
        ZFSFSAL_VFS_RDLock();
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
       	if(retval ) {
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

	}
              
        ZFSFSAL_VFS_Unlock();

        //FIXME!!! Hook to remove the i_snap bit when going up from the .zfs directory
        if(object.inode == 3)
          i_snap = 0;
      }
      cred.uid = opctx->creds->caller_uid;
      cred.gid = opctx->creds->caller_gid;

      ZFSFSAL_VFS_RDLock();
      retval = libzfswrap_getattr( p_vfs,
                                   &cred,
                                   object,
                                   &stat, 
                                   &type );

        ZFSFSAL_VFS_Unlock();
	if(retval ) {
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}

        /* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh,
                           &stat,
                           NULL,
			   parent->export);
	if(hdl != NULL) {
		*handle = &hdl->obj_handle ;

                hdl->handle->zfs_handle = object;
                hdl->handle->i_snap = 0;
	} else {
		fsal_error = ERR_FSAL_NOMEM;
		*handle = NULL; /* poison it */
		goto errout;
	}
      	return fsalstat(ERR_FSAL_NO_ERROR, 0);
	
errout:
        ZFSFSAL_VFS_Unlock();
	return fsalstat(fsal_error, retval);	
}

/* lookup_path
 * should not be used for "/" only is exported */

fsal_status_t tank_lookup_path(struct fsal_export *exp_hdl,
		     	      const struct req_op_context *opctx,
                              const char *path,
			      struct fsal_obj_handle **handle)
{
  
   if( strcmp( path, "/" ) )
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
   else
    {
        inogen_t object;
        int rc = 0 ;
	struct zfs_fsal_obj_handle *hdl;
	struct zfs_file_handle *fh = NULL ;
        struct stat stat ;		
        int type ;
        creden_t cred ;

        if( (rc = libzfswrap_getroot( tank_get_root_pvfs( exp_hdl) , &object ) ) )
          return fsalstat( posix2fsal_error(rc ) , rc ) ;

        if( opctx )
         {
            cred.uid = opctx->creds->caller_uid;
            cred.gid = opctx->creds->caller_gid;
         }
        else
         {
            cred.uid = 0 ;
            cred.gid = 0 ;
         }

        if( (rc = libzfswrap_getattr( tank_get_root_pvfs( exp_hdl), 
                                      &cred,
                                      object,
                                      &stat, 
                                      &type ) ) )
          return fsalstat( posix2fsal_error(rc ) , rc ) ;


        fh =  alloca(sizeof(struct zfs_file_handle));
        if( fh == NULL )
         {
		*handle = NULL; /* poison it */
                return fsalstat( ERR_FSAL_NOMEM , 0 ) ;
         } 
        else
         {
            fh->zfs_handle = object;
            fh->i_snap = 0;
         }

	hdl = alloc_handle( fh,
                            &stat,
                            NULL,
			    exp_hdl);
	if(hdl != NULL) {
		*handle = &hdl->obj_handle ;
	} else {
		*handle = NULL; /* poison it */
                return fsalstat( ERR_FSAL_NOMEM , 0 ) ;
	}

    }
        
    return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* create
 * create a regular file and set its attributes
 */

static fsal_status_t tank_create( struct fsal_obj_handle *dir_hdl,
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
        struct stat stat ;
        int type ;

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

        retval = libzfswrap_create( tank_get_root_pvfs( dir_hdl->export ),
                                    &cred,
                                    myself->handle->zfs_handle,
                                    name,
                                    fsal2unix_mode(attrib->mode),
                                    &object);

	if(retval ) {
		goto fileerr;
	}

       retval = libzfswrap_getattr(  tank_get_root_pvfs( dir_hdl->export ),
                                     &cred,
                                     object,
                                     &stat, 
                                     &type );

	if(retval ) {
		goto fileerr;
	}


	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, dir_hdl->export);
	if(hdl != NULL)
        {
           /* >> set output handle << */
           hdl->handle->zfs_handle = object ;
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

static fsal_status_t tank_mkdir( struct fsal_obj_handle *dir_hdl,
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
        struct stat stat ;
        int type ;

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

        retval = libzfswrap_mkdir( tank_get_root_pvfs( dir_hdl->export ),
                                   &cred,
                                   myself->handle->zfs_handle,
                                   name,
                                   fsal2unix_mode(attrib->mode),
                                   &object);

	if(retval ) {
		goto fileerr;
	}

        retval = libzfswrap_getattr( tank_get_root_pvfs( dir_hdl->export ),
                                     &cred,
                                     object,
                                     &stat, 
                                     &type );

	if(retval ) {
		goto fileerr;
	}


	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, NULL, dir_hdl->export);
	if(hdl != NULL)
        {
           /* >> set output handle << */
           hdl->handle->zfs_handle = object ;
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


static fsal_status_t tank_makenode( struct fsal_obj_handle *dir_hdl,
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

static fsal_status_t tank_makesymlink(struct fsal_obj_handle *dir_hdl,
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
        struct stat stat ;
        int type ;

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

        retval = libzfswrap_symlink( tank_get_root_pvfs( dir_hdl->export ),
                                     &cred,
                                     myself->handle->zfs_handle,
                                     name,
                                     link_path,
                                     &object);
	if(retval ) 
		goto err;

        if( (retval = libzfswrap_getattr( tank_get_root_pvfs( dir_hdl->export), 
                                          &cred,
                                          object,
                                          &stat, 
                                          &type ) ) )
                goto err;

	/* allocate an obj_handle and fill it up */
	hdl = alloc_handle(fh, &stat, link_path, dir_hdl->export);
	if(hdl == NULL) {
		retval = ENOMEM;
		goto err;
	}
	*handle = &hdl->obj_handle;
        hdl->handle->zfs_handle = object ;
        hdl->handle->i_snap= 0  ;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

err:
	if(retval == ENOENT)
		fsal_error = ERR_FSAL_STALE;
	else
		fsal_error = posix2fsal_error(retval);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t tank_readsymlink(struct fsal_obj_handle *obj_hdl,
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

        retlink = libzfswrap_readlink( tank_get_root_pvfs( obj_hdl->export ),
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

static fsal_status_t tank_linkfile(struct fsal_obj_handle *obj_hdl,
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

        retval = libzfswrap_link( tank_get_root_pvfs( obj_hdl->export ),
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

#define MAX_ENTRIES 256
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
static fsal_status_t tank_readdir(struct fsal_obj_handle *dir_hdl,
			      	  const struct req_op_context *opctx,
				  struct fsal_cookie *whence,
				  void *dir_state,
                                  fsal_readdir_cb cb,
                                  bool *eof)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
	off_t seekloc = 0;
        creden_t cred ;
	struct fsal_cookie *entry_cookie;
        libzfswrap_vfs_t   * p_vfs   = NULL ;
        libzfswrap_vnode_t * pvnode  = NULL ;
        libzfswrap_entry_t * dirents = NULL ;
        unsigned int index  = 0 ;

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

        cred.uid = opctx->creds->caller_uid;
        cred.gid = opctx->creds->caller_gid;

        ZFSFSAL_VFS_RDLock();

        p_vfs = ZFSFSAL_GetVFS( myself->handle );
        if(!p_vfs)
         {
             fsal_error = ERR_FSAL_NOENT ;
             retval = 0 ;
             goto out;
         }


        /* Open the directory */
        if( ( retval = libzfswrap_opendir( p_vfs,  
                                           &cred,
                                           myself->handle->zfs_handle,
                                           &pvnode ) ) )

             goto out;

        ZFSFSAL_VFS_Unlock() ; /* Release the lock for interlacing the request */

      
        if( ( dirents = gsh_malloc( MAX_ENTRIES * sizeof(libzfswrap_entry_t) ) ) == NULL )
          return fsalstat( ERR_FSAL_NOMEM, 0 ) ;


        if( ( retval = libzfswrap_readdir( p_vfs,
                                           &cred,
                                           pvnode,
                                           dirents,  
                                           MAX_ENTRIES,
                                           &seekloc ) ) )
             goto out;

        ZFSFSAL_VFS_Unlock() ;

        *eof = FALSE ;
        for( index = 0 ; index < MAX_ENTRIES ; index ++ )
        {
             /* If psz_filename is NULL, that's the end of the list */
             if(dirents[index].psz_filename[0] == '\0')
              {
                *eof = TRUE ;
                break;
              }

             /* Skip '.' and '..' */
             if(!strcmp(dirents[index].psz_filename, ".") || !strcmp(dirents[index].psz_filename, ".."))
                continue;
   
             entry_cookie->size = sizeof(off_t);
	     memcpy(&entry_cookie->cookie, &index, sizeof(off_t));

             /* callback to cache inode */
             if(!cb( opctx,
                     dirents[index].psz_filename,
                     dir_state,
                     entry_cookie ) ) 
                break ;
        }

        /* Close the directory */
        ZFSFSAL_VFS_RDLock();
        if( ( retval = libzfswrap_closedir( p_vfs,
                                            &cred,
                                            pvnode ) ) )
             goto out;


        /* read the directory */
        ZFSFSAL_VFS_RDLock();

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
out:
        ZFSFSAL_VFS_Unlock();
        return fsalstat(posix2fsal_error( retval ), retval);
}

static fsal_status_t tank_renamefile( struct fsal_obj_handle *olddir_hdl,
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

        retval = libzfswrap_rename( tank_get_root_pvfs( olddir_hdl->export ),
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

static fsal_status_t tank_getattrs(struct fsal_obj_handle *obj_hdl,
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
            retval = libzfswrap_getattr( tank_get_root_pvfs( obj_hdl->export ),
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

static fsal_status_t tank_setattrs( struct fsal_obj_handle *obj_hdl,
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

  retval = libzfswrap_setattr( tank_get_root_pvfs( obj_hdl->export ),
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
	    (myself->handle->zfs_handle.inode      != other->handle->zfs_handle.inode) ||
	    (myself->handle->zfs_handle.generation != other->handle->zfs_handle.generation) )
		return false;

        return true;
}

/* file_truncate
 * truncate a file to the size specified.
 * size should really be off_t...
 */

static fsal_status_t tank_file_truncate( struct fsal_obj_handle *obj_hdl,
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
 
  retval = libzfswrap_truncate( tank_get_root_pvfs( obj_hdl->export ),
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

static fsal_status_t tank_file_unlink( struct fsal_obj_handle *dir_hdl,
				      const struct req_op_context *opctx,
				      const char *name)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        creden_t cred;
        inogen_t object ;
        int type = 0 ;

        cred.uid = opctx->creds->caller_uid;
        cred.gid = opctx->creds->caller_gid;

	myself = container_of(dir_hdl, struct zfs_fsal_obj_handle, obj_handle);

        /* check for presence of file and get its type */
        if( (retval = libzfswrap_lookup( tank_get_root_pvfs( dir_hdl->export ),
                                         &cred,
                                         myself->handle->zfs_handle,
                                         name,
                                         &object,
                                         &type ) ) == 0 )
         {
           if( type == S_IFDIR )
             retval = libzfswrap_rmdir( tank_get_root_pvfs( dir_hdl->export ),
                                        &cred,
                                        myself->handle->zfs_handle,
                                        name);

           else
             retval = libzfswrap_unlink( tank_get_root_pvfs( dir_hdl->export ),
                                         &cred,
                                         myself->handle->zfs_handle,
                                         name);
        }

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

static fsal_status_t tank_handle_digest( struct fsal_obj_handle *obj_hdl,
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

static void tank_handle_to_key( struct fsal_obj_handle *obj_hdl,
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
	ops->lookup = tank_lookup;
	ops->readdir = tank_readdir;
	ops->create = tank_create;
	ops->mkdir = tank_mkdir;
	ops->mknode = tank_makenode;
	ops->symlink = tank_makesymlink;
	ops->readlink = tank_readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = tank_getattrs;
	ops->setattrs = tank_setattrs;
	ops->link = tank_linkfile;
	ops->rename = tank_renamefile;
	ops->unlink = tank_file_unlink;
	ops->truncate = tank_file_truncate;
	//////ops->open = tank_open;
	ops->status = tank_status;
	ops->read = tank_read;
	ops->write = tank_write;
	///////////////////////ops->commit = tank_commit;
	//////////////////////ops->lock_op = tank_lock_op;
	ops->close = tank_close;
	ops->lru_cleanup = tank_lru_cleanup;
	ops->compare = compare;
	ops->handle_digest = tank_handle_digest;
	ops->handle_to_key = tank_handle_to_key;
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

fsal_status_t tank_create_handle( struct fsal_export *exp_hdl,
				 const struct req_op_context *opctx,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle)
{
	struct zfs_fsal_obj_handle *hdl;
	struct zfs_file_handle  *fh;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        char link_buff[PATH_MAX];
        char *link_content = NULL;
        struct stat stat ;
        creden_t cred ;
        int type ;
        int retval ;

	*handle = NULL; /* poison it first */
	if( hdl_desc->len > sizeof(struct zfs_file_handle) ) 
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh = alloca(hdl_desc->len);
	memcpy(fh, hdl_desc->addr, hdl_desc->len);  /* struct aligned copy */

        cred.uid = opctx->creds->caller_uid;
	cred.gid = opctx->creds->caller_gid;

         if( ( retval = libzfswrap_getattr( tank_get_root_pvfs( exp_hdl), 
                                            &cred,
                                            fh->zfs_handle, 
                                            &stat, 
                                            &type ) ) )
                return fsalstat( posix2fsal_error( retval ), retval ) ;

        link_content = NULL ;
        if(S_ISLNK(stat.st_mode))  
         {
            if( ( retval = libzfswrap_readlink( tank_get_root_pvfs( exp_hdl), 
                                                &cred,          
                                                fh->zfs_handle, 
                                                link_buff,
                                                PATH_MAX) ) )
                return fsalstat( posix2fsal_error( retval ), retval ) ;
            
             link_content = link_buff ;
	   
         }

        hdl = alloc_handle(fh, &stat, link_content, exp_hdl);
	if(hdl == NULL) {
		fsal_error = ERR_FSAL_NOMEM;
	        return fsalstat(fsal_error, 0 );	
	}
	*handle = &hdl->obj_handle;
	
	return fsalstat(fsal_error, 0 );	
}

