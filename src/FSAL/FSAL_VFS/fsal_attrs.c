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
 * ---------------------------------------
 */

/**
 *
 * \file    fsal_attrs.c
 * \brief   Attributes functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>

extern fsal_status_t posixstat64_2_fsal_attributes(struct stat64 *p_buffstat,
                                                   fsal_attrib_list_t * p_fsalattr_out);

/**
 * VFSFSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t VFSFSAL_getattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_object_attributes    /* IN/OUT */
    )
{
  fsal_status_t st;
  int rc = 0 ;
  int errsv;
  struct stat buffstat;

  /* sanity checks.
   * note : object_attributes is mandatory in VFSFSAL_getattrs.
   */
  if(!p_filehandle || !p_context || !p_object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  TakeTokenFSCall();
  rc = vfs_stat_by_handle( ((vfsfsal_op_context_t *)p_context)->export_context->mount_root_fd,
                           &((vfsfsal_handle_t *)p_filehandle)->data.vfs_handle,
                           &buffstat ) ;
  errsv = errno;
  ReleaseTokenFSCall();

  if( rc == -1 )
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_getattrs);

  /* convert attributes */
  st = posix2fsal_attributes(&buffstat, p_object_attributes);
  if(FSAL_IS_ERROR(st))
    {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
      ReturnStatus(st, INDEX_FSAL_getattrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);

}

/**
 * VFSFSAL_getattrs_descriptor:
 * Get attributes for the object specified by its descriptor or by it's filehandle.
 *
 * \param p_file_descriptor (input):
 *        The file descriptor of the object to get parameters.
 * \param p_filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t VFSFSAL_getattrs_descriptor(fsal_file_t * p_file_descriptor,     /* IN */
                                           fsal_handle_t * p_filehandle,        /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           fsal_attrib_list_t * p_object_attributes /* IN/OUT */
    )
{
  fsal_status_t st;
  struct stat64 buffstat;
  int rc, errsv;

  /* sanity checks.
   * note : object_attributes is mandatory in VFSFSAL_getattrs.
   */
  if(!p_file_descriptor || !p_filehandle || !p_context || !p_object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs_descriptor);

  TakeTokenFSCall();
  rc = fstat64(((vfsfsal_file_t *)p_file_descriptor)->fd, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc == -1)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_getattrs_descriptor);

  /* convert attributes */
  st = posixstat64_2_fsal_attributes(&buffstat, p_object_attributes);
  if(FSAL_IS_ERROR(st))
    {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
      ReturnStatus(st, INDEX_FSAL_getattrs_descriptor);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs_descriptor);

}

/**
 * VFSFSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t VFSFSAL_setattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_attrib_set,  /* IN */
                            fsal_attrib_list_t * p_object_attributes    /* [ IN/OUT ] */
    )
{
  vfsfsal_op_context_t * vfs_context = (vfsfsal_op_context_t *) p_context;
  int rc, errsv;
  unsigned int i;
  fsal_status_t status;
  fsal_attrib_list_t attrs;

  int fd;
  struct stat buffstat;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context || !p_attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* local copy of attributes */
  attrs = *p_attrib_set;

  /* It does not make sense to setattr on a symlink */
  /* if(p_filehandle->type == DT_LNK)
     return fsal_internal_setattrs_symlink(p_filehandle, p_context, p_attrib_set,
     p_object_attributes);
   */
  /* First, check that FSAL attributes changes are allowed. */

  /* Is it allowed to change times ? */

  if(!global_fs_info.cansettime)
    {

      if(attrs.asked_attributes
         & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))
        {
          /* handled as an unsettable attribute. */
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
        }
    }

  /* apply umask, if mode attribute is to be changed */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {
      attrs.mode &= (~global_fs_info.umask);
    }

  TakeTokenFSCall();
  status = fsal_internal_handle2fd(p_context, p_filehandle, &fd, O_RDONLY);
  ReleaseTokenFSCall();
  if(FSAL_IS_ERROR(status))
   {
     /* Symbolic link are handled here, they are to be opened as O_PATH */
     if( status.minor == ELOOP )
      {
       Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);
      }
       
     ReturnStatus( status, INDEX_FSAL_setattrs);
   }

  /* get current attributes */
  TakeTokenFSCall();
  rc = fstat(fd, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc != 0)
    {
      close(fd);

      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_setattrs);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_setattrs);
    }

  /***********
   *  CHMOD  *
   ***********/
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {

      /* The POSIX chmod call don't affect the symlink object, but
       * the entry it points to. So we must ignore it.
       */
      if(!S_ISLNK(buffstat.st_mode))
        {

          /* For modifying mode, user must be root or the owner */
          if((vfs_context->credential.user != 0)
             && (vfs_context->credential.user != buffstat.st_uid))
            {
              LogFullDebug(COMPONENT_FSAL,
                           "Permission denied for CHMOD opeartion: current owner=%d, credential=%d",
                           buffstat.st_uid, vfs_context->credential.user);
              close(fd);
              Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
            }

          TakeTokenFSCall();
          rc = fchmod(fd, fsal2unix_mode(attrs.mode));
          errsv = errno;
          ReleaseTokenFSCall();

          if(rc)
            {
              close(fd);
              Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_setattrs);
            }

        }

    }

  /***********
   *  CHOWN  *
   ***********/
  /* Only root can change uid and A normal user must be in the group he wants to set */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER))
    {

      /* For modifying owner, user must be root or current owner==wanted==client */
      if((vfs_context->credential.user != 0) &&
         ((vfs_context->credential.user != buffstat.st_uid) ||
          (vfs_context->credential.user != attrs.owner)))
        {
          LogFullDebug(COMPONENT_FSAL,
                       "Permission denied for CHOWN opeartion: current owner=%d, credential=%d, new owner=%d",
                       buffstat.st_uid, vfs_context->credential.user, attrs.owner);
          close(fd);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_GROUP))
    {

      /* For modifying group, user must be root or current owner */
      if((vfs_context->credential.user != 0)
         && (vfs_context->credential.user != buffstat.st_uid))
        {
          close(fd);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }

      int in_grp = 0;
      /* set in_grp */
      if(vfs_context->credential.group == attrs.group)
        in_grp = 1;
      else
        for(i = 0; i < vfs_context->credential.nbgroups; i++)
          {
            if((in_grp = (attrs.group == vfs_context->credential.alt_groups[i])))
              break;
          }

      /* it must also be in target group */
      if(vfs_context->credential.user != 0 && !in_grp)
        {
          LogFullDebug(COMPONENT_FSAL,
                       "Permission denied for CHOWN operation: current group=%d, credential=%d, new group=%d",
                       buffstat.st_gid, vfs_context->credential.group, attrs.group);
          close(fd);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      /*      LogFullDebug(COMPONENT_FSAL, "Performing chown(%s, %d,%d)",
                        fsalpath.path, FSAL_TEST_MASK(attrs.asked_attributes,
                                                      FSAL_ATTR_OWNER) ? (int)attrs.owner
                        : -1, FSAL_TEST_MASK(attrs.asked_attributes,
			FSAL_ATTR_GROUP) ? (int)attrs.group : -1);*/

      TakeTokenFSCall();
      rc = fchown(fd,
                  FSAL_TEST_MASK(attrs.asked_attributes,
                                 FSAL_ATTR_OWNER) ? (int)attrs.owner : -1,
                  FSAL_TEST_MASK(attrs.asked_attributes,
                                 FSAL_ATTR_GROUP) ? (int)attrs.group : -1);
      ReleaseTokenFSCall();
      if(rc)
        {
          close(fd);
          Return(posix2fsal_error(errno), errno, INDEX_FSAL_setattrs);
        }
    }

  /***********
   *  UTIME  *
   ***********/

  /* user must be the owner or have read access to modify 'atime' */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME)
     && (vfs_context->credential.user != 0)
     && (vfs_context->credential.user != buffstat.st_uid)
     && ((status = fsal_check_access(p_context, FSAL_R_OK, &buffstat, NULL)).major
         != ERR_FSAL_NO_ERROR))
    {
      close(fd);
      ReturnStatus(status, INDEX_FSAL_setattrs);
    }
  /* user must be the owner or have write access to modify 'mtime' */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME)
     && (vfs_context->credential.user != 0)
     && (vfs_context->credential.user != buffstat.st_uid)
     && ((status = fsal_check_access(p_context, FSAL_W_OK, &buffstat, NULL)).major
         != ERR_FSAL_NO_ERROR))
    {
      close(fd);
      ReturnStatus(status, INDEX_FSAL_setattrs);
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME | FSAL_ATTR_MTIME))
    {

      struct timeval timebuf[2];

      /* Atime */
      timebuf[0].tv_sec =
          (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME) ? (time_t) attrs.
           atime.seconds : buffstat.st_atime);
      timebuf[0].tv_usec = 0;

      /* Mtime */
      timebuf[1].tv_sec =
          (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME) ? (time_t) attrs.
           mtime.seconds : buffstat.st_mtime);
      timebuf[1].tv_usec = 0;

      TakeTokenFSCall();
      rc = futimes(fd, timebuf);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)
        {
          close(fd);
          Return(posix2fsal_error(errno), errno, INDEX_FSAL_setattrs);
        }
    }

  /* Optionaly fills output attributes. */

  if(p_object_attributes)
    {
      status = VFSFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  close(fd);
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}
