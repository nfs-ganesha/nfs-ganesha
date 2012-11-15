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

/* file.c
 * File I/O methods for VFS module
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "zfs_methods.h"
#include "FSAL/FSAL_ZFS/fsal_types.h"
#include <stdbool.h>

libzfswrap_vfs_t *ZFSFSAL_GetVFS(zfs_file_handle_t *handle) ;

/** lustre_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t tank_open( struct fsal_obj_handle *obj_hdl,
		        fsal_openflags_t openflags)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
        libzfswrap_vnode_t *p_vnode;
        creden_t cred;
        int posix_flags;
 
        /* At this point, test_access has been run and access is checked
         * as a workaround let's use root credentials */
        cred.uid = 0 ;
        cred.gid = 0 ;
 
        rc = fsal2posix_openflags(openflags, &posix_flags);
        if( rc ) 
          return fsalstat( ERR_FSAL_INVAL, rc ) ;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
        
        rc = libzfswrap_open( ZFSFSAL_GetVFS( myself->handle ), 
                             &cred,
                             myself->handle->zfs_handle,  
                             posix_flags, 
                             &p_vnode);

        if( rc )
         { 
           fsal_error = posix2fsal_error( rc );
	   return fsalstat( fsal_error, rc );	
         }
 
        /* >> fill output struct << */
        myself->u.file.openflags = posix_flags;
        myself->u.file.current_offset = 0;
        myself->u.file.p_vnode = p_vnode;
        myself->u.file.cred = cred;
        myself->u.file.is_closed = 0;

	return fsalstat(fsal_error, rc );	
}

/* lustre_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t tank_status(struct fsal_obj_handle *obj_hdl)
{
	struct zfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
	return myself->u.file.openflags;
}

/* lustre_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t tank_read(struct fsal_obj_handle *obj_hdl,
                       const struct req_op_context *opctx,
		       uint64_t offset,
                       size_t buffer_size,
                       void *buffer,
		       size_t *read_amount,
		       bool *end_of_file)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int rc = 0;
        creden_t cred;
        int behind = 0;

        cred.uid = opctx->creds->caller_uid;
        cred.gid = opctx->creds->caller_gid;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.openflags != FSAL_O_CLOSED);

        rc = libzfswrap_read( ZFSFSAL_GetVFS( myself->handle ),  
                              &cred, 
                              myself->u.file.p_vnode,
                              buffer, 
                              buffer_size, 
                              behind, 
                              offset );

        if(!rc) 
           * end_of_file = true ;
        

        fsal_error = posix2fsal_error( rc );

        *read_amount = buffer_size;

	return fsalstat(fsal_error, rc);	
}

/* lustre_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t tank_write(struct fsal_obj_handle *obj_hdl,
                        const struct req_op_context *opctx,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *write_amount)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        creden_t cred;
	int retval = 0;
        int behind = 0 ;

        cred.uid = opctx->creds->caller_uid;
        cred.gid = opctx->creds->caller_gid;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.openflags != FSAL_O_CLOSED);

        retval = libzfswrap_write( ZFSFSAL_GetVFS( myself->handle ),
                                   &cred, 
                                   myself->u.file.p_vnode,
                                   buffer, 
                                   buffer_size, 
                                   behind, 
                                   offset);

	if(offset == -1 ) {
		fsal_error = posix2fsal_error(retval);
		goto out;
	}
	*write_amount = buffer_size;
out:
	return fsalstat(fsal_error, retval);	
}

/* lustre_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t tank_commit( struct fsal_obj_handle *obj_hdl, /* sync */
			  off_t offset,
			  size_t len)
{
	return fsalstat( ERR_FSAL_NO_ERROR, 0);	
}


/* lustre_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t tank_close(struct fsal_obj_handle *obj_hdl)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);

	myself->u.file.openflags = FSAL_O_CLOSED;
	myself->u.file.is_closed = true ;
	return fsalstat(fsal_error, retval);	
}

/* lustre_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t tank_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			      lru_actions_t requests)
{
	struct zfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct zfs_fsal_obj_handle, obj_handle);
	myself->u.file.is_closed = true ;
        myself->u.file.openflags = FSAL_O_CLOSED;
	
	return fsalstat(fsal_error, retval);	
}
