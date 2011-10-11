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
#include "fsal_convert.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <inttypes.h>

/**
 * FSAL_getattrs:
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
fsal_status_t XFSFSAL_getattrs(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_object_attributes /* IN/OUT */
    )
{
  int rc, errsv;
  fsal_status_t st;
  int fd;
  struct stat buffstat;

  /* sanity checks.
   * note : object_attributes is mandatory in FSAL_getattrs.
   */
  if(!p_filehandle || !p_context || !p_object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  TakeTokenFSCall();
  st = fsal_internal_handle2fd(p_context, p_filehandle, &fd, O_RDONLY);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(st))
    ReturnStatus(st, INDEX_FSAL_getattrs);

  /* get file metadata */
  TakeTokenFSCall();
  rc = fstat(fd, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  close(fd);

  if(rc != 0)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_getattrs);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_getattrs);
    }

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
 * FSAL_setattrs:
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
fsal_status_t XFSFSAL_setattrs(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_attrib_set,       /* IN */
                               fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  unsigned int i;
  fsal_status_t status;
  fsal_attrib_list_t attrs;

  int fd;
  struct stat buffstat;
  uid_t userid = ((xfsfsal_op_context_t *)p_context)->credential.user;
  gid_t groupid = ((xfsfsal_op_context_t *)p_context)->credential.group;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context || !p_attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* local copy of attributes */
  attrs = *p_attrib_set;

  /* It does not make sense to setattr on a symlink */
  if(((xfsfsal_handle_t *)p_filehandle)->data.type == DT_LNK)
    return fsal_internal_setattrs_symlink(p_filehandle, p_context, p_attrib_set,
                                          p_object_attributes);

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
  status = fsal_internal_handle2fd(p_context, p_filehandle, &fd, O_RDWR);
  ReleaseTokenFSCall();
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_setattrs);

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
          if((userid != 0)
             && (userid != buffstat.st_uid))
            {

              LogFullDebug(COMPONENT_FSAL, 
                                "Permission denied for CHMOD opeartion: current owner=%d, credential=%d",
                                buffstat.st_uid, userid);

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
      if((userid != 0) &&
         ((userid != buffstat.st_uid) ||
          (userid != attrs.owner)))
        {

          LogFullDebug(COMPONENT_FSAL,
                            "Permission denied for CHOWN opeartion: current owner=%d, credential=%d, new owner=%d",
                            buffstat.st_uid, userid, attrs.owner);

          close(fd);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_GROUP))
    {

      /* For modifying group, user must be root or current owner */
      if((userid != 0)
         && (userid != buffstat.st_uid))
        {
          close(fd);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }

      int in_grp = 0;
      /* set in_grp */
      if(groupid == attrs.group)
        in_grp = 1;
      else
        for(i = 0; i < ((xfsfsal_op_context_t *)p_context)->credential.nbgroups; i++)
          {
            if((in_grp = (attrs.group == ((xfsfsal_op_context_t *)p_context)->credential.alt_groups[i])))
              break;
          }

      /* it must also be in target group */
      if(userid != 0 && !in_grp)
        {

          LogFullDebug(COMPONENT_FSAL,
                            "Permission denied for CHOWN operation: current group=%d, credential=%d, new group=%d",
                            buffstat.st_gid, groupid, attrs.group);

          close(fd);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {

      LogFullDebug(COMPONENT_FSAL, "Performing chown(inode=%"PRIu64", %d,%d)",
                        buffstat.st_ino, FSAL_TEST_MASK(attrs.asked_attributes,
                                                      FSAL_ATTR_OWNER) ? (int)attrs.owner
                        : -1, FSAL_TEST_MASK(attrs.asked_attributes,
                                             FSAL_ATTR_GROUP) ? (int)attrs.group : -1);


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
     && (userid != 0)
     && (userid != buffstat.st_uid)
     && ((status = fsal_internal_testAccess(p_context, FSAL_R_OK, &buffstat, NULL)).major
         != ERR_FSAL_NO_ERROR))
    {
      close(fd);
      ReturnStatus(status, INDEX_FSAL_setattrs);
    }
  /* user must be the owner or have write access to modify 'mtime' */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME)
     && (userid != 0)
     && (userid != buffstat.st_uid)
     && ((status = fsal_internal_testAccess(p_context, FSAL_W_OK, &buffstat, NULL)).major
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
      status = XFSFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          close(fd);
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  close(fd);
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}

/**
 * FSAL_getetxattrs:
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
fsal_status_t XFSFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_extattrib_list_t * p_object_attributes /* OUT */
    )
{
  fsal_status_t st ;
  xfs_bstat_t bstat;
  xfs_ino_t xfs_ino;
  int fd = 0 ;

  /* sanity checks.
   * note : object_attributes is mandatory in FSAL_getattrs.
   */
  if(!p_filehandle || !p_context || !p_object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  TakeTokenFSCall();
  st = fsal_internal_handle2fd(p_context, p_filehandle, &fd, O_RDONLY);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(st))
    ReturnStatus(st, INDEX_FSAL_getextattrs);

  if( p_object_attributes->asked_attributes & FSAL_ATTR_GENERATION )
   {
     /* get file metadata */
     xfs_ino = ((xfsfsal_handle_t *)p_filehandle)->data.inode ;
     TakeTokenFSCall();
     if(fsal_internal_get_bulkstat_by_inode(fd, &xfs_ino, &bstat) < 0)
      {
        close(fd);
        ReleaseTokenFSCall();
        ReturnCode(posix2fsal_error(errno), errno);
      }
     ReleaseTokenFSCall();

     p_object_attributes->generation = bstat.bs_gen ;
    }
 
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getextattrs);
} /* XFSFSAL_getextattrs */
