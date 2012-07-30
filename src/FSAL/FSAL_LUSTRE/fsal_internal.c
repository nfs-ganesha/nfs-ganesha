/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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

/**
 *
 * \file    fsal_internal.c
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.24 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include  "fsal.h"
#include "fsal_internal.h"
#include "SemN.h"
#include "fsal_convert.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include "abstract_mem.h"


/* Add missing prototype in vfs.h */
int fd_to_handle(int fd, void **hanp, size_t * hlen);

/* credential lifetime (1h) */
uint32_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
struct fsal_staticfsinfo_t global_fs_info;

/* filesystem info for VFS */
/* static fsal_staticfsinfo_t default_posix_info = { */
/*   0xFFFFFFFFFFFFFFFFLL,         /\* max file size (64bits) *\/ */
/*   _POSIX_LINK_MAX,              /\* max links *\/ */
/*   FSAL_MAX_NAME_LEN,            /\* max filename *\/ */
/*   FSAL_MAX_PATH_LEN,            /\* max pathlen *\/ */
/*   TRUE,                         /\* no_trunc *\/ */
/*   TRUE,                         /\* chown restricted *\/ */
/*   FALSE,                        /\* case insensitivity *\/ */
/*   TRUE,                         /\* case preserving *\/ */
/*   FSAL_EXPTYPE_PERSISTENT,      /\* FH expire type *\/ */
/*   TRUE,                         /\* hard link support *\/ */
/*   TRUE,                         /\* symlink support *\/ */
/*   TRUE,                         /\* lock management *\/ */
/*   FALSE,                        /\* lock owners *\/ */
/*   FALSE,                        /\* async blocking locks *\/ */
/*   TRUE,                         /\* named attributes *\/ */
/*   TRUE,                         /\* handles are unique and persistent *\/ */
/*   {10, 0},                      /\* Duration of lease at FS in seconds *\/ */
/*   FSAL_ACLSUPPORT_ALLOW,        /\* ACL support *\/ */
/*   TRUE,                         /\* can change times *\/ */
/*   TRUE,                         /\* homogenous *\/ */
/*   VFS_SUPPORTED_ATTRIBUTES,     /\* supported attributes *\/ */
/*   0,                            /\* maxread size *\/ */
/*   0,                            /\* maxwrite size *\/ */
/*   0,                            /\* default umask *\/ */
/*   0,                            /\* cross junctions *\/ */
/*   0400,                         /\* default access rights for xattrs: root=RW, owner=R *\/ */
/*   0                             /\* default access check support in FSAL *\/ */
/** @TODO new params for share op to be added to new api
 */
/*   0,                            /\* default share reservation support in FSAL *\/ */
/*   0                             /\* default share reservation support with open owners in F *//* }; */


#if 0
fsal_status_t fsal_internal_handle2fd(fsal_op_context_t * p_context,
                                      fsal_handle_t * p_handle, int *pfd, int oflags)
{
  int rc = 0;
  int errsv;


  if(!p_handle || !pfd || !p_context)
    ReturnCode(ERR_FSAL_FAULT, 0);

#if 0
  {
  char str[1024] ;
  sprint_mem( str, phandle->data.vfs_handle.handle, phandle->data.vfs_handle.handle_bytes ) ;
  printf( "=====> fsal_internal_handle2fd: type=%u bytes=%u|%s\n",
          phandle->data.vfs_handle.handle_type, phandle->data.vfs_handle.handle_bytes, str ) ;
  }
#endif


  rc =  vfs_open_by_handle( ((vfsfsal_op_context_t *)p_context)->export_context->mount_root_fd,
			    &((vfsfsal_handle_t *)p_handle)->data.vfs_handle,
                            oflags ) ;
  if(rc == -1)
    {
      errsv = errno;

      ReturnCode(posix2fsal_error(errsv), errsv);
    }

  *pfd = rc;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* fsal_internal_handle2fd */

fsal_status_t fsal_internal_fd2handle( fsal_op_context_t *p_context,
                                       int fd, 
				       fsal_handle_t *p_handle)
{
  int rc = 0;
  int errsv; 
  int mnt_id = 0;

  memset(p_handle, 0, sizeof(vfsfsal_handle_t));

  ((vfsfsal_handle_t *)p_handle)->data.vfs_handle.handle_bytes = VFS_HANDLE_LEN ;
  if( ( rc = vfs_fd_to_handle( fd,
			       &((vfsfsal_handle_t *)p_handle)->data.vfs_handle,
			       &mnt_id ) ) )
    {
      errsv = errno;
      ReturnCode(posix2fsal_error(errsv), errsv);
    }

#if 0
  {
    char str[1024] ;
    sprint_mem( str, p_handle->data.vfs_handle.handle, p_handle->data.vfs_handle.handle_bytes ) ;
    printf( "=====> fsal_internal_fd2handle: type=%u bytes=%u|%s\n",  
            p_handle->data.vfs_handle.handle_type, p_handle->data.vfs_handle.handle_bytes, str ) ;
  }
#endif

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* fsal_internal_fd2handle */

fsal_status_t fsal_internal_Path2Handle(fsal_op_context_t * p_context,       /* IN */
                                        fsal_path_t * p_fsalpath,       /* IN */
                                        fsal_handle_t * p_handle /* OUT */ )
{
  int objectfd;
  fsal_status_t st;

  if(!p_context || !p_handle || !p_fsalpath)
    ReturnCode(ERR_FSAL_FAULT, 0);

  LogFullDebug(COMPONENT_FSAL, "Lookup handle for %s", p_fsalpath->path);

  if((objectfd = open(p_fsalpath->path, O_RDONLY, 0600)) < 0)
    ReturnCode(posix2fsal_error(errno), errno);

  st = fsal_internal_fd2handle(p_context, objectfd, p_handle);
  close(objectfd);
  return st;
}                               /* fsal_internal_Path2Handle */


/**
 * fsal_internal_get_handle_at:
 * Create a handle from a directory pointer and filename
 *
 * \param dfd (input):
 *        Open directory handle
 * \param p_fsalname (input):
 *        Name of the file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_get_handle_at(int dfd,      /* IN */
                                          const char *name,     /* IN */
                                          fsal_handle_t *p_handle      /* OUT
                                                                         */ )
{
  int errsrv = 0 ;

  if( !name || !p_handle )
    ReturnCode(ERR_FSAL_FAULT, 0);

  memset(p_handle, 0, sizeof(vfsfsal_handle_t));

  LogFullDebug(COMPONENT_FSAL, "get handle at for %s", name);

  ((vfsfsal_handle_t *)p_handle)->data.vfs_handle.handle_bytes = VFS_HANDLE_LEN ;
  if( vfs_name_by_handle_at( dfd, name, &((vfsfsal_handle_t *)p_handle)->data.vfs_handle ) != 0 )
   {
      errsrv = errno;
      ReturnCode(posix2fsal_error(errsrv), errsrv);
   }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
} /* fsal_internal_get_handle_at */

fsal_status_t fsal_internal_setattrs_symlink(fsal_handle_t * p_filehandle,   /* IN */
                                             fsal_op_context_t * p_context,  /* IN */
                                             fsal_attrib_list_t * p_attrib_set, /* IN */
                                             fsal_attrib_list_t * p_object_attributes)
{
  if(!p_filehandle || !p_context || !p_attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  *p_object_attributes = *p_attrib_set;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* fsal_internal_setattrs_symlink */

#endif
