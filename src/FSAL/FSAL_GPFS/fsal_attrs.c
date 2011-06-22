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
#include <sys/time.h>

extern fsal_status_t posixstat64_2_fsal_attributes(struct stat64 *p_buffstat,
                                                   fsal_attrib_list_t * p_fsalattr_out);

extern fsal_status_t gpfsfsal_xstat_2_fsal_attributes(gpfsfsal_xstat_t *p_buffxstat,
                                                      fsal_attrib_list_t *p_fsalattr_out);

/**
 * GPFSFSAL_getattrs:
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
fsal_status_t GPFSFSAL_getattrs(gpfsfsal_handle_t * p_filehandle,       /* IN */
                            gpfsfsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_object_attributes    /* IN/OUT */
    )
{
  fsal_status_t st;
  gpfsfsal_xstat_t buffxstat;

  /* sanity checks.
   * note : object_attributes is mandatory in GPFSFSAL_getattrs.
   */
  if(!p_filehandle || !p_context || !p_object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  TakeTokenFSCall();
  st = fsal_get_xstat_by_handle(p_context,
                           p_filehandle,
                                &buffxstat);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(st))
    ReturnStatus(st, INDEX_FSAL_getattrs);

  /* convert attributes */
  st = gpfsfsal_xstat_2_fsal_attributes(&buffxstat, p_object_attributes);
  if(FSAL_IS_ERROR(st))
    {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
      ReturnStatus(st, INDEX_FSAL_getattrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);

}

/**
 * GPFSFSAL_getattrs_descriptor:
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
fsal_status_t GPFSFSAL_getattrs_descriptor(gpfsfsal_file_t * p_file_descriptor,     /* IN */
                                           gpfsfsal_handle_t * p_filehandle,        /* IN */
                                           gpfsfsal_op_context_t * p_context,       /* IN */
                                           fsal_attrib_list_t * p_object_attributes /* IN/OUT */
    )
{
  return GPFSFSAL_getattrs(p_filehandle, p_context, p_object_attributes);
}

/**
 * GPFSFSAL_setattrs:
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
fsal_status_t GPFSFSAL_setattrs(gpfsfsal_handle_t * p_filehandle,       /* IN */
                            gpfsfsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_attrib_set,  /* IN */
                            fsal_attrib_list_t * p_object_attributes    /* [ IN/OUT ] */
    )
{
  unsigned int i;
  fsal_status_t status;
  fsal_attrib_list_t attrs;

  gpfsfsal_xstat_t buffxstat;

  /* Stat buffer that will include values to be set. */
  struct stat64 newbuffstat;

  /* Indicate if stat or acl or both should be changed. */
  int attr_valid = 0;

  /* Indiate which attribute in stat should be changed. */
  int attr_changed = 0;

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

  /* get current attributes */
  TakeTokenFSCall();
  status = fsal_get_xstat_by_handle(p_context,
                                    p_filehandle,
                                    &buffxstat);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(status))
    {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
      ReturnStatus(status, INDEX_FSAL_setattrs);
    }

  /* Stat buffer that will include values to be set. */
  newbuffstat = buffxstat.buffstat;

  /***********
   *  CHMOD  *
   ***********/
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {

      /* The POSIX chmod call don't affect the symlink object, but
       * the entry it points to. So we must ignore it.
       */
      if(!S_ISLNK(buffxstat.buffstat.st_mode))
        {

          /* For modifying mode, user must be root or the owner */
          if((p_context->credential.user != 0)
             && (p_context->credential.user != buffxstat.buffstat.st_uid))
            {
              LogFullDebug(COMPONENT_FSAL,
                           "Permission denied for CHMOD opeartion: current owner=%d, credential=%d",
                           buffxstat.buffstat.st_uid, p_context->credential.user);
              Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
            }

            attr_valid |= XATTR_STAT;
            attr_changed |= XATTR_MODE;
            newbuffstat.st_mode = fsal2unix_mode(attrs.mode);
            LogDebug(COMPONENT_FSAL,
                     "current mode = %o, new mode = %o",
                     buffxstat.buffstat.st_mode, newbuffstat.st_mode);

        }

    }

  /***********
   *  CHOWN  *
   ***********/
  /* Only root can change uid and A normal user must be in the group he wants to set */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER))
    {

      /* For modifying owner, user must be root or current owner==wanted==client */
      if((p_context->credential.user != 0) &&
         ((p_context->credential.user != buffxstat.buffstat.st_uid) ||
          (p_context->credential.user != attrs.owner)))
        {
          LogFullDebug(COMPONENT_FSAL,
                       "Permission denied for CHOWN opeartion: current owner=%d, credential=%d, new owner=%d",
                       buffxstat.buffstat.st_uid, p_context->credential.user, attrs.owner);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_GROUP))
    {

      /* For modifying group, user must be root or current owner */
      if((p_context->credential.user != 0)
         && (p_context->credential.user != buffxstat.buffstat.st_uid))
        {
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }

      int in_grp = 0;
      /* set in_grp */
      if(p_context->credential.group == attrs.group)
        in_grp = 1;
      else
        for(i = 0; i < p_context->credential.nbgroups; i++)
          {
            if((in_grp = (attrs.group == p_context->credential.alt_groups[i])))
              break;
          }

      /* it must also be in target group */
      if(p_context->credential.user != 0 && !in_grp)
        {
          LogFullDebug(COMPONENT_FSAL,
                       "Permission denied for CHOWN operation: current group=%d, credential=%d, new group=%d",
                       buffxstat.buffstat.st_gid, p_context->credential.group, attrs.group);
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

      attr_valid |= XATTR_STAT;
      attr_changed |= FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER) ? XATTR_UID : XATTR_GID;
      if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER))
        {
          newbuffstat.st_uid = (int)attrs.owner;
          LogDebug(COMPONENT_FSAL,
                   "current uid = %d, new uid = %d",
                   buffxstat.buffstat.st_uid, newbuffstat.st_uid);
        }
      else
        {
          newbuffstat.st_gid = (int)attrs.group;
          LogDebug(COMPONENT_FSAL,
                   "current gid = %d, new gid = %d",
                   buffxstat.buffstat.st_gid, newbuffstat.st_gid);
        }

    }

  /***********
   *  UTIME  *
   ***********/

  /* user must be the owner or have read access to modify 'atime' */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME)
     && (p_context->credential.user != 0)
     && (p_context->credential.user != buffxstat.buffstat.st_uid)
     && ((status = fsal_check_access_by_mode(p_context, FSAL_R_OK, &buffxstat.buffstat)).major
         != ERR_FSAL_NO_ERROR))
    {
      ReturnStatus(status, INDEX_FSAL_setattrs);
    }
  /* user must be the owner or have write access to modify 'mtime' */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME)
     && (p_context->credential.user != 0)
     && (p_context->credential.user != buffxstat.buffstat.st_uid)
     && ((status = fsal_check_access_by_mode(p_context, FSAL_W_OK, &buffxstat.buffstat)).major
         != ERR_FSAL_NO_ERROR))
    {
      ReturnStatus(status, INDEX_FSAL_setattrs);
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME | FSAL_ATTR_MTIME))
    {
      attr_valid |= XATTR_STAT;
      attr_changed |= FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME) ? XATTR_ATIME_SET : XATTR_MTIME_SET;
      if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME))
        {
          newbuffstat.st_atime = (time_t) attrs.atime.seconds;
          LogDebug(COMPONENT_FSAL,
                   "current atime = %lu, new atime = %lu",
                   (unsigned long)buffxstat.buffstat.st_atime, (unsigned long)newbuffstat.st_atime);
        }
      else
        {
          newbuffstat.st_mtime = (time_t) attrs.mtime.seconds;
          LogDebug(COMPONENT_FSAL,
                   "current mtime = %lu, new mtime = %lu",
                   (unsigned long)buffxstat.buffstat.st_mtime, (unsigned long)newbuffstat.st_mtime);
        }
    }

  /* If there is any change in stat, send it down to file system. */
  if(attr_valid == XATTR_STAT && attr_changed !=0)
    {
      /* Copy stat to be set. */
      buffxstat.buffstat = newbuffstat;

      status = fsal_set_xstat_by_handle(p_context,
                                        p_filehandle,
                                        attr_valid,
                                        attr_changed,
                                        &buffxstat);

      if(FSAL_IS_ERROR(status))
        ReturnStatus(status, INDEX_FSAL_setattrs);
    }

  /* Optionaly fills output attributes. */

  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

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
fsal_status_t GPFSFSAL_getextattrs(gpfsfsal_handle_t * p_filehandle, /* IN */
                                   gpfsfsal_op_context_t * p_context,        /* IN */
                                   fsal_extattrib_list_t * p_object_attributes /* OUT */
    )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_getextattrs);
} /* GPFSFSAL_getextattrs */
