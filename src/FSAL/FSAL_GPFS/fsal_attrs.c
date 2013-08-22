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
#include "FSAL/access_check.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>

extern fsal_status_t gpfsfsal_xstat_2_fsal_attributes(gpfsfsal_xstat_t *p_buffxstat,
                                                      fsal_attrib_list_t *p_fsalattr_out);

#ifdef _USE_NFS4_ACL
extern fsal_status_t fsal_acl_2_gpfs_acl(fsal_acl_t *p_fsalacl, gpfsfsal_xstat_t *p_buffxstat);
#endif                          /* _USE_NFS4_ACL */

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
fsal_status_t GPFSFSAL_getattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
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
  if(buffxstat.attr_valid & XATTR_NO_CACHE)
  {

      LogDebug(COMPONENT_FSAL,
                   "fsal_get_xstat_by_handle returned XATTR_NO_CACHE");
      Return(ERR_FSAL_NO_ERROR, ERR_FSAL_STALE, INDEX_FSAL_getattrs);
  } else {
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);
  }
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
fsal_status_t GPFSFSAL_getattrs_descriptor(fsal_file_t * p_file_descriptor,     /* IN */
                                           fsal_handle_t * p_filehandle,        /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
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
fsal_status_t GPFSFSAL_setattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_attrib_set,  /* IN */
                            fsal_attrib_list_t * p_object_attributes    /* [ IN/OUT ] */
    )
{
  fsal_status_t status;

  /* Buffer that will be passed to gpfs_ganesha API. */
  gpfsfsal_xstat_t buffxstat;

  /* Indicate if stat or acl or both should be changed. */
  int attr_valid = 0;

  /* Indiate which attribute in stat should be changed. */
  int attr_changed = 0;

  fsal_attrib_list_t current_attrs;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context || !p_attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* First, check that FSAL attributes changes are allowed. */

  /* Is it allowed to change times ? */

  if(!global_fs_info.cansettime)
    {
      if(p_attrib_set->asked_attributes
         & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME |
            FSAL_ATTR_ATIME_SERVER | FSAL_ATTR_MTIME_SERVER))
        {
          /* handled as an unsettable attribute. */
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
        }
    }

  if(isDebug(COMPONENT_FSAL))
    {
      /* get current attributes for debug */
      current_attrs.asked_attributes = GPFS_SUPPORTED_ATTRIBUTES;
      status = GPFSFSAL_getattrs(p_filehandle, p_context, &current_attrs);
      if(FSAL_IS_ERROR(status))
        {
          /* Make sure attributes have valid values but don't pass up error */
          LogDebug(COMPONENT_FSAL,
                   "GPFSFSAL_getattrs failed to get current attr");
          memset(&current_attrs, 0, sizeof(current_attrs));
        }
    }

  /**************
   *  TRUNCATE  *
   **************/

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_SIZE))
    {
      attr_changed |= XATTR_SIZE;
    
      /* Fill wanted mode. */
      buffxstat.buffstat.st_size = p_attrib_set->filesize;
      LogDebug(COMPONENT_FSAL,
               "current size = %llu, new size = %llu",
               (unsigned long long) current_attrs.filesize,
               (unsigned long long) buffxstat.buffstat.st_size);
    }

  /***********
   *  CHMOD  *
   ***********/

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_MODE))
    {
      attr_changed |= XATTR_MODE;
    
      /* Fill wanted mode, apply umask, if mode attribute is to be changed */
      buffxstat.buffstat.st_mode = fsal2unix_mode(p_attrib_set->mode &
                                   (~global_fs_info.umask));
      LogDebug(COMPONENT_FSAL,
               "current mode = %o, new mode = %o",
               fsal2unix_mode(current_attrs.mode), buffxstat.buffstat.st_mode);
    }

  /***********
   *  CHOWN  *
   ***********/

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_OWNER))
    {
        attr_changed |= XATTR_UID;

        buffxstat.buffstat.st_uid = (int)p_attrib_set->owner;
        LogDebug(COMPONENT_FSAL,
                 "current uid = %d, new uid = %d",
                 current_attrs.owner, buffxstat.buffstat.st_uid);
    }

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_GROUP))
    {
      attr_changed |= XATTR_GID;

      buffxstat.buffstat.st_gid = (int)p_attrib_set->group;
      LogDebug(COMPONENT_FSAL,
               "current gid = %d, new gid = %d",
               current_attrs.group, buffxstat.buffstat.st_gid);
    }

  /***********
   *  UTIME  *
   ***********/

  /* Fill wanted atime. */
  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_ATIME))
    {
      attr_changed |= XATTR_ATIME;
      buffxstat.buffstat.st_atime        = p_attrib_set->atime.seconds;
      buffxstat.buffstat.st_atim.tv_nsec = p_attrib_set->atime.nseconds;
      LogDebug(COMPONENT_FSAL,
               "current atime = %lu, new atime = %lu",
               (unsigned long)current_attrs.atime.seconds,
               (unsigned long)buffxstat.buffstat.st_atime);
    }

  /* Fill wanted mtime. */
  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_MTIME))
    {
      attr_changed |= XATTR_MTIME;
      buffxstat.buffstat.st_mtime        = p_attrib_set->mtime.seconds;
      buffxstat.buffstat.st_mtim.tv_nsec = p_attrib_set->mtime.nseconds;
      LogDebug(COMPONENT_FSAL,
               "current mtime = %lu, new mtime = %lu",
               (unsigned long)current_attrs.mtime.seconds,
               (unsigned long)buffxstat.buffstat.st_mtime);
    }

  /* Asking to set atime to NOW */
  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_ATIME_SERVER))
    {
      attr_changed |= XATTR_ATIME_NOW;
      LogDebug(COMPONENT_FSAL,
               "current atime = %lu, new atime = NOW",
               (unsigned long)current_attrs.atime.seconds);
    }

  /* Asking to set mtime to NOW */
  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_MTIME_SERVER))
    {
      attr_changed |= XATTR_MTIME_NOW;
      LogDebug(COMPONENT_FSAL,
               "current mtime = %lu, new mtime = NOW",
               (unsigned long)current_attrs.mtime.seconds);
    }

  /* If any stat changed, indicate that */
  if(attr_changed !=0)
    attr_valid |= XATTR_STAT;

#ifdef _USE_NFS4_ACL
   /***********
   *  ACL  *
   ***********/

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_ACL)) 
   {
      if(p_attrib_set->acl)
        {
          attr_valid |= XATTR_ACL;
          LogDebug(COMPONENT_FSAL, "setattr acl = %p", p_attrib_set->acl);

          /* Convert FSAL ACL to GPFS NFS4 ACL and fill the buffer. */
          status = fsal_acl_2_gpfs_acl(p_attrib_set->acl, &buffxstat);

          if(FSAL_IS_ERROR(status))
            ReturnStatus(status, INDEX_FSAL_setattrs);
        }
      else
        {
          LogCrit(COMPONENT_FSAL, "setattr acl is NULL");
          Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);
        }
    }
#endif                          /* _USE_NFS4_ACL */

  /* If there is any change in stat or acl or both, send it down to file system. */
  if(attr_valid != 0)
    {
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
